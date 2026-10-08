CC ?= cc
AR ?= ar
CPPFLAGS ?=
CFLAGS ?= -O2
LDFLAGS ?=
ASAN_OPTIONS ?= detect_leaks=1
WARNINGS = -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
INCLUDES = -Iinclude

.PHONY: all test test-idf sanitize sanitize-idf clean

all: libsc_touch.a libsc_touch.so

build:
	mkdir -p build

build/sc_touch.o: src/sc_touch.c include/sc_touch.h | build
	$(CC) $(CPPFLAGS) $(INCLUDES) $(CFLAGS) $(WARNINGS) -fPIC -c $< -o $@

build/sc_capture.o: src/sc_capture.c include/sc_capture.h include/sc_touch.h | build
	$(CC) $(CPPFLAGS) $(INCLUDES) $(CFLAGS) $(WARNINGS) -fPIC -c $< -o $@

build/sc_airkiss.o: src/sc_airkiss.c include/sc_airkiss.h include/sc_touch.h | build
	$(CC) $(CPPFLAGS) $(INCLUDES) $(CFLAGS) $(WARNINGS) -fPIC -c $< -o $@

build/sc_airkiss_capture.o: src/sc_airkiss_capture.c include/sc_airkiss_capture.h include/sc_airkiss.h include/sc_capture.h | build
	$(CC) $(CPPFLAGS) $(INCLUDES) $(CFLAGS) $(WARNINGS) -fPIC -c $< -o $@

build/sc_touch2.o: src/sc_touch2.c include/sc_touch2.h include/sc_touch.h | build
	$(CC) $(CPPFLAGS) $(INCLUDES) $(CFLAGS) $(WARNINGS) -fPIC -c $< -o $@

build/sc_touch2_capture.o: src/sc_touch2_capture.c include/sc_touch2_capture.h include/sc_touch2.h include/sc_capture.h | build
	$(CC) $(CPPFLAGS) $(INCLUDES) $(CFLAGS) $(WARNINGS) -fPIC -c $< -o $@

libsc_touch.a: build/sc_touch.o build/sc_capture.o build/sc_airkiss.o build/sc_airkiss_capture.o build/sc_touch2.o build/sc_touch2_capture.o
	$(AR) rcs $@ $^

libsc_touch.so: build/sc_touch.o build/sc_capture.o build/sc_airkiss.o build/sc_airkiss_capture.o build/sc_touch2.o build/sc_touch2_capture.o
	$(CC) -shared $(LDFLAGS) $^ -o $@

build/test_sc_touch: tests/test_sc_touch.c src/sc_touch.c include/sc_touch.h | build
	$(CC) $(CPPFLAGS) $(INCLUDES) $(CFLAGS) $(WARNINGS) tests/test_sc_touch.c src/sc_touch.c $(LDFLAGS) -o $@

build/test_sc_capture: tests/test_sc_capture.c src/sc_capture.c src/sc_touch.c include/sc_capture.h include/sc_touch.h | build
	$(CC) $(CPPFLAGS) $(INCLUDES) $(CFLAGS) $(WARNINGS) tests/test_sc_capture.c src/sc_capture.c src/sc_touch.c $(LDFLAGS) -o $@

AIRKISS_TEST_INPUTS = tests/test_sc_airkiss.c src/sc_airkiss.c src/sc_airkiss_capture.c src/sc_touch.c
AIRKISS_TEST_HEADERS = include/sc_airkiss.h include/sc_airkiss_capture.h include/sc_capture.h include/sc_touch.h
build/test_sc_airkiss: $(AIRKISS_TEST_INPUTS) $(AIRKISS_TEST_HEADERS) | build
	$(CC) $(CPPFLAGS) $(INCLUDES) $(CFLAGS) $(WARNINGS) $(AIRKISS_TEST_INPUTS) $(LDFLAGS) -o $@

TOUCH2_TEST_INPUTS = tests/test_sc_touch2.c src/sc_touch2.c src/sc_touch2_capture.c src/sc_touch.c
TOUCH2_TEST_HEADERS = include/sc_touch2.h include/sc_touch2_capture.h include/sc_capture.h include/sc_touch.h
build/test_sc_touch2: $(TOUCH2_TEST_INPUTS) $(TOUCH2_TEST_HEADERS) | build
	$(CC) $(CPPFLAGS) $(INCLUDES) $(CFLAGS) $(WARNINGS) $(TOUCH2_TEST_INPUTS) $(LDFLAGS) -o $@

