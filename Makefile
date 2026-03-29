CC=gcc
FLAGS = -D_GNU_SOURCE -D_POSIX_C_SOURCE=200112L -W -Wall -pedantic -Werror -std=c99 `pkg-config fuse3 --cflags --libs`
SOURCES = fs/*.c fs/utils/*.c 
HEADERS = fs/*.h fs/utils/*.h 

.PHONY: build install run

setup: 
	mkdir -p X && mkdir -p build

build: FLAGS += -O2
build: setup
	$(CC) $(FLAGS) $(SOURCES) -o ./build/fschat

run: build
	./build/fschat -f "./X"

build_debug_address: FLAGS += -fsanitize=undefined,address -g -D __DEBUG__
build_debug_address: setup
	$(CC) $(FLAGS) $(SOURCES) -o ./build/fschat-debug

run_debug_address: build_debug_address
	./build/fschat-debug -f "./X" 

build_debug_thread: FLAGS += -fsanitize=undefined,thread -g -D __DEBUG__
build_debug_thread: setup
	$(CC) $(FLAGS) $(SOURCES) -o ./build/fschat-debug

run_debug_thread: build_debug_thread
	./build/fschat-debug -f "./X"

format: 
	clang-format -i $(SOURCES) $(HEADERS)