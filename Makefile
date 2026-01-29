# Pith Makefile
# 
# Builds the Pith editor runtime.
# Requires raylib to be installed.

# Compiler
CC = clang

# Directories
SRC_DIR = src
INC_DIR = include
BUILD_DIR = build

# Source files
SOURCES = $(SRC_DIR)/main.c \
          $(SRC_DIR)/pith_runtime.c \
          $(SRC_DIR)/pith_ui.c

# Object files
OBJECTS = $(SOURCES:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

# Output
TARGET = pith

# Compiler flags
CFLAGS = -Wall -Wextra -std=c11 -I$(INC_DIR)
CFLAGS += -g  # Debug symbols

# Platform detection
UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Darwin)
    # macOS
    # Check for Homebrew raylib
    RAYLIB_PATH ?= $(shell brew --prefix raylib 2>/dev/null || echo "/usr/local")
    CFLAGS += -I$(RAYLIB_PATH)/include
    LDFLAGS = -L$(RAYLIB_PATH)/lib -lraylib
    LDFLAGS += -framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo
else ifeq ($(UNAME_S),Linux)
    # Linux
    LDFLAGS = -lraylib -lGL -lm -lpthread -ldl -lrt -lX11
else
    # Windows (MinGW)
    LDFLAGS = -lraylib -lopengl32 -lgdi32 -lwinmm
    TARGET = pith.exe
endif

# Release build
release: CFLAGS += -O2 -DNDEBUG
release: all

# Debug build (default)
debug: CFLAGS += -O0 -DDEBUG
debug: all

# Default target
all: $(BUILD_DIR) $(TARGET)

# Create build directory
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Link
$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)
	@echo "Built $(TARGET)"

# Compile
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean
clean:
	rm -rf $(BUILD_DIR) $(TARGET)

# Install (macOS/Linux)
install: $(TARGET)
	cp $(TARGET) /usr/local/bin/
	@echo "Installed to /usr/local/bin/$(TARGET)"

# Uninstall
uninstall:
	rm -f /usr/local/bin/$(TARGET)

# Run
run: $(TARGET)
	./$(TARGET)

# Run with example project
run-example: $(TARGET)
	./$(TARGET) examples/hello

# Run tests
test: $(TARGET)
	@./test/run-tests.sh

# Format code (requires clang-format)
format:
	clang-format -i $(SRC_DIR)/*.c $(INC_DIR)/*.h

# Check for raylib
check-deps:
	@which raylib-config > /dev/null 2>&1 || (echo "raylib not found. Install with: brew install raylib (macOS) or apt install libraylib-dev (Linux)" && exit 1)
	@echo "Dependencies OK"

.PHONY: all clean install uninstall run run-example test format check-deps release debug
