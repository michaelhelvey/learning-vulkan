CC := clang
BUILDDIR := build
SRCDIR := src
TARGET := $(BUILDDIR)/main
MODE ?= debug
GLSLC := glslc

SHADERDIR := shaders
SHADERS := $(wildcard $(SHADERDIR)/*.frag $(SHADERDIR)/*.vert)
SHADERS_OUT := $(SHADERS:%=%.spv)

SRCS := $(wildcard $(SRCDIR)/*.c)
OBJS := $(patsubst $(SRCDIR)/%.c, $(BUILDDIR)/%.o, $(SRCS))

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

$(TARGET): $(OBJS) $(SHADERS_OUT)
	$(CC) $(LDFLAGS) $(OBJS) -o $@

$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

%.spv: %
	$(GLSLC) $< -o $@

clean:
	rm -rf $(BUILDDIR)
	rm -f $(SHADERS_OUT)
