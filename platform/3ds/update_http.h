#ifndef CANE_UPDATE_HTTP_H
#define CANE_UPDATE_HTTP_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct CaneUpdateTransfer {
    char *memory;
    size_t capacity;
    size_t length;
    const char *file;
    const char *digest;
    size_t expected;
    int (*cancel)(void *);
    void (*progress)(void *, size_t, size_t);
    void *user;
};

int cane_update_http(const char *url, struct CaneUpdateTransfer *transfer,
                     const void *ca, size_t ca_size,
                     char *error, size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
