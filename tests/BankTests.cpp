#include "../src/Bank.h"
#include <cassert>
#include <bit>
#include <cstring>
#include <iostream>

static void put(std::vector<uint8_t>& b, size_t p, uint32_t n) {
    for (int i=3;i>=0;--i) { b[p+size_t(i)]=uint8_t(n); n>>=8; }
}
static std::vector<uint8_t> fixture() {
    std::vector<uint8_t> b(156+452);
    std::memcpy(b.data(),"CcnK",4);std::memcpy(b.data()+8,"FxBk",4);
    put(b,16,0x33455353);put(b,24,1);
    std::memcpy(b.data()+156,"CcnK",4);std::memcpy(b.data()+164,"FxCk",4);
    put(b,172,0x33455353);put(b,180,99);
    std::memcpy(b.data()+184,"Original test",13);
    for(size_t i=0;i<99;++i)put(b,212+4*i,std::bit_cast<uint32_t>(float(i)/98.0f));
    return b;
}
static bool rejected(const std::vector<uint8_t>& bytes) {
    try { (void)a1::parseBank(bytes); return false; }
    catch (const std::exception&) { return true; }
}
int main() {
    auto b=fixture();auto p=a1::parseBank(b);
    assert(p.size()==1 && p[0].name=="Original test");
    assert(p[0].parameters[0]==0 && p[0].parameters[98]==1);
    b.pop_back();assert(rejected(b));
    b=fixture();put(b,16,0);assert(rejected(b));
    b=fixture();put(b,180,98);assert(rejected(b));
    b=fixture();put(b,212,std::bit_cast<uint32_t>(1.1f));assert(rejected(b));
    b=fixture();put(b,212,0x7fc00000);assert(rejected(b));
    b=fixture();b.push_back(0);assert(rejected(b));
    std::cout<<"PASS: original zero-size FXB fields, exact floats, malformed banks\n";
}
