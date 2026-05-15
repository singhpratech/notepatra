# Notepatra palette preview — synthetic; no real data

# Variables — three flavors
CC      = gcc
CXX    := g++
PREFIX ?= /usr/local

CFLAGS   := -Wall -Wextra -O2 -std=c11
CXXFLAGS := -Wall -Wextra -O2 -std=c++17
LDFLAGS  :=
LDLIBS   := -lm

BUILD_DIR := build
SRC_DIR   := src

SRCS  := $(wildcard $(SRC_DIR)/*.c)
OBJS  := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))
BIN   := $(BUILD_DIR)/notepatra-demo

# Optional include
-include local.mk

.PHONY: all build clean install uninstall test help

all: build

build: $(BIN)

$(BIN): $(OBJS)
	@mkdir -p $(@D)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf $(BUILD_DIR)

install: $(BIN)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 0755 $(BIN) $(DESTDIR)$(PREFIX)/bin/

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/notepatra-demo

test:
	@echo "Running tests..."
	@./scripts/run-tests.sh

ifeq ($(OS),Windows_NT)
  PLATFORM := windows
else
  PLATFORM := $(shell uname -s | tr '[:upper:]' '[:lower:]')
endif

help:
	@echo "Targets: all build clean install uninstall test help"
	@echo "Platform detected: $(PLATFORM)"
