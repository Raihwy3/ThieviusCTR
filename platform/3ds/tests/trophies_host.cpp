#include "../../../Sly1-Release/trophies.h"
#include "../../../Sly1-Release/trophy_record.inc"
#include "../../../Sly1-Release/trophy_ui_state.inc"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace trophies;
static bool has(uint64_t mask, int i)
{
    return mask & (uint64_t(1) << i);
}
int main()
{
    assert(ui::Playing(3,-1)); // ordinary game uses UIS_Nil as its base
    assert(ui::Playing(5,-1)); // paused gameplay
    assert(ui::Playing(2,2)); // hideout
    assert(!ui::Playing(1,1)); // title demo
    assert(!ui::Playing(5,1)); // menu opened over title demo
    assert(!ui::Playing(0,-1)); // startup
    assert(!ui::Playing(8,-1)); // world transition

    int tiers[4] = {};
    for (auto &d : definitions)
        ++tiers[d.tier];
    assert(tiers[Bronze] == 22 && tiers[Silver] == 8 && tiers[Gold] == 5 && tiers[Platinum] == 1);
    Snapshot s;
    s.vault = 0xffffffff;
    s.coins = 100;
    s.visitedWorlds = s.completeWorlds = 63;
    s.allBottles = true;
    assert(Evaluate(s) == 0); // attract mode never earns trophies
    s.playing = true;
    assert(Merge(0, Evaluate(s)) == All);
    s = {};
    s.playing = true;
    s.coins = 59;
    assert(has(Evaluate(s), 2) && !has(Evaluate(s), 3));
    s.coins = 60;
    assert(has(Evaluate(s), 3) && !has(Evaluate(s), 4));
    s.coins = 99;
    uint64_t earned = Merge(0, Evaluate(s));
    assert(has(earned, 4));
    s.coins = 0;
    assert(has(Merge(earned, Evaluate(s)), 4));
    s = {};
    s.playing = true;
    s.vault = 0x10000;
    assert(has(Evaluate(s), 11) && !has(Evaluate(s), 28));
    s.vault = 0x12000;
    assert(has(Evaluate(s), 28));
    s.vault = 0x1008;
    assert(has(Evaluate(s), 25));
    s.vault = 0x12;
    assert(has(Evaluate(s), 35));
    s.vault = 0xf0000000;
    assert(has(Evaluate(s), 30) && !has(Evaluate(s), 32));
    s.vault = 0xf000ffff;
    assert(has(Evaluate(s), 32));
    assert(Merge(All ^ 3, 2) == All);
    assert(Merge(0, 1ull << 63) == 0);
    s = {};
    s.playing = true;
    s.completeWorlds = 1 << 2;
    assert(has(Evaluate(s), 22) && !has(Evaluate(s), 21));
    s.visitedWorlds = 1 << 3;
    assert(has(Evaluate(s), 29) && !has(Evaluate(s), 27));
    FILE *file = std::tmpfile();
    assert(file);
    assert(record::Write(file, earned));
    std::rewind(file);
    uint64_t restored = 0;
    assert(record::Read(file, restored) && restored == earned);
    std::fclose(file);
    const char *bad[] = {"", "THIEVIUS-TROPHIES 1 1", "THIEVIUS-TROPHIES 2 0 ffffffffffffffff",
                         "THIEVIUS-TROPHIES 1 1 ffffffffffffffff",
                         "THIEVIUS-TROPHIES 1 8000000000000000 7fffffffffffffff"};
    for (const char *text : bad)
    {
        file = std::tmpfile();
        assert(file);
        std::fputs(text, file);
        std::rewind(file);
        restored = 123;
        assert(!record::Read(file, restored) && restored == 123);
        std::fclose(file);
    }
    puts("Trophy conditions, tiers, completion and damaged-save checks passed.");
}
