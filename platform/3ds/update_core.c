#include "update_core.h"

#include <minizip/unzip.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define JSMN_STATIC
#define JSMN_STRICT
#include "jsmn.h"

#ifndef CANE_UPDATE_RENAME
#define CANE_UPDATE_RENAME rename
#endif

static const char *const kLauncherNames[2] = {
    "ThieviusCTR.3dsx",
    "ThieviusCTR.smdh"
};

static int parse_version(const char *s, unsigned v[4]) {
    if (!s) return 0;
    if (*s == 'v') s++;
    for (int i = 0; i < 4; i++) v[i] = 0;
    for (int i = 0; i < 3; i++) {
        if (*s < '0' || *s > '9') return 0;
        do {
            if (v[i] > 6553) return 0;
            v[i] = v[i] * 10u + (unsigned)(*s++ - '0');
        } while (*s >= '0' && *s <= '9');
        if (i < 2 && *s++ != '.') return 0;
    }
    if (!*s) return 1;
    if (s[0] != '-' || s[1] != 'r') return 0;
    s += 2;
    if (*s < '0' || *s > '9') return 0;
    do {
        if (v[3] > 6553) return 0;
        v[3] = v[3] * 10u + (unsigned)(*s++ - '0');
    } while (*s >= '0' && *s <= '9');
    return !*s;
}

int cane_update_newer(const char *remote, const char *installed) {
    unsigned a[4], b[4];
    if (!parse_version(remote, a) || !parse_version(installed, b)) return 0;
    for (int i = 0; i < 4; i++) {
        if (a[i] != b[i]) return a[i] > b[i];
    }
    return 0;
}

struct Json {
    const char *text;
    jsmntok_t *tokens;
    int count;
};

static int json_skip(struct Json *j, int i) {
    int end = j->tokens[i].end;
    i++;
    while (i < j->count && j->tokens[i].start < end) i++;
    return i;
}

static int json_equal(struct Json *j, int i, const char *s) {
    size_t slen = strlen(s);
    return i >= 0 &&
           j->tokens[i].end - j->tokens[i].start == (int)slen &&
           !memcmp(j->text + j->tokens[i].start, s, slen);
}

static int json_field(struct Json *j, int object, const char *key) {
    if (object < 0 || j->tokens[object].type != JSMN_OBJECT) return -1;
    int found = -1;
    for (int i = object + 1; i < j->count && j->tokens[i].start < j->tokens[object].end;) {
        if (j->tokens[i].type != JSMN_STRING || i + 1 >= j->count) return -1;
        if (json_equal(j, i, key)) {
            if (found >= 0) return -1;
            found = i + 1;
        }
        i = json_skip(j, i + 1);
    }
    return found;
}

static int json_string(struct Json *j, int i, char *out, size_t capacity) {
    if (i < 0 || j->tokens[i].type != JSMN_STRING) return 0;
    size_t n = 0;
    int at = j->tokens[i].start;
    int end = j->tokens[i].end;
    while (at < end) {
        unsigned char c = (unsigned char)j->text[at++];
        if (c == '\\') {
            if (at >= end) return 0;
            c = (unsigned char)j->text[at++];
            if (c != '/' && c != '\\' && c != '"') return 0;
        }
        if (c < 32 || c > 126 || n + 1 >= capacity) return 0;
        out[n++] = (char)c;
    }
    out[n] = '\0';
    return 1;
}

static int json_number(struct Json *j, int i, size_t *out) {
    if (i < 0 || j->tokens[i].type != JSMN_PRIMITIVE) return 0;
    size_t n = 0;
    for (int at = j->tokens[i].start; at < j->tokens[i].end; at++) {
        int c = j->text[at] - '0';
        if (c < 0 || c > 9 || n > CANE_UPDATE_MAX_ZIP / 10u) return 0;
        n = n * 10u + (unsigned)c;
    }
    if (!n || n > CANE_UPDATE_MAX_ZIP) return 0;
    *out = n;
    return 1;
}

