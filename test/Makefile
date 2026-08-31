# Host tests and tools for the Arduino-free half of SynthCard.
SRC_DIR := ../SynthCard/src
CXX     ?= g++
CXXFLAGS := -std=c++17 -O1 -g -Wall -Wextra -Wno-unused-parameter -I$(SRC_DIR)

# Headers are dependencies too. Without this a change to sequencer.h - which is
# where the step layout lives - rebuilds nothing at all, and the suite happily
# reports a pass for code that was never recompiled.
HEADERS := $(wildcard $(SRC_DIR)/*.h) $(wildcard $(SRC_DIR)/*/*.h) \
           $(wildcard stub/*.h) $(wildcard stub/*/*.h) $(wildcard *.h)

# Everything that builds without Arduino. One list, used by every target, so a
# new engine or drum family cannot be added to the firmware and forgotten here.
CORE := $(SRC_DIR)/audio/dsp.cpp \
        $(SRC_DIR)/audio/patch.cpp \
        $(SRC_DIR)/audio/voice.cpp \
        $(SRC_DIR)/audio/eng_analog.cpp \
        $(SRC_DIR)/audio/eng_fm.cpp \
        $(SRC_DIR)/audio/eng_wt.cpp \
        $(SRC_DIR)/audio/eng_pluck.cpp \
        $(SRC_DIR)/audio/eng_organ.cpp \
        $(SRC_DIR)/audio/eng_chip.cpp \
        $(SRC_DIR)/audio/wavetables.cpp \
        $(SRC_DIR)/audio/drums.cpp \
        $(SRC_DIR)/audio/drum_membrane.cpp \
        $(SRC_DIR)/audio/drum_snare.cpp \
        $(SRC_DIR)/audio/drum_metal.cpp \
        $(SRC_DIR)/audio/drum_noise.cpp \
        $(SRC_DIR)/audio/drum_perc.cpp \
        $(SRC_DIR)/audio/effects.cpp \
        $(SRC_DIR)/sequencer/sequencer.cpp \
        $(SRC_DIR)/music/music.cpp \
        $(SRC_DIR)/storage/serialize.cpp

SOURCES := unit_tests.cpp $(CORE)

unit_tests: $(SOURCES) $(HEADERS)
	$(CXX) $(CXXFLAGS) -o $@ $(SOURCES)

test: unit_tests
	./unit_tests

clean:
	rm -f unit_tests ui_tests render bench gen_controls
	rm -rf out

.PHONY: test clean

# --- UI tests ---------------------------------------------------------------
# Builds the real ui/, screens/, app and engine sources against host stand-ins
# for M5GFX and FreeRTOS, under ASan/UBSan.
UI_SOURCES := ui_tests.cpp host_stubs.cpp \
              $(SRC_DIR)/app.cpp \
              $(SRC_DIR)/ui/ui.cpp \
              $(SRC_DIR)/ui/screens.cpp \
              $(SRC_DIR)/ui/legend.cpp \
              $(SRC_DIR)/ui/tour.cpp \
              $(SRC_DIR)/input/keys.cpp \
              $(SRC_DIR)/audio/engine.cpp \
              $(CORE)

UIFLAGS := -std=c++17 -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-narrowing \
           -fsanitize=address,undefined -fno-omit-frame-pointer \
           -I$(SRC_DIR) -Istub

ui_tests: $(UI_SOURCES) $(HEADERS)
	$(CXX) $(UIFLAGS) -o $@ $(UI_SOURCES)

ui-test: ui_tests
	./ui_tests

all-tests: test ui-test

.PHONY: ui-test all-tests

# --- WAV renderer -----------------------------------------------------------
# Renders the kits, the engines and a short sequenced demo to out/*.wav, and
# prints the measurements that say whether a sound is doing what it claims.
# The point is that the numbers can be checked here and the files can be
# listened to somewhere with speakers.
RENDER_SOURCES := render.cpp $(CORE)

render: $(RENDER_SOURCES) $(HEADERS)
	$(CXX) $(CXXFLAGS) -O2 -o $@ $(RENDER_SOURCES)

render-wav: render
	./render

.PHONY: render-wav

# --- benchmark --------------------------------------------------------------
# Per-engine and per-lane cost, as a share of the real-time budget. A host CPU
# is not an LX7, so the absolute numbers mean nothing; the ratios between
# engines, and the change in them between commits, are the point.
BENCH_SOURCES := bench.cpp $(CORE)

bench: $(BENCH_SOURCES) $(HEADERS)
	$(CXX) $(CXXFLAGS) -O2 -o $@ $(BENCH_SOURCES)

bench-run: bench
	./bench

.PHONY: bench-run

# --- generated CONTROLS.md ---------------------------------------------------
# The key tables are produced by walking mapAction(), so the manual cannot
# describe a binding that does not exist or miss one that does.
CONTROLS_SOURCES := gen_controls.cpp $(SRC_DIR)/input/keys.cpp $(CORE)

gen_controls: $(CONTROLS_SOURCES) $(HEADERS)
	$(CXX) $(CXXFLAGS) -Wno-narrowing -Istub -o $@ $(CONTROLS_SOURCES) host_stubs.cpp \
	    $(SRC_DIR)/ui/ui.cpp $(SRC_DIR)/ui/screens.cpp $(SRC_DIR)/ui/legend.cpp \
	    $(SRC_DIR)/ui/tour.cpp $(SRC_DIR)/app.cpp $(SRC_DIR)/audio/engine.cpp

controls-md: gen_controls
	./gen_controls

.PHONY: controls-md
