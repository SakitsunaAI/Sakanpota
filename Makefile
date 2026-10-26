CXX = clang++
SDL_CFLAGS = -I/opt/local/include/SDL2 -D_THREAD_SAFE
SDL_LIBS = -L/opt/local/lib -lSDL2

CXXFLAGS = -std=c++17 -O3 -march=native -Wall -Wextra -Wno-unused-parameter $(SDL_CFLAGS) -Iinclude -Isrc -Isrc/pokemini -Isrc/pk201/psemu
LDFLAGS = $(SDL_LIBS) -lz -L. -lpokemini -lpk201

TARGET = sakanpota

SRCS = src/main.cpp \
       src/cpu6502.cpp \
       src/memory.cpp \
       src/via6522.cpp \
       src/sn76489.cpp \
       src/saa5050.cpp \
       src/crtc6845.cpp \
       src/fdc1770.cpp \
       src/keyboard.cpp \
       src/debugger.cpp \
       src/crt_filter.cpp \
       src/model_detector.cpp \
       src/bbc_machine.cpp \
       src/dos_cpu8086.cpp \
       src/dos_machine.cpp \
       src/c64_machine.cpp \
       src/mac_cpu68000.cpp \
       src/mac_machine.cpp \
       src/lisa_machine.cpp \
       src/cpu_arm7.cpp \
       src/pocketstation_machine.cpp \
       src/atari_machine.cpp \
       src/cpu_z80.cpp \
       src/jupiter_ace_machine.cpp \
       src/enterprise_machine.cpp \
       src/sharp_mz_machine.cpp \
       src/sinclair_ql_machine.cpp \
       src/oric_machine.cpp \
       src/lynx_machine.cpp \
       src/camputers_lynx_machine.cpp \
       src/vmu_machine.cpp \
       src/embedded_vmu_games.cpp \
       src/vm2_machine.cpp \
       src/vmu_pro_machine.cpp \
       src/embedded_nextgen_vmu.cpp \
       src/tamagotchi_machine.cpp \
       src/supervision_machine.cpp \
       src/ti_machine.cpp \
       src/studio2_machine.cpp \
       src/embedded_pokemon_mini.cpp \
       src/pokemon_mini_machine.cpp \
       src/cpu_tms1100.cpp \
       src/microvision_machine.cpp \
       src/embedded_roms_ext.cpp \
       src/spectrum_machine.cpp \
       src/cpc_machine.cpp \
       src/odyssey2_machine.cpp \
       src/newton_cpu_arm.cpp \
       src/newton_runt.cpp \
       src/newton_machine.cpp \
       src/embedded_newton_rom.cpp \
       src/pico8_machine.cpp \
       src/embedded_pico8_carts.cpp \
       src/flipper_machine.cpp \
       src/embedded_flipper_assets.cpp \
       src/universal_detector.cpp \
       src/embedded_roms.cpp \
       src/crypto_secure.cpp \
       src/gui_font.cpp

OBJS = $(SRCS:.cpp=.o)

all: libpokemini.a libpk201.a $(TARGET)

libpokemini.a:
	clang -O3 -march=native -Isrc/pokemini -c src/pokemini/PokeMini.c src/pokemini/Hardware.c src/pokemini/MinxCPU.c src/pokemini/MinxCPU_CE.c src/pokemini/MinxCPU_CF.c src/pokemini/MinxCPU_SP.c src/pokemini/MinxCPU_XX.c src/pokemini/MinxLCD.c src/pokemini/MinxPRC.c src/pokemini/MinxColorPRC.c src/pokemini/MinxAudio.c src/pokemini/MinxTimers.c src/pokemini/MinxIO.c src/pokemini/MinxIRQ.c src/pokemini/Multicart.c src/pokemini/Video.c src/pokemini/Video_x1.c src/pokemini/Video_x2.c src/pokemini/Video_x3.c src/pokemini/Video_x4.c src/pokemini/Video_x5.c src/pokemini/Video_x6.c src/pokemini/PokeMini_ColorPal.c src/pokemini/CommandLine.c src/pokemini/PMCommon.c src/pokemini/freebios.c src/pokemini/unzip.c src/pokemini/ioapi.c
	ar rcs libpokemini.a PokeMini.o Hardware.o MinxCPU*.o MinxLCD.o MinxPRC.o MinxColorPRC.o MinxAudio.o MinxTimers.o MinxIO.o MinxIRQ.o Multicart.o Video*.o PokeMini_ColorPal.o CommandLine.o PMCommon.o freebios.o unzip.o ioapi.o

libpk201.a:
	clang -O3 -march=native -Isrc/pk201 -Isrc/pk201/psemu -c src/pk201/arm_exec.c src/pk201/clk.c src/pk201/com.c src/pk201/cpu.c src/pk201/dac.c src/pk201/flash.c src/pk201/intc.c src/pk201/iop.c src/pk201/ir.c src/pk201/lcd.c src/pk201/memory.c src/pk201/psemu.c src/pk201/rtc.c src/pk201/state.c src/pk201/thumb_exec.c src/pk201/timer.c
	ar rcs libpk201.a arm_exec.o clk.o com.o cpu.o dac.o flash.o intc.o iop.o ir.o lcd.o memory.o psemu.o rtc.o state.o thumb_exec.o timer.o

$(TARGET): $(OBJS) libpokemini.a libpk201.a
	$(CXX) $(OBJS) -o $(TARGET) $(LDFLAGS)
	ln -sf $(TARGET) bbc_micro
	@echo "========================================="
	@echo " Sakanpota Emulator built successfully!"
	@echo " Run with: ./$(TARGET)"
	@echo "========================================="

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

install: $(TARGET)
	install -d /usr/local/bin
	install -m 755 $(TARGET) /usr/local/bin/$(TARGET)

uninstall:
	rm -f /usr/local/bin/$(TARGET)

clean:
	rm -f $(OBJS) $(TARGET) libpokemini.a libpk201.a PokeMini.o Hardware.o MinxCPU*.o MinxLCD.o MinxPRC.o MinxColorPRC.o MinxAudio.o MinxTimers.o MinxIO.o MinxIRQ.o Multicart.o Video*.o PokeMini_ColorPal.o CommandLine.o PMCommon.o arm_exec.o clk.o com.o cpu.o dac.o flash.o intc.o iop.o ir.o lcd.o memory.o psemu.o rtc.o state.o thumb_exec.o timer.o bbc_micro

.PHONY: all clean install uninstall