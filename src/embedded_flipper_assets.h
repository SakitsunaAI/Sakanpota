#pragma once
#include "types.h"
#include <string>
#include <vector>
#include <cstddef>

namespace EmbeddedFlipperAssets {
    struct AssetEntry {
        const char* id;
        const char* filename;
        const char* category; // "subghz", "nfc", "rfid", "ir", "ibutton", "badusb", "music"
        const char* title;
        size_t size;
    };

    constexpr int ASSET_COUNT = 15;
    extern const AssetEntry ASSETS[ASSET_COUNT];

    // Decrypted buffers in memory:
    extern std::vector<uint8> SUB_CAR_KEYFOB;
    extern std::vector<uint8> SUB_GATE_OPENER;
    extern std::vector<uint8> SUB_WEATHER_STATION;
    extern std::vector<uint8> NFC_MIFARE_CLASSIC;
    extern std::vector<uint8> NFC_NTAG215_AMIIBO;
    extern std::vector<uint8> NFC_EMV_BANK_CARD;
    extern std::vector<uint8> RFID_EM4100_BLUEKEY;
    extern std::vector<uint8> RFID_HID_PROX_BADGE;
    extern std::vector<uint8> IR_SAMSUNG_TV;
    extern std::vector<uint8> IR_SONY_AUDIO;
    extern std::vector<uint8> IBUTTON_DALLAS_DS1990;
    extern std::vector<uint8> BADUSB_RICKROLL;
    extern std::vector<uint8> BADUSB_SYSTEM_AUDIT;
    extern std::vector<uint8> FMF_MARIO;
    extern std::vector<uint8> FMF_TETRIS;

    void ensureDecrypted();
    const uint8* getAssetData(int index, size_t& outSize);
    const uint8* getAssetDataById(const std::string& id, size_t& outSize);
    const AssetEntry* getAssetEntry(int index);
}
