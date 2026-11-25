#pragma once
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace a1 {
struct Program { std::string name; std::array<float,99> parameters{}; };
inline std::vector<Program> parseBank(std::span<const uint8_t> bytes) {
    auto bad=[] { throw std::runtime_error("Invalid or incompatible original A1 bank"); };
    auto u32=[&](size_t offset) {
        if(offset>bytes.size() || bytes.size()-offset<4)bad();
        uint32_t n=0;for(size_t i=0;i<4;++i)n=(n<<8)|bytes[offset+i];return n;
    };
    auto magic=[&](size_t offset,const char* text) {
        return offset<=bytes.size() && bytes.size()-offset>=4
            && std::memcmp(bytes.data()+offset,text,4)==0;
    };
    if(bytes.size()<156 || !magic(0,"CcnK") || !magic(8,"FxBk") || u32(16)!=0x33455353)bad();
    const auto count=u32(24);if(count==0 || count>512)bad();
    std::vector<Program> programs;programs.reserve(count);size_t offset=156;
    for(uint32_t i=0;i<count;++i) {
        if(offset>bytes.size() || bytes.size()-offset<452 || !magic(offset,"CcnK")
            || !magic(offset+8,"FxCk") || u32(offset+16)!=0x33455353 || u32(offset+24)!=99)bad();
        Program p;
        for(size_t j=0;j<28 && bytes[offset+28+j];++j)p.name+=char(bytes[offset+28+j]);
        for(size_t j=0;j<99;++j) {
            float v=std::bit_cast<float>(u32(offset+56+4*j));
            if(!std::isfinite(v) || v<0 || v>1)bad();
            p.parameters[j]=v;
        }
        programs.push_back(std::move(p));offset+=452;
    }
    if(offset!=bytes.size())bad();
    return programs;
}
}
