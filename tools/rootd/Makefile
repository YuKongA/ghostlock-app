NDK ?= /Users/nickji/Library/Android/sdk/ndk/30.0.16248370
CC := $(NDK)/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android31-clang
CFLAGS := -O2 -Wall -Wextra -static -s
all: build/ghostlock-rootd
build/ghostlock-rootd: rootd.c
	mkdir -p build
	$(CC) $(CFLAGS) -o $@ $<
clean:
	rm -rf build
