#pragma once
#include "types.h"
#include <string>
#include <vector>
#include <cstddef>

namespace EmbeddedPico8Carts {
    struct CartInfo {
        const char* id;
        const char* title;
        const char* author;
        const char* desc;
        size_t size;
    };

    constexpr int CART_COUNT = 6;
    extern const CartInfo CART_INFO[CART_COUNT];

    extern std::vector<uint8> CART_CELESTE;
    extern std::vector<uint8> CART_DUSK;
    extern std::vector<uint8> CART_MAICHAN;
    extern std::vector<uint8> CART_RACERS;
    extern std::vector<uint8> CART_MISTIGRI;
    extern std::vector<uint8> CART_BOOT;

    void ensureDecrypted();
    const uint8* getCartData(int index, size_t& outSize);
    const uint8* getCartDataById(const std::string& id, size_t& outSize);
    const CartInfo* getCartInfo(int index);
}
