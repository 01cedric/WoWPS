#include "addons/interface_source.hpp"
#include "addons/lua_memory_budget.hpp"
#include <cassert>
#include <iostream>
static int remaining=-1;
void* operator new(std::size_t n) {
    if(remaining==0) throw std::bad_alloc();
    if(remaining>0) --remaining;
    if(void* p=std::malloc(n?n:1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) {return ::operator new(n);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}
static bool failRealloc=false;
extern "C" void* __real_realloc(void*,size_t);
extern "C" void* __wrap_realloc(void* p,size_t n) {return failRealloc?nullptr:__real_realloc(p,n);}
int main() {
    using namespace wowee::addons;
    const std::string path="mpq/interface/framexml/test.lua";
    InterfaceSource source;
    int probes=0;
    source.setArchive({},[&](const auto&){++probes;remaining=0;return true;},{});
    bool found=source.exists(path);remaining=-1;assert(found);
    found=source.exists(path);remaining=-1;assert(found&&probes==2);
    source.setArchive({},[&](const auto&){++probes;return true;},{});
    assert(source.exists(path));assert(source.exists(path));assert(probes==3);
    std::cout<<"PASS existence cache allocation failure preserves provider result and resets on new provider\n";
    for(int allowance=1;allowance<=3;++allowance) {
        int reads=0;bool inject=true;
        source.setArchive([&](const auto&){++reads;std::vector<uint8_t> data(4096,'x');if(inject)remaining=allowance;return data;},[](const auto&){return true;},{});
        assert(source.exists(path));
        auto text=source.read(path);remaining=-1;inject=false;
        assert(text&&text->size()==4096&&text->front()=='x');
        assert(source.read(path)==text);assert(reads==2);
        assert(source.releaseSourceCache()==0);
    }
    std::cout<<"PASS optional source cache node/key/value allocation failures preserve source and suspend caching\n";
    int reads=0;
    source.setArchive([&](const auto&){++reads;return std::vector<uint8_t>(4096,'y');},[](const auto&){return true;},{});
    assert(source.read(path));assert(source.read(path));assert(reads==1);
    assert(source.releaseSourceCache()==4096);assert(source.read(path));assert(reads==2);
    std::cout<<"PASS provider reset restores caching and explicit pressure release preserves reads\n";
    LuaMemoryBudget budget;budget.limit=1024;
    void* p=LuaMemoryBudget::allocate(&budget,nullptr,0,512);assert(p);
    static_cast<char*>(p)[0]='z';budget.limit=64;
    p=LuaMemoryBudget::allocate(&budget,p,512,256);assert(p&&budget.used==256);
    failRealloc=true;
    void* shrunk=LuaMemoryBudget::allocate(&budget,p,256,128);
    assert(shrunk==p&&budget.used==128&&static_cast<char*>(p)[0]=='z');
    assert(LuaMemoryBudget::allocate(&budget,p,128,128)==p);
    failRealloc=false;
    assert(!LuaMemoryBudget::allocate(&budget,p,128,129));assert(budget.used==128);
    LuaMemoryBudget::allocate(&budget,p,128,0);assert(budget.used==0&&budget.peak==512&&budget.failures==1);
    std::cout<<"PASS Lua shrink survives lowered budget and failed realloc; rejected growth preserves ownership/accounting\n";
}
