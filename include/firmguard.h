#ifndef FIRMGUARD_H
#define FIRMGUARD_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FG_MAX_INPUT 65536u
#define FG_MAX_IMAGES 64u
#define FG_MAX_RECORDS 512u
#define FG_MAX_VALUE 4096u
#define FG_MAX_PATH 128u
#define FG_MAX_DEVICE 64u
#define FG_MAX_CANONICAL 12288u

typedef enum {
    FG_OK = 0, FG_ARGUMENT = 1, FG_SIZE = 2, FG_TRUNCATED = 3,
    FG_HEADER = 4, FG_FORMAT = 5, FG_LIMIT = 6, FG_SEMANTIC = 7
} fg_status;

typedef struct { const uint8_t *data; size_t size; } fg_span;
typedef struct {
    fg_span path;
    uint64_t offset, size, version;
    uint8_t sha256[32];
} fg_image;
typedef struct {
    fg_span device;
    uint64_t payload_size, sequence;
    size_t image_count;
    fg_image images[FG_MAX_IMAGES];
} fg_manifest;

/* Reentrant, bounded, no allocation or I/O. Input and output must not overlap.
 * data must address size immutable readable bytes (NULL is allowed only for 0).
 * out must address a writable fg_manifest. Output is zeroed on every rejection.
 * Successful spans borrow input; keep input alive and immutable until done.
 * Do not trust a successful parse as proof of authenticity or payload integrity.
 */
fg_status fg_parse(const uint8_t *data, size_t size, fg_manifest *out);
const char *fg_status_name(fg_status status);

/* Only pass an unmodified successful fg_parse result. Output must not alias the
 * input/result. On error *written is 0 and buffer bytes are unspecified.
 */
fg_status fg_canonical(const fg_manifest *manifest, uint8_t *buffer,
                       size_t capacity, size_t *written);
/* Convenience comparison API: same parse status, complete canonical bytes. */
fg_status fg_parse_canonical(const uint8_t *data, size_t size, uint8_t *buffer,
                             size_t capacity, size_t *written);

#ifdef __cplusplus
}
#endif
#endif
