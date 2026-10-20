#include "pd_zip.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "tinf.h"

#define ZIP_MAX (40u * 1024u * 1024u)

static uint16_t u16(const unsigned char* p) {
    return (uint16_t) (p[0] | (p[1] << 8));
}

static uint32_t u32(const unsigned char* p) {
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
}

static int rom_name(const char* name, size_t n) {
    if (n >= 4 && !strncasecmp(name + n - 4, ".gba", 4)) return 2;
    if (n >= 4 && !strncasecmp(name + n - 4, ".agb", 4)) return 2;
    if (n >= 4 && !strncasecmp(name + n - 4, ".gbc", 4)) return 1;
    if (n >= 3 && !strncasecmp(name + n - 3, ".gb", 3)) return 1;
    return 0;
}

bool pd_zip_extract_rom(const char* zipPath, const char* destPath) {
    FILE* f = fopen(zipPath, "rb");
    if (!f) return false;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return false; }
    long fileLen = ftell(f);
    if (fileLen < 22 || fileLen > 64L * 1024L * 1024L) { fclose(f); return false; }
    unsigned char* zip = malloc((size_t) fileLen);
    if (!zip) { fclose(f); return false; }
    rewind(f);
    if (fread(zip, 1, (size_t) fileLen, f) != (size_t) fileLen) {
        free(zip);
        fclose(f);
        return false;
    }
    fclose(f);

    // End of central directory, comment and all, within the last 64K + 22.
    long scan = fileLen - 22;
    long stop = fileLen > 65557 ? fileLen - 65557 : 0;
    long eocd = -1;
    for (; scan >= stop; scan--) {
        if (u32(zip + scan) == 0x06054b50u) { eocd = scan; break; }
    }
    if (eocd < 0) { free(zip); return false; }
    uint16_t entries = u16(zip + eocd + 10);
    uint32_t cdOff = u32(zip + eocd + 16);
    if (cdOff >= (uint32_t) fileLen) { free(zip); return false; }

    uint32_t bestOff = 0, bestComp = 0, bestRaw = 0;
    int bestRank = 0;
    uint16_t bestMethod = 0;
    uint32_t p = cdOff;
    for (uint16_t i = 0; i < entries; i++) {
        if (p + 46 > (uint32_t) fileLen || u32(zip + p) != 0x02014b50u) break;
        uint16_t method = u16(zip + p + 10);
        uint32_t comp = u32(zip + p + 20);
        uint32_t raw = u32(zip + p + 24);
        uint16_t nameLen = u16(zip + p + 28);
        uint16_t extra = u16(zip + p + 30);
        uint16_t comment = u16(zip + p + 32);
        uint32_t local = u32(zip + p + 42);
        if (p + 46u + nameLen > (uint32_t) fileLen) break;
        int rank = rom_name((const char*) (zip + p + 46), nameLen);
        if (rank > bestRank && raw > 0 && raw <= ZIP_MAX && (method == 0 || method == 8)) {
            bestRank = rank;
            bestOff = local;
            bestComp = comp;
            bestRaw = raw;
            bestMethod = method;
        }
        p += 46u + nameLen + extra + comment;
    }
    if (!bestRank || bestOff + 30 > (uint32_t) fileLen || u32(zip + bestOff) != 0x04034b50u) {
        free(zip);
        return false;
    }
    uint16_t nameLen = u16(zip + bestOff + 26);
    uint16_t extra = u16(zip + bestOff + 28);
    uint32_t data = bestOff + 30u + nameLen + extra;
    if (data + bestComp > (uint32_t) fileLen) { free(zip); return false; }

    unsigned char* out = malloc(bestRaw);
    if (!out) { free(zip); return false; }
    int ok = 0;
    if (bestMethod == 0 && bestComp == bestRaw) {
        memcpy(out, zip + data, bestRaw);
        ok = 1;
    } else if (bestMethod == 8) {
        unsigned int destLen = bestRaw;
        ok = tinf_uncompress(out, &destLen, zip + data, bestComp) == TINF_OK && destLen == bestRaw;
    }
    free(zip);
    if (!ok) { free(out); return false; }

    FILE* w = fopen(destPath, "wb");
    if (!w) { free(out); return false; }
    ok = fwrite(out, 1, bestRaw, w) == bestRaw;
    ok = fclose(w) == 0 && ok;
    free(out);
    if (!ok) remove(destPath);
    return ok;
}
