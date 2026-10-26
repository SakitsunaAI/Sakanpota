#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include "types.h"
#include "crypto_secure.h"

static void retrieveSecurityDescriptor(uint8_t outKey[92]) {
    static const uint32 stellaRef[128] = {
        0xFF000000, 0xFF242424, 0xFF484848, 0xFF6C6C6C, 0xFF909090, 0xFFB4B4B4, 0xFFD8D8D8, 0xFFFFFFFF,
        0xFF403800, 0xFF645C00, 0xFF888000, 0xFFACA400, 0xFFD0C800, 0xFFF4EC00, 0xFFFFFF44, 0xFFFFFF88,
        0xFF542800, 0xFF784C00, 0xFF9C7000, 0xFFC09400, 0xFFE4B800, 0xFFFFDC24, 0xFFFFFF68, 0xFFFFFFAC,
        0xFF641400, 0xFF883800, 0xFFAC5C00, 0xFFD08000, 0xFFF4A400, 0xFFFFC824, 0xFFFFEC68, 0xFFFFFFAC,
        0xFF6C0000, 0xFF902000, 0xFFB44400, 0xFFD86800, 0xFFFC8C14, 0xFFFFB058, 0xFFFFD49C, 0xFFFFF8E0,
        0xFF640030, 0xFF881C54, 0xFFAC4078, 0xFFD0649C, 0xFFF488C0, 0xFFFFACE4, 0xFFFFD0FF, 0xFFFFF4FF,
        0xFF4C0058, 0xFF701C7C, 0xFF9440A0, 0xFFB864C4, 0xFFDC88E8, 0xFFFFACFF, 0xFFFFD0FF, 0xFFFFF4FF,
        0xFF2C0074, 0xFF501C98, 0xFF7440BC, 0xFF9864E0, 0xFFBC88FF, 0xE0ACFF, 0xFFFFD0FF, 0xFFFFF4FF,
        0xFF0C007C, 0xFF301CA0, 0xFF5440C4, 0xFF7864E8, 0xFF9C88FF, 0xC0ACFF, 0xE4D0FF, 0xFFFFF4FF,
        0xFF001474, 0xFF183898, 0xFF3C5CBC, 0xFF6080E0, 0xFF84A4FF, 0xFFA8C8FF, 0xFFCCEDFF, 0xFFF0FFFF,
        0xFF002454, 0xFF004878, 0xFF206C9C, 0xFF4490C0, 0xFF68B4E4, 0xFF8CD8FF, 0xFFB0FCFF, 0xFFD4FFFF,
        0xFF003434, 0xFF005858, 0xFF087C7C, 0xFF2CA0A0, 0xFF50C4C4, 0xFF74E8E8, 0xFF98FFFF, 0xFFBCFFFF,
        0xFF003C0C, 0xFF006030, 0xFF008454, 0xFF1AA878, 0xFF3ECC9C, 0xFF62F0C0, 0xFF86FFE4, 0xFFAAFFFF,
        0xFF003C00, 0xFF146000, 0xFF388400, 0xFF5CA804, 0xFF80CC28, 0xFFA4F04C, 0xFFC8FF70, 0xFFECFF94,
        0xFF143800, 0xFF385C00, 0xFF5C8000, 0xFF80A400, 0xFFA4C800, 0xFFC8EC1C, 0xFFECFF60, 0xFFFFFFA4,
        0xFF2C3000, 0xFF505400, 0xFF747800, 0xFF989C00, 0xFFBCC000, 0xE0E414, 0xFFFFFF58, 0xFFFFFF9C
    };

    uint16 lfsr9 = 0x1A5;
    uint8 lfsr5 = 0x1F;
    for (int i = 0; i < 92; i++) {
        uint8 b9 = ((lfsr9 >> 8) ^ (lfsr9 >> 4)) & 1;
        lfsr9 = ((lfsr9 << 1) | b9) & 0x1FF;

        uint8 b5 = ((lfsr5 >> 4) ^ (lfsr5 >> 2)) & 1;
        lfsr5 = ((lfsr5 << 1) | b5) & 0x1F;

        int palIdx = (i * 7 + 13) % 128;
        uint32 pal = stellaRef[palIdx];

        uint8 tiaMix = ((pal >> ((i % 3) * 8)) & 0xFF) ^ (lfsr9 & 0xFF) ^ (lfsr5 << 3);
        outKey[i] = (tiaMix ^ ((i * 37 + 101) & 0xFF)) & 0xFF;
    }
}

struct AssetDef {
    std::string id;
    std::string varName;
    std::string filename;
    std::string category;
    std::string title;
    std::string content;
};

