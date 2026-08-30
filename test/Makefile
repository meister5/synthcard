# Host unit tests for the Arduino-free half of SynthCard.
SRC_DIR := ../SynthCard/src
CXX     ?= g++
CXXFLAGS := -std=c++17 -O1 -g -Wall -Wextra -Wno-unused-parameter -I$(SRC_DIR)

SOURCES := unit_tests.cpp \
           $(SRC_DIR)/audio/patch.cpp \
           $(SRC_DIR)/audio/voice.cpp \
           $(SRC_DIR)/audio/drums.cpp \
           $(SRC_DIR)/audio/effects.cpp \
           $(SRC_DIR)/sequencer/sequencer.cpp \
           $(SRC_DIR)/music/music.cpp \
           $(SRC_DIR)/storage/serialize.cpp

unit_tests: $(SOURCES)
	$(CXX) $(CXXFLAGS) -o $@ $(SOURCES)

test: unit_tests
	./unit_tests

clean:
	rm -f unit_tests ui_tests

.PHONY: test clean

# --- UI tests ---------------------------------------------------------------
# Builds the real ui/, screens/, app and engine sources against host stand-ins
# for M5GFX and FreeRTOS, under ASan/UBSan.
UI_SOURCES := ui_tests.cpp host_stubs.cpp \
              $(SRC_DIR)/app.cpp \
              $(SRC_DIR)/ui/ui.cpp \
              $(SRC_DIR)/ui/screens.cpp \
              $(SRC_DIR)/input/keys.cpp \
              $(SRC_DIR)/audio/engine.cpp \
              $(SRC_DIR)/audio/patch.cpp \
              $(SRC_DIR)/audio/voice.cpp \
              $(SRC_DIR)/audio/drums.cpp \
              $(SRC_DIR)/audio/effects.cpp \
              $(SRC_DIR)/sequencer/sequencer.cpp \
              $(SRC_DIR)/music/music.cpp \
              $(SRC_DIR)/storage/serialize.cpp

UIFLAGS := -std=c++17 -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-narrowing \
           -fsanitize=address,undefined -fno-omit-frame-pointer \
           -I$(SRC_DIR) -Istub

ui_tests: $(UI_SOURCES)
	$(CXX) $(UIFLAGS) -o $@ $(UI_SOURCES)

ui-test: ui_tests
	./ui_tests

all-tests: test ui-test

.PHONY: ui-test all-tests
