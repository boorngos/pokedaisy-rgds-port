// Pull the first GBA/GBC/GB image out of a .zip (stored or deflate) and write
// it to destPath. The library list shows the archive; the emulator sees a
// plain ROM. False when the zip has no game or the inflate fails.
#ifndef PD_ZIP_H
#define PD_ZIP_H

#include <stdbool.h>

bool pd_zip_extract_rom(const char* zipPath, const char* destPath);

#endif
