#include "firmguard.h"
#include <inttypes.h>
#include <time.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
#include "io.h"

static int read_manifest(const char *path, uint8_t *data, size_t *size) {
    if (!fg_read_input(path, data, size)) { fputs("input read failed\n", stderr); return 0; }
    return 1;
}
int main(int argc, char **argv) {
    int inspect = argc == 3 && strcmp(argv[1], "inspect") == 0;
    int bench = argc == 4 && strcmp(argv[1], "bench") == 0;
    if (!inspect && !bench) {
        fputs("usage: fg-inspect inspect FILE|- | bench FILE|- ITERATIONS\n", stderr); return 2;
    }
    uint8_t data[FG_MAX_INPUT + 1u]; size_t size = 0;
    if (!read_manifest(argv[2], data, &size)) return 2;
    if (bench) {
        char *end = NULL;
        uintmax_t iterations = strtoumax(argv[3], &end, 10);
        if (end == argv[3] || *end != '\0' || iterations == 0 || iterations > 100000000u) {
            fputs("iterations must be in 1..100000000\n", stderr); return 2;
        }
        struct timespec start, finish;
        if (timespec_get(&start, TIME_UTC) != TIME_UTC) return 2;
        uint64_t checksum = 0;
        for (uintmax_t i = 0; i < iterations; ++i) {
            fg_manifest current;
            fg_status result = fg_parse(data, size, &current);
            if (result != FG_OK) { fprintf(stderr, "benchmark input rejected: %s\n", fg_status_name(result)); return 1; }
            checksum += current.sequence + current.image_count;
        }
        if (timespec_get(&finish, TIME_UTC) != TIME_UTC) return 2;
        int64_t elapsed_ns = (int64_t)(finish.tv_sec - start.tv_sec) * INT64_C(1000000000) +
                             (int64_t)finish.tv_nsec - (int64_t)start.tv_nsec;
        if (elapsed_ns <= 0) { fputs("benchmark clock did not advance\n", stderr); return 2; }
        uint64_t ns = (uint64_t)elapsed_ns;
        printf("{\"iterations\":%" PRIuMAX ",\"threads\":1,\"ns\":%" PRIu64 ",\"checksum\":%" PRIu64 "}\n", iterations, ns, checksum);
        return ferror(stdout) ? 2 : 0;
    }
    fg_manifest m;
    fg_status status = fg_parse(data, size, &m);
    if (status != FG_OK) { printf("{\"status\":\"%s\",\"code\":%d}\n", fg_status_name(status), (int)status); return 1; }
    printf("{\"status\":\"ok\",\"device\":\"%.*s\",\"payload_size\":%" PRIu64 ",\"sequence\":%" PRIu64 ",\"images\":[", (int)m.device.size, (const char *)m.device.data, m.payload_size, m.sequence);
    for (size_t i = 0; i < m.image_count; ++i) {
        const fg_image *im = &m.images[i];
        if (i != 0) putchar(',');
        printf("{\"path\":\"%.*s\",\"offset\":%" PRIu64 ",\"size\":%" PRIu64 ",\"version\":%" PRIu64 ",\"sha256\":\"", (int)im->path.size, (const char *)im->path.data, im->offset, im->size, im->version);
        for (size_t j = 0; j < 32; ++j) printf("%02x", (unsigned)im->sha256[j]);
        fputs("\"}", stdout);
    }
    puts("]}");
    return ferror(stdout) ? 2 : 0;
}
