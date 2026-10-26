#pragma once

#include "types.h"
#include <SDL.h>
#include <string>
#include <vector>

class EmulatorCore {
public:
    virtual ~EmulatorCore() = default;

    virtual SystemType getSystemType() const = 0;
    virtual std::string getSystemName() const = 0;
    virtual std::string getMediaName() const = 0;

    virtual void init() = 0;
    virtual void reset() = 0;
    virtual void runFrame() = 0;

    // Video output: 640x512 ARGB8888 canvas buffer
    virtual const uint32* getRenderedScreen() const = 0;

    // Audio output: 50/60 Hz frame audio buffer (16-bit mono or stereo)
    virtual const int16* getAudioBuffer() const = 0;
    virtual size_t getAudioSampleCount() const = 0;

    // Keyboard & Mouse input
    virtual void handleKeyDown(SDL_Keycode key, uint16 mod) = 0;
    virtual void handleKeyUp(SDL_Keycode key, uint16 mod) = 0;
    virtual void handleMouseMotion(int x, int y) {}
    virtual void handleMouseButton(uint8 button, bool pressed, int x, int y) {}

    // Load media (disk, ROM, executable)
    virtual bool loadMedia(const std::string& path) = 0;
};
