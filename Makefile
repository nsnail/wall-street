CXX ?= g++
WINDRES ?= windres

TARGET := WallStreetTicker.exe
BUILD_DIR := build
SRC := src/main.cpp
OBJ := $(BUILD_DIR)/main.o

CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Wpedantic -municode
LDFLAGS := -mwindows -municode -static -static-libgcc -static-libstdc++
LDLIBS := -lgdi32 -luser32 -lshell32 -lwinhttp -lwinmm -lcomctl32 -lshlwapi

.PHONY: all clean run

all: $(BUILD_DIR)/$(TARGET)

$(BUILD_DIR):
	mkdir -p "$(BUILD_DIR)"

$(OBJ): $(SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/$(TARGET): $(OBJ)
	$(CXX) $(LDFLAGS) $^ -o $@ $(LDLIBS)
	cp -f appsettings.json "$(BUILD_DIR)/appsettings.json"

run: all
	"$(BUILD_DIR)\$(TARGET)"

clean:
	rm -rf "$(BUILD_DIR)"
