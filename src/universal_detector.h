#pragma once

#include "types.h"
#include <string>
#include <vector>

struct UniversalTarget {
    SystemType system = SystemType::None;
    std::string systemName = "";
    std::string modelDetails = "";
    int confidence = 0;
    std::string reason = "";
};

class UniversalDetector {
public:
    static UniversalTarget detect(const std::string& path);
    static UniversalTarget detect(const std::string& path, const std::vector<uint8>& data);
};
