#include "model_detector.h"
#include <algorithm>
#include <cctype>
#include <iostream>

static std::string toLower(const std::string& str) {
    std::string res = str;
    std::transform(res.begin(), res.end(), res.begin(), [](unsigned char c) { return std::tolower(c); });
    return res;
}

static std::string toUpper(const std::string& str) {
    std::string res = str;
    std::transform(res.begin(), res.end(), res.begin(), [](unsigned char c) { return std::toupper(c); });
    return res;
}

DetectionResult ModelDetector::detectFromDisk(const std::string& path, const std::vector<uint8>& diskData, const FDC1770& fdc, int drive) {
    DetectionResult result;
    result.model = BBCModel::ModelB; // Default to the ubiquitous Model B
    result.confidence = 50;
    result.reason = "Standard BBC Micro disk";

    int scoreMaster = 0;
    int scoreBPlus = 0;
    int scoreModelA = 0;
    bool fitsIn16K = true;

    std::string lowerPath = toLower(path);

    // 1. Filename & Path Heuristics
    if (lowerPath.find("master128") != std::string::npos ||
        lowerPath.find("m128") != std::string::npos ||
        lowerPath.find("mstr") != std::string::npos ||
        lowerPath.find("master") != std::string::npos ||
        lowerPath.find("mos3") != std::string::npos ||
        lowerPath.find("65c02") != std::string::npos) {
        scoreMaster += 40;
    }

    if (lowerPath.find("bplus") != std::string::npos ||
        lowerPath.find("b_plus") != std::string::npos ||
        lowerPath.find("b+") != std::string::npos) {
        scoreBPlus += 40;
    }

    if (lowerPath.find("modela") != std::string::npos ||
        lowerPath.find("model_a") != std::string::npos ||
        lowerPath.find("16k") != std::string::npos) {
        scoreModelA += 40;
    }

    // 2. Disk Title Heuristics
    if (diskData.size() >= 512) {
        std::string title;
        for (int i = 0; i < 8; i++) {
            char c = diskData[i];
            if (c >= 32 && c < 127) title += c;
        }
        for (int i = 0; i < 4; i++) {
            char c = diskData[256 + i];
            if (c >= 32 && c < 127) title += c;
        }
        std::string upperTitle = toUpper(title);

        if (upperTitle.find("MASTER") != std::string::npos ||
            upperTitle.find("M128") != std::string::npos ||
            upperTitle.find("MOS 3") != std::string::npos ||
            upperTitle.find("MOS3") != std::string::npos ||
            upperTitle.find("COMPACT") != std::string::npos) {
            scoreMaster += 50;
        }

        if (upperTitle.find("BPLUS") != std::string::npos ||
            upperTitle.find("B+") != std::string::npos) {
            scoreBPlus += 50;
        }

        if (upperTitle.find("MODEL A") != std::string::npos ||
            upperTitle.find("16K") != std::string::npos) {
            scoreModelA += 50;
        }
    }

    // 3. Catalog & Files Analysis
    auto files = const_cast<FDC1770&>(fdc).getCatalog(drive);
    for (const auto& file : files) {
        std::string upperName = toUpper(file.name);

        if (file.loadAddress + file.length > 0x4000) {
            fitsIn16K = false; // Cannot run on Model A (16KB limit)
        }

        // Check for Hazel RAM ($C000..$CFFF) load address: Unmistakable Master 128 feature!
        if (file.loadAddress >= 0xC000 && file.loadAddress < 0xD000) {
            scoreMaster += 70;
        }

        // Sideways RAM ($8000..$BFFF) load address
        if (file.loadAddress >= 0x8000 && file.loadAddress < 0xC000) {
            scoreMaster += 30;
            scoreBPlus += 30;
        }

        if (upperName.find("M128") != std::string::npos ||
            upperName.find("MASTER") != std::string::npos ||
            upperName.find("MOS3") != std::string::npos) {
            scoreMaster += 35;
        }

        if (upperName.find("BPLUS") != std::string::npos ||
            upperName.find("B+") != std::string::npos) {
            scoreBPlus += 35;
        }
    }

    // 4. Code & Boot file Deep Inspection
    // Look for !BOOT, BOOT, LOAD, START, WELCOME
    for (const auto& file : files) {
        std::string upperName = toUpper(file.name);
        if (upperName == "!BOOT" || upperName == "BOOT" || upperName == "LOAD" ||
            upperName == "START" || upperName == "WELCOME" || upperName == "MENU") {
            uint32 offset = file.startSector * 256;
            if (offset + file.length <= diskData.size()) {
                const uint8* fileBytes = diskData.data() + offset;

                // A) Binary scan for Master 128 ACCCON ($FE34) register access
                // Opcode patterns: 8D 34 FE (STA $FE34), AD 34 FE (LDA $FE34), 8E 34 FE (STX $FE34)
                for (uint32 b = 0; b + 2 < file.length; b++) {
                    if (fileBytes[b + 1] == 0x34 && fileBytes[b + 2] == 0xFE) {
                        uint8 op = fileBytes[b];
                        if (op == 0x8D || op == 0xAD || op == 0x8E || op == 0x8C || op == 0x0C || op == 0x1C) {
                            scoreMaster += 80;
                            break;
                        }
                    }
                }

                // B) Text scan in boot file
                std::string contentText;
                for (uint32 b = 0; b < file.length; b++) {
                    char c = fileBytes[b];
                    if (c >= 32 && c < 127) contentText += c;
                    else contentText += ' ';
                }
                std::string upperText = toUpper(contentText);

                if (upperText.find("MASTER") != std::string::npos ||
                    upperText.find("M128") != std::string::npos ||
                    upperText.find("65C02") != std::string::npos ||
                    upperText.find("?&28F") != std::string::npos) {
                    scoreMaster += 40;
                }

                if (upperText.find("BPLUS") != std::string::npos ||
                    upperText.find("B+") != std::string::npos) {
                    scoreBPlus += 40;
                }
            }
            break;
        }
    }

    // Disqualify Model A if RAM requirements exceed 16KB
    if (!fitsIn16K) {
        scoreModelA = 0;
    }

    // 5. Select Winning Model
    if (scoreMaster > scoreBPlus && scoreMaster > scoreModelA && scoreMaster >= 30) {
        result.model = BBCModel::Master128;
        result.confidence = std::min(95, 50 + scoreMaster);
        result.reason = "Master 128 specific files/code detected";
    } else if (scoreBPlus > scoreMaster && scoreBPlus > scoreModelA && scoreBPlus >= 30) {
        result.model = BBCModel::ModelBPlus;
        result.confidence = std::min(90, 50 + scoreBPlus);
        result.reason = "BBC Model B+ configuration detected";
    } else if (scoreModelA >= 30 && fitsIn16K) {
        result.model = BBCModel::ModelA;
        result.confidence = std::min(90, 50 + scoreModelA);
        result.reason = "BBC Model A compatible disk (16KB footprint)";
    } else {
        result.model = BBCModel::ModelB;
        result.confidence = 85;
        result.reason = "Standard BBC Model B target (32KB DFS compatible)";
    }

    return result;
}
