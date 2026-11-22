#include "launcher_extract_3ds.h"
#include "launcher_ui_3ds.h"
#include "fmv_decode_3ds.h"
#include "BrxWalker.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <exception>
#include <fcntl.h>
#include <map>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

constexpr size_t kIsoReadChunk = 256u * 1024u;
constexpr size_t kMaxRawLevelBytes = 12u * 1024u * 1024u;
constexpr u64 kSectorBytes = 2048ull;
constexpr u32 kElfBaseVaddr = 0x00100000u;
constexpr u32 kElfFileBase = 0x00001000u;

void logLauncher(FILE* logFile, const char* fmt, ...) {
    if (!logFile) return;
    va_list args;
    va_start(args, fmt);
    std::vfprintf(logFile, fmt, args);
    std::fputc('\n', logFile);
    std::fflush(logFile);
    va_end(args);
}

u32 readU32Le(const u8* p) {
    return u32(p[0]) | (u32(p[1]) << 8) | (u32(p[2]) << 16) | (u32(p[3]) << 24);
}

u32 readU32Be(const u8* p) {
    return (u32(p[0]) << 24) | (u32(p[1]) << 16) | (u32(p[2]) << 8) | u32(p[3]);
}

float readF32Le(const u8* p) {
    float v = 0.0f;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

void writeU16Le(u8* p, u16 v) {
    p[0] = u8(v & 0xffu);
    p[1] = u8(v >> 8);
}

void writeU32Le(u8* p, u32 v) {
    p[0] = u8(v & 0xffu);
    p[1] = u8((v >> 8) & 0xffu);
    p[2] = u8((v >> 16) & 0xffu);
    p[3] = u8((v >> 24) & 0xffu);
}

bool eqNoCase(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

bool endsWithNoCase(const std::string& str, const char* suffix) {
    const size_t slen = std::strlen(suffix);
    if (str.size() < slen) return false;
    return eqNoCase(str.substr(str.size() - slen), suffix);
}

bool ensureDir(const std::string& path) {
    if (path.empty()) return false;
    size_t pos = 0;
    const size_t colon = path.find(':');
    if (colon != std::string::npos) pos = colon + 1;
    if (pos < path.size() && path[pos] == '/') ++pos;
    for (; pos <= path.size(); ++pos) {
        if (pos == path.size() || path[pos] == '/') {
            const std::string sub = path.substr(0, pos);
            if (!sub.empty() && sub.back() != ':') {
                if (mkdir(sub.c_str(), 0777) != 0 && errno != EEXIST)
                    return false;
            }
        }
    }
    return true;
}

bool ensureParentDir(const std::string& filePath) {
    const size_t slash = filePath.find_last_of('/');
    if (slash == std::string::npos || slash == 0) return true;
    return ensureDir(filePath.substr(0, slash));
}

bool commitTempFile(const std::string& tmpPath, const std::string& dstPath) {
    std::remove(dstPath.c_str());
    return std::rename(tmpPath.c_str(), dstPath.c_str()) == 0;
}

bool writeAtomicBytes(const std::string& dstPath, const void* data, size_t size) {
    if (!ensureParentDir(dstPath)) return false;
    const std::string tmpPath = dstPath + ".tmp";
    std::FILE* f = std::fopen(tmpPath.c_str(), "wb");
    if (!f) return false;
    const bool ok = (size == 0 || std::fwrite(data, 1, size, f) == size) &&
                    std::fflush(f) == 0;
    if (std::fclose(f) != 0 || !ok || !commitTempFile(tmpPath, dstPath)) {
        std::remove(tmpPath.c_str());
        return false;
    }
    return true;
}

bool writeAtomicText(const std::string& dstPath, const std::string& text) {
    return writeAtomicBytes(dstPath, text.data(), text.size());
}

bool fileSizeSmall(const std::string& path, size_t& outSize) {
    struct stat st {};
    if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return false;
    if (st.st_size < 0) return false;
    outSize = size_t(st.st_size);
    return true;
}

bool isValidNonEmptyFile(const std::string& path) {
    size_t sz = 0;
    return fileSizeSmall(path, sz) && sz > 0;
}

bool isValidPcBrx(const std::string& path) {
    size_t sz = 0;
    if (!fileSizeSmall(path, sz) || sz < 64) return false;
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    u8 hdr[8]{};
    const bool readOk = std::fread(hdr, 1, sizeof(hdr), f) == sizeof(hdr);
    std::fclose(f);
    if (!readOk) return false;
    const u32 magic = readU32Le(hdr);
    const u32 ver = readU32Le(hdr + 4);
    return magic == 0x52424350u && ver >= 17u && ver <= 19u;
}

bool isValidWav(const std::string& path) {
    size_t sz = 0;
    if (!fileSizeSmall(path, sz) || sz < 44) return false;
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    u8 hdr[12]{};
    const bool readOk = std::fread(hdr, 1, sizeof(hdr), f) == sizeof(hdr);
    std::fclose(f);
    if (!readOk) return false;
    return std::memcmp(hdr, "RIFF", 4) == 0 && std::memcmp(hdr + 8, "WAVE", 4) == 0;
}

bool isValidBnk(const std::string& path, u32 expectedSize) {
    size_t sz = 0;
    if (!fileSizeSmall(path, sz) || sz < 4) return false;
    if (expectedSize != 0 && sz != expectedSize) return false;
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    u8 hdr[4]{};
    const bool readOk = std::fread(hdr, 1, sizeof(hdr), f) == sizeof(hdr);
    std::fclose(f);
    return readOk && readU32Le(hdr) == 3u;
}

bool isValidMus(const std::string& path, u32 expectedSize) {
    size_t sz = 0;
    if (!fileSizeSmall(path, sz) || sz == 0) return false;
    return expectedSize == 0 || sz == expectedSize;
}

bool isValidCmovPair(const std::string& baseDir, int i) {
    char path[256];
    std::snprintf(path, sizeof(path), "%s/Cutscenes/Cutscene_%02d.cmov", baseDir.c_str(), i);
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    u8 hdr[24]{};
    bool ok = std::fread(hdr, 1, sizeof(hdr), f) == sizeof(hdr);
    if (ok) {
        const u32 w = readU32Le(hdr + 4);
        const u32 h = readU32Le(hdr + 8);
        const u32 fps = readU32Le(hdr + 12);
        const u32 nf = readU32Le(hdr + 16);
        const u32 fbytes = readU32Le(hdr + 20);
        const u32 expectedBytes = (w / 8u) * (h / 8u) * 32u;
        const bool fpsValid = (fps >= 1u && fps <= 60u) || (fps >= 1000u && fps <= 60000u);
        ok = std::memcmp(hdr, "CMV2", 4) == 0 &&
             w >= 64u && w <= 800u && (w % 8u) == 0u &&
             h >= 64u && h <= 240u && (h % 8u) == 0u &&
             fpsValid && nf > 0u && fbytes == expectedBytes;
        if (ok && fseeko(f, 0, SEEK_END) == 0) {
            const off_t end = ftello(f);
            ok = end > 0 && u64(end) == 24ull + u64(nf) * u64(fbytes);
        } else {
            ok = false;
        }
    }
    std::fclose(f);
    if (!ok) return false;
    std::snprintf(path, sizeof(path), "%s/Cutscenes/Cutscene_%02d.wav", baseDir.c_str(), i);
    return isValidWav(path);
}

bool isValidM2vPair(const std::string& baseDir, int i) {
    char path[256];
    std::snprintf(path, sizeof(path), "%s/Cutscenes/Cutscene_%02d.m2v", baseDir.c_str(), i);
    if (!CaneIsValidM2vFile3ds(path)) return false;
    std::snprintf(path, sizeof(path), "%s/Cutscenes/Cutscene_%02d.wav", baseDir.c_str(), i);
    return isValidWav(path);
}

bool hasValidCutscene(const std::string& baseDir, int i) {
    char path[256];
    std::snprintf(path, sizeof(path), "%s/Cutscenes/Cutscene_%02d.bik", baseDir.c_str(), i);
    if (CaneIsValidBikFile3ds(path)) return true;
    if (const char* bikName = CanePs3BikName3ds(i)) {
        std::snprintf(path, sizeof(path), "%s/Cutscenes/%s", baseDir.c_str(), bikName);
        if (CaneIsValidBikFile3ds(path)) return true;
    }
    return isValidCmovPair(baseDir, i) || isValidM2vPair(baseDir, i);
}

unsigned countValidCutscenes(const std::string& baseDir) {
    unsigned count = 0;
    for (int i = 0; i < 18; ++i) {
        if (hasValidCutscene(baseDir, i)) ++count;
    }
    return count;
}

const char* episodeName(u32 levelId) {
    switch (levelId >> 8) {
    case 1: return "Tide of Terror";
    case 2: return "Sunset Snake Eyes";
    case 3: return "Vicious Voodoo";
    case 4: return "Fire in the sky";
    case 5: return "The Cold Heart of Hate";
    default: return "";
    }
}

std::string sanitizeName(const std::string& name) {
    std::string out;
    out.reserve(name.size());
    for (unsigned char c : name) {
        if (c < 32 || std::strchr("<>:\"/\\|?*", c) != nullptr)
            out.push_back('_');
        else
            out.push_back(char(c));
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '.'))
        out.pop_back();
    return out.empty() ? std::string("Unnamed Level") : out;
}

std::string canonicalLevelFolder(u32 levelId, const char* rawName) {
    if ((levelId & 0xffffu) == 3u) return "Paris";
    if ((levelId & 0xffffu) == 4u) return "The Hideout";
    return sanitizeName(rawName ? rawName : "");
}

std::string makeBrxPath(const std::string& baseDir, u32 levelId, const std::string& name) {
    const char* ep = episodeName(levelId);
    std::string dir = baseDir + "/Worlds/";
    if (ep[0] != '\0') {
        dir += ep;
        dir += '/';
    }
    dir += name + '/' + name + ".brx";
    return dir;
}

std::string makeDialogDir(const std::string& baseDir, u32 levelId, const std::string& name) {
    const char* ep = episodeName(levelId);
    std::string dir = baseDir + "/Worlds/";
    if (ep[0] != '\0') {
        dir += ep;
        dir += '/';
    }
    dir += name + "/Dialog";
    return dir;
}

constexpr const char* kBankNames[7] = {
    "Common", "Intro", "Underwater", "Muggshot", "Voodoo", "Snow", "Clockwerk"
};

struct MusicEntry {
    const char* levelName;
    u32 musid;
    u32 sector;
    u32 size;
};

constexpr MusicEntry kMusicTable[] = {
    {"Paris, France", 0x55, 0xC8E0E, 0x7A6A0},
    {"The Hideout", 0x43, 0xD3411, 0x6F7A0},
    {"A Cunning Disguise", 0x47, 0xC4602, 0x72324},
    {"A Stealthy Approach", 0x51, 0xD44ED, 0x745F8},
    {"High Class Heist", 0x59, 0xCDE0B, 0x74674},
    {"Into the Machine", 0x3F, 0xC839D, 0x7B414},
    {"Prowling the Grounds", 0x44, 0xBFA16, 0x746D4},
    {"The Eye of the Storm", 0x57, 0xC354B, 0x7A0C4},
    {"The Fire Down Below", 0x46, 0xC130C, 0x775E0},
    {"The Gunboat Graveyard", 0x42, 0xCAC3D, 0x70D4C},
    {"Treasure in the Depths", 0x3E, 0xD4E9D, 0x748F8},
    {"A Rocky Start", 0x50, 0xBC6B4, 0x64A34},
    {"At the Dog Track", 0x4E, 0xCFBE5, 0x726F8},
    {"Back Alley Heist", 0x4C, 0xC1B99, 0x77978},
    {"Boneyard Casino", 0x49, 0xCF341, 0x71A18},
    {"Last Call", 0x48, 0xC02DE, 0x731B8},
    {"Muggshot's Turf", 0x40, 0xCE9BB, 0x5DB18},
    {"Murray's Big Gamble", 0x4A, 0xCD592, 0x767B8},
    {"Straight to the Top", 0x4C, 0xD04EF, 0x77978},
    {"Two to Tango", 0x4D, 0xD5A8B, 0x788FC},
    {"A Deadly Dance", 0x68, 0xBE35F, 0x771E8},
    {"A Ghastly Voyage", 0x65, 0xC6338, 0x76D9C},
    {"A Grave Undertaking", 0x55, 0xCCD68, 0x7A6A0},
    {"Descent into Danger", 0x64, 0xC25C6, 0x6EBB8},
    {"Down Home Cooking", 0x62, 0xC50BB, 0x73978},
    {"Piranha Lake", 0x67, 0xD4AFA, 0x73978},
    {"The Dread Swamp Path", 0x60, 0xC6D86, 0x76A60},
    {"The Lair of the Beast", 0x64, 0xCB6A5, 0x6EBB8},
    {"The Swamp's Dark Center", 0x64, 0xD1852, 0x6EBB8},
    {"A Desperate Race", 0x4E, 0xCBD9B, 0x726F8},
    {"A Perilous Ascent", 0x53, 0xD2424, 0x773CC},
    {"Duel by the Dragon", 0x5B, 0xBD2A0, 0x6DA90},
    {"Flame Fu!", 0x56, 0xC4B48, 0x6EA6C},
    {"Flaming Temple of Flame", 0x54, 0xBEF0F, 0x7737C},
    {"Inside the Stronghold", 0x5B, 0xC7870, 0x6DA90},
    {"Rapid Fire Assault", 0x5D, 0xC2F89, 0x6C2BC},
    {"The King of the Hill", 0x5C, 0xD2D42, 0x78064},
    {"The Unseen Foe", 0x44, 0xC5A33, 0x746D4},
    {"A Daring Rescue", 0x38, 0xBD8B1, 0x788F8},
    {"A Hazardous Path", 0x35, 0xC3CB5, 0x77C7C},
    {"A Strange Reunion", 0x34, 0xC0AAC, 0x95774},
    {"A Temporary Truce", 0x39, 0xD0BD7, 0x74DAC},
    {"Bentley Comes Through", 0x36, 0xD50F1, 0x8C528},
    {"Burning Rubber", 0x3A, 0xCC1CA, 0x77614},
    {"Sinking Peril", 0x37, 0xD3A9E, 0x77F08},
};

enum class DialogAudioSource : u8 { ASEG = 0, DIALOG = 1 };

struct DialogRow {
    u32 levelId;
    u32 sector;
    u32 size;
    u32 identity;
    DialogAudioSource source;
};

constexpr DialogRow kRawDialogRows[] = {
#include "Data/DialogAudioTable.inc"
};

std::vector<DialogRow> uniqueDialogRows() {
    std::vector<DialogRow> out;
    out.reserve(sizeof(kRawDialogRows) / sizeof(kRawDialogRows[0]));
    for (const DialogRow& row : kRawDialogRows) {
        bool dup = false;
        for (const DialogRow& prev : out) {
            if (prev.levelId == row.levelId && prev.sector == row.sector &&
                prev.size == row.size && prev.identity == row.identity &&
                prev.source == row.source) {
                dup = true;
                break;
            }
        }
        if (!dup) out.push_back(row);
    }
    return out;
}

std::vector<MusicEntry> uniqueMusicEntries() {
    std::map<u32, MusicEntry> byId;
    for (const auto& m : kMusicTable)
        byId[m.musid] = m;
    std::vector<MusicEntry> out;
    out.reserve(byId.size());
    for (const auto& kv : byId)
        out.push_back(kv.second);
    return out;
}

class ProgressController {
public:
    ProgressController(PrintConsole* top, PrintConsole* bottom, int model, unsigned cutscenes)
        : top_(top), bottom_(bottom), model_(model), cutscenes_(cutscenes) {
        hidScanInput();
        bReleased_ = (hidKeysHeld() & KEY_B) == 0;
    }

    bool cancelled() const { return cancelled_; }

    bool pump(const char* stage,
              const char* detail,
              unsigned itemDone,
              unsigned itemTotal,
              unsigned overallDone,
              unsigned overallTotal,
              int mode,
              bool force = false) {
        if (!aptMainLoop()) {
            cancelled_ = true;
            return false;
        }
        hidScanInput();
        const u32 held = hidKeysHeld();
        const u32 down = hidKeysDown();
        if ((held & KEY_B) == 0) bReleased_ = true;
        if (mode == 1 && bReleased_ && ((down & KEY_B) || (held & KEY_B))) {
            cancelled_ = true;
            return false;
        }
        const u64 now = osGetTime();
        const bool stageChanged = (lastStage_ == nullptr) || std::strcmp(lastStage_, stage) != 0;
        if (force || stageChanged || now - lastPaintMs_ >= 150) {
            lastStage_ = stage;
            lastPaintMs_ = now;
            CaneLauncherDrawProgress(top_, bottom_, model_, stage, detail,
                                     itemDone, itemTotal, overallDone, overallTotal,
                                     cutscenes_, mode);
        }
        return true;
    }

    PrintConsole* top() const { return top_; }
    PrintConsole* bottom() const { return bottom_; }
    int model() const { return model_; }
    unsigned cutscenes() const { return cutscenes_; }
    void setCutscenes(unsigned c) { cutscenes_ = c; }

private:
    PrintConsole* top_;
    PrintConsole* bottom_;
    int model_;
    unsigned cutscenes_;
    u64 lastPaintMs_{0};
    const char* lastStage_{nullptr};
    bool bReleased_{false};
    bool cancelled_{false};
};

class Iso64Reader {
public:
    ~Iso64Reader() { close(); }

    bool open(const std::string& path) {
        close();
        std::string rel = path;
        if (rel.rfind("sdmc:", 0) == 0) rel.erase(0, 5);
        if (rel.empty() || rel[0] != '/') rel.insert(rel.begin(), '/');

        Result res = FSUSER_OpenFileDirectly(&fsHandle_, ARCHIVE_SDMC,
                                             fsMakePath(PATH_EMPTY, ""),
                                             fsMakePath(PATH_ASCII, rel.c_str()),
                                             FS_OPEN_READ, 0);
        if (R_SUCCEEDED(res)) {
            if (R_SUCCEEDED(FSFILE_GetSize(fsHandle_, &size_)) && size_ > 0) {
                useFs_ = true;
                return true;
            }
            FSFILE_Close(fsHandle_);
            fsHandle_ = 0;
        }

        // UTF-16 path fallback for non-ASCII paths if any.
        std::vector<u16> u16;
        u16.resize(rel.size() + 1);
        for (size_t i = 0; i < rel.size(); ++i) u16[i] = u8(rel[i]);
        u16[rel.size()] = 0;
        FS_Path utf16Path{PATH_UTF16, u32(u16.size() * sizeof(u16)), u16.data()};
        res = FSUSER_OpenFileDirectly(&fsHandle_, ARCHIVE_SDMC,
                                      fsMakePath(PATH_EMPTY, ""),
                                      utf16Path, FS_OPEN_READ, 0);
        if (R_SUCCEEDED(res)) {
            if (R_SUCCEEDED(FSFILE_GetSize(fsHandle_, &size_)) && size_ > 0) {
                useFs_ = true;
                return true;
            }
            FSFILE_Close(fsHandle_);
            fsHandle_ = 0;
        }

        // POSIX 64-bit off_t fallback (devkitARM newlib uses 64-bit off_t).
        fd_ = ::open(path.c_str(), O_RDONLY);
        if (fd_ >= 0) {
            const off_t end = ::lseek(fd_, 0, SEEK_END);
            if (end > 0) {
                size_ = u64(end);
                useFs_ = false;
                return true;
            }
            ::close(fd_);
            fd_ = -1;
        }
        return false;
    }

    void close() {
        if (useFs_ && fsHandle_) {
            FSFILE_Close(fsHandle_);
            fsHandle_ = 0;
        }
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
        useFs_ = false;
        size_ = 0;
    }

    u64 size() const { return size_; }

    bool readExact(u64 offset, void* dst, size_t len) const {
        if (offset > size_ || u64(len) > size_ - offset) return false;
        u8* out = static_cast<u8*>(dst);
        size_t done = 0;
        while (done < len) {
            const u32 chunk = u32(std::min<size_t>(len - done, kIsoReadChunk));
            if (useFs_) {
                u32 bytesRead = 0;
                const Result res = FSFILE_Read(fsHandle_, &bytesRead, offset + done, out + done, chunk);
                if (R_FAILED(res) || bytesRead != chunk) return false;
            } else {
                if (::lseek(fd_, off_t(offset + done), SEEK_SET) < 0) return false;
                size_t got = 0;
                while (got < chunk) {
                    const ssize_t n = ::read(fd_, out + done + got, chunk - got);
                    if (n <= 0) return false;
                    got += size_t(n);
                }
            }
            done += chunk;
        }
        return true;
    }

    bool findRootFile(const char* targetName, u64& outOffset, u32& outSize) const {
        std::array<u8, 2048> pvd{};
        if (!readExact(16ull * kSectorBytes, pvd.data(), pvd.size())) return false;
        if (pvd[0] != 1 || std::memcmp(pvd.data() + 1, "CD001", 5) != 0) return false;
        const u32 rootLba = readU32Le(pvd.data() + 156 + 2);
        const u32 rootSize = readU32Le(pvd.data() + 156 + 10);
        if (rootSize == 0 || rootSize > 256u * 1024u) return false;
        std::vector<u8> rd(rootSize);
        if (!readExact(u64(rootLba) * kSectorBytes, rd.data(), rd.size())) return false;

        size_t pos = 0;
        while (pos < rd.size()) {
            const u8 rlen = rd[pos];
            if (rlen == 0) {
                pos = ((pos / 2048u) + 1u) * 2048u;
                continue;
            }
            if (pos + rlen > rd.size() || rlen < 34) break;
            const u8 nlen = rd[pos + 32];
            if (size_t(33) + nlen > rlen) break;
            std::string name(reinterpret_cast<const char*>(&rd[pos + 33]), nlen);
            const size_t semi = name.find(';');
            if (semi != std::string::npos) name.resize(semi);
            if (eqNoCase(name, targetName)) {
                const u32 lba = readU32Le(&rd[pos + 2]);
                const u32 sz = readU32Le(&rd[pos + 10]);
                outOffset = u64(lba) * kSectorBytes;
                outSize = sz;
                return outOffset <= size_ && u64(sz) <= size_ - outOffset;
            }
            pos += rlen;
        }
        return false;
    }

private:
    Handle fsHandle_{0};
    int fd_{-1};
    bool useFs_{false};
    u64 size_{0};
};

struct LinearScratch {
    u8* ptr{nullptr};
    size_t cap{0};

    ~LinearScratch() { reset(); }
    void reset() {
        if (ptr) {
            linearFree(ptr);
            ptr = nullptr;
            cap = 0;
        }
    }
    bool allocAtLeast(size_t needed) {
        if (ptr && cap >= needed) return true;
        reset();
        ptr = static_cast<u8*>(linearAlloc(needed));
        if (!ptr) return false;
        cap = needed;
        return true;
    }
};

struct LevelTask {
    u32 id;
    std::string name;
    u64 offset;
    u32 compSize;
    std::string brxPath;
    std::string elfRawName;
};

/* Matches extract_ntsc.py::decompress() byte-for-byte with bounded scratch output. */
size_t decompressLzss(const u8* src, size_t srcLen, u8* dst, size_t dstCap,
                      ProgressController& prog, const char* detail,
                      unsigned overallDone, unsigned overallTotal) {
    std::array<u8, 8202> work{};
    size_t ip = 0;
    size_t op = 0;
    size_t outLen = 0;
    unsigned flags = 0;
    u64 nextPump = 0;

    while (ip < srcLen) {
        flags >>= 1;
        if ((flags & 0x100u) == 0) {
            flags = 0xff00u | src[ip++];
        }
        if (ip >= srcLen) break;
        const u8 first = src[ip++];
        if (flags & 1u) {
            work[op++] = first;
        } else {
            if (ip >= srcLen) throw std::runtime_error("Truncated compressed LZSS match");
            const unsigned token = unsigned(first) | (unsigned(src[ip++]) << 8);
            unsigned source = token & 8191u;
            const unsigned run = (token >> 13) + 3u;
            for (unsigned k = 0; k < run; ++k) {
                work[op++] = work[source & 8191u];
                ++source;
            }
        }
        if (op >= 8192u) {
            if (outLen + 8192u > dstCap)
                throw std::runtime_error("Decompressed level exceeds scratch buffer");
            std::memcpy(dst + outLen, work.data(), 8192u);
            outLen += 8192u;
            const size_t spill = op - 8192u;
            if (spill > 0) std::memmove(work.data(), work.data() + 8192u, spill);
            op = spill;
            if (outLen >= nextPump) {
                nextPump = outLen + 262144u;
                if (!prog.pump("Extracting levels", detail,
                               unsigned(ip / 1024u), unsigned(std::max<size_t>(1, srcLen / 1024u)),
                               overallDone, overallTotal, 1)) {
                    throw std::runtime_error("CANCELLED");
                }
            }
        }
    }
    if (op > 0) {
        if (outLen + op > dstCap)
            throw std::runtime_error("Decompressed level exceeds scratch buffer");
        std::memcpy(dst + outLen, work.data(), op);
        outLen += op;
    }
    return outLen;
}

static constexpr int kVagCoeff[5][2] = {
    {0, 0}, {60, 0}, {115, -52}, {98, -55}, {122, -60}
};

void fillWavHeaderMono16(u8 wavHdr[44], u32 rate, u32 dataBytes) {
    std::memset(wavHdr, 0, 44);
    std::memcpy(wavHdr, "RIFF", 4);
    writeU32Le(wavHdr + 4, 36u + dataBytes);
    std::memcpy(wavHdr + 8, "WAVEfmt ", 8);
    writeU32Le(wavHdr + 16, 16u);
    writeU16Le(wavHdr + 20, 1u);
    writeU16Le(wavHdr + 22, 1u);
    writeU32Le(wavHdr + 24, rate);
    writeU32Le(wavHdr + 28, rate * 2u);
    writeU16Le(wavHdr + 32, 2u);
    writeU16Le(wavHdr + 34, 16u);
    std::memcpy(wavHdr + 36, "data", 4);
    writeU32Le(wavHdr + 40, dataBytes);
}

bool decodeVagToWavFile(const u8* vagData, size_t vagLen,
                        const std::string& dstPath,
                        std::vector<s16>& pcmScratch) {
    if (vagLen < 48 || std::memcmp(vagData, "VAGp", 4) != 0) return false;
    const u32 bodySize = readU32Be(vagData + 12);
    const u32 rate = readU32Be(vagData + 16);
    if (rate < 4000u || rate > 192000u) return false;
    const size_t end = std::min(vagLen, size_t(48) + (bodySize ? size_t(bodySize) : vagLen - 48));

    pcmScratch.clear();
    pcmScratch.reserve(((end > 48 ? end - 48 : 0) / 16u + 1u) * 28u);
    int h1 = 0;
    int h2 = 0;
    for (size_t block = 48; block + 15 < end; block += 16) {
        const unsigned shift = vagData[block] & 15u;
        const unsigned predictor = vagData[block] >> 4;
        const int a = predictor < 5u ? kVagCoeff[predictor][0] : 0;
        const int b = predictor < 5u ? kVagCoeff[predictor][1] : 0;
        for (unsigned sample = 0; sample < 28u; ++sample) {
            const u8 packed = vagData[block + 2u + sample / 2u];
            int nibble = (packed >> (4u * (sample & 1u))) & 15u;
            if (nibble >= 8) nibble -= 16;
            const int pred = ((nibble * 4096) >> shift) + ((a * h1 + b * h2 + 32) >> 6);
            const int clamped = std::clamp(pred, -32768, 32767);
            h2 = h1;
            h1 = clamped;
            pcmScratch.push_back(s16(clamped));
        }
        if (vagData[block + 1u] & 1u) break;
    }
    if (pcmScratch.empty()) return false;

    const u32 dataBytes = u32(pcmScratch.size() * sizeof(s16));
    u8 wavHdr[44];
    fillWavHeaderMono16(wavHdr, rate, dataBytes);

    if (!ensureParentDir(dstPath)) return false;
    const std::string tmpPath = dstPath + ".tmp";
    std::FILE* f = std::fopen(tmpPath.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(wavHdr, 1, 44, f) == 44 &&
                    std::fwrite(pcmScratch.data(), 1, dataBytes, f) == dataBytes &&
                    std::fflush(f) == 0;
    if (std::fclose(f) != 0 || !ok || !commitTempFile(tmpPath, dstPath)) {
        std::remove(tmpPath.c_str());
        return false;
    }
    return true;
}

bool copyFileAtomic(const std::string& srcPath, const std::string& dstPath) {
    if (!ensureParentDir(dstPath)) return false;
    std::FILE* src = std::fopen(srcPath.c_str(), "rb");
    if (!src) return false;
    const std::string tmpPath = dstPath + ".tmp";
    std::FILE* dst = std::fopen(tmpPath.c_str(), "wb");
    if (!dst) {
        std::fclose(src);
        return false;
    }
    std::vector<u8> buf(64u * 1024u);
    bool ok = true;
    for (;;) {
        const size_t got = std::fread(buf.data(), 1, buf.size(), src);
        if (got > 0 && std::fwrite(buf.data(), 1, got, dst) != got) {
            ok = false;
            break;
        }
        if (got < buf.size()) {
            if (std::ferror(src)) ok = false;
            break;
        }
    }
    if (std::fflush(dst) != 0) ok = false;
    std::fclose(src);
    if (std::fclose(dst) != 0) ok = false;
    if (!ok || !commitTempFile(tmpPath, dstPath)) {
        std::remove(tmpPath.c_str());
        return false;
    }
    return true;
}

bool demuxPssToM2vAndWav(const Iso64Reader& iso,
                         u64 isoOffset,
                         u32 pssSize,
                         const std::string& m2vPath,
                         const std::string& wavPath,
                         ProgressController& prog,
                         unsigned idx,
                         unsigned total,
                         FILE* logFile) {
    if (!ensureParentDir(m2vPath) || !ensureParentDir(wavPath) || pssSize < 64u) return false;
    if (isoOffset > iso.size() || u64(pssSize) > iso.size() - isoOffset) return false;

    const std::string m2vTmp = m2vPath + ".tmp";
    const std::string wavTmp = wavPath + ".tmp";
    std::remove(m2vTmp.c_str());
    std::remove(wavTmp.c_str());

    struct DemuxFiles {
        std::FILE* m2v = nullptr;
        std::FILE* wav = nullptr;
        std::string m2vTmpPath;
        std::string wavTmpPath;
        bool committed = false;
        ~DemuxFiles() {
            if (m2v) { std::fclose(m2v); m2v = nullptr; }
            if (wav) { std::fclose(wav); wav = nullptr; }
            if (!committed) {
                std::remove(m2vTmpPath.c_str());
                std::remove(wavTmpPath.c_str());
            }
        }
    } files{ nullptr, nullptr, m2vTmp, wavTmp, false };

    files.m2v = std::fopen(m2vTmp.c_str(), "wb");
    files.wav = std::fopen(wavTmp.c_str(), "wb");
    if (!files.m2v || !files.wav) return false;
    std::setvbuf(files.m2v, nullptr, _IOFBF, 128u * 1024u);
    std::setvbuf(files.wav, nullptr, _IOFBF, 64u * 1024u);

    u8 wavHdr[44]{};
    if (std::fwrite(wavHdr, 1, 44, files.wav) != 44) return false;

    struct PssAudioState {
        std::FILE* wavFile = nullptr;
        std::vector<u8> directHdr;
        std::vector<u8> hdrAccum;
        std::vector<u8> blockBuf;
        std::vector<s16> chPcm[2];
        std::vector<s16> wavOut;
        bool headerParsed = false;
        bool directMode = false;
        u32 fmt = 0;
        u32 rate = 0;
        u32 outRate = 16000;
        u32 channels = 0;
        u32 interleave = 0x800;
        u32 bodyLimit = 0;
        u32 bodyWritten = 0;
        int h1[2]{ 0, 0 };
        int h2[2]{ 0, 0 };
        bool adpcmEnded[2]{ false, false };
        u32 decAccum = 0;
        int32_t decSum = 0;
        u32 wavDataBytes = 0;

        static size_t findTag(const std::vector<u8>& buf, size_t start, const char tag[4]) {
            if (buf.size() < start + 4u) return std::string::npos;
            for (size_t i = start; i + 4u <= buf.size(); ++i) {
                if (std::memcmp(&buf[i], tag, 4) == 0) return i;
            }
            return std::string::npos;
        }

        bool flushWavChunk() {
            if (wavOut.empty()) return true;
            const size_t n = wavOut.size();
            const size_t bytes = n * sizeof(s16);
            if (std::fwrite(wavOut.data(), 1, bytes, wavFile) != bytes) return false;
            wavDataBytes += u32(bytes);
            wavOut.clear();
            return true;
        }

        bool pushOutSample(s16 s) {
            wavOut.push_back(s);
            if (wavOut.size() >= 4096u) return flushWavChunk();
            return true;
        }

        bool emitMonoSample(s16 s) {
            if (rate == 48000u && outRate == 16000u) {
                decSum += int32_t(s);
                ++decAccum;
                if (decAccum == 3u) {
                    const s16 avg = s16(decSum / 3);
                    decSum = 0;
                    decAccum = 0;
                    return pushOutSample(avg);
                }
                return true;
            }
            return pushOutSample(s);
        }

        void decodeChannelSlice(const u8* data, size_t len, unsigned chIdx, std::vector<s16>& out) {
            out.clear();
            if (fmt == 1u) {
                const size_t count = len / 2u;
                out.reserve(count);
                for (size_t i = 0; i < count; ++i) {
                    out.push_back(s16(u16(data[i * 2u]) | (u16(data[i * 2u + 1u]) << 8)));
                }
                return;
            }
            if (fmt == 0u) {
                const size_t count = len / 2u;
                out.reserve(count);
                for (size_t i = 0; i < count; ++i) {
                    out.push_back(s16((u16(data[i * 2u]) << 8) | u16(data[i * 2u + 1u])));
                }
                return;
            }
            if (adpcmEnded[chIdx]) return;
            for (size_t pos = 0; pos + 15u < len; pos += 16u) {
                const u8* blk = data + pos;
                if (blk[0] == 0 && blk[1] == 1) {
                    bool all77 = true;
                    for (int k = 2; k < 16; ++k) {
                        if (blk[k] != 0x77) { all77 = false; break; }
                    }
                    if (all77) {
                        adpcmEnded[chIdx] = true;
                        break;
                    }
                }
                const unsigned predictor = blk[0] >> 4;
                const unsigned shift = blk[0] & 15u;
                if (predictor >= 5u) continue;
                const int a = kVagCoeff[predictor][0];
                const int b = kVagCoeff[predictor][1];
                for (unsigned i = 0; i < 28u; ++i) {
                    const u8 packed = blk[2u + i / 2u];
                    int nibble = (packed >> (4u * (i & 1u))) & 15u;
                    if (nibble >= 8) nibble -= 16;
                    const int pred = ((nibble * 4096) >> shift) + ((a * h1[chIdx] + b * h2[chIdx] + 32) >> 6);
                    const int clamped = std::clamp(pred, -32768, 32767);
                    h2[chIdx] = h1[chIdx];
                    h1[chIdx] = clamped;
                    out.push_back(s16(clamped));
                }
            }
        }

        bool pushBody(const u8* data, size_t len) {
            if (bodyLimit > 0u) {
                if (bodyWritten >= bodyLimit) return true;
                len = std::min<size_t>(len, bodyLimit - bodyWritten);
                bodyWritten += u32(len);
            }
            if (len == 0) return true;
            blockBuf.insert(blockBuf.end(), data, data + len);

            if (channels == 1u) {
                const size_t unit = (fmt <= 1u) ? 2u : 16u;
                const size_t usable = (blockBuf.size() / unit) * unit;
                if (usable > 0) {
                    decodeChannelSlice(blockBuf.data(), usable, 0u, chPcm[0]);
                    blockBuf.erase(blockBuf.begin(), blockBuf.begin() + ptrdiff_t(usable));
                    for (s16 s : chPcm[0]) {
                        if (!emitMonoSample(s)) return false;
                    }
                }
                return true;
            }

            const size_t pairBytes = size_t(interleave) * 2u;
            size_t off = 0;
            while (blockBuf.size() - off >= pairBytes) {
                decodeChannelSlice(blockBuf.data() + off, interleave, 0u, chPcm[0]);
                decodeChannelSlice(blockBuf.data() + off + interleave, interleave, 1u, chPcm[1]);
                const size_t n = std::min(chPcm[0].size(), chPcm[1].size());
                for (size_t k = 0; k < n; ++k) {
                    if (!emitMonoSample(s16((int(chPcm[0][k]) + int(chPcm[1][k])) >> 1))) return false;
                }
                off += pairBytes;
            }
            if (off > 0) {
                blockBuf.erase(blockBuf.begin(), blockBuf.begin() + ptrdiff_t(off));
            }
            return true;
        }

        bool feedPacket(u8 subType, const u8* payload, size_t len) {
            if (!headerParsed) {
                if (subType == 1) {
                    directMode = true;
                    directHdr.insert(directHdr.end(), payload, payload + len);
                    if (directHdr.size() >= 0x800u) {
                        fmt = 2u;
                        interleave = 0x800u;
                        rate = (u32(directHdr[6]) << 8) | u32(directHdr[7]);
                        channels = directHdr[8];
                        if ((channels != 1u && channels != 2u) || rate < 4000u || rate > 192000u) return false;
                        outRate = (rate == 48000u) ? 16000u : rate;
                        headerParsed = true;
                        const size_t rem = directHdr.size() - 0x800u;
                        const bool ok = rem == 0u || pushBody(directHdr.data() + 0x800u, rem);
                        directHdr.clear();
                        return ok;
                    }
                    return true;
                }
                hdrAccum.insert(hdrAccum.end(), payload, payload + len);
                const size_t h = findTag(hdrAccum, 0, "SShd");
                if (h != std::string::npos) {
                    const size_t b = findTag(hdrAccum, h + 8u, "SSbd");
                    if (b != std::string::npos && b + 8u <= hdrAccum.size() && h + 24u <= hdrAccum.size()) {
                        fmt = readU32Le(&hdrAccum[h + 8u]);
                        rate = readU32Le(&hdrAccum[h + 12u]);
                        channels = readU32Le(&hdrAccum[h + 16u]);
                        interleave = readU32Le(&hdrAccum[h + 20u]);
                        if (!interleave) interleave = 0x800u;
                        bodyLimit = readU32Le(&hdrAccum[b + 4u]);
                        if ((fmt != 0u && fmt != 1u && fmt != 2u && fmt != 0x10u) ||
                            (channels != 1u && channels != 2u) ||
                            rate < 4000u || rate > 192000u) {
                            return false;
                        }
                        outRate = (rate == 48000u) ? 16000u : rate;
                        headerParsed = true;
                        const size_t bodyStart = b + 8u;
                        const bool ok = hdrAccum.size() <= bodyStart ||
                            pushBody(hdrAccum.data() + bodyStart, hdrAccum.size() - bodyStart);
                        hdrAccum.clear();
                        return ok;
                    }
                }
                return hdrAccum.size() <= 65536u;
            }
            return pushBody(payload, len);
        }

        bool finish() {
            if (!headerParsed) return false;
            if (channels == 2u && blockBuf.size() > interleave) {
                decodeChannelSlice(blockBuf.data(), interleave, 0u, chPcm[0]);
                decodeChannelSlice(blockBuf.data() + interleave, blockBuf.size() - interleave, 1u, chPcm[1]);
                const size_t n = std::min(chPcm[0].size(), chPcm[1].size());
                for (size_t k = 0; k < n; ++k) {
                    if (!emitMonoSample(s16((int(chPcm[0][k]) + int(chPcm[1][k])) >> 1))) return false;
                }
            }
            blockBuf.clear();
            return flushWavChunk();
        }
    } audio;
    audio.wavFile = files.wav;

    std::vector<u8> inBuf(512u * 1024u);
    size_t bufPos = 0;
    size_t bufEnd = 0;
    u64 isoCursor = isoOffset;
    const u64 pssEnd = isoOffset + u64(pssSize);

    auto ensureBytes = [&](size_t needed) -> bool {
        if (bufEnd - bufPos >= needed) return true;
        const size_t avail = bufEnd - bufPos;
        if (avail > 0u && bufPos > 0u) {
            std::memmove(inBuf.data(), inBuf.data() + bufPos, avail);
        }
        bufPos = 0;
        bufEnd = avail;
        while (bufEnd < needed && isoCursor < pssEnd) {
            const size_t space = inBuf.size() - bufEnd;
            if (space == 0u) break;
            const size_t want = size_t(std::min<u64>(pssEnd - isoCursor, std::min<size_t>(space, kIsoReadChunk)));
            if (!iso.readExact(isoCursor, inBuf.data() + bufEnd, want)) return false;
            isoCursor += want;
            bufEnd += want;
        }
        return (bufEnd - bufPos) >= needed;
    };

    char label[64];
    std::snprintf(label, sizeof(label), "Cutscene_%02u (%u/%u)", idx, idx + 1u, total);
    const unsigned totalMiB = std::max(1u, unsigned((u64(pssSize) + 1048575ull) / 1048576ull));
    u64 nextPumpCursor = isoOffset;

    while (ensureBytes(4u)) {
        const u64 processed = (isoCursor - isoOffset) - u64(bufEnd - bufPos);
        if (isoOffset + processed >= nextPumpCursor) {
            nextPumpCursor = isoOffset + processed + 2ull * 1024ull * 1024ull;
            const unsigned doneMiB = std::min(totalMiB, unsigned(processed / 1048576ull));
            if (!prog.pump("Demuxing cutscenes", label, doneMiB, totalMiB, idx, total, 1)) {
                logLauncher(logFile, "Extraction cancelled while demuxing Cutscene_%02u", idx);
                return false;
            }
        }

        const u32 tag = readU32Be(&inBuf[bufPos]);
        if (tag == 0x000001B9u) break;
        if (tag == 0x000001BAu) {
            if (!ensureBytes(14u)) break;
            const size_t skip = 14u + size_t(inBuf[bufPos + 13u] & 7u);
            if (!ensureBytes(skip)) break;
            bufPos += skip;
            continue;
        }
        if (tag == 0x000001BBu || tag == 0x000001BEu) {
            if (!ensureBytes(6u)) break;
            const size_t skip = 6u + ((size_t(inBuf[bufPos + 4u]) << 8) | size_t(inBuf[bufPos + 5u]));
            if (!ensureBytes(skip)) break;
            bufPos += skip;
            continue;
        }
        if (tag != 0x000001E0u && tag != 0x000001BDu) {
            bufPos += 1u;
            continue;
        }

        if (!ensureBytes(9u)) break;
        const size_t pesSize = (size_t(inBuf[bufPos + 4u]) << 8) | size_t(inBuf[bufPos + 5u]);
        const size_t endRel = 6u + pesSize;
        const size_t startRel = 9u + size_t(inBuf[bufPos + 8u]);
        if (endRel > inBuf.size() || !ensureBytes(endRel) || startRel > endRel) return false;

        const u8* packet = &inBuf[bufPos + startRel];
        const size_t packetLen = endRel - startRel;
        if (tag == 0x000001E0u) {
            if (packetLen > 0u && std::fwrite(packet, 1, packetLen, files.m2v) != packetLen) return false;
        } else if (packetLen > 4u) {
            if (!audio.feedPacket(packet[3], packet + 4, packetLen - 4u)) return false;
        }
        bufPos += endRel;
    }

    if (!audio.finish() || audio.wavDataBytes == 0u) return false;
    fillWavHeaderMono16(wavHdr, audio.outRate, audio.wavDataBytes);
    if (std::fflush(files.wav) != 0 || fseeko(files.wav, 0, SEEK_SET) != 0 ||
        std::fwrite(wavHdr, 1, 44, files.wav) != 44 || std::fflush(files.wav) != 0) {
        return false;
    }
    if (std::fflush(files.m2v) != 0) return false;
    std::fclose(files.m2v); files.m2v = nullptr;
    std::fclose(files.wav); files.wav = nullptr;

    if (!CaneIsValidM2vFile3ds(m2vTmp.c_str()) || !isValidWav(wavTmp)) return false;
    if (!commitTempFile(m2vTmp, m2vPath) || !commitTempFile(wavTmp, wavPath)) return false;
    files.committed = true;
    return true;
}

bool writeManifestAndStamp(const std::string& baseDir,
                           unsigned brxCount,
                           unsigned dialogCount,
                           unsigned cutsceneCount) {
    static const char* kManifest =
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"serial\": \"SCUS_971.98\",\n"
        "  \"region\": \"NTSC-U\",\n"
        "  \"levelFormat\": \"PCBRX\",\n"
        "  \"levelFormatVersion\": 17,\n"
        "  \"languages\": [\"English\"],\n"
        "  \"voiceLanguages\": [\"English\"]\n"
        "}\n";
    if (!writeAtomicText(baseDir + "/extraction_manifest.json", kManifest))
        return false;

    char stamp[256];
    std::snprintf(stamp, sizeof(stamp),
                  "serial=SCUS_971.98\nlevels=%u\nbanks=7\nmusic=38\n"
                  "dialog=%u\ncommentary=33\nambient=6\nprompt=1\ncutscenes=%u\n",
                  brxCount, dialogCount, cutsceneCount);
    return writeAtomicText(baseDir + "/assets-ready.stamp", stamp);
}

bool verifyCommentaryFromMap(const std::string& baseDir) {
    const std::string mapPath = baseDir + "/Worlds/Sounds/Commentary/CommentaryMap.txt";
    std::FILE* f = std::fopen(mapPath.c_str(), "r");
    if (!f) return false;
    char line[512];
    unsigned count = 0;
    bool allValid = true;
    while (std::fgets(line, sizeof(line), f)) {
        char wavName[128]{};
        if (std::sscanf(line, "%127s |", wavName) == 1 && wavName[0]) {
            const std::string wavPath = baseDir + "/Worlds/Sounds/Commentary/" + wavName;
            if (!isValidWav(wavPath)) {
                allValid = false;
                break;
            }
            ++count;
        }
    }
    std::fclose(f);
    return allValid && count == 33u;
}

bool verifyDialogRowsAndMaps(const std::string& baseDir,
                             const CaneLauncherLevelRef* levels,
                             unsigned levelCount,
                             const std::vector<DialogRow>& rows) {
    std::map<u32, std::string> names;
    for (unsigned i = 0; i < levelCount; ++i)
        names[levels[i].levelId] = canonicalLevelFolder(levels[i].levelId, levels[i].name);

    std::map<std::string, bool> mapsChecked;
    for (const DialogRow& row : rows) {
        auto it = names.find(row.levelId);
        const std::string lname = (it != names.end()) ? it->second : ("Level_" + std::to_string(row.levelId));
        const std::string ddir = makeDialogDir(baseDir, row.levelId, lname);
        if (!mapsChecked[ddir]) {
            if (!isValidNonEmptyFile(ddir + "/DialogMap.txt")) return false;
            mapsChecked[ddir] = true;
        }
        char wavName[64];
        std::snprintf(wavName, sizeof(wavName), "VAG_%04X_%06X.wav",
                      unsigned(row.identity), unsigned(row.sector));
        if (!isValidWav(ddir + "/" + wavName)) return false;
    }
    return true;
}

std::string findIsoPath(const std::string& baseDir) {
    static const char* kPreferred[] = {
        "Slygame.iso", "Sly.iso", "sly1.iso", "SCUS_971.98.iso"
    };
    for (const char* name : kPreferred) {
        const std::string cand = baseDir + "/" + name;
        struct stat st {};
        if (stat(cand.c_str(), &st) == 0 && S_ISREG(st.st_mode))
            return cand;
    }
    DIR* d = opendir(baseDir.c_str());
    if (!d) return {};
    std::string found;
    while (struct dirent* ent = readdir(d)) {
        if (ent->d_name[0] == '\0' || ent->d_name[0] == '.') continue;
        if (endsWithNoCase(ent->d_name, ".iso")) {
            const std::string cand = baseDir + "/" + ent->d_name;
            struct stat st {};
            if (stat(cand.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
                found = cand;
                break;
            }
        }
    }
    closedir(d);
    return found;
}

void showErrorAndWaitStart(ProgressController& prog,
                           const char* title,
                           const char* detail,
                           const char* message,
                           FILE* logFile) {
    logLauncher(logFile, "Launcher error (%s): %s", title, message);
    CaneLauncherDrawPrompt(prog.top(), prog.bottom(), prog.model(),
                           title, detail, message, "START: Exit", prog.cutscenes());
    bool released = false;
    while (aptMainLoop()) {
        hidScanInput();
        const u32 held = hidKeysHeld();
        const u32 down = hidKeysDown();
        if ((held & (KEY_START | KEY_B | KEY_A)) == 0) released = true;
        if (released && (down & (KEY_START | KEY_B))) break;
        gspWaitForVBlank();
    }
}

bool showPartialPromptAndWait(ProgressController& prog,
                              unsigned readyLevels,
                              unsigned totalLevels,
                              FILE* logFile) {
    char detail[64];
    std::snprintf(detail, sizeof(detail), "%u/%u levels ready", readyLevels, totalLevels);
    logLauncher(logFile, "Partial data prompt: %s (no ISO present)", detail);
    CaneLauncherDrawPrompt(
        prog.top(), prog.bottom(), prog.model(),
        "PARTIAL GAME DATA",
        detail,
        "Some worlds or audio files are\nmissing and no ISO was found.\n\nPut Slygame.iso in\nsd:/3ds/ThieviusCTR/ or run\nextract_all.py on PC.",
        "A: Play        B: Exit",
        prog.cutscenes());

    bool released = false;
    while (aptMainLoop()) {
        hidScanInput();
        const u32 held = hidKeysHeld();
        const u32 down = hidKeysDown();
        if ((held & (KEY_A | KEY_B | KEY_START)) == 0) released = true;
        if (released) {
            if (down & KEY_A) return true;
            if (down & (KEY_B | KEY_START)) return false;
        }
        gspWaitForVBlank();
    }
    return false;
}

struct BrxProgressCtx {
    ProgressController* prog;
    const char* detail;
    unsigned doneLevels;
    unsigned totalLevels;
};

bool onBrxTextureProgress(void* context, unsigned texDone, unsigned texTotal) {
    auto* ctx = static_cast<BrxProgressCtx*>(context);
    const unsigned texPct = texTotal > 0 ? (texDone * 100u) / texTotal : 100u;
    const unsigned scaledDone = ctx->doneLevels * 100u + texPct;
    const unsigned scaledTotal = ctx->totalLevels * 100u;
    return ctx->prog->pump("Extracting levels", ctx->detail,
                           texDone, texTotal > 0 ? texTotal : 1u,
                           scaledDone, scaledTotal > 0 ? scaledTotal : 1u, 1);
}

std::string readCstrElf(const std::vector<u8>& elf, u32 va) {
    if (va < kElfBaseVaddr) return {};
    const size_t off = size_t(va - kElfBaseVaddr + kElfFileBase);
    if (off >= elf.size()) return {};
    size_t end = off;
    const size_t limit = std::min(elf.size(), off + 256u);
    while (end < limit && elf[end] != 0) ++end;
    if (end >= limit) return {};
    return std::string(reinterpret_cast<const char*>(&elf[off]), end - off);
}

void decodeDesc(const u8* raw, u32& sector, u32& size, u32& identity) {
    u32 w[8]{};
    for (int i = 0; i < 8; ++i) w[i] = readU32Le(raw + i * 4);
    sector = w[0] ^ w[5];
    size = w[1] ^ w[7];
    identity = w[2] ^ w[6];
}

} // namespace

bool CaneLauncherEnsureAssets3ds(const char* baseDir,
                                 const CaneLauncherLevelRef* levels,
                                 unsigned levelCount,
                                 PrintConsole* top,
                                 PrintConsole* bottom,
                                 FILE* logFile) {
    if (!baseDir || !levels || levelCount == 0 || !top || !bottom) return false;
    const std::string dir(baseDir);
    const int model = CaneLauncherDetectConsoleModel();
    unsigned cutsceneCount = countValidCutscenes(dir);
    ProgressController prog(top, bottom, model, cutsceneCount);

    ensureDir(dir);
    ensureDir(dir + "/Saves");

    logLauncher(logFile, "Launcher check: console=%s (%d) cutscenes=%u/18",
                CaneLauncherConsoleName(model), model, cutsceneCount);
    prog.pump("Checking game files", "Verifying Worlds & audio...", 0, 1, 0, 1, 0, true);

    unsigned validLevels = 0;
    for (unsigned i = 0; i < levelCount; ++i) {
        const std::string lname = canonicalLevelFolder(levels[i].levelId, levels[i].name);
        if (isValidPcBrx(makeBrxPath(dir, levels[i].levelId, lname)))
            ++validLevels;
    }

    unsigned validBanks = 0;
    for (const char* bname : kBankNames) {
        if (isValidBnk(dir + "/Worlds/Sound Banks/" + bname + ".bnk", 0))
            ++validBanks;
    }

    const std::vector<MusicEntry> uniqueMusic = uniqueMusicEntries();
    unsigned validMusic = 0;
    for (const MusicEntry& m : uniqueMusic) {
        char fname[32];
        std::snprintf(fname, sizeof(fname), "MUSID_%02X.mus", unsigned(m.musid));
        if (isValidMus(dir + "/Worlds/Music/" + fname, m.size))
            ++validMusic;
    }
    const bool hasMusicMap = isValidNonEmptyFile(dir + "/Worlds/Music/MusicMap.txt");
    const bool hasPrompt = isValidWav(dir + "/Worlds/Sounds/Prompt/PromptPrk18.wav");
    unsigned validAmbient = 0;
    for (int i = 1; i <= 6; ++i) {
        char fname[64];
        std::snprintf(fname, sizeof(fname), "/Worlds/Sounds/JLO Ambient/JloAmbient_%d.wav", i);
        if (isValidWav(dir + fname)) ++validAmbient;
    }
    const bool hasCommMap = isValidNonEmptyFile(dir + "/Worlds/Sounds/Commentary/CommentaryMap.txt");
    const bool hasStamp = isValidNonEmptyFile(dir + "/assets-ready.stamp");
    const bool hasManifest = isValidNonEmptyFile(dir + "/extraction_manifest.json");

    bool fastComplete = (validLevels == levelCount) &&
                        (validBanks == 7u) &&
                        (validMusic == uniqueMusic.size()) &&
                        hasMusicMap && hasPrompt && (validAmbient == 6u) && hasCommMap;

    const std::vector<DialogRow> dialogRows = uniqueDialogRows();

    const std::string isoPath = findIsoPath(dir);
    if (fastComplete && (cutsceneCount == 18u || isoPath.empty())) {
        if (!hasStamp || !hasManifest) {
            // Older PC extraction without stamp/manifest: verify commentary & dialog once and cache stamp.
            if (verifyCommentaryFromMap(dir) &&
                verifyDialogRowsAndMaps(dir, levels, levelCount, dialogRows)) {
                writeManifestAndStamp(dir, levelCount, unsigned(dialogRows.size()), cutsceneCount);
                logLauncher(logFile, "Launcher check: verified existing assets and cached stamp");
            } else {
                fastComplete = false;
            }
        }
        if (fastComplete) {
            logLauncher(logFile, "Launcher check: fast-path ready (%u/%u levels, %u/18 cutscenes)",
                        validLevels, levelCount, cutsceneCount);
            prog.pump("Vault ready", "Starting game...", 1, 1, 1, 1, 2, true);
            return true;
        }
    }

    logLauncher(logFile,
                "Launcher check: incomplete assets (levels=%u/%u banks=%u/7 music=%u/%zu prompt=%d amb=%u/6 cutscenes=%u/18 stamp=%d)",
                validLevels, levelCount, validBanks, validMusic, uniqueMusic.size(),
                int(hasPrompt), validAmbient, cutsceneCount, int(hasStamp));

    if (isoPath.empty()) {
        const std::string splashBrx = makeBrxPath(dir, 0x002, "Splash");
        const std::string parisBrx = makeBrxPath(dir, 0x003, "Paris");
        const bool coreOk = isValidPcBrx(splashBrx) &&
                            isValidPcBrx(parisBrx) &&
                            isValidBnk(dir + "/Worlds/Sound Banks/Common.bnk", 0) &&
                            isValidBnk(dir + "/Worlds/Sound Banks/Intro.bnk", 0) &&
                            isValidMus(dir + "/Worlds/Music/MUSID_55.mus", 0);
        if (coreOk) {
            return showPartialPromptAndWait(prog, validLevels, levelCount, logFile);
        }
        showErrorAndWaitStart(
            prog,
            "SETUP REQUIRED",
            "Missing game files & disc ISO",
            "Place your legally backed-up US\nSly Cooper ISO (SCUS_971.98) at\nsd:/3ds/ThieviusCTR/Slygame.iso\n\nOr prepare files on PC with:\nplatform/3ds/extract_all.py",
            logFile);
        return false;
    }

    logLauncher(logFile, "Launcher extract: opening ISO %s", isoPath.c_str());
    prog.pump("Checking disc image", "Reading ISO9660 volume...", 0, 1, 0, 1, 1, true);

    Iso64Reader iso;
    if (!iso.open(isoPath)) {
        showErrorAndWaitStart(
            prog, "ISO OPEN FAILED", "Cannot read disc image",
            "Could not open the ISO file on\nSD card. Verify file permissions\nand free space, then relaunch.",
            logFile);
        return false;
    }

    u64 cnfOff = 0; u32 cnfSz = 0;
    u64 elfOff = 0; u32 elfSz = 0;
    if (!iso.findRootFile("SYSTEM.CNF", cnfOff, cnfSz) || cnfSz == 0 || cnfSz > 4096u ||
        !iso.findRootFile("SCUS_971.98", elfOff, elfSz) || elfSz < 0x170000u || elfSz > 16u * 1024u * 1024u) {
        showErrorAndWaitStart(
            prog, "UNSUPPORTED ISO", "Requires US SCUS_971.98 disc",
            "This disc image does not contain\nSYSTEM.CNF / SCUS_971.98.\nPlease use a valid US retail\nSly Cooper ISO.",
            logFile);
        return false;
    }

    std::string cnf(cnfSz, '\0');
    if (!iso.readExact(cnfOff, cnf.data(), cnf.size()) ||
        cnf.find("SCUS_971.98") == std::string::npos) {
        showErrorAndWaitStart(
            prog, "UNSUPPORTED REGION", "Requires US SCUS_971.98",
            "SYSTEM.CNF does not match\nSCUS_971.98. Only the US retail\ndisc is supported.",
            logFile);
        return false;
    }

    std::vector<u8> elf(elfSz);
    if (!iso.readExact(elfOff, elf.data(), elf.size()) ||
        std::memcmp(elf.data(), "\x7F" "ELF", 4) != 0) {
        showErrorAndWaitStart(
            prog, "INVALID DISC EXECUTABLE", "Corrupt SCUS_971.98",
            "SCUS_971.98 failed ELF validation.\nYour ISO dump may be incomplete.",
            logFile);
        return false;
    }

    // Check SD free space (non-fatal warning if query fails under some emulators).
    FS_ArchiveResource sdRes{};
    if (R_SUCCEEDED(FSUSER_GetSdmcArchiveResource(&sdRes))) {
        const u64 freeBytes = u64(sdRes.freeClusters) * u64(sdRes.clusterSize);
        const u64 neededLevels = u64(levelCount > validLevels ? (levelCount - validLevels) : 0) * 32ull * 1024ull * 1024ull;
        const u64 neededAudio = hasStamp ? 0ull : 280ull * 1024ull * 1024ull;
        const u64 neededCutscenes = u64(cutsceneCount < 18u ? (18u - cutsceneCount) : 0u) * 64ull * 1024ull * 1024ull;
        const u64 estNeeded = neededLevels + neededAudio + neededCutscenes;
        logLauncher(logFile, "SD free space: %llu MiB (estimated need: %llu MiB)",
                    (unsigned long long)(freeBytes / 1048576ull),
                    (unsigned long long)(estNeeded / 1048576ull));
        if (estNeeded > 0 && freeBytes < estNeeded / 2u && freeBytes < 128ull * 1024ull * 1024ull) {
            char msg[256];
            std::snprintf(msg, sizeof(msg),
                          "Low SD free space: %llu MiB.\nNeed up to %llu MiB free for\nextraction. Free space on SD\nand relaunch.",
                          (unsigned long long)(freeBytes / 1048576ull),
                          (unsigned long long)(estNeeded / 1048576ull));
            showErrorAndWaitStart(prog, "LOW SD SPACE", "Not enough free SD space", msg, logFile);
            return false;
        }
    }

    prog.pump("Checking disc image", "SCUS_971.98 verified", 1, 1, 1, 1, 1, true);

    // Parse 46 levels from ELF @ 0x148AF0, ordered to match g_levelTable (Splash, Paris, Hideout first).
    struct RawDiscLevel {
        u32 id;
        std::string name;
        u64 offset;
        u32 compSize;
    };
    std::vector<RawDiscLevel> discLevels;
    discLevels.reserve(46);
    std::map<u32, std::string> dialogLevelNames;
    constexpr size_t kLevelTableOff = 0x148af0u;
    for (unsigned i = 0; i < 46u; ++i) {
        if (kLevelTableOff + (i + 1u) * 44u > elf.size()) {
            showErrorAndWaitStart(prog, "ISO ERROR", "Truncated ELF level table",
                                  "SCUS_971.98 level table truncated.", logFile);
            return false;
        }
        const u8* row = &elf[kLevelTableOff + i * 44u];
        u32 w[11]{};
        for (int j = 0; j < 11; ++j) w[j] = readU32Le(row + j * 4);
        const u32 ident = w[8];
        std::string rawName = readCstrElf(elf, w[9]);
        if (rawName.empty() || rawName.find('/') != std::string::npos ||
            rawName.find('\\') != std::string::npos || rawName == "." || rawName == "..") {
            showErrorAndWaitStart(prog, "ISO ERROR", "Invalid level descriptor",
                                  "Invalid level name in SCUS_971.98.", logFile);
            return false;
        }
        const std::string sanitized = canonicalLevelFolder(ident, rawName.c_str());
        if (dialogLevelNames.find(ident) == dialogLevelNames.end())
            dialogLevelNames[ident] = sanitized;
        const u64 offset = u64(w[0] ^ w[5]) * kSectorBytes;
        const u32 compSize = w[1] ^ w[7];
        if (compSize == 0 || compSize > 256u * 1024u * 1024u || offset > iso.size() ||
            u64(compSize) > iso.size() - offset) {
            showErrorAndWaitStart(prog, "ISO ERROR", "Level outside ISO",
                                  "Level offsets point past end of ISO.", logFile);
            return false;
        }
        discLevels.push_back({ident, sanitized, offset, compSize});
    }
    dialogLevelNames[3] = "Paris";
    dialogLevelNames[4] = "The Hideout";

    std::vector<LevelTask> orderedTasks;
    orderedTasks.reserve(discLevels.size());
    for (unsigned i = 0; i < levelCount; ++i) {
        for (const auto& d : discLevels) {
            if (d.id == levels[i].levelId) {
                orderedTasks.push_back({d.id, d.name, d.offset, d.compSize,
                                        makeBrxPath(dir, d.id, d.name), d.name});
                break;
            }
        }
    }

    // Step A: Extract missing/invalid BRX levels.
    {
        LinearScratch rawScratch;
        LinearScratch compScratch;
        unsigned doneLevels = 0;
        for (const LevelTask& task : orderedTasks) {
            if (isValidPcBrx(task.brxPath)) {
                ++doneLevels;
                continue;
            }
            ensureParentDir(task.brxPath);
            const std::string tmpBrx = task.brxPath + ".tmp";
            std::remove(tmpBrx.c_str());

            std::string detail = task.name + " (" + std::to_string(doneLevels + 1) + "/" +
                                 std::to_string(orderedTasks.size()) + ")";
            logLauncher(logFile, "Extracting level 0x%03X: %s (%u bytes compressed)",
                        unsigned(task.id), task.name.c_str(), unsigned(task.compSize));

            if (!compScratch.allocAtLeast(task.compSize) ||
                !rawScratch.allocAtLeast(kMaxRawLevelBytes)) {
                showErrorAndWaitStart(prog, "MEMORY ERROR", task.name.c_str(),
                                      "Insufficient linear RAM to extract\nthis level.", logFile);
                return false;
            }

            // Read compressed level from ISO in chunks so B: Cancel responds smoothly.
            size_t readDone = 0;
            while (readDone < task.compSize) {
                const size_t chunk = std::min<size_t>(task.compSize - readDone, kIsoReadChunk);
                if (!iso.readExact(task.offset + readDone, compScratch.ptr + readDone, chunk)) {
                    showErrorAndWaitStart(prog, "ISO READ ERROR", task.name.c_str(),
                                          "Failed reading level data from\nISO. Check the ISO file.", logFile);
                    return false;
                }
                readDone += chunk;
                if (!prog.pump("Extracting levels", detail.c_str(),
                               unsigned(readDone / 1024u),
                               unsigned(std::max<size_t>(1, task.compSize / 1024u)),
                               doneLevels * 100u, unsigned(orderedTasks.size() * 100u), 1)) {
                    logLauncher(logFile, "Extraction cancelled during ISO read of %s", task.name.c_str());
                    return false;
                }
            }

            try {
                const size_t rawLen = decompressLzss(
                    compScratch.ptr, task.compSize, rawScratch.ptr, kMaxRawLevelBytes,
                    prog, detail.c_str(), doneLevels * 100u, unsigned(orderedTasks.size() * 100u));
                // Free compressed scratch before BRX rebuilding to maximize heap headroom.
                compScratch.reset();

                BrxProgressCtx ctx{&prog, detail.c_str(), doneLevels, unsigned(orderedTasks.size())};
                BrxWalker walker(StringEncoding::Narrow, BrxSourceRegion::NtscU);
                walker.WalkMemory(rawScratch.ptr, rawLen, tmpBrx, onBrxTextureProgress, &ctx);

                if (!isValidPcBrx(tmpBrx) || !commitTempFile(tmpBrx, task.brxPath))
                    throw std::runtime_error("Failed validating or saving " + task.name);
            } catch (const std::exception& e) {
                std::remove(tmpBrx.c_str());
                if (std::strcmp(e.what(), "CANCELLED") == 0 || prog.cancelled()) {
                    logLauncher(logFile, "Extraction cancelled by user during %s", task.name.c_str());
                    return false;
                }
                logLauncher(logFile, "Level extraction failed (%s): %s", task.name.c_str(), e.what());
                showErrorAndWaitStart(prog, "EXTRACTION ERROR", task.name.c_str(), e.what(), logFile);
                return false;
            }
            ++doneLevels;
        }
    }

    // Step B: Sound Banks (7) & Music (38 unique MUSIDs + MusicMap.txt).
    const unsigned totalMusicAndBanks = 7u + unsigned(uniqueMusic.size()) + 1u;
    unsigned mbDone = 0;
    {
        const std::string bankDir = dir + "/Worlds/Sound Banks";
        ensureDir(bankDir);
        const u32 bankTableOff = 0x00244F40u - kElfBaseVaddr + kElfFileBase;
        std::vector<u8> buf;
        for (unsigned i = 0; i < 7u; ++i) {
            if (!prog.pump("Extracting music & banks", kBankNames[i],
                           mbDone, totalMusicAndBanks, mbDone, totalMusicAndBanks, 1)) {
                return false;
            }
            const std::string bnkPath = bankDir + "/" + kBankNames[i] + ".bnk";
            u32 sec = 0, sz = 0, ident = 0;
            decodeDesc(&elf[bankTableOff + i * 32u], sec, sz, ident);
            const u64 off = u64(sec) * kSectorBytes;
            if (!isValidBnk(bnkPath, sz)) {
                if (sz == 0 || sz > 8u * 1024u * 1024u || off > iso.size() || u64(sz) > iso.size() - off) {
                    showErrorAndWaitStart(prog, "AUDIO ERROR", kBankNames[i], "Invalid sound bank offset in ISO.", logFile);
                    return false;
                }
                buf.resize(sz);
                if (!iso.readExact(off, buf.data(), sz) || readU32Le(buf.data()) != 3u ||
                    !writeAtomicBytes(bnkPath, buf.data(), sz)) {
                    showErrorAndWaitStart(prog, "AUDIO ERROR", kBankNames[i], "Failed extracting sound bank.", logFile);
                    return false;
                }
                logLauncher(logFile, "Extracted sound bank: %s.bnk (%u bytes)", kBankNames[i], unsigned(sz));
            }
            ++mbDone;
        }

        const std::string musicDir = dir + "/Worlds/Music";
        ensureDir(musicDir);
        std::string musicMap;
        musicMap.reserve(4096);
        for (const MusicEntry& m : kMusicTable) {
            char line[160];
            std::snprintf(line, sizeof(line), "%s | MUSID 0x%X | sector 0x%X | bytes 0x%X\n",
                          m.levelName, unsigned(m.musid), unsigned(m.sector), unsigned(m.size));
            musicMap += line;
        }
        if (!isValidNonEmptyFile(musicDir + "/MusicMap.txt")) {
            if (!writeAtomicText(musicDir + "/MusicMap.txt", musicMap)) {
                showErrorAndWaitStart(prog, "WRITE ERROR", "MusicMap.txt", "Cannot write MusicMap.txt to SD.", logFile);
                return false;
            }
        }
        ++mbDone;

        for (const MusicEntry& m : uniqueMusic) {
            char fname[32];
            std::snprintf(fname, sizeof(fname), "MUSID_%02X.mus", unsigned(m.musid));
            if (!prog.pump("Extracting music & banks", fname,
                           mbDone, totalMusicAndBanks, mbDone, totalMusicAndBanks, 1)) {
                return false;
            }
            const std::string musPath = musicDir + "/" + fname;
            if (!isValidMus(musPath, m.size)) {
                const u64 off = u64(m.sector) * kSectorBytes;
                if (m.size == 0 || m.size > 8u * 1024u * 1024u || off > iso.size() || u64(m.size) > iso.size() - off) {
                    showErrorAndWaitStart(prog, "MUSIC ERROR", fname, "Music track outside ISO.", logFile);
                    return false;
                }
                buf.resize(m.size);
                if (!iso.readExact(off, buf.data(), m.size) ||
                    !writeAtomicBytes(musPath, buf.data(), m.size)) {
                    showErrorAndWaitStart(prog, "MUSIC ERROR", fname, "Failed writing music track to SD.", logFile);
                    return false;
                }
            }
            ++mbDone;
        }
        logLauncher(logFile, "Verified music & sound banks (%zu music tracks, 7 banks)", uniqueMusic.size());
    }

    // Step C: Dialog WAVs (1,368 clips + per-level DialogMap.txt).
    {
        std::map<std::string, std::string> dialogMaps;
        std::vector<u8> vagBuf;
        std::vector<s16> pcmBuf;
        unsigned decodedNow = 0;
        const unsigned totalDialogs = unsigned(dialogRows.size());

        for (unsigned idx = 0; idx < totalDialogs; ++idx) {
            const DialogRow& row = dialogRows[idx];
            const auto nameIt = dialogLevelNames.find(row.levelId);
            const std::string lname = nameIt != dialogLevelNames.end()
                ? nameIt->second
                : ("Level_" + std::to_string(row.levelId));
            const std::string ddir = makeDialogDir(dir, row.levelId, lname);
            char wavName[64];
            std::snprintf(wavName, sizeof(wavName), "VAG_%04X_%06X.wav",
                          unsigned(row.identity), unsigned(row.sector));
            const std::string wavPath = ddir + "/" + wavName;

            u32 sz = row.size;
            if (sz == 0) {
                u8 hdr[48]{};
                if (!iso.readExact(u64(row.sector) * kSectorBytes, hdr, 48)) {
                    showErrorAndWaitStart(prog, "DIALOG ERROR", wavName, "Failed reading VAG header.", logFile);
                    return false;
                }
                sz = 48u + readU32Be(hdr + 12);
            }

            if ((idx & 15u) == 0u || idx + 1u == totalDialogs) {
                if (!prog.pump("Decoding voice clips", lname.c_str(),
                               idx, totalDialogs, idx, totalDialogs, 1)) {
                    return false;
                }
            }

            if (!isValidWav(wavPath)) {
                const u64 off = u64(row.sector) * kSectorBytes;
                if (sz < 48u || sz > 8u * 1024u * 1024u || off > iso.size() || u64(sz) > iso.size() - off) {
                    showErrorAndWaitStart(prog, "DIALOG ERROR", wavName, "Voice clip outside ISO bounds.", logFile);
                    return false;
                }
                vagBuf.resize(sz);
                if (!iso.readExact(off, vagBuf.data(), sz) ||
                    !decodeVagToWavFile(vagBuf.data(), sz, wavPath, pcmBuf)) {
                    showErrorAndWaitStart(prog, "DIALOG ERROR", wavName, "Failed decoding voice WAV.", logFile);
                    return false;
                }
                ++decodedNow;
            }

            char line[192];
            std::snprintf(line, sizeof(line),
                          "%s | levelId 0x%X | identity 0x%X | sector 0x%X | bytes 0x%X | source %s\n",
                          wavName, unsigned(row.levelId), unsigned(row.identity),
                          unsigned(row.sector), unsigned(sz),
                          row.source == DialogAudioSource::ASEG ? "ASEG" : "DIALOG");
            dialogMaps[ddir] += line;
        }

        for (const auto& kv : dialogMaps) {
            const std::string mapPath = kv.first + "/DialogMap.txt";
            if (!isValidNonEmptyFile(mapPath)) {
                if (!writeAtomicText(mapPath, kv.second)) {
                    showErrorAndWaitStart(prog, "WRITE ERROR", "DialogMap.txt", "Failed writing DialogMap.txt.", logFile);
                    return false;
                }
            }
        }
        logLauncher(logFile, "Verified dialog clips: total=%u newlyDecoded=%u", totalDialogs, decodedNow);
    }

    // Step D: Commentary (33), JLO Ambient (6), Prompt (1).
    {
        std::vector<u8> vagBuf;
        std::vector<s16> pcmBuf;
        const unsigned totalExtra = 41u;
        unsigned extraDone = 0;

        const std::string commDir = dir + "/Worlds/Sounds/Commentary";
        ensureDir(commDir);
        const u32 commBase = 0x00248748u - kElfBaseVaddr + kElfFileBase;
        std::string commMap;
        commMap.reserve(4096);
        for (unsigned i = 0; i < 33u; ++i) {
            const u32 off = commBase + i * 44u;
            const u32 levelId = readU32Le(&elf[off]);
            const float dur = readF32Le(&elf[off + 4]);
            u32 sec = 0, sz = 0, ident = 0;
            decodeDesc(&elf[off + 8], sec, sz, ident);
            const u32 speakerVa = readU32Le(&elf[off + 40]);
            if (!prog.pump("Decoding extra audio", "Developer commentary",
                           extraDone, totalExtra, extraDone, totalExtra, 1)) {
                return false;
            }
            if (sec && sz) {
                char wavName[64];
                std::snprintf(wavName, sizeof(wavName), "VAG_%04X_%06X.wav",
                              unsigned(ident), unsigned(sec));
                const std::string wavPath = commDir + "/" + wavName;
                if (!isValidWav(wavPath)) {
                    const u64 dOff = u64(sec) * kSectorBytes;
                    if (sz > 8u * 1024u * 1024u || dOff > iso.size() || u64(sz) > iso.size() - dOff) {
                        showErrorAndWaitStart(prog, "COMMENTARY ERROR", wavName, "Commentary clip outside ISO.", logFile);
                        return false;
                    }
                    vagBuf.resize(sz);
                    if (!iso.readExact(dOff, vagBuf.data(), sz) ||
                        !decodeVagToWavFile(vagBuf.data(), sz, wavPath, pcmBuf)) {
                        showErrorAndWaitStart(prog, "COMMENTARY ERROR", wavName, "Failed decoding commentary WAV.", logFile);
                        return false;
                    }
                }
                std::string speakers = readCstrElf(elf, speakerVa);
                if (speakers.empty()) speakers = "Unknown";
                for (char& ch : speakers) {
                    if (ch == '\r' || ch == '\n') ch = ' ';
                }
                char line[320];
                std::snprintf(line, sizeof(line),
                              "%s | levelId 0x%X | duration %.6g | identity 0x%X | sector 0x%X | bytes 0x%X | speakers %s\n",
                              wavName, unsigned(levelId), double(dur), unsigned(ident),
                              unsigned(sec), unsigned(sz), speakers.c_str());
                commMap += line;
            }
            ++extraDone;
        }
        if (!isValidNonEmptyFile(commDir + "/CommentaryMap.txt")) {
            if (!writeAtomicText(commDir + "/CommentaryMap.txt", commMap)) {
                showErrorAndWaitStart(prog, "WRITE ERROR", "CommentaryMap.txt", "Failed writing CommentaryMap.txt.", logFile);
                return false;
            }
        }
        ++extraDone;

        const std::string ambDir = dir + "/Worlds/Sounds/JLO Ambient";
        ensureDir(ambDir);
        const u32 ambBase = 0x002482D8u - kElfBaseVaddr + kElfFileBase;
        for (unsigned i = 0; i < 6u; ++i) {
            u32 sec = 0, sz = 0, ident = 0;
            decodeDesc(&elf[ambBase + i * 32u], sec, sz, ident);
            char wavName[32];
            std::snprintf(wavName, sizeof(wavName), "JloAmbient_%u.wav", i + 1u);
            if (!prog.pump("Decoding extra audio", wavName,
                           extraDone, totalExtra, extraDone, totalExtra, 1)) {
                return false;
            }
            const std::string wavPath = ambDir + "/" + wavName;
            if (!isValidWav(wavPath)) {
                const u64 dOff = u64(sec) * kSectorBytes;
                if (sz == 0 || sz > 8u * 1024u * 1024u || dOff > iso.size() || u64(sz) > iso.size() - dOff) {
                    showErrorAndWaitStart(prog, "AUDIO ERROR", wavName, "Ambient clip outside ISO.", logFile);
                    return false;
                }
                vagBuf.resize(sz);
                if (!iso.readExact(dOff, vagBuf.data(), sz) ||
                    !decodeVagToWavFile(vagBuf.data(), sz, wavPath, pcmBuf)) {
                    showErrorAndWaitStart(prog, "AUDIO ERROR", wavName, "Failed decoding ambient WAV.", logFile);
                    return false;
                }
            }
            ++extraDone;
        }

        const std::string promptDir = dir + "/Worlds/Sounds/Prompt";
        ensureDir(promptDir);
        u32 pSec = 0, pSz = 0, pIdent = 0;
        decodeDesc(&elf[0x0026A118u - kElfBaseVaddr + kElfFileBase], pSec, pSz, pIdent);
        if (!prog.pump("Decoding extra audio", "PromptPrk18.wav",
                       extraDone, totalExtra, extraDone, totalExtra, 1)) {
            return false;
        }
        const std::string promptPath = promptDir + "/PromptPrk18.wav";
        if (!isValidWav(promptPath)) {
            const u64 dOff = u64(pSec) * kSectorBytes;
            if (pSz == 0 || pSz > 8u * 1024u * 1024u || dOff > iso.size() || u64(pSz) > iso.size() - dOff) {
                showErrorAndWaitStart(prog, "AUDIO ERROR", "PromptPrk18.wav", "Prompt clip outside ISO.", logFile);
                return false;
            }
            vagBuf.resize(pSz);
            if (!iso.readExact(dOff, vagBuf.data(), pSz) ||
                !decodeVagToWavFile(vagBuf.data(), pSz, promptPath, pcmBuf)) {
                showErrorAndWaitStart(prog, "AUDIO ERROR", "PromptPrk18.wav", "Failed decoding prompt WAV.", logFile);
                return false;
            }
        }
    }

    // Step E: Demux missing PS2 cutscenes into raw MPEG-2 (.m2v) + 16 kHz mono WAV.
    {
        const std::string cutsceneDir = dir + "/Cutscenes";
        ensureDir(cutsceneDir);
        const u32 movieTableOff = 0x00248490u - kElfBaseVaddr + kElfFileBase;
        if (movieTableOff + 17u * 32u > elf.size()) {
            showErrorAndWaitStart(prog, "ISO ERROR", "Truncated movie table",
                                  "SCUS_971.98 movie table truncated.", logFile);
            return false;
        }
        struct MovieDesc { u32 sector; u32 size; };
        MovieDesc movieDescs[18]{};
        for (unsigned i = 0; i < 17u; ++i) {
            u32 sec = 0, sz = 0, ident = 0;
            decodeDesc(&elf[movieTableOff + i * 32u], sec, sz, ident);
            const unsigned outIdx = (i == 0u) ? 16u : (i - 1u);
            movieDescs[outIdx] = { sec, sz };
        }
        movieDescs[17] = { 0xC8F03u, 0xAB4004u };

        for (unsigned idx = 0; idx < 18u; ++idx) {
            if (hasValidCutscene(dir, int(idx))) continue;

            char m2vName[256], wavName[256];
            std::snprintf(m2vName, sizeof(m2vName), "%s/Cutscene_%02u.m2v", cutsceneDir.c_str(), idx);
            std::snprintf(wavName, sizeof(wavName), "%s/Cutscene_%02u.wav", cutsceneDir.c_str(), idx);

            if (idx == 17u && isValidM2vPair(dir, 16)) {
                if (!prog.pump("Demuxing cutscenes", "Cutscene_17 (splash copy)", 0, 1, idx, 18u, 1, true)) {
                    return false;
                }
                if (!copyFileAtomic(cutsceneDir + "/Cutscene_16.m2v", m2vName) ||
                    !copyFileAtomic(cutsceneDir + "/Cutscene_16.wav", wavName)) {
                    showErrorAndWaitStart(prog, "WRITE ERROR", "Cutscene_17",
                                          "Failed copying Cutscene_17 from splash.", logFile);
                    return false;
                }
            } else {
                const MovieDesc& md = movieDescs[idx];
                const u64 pssOff = u64(md.sector) * kSectorBytes;
                if (md.size == 0u || pssOff > iso.size() || u64(md.size) > iso.size() - pssOff) {
                    char msg[128];
                    std::snprintf(msg, sizeof(msg), "Cutscene_%02u descriptor outside ISO.", idx);
                    showErrorAndWaitStart(prog, "ISO ERROR", "Invalid cutscene offset", msg, logFile);
                    return false;
                }
                logLauncher(logFile, "Demuxing Cutscene_%02u (sector=0x%X size=%u bytes)",
                            idx, unsigned(md.sector), unsigned(md.size));
                if (!demuxPssToM2vAndWav(iso, pssOff, md.size, m2vName, wavName, prog, idx, 18u, logFile)) {
                    if (prog.cancelled()) return false;
                    char msg[128];
                    std::snprintf(msg, sizeof(msg), "Failed demuxing Cutscene_%02u from ISO.", idx);
                    showErrorAndWaitStart(prog, "DEMUX ERROR", "Cutscene demux failed", msg, logFile);
                    return false;
                }
            }
            cutsceneCount = countValidCutscenes(dir);
            prog.setCutscenes(cutsceneCount);
        }
    }

    cutsceneCount = countValidCutscenes(dir);
    prog.setCutscenes(cutsceneCount);
    prog.pump("Finalizing setup", "Writing manifest...", 0, 1, 0, 1, 1, true);
    if (!writeManifestAndStamp(dir, unsigned(orderedTasks.size()), unsigned(dialogRows.size()), cutsceneCount)) {
        showErrorAndWaitStart(prog, "WRITE ERROR", "extraction_manifest.json",
                              "Failed writing manifest or stamp to SD.", logFile);
        return false;
    }

    logLauncher(logFile, "On-console ISO extraction complete (%zu levels, %zu music, 7 banks, %zu dialogs, %u cutscenes)",
                orderedTasks.size(), uniqueMusic.size(), dialogRows.size(), cutsceneCount);
    prog.pump("Vault ready", "Starting game...", 1, 1, 1, 1, 2, true);
    return true;
}

void CaneLauncherRestoreTopDisplay3ds(PrintConsole* bottom) {
    gfxSetScreenFormat(GFX_TOP, GSP_BGR8_OES);
    gfxSetDoubleBuffering(GFX_TOP, true);
    for (unsigned i = 0; i < 2u; ++i) {
        u16 w = 0, h = 0;
        u8* fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &w, &h);
        if (fb && w > 0 && h > 0) std::memset(fb, 0, size_t(w) * h * 3u);
        gfxFlushBuffers();
        gfxScreenSwapBuffers(GFX_TOP, false);
        gspWaitForVBlank();
    }
    if (bottom) {
        consoleSelect(bottom);
        consoleClear();
        gfxFlushBuffers();
    }
}
