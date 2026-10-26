#include "firmguard.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    fg_manifest manifest;
    fg_status status = fg_parse(data, size, &manifest);
    if (status != FG_OK) {
        const uint8_t zero[sizeof(manifest)] = {0};
        if (memcmp(&manifest, zero, sizeof(manifest)) != 0) __builtin_trap();
        return 0;
    }
    uint8_t canonical[FG_MAX_CANONICAL]; size_t written = 0;
    if (fg_canonical(&manifest, canonical, sizeof(canonical), &written) != FG_OK || written > sizeof(canonical)) __builtin_trap();
    return 0;
}
