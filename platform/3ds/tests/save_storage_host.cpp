#include <sys/stat.h>
#include <filesystem>
#include <fstream>
#include <cassert>
#include <iostream>
#include <unistd.h>
#include "../save_storage.inc"
std::string read(const std::string& p) {std::ifstream f(p);return {std::istreambuf_iterator<char>(f),{}};}
void write(const std::string& p,const char* s) {std::ofstream(p)<<s;}
int main() {
 const auto dir=std::filesystem::temp_directory_path()/("thievius-save-"+std::to_string(getpid()));
 std::filesystem::create_directories(dir);
 const auto p=(dir/"slot1.sav").string(), t=p+".tmp";
 write(t,"first");assert(cane_save3ds::Replace(t,p));assert(read(p)=="first");
 write(t,"second");assert(cane_save3ds::Replace(t,p));assert(read(p)=="second");
 assert(!std::filesystem::exists(p+".bak"));
 assert(!cane_save3ds::Replace(t,p));assert(read(p)=="second");
 std::filesystem::rename(p,p+".bak");assert(cane_save3ds::Recover(p));assert(read(p)=="second");
 write(p+".bak","stale");assert(cane_save3ds::Recover(p));assert(read(p)=="second");
 write(t,"third");assert(cane_save3ds::Replace(t,p));assert(read(p)=="third");
 std::filesystem::remove_all(dir);
 std::cout<<"PASS first save, overwrite, failed-install rollback, interrupted-save recovery and stale backup\n";
}
