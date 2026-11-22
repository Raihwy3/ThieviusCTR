#ifndef CANE_UPDATE_CORE_H
#define CANE_UPDATE_CORE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CANE_UPDATE_API "https://api.github.com/repos/Raihwy3/ThieviusCTR/releases/latest"
#define CANE_UPDATE_MAX_ZIP (24u * 1024u * 1024u)
#define CANE_UPDATE_MAX_3DSX (32u * 1024u * 1024u)

struct CaneUpdateRelease {
    char tag[64];
    char url[768];
    char digest[65];
    size_t bytes;
};

int cane_update_newer(const char *remote, const char *installed);
int cane_update_release(const char *json, size_t length, const char *installed, struct CaneUpdateRelease *out);
/* A nonzero return means the installed pair is usable, or no recovery was needed. */
int cane_update_recover(const char *directory, char *error, size_t capacity);
/* Stage both files, check ZIP CRCs and headers, then replace with a rollback journal. */
int cane_update_install(const char *zip, const char *directory, int (*progress)(size_t, size_t), char *error, size_t capacity);

#ifdef __cplusplus
}
#endif

#endif