int main() {
    std::vector<AssetDef> assets;

    // 1. Sub-GHz Car Keyfob
    {
        std::ostringstream ss;
        ss << "Filetype: Flipper SubGhz RAW File\n"
           << "Version: 1\n"
           << "Frequency: 433920000\n"
           << "Preset: FuriHalSubGhzPreset2FSKDev47KHz\n"
           << "Protocol: Princeton\n"
           << "Bit: 24\n"
           << "Key: 00 00 00 00 00 A1 B2 C3\n"
           << "TE: 400\n"
           << "RAW_Data: 400 -800 800 -400 400 -800 800 -400 400 -800 400 -800 800 -400\n";
        assets.push_back({"sub_keyfob", "SUB_CAR_KEYFOB", "car_keyfob.sub", "subghz", "Car Remote Keyfob (433.92 MHz)", ss.str()});
    }

    // 2. Sub-GHz Gate Opener
    {
        std::ostringstream ss;
        ss << "Filetype: Flipper SubGhz Key File\n"
           << "Version: 1\n"
           << "Frequency: 868350000\n"
           << "Preset: FuriHalSubGhzPresetOok650Async\n"
           << "Protocol: Came\n"
           << "Bit: 12\n"
           << "Key: 00 00 00 00 00 00 0E 5A\n";
        assets.push_back({"sub_gate", "SUB_GATE_OPENER", "gate_came.sub", "subghz", "Barrier Gate Opener (868.35 MHz)", ss.str()});
    }

    // 3. Sub-GHz Weather Station
    {
        std::ostringstream ss;
        ss << "Filetype: Flipper SubGhz RAW File\n"
           << "Version: 1\n"
           << "Frequency: 433920000\n"
           << "Protocol: Oregon-v2.1\n"
           << "Bit: 80\n"
           << "Temp: 21.4 C\n"
           << "Humidity: 48%\n"
           << "Battery: OK\n";
        assets.push_back({"sub_weather", "SUB_WEATHER_STATION", "weather_station.sub", "subghz", "Oregon Scientific Weather Station (433.92 MHz)", ss.str()});
    }

    // 4. NFC MIFARE Classic 1K
    {
        std::ostringstream ss;
        ss << "Filetype: Flipper NFC device\n"
           << "Version: 2\n"
           << "Device type: Mifare Classic\n"
           << "UID: 04 A2 8B C1 23 45 67\n"
           << "ATQA: 00 04\n"
           << "SAK: 08\n"
           << "Mifare Classic type: 1K\n"
           << "Block 0: 04 A2 8B C1 23 45 67 08 04 00 62 63 64 65 66 67\n"
           << "Block 1: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
           << "Block 2: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
           << "Block 3: FF FF FF FF FF FF FF 07 80 69 FF FF FF FF FF FF\n";
        assets.push_back({"nfc_classic", "NFC_MIFARE_CLASSIC", "mifare_1k.nfc", "nfc", "MIFARE Classic 1K Access Badge", ss.str()});
    }

    // 5. NFC NTAG215 Amiibo
    {
        std::ostringstream ss;
        ss << "Filetype: Flipper NFC device\n"
           << "Version: 2\n"
           << "Device type: NTAG215\n"
           << "UID: 04 1A 2B 3C 4D 5E 6F\n"
           << "ATQA: 00 44\n"
           << "SAK: 00\n"
           << "Pages total: 135\n"
           << "Page 0: 04 1A 2B C2\n"
           << "Page 1: 3C 4D 5E 6F\n"
           << "Page 2: 88 48 00 00\n";
        assets.push_back({"nfc_ntag", "NFC_NTAG215_AMIIBO", "zelda_link.nfc", "nfc", "NTAG215 Zelda Amiibo Figure", ss.str()});
    }

    // 6. NFC EMV Bank Card
    {
        std::ostringstream ss;
        ss << "Filetype: Flipper NFC device\n"
           << "Version: 2\n"
           << "Device type: EMV Contactless\n"
           << "UID: 12 34 56 78\n"
           << "Protocol: ISO14443-4\n"
           << "AID: A0 00 00 00 04 10 10\n"
           << "Application: Mastercard PayPass\n"
           << "Cardholder: FLIPPER USER\n";
        assets.push_back({"nfc_emv", "NFC_EMV_BANK_CARD", "bank_card.nfc", "nfc", "EMV Contactless Payment Card", ss.str()});
    }

    // 7. 125 kHz RFID EM4100 Blue Key
    {
        std::ostringstream ss;
        ss << "Filetype: Flipper RFID key\n"
           << "Version: 1\n"
           << "Key type: EM4100\n"
           << "Data: 01 02 03 04 05\n";
        assets.push_back({"rfid_em4100", "RFID_EM4100_BLUEKEY", "apartment_fob.rfid", "rfid", "EM4100 Apartment Blue Keyfob", ss.str()});
    }

    // 8. 125 kHz RFID HID Prox Badge
    {
        std::ostringstream ss;
        ss << "Filetype: Flipper RFID key\n"
           << "Version: 1\n"
           << "Key type: H10301\n"
           << "Data: 1F 2E 3D 4C 5B\n"
           << "Facility code: 137\n"
           << "Card number: 4210\n";
        assets.push_back({"rfid_hid", "RFID_HID_PROX_BADGE", "office_badge.rfid", "rfid", "HID ProxCard II Office Badge", ss.str()});
    }

    // 9. Infrared Samsung TV
    {
        std::ostringstream ss;
        ss << "Filetype: Flipper IR file\n"
           << "Version: 1\n"
           << "name: POWER\n"
           << "type: parsed\n"
           << "protocol: Samsung32\n"
           << "address: 07 00 00 00\n"
           << "command: 02 00 00 00\n"
           << "name: VOL_UP\n"
           << "type: parsed\n"
           << "protocol: Samsung32\n"
           << "address: 07 00 00 00\n"
           << "command: 07 00 00 00\n"
           << "name: VOL_DOWN\n"
           << "type: parsed\n"
           << "protocol: Samsung32\n"
           << "address: 07 00 00 00\n"
           << "command: 0B 00 00 00\n";
        assets.push_back({"ir_samsung", "IR_SAMSUNG_TV", "samsung_tv.ir", "ir", "Samsung Smart TV Universal Remote", ss.str()});
    }

    // 10. Infrared Sony Audio
    {
        std::ostringstream ss;
        ss << "Filetype: Flipper IR file\n"
           << "Version: 1\n"
           << "name: POWER\n"
           << "type: parsed\n"
           << "protocol: SIRC\n"
           << "address: 10 00 00 00\n"
           << "command: 15 00 00 00\n";
        assets.push_back({"ir_sony", "IR_SONY_AUDIO", "sony_audio.ir", "ir", "Sony Soundbar & Audio Receiver", ss.str()});
    }

    // 11. iButton Dallas DS1990
    {
        std::ostringstream ss;
        ss << "Filetype: Flipper iButton key\n"
           << "Version: 1\n"
           << "Key type: Dallas\n"
           << "Data: 01 23 45 67 89 AB CD EF\n";
        assets.push_back({"ibutton_dallas", "IBUTTON_DALLAS_DS1990", "dallas_ds1990.ibtn", "ibutton", "Dallas DS1990A 1-Wire Intercom Key", ss.str()});
    }

    // 12. BadUSB Rickroll
    {
        std::ostringstream ss;
        ss << "REM BadUSB Rickroll Demonstration\n"
           << "DELAY 1000\n"
           << "GUI SPACE\n"
           << "DELAY 500\n"
           << "STRING terminal\n"
           << "ENTER\n"
           << "DELAY 1000\n"
           << "STRING curl -s -L https://raw.githubusercontent.com/keroserene/rickrollrc/master/roll.sh | bash\n"
           << "ENTER\n";
        assets.push_back({"badusb_rickroll", "BADUSB_RICKROLL", "rickroll.txt", "badusb", "BadUSB Quick Keystroke Demonstration", ss.str()});
    }

    // 13. BadUSB System Audit
    {
        std::ostringstream ss;
        ss << "REM System Audit Tool\n"
           << "DELAY 500\n"
           << "STRING uname -a; whoami; uptime\n"
           << "ENTER\n";
        assets.push_back({"badusb_audit", "BADUSB_SYSTEM_AUDIT", "sys_audit.txt", "badusb", "BadUSB System Info Script", ss.str()});
    }

    // 14. Music Track: Mario Theme
    {
        std::ostringstream ss;
        ss << "Filetype: Flipper Music Format\n"
           << "Version: 1\n"
           << "BPM: 150\n"
           << "E5 8 E5 8 P8 E5 8 P8 C5 8 E5 4 G5 4 P4 G4 4\n";
        assets.push_back({"fmf_mario", "FMF_MARIO", "mario_theme.fmf", "music", "Super Mario Bros Theme Chiptune", ss.str()});
    }

    // 15. Music Track: Tetris Korobeiniki
    {
        std::ostringstream ss;
        ss << "Filetype: Flipper Music Format\n"
           << "Version: 1\n"
           << "BPM: 140\n"
           << "E5 4 B4 8 C5 8 D5 4 C5 8 B4 8 A4 4 A4 8 C5 8 E5 4 D5 8 C5 8 B4 6 C5 8 D5 4 E5 4 C5 4 A4 4 A4 4\n";
        assets.push_back({"fmf_tetris", "FMF_TETRIS", "tetris.fmf", "music", "Tetris Korobeiniki Chiptune Theme", ss.str()});
    }

    // Security Key
    uint8_t kd[92];
    retrieveSecurityDescriptor(kd);

    std::ofstream out("src/embedded_flipper_assets.cpp");
    out << "// Flipper Zero Encrypted Asset Library (ChaCha20 + AES-256 Cascade)\n";
    out << "#include \"embedded_flipper_assets.h\"\n";
    out << "#include \"crypto_secure.h\"\n";
    out << "#include \"atari_machine.h\"\n";
    out << "#include <cstring>\n\n";
    out << "namespace EmbeddedFlipperAssets {\n\n";

    // Info array
    out << "const AssetEntry ASSETS[ASSET_COUNT] = {\n";
    for (size_t c = 0; c < assets.size(); c++) {
        out << "    {\"" << assets[c].id << "\", \""
            << assets[c].filename << "\", \""
            << assets[c].category << "\", \""
            << assets[c].title << "\", "
            << std::dec << assets[c].content.size() << "}"
            << (c + 1 < assets.size() ? ",\n" : "\n");
    }
    out << "};\n\n";

    // Decrypted vectors
    for (const auto& c : assets) {
        out << "std::vector<uint8> " << c.varName << ";\n";
    }
    out << "static bool isDecrypted = false;\n\n";

    // Encrypt each asset
    for (const auto& c : assets) {
        size_t len = c.content.size();
        std::vector<uint8_t> plain(c.content.begin(), c.content.end());
        std::vector<uint8_t> enc(len);
        SecureCrypto::cascadeEncrypt(plain.data(), enc.data(), len, kd);

        out << "// Encrypted " << c.title << " (" << std::dec << len << " bytes)\n";
        out << "static const uint8_t ENC_" << c.varName << "[" << std::dec << len << "] = {\n";
        for (size_t i = 0; i < len; i++) {
            if (i % 16 == 0) out << "    ";
            out << "0x" << std::hex << std::setw(2) << std::setfill('0') << (int)enc[i];
            if (i + 1 < len) out << ", ";
            if ((i + 1) % 16 == 0 || i + 1 == len) out << "\n";
        }
        out << "};\n\n";
    }

    out << "void ensureDecrypted() {\n";
    out << "    if (isDecrypted) return;\n";
    out << "    uint8_t kd[92];\n";
    out << "    AtariMachine::retrieveSecurityDescriptor(kd);\n\n";
    for (const auto& c : assets) {
        out << "    " << c.varName << ".resize(" << std::dec << c.content.size() << ");\n";
        out << "    SecureCrypto::cascadeDecrypt(ENC_" << c.varName << ", " << c.varName << ".data(), " << std::dec << c.content.size() << ", kd);\n";
    }
    out << "    isDecrypted = true;\n";
    out << "}\n\n";

    out << "const uint8* getAssetData(int index, size_t& outSize) {\n";
    out << "    ensureDecrypted();\n";
    out << "    if (index < 0 || index >= ASSET_COUNT) index = 0;\n";
    out << "    outSize = ASSETS[index].size;\n";
    out << "    switch (index) {\n";
    for (size_t i = 0; i < assets.size(); i++) {
        out << "        case " << std::dec << i << ": return " << assets[i].varName << ".data();\n";
    }
    out << "        default: return SUB_CAR_KEYFOB.data();\n";
    out << "    }\n";
    out << "}\n\n";

    out << "const uint8* getAssetDataById(const std::string& id, size_t& outSize) {\n";
    out << "    ensureDecrypted();\n";
    for (size_t i = 0; i < assets.size(); i++) {
        out << "    if (id == \"" << assets[i].id << "\") { outSize = " << assets[i].varName << ".size(); return " << assets[i].varName << ".data(); }\n";
    }
    out << "    outSize = SUB_CAR_KEYFOB.size();\n";
    out << "    return SUB_CAR_KEYFOB.data();\n";
    out << "}\n\n";

    out << "const AssetEntry* getAssetEntry(int index) {\n";
    out << "    if (index >= 0 && index < ASSET_COUNT) return &ASSETS[index];\n";
    out << "    return &ASSETS[0];\n";
    out << "}\n\n";

    out << "} // namespace EmbeddedFlipperAssets\n";
    out.close();

    std::cout << "Successfully generated src/embedded_flipper_assets.cpp!\n";
    return 0;
}
