#pragma once
#include <string_view>

namespace a1 {
inline bool allowCubaseContext(bool cubaseProcess, bool realHostInterface,
                               std::string_view reportedName) {
    return cubaseProcess && realHostInterface && reportedName.starts_with("Cubase")
        && (reportedName.size() == 6 || reportedName[6] == ' ');
}
}
