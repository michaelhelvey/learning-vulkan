CC := clang
OBJC := clang
BUILDDIR := build
SRCDIR := src
TARGET := $(BUILDDIR)/main
MODE ?= debug

SRCS := $(wildcard $(SRCDIR)/*.c) $(wildcard $(SRCDIR)/*.m)
OBJS := $(patsubst $(SRCDIR)/%.c, $(BUILDDIR)/%.o, $(SRCS:.m=.c))

INCLUDE_FLAGS := -I/usr/local/include/ -I/opt/homebrew/include

ifeq ($(MODE), release)
	CFLAGS := -Wall -Wextra -O2 $(INCLUDE_FLAGS)
else
	CFLAGS := -Wall -Wextra -g -O0 $(INCLUDE_FLAGS)
endif

LDFLAGS := \
	-L/usr/local/lib \
	-L/opt/homebrew/lib \
	-lvulkan \
	-lsdl2 \
	-rpath $(HOME)/dev/vulkan/current/macOS/lib

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) $^ -o $@

$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/%.o: $(SRCDIR)/%.m | $(BUILDDIR)
	$(OBJC) $(CFLAGS) -c $< -o $@

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

clean:
	rm -rf $(BUILDDIR)
