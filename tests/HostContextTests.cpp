#include "../src/HostContext.h"
#include <cassert>
#include <iostream>

int main() {
    using a1::allowCubaseContext;
    assert(!allowCubaseContext(false, false, "Cubase"));
    assert(!allowCubaseContext(true, false, "Cubase"));
    assert(!allowCubaseContext(false, true, "Cubase"));
    assert(!allowCubaseContext(true, true, ""));
    assert(!allowCubaseContext(true, true, "REAPER"));
    assert(!allowCubaseContext(true, true, "Fake Cubase"));
    assert(allowCubaseContext(true, true, "Cubase"));
    assert(allowCubaseContext(true, true, "Cubase 15"));
    assert(!allowCubaseContext(true, true, "CubaseImpersonator"));
    std::cout << "PASS: Cubase requires process detection and actual VST3 context\n";
}
