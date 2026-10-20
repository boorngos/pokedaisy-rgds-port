#ifndef PD_SHA1_H
#define PD_SHA1_H

#include <stdbool.h>

// Lowercase hex, 40 chars + NUL. False if the file can't be read.
bool pd_sha1_file(const char* path, char out[41]);

#endif
