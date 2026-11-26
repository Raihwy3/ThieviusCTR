// Shared by the runtime and host persistence checks.
#include <cstdio>
namespace trophies::record
{
inline bool Read(FILE *file, uint64_t &result)
{
    unsigned version = 0;
    unsigned long long bits = 0, check = 0;
    const int count = std::fscanf(file, "THIEVIUS-TROPHIES %u %llx %llx", &version, &bits, &check);
    if (count != 3 || version != 1 || (bits ^ check) != ~0ull || (bits & ~All))
        return false;
    result = bits;
    return true;
}
inline bool Write(FILE *file, uint64_t bits)
{
    return std::fprintf(file, "THIEVIUS-TROPHIES 1 %016llx %016llx\n", (unsigned long long)bits,
                        (unsigned long long)~bits) > 0;
}
} // namespace trophies::record
