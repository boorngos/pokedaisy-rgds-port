// Signs in to RetroAchievements. The password is not stored; a token is.
#ifndef PD_RA_H
#define PD_RA_H

#include <stddef.h>
#include <stdbool.h>

// Fills token on success. err is a short line either way when it fails.
bool pd_ra_login(const char* user, const char* pass, char* token, size_t tokenLen, char* err, size_t errLen);

#endif