int cane_update_release(const char *json, size_t length, const char *installed, struct CaneUpdateRelease *out) {
    if (!json || !installed || !out || length > 65536u) return 0;
    jsmntok_t *tokens = calloc(4096u, sizeof *tokens);
    if (!tokens) return 0;

    jsmn_parser parser;
    jsmn_init(&parser);
    int n = jsmn_parse(&parser, json, length, tokens, 4096);
    struct Json j = { json, tokens, n };
    struct CaneUpdateRelease r = { 0 };
    int ok = 0;

    if (n < 1 || tokens[0].type != JSMN_OBJECT) goto end;
    for (size_t i = (size_t)tokens[0].end; i < length; i++) {
        if (json[i] != ' ' && json[i] != '\r' && json[i] != '\n' && json[i] != '\t') goto end;
    }
    if (!json_equal(&j, json_field(&j, 0, "draft"), "false") ||
        !json_equal(&j, json_field(&j, 0, "prerelease"), "false")) {
        goto end;
    }
    if (!json_string(&j, json_field(&j, 0, "tag_name"), r.tag, sizeof r.tag) ||
        !cane_update_newer(r.tag, installed)) {
        goto end;
    }

    int assets = json_field(&j, 0, "assets");
    if (assets < 0 || tokens[assets].type != JSMN_ARRAY) goto end;

    for (int i = assets + 1; i < n && tokens[i].start < tokens[assets].end; i = json_skip(&j, i)) {
        char name[160], hash[80];
        if (!json_string(&j, json_field(&j, i, "name"), name, sizeof name)) continue;
        size_t size = strlen(name);
        if (size < 16u || strcmp(name + size - 4, ".zip") != 0 ||
            strncmp(name, "ThieviusCTR", 11) != 0 || strstr(name, "source") != NULL) {
            continue;
        }
        if (!json_string(&j, json_field(&j, i, "browser_download_url"), r.url, sizeof r.url) ||
            !json_string(&j, json_field(&j, i, "digest"), hash, sizeof hash) ||
            strncmp(hash, "sha256:", 7) != 0 || strlen(hash) != 71u ||
            !json_number(&j, json_field(&j, i, "size"), &r.bytes)) {
            continue;
        }
        int valid_hex = 1;
        for (int k = 7; k < 71; k++) {
            if (!((hash[k] >= '0' && hash[k] <= '9') || (hash[k] >= 'a' && hash[k] <= 'f'))) {
                valid_hex = 0;
                break;
            }
        }
        if (!valid_hex) continue;

        char prefix[256];
        snprintf(prefix, sizeof prefix, "https://github.com/Raihwy3/ThieviusCTR/releases/download/%s/", r.tag);
        size_t plen = strlen(prefix);
        if (strncmp(r.url, prefix, plen) != 0 || strcmp(r.url + plen, name) != 0) continue;

        memcpy(r.digest, hash + 7, 65);
        *out = r;
        ok = 1;
        break;
    }

end:
    free(tokens);
    return ok;
}

static int fail_msg(char *out, size_t capacity, const char *text) {
    if (out && capacity) snprintf(out, capacity, "%s", text);
    return 0;
}

static int make_path(char *out, size_t capacity, const char *dir, const char *name, const char *suffix) {
    int n = snprintf(out, capacity, "%s/%s%s", dir, name, suffix);
    return n >= 0 && (size_t)n < capacity;
}

static int file_exists(const char *p) {
    struct stat st;
    return stat(p, &st) == 0;
}

