#include "update_http.h"
#include "update_core.h"

#include <curl/curl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#ifdef CANE_UPDATE_HOST
#include <openssl/evp.h>
#else
#include <mbedtls/sha256.h>
#include <mbedtls/version.h>
#endif

struct Writer {
    struct CaneUpdateTransfer *t;
    FILE *file;
};

static size_t write_data(char *data, size_t size, size_t count, void *arg) {
    struct Writer *w = arg;
    struct CaneUpdateTransfer *t = w->t;
    if (size && count > SIZE_MAX / size) return 0;
    size_t bytes = size * count;
    size_t limit = w->file ? t->expected : t->capacity - 1u;
    if ((t->cancel && t->cancel(t->user)) || t->length > limit || bytes > limit - t->length) {
        return 0;
    }
    if (w->file) {
        if (fwrite(data, 1, bytes, w->file) != bytes) return 0;
    } else {
        memcpy(t->memory + t->length, data, bytes);
    }
    t->length += bytes;
    if (t->progress) t->progress(t->user, t->length, w->file ? t->expected : 0u);
    return bytes;
}

static int xfer_progress(void *arg, curl_off_t total, curl_off_t now,
                         curl_off_t upload_total, curl_off_t upload_now) {
    (void)total;
    (void)now;
    (void)upload_total;
    (void)upload_now;
    struct CaneUpdateTransfer *t = arg;
    return t->cancel && t->cancel(t->user);
}

static int verify_file_sha256(const char *name, const char *expected) {
    unsigned char result[32] = { 0 };
    unsigned char buffer[8192];
    int ok = 1;
    size_t bytes;
    FILE *f = fopen(name, "rb");
    if (!f) return 0;

#ifdef CANE_UPDATE_HOST
    EVP_MD_CTX *hash = EVP_MD_CTX_new();
    if (!hash) {
        fclose(f);
        return 0;
    }
    ok = EVP_DigestInit_ex(hash, EVP_sha256(), NULL) == 1;
    while (ok && (bytes = fread(buffer, 1, sizeof buffer, f)) > 0u) {
        ok = EVP_DigestUpdate(hash, buffer, bytes) == 1;
    }
    unsigned length = 0;
    if (ok) ok = EVP_DigestFinal_ex(hash, result, &length) == 1 && length == 32u;
    EVP_MD_CTX_free(hash);
#else
    mbedtls_sha256_context hash;
    mbedtls_sha256_init(&hash);
#if defined(MBEDTLS_VERSION_MAJOR) && MBEDTLS_VERSION_MAJOR >= 3
    ok = mbedtls_sha256_starts(&hash, 0) == 0;
    while (ok && (bytes = fread(buffer, 1, sizeof buffer, f)) > 0u) {
        ok = mbedtls_sha256_update(&hash, buffer, bytes) == 0;
    }
    if (ok) ok = mbedtls_sha256_finish(&hash, result) == 0;
#else
    ok = mbedtls_sha256_starts_ret(&hash, 0) == 0;
    while (ok && (bytes = fread(buffer, 1, sizeof buffer, f)) > 0u) {
        ok = mbedtls_sha256_update_ret(&hash, buffer, bytes) == 0;
    }
    if (ok) ok = mbedtls_sha256_finish_ret(&hash, result) == 0;
#endif
    mbedtls_sha256_free(&hash);
#endif

    if (ferror(f)) ok = 0;
    fclose(f);
    char hex[65];
    for (unsigned i = 0; i < 32u; i++) {
        snprintf(hex + i * 2u, 3u, "%02x", result[i]);
    }
    return ok && expected && strcmp(hex, expected) == 0;
}

int cane_update_http(const char *url, struct CaneUpdateTransfer *t,
                     const void *ca, size_t ca_size,
                     char *error, size_t capacity) {
    int ok = 0;
    char detail[CURL_ERROR_SIZE] = { 0 };
    struct Writer writer = { t, NULL };
    CURL *curl = NULL;

    if (!url || !t || strncmp(url, "https://", 8) != 0 ||
        (!t->file && (!t->memory || t->capacity < 2u)) ||
        (t->file && (!t->digest || !t->expected || t->expected > CANE_UPDATE_MAX_ZIP))) {
        goto end;
    }
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) goto end;
    curl = curl_easy_init();
    if (!curl) goto cleanup;

    if (t->file) {
        writer.file = fopen(t->file, "wb");
        if (!writer.file) goto cleanup;
    }
    t->length = 0;

    struct curl_blob trust = { (void *)ca, ca_size, CURL_BLOB_NOCOPY };
#define SET_OPT(option, value) \
    do { \
        if (curl_easy_setopt(curl, option, value) != CURLE_OK) goto cleanup; \
    } while (0)

    SET_OPT(CURLOPT_URL, url);
    SET_OPT(CURLOPT_USERAGENT, "ThieviusCTR-Launcher/update-check");
    SET_OPT(CURLOPT_FOLLOWLOCATION, 1L);
    SET_OPT(CURLOPT_MAXREDIRS, 4L);
    SET_OPT(CURLOPT_PROTOCOLS_STR, "https");
    SET_OPT(CURLOPT_REDIR_PROTOCOLS_STR, "https");
    SET_OPT(CURLOPT_SSL_VERIFYPEER, 1L);
    SET_OPT(CURLOPT_SSL_VERIFYHOST, 2L);
    SET_OPT(CURLOPT_CAINFO_BLOB, &trust);
    SET_OPT(CURLOPT_CONNECTTIMEOUT_MS, 3000L);
    SET_OPT(CURLOPT_TIMEOUT_MS, t->file ? 120000L : 4000L);
    SET_OPT(CURLOPT_LOW_SPEED_LIMIT, 1024L);
    SET_OPT(CURLOPT_LOW_SPEED_TIME, 8L);
    SET_OPT(CURLOPT_NOSIGNAL, 1L);
    SET_OPT(CURLOPT_FAILONERROR, 1L);
    SET_OPT(CURLOPT_ERRORBUFFER, detail);
    SET_OPT(CURLOPT_WRITEFUNCTION, write_data);
    SET_OPT(CURLOPT_WRITEDATA, &writer);
    SET_OPT(CURLOPT_NOPROGRESS, 0L);
    SET_OPT(CURLOPT_XFERINFOFUNCTION, xfer_progress);
    SET_OPT(CURLOPT_XFERINFODATA, t);

    CURLcode result = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    if (writer.file) {
        if (fflush(writer.file) != 0 || fsync(fileno(writer.file)) != 0) result = CURLE_WRITE_ERROR;
        if (fclose(writer.file) != 0) result = CURLE_WRITE_ERROR;
        writer.file = NULL;
    }
    if (result == CURLE_OK && status == 200L && !(t->cancel && t->cancel(t->user))) {
        if (t->file) {
            ok = (t->length == t->expected) && verify_file_sha256(t->file, t->digest);
        } else {
            t->memory[t->length] = '\0';
            ok = 1;
        }
    }

cleanup:
    if (writer.file) fclose(writer.file);
    if (curl) curl_easy_cleanup(curl);
    curl_global_cleanup();

end:
    if (!ok) {
        if (t && t->file) remove(t->file);
        if (error && capacity) {
            snprintf(error, capacity, "%s",
                     detail[0] ? detail : "Download failed or did not match the release checksum.");
        }
    }
    return ok;
#undef SET_OPT
}
