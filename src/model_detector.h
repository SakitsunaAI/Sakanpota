#pragma once

#include "types.h"
#include "fdc1770.h"
#include <string>

struct DetectionResult {
    BBCModel model = BBCModel::ModelB;
    int confidence = 0; // 0..100
    std::string reason;
};

class ModelDetector {
public:
    // Analyzes a disk image and returns the best matching BBC Model
    static DetectionResult detectFromDisk(const std::string& path, const std::vector<uint8>& diskData, const FDC1770& fdc, int drive = 0);
};
