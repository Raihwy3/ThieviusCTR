// Exercise the 3DS stream buffer against a memory stream, including reads
// across buffer boundaries, backward seeks and truncated input.
#include "bis.h"
#include <cassert>
#include <filesystem>
#include <vector>
#include <iostream>

int main(int argc,char** argv) {
    assert(argc==2);
    const std::string path=argv[1];
    std::vector<byte> source(512*1024+37);
    uint32_t state=12345;
    for(auto& value:source) {state=state*1664525+1013904223;value=byte(state>>24);}
    {std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<const char*>(source.data()),source.size());}
    for(unsigned frame=0;frame<8;++frame)CanePrefetchWorld3ds(path);
    CBinaryInputStream disk(path),memory;
    memory.OpenMemory(int(source.size()),source.data());
    assert(disk.GetSize()==source.size());
    for(unsigned i=0;i<2000;++i) {
        state=state*1664525+1013904223;
        const size_t offset=i%3?disk.GetOffset():state%(source.size()-8192);
        disk.Seek(offset);memory.Seek(offset);
        const size_t count=std::min<size_t>(1+(state%4096),source.size()-offset);
        std::vector<byte> a(count),b(count);
        disk.ReadBytes(a.data(),count);memory.ReadBytes(b.data(),count);
        assert(a==b && disk.GetOffset()==memory.GetOffset());
        assert(disk.Remaining()==memory.Remaining());
        if(disk.Remaining()>=16) {disk.Align(16);memory.Align(16);assert(disk.GetOffset()==memory.GetOffset());}
    }
    disk.Seek(source.size()-2);
    bool rejected=false;
    try {disk.U32Read();}catch(const std::runtime_error&) {rejected=true;}
    assert(rejected);
    disk.Seek(0);assert(disk.U8Read()==source[0]);
    disk.Seek(0);std::vector<byte> bulk(source.size());
    disk.ReadBytes(bulk.data(),bulk.size());assert(bulk==source);
    disk.Close();disk.OpenFile(path);assert(disk.GetSize()==source.size() && disk.U8Read()==source[0]);
    disk.Close();std::filesystem::remove(path);
    std::cout<<"PASS buffered BRX reads, seek/alignment, cached size, EOF recovery and reopen\n";
}
