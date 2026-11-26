#pragma once
#include <cstdint>
namespace trophies {
constexpr int Count=36;
enum Tier { Bronze, Silver, Gold, Platinum };
struct Definition { const char* title; const char* description; Tier tier; uint32_t vault; };
inline constexpr Definition definitions[Count]={
{"True Thievius Raccoonus","Earn every other trophy.",Platinum,0},
{"Foxy Lady","Recover Carmelita's police file in Paris.",Bronze,1},
{"Coin Collector","Collect a coin.",Bronze,0},
{"Coin Recycler","Reach 60 coins.",Bronze,0},
{"Coins, Coins Everywhere!","Reach 99 coins.",Bronze,0},
{"Snowy Blueprints","Find the Fire in the Sky blueprint.",Bronze,0x10000000},
{"Programmers Can Do Anything","Find the Tide of Terror blueprint.",Bronze,0x20000000},
{"French Bulldog Style","Find the Sunset Snake Eyes blueprint.",Bronze,0x40000000},
{"Top Of The Morning","Learn the explosive hat move.",Bronze,0x40},
{"Sucker Punched!","Start playing ThieviusCTR.",Bronze,0},
{"Slytunkhamen Approves","Learn the improved shadow move.",Bronze,0x2000},
{"Run Like An Egyptian","Learn the shadow technique.",Bronze,0x10000},
{"Dive Bomb","Learn the fast dive move.",Bronze,2},
{"Slow Slacker","Learn the improved slow-motion move.",Bronze,0x1000},
{"Take It Slow","Learn the slow-motion jump.",Bronze,8},
{"Now Ya See Me","Learn to create a decoy.",Bronze,0x200},
{"Coin Sucker","Learn the coin magnet move.",Bronze,0x20},
{"Spikes Suck","Learn the anti-gravity technique.",Bronze,0x4000},
{"Singing Gator","Find the Vicious Voodoo blueprint.",Bronze,0x80000000},
{"Freeze, You're It","Learn the speed clock move.",Bronze,0x80},
{"Waterlogged","Learn the water safety technique.",Bronze,0x100},
{"Frog Legs","Defeat Raleigh.",Silver,0},
{"Giving The Dog A Bone","Defeat Muggshot.",Silver,0},
{"Dance, Dance, Ruby","Defeat Mz. Ruby.",Silver,0},
{"Pandas Aren't Always Cute","Defeat the Panda King.",Silver,0},
{"Time Stopper","Learn both slow-motion upgrades.",Silver,0x1008},
{"Learn The Ropes","Begin the Tide of Terror adventure.",Bronze,0},
{"Pug Gambling","Enter Sunset Snake Eyes.",Bronze,0},
{"Complete Darkness","Learn both shadow techniques.",Silver,0x12000},
{"Bayou Lily","Enter Vicious Voodoo.",Silver,0},
{"Meeting The Ancestors","Collect all four blueprints.",Gold,0xf0000000},
{"Clash Of The Clockwerk","Reach Cold Heart of Hate.",Silver,0},
{"Greedy Raccoon","Open every vault.",Gold,0xf000ffff},
{"Oh No He Didn't","Defeat Clockwerk.",Gold,0},
{"Bottle Hoarder","Collect every clue bottle in one level.",Gold,0},
{"Nimble Like A Thief","Learn both dive upgrades.",Gold,0x12}
};
constexpr uint64_t All=(uint64_t(1)<<Count)-1;
struct Snapshot { uint32_t vault=0; unsigned completeWorlds=0,visitedWorlds=0; int coins=0; bool allBottles=false,playing=false; };
inline uint64_t Evaluate(const Snapshot& s) {
 if(!s.playing)return 0;
 uint64_t bits=uint64_t(1)<<9;
 for(int i=1;i<Count;++i)if(definitions[i].vault && (s.vault & definitions[i].vault)==definitions[i].vault)bits|=uint64_t(1)<<i;
 if(s.coins>=1)bits|=uint64_t(1)<<2;
 if(s.coins>=60)bits|=uint64_t(1)<<3;
 if(s.coins>=99)bits|=uint64_t(1)<<4;
 for(int w=1;w<=4;++w)if(s.completeWorlds&(1u<<w))bits|=uint64_t(1)<<(20+w);
 for(int w=1;w<=3;++w)if(s.visitedWorlds&(1u<<w))bits|=uint64_t(1)<<(w==1?26:w==2?27:29);
 if(s.visitedWorlds&(1u<<5))bits|=uint64_t(1)<<31;
 if(s.completeWorlds&(1u<<5))bits|=uint64_t(1)<<33;
 if(s.allBottles)bits|=uint64_t(1)<<34;
 return bits;
}
inline uint64_t Merge(uint64_t old,uint64_t earned) {
 uint64_t bits=(old|earned)&All;
 if((bits & (All^1))==(All^1))bits|=1;
 return bits;
}
void Update(float dt);
void Coin(int coins);
void Draw();
bool Unlocked(int index);
int UnlockedCount();
const char* Row(int index);
const char* Detail(int index);
const char* MenuTitle();
}
