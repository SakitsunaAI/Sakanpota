#include "keyboard.h"
#include <cctype>
#include <iostream>

Keyboard::Keyboard() {
    reset();
}

void Keyboard::reset() {
    for (auto& col : matrix) col.fill(false);
    shiftPressed = false;
    ctrlPressed = false;
    breakPressed = false;
    selectedColumn = 0;
    keyboardEnabled = false;
    while (!pasteQueue.empty()) pasteQueue.pop();
    pasteDelay = 0;
    currentPasteChar = 0;
    pasteHold = 0;
}

void Keyboard::setKey(uint8 col, uint8 row, bool pressed) {
    if (col < 10 && row < 8) {
        matrix[col][row] = pressed;
    }
}

void Keyboard::setColumn(uint8 col) {
    selectedColumn = col & 0x0F;
}

void Keyboard::setEnable(bool enable) {
    keyboardEnabled = enable;
}

bool Keyboard::isKeyPressed(uint8 row) const {
    if (selectedColumn < 10 && row < 8) {
        return matrix[selectedColumn][row];
    }
    return false;
}

bool Keyboard::hasAnyKeyDownInColumn(uint8 col) const {
    if (col < 10) {
        for (int r = 0; r < 8; r++) {
            if (matrix[col][r]) return true;
        }
    }
    return false;
}

bool Keyboard::hasAnyKeyDown() const {
    for (int c = 0; c < 10; c++) {
        for (int r = 0; r < 8; r++) {
            if (matrix[c][r]) return true;
        }
    }
    return false;
}

void Keyboard::handleKeyDown(SDL_Keycode key, uint16 mod) {
    shiftPressed = (mod & KMOD_SHIFT) != 0;
    ctrlPressed = (mod & KMOD_CTRL) != 0;
    setKey(0, 0, shiftPressed);
    setKey(1, 0, ctrlPressed);
    mapSDLKey(key, true);
}

void Keyboard::handleKeyUp(SDL_Keycode key, uint16 mod) {
    shiftPressed = (mod & KMOD_SHIFT) != 0;
    ctrlPressed = (mod & KMOD_CTRL) != 0;
    setKey(0, 0, shiftPressed);
    setKey(1, 0, ctrlPressed);
    mapSDLKey(key, false);
}

