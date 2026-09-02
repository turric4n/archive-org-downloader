# GNU Makefile for archive-org-downloader (Linux / macOS)
#
# Requires: gcc (or clang), libcurl development headers
#   Debian/Ubuntu:  sudo apt install libcurl4-openssl-dev
#   Fedora:         sudo dnf install libcurl-devel
#   macOS:          brew install curl (usually already available)

CC       ?= cc
CFLAGS   ?= -O2 -Wall -Wextra -std=c11
CPPFLAGS += -Isrc
LDLIBS   += -lcurl
LDFLAGS  ?=

# Build version, embedded via -DVERSION. Override with:
#   make VERSION="$(git describe --tags --always)"
ifndef VERSION
VERSION := dev
endif
CPPFLAGS += -DVERSION=\"$(VERSION)\"

SRC_DIR   = src
OBJ_DIR   = obj

SOURCES = $(SRC_DIR)/main.c \
          $(SRC_DIR)/log.c \
          $(SRC_DIR)/util.c \
          $(SRC_DIR)/color.c \
          $(SRC_DIR)/http.c \
          $(SRC_DIR)/auth.c \
          $(SRC_DIR)/archive.c \
          $(SRC_DIR)/downloader.c \
          $(SRC_DIR)/parson.c

OBJECTS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SOURCES))

DEPS    = $(OBJECTS:.o=.d)

TARGET  = archive_downloader

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $(OBJECTS) $(LDLIBS)

$(OBJ_DIR):
	@mkdir -p $(OBJ_DIR)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c -o $@ $<

-include $(DEPS)

clean:
	rm -rf $(OBJ_DIR) $(TARGET)

.PHONY: all clean