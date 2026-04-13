CC=gcc
FLAGS = -fno-builtin -nostdlib -static
SOURCES = cli.c

.PHONY: build

setup: 
	mkdir -p build

build: FLAGS += -O2
build: setup
	$(CC) $(FLAGS) $(SOURCES) -o ./build/cli