void Keyboard::mapSDLKey(SDL_Keycode key, bool pressed) {
    switch (key) {
        // Red Function Keys f0..f9
        case SDLK_F10: setKey(0, 2, pressed); break; // f0
        case SDLK_F1:  setKey(1, 7, pressed); break; // f1
        case SDLK_F2:  setKey(2, 7, pressed); break; // f2
        case SDLK_F3:  setKey(3, 7, pressed); break; // f3
        case SDLK_F4:  setKey(4, 1, pressed); break; // f4
        case SDLK_F5:  setKey(4, 7, pressed); break; // f5
        case SDLK_F6:  setKey(5, 7, pressed); break; // f6
        case SDLK_F7:  setKey(6, 1, pressed); break; // f7
        case SDLK_F8:  setKey(6, 7, pressed); break; // f8
        case SDLK_F9:  setKey(7, 7, pressed); break; // f9

        // Break key
        case SDLK_F12:
        case SDLK_PAUSE:
            breakPressed = pressed;
            break;

        // Escape
        case SDLK_ESCAPE: setKey(0, 7, pressed); break;

        // Numbers 0..9
        case SDLK_0: setKey(7, 2, pressed); break;
        case SDLK_1: setKey(0, 3, pressed); break;
        case SDLK_2: setKey(1, 3, pressed); break;
        case SDLK_3: setKey(1, 1, pressed); break;
        case SDLK_4: setKey(2, 1, pressed); break;
        case SDLK_5: setKey(3, 1, pressed); break;
        case SDLK_6: setKey(4, 3, pressed); break;
        case SDLK_7: setKey(4, 2, pressed); break;
        case SDLK_8: setKey(5, 1, pressed); break;
        case SDLK_9: setKey(6, 2, pressed); break;

        // Letters
        case SDLK_a: setKey(1, 4, pressed); break;
        case SDLK_b: setKey(4, 6, pressed); break;
        case SDLK_c: setKey(2, 5, pressed); break;
        case SDLK_d: setKey(2, 3, pressed); break;
        case SDLK_e: setKey(2, 2, pressed); break;
        case SDLK_f: setKey(3, 4, pressed); break;
        case SDLK_g: setKey(3, 5, pressed); break;
        case SDLK_h: setKey(4, 5, pressed); break;
        case SDLK_i: setKey(5, 2, pressed); break;
        case SDLK_j: setKey(5, 4, pressed); break;
        case SDLK_k: setKey(6, 4, pressed); break;
        case SDLK_l: setKey(6, 5, pressed); break;
        case SDLK_m: setKey(5, 6, pressed); break;
        case SDLK_n: setKey(5, 5, pressed); break;
        case SDLK_o: setKey(6, 3, pressed); break;
        case SDLK_p: setKey(7, 3, pressed); break;
        case SDLK_q: setKey(0, 1, pressed); break;
        case SDLK_r: setKey(3, 3, pressed); break;
        case SDLK_s: setKey(1, 5, pressed); break;
        case SDLK_t: setKey(3, 2, pressed); break;
        case SDLK_u: setKey(5, 3, pressed); break;
        case SDLK_v: setKey(3, 6, pressed); break;
        case SDLK_w: setKey(1, 2, pressed); break;
        case SDLK_x: setKey(2, 4, pressed); break;
        case SDLK_y: setKey(4, 4, pressed); break;
        case SDLK_z: setKey(1, 6, pressed); break;

        // Space, Return, Delete
        case SDLK_SPACE:     setKey(2, 6, pressed); break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:  setKey(9, 4, pressed); break;
        case SDLK_BACKSPACE: setKey(9, 5, pressed); break;
        case SDLK_TAB:       setKey(0, 7, pressed); break;

        // Cursor / Arrow keys
        case SDLK_LEFT:  setKey(9, 7, pressed); break;
        case SDLK_RIGHT: setKey(9, 1, pressed); break;
        case SDLK_UP:    setKey(9, 3, pressed); break;
        case SDLK_DOWN:  setKey(9, 2, pressed); break;

        // Symbols
        case SDLK_MINUS:        setKey(7, 1, pressed); break;
        case SDLK_EQUALS:       setKey(7, 1, pressed); shiftPressed = pressed; setKey(0, 0, pressed); break;
        case SDLK_SEMICOLON:    setKey(7, 5, pressed); break;
        case SDLK_COMMA:        setKey(6, 6, pressed); break;
        case SDLK_PERIOD:       setKey(7, 6, pressed); break;
        case SDLK_SLASH:        setKey(8, 6, pressed); break;
        case SDLK_LEFTBRACKET:  setKey(8, 3, pressed); break;
        case SDLK_RIGHTBRACKET: setKey(8, 5, pressed); break;
        case SDLK_BACKSLASH:    setKey(8, 7, pressed); break;
        case SDLK_QUOTE:        setKey(4, 2, pressed); shiftPressed = pressed; setKey(0, 0, pressed); break;

        // Modifiers
        case SDLK_LSHIFT:
        case SDLK_RSHIFT:
            setKey(0, 0, pressed);
            shiftPressed = pressed;
            break;
        case SDLK_LCTRL:
        case SDLK_RCTRL:
            setKey(1, 0, pressed);
            ctrlPressed = pressed;
            break;
        case SDLK_CAPSLOCK:
            setKey(0, 4, pressed);
            break;

        default:
            break;
    }
}

