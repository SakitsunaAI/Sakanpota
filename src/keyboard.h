#pragma once

#include "types.h"
#include <SDL.h>
#include <array>
#include <string>
#include <queue>

class Keyboard {
public:
    // 10 columns x 8 rows matrix
    std::array<std::array<bool, 8>, 10> matrix{};

    bool shiftPressed = false;
    bool ctrlPressed = false;
    bool breakPressed = false;

    // Currently selected column by 74LS145
    uint8 selectedColumn = 0;
    bool keyboardEnabled = false;

    // Clipboard typing queue
    std::queue<char> pasteQueue;
    int pasteDelay = 0;
    char currentPasteChar = 0;
    int pasteHold = 0;

    Keyboard();

    void reset();

    // SDL Key events
    void handleKeyDown(SDL_Keycode key, uint16 mod);
    void handleKeyUp(SDL_Keycode key, uint16 mod);

    // Hardware interface with System VIA
    void setColumn(uint8 col);
    void setEnable(bool enable);
    bool isKeyPressed(uint8 row) const;
    bool hasAnyKeyDownInColumn(uint8 col) const;
    bool hasAnyKeyDown() const;

    // Clipboard paste support
    void pasteText(const std::string& text);
    void updatePaste();

    void setKey(uint8 col, uint8 row, bool pressed);

    // Map character to (column, row, shiftNeeded)
    static bool charToKey(char c, uint8& col, uint8& row, bool& needShift);

private:
    void mapSDLKey(SDL_Keycode key, bool pressed);
};
