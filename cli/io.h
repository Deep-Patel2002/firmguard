#ifndef FG_IO_H
#define FG_IO_H
#include "firmguard.h"
#include <stdio.h>
#include <string.h>
/* Read at most MAX_INPUT+1 bytes, including streams with no seek/stat support. */
static int fg_read_input(const char *path, uint8_t *data, size_t *size) {
    FILE *f = strcmp(path, "-") == 0 ? stdin : fopen(path, "rb");
    if (f == NULL) return 0;
#ifdef _WIN32
    if (f == stdin) { (void)_setmode(_fileno(stdin), _O_BINARY); }
#endif
    *size = fread(data, 1, FG_MAX_INPUT + 1u, f);
    int ok = !ferror(f);
    if (f != stdin && fclose(f) != 0) ok = 0;
    return ok;
}
#endif