bool Keyboard::charToKey(char c, uint8& col, uint8& row, bool& needShift) {
    needShift = false;
    char upper = (char)std::toupper((unsigned char)c);

    if (upper >= 'A' && upper <= 'Z') {
        // BBC Micro boots with Caps Lock active.
        // Plain keypress produces uppercase; Shift inverts it to lowercase.
        needShift = (c >= 'a' && c <= 'z');
        switch (upper) {
            case 'A': col = 1; row = 4; return true;
            case 'B': col = 4; row = 6; return true;
            case 'C': col = 2; row = 5; return true;
            case 'D': col = 2; row = 3; return true;
            case 'E': col = 2; row = 2; return true;
            case 'F': col = 3; row = 4; return true;
            case 'G': col = 3; row = 5; return true;
            case 'H': col = 4; row = 5; return true;
            case 'I': col = 5; row = 2; return true;
            case 'J': col = 5; row = 4; return true;
            case 'K': col = 6; row = 4; return true;
            case 'L': col = 6; row = 5; return true;
            case 'M': col = 5; row = 6; return true;
            case 'N': col = 5; row = 5; return true;
            case 'O': col = 6; row = 3; return true;
            case 'P': col = 7; row = 3; return true;
            case 'Q': col = 0; row = 1; return true;
            case 'R': col = 3; row = 3; return true;
            case 'S': col = 1; row = 5; return true;
            case 'T': col = 3; row = 2; return true;
            case 'U': col = 5; row = 3; return true;
            case 'V': col = 3; row = 6; return true;
            case 'W': col = 1; row = 2; return true;
            case 'X': col = 2; row = 4; return true;
            case 'Y': col = 4; row = 4; return true;
            case 'Z': col = 1; row = 6; return true;
        }
    }

    if (c >= '0' && c <= '9') {
        switch (c) {
            case '0': col = 7; row = 2; return true;
            case '1': col = 0; row = 3; return true;
            case '2': col = 1; row = 3; return true;
            case '3': col = 1; row = 1; return true;
            case '4': col = 2; row = 1; return true;
            case '5': col = 3; row = 1; return true;
            case '6': col = 4; row = 3; return true;
            case '7': col = 4; row = 2; return true;
            case '8': col = 5; row = 1; return true;
            case '9': col = 6; row = 2; return true;
        }
    }

    switch (c) {
        case ' ': col = 2; row = 6; return true;
        case '\n':
        case '\r': col = 9; row = 4; return true; // Return key on BBC Micro
        case '\b': col = 9; row = 5; return true; // Delete key on BBC Micro
        case ':': col = 8; row = 4; return true;
        case ';': col = 7; row = 5; return true;
        case ',': col = 6; row = 6; return true;
        case '.': col = 7; row = 6; return true;
        case '/': col = 8; row = 6; return true;
        case '-': col = 0; row = 7; return true;
        case '^': col = 8; row = 1; return true;
        case '@': col = 7; row = 4; return true;
        case '[': col = 8; row = 3; return true;
        case ']': col = 8; row = 5; return true;
        case '\\': col = 8; row = 7; return true;
        case '_': col = 8; row = 2; return true;

        // Shifted symbols
        case '!': col = 0; row = 3; needShift = true; return true; // Shift + '1'
        case '"': col = 1; row = 3; needShift = true; return true; // Shift + '2'
        case '#': col = 1; row = 1; needShift = true; return true; // Shift + '3'
        case '$': col = 2; row = 1; needShift = true; return true; // Shift + '4'
        case '%': col = 3; row = 1; needShift = true; return true; // Shift + '5'
        case '&': col = 4; row = 3; needShift = true; return true; // Shift + '6'
        case '\'': col = 4; row = 2; needShift = true; return true; // Shift + '7'
        case '(': col = 5; row = 1; needShift = true; return true; // Shift + '8'
        case ')': col = 6; row = 2; needShift = true; return true; // Shift + '9'
        case '*': col = 8; row = 4; needShift = true; return true; // Shift + ':'
        case '+': col = 7; row = 5; needShift = true; return true; // Shift + ';'
        case '<': col = 6; row = 6; needShift = true; return true; // Shift + ','
        case '>': col = 7; row = 6; needShift = true; return true; // Shift + '.'
        case '?': col = 8; row = 6; needShift = true; return true; // Shift + '/'
        case '=': col = 7; row = 1; needShift = true; return true; // Shift + '-'
        case '~': col = 8; row = 1; needShift = true; return true; // Shift + '^'
    }

    return false;
}

void Keyboard::pasteText(const std::string& text) {
    for (char c : text) {
        pasteQueue.push(c);
    }
}

void Keyboard::updatePaste() {
    if (pasteHold > 0) {
        pasteHold--;
        if (pasteHold == 0) {
            // Release key
            uint8 col, row;
            bool shift;
            if (charToKey(currentPasteChar, col, row, shift)) {
                setKey(col, row, false);
                setKey(0, 0, false);
            }
            pasteDelay = 4; // Gap between keys
        }
        return;
    }

    if (pasteDelay > 0) {
        pasteDelay--;
        return;
    }

    if (!pasteQueue.empty()) {
        currentPasteChar = pasteQueue.front();
        pasteQueue.pop();

        uint8 col, row;
        bool shift;
        if (charToKey(currentPasteChar, col, row, shift)) {
            if (shift) setKey(0, 0, true);
            setKey(col, row, true);
            pasteHold = 5; // Hold key down for 5 frames
            std::cout << "[PASTE] Key: '" << currentPasteChar << "' (col=" << (int)col << ", row=" << (int)row << ", shift=" << shift << ")\n";
        }
    }
}
