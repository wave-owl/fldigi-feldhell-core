# fldigi-feldhell-core -- plain make, C99, no dependencies besides libm and the pinned
# fldigi-dsp-core submodule dsp/.
#   make            static + shared library into $(BUILD) (after the dsp/ pin check)
#   make test       build and run the C tests (receiver, transmitter, fldigi goldens)
#   make sanitize   the C tests under AddressSanitizer + UndefinedBehaviorSanitizer
#   make bench      CPU and memory benchmark (per submode, receiver and transmitter)
#   make check-dsp  only the dsp/ pin check (dsp.lock, R-920.3)
#   make BUILD=/abs/dir shared   out-of-tree build (used by wave-owl)
BUILD ?= build
CC ?= cc
# -ffp-contract=on: fuse a*b+c within one expression, as clang does by default and as the
# fldigi 4.2.13 reference build did.
CFLAGS ?= -O3
DSP ?= dsp
CORE_FLAGS = -std=c99 -Wall -Wextra -Werror -pedantic -ffp-contract=on -fPIC -Iinclude \
             -I$(DSP)/include
LDLIBS = -lm
# Tests rebuild the golden noise exactly as Python does: no contraction there.
TEST_FLAGS = -std=c99 -Wall -Wextra -Werror -pedantic -ffp-contract=off -Iinclude -I$(DSP)/include

UNAME := $(shell uname)
ifeq ($(UNAME),Darwin)
  SHLIB = libfldigi_feldhell.dylib
  SHFLAGS = -dynamiclib -install_name @rpath/libfldigi_feldhell.dylib
else
  SHLIB = libfldigi_feldhell.so
  SHFLAGS = -shared -Wl,-soname,libfldigi_feldhell.so
endif

DSP_SRC = fdsp_gfft fdsp_filters
HDRS = include/fldigi_feldhell.h src/feldhell_internal.h $(DSP)/include/fldigi_dsp.h
OBJ = $(BUILD)/feldhell_common.o $(BUILD)/feldhell_fonts.o $(BUILD)/feldhell_rx.o \
      $(BUILD)/feldhell_decode.o $(BUILD)/feldhell_tx.o \
      $(patsubst %,$(BUILD)/dsp_%.o,$(DSP_SRC))

all: static shared
static: $(BUILD)/libfldigi_feldhell.a
shared: $(BUILD)/$(SHLIB)

$(BUILD):
	mkdir -p $(BUILD)

# The pin gate runs before anything is compiled and again whenever dsp/ or dsp.lock change.
$(BUILD)/dsp.ok: dsp.lock tools/check_dsp.sh $(wildcard $(DSP)/include/*.h $(DSP)/src/*.c) | $(BUILD)
	sh tools/check_dsp.sh dsp.lock $(DSP)
	touch $@

check-dsp:
	sh tools/check_dsp.sh dsp.lock $(DSP)

$(BUILD)/%.o: src/%.c $(HDRS) $(BUILD)/dsp.ok
	$(CC) $(CORE_FLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/dsp_%.o: $(DSP)/src/%.c $(HDRS) $(BUILD)/dsp.ok
	$(CC) $(CORE_FLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/libfldigi_feldhell.a: $(OBJ)
	ar rcs $@ $^

$(BUILD)/$(SHLIB): $(OBJ)
	$(CC) $(SHFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/test_feldhell: tests/test_feldhell.c tests/test_util.h $(BUILD)/libfldigi_feldhell.a
	$(CC) $(TEST_FLAGS) $(CFLAGS) $< $(BUILD)/libfldigi_feldhell.a -o $@ $(LDLIBS)

$(BUILD)/test_feldhell_fldigi: tests/test_feldhell_fldigi.c tests/test_util.h $(BUILD)/libfldigi_feldhell.a
	$(CC) $(TEST_FLAGS) $(CFLAGS) $< $(BUILD)/libfldigi_feldhell.a -o $@ $(LDLIBS)

test: $(BUILD)/test_feldhell $(BUILD)/test_feldhell_fldigi
	$(BUILD)/test_feldhell
	$(BUILD)/test_feldhell_fldigi tests/golden

SAN_FLAGS = -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all

sanitize:
	$(MAKE) --no-print-directory BUILD=$(BUILD)/san CFLAGS="$(SAN_FLAGS)" \
		LDLIBS="$(LDLIBS) $(SAN_FLAGS)" test

$(BUILD)/bench_feldhell: tests/bench_feldhell.c tests/test_util.h $(BUILD)/libfldigi_feldhell.a
	$(CC) $(TEST_FLAGS) $(CFLAGS) $< $(BUILD)/libfldigi_feldhell.a -o $@ $(LDLIBS)

bench: $(BUILD)/bench_feldhell
	$(BUILD)/bench_feldhell

clean:
	rm -rf $(BUILD)

.PHONY: all static shared check-dsp test sanitize bench clean