TOUCH2_GUIDE_TEST_INPUTS = tests/probe_touch2_guide_overlap.c src/sc_touch2_capture.c src/sc_touch2.c src/sc_touch.c
build/probe_touch2_guide_overlap: $(TOUCH2_GUIDE_TEST_INPUTS) $(TOUCH2_TEST_HEADERS) | build
	$(CC) $(CPPFLAGS) $(INCLUDES) $(CFLAGS) $(WARNINGS) $(TOUCH2_GUIDE_TEST_INPUTS) $(LDFLAGS) -o $@

test: build/test_sc_touch build/test_sc_capture build/test_sc_airkiss build/test_sc_touch2 build/probe_touch2_guide_overlap
	./build/test_sc_touch
	./build/test_sc_capture
	./build/test_sc_airkiss
	./build/test_sc_touch2
	./build/probe_touch2_guide_overlap

IDF_TEST_INPUTS = tests/test_sc_touch_idf.c idf/sc_touch_idf.c src/sc_capture.c src/sc_touch.c
IDF_TEST_HEADERS = $(wildcard tests/idf_stubs/*.h) tests/idf_stubs/freertos/FreeRTOS.h idf/sc_touch_idf.h include/sc_capture.h include/sc_touch.h
build/test_sc_touch_idf: $(IDF_TEST_INPUTS) $(IDF_TEST_HEADERS) | build
	$(CC) $(CPPFLAGS) $(INCLUDES) -Itests/idf_stubs -Iidf $(CFLAGS) $(WARNINGS) $(IDF_TEST_INPUTS) $(LDFLAGS) -o $@

AIRKISS_IDF_TEST_INPUTS = tests/test_sc_airkiss_idf.c idf/sc_airkiss_idf.c src/sc_airkiss_capture.c src/sc_airkiss.c src/sc_touch.c
build/test_sc_airkiss_idf: $(AIRKISS_IDF_TEST_INPUTS) $(IDF_TEST_HEADERS) idf/sc_airkiss_idf.h $(AIRKISS_TEST_HEADERS) | build
	$(CC) $(CPPFLAGS) $(INCLUDES) -Itests/idf_stubs -Iidf $(CFLAGS) $(WARNINGS) $(AIRKISS_IDF_TEST_INPUTS) $(LDFLAGS) -o $@

TOUCH2_IDF_TEST_INPUTS = tests/test_sc_touch2_idf.c idf/sc_touch2_idf.c src/sc_touch2_capture.c src/sc_touch2.c src/sc_touch.c
build/test_sc_touch2_idf: $(TOUCH2_IDF_TEST_INPUTS) $(IDF_TEST_HEADERS) idf/sc_touch2_idf.h idf/sc_touch2_psa.h $(TOUCH2_TEST_HEADERS) | build
	$(CC) $(CPPFLAGS) $(INCLUDES) -Itests/idf_stubs -Iidf $(CFLAGS) $(WARNINGS) $(TOUCH2_IDF_TEST_INPUTS) $(LDFLAGS) -o $@

PSA_TEST_INPUTS = tests/test_sc_touch2_psa.c idf/sc_touch2_psa.c
PSA_TEST_HEADERS = idf/sc_touch2_psa.h include/sc_touch2.h tests/idf_stubs/psa/crypto.h
build/test_sc_touch2_psa: $(PSA_TEST_INPUTS) $(PSA_TEST_HEADERS) | build
	$(CC) $(CPPFLAGS) $(INCLUDES) -Itests/idf_stubs -Iidf $(CFLAGS) $(WARNINGS) $(PSA_TEST_INPUTS) $(LDFLAGS) -o $@

test-idf: build/test_sc_touch_idf build/test_sc_airkiss_idf build/test_sc_touch2_idf build/test_sc_touch2_psa
	./build/test_sc_touch_idf
	./build/test_sc_airkiss_idf
	./build/test_sc_touch2_idf
	./build/test_sc_touch2_psa

build/test_sc_touch_idf_sanitize: $(IDF_TEST_INPUTS) $(IDF_TEST_HEADERS) | build
	$(CC) $(CPPFLAGS) $(INCLUDES) -Itests/idf_stubs -Iidf -O1 -g $(WARNINGS) -fno-omit-frame-pointer -fsanitize=address,undefined $(IDF_TEST_INPUTS) $(LDFLAGS) -o $@

build/test_sc_airkiss_idf_sanitize: $(AIRKISS_IDF_TEST_INPUTS) $(IDF_TEST_HEADERS) idf/sc_airkiss_idf.h $(AIRKISS_TEST_HEADERS) | build
	$(CC) $(CPPFLAGS) $(INCLUDES) -Itests/idf_stubs -Iidf -O1 -g $(WARNINGS) -fno-omit-frame-pointer -fsanitize=address,undefined $(AIRKISS_IDF_TEST_INPUTS) $(LDFLAGS) -o $@

build/test_sc_touch2_idf_sanitize: $(TOUCH2_IDF_TEST_INPUTS) $(IDF_TEST_HEADERS) idf/sc_touch2_idf.h idf/sc_touch2_psa.h $(TOUCH2_TEST_HEADERS) | build
	$(CC) $(CPPFLAGS) $(INCLUDES) -Itests/idf_stubs -Iidf -O1 -g $(WARNINGS) -fno-omit-frame-pointer -fsanitize=address,undefined $(TOUCH2_IDF_TEST_INPUTS) $(LDFLAGS) -o $@

build/test_sc_touch2_psa_sanitize: $(PSA_TEST_INPUTS) $(PSA_TEST_HEADERS) | build
	$(CC) $(CPPFLAGS) $(INCLUDES) -Itests/idf_stubs -Iidf -O1 -g $(WARNINGS) -fno-omit-frame-pointer -fsanitize=address,undefined $(PSA_TEST_INPUTS) $(LDFLAGS) -o $@

sanitize-idf: build/test_sc_touch_idf_sanitize build/test_sc_airkiss_idf_sanitize build/test_sc_touch2_idf_sanitize build/test_sc_touch2_psa_sanitize
	ASAN_OPTIONS='$(ASAN_OPTIONS)' ./build/test_sc_touch_idf_sanitize
	ASAN_OPTIONS='$(ASAN_OPTIONS)' ./build/test_sc_airkiss_idf_sanitize
	ASAN_OPTIONS='$(ASAN_OPTIONS)' ./build/test_sc_touch2_idf_sanitize
	ASAN_OPTIONS='$(ASAN_OPTIONS)' ./build/test_sc_touch2_psa_sanitize

build/test_sc_touch_sanitize: tests/test_sc_touch.c src/sc_touch.c include/sc_touch.h | build
	$(CC) $(CPPFLAGS) $(INCLUDES) -O1 -g $(WARNINGS) -fno-omit-frame-pointer -fsanitize=address,undefined tests/test_sc_touch.c src/sc_touch.c $(LDFLAGS) -o $@

build/test_sc_capture_sanitize: tests/test_sc_capture.c src/sc_capture.c src/sc_touch.c include/sc_capture.h include/sc_touch.h | build
	$(CC) $(CPPFLAGS) $(INCLUDES) -O1 -g $(WARNINGS) -fno-omit-frame-pointer -fsanitize=address,undefined tests/test_sc_capture.c src/sc_capture.c src/sc_touch.c $(LDFLAGS) -o $@

build/test_sc_airkiss_sanitize: $(AIRKISS_TEST_INPUTS) $(AIRKISS_TEST_HEADERS) | build
	$(CC) $(CPPFLAGS) $(INCLUDES) -O1 -g $(WARNINGS) -fno-omit-frame-pointer -fsanitize=address,undefined $(AIRKISS_TEST_INPUTS) $(LDFLAGS) -o $@

build/test_sc_touch2_sanitize: $(TOUCH2_TEST_INPUTS) $(TOUCH2_TEST_HEADERS) | build
	$(CC) $(CPPFLAGS) $(INCLUDES) -O1 -g $(WARNINGS) -fno-omit-frame-pointer -fsanitize=address,undefined $(TOUCH2_TEST_INPUTS) $(LDFLAGS) -o $@

build/probe_touch2_guide_overlap_sanitize: $(TOUCH2_GUIDE_TEST_INPUTS) $(TOUCH2_TEST_HEADERS) | build
	$(CC) $(CPPFLAGS) $(INCLUDES) -O1 -g $(WARNINGS) -fno-omit-frame-pointer -fsanitize=address,undefined $(TOUCH2_GUIDE_TEST_INPUTS) $(LDFLAGS) -o $@

sanitize: build/test_sc_touch_sanitize build/test_sc_capture_sanitize build/test_sc_airkiss_sanitize build/test_sc_touch2_sanitize build/probe_touch2_guide_overlap_sanitize
	ASAN_OPTIONS='$(ASAN_OPTIONS)' ./build/test_sc_touch_sanitize
	ASAN_OPTIONS='$(ASAN_OPTIONS)' ./build/test_sc_capture_sanitize
	ASAN_OPTIONS='$(ASAN_OPTIONS)' ./build/test_sc_airkiss_sanitize
	ASAN_OPTIONS='$(ASAN_OPTIONS)' ./build/test_sc_touch2_sanitize
	ASAN_OPTIONS='$(ASAN_OPTIONS)' ./build/probe_touch2_guide_overlap_sanitize

clean:
	rm -rf build libsc_touch.a libsc_touch.so
