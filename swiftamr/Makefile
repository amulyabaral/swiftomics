# SwiftAMR Makefile
# Compiles both native binary (for testing) and WebAssembly

CC = gcc
EMCC = ../emsdk/upstream/emscripten/emcc

CFLAGS = -O3 -Wall -std=c99
EMFLAGS = -O3 \
          -s WASM=1 \
          -s EXPORTED_FUNCTIONS='["_swiftamr_build_index","_swiftamr_align_fastq","_swiftamr_get_stats","_swiftamr_cleanup","_malloc","_free"]' \
          -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","UTF8ToString","writeArrayToMemory"]' \
          -s ALLOW_MEMORY_GROWTH=1 \
          -s INITIAL_MEMORY=128MB \
          -s MAXIMUM_MEMORY=2GB \
          -s MODULARIZE=1 \
          -s EXPORT_NAME='createSwiftAMRModule' \
          -s ENVIRONMENT='web,worker' \
          --no-entry

SOURCES = swiftamr.c main.c
HEADERS = swiftamr.h

# Targets
all: native wasm

native: $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) $(SOURCES) -o swiftamr -lm

wasm: $(SOURCES) $(HEADERS)
	$(EMCC) $(EMFLAGS) $(SOURCES) -o swiftamr.js

clean:
	rm -f swiftamr swiftamr.js swiftamr.wasm

test: native
	./swiftamr test_db.fasta test_reads.fastq

.PHONY: all native wasm clean test
