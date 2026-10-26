#include "firmguard.h"
#include <limits.h>
#include <string.h>

_Static_assert(CHAR_BIT == 8, "FGMF requires eight-bit bytes");
typedef struct { const uint8_t *p; size_t left; } cursor;
typedef struct { uint16_t type, flags; fg_span value; } record;

static uint16_t u16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8));
}
static uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t u64(const uint8_t *p) {
    return (uint64_t)u32(p) | ((uint64_t)u32(p + 4) << 32);
}
static int take(cursor *c, size_t n, const uint8_t **p) {
    if (n > c->left) return 0;
    *p = c->p;
    c->p += n;
    c->left -= n;
    return 1;
}
static fg_status next(cursor *c, size_t *budget, record *r) {
    const uint8_t *h, *v;
    if (!take(c, 8, &h)) return FG_TRUNCATED;
    r->type = u16(h); r->flags = u16(h + 2);
    if ((r->flags & (uint16_t)~1u) != 0) return FG_FORMAT;
    if (*budget == 0 || u32(h + 4) > FG_MAX_VALUE) return FG_LIMIT;
    --*budget;
    size_t n = u32(h + 4);
    if (!take(c, n, &v)) return FG_TRUNCATED;
    r->value = (fg_span){v, n};
    return FG_OK;
}
static int known(uint16_t type) {
    return type == 1 || (type >= 0x10 && type <= 0x14);
}
static int token_char(uint8_t c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
}
static int token(fg_span s) {
    if (s.size == 0 || s.size > FG_MAX_DEVICE) return 0;
    for (size_t i = 0; i < s.size; ++i) if (!token_char(s.data[i])) return 0;
    return 1;
}
static int path(fg_span s) {
    if (s.size == 0 || s.size > FG_MAX_PATH) return 0;
    size_t start = 0;
    for (size_t i = 0; i <= s.size; ++i) {
        if (i == s.size || s.data[i] == '/') {
            size_t n = i - start;
            if (n == 0 || (n == 1 && s.data[start] == '.') ||
                (n == 2 && s.data[start] == '.' && s.data[start + 1] == '.')) return 0;
            start = i + 1;
        } else if (!token_char(s.data[i])) return 0;
    }
    return 1;
}
static fg_status image(fg_span value, size_t *budget, fg_image *out) {
    cursor c = {value.data, value.size};
    unsigned fields = 0;
    while (c.left != 0) {
        record r;
        fg_status status = next(&c, budget, &r);
        if (status != FG_OK) return status;
        if (r.type < 0x11 || r.type > 0x14) {
            if (known(r.type) || (r.flags & 1u) != 0) return FG_FORMAT;
            continue;
        }
        unsigned bit = 1u << (unsigned)(r.type - 0x11);
        if (r.flags != 0 || (fields & bit) != 0) return FG_FORMAT;
        fields |= bit;
        switch (r.type) {
        case 0x11:
            if (!path(r.value)) return FG_SEMANTIC;
            out->path = r.value; break;
        case 0x12:
            if (r.value.size != 16) return FG_FORMAT;
            out->offset = u64(r.value.data); out->size = u64(r.value.data + 8); break;
        case 0x13:
            if (r.value.size != 32) return FG_FORMAT;
            memcpy(out->sha256, r.value.data, 32); break;
        case 0x14:
            if (r.value.size != 8) return FG_FORMAT;
            out->version = u64(r.value.data);
            if (out->version == 0) return FG_SEMANTIC;
            break;
        default: return FG_FORMAT;
        }
    }
    return fields == 15u ? FG_OK : FG_FORMAT;
}