int cane_update_recover(const char *directory, char *error, size_t capacity) {
    char marker[640];
    if (!make_path(marker, sizeof marker, directory, "launcher-update.pending", "")) {
        return fail_msg(error, capacity, "Update folder path is too long.");
    }
    FILE *f = fopen(marker, "rb");
    if (!f) {
        return !file_exists(marker) ? 1 : fail_msg(error, capacity, "Cannot read update recovery record.");
    }
    unsigned char record[6] = { 0 };
    size_t count = fread(record, 1, sizeof record, f);
    fclose(f);
    if (count != 5u || memcmp(record, "TCTU", 4) != 0 || record[4] > 3u) {
        return fail_msg(error, capacity, "Update recovery record is invalid.");
    }
    for (int i = 0; i < 2; i++) {
        char current[640], backup[640], staged[640];
        if (!make_path(current, sizeof current, directory, kLauncherNames[i], "") ||
            !make_path(backup, sizeof backup, directory, kLauncherNames[i], ".update-old") ||
            !make_path(staged, sizeof staged, directory, kLauncherNames[i], ".update-new")) {
            return fail_msg(error, capacity, "Update folder path is too long.");
        }
        if (file_exists(backup)) {
            if (file_exists(current) && remove(current) != 0) {
                return fail_msg(error, capacity, "Cannot recover the previous launcher.");
            }
            if (CANE_UPDATE_RENAME(backup, current) != 0) {
                return fail_msg(error, capacity, "Cannot restore the previous launcher.");
            }
        } else if (!(record[4] & (1u << i)) && file_exists(current) && remove(current) != 0) {
            return fail_msg(error, capacity, "Cannot remove an incomplete update.");
        }
        remove(staged);
    }
    if (remove(marker) != 0) {
        return fail_msg(error, capacity, "Cannot finish update recovery.");
    }
    return 1;
}

