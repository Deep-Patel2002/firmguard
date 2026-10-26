#define _POSIX_C_SOURCE 200809L
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "firmguard.h"

#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(FG_ALLOCATION_WRAP) && !defined(FG_SKIP_ALLOC_WRAP)
void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
static _Atomic int allocation_forbidden;
void *__wrap_malloc(size_t n) { assert(!atomic_load(&allocation_forbidden)); return __real_malloc(n); }
void *__wrap_calloc(size_t n, size_t s) { assert(!atomic_load(&allocation_forbidden)); return __real_calloc(n, s); }
void *__wrap_realloc(void *p, size_t n) { assert(!atomic_load(&allocation_forbidden)); return __real_realloc(p, n); }
#endif

enum { CAP = 16384 };
typedef struct { uint8_t b[CAP]; size_t n; } bytes;
static void put(bytes *b, const void *p, size_t n) { assert(n <= CAP - b->n); memcpy(b->b + b->n, p, n); b->n += n; }
static void le16(bytes *b, uint16_t x) { uint8_t v[] = {(uint8_t)x, (uint8_t)(x >> 8)}; put(b, v, 2); }
static void le32(bytes *b, uint32_t x) { uint8_t v[] = {(uint8_t)x, (uint8_t)(x >> 8), (uint8_t)(x >> 16), (uint8_t)(x >> 24)}; put(b, v, 4); }
static void le64(bytes *b, uint64_t x) { le32(b, (uint32_t)x); le32(b, (uint32_t)(x >> 32)); }
static void record(bytes *dst, uint16_t type, uint16_t flags, const void *data, size_t n) {
    assert(n <= UINT32_MAX); le16(dst, type); le16(dst, flags); le32(dst, (uint32_t)n); put(dst, data, n);
}
static bytes fixture(void) {
    bytes out = {{0}, 32}, device = {{0}, 0}, image = {{0}, 0}, range = {{0}, 0};
    memcpy(out.b, "FGMF", 4); out.b[4] = 1; out.b[12] = 64; out.b[20] = 1;
    le64(&range, 0); le64(&range, 32);
    record(&device, 1, 0, "device-1", 8);
    record(&image, 0x11, 0, "boot/image.bin", 14);
    record(&image, 0x12, 0, range.b, range.n);
    uint8_t digest[32] = {0}; record(&image, 0x13, 0, digest, sizeof(digest));
    bytes version = {{0}, 0}; le64(&version, 7); record(&image, 0x14, 0, version.b, version.n);
    record(&out, 1, 0, "device-1", 8);
    record(&out, 0x10, 0, image.b, image.n);
    out.b[8] = (uint8_t)out.n; out.b[9] = (uint8_t)(out.n >> 8);
    out.b[28] = 1;
    return out;
}
static void expect(fg_status wanted, const uint8_t *data, size_t size) {
    fg_manifest m; memset(&m, 0xA5, sizeof(m));
    fg_status got = fg_parse(data, size, &m);
    if (got != wanted) { fprintf(stderr, "expected %s, got %s\n", fg_status_name(wanted), fg_status_name(got)); abort(); }
    if (got != FG_OK) {
        const uint8_t zero[sizeof(m)] = {0};
        assert(memcmp(&m, zero, sizeof(m)) == 0);
    }
}
static void test_contract(void) {
    bytes b = fixture();
    uint8_t unaligned[CAP + 1]; memcpy(unaligned + 1, b.b, b.n);
    fg_manifest m; fg_status initial = fg_parse(unaligned + 1, b.n, &m);
    if (initial != FG_OK) fprintf(stderr, "base fixture parse failed: %s size=%zu\n", fg_status_name(initial), b.n);
    assert(initial == FG_OK);
    assert(m.image_count == 1 && m.device.size == 8 && m.images[0].offset == 0 && m.images[0].size == 32);
    expect(FG_ARGUMENT, NULL, 1);
    expect(FG_TRUNCATED, b.b, 31);
    expect(FG_HEADER, (uint8_t[32]){0}, 32);
    uint8_t oversized[FG_MAX_INPUT + 1u] = {0};
    expect(FG_SIZE, oversized, sizeof(oversized));
    uint8_t saved[CAP]; memcpy(saved, b.b, b.n);
#define MUTATE(byte, value, status) do { memcpy(b.b, saved, b.n); b.b[(byte)] = (uint8_t)(value); expect((status), b.b, b.n); } while (0)
    MUTATE(4, 2, FG_HEADER);                         /* unsupported version */
    MUTATE(6, 1, FG_HEADER);                         /* reserved header bits */
    MUTATE(8, b.n - 1, FG_HEADER);                   /* inconsistent length */
    MUTATE(28, 0, FG_LIMIT);                         /* no images */
    MUTATE(28, 65, FG_LIMIT);                        /* too many images */
    MUTATE(32 + 2, 1, FG_FORMAT);                    /* unknown header flags */
    memcpy(b.b, saved, b.n); b.b[36] = 0x01; b.b[37] = 0x10;
    expect(FG_LIMIT, b.b, b.n);                      /* bounded value length */
    MUTATE(32 + 8, '/', FG_SEMANTIC);                 /* invalid device token */
    MUTATE(b.n - 8, 0, FG_SEMANTIC);                 /* version zero */
    MUTATE(b.n - 8, 1, FG_OK);                       /* minimal valid version */
#undef MUTATE
    /* An offset/size pair that would overflow on addition must fail by subtraction. */
    memcpy(b.b, saved, b.n);
    memset(b.b + 12, 0xff, 8);                       /* payload spans uint64 */
    /* Record order is variable; locate the 16-byte range value by its type. */
    for (size_t i = 32; i + 8 <= b.n;) {
        uint16_t type = (uint16_t)(b.b[i] | ((uint16_t)b.b[i + 1] << 8));
        uint32_t len = (uint32_t)b.b[i + 4] | ((uint32_t)b.b[i + 5] << 8) |
                       ((uint32_t)b.b[i + 6] << 16) | ((uint32_t)b.b[i + 7] << 24);
        if (type == 0x10) { i += 8; continue; }
        if (type == 0x12 && len == 16) {
            memset(b.b + i + 8, 0xff, 8); memset(b.b + i + 16, 0xff, 8); break;
        }
        i += 8u + len;
    }
    expect(FG_SEMANTIC, b.b, b.n);
    /* C API success representation is complete and capacity bounded. */
    uint8_t canonical[FG_MAX_CANONICAL]; size_t written = 99;
    assert(fg_parse_canonical(saved, b.n, canonical, sizeof(canonical), &written) == FG_OK);
    assert(written != 0 && written < sizeof(canonical));
    written = 99;
    assert(fg_parse_canonical(saved, b.n, canonical, 1, &written) == FG_SIZE && written == 0);
    /* Allocation audit brackets only the parser call. */
#if defined(FG_ALLOCATION_WRAP) && !defined(FG_SKIP_ALLOC_WRAP)
    atomic_store(&allocation_forbidden, 1);
    assert(fg_parse(saved, ((size_t)saved[8] | ((size_t)saved[9] << 8)), &m) == FG_OK);
    atomic_store(&allocation_forbidden, 0);
#endif
}

typedef struct { const bytes *input; unsigned iterations; } work;
static void *worker(void *arg) {
    const work *w = arg;
    for (unsigned i = 0; i < w->iterations; ++i) {
        fg_manifest m;
        if (fg_parse(w->input->b, w->input->n, &m) != FG_OK || m.sequence != 1) return (void *)1;
    }
    return NULL;
}
static void test_reentrant(void) {
    bytes b = fixture();
    const work w = {&b, 10000}; pthread_t threads[8];
    for (size_t i = 0; i < 8; ++i) assert(pthread_create(&threads[i], NULL, worker, (void *)&w) == 0);
    for (size_t i = 0; i < 8; ++i) { void *result = NULL; assert(pthread_join(threads[i], &result) == 0); assert(result == NULL); }
}
int main(void) {
    test_contract(); test_reentrant(); puts("parser contract: passed; 80,000 concurrent parses"); return 0;
}
