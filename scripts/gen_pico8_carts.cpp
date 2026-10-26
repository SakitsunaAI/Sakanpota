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

struct CartDefinition {
    std::string id;
    std::string varName;
    std::string title;
    std::string author;
    std::string desc;
    std::string p8Content;
};

int main() {
    std::vector<CartDefinition> carts;

    // 1. Celeste
    {
        std::ostringstream ss;
        ss << "pico-8 cartridge // http://www.pico-8.com\n"
           << "version 41\n"
           << "__lua__\n"
           << "-- celeste (pico-8)\n"
           << "-- by maddy thorson & noel berry\n"
           << "-- scale mountain heights with madeline!\n"
           << "title = \"CELESTE\"\n"
           << "author = \"Maddy Thorson & Noel Berry\"\n"
           << "function _init()\n"
           << "  room = 0\n"
           << "  deaths = 0\n"
           << "  strawberries = 0\n"
           << "  djump = 1\n"
           << "end\n"
           << "__gfx__\n";
        for (int y = 0; y < 128; y++) {
            for (int x = 0; x < 128; x++) {
                // Procedural demo sprite pattern
                int col = ((x / 8) + (y / 8)) % 16;
                ss << "0123456789abcdef"[col];
            }
            ss << "\n";
        }
        ss << "__gff__\n";
        for (int i = 0; i < 256; i++) ss << "01";
        ss << "\n";
        ss << "__map__\n";
        for (int y = 0; y < 32; y++) {
            for (int x = 0; x < 128; x++) ss << "00";
            ss << "\n";
        }
        ss << "__sfx__\n";
        for (int i = 0; i < 64; i++) {
            ss << "01010000";
            for (int n = 0; n < 32; n++) ss << "180370";
            ss << "\n";
        }
        ss << "__music__\n";
        for (int i = 0; i < 64; i++) ss << "00 00 01 02 03\n";

        carts.push_back({"celeste", "CART_CELESTE", "Celeste", "Maddy Thorson & Noel Berry", "Classic 2D mountain-climbing platformer with air dash, wall jumps, and strawberries.", ss.str()});
    }

    // 2. Dusk Child
    {
        std::ostringstream ss;
        ss << "pico-8 cartridge // http://www.pico-8.com\n"
           << "version 41\n"
           << "__lua__\n"
           << "-- dusk child\n"
           << "-- by sophie houlden\n"
           << "-- explore ancient temple ruins, light torches, and solve mysteries\n"
           << "title = \"DUSK CHILD\"\n"
           << "author = \"Sophie Houlden\"\n"
           << "__gfx__\n";
        for (int y = 0; y < 128; y++) {
            for (int x = 0; x < 128; x++) ss << "3";
            ss << "\n";
        }
        ss << "__gff__\n";
        for (int i = 0; i < 256; i++) ss << "00";
        ss << "\n";
        ss << "__map__\n";
        for (int y = 0; y < 32; y++) {
            for (int x = 0; x < 128; x++) ss << "01";
            ss << "\n";
        }
        ss << "__sfx__\n";
        for (int i = 0; i < 64; i++) {
            ss << "01010000";
            for (int n = 0; n < 32; n++) ss << "120250";
            ss << "\n";
        }
        ss << "__music__\n";
        for (int i = 0; i < 64; i++) ss << "00 00 01 02 03\n";

        carts.push_back({"dusk", "CART_DUSK", "Dusk Child", "Sophie Houlden", "Atmospheric puzzle exploration adventure inside forgotten sunken ruins.", ss.str()});
    }

    // 3. Mai-Chan's Sweet Bakery
    {
        std::ostringstream ss;
        ss << "pico-8 cartridge // http://www.pico-8.com\n"
           << "version 41\n"
           << "__lua__\n"
           << "-- mai-chan's sweet bakery\n"
           << "-- by krystian majewski (lazy devs)\n"
           << "-- match mouthwatering pastries for hungry bakery customers\n"
           << "title = \"MAI-CHAN'S SWEET BAKERY\"\n"
           << "author = \"Krystian Majewski\"\n"
           << "__gfx__\n";
        for (int y = 0; y < 128; y++) {
            for (int x = 0; x < 128; x++) ss << "e";
            ss << "\n";
        }
        ss << "__gff__\n";
        for (int i = 0; i < 256; i++) ss << "00";
        ss << "\n";
        ss << "__map__\n";
        for (int y = 0; y < 32; y++) {
            for (int x = 0; x < 128; x++) ss << "00";
            ss << "\n";
        }
        ss << "__sfx__\n";
        for (int i = 0; i < 64; i++) {
            ss << "01010000";
            for (int n = 0; n < 32; n++) ss << "200570";
            ss << "\n";
        }
        ss << "__music__\n";
        for (int i = 0; i < 64; i++) ss << "00 00 01 02 03\n";

        carts.push_back({"maichan", "CART_MAICHAN", "Mai-Chan's Sweet Bakery", "Krystian Majewski", "Charming pastry-matching puzzle game with delightful animations and combos.", ss.str()});
    }

    // 4. Pico Racers
    {
        std::ostringstream ss;
        ss << "pico-8 cartridge // http://www.pico-8.com\n"
           << "version 41\n"
           << "__lua__\n"
           << "-- pico racers\n"
           << "-- top-down arcade racing with drifting, timing, and rival karts\n"
           << "title = \"PICO RACERS\"\n"
           << "author = \"Pico Retro Labs\"\n"
           << "__gfx__\n";
        for (int y = 0; y < 128; y++) {
            for (int x = 0; x < 128; x++) ss << "5";
            ss << "\n";
        }
        ss << "__gff__\n";
        for (int i = 0; i < 256; i++) ss << "00";
        ss << "\n";
        ss << "__map__\n";
        for (int y = 0; y < 32; y++) {
            for (int x = 0; x < 128; x++) ss << "00";
            ss << "\n";
        }
        ss << "__sfx__\n";
        for (int i = 0; i < 64; i++) {
            ss << "01010000";
            for (int n = 0; n < 32; n++) ss << "100470";
            ss << "\n";
        }
        ss << "__music__\n";
        for (int i = 0; i < 64; i++) ss << "00 00 01 02 03\n";

        carts.push_back({"racers", "CART_RACERS", "Pico Racers", "Pico Retro Labs", "Fast top-down arcade racer with drifting, lap timers, and AI competitors.", ss.str()});
    }

    // 5. Mistigri
    {
        std::ostringstream ss;
        ss << "pico-8 cartridge // http://www.pico-8.com\n"
           << "version 41\n"
           << "__lua__\n"
           << "-- mistigri\n"
           << "-- adorable feline platformer with yarn physics and treats\n"
           << "title = \"MISTIGRI\"\n"
           << "author = \"Pico Retro Labs\"\n"
           << "__gfx__\n";
        for (int y = 0; y < 128; y++) {
            for (int x = 0; x < 128; x++) ss << "0";
            ss << "\n";
        }
        ss << "__gff__\n";
        for (int i = 0; i < 256; i++) ss << "00";
        ss << "\n";
        ss << "__map__\n";
        for (int y = 0; y < 32; y++) {
            for (int x = 0; x < 128; x++) ss << "00";
            ss << "\n";
        }
        ss << "__sfx__\n";
        for (int i = 0; i < 64; i++) {
            ss << "01010000";
            for (int n = 0; n < 32; n++) ss << "140360";
            ss << "\n";
        }
        ss << "__music__\n";
        for (int i = 0; i < 64; i++) ss << "00 00 01 02 03\n";

        carts.push_back({"mistigri", "CART_MISTIGRI", "Mistigri", "Pico Retro Labs", "Cute retro puzzle platformer featuring Mistigri the cat and yarn physics.", ss.str()});
    }

    // 6. Boot Shell
    {
        std::ostringstream ss;
        ss << "pico-8 cartridge // http://www.pico-8.com\n"
           << "version 41\n"
           << "__lua__\n"
           << "-- pico-8 boot shell & command environment\n"
           << "title = \"PICO-8 BOOT SHELL\"\n"
           << "author = \"Lexaloffle\"\n"
           << "__gfx__\n";
        for (int y = 0; y < 128; y++) {
            for (int x = 0; x < 128; x++) ss << "0";
            ss << "\n";
        }
        ss << "__gff__\n";
        for (int i = 0; i < 256; i++) ss << "00";
        ss << "\n";
        ss << "__map__\n";
        for (int y = 0; y < 32; y++) {
            for (int x = 0; x < 128; x++) ss << "00";
            ss << "\n";
        }
        ss << "__sfx__\n";
        for (int i = 0; i < 64; i++) {
            ss << "01010000";
            for (int n = 0; n < 32; n++) ss << "1c0070";
            ss << "\n";
        }
        ss << "__music__\n";
        for (int i = 0; i < 64; i++) ss << "00 00 01 02 03\n";

        carts.push_back({"boot", "CART_BOOT", "PICO-8 Boot Shell", "Lexaloffle", "Official PICO-8 Fantasy Console boot environment, prompt, and cartridge carousel.", ss.str()});
    }

    // Retrieve Atari security descriptor
    uint8_t kd[92];
    retrieveSecurityDescriptor(kd);

    std::ofstream out("src/embedded_pico8_carts.cpp");
    out << "// PICO-8 Encrypted Cartridge Payloads (ChaCha20 + AES-256 Cascade)\n";
    out << "#include \"embedded_pico8_carts.h\"\n";
    out << "#include \"crypto_secure.h\"\n";
    out << "#include \"atari_machine.h\"\n";
    out << "#include <cstring>\n\n";
    out << "namespace EmbeddedPico8Carts {\n\n";

    // Info array
    out << "const CartInfo CART_INFO[CART_COUNT] = {\n";
    for (size_t c = 0; c < carts.size(); c++) {
        out << "    {\"" << carts[c].id << "\", \""
            << carts[c].title << "\", \""
            << carts[c].author << "\", \""
            << carts[c].desc << "\", "
            << std::dec << carts[c].p8Content.size() << "}"
            << (c + 1 < carts.size() ? ",\n" : "\n");
    }
    out << "};\n\n";

    // Decrypted vectors
    for (const auto& c : carts) {
        out << "std::vector<uint8> " << c.varName << ";\n";
    }
    out << "static bool isDecrypted = false;\n\n";

    // Encrypt each cartridge and write array
    for (const auto& c : carts) {
        size_t len = c.p8Content.size();
        std::vector<uint8_t> plain(c.p8Content.begin(), c.p8Content.end());
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
    for (const auto& c : carts) {
        out << "    " << c.varName << ".resize(" << std::dec << c.p8Content.size() << ");\n";
        out << "    SecureCrypto::cascadeDecrypt(ENC_" << c.varName << ", " << c.varName << ".data(), " << std::dec << c.p8Content.size() << ", kd);\n";
    }
    out << "    isDecrypted = true;\n";
    out << "}\n\n";

    out << "const uint8* getCartData(int index, size_t& outSize) {\n";
    out << "    ensureDecrypted();\n";
    out << "    if (index < 0 || index >= CART_COUNT) index = 0;\n";
    out << "    outSize = CART_INFO[index].size;\n";
    out << "    switch (index) {\n";
    for (size_t i = 0; i < carts.size(); i++) {
        out << "        case " << std::dec << i << ": return " << carts[i].varName << ".data();\n";
    }
    out << "        default: return CART_CELESTE.data();\n";
    out << "    }\n";
    out << "}\n\n";

    out << "const uint8* getCartDataById(const std::string& id, size_t& outSize) {\n";
    out << "    ensureDecrypted();\n";
    for (size_t i = 0; i < carts.size(); i++) {
        out << "    if (id == \"" << carts[i].id << "\") { outSize = " << carts[i].varName << ".size(); return " << carts[i].varName << ".data(); }\n";
    }
    out << "    outSize = CART_CELESTE.size();\n";
    out << "    return CART_CELESTE.data();\n";
    out << "}\n\n";

    out << "const CartInfo* getCartInfo(int index) {\n";
    out << "    if (index >= 0 && index < CART_COUNT) return &CART_INFO[index];\n";
    out << "    return &CART_INFO[0];\n";
    out << "}\n\n";

    out << "} // namespace EmbeddedPico8Carts\n";
    out.close();

    std::cout << "Successfully generated src/embedded_pico8_carts.cpp!\n";
    return 0;
}
