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
	rm -f unit_tests

.PHONY: test clean