static unsigned little16(const unsigned char *p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static int extract_member(unzFile zip, const char *directory, int index,
                          int (*progress)(size_t, size_t), char *error, size_t capacity) {
    char member[160], staged[640];
    snprintf(member, sizeof member, "3ds/ThieviusCTR/%s", kLauncherNames[index]);
    unz_file_info64 info;
    if (unzLocateFile(zip, member, 1) != UNZ_OK ||
        unzGetCurrentFileInfo64(zip, &info, NULL, 0, NULL, 0, NULL, 0) != UNZ_OK ||
        (info.flag & 1u) ||
        (info.compression_method != 0u && info.compression_method != 8u) ||
        info.uncompressed_size < 32u ||
        info.uncompressed_size > CANE_UPDATE_MAX_3DSX ||
        (index == 1 && info.uncompressed_size != 0x36c0u)) {
        return fail_msg(error, capacity, "Release has no valid launcher pair.");
    }
    if (!make_path(staged, sizeof staged, directory, kLauncherNames[index], ".update-new")) {
        return fail_msg(error, capacity, "Update folder path is too long.");
    }
    if (unzOpenCurrentFile(zip) != UNZ_OK) {
        return fail_msg(error, capacity, "Cannot open launcher in the ZIP.");
    }
    FILE *f = fopen(staged, "wb");
    if (!f) {
        unzCloseCurrentFile(zip);
        return fail_msg(error, capacity, "Cannot stage update on the SD card.");
    }

    unsigned char block[16384];
    size_t total = 0;
    int valid = 1;
    for (;;) {
        int got = unzReadCurrentFile(zip, block, sizeof block);
        if (got < 0) {
            valid = 0;
            break;
        }
        if (got == 0) break;
        if (total == 0u) {
            if (got < 32 ||
                memcmp(block, index ? "SMDH" : "3DSX", 4) != 0 ||
                (!index && (little16(block + 4) < 32u || little16(block + 6) < 8u))) {
                valid = 0;
                break;
            }
        }
        if ((size_t)got > (size_t)info.uncompressed_size - total ||
            fwrite(block, 1, (size_t)got, f) != (size_t)got) {
            valid = 0;
            break;
        }
        total += (size_t)got;
        if (progress && !progress(total, (size_t)info.uncompressed_size)) {
            valid = 0;
            break;
        }
    }
    if (fflush(f) != 0 || fsync(fileno(f)) != 0) valid = 0;
    if (fclose(f) != 0) valid = 0;
    if (unzCloseCurrentFile(zip) != UNZ_OK || total != info.uncompressed_size) valid = 0;
    if (!valid) {
        remove(staged);
        return fail_msg(error, capacity, "Update was cancelled, damaged, or could not be saved.");
    }
    return 1;
}

int cane_update_install(const char *archive, const char *directory,
                        int (*progress)(size_t, size_t), char *error, size_t capacity) {
    if (!cane_update_recover(directory, error, capacity)) return 0;
    unzFile zip = unzOpen64(archive);
    if (!zip) return fail_msg(error, capacity, "Downloaded file is not a readable ZIP.");

    unsigned count[2] = { 0, 0 };
    int state = unzGoToFirstFile(zip);
    while (state == UNZ_OK) {
        char name[256];
        unz_file_info64 info;
        if (unzGetCurrentFileInfo64(zip, &info, name, sizeof name, NULL, 0, NULL, 0) != UNZ_OK) {
            unzClose(zip);
            return fail_msg(error, capacity, "Invalid release ZIP directory.");
        }
        if (info.size_filename < sizeof name) {
            for (int i = 0; i < 2; i++) {
                char wanted[160];
                snprintf(wanted, sizeof wanted, "3ds/ThieviusCTR/%s", kLauncherNames[i]);
                if (strcmp(name, wanted) == 0) count[i]++;
            }
        }
        state = unzGoToNextFile(zip);
    }

    int ok = (state == UNZ_END_OF_LIST_OF_FILE) && count[0] == 1u && count[1] == 1u;
    if (ok) {
        for (int i = 0; i < 2; i++) {
            if (!extract_member(zip, directory, i, progress, error, capacity)) {
                ok = 0;
                break;
            }
        }
    } else {
        fail_msg(error, capacity, "Release ZIP has missing or duplicate launcher files.");
    }
    unzClose(zip);

    if (!ok) {
        for (int i = 0; i < 2; i++) {
            char p[640];
            if (make_path(p, sizeof p, directory, kLauncherNames[i], ".update-new")) remove(p);
        }
        return 0;
    }

    unsigned char record[5] = { 'T', 'C', 'T', 'U', 0 };
    char marker[640];
    if (!make_path(marker, sizeof marker, directory, "launcher-update.pending", "")) {
        return fail_msg(error, capacity, "Update folder path is too long.");
    }
    for (int i = 0; i < 2; i++) {
        char p[640];
        if (!make_path(p, sizeof p, directory, kLauncherNames[i], "")) {
            return fail_msg(error, capacity, "Update folder path is too long.");
        }
        if (file_exists(p)) record[4] |= (unsigned char)(1u << i);
        if (!make_path(p, sizeof p, directory, kLauncherNames[i], ".update-old")) {
            return fail_msg(error, capacity, "Update folder path is too long.");
        }
        if (file_exists(p) && remove(p) != 0) {
            return fail_msg(error, capacity, "Cannot clear previous update backup.");
        }
    }

    FILE *f = fopen(marker, "wb");
    if (!f) return fail_msg(error, capacity, "Cannot save update recovery record.");
    int saved = fwrite(record, 1, 5, f) == 5u;
    if (fflush(f) != 0 || fsync(fileno(f)) != 0) saved = 0;
    if (fclose(f) != 0) saved = 0;
    if (!saved) {
        remove(marker);
        return fail_msg(error, capacity, "Cannot save update recovery record.");
    }

    for (int i = 0; i < 2; i++) {
        char current[640], backup[640], staged[640];
        make_path(current, sizeof current, directory, kLauncherNames[i], "");
        make_path(backup, sizeof backup, directory, kLauncherNames[i], ".update-old");
        make_path(staged, sizeof staged, directory, kLauncherNames[i], ".update-new");
        if (((record[4] & (1u << i)) && CANE_UPDATE_RENAME(current, backup) != 0) ||
            CANE_UPDATE_RENAME(staged, current) != 0) {
            if (!cane_update_recover(directory, error, capacity)) return 0;
            return fail_msg(error, capacity, "Update replacement failed; previous launcher restored.");
        }
    }

    if (remove(marker) != 0) {
        cane_update_recover(directory, error, capacity);
        return fail_msg(error, capacity, "Update commit failed; recovery attempted.");
    }
    for (int i = 0; i < 2; i++) {
        char p[640];
        make_path(p, sizeof p, directory, kLauncherNames[i], ".update-old");
        remove(p);
    }
    return 1;
}