fg_status fg_parse(const uint8_t *data, size_t size, fg_manifest *out) {
    if (out == NULL) return FG_ARGUMENT;
    memset(out, 0, sizeof(*out));
    if (data == NULL && size != 0) return FG_ARGUMENT;
    if (size > FG_MAX_INPUT) return FG_SIZE;
    if (size < 32) return FG_TRUNCATED;
    if (memcmp(data, "FGMF", 4) != 0 || u16(data + 4) != 1 ||
        u16(data + 6) != 0 || u32(data + 8) != size ||
        u64(data + 12) == 0 || u64(data + 20) == 0 || u16(data + 30) != 0) return FG_HEADER;
    size_t count = u16(data + 28);
    if (count == 0 || count > FG_MAX_IMAGES) return FG_LIMIT;
    fg_manifest m = {0};
    m.payload_size = u64(data + 12); m.sequence = u64(data + 20);
    cursor c = {data + 32, size - 32};
    size_t budget = FG_MAX_RECORDS;
    int device_seen = 0;
    uint64_t previous_end = 0;
    while (c.left != 0) {
        record r;
        fg_status status = next(&c, &budget, &r);
        if (status != FG_OK) return status;
        if (r.type == 1) {
            if (r.flags != 0 || device_seen) return FG_FORMAT;
            if (!token(r.value)) return FG_SEMANTIC;
            m.device = r.value; device_seen = 1;
        } else if (r.type == 0x10) {
            if (r.flags != 0) return FG_FORMAT;
            if (m.image_count >= count) return FG_FORMAT;
            fg_image *im = &m.images[m.image_count];
            status = image(r.value, &budget, im);
            if (status != FG_OK) return status;
            if (im->size == 0 || im->offset > m.payload_size ||
                im->size > m.payload_size - im->offset || im->offset < previous_end) return FG_SEMANTIC;
            for (size_t i = 0; i < m.image_count; ++i) {
                if (m.images[i].path.size == im->path.size &&
                    memcmp(m.images[i].path.data, im->path.data, im->path.size) == 0) return FG_SEMANTIC;
            }
            previous_end = im->offset + im->size;
            ++m.image_count;
        } else if (known(r.type) || (r.flags & 1u) != 0) return FG_FORMAT;
    }
    if (!device_seen || m.image_count != count) return FG_FORMAT;
    *out = m;
    return FG_OK;
}

const char *fg_status_name(fg_status status) {
    switch (status) {
    case FG_OK: return "ok"; case FG_ARGUMENT: return "argument";
    case FG_SIZE: return "size"; case FG_TRUNCATED: return "truncated";
    case FG_HEADER: return "header"; case FG_FORMAT: return "format";
    case FG_LIMIT: return "limit"; case FG_SEMANTIC: return "semantic";
    default: return "unknown";
    }
}

typedef struct { uint8_t *p; size_t left, used; } writer;
static int put(writer *w, const uint8_t *p, size_t n) {
    if (n > w->left) return 0;
    memcpy(w->p, p, n); w->p += n; w->left -= n; w->used += n;
    return 1;
}
static int number(writer *w, uint64_t n, size_t bytes) {
    uint8_t encoded[8];
    for (size_t i = 0; i < bytes; ++i) encoded[i] = (uint8_t)(n >> (8 * i));
    return put(w, encoded, bytes);
}
fg_status fg_canonical(const fg_manifest *m, uint8_t *buffer, size_t capacity, size_t *written) {
    if (written == NULL) return FG_ARGUMENT;
    *written = 0;
    if (m == NULL || buffer == NULL || m->device.data == NULL || m->device.size > FG_MAX_DEVICE ||
        m->image_count > FG_MAX_IMAGES) return FG_ARGUMENT;
    writer w = {buffer, capacity, 0};
    if (!number(&w, m->device.size, 2) || !put(&w, m->device.data, m->device.size) ||
        !number(&w, m->payload_size, 8) || !number(&w, m->sequence, 8) ||
        !number(&w, m->image_count, 2)) return FG_SIZE;
    for (size_t i = 0; i < m->image_count; ++i) {
        const fg_image *im = &m->images[i];
        if (im->path.data == NULL || im->path.size > FG_MAX_PATH) return FG_ARGUMENT;
        if (!number(&w, im->path.size, 2) || !put(&w, im->path.data, im->path.size) ||
            !number(&w, im->offset, 8) || !number(&w, im->size, 8) ||
            !number(&w, im->version, 8) || !put(&w, im->sha256, 32)) return FG_SIZE;
    }
    *written = w.used;
    return FG_OK;
}
fg_status fg_parse_canonical(const uint8_t *data, size_t size, uint8_t *buffer, size_t capacity, size_t *written) {
    if (written == NULL) return FG_ARGUMENT;
    *written = 0;
    fg_manifest m;
    fg_status status = fg_parse(data, size, &m);
    if (status != FG_OK) return status;
    return fg_canonical(&m, buffer, capacity, written);
}
