// The SD filesystem does not provide POSIX rename-over-existing semantics.
// Keep the old file until the replacement has been installed successfully.
#include <cstdio>
#include <cerrno>
#include <string>

namespace cane_save3ds {
bool Recover(const std::string& path)
{
    struct stat info{};
    if (::stat(path.c_str(), &info) == 0) return true;
    if (errno != ENOENT) return false;
    const std::string backup = path + ".bak";
    if (::stat(backup.c_str(), &info) != 0) return errno == ENOENT;
    return std::rename(backup.c_str(), path.c_str()) == 0;
}

bool Replace(const std::string& temporary, const std::string& path)
{
    if (!Recover(path)) return false;
    struct stat info{};
    const bool existed = ::stat(path.c_str(), &info) == 0;
    if (!existed && errno != ENOENT) return false;
    const std::string backup = path + ".bak";
    if (existed) {
        if (std::remove(backup.c_str()) != 0 && errno != ENOENT) return false;
        if (std::rename(path.c_str(), backup.c_str()) != 0) return false;
    }
    if (std::rename(temporary.c_str(), path.c_str()) != 0) {
        const int error = errno;
        if (existed) std::rename(backup.c_str(), path.c_str());
        errno = error;
        std::fprintf(stderr, "Save replacement failed: %s (errno=%d)\n", path.c_str(), error);
        return false;
    }
    if (existed) std::remove(backup.c_str());
    return true;
}
}
