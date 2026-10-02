# SPDX-License-Identifier: MIT
CC ?= cc
AR ?= ar
PKG_CONFIG ?= pkg-config
PREFIX ?= /usr
LIBDIR ?= $(PREFIX)/lib
INCLUDEDIR ?= $(PREFIX)/include
DATADIR ?= $(PREFIX)/share
PKGCONFIGDIR ?= $(LIBDIR)/pkgconfig
BUILD_DIR ?= build
VERSION := 0.1.0-alpha.1
ABI_VERSION := 1.0.0
SOVERSION := 1

CPPFLAGS ?=
CFLAGS ?=
LDFLAGS ?=
LDLIBS ?=
TARGET_MACHINE := $(shell $(CC) -dumpmachine)
# Select the compiler target, not the build host. Runtime qualification is separate.
ARCH_CFLAGS := $(if $(findstring x86_64,$(TARGET_MACHINE)),-march=x86-64 -mtune=generic,$(if $(findstring aarch64,$(TARGET_MACHINE)),-march=armv8-a -mtune=generic))
ISA_NOTE := $(shell $(CC) -c -x c -Wa,-mx86-used-note=yes -o /dev/null /dev/null 2>/dev/null && echo -Wa,-mx86-used-note=yes)
BASE_CPPFLAGS = -D_FORTIFY_SOURCE=3
BASE_CFLAGS = -O2 -g -std=c17 -Wall -Wextra -Wpedantic -Werror -fstack-protector-strong $(ARCH_CFLAGS) $(ISA_NOTE)
BASE_LDFLAGS = -Wl,-z,relro,-z,now
AVAHI_CFLAGS = $(shell $(PKG_CONFIG) --cflags avahi-client)
AVAHI_LIBS = $(shell $(PKG_CONFIG) --libs avahi-client)
OBJECTS = $(BUILD_DIR)/mdns.o $(BUILD_DIR)/version.o
SHARED = $(BUILD_DIR)/libsynapse-mdns.so.$(ABI_VERSION)
STATIC = $(BUILD_DIR)/libsynapse-mdns.a
PC = $(BUILD_DIR)/synapse-mdns.pc
PUBLIC_TEST = $(BUILD_DIR)/public-api
STATIC_TEST = $(BUILD_DIR)/public-api-static
FIXTURE = $(BUILD_DIR)/mdns-fixture
PILOT = $(BUILD_DIR)/mdns-pilot
WRONG = $(BUILD_DIR)/wrong-major/libsynapse-mdns.so.$(SOVERSION)
CPU_PROBE = $(BUILD_DIR)/isa-cpu
ISA_EMULATOR ?=
ISA_SYSROOT ?=
ISA_LIBRARY_PATH ?=
ISA_EXECUTION_ARGS = $(if $(strip $(ISA_EMULATOR)),--emulator "$(ISA_EMULATOR)" --sysroot "$(ISA_SYSROOT)" --cpu-probe "$(CPU_PROBE)" --library-path "$(ISA_LIBRARY_PATH)")

.PHONY: all clean test sdk-test abi-test isa-test build-flags-test target-test dgx-check rig-build wrong-major install isa-check
all: $(SHARED) $(STATIC) $(PC)

$(BUILD_DIR):
	install -d -m 0755 "$@"

$(BUILD_DIR)/%.o: src/%.c include/synapse/mdns.h Makefile | $(BUILD_DIR)
	$(CC) $(BASE_CPPFLAGS) $(CPPFLAGS) $(BASE_CFLAGS) $(CFLAGS) -fPIC -fvisibility=hidden -Iinclude $(AVAHI_CFLAGS) -c -o "$@" "$<"

$(SHARED): $(OBJECTS) src/exports.map Makefile
	$(CC) -shared -Wl,-soname,libsynapse-mdns.so.$(SOVERSION) -Wl,--version-script=src/exports.map -Wl,--no-undefined -o "$@" $(OBJECTS) $(BASE_LDFLAGS) $(LDFLAGS) $(AVAHI_LIBS) $(LDLIBS)
	ln -sfn "libsynapse-mdns.so.$(ABI_VERSION)" "$(BUILD_DIR)/libsynapse-mdns.so.$(SOVERSION)"
	ln -sfn "libsynapse-mdns.so.$(SOVERSION)" "$(BUILD_DIR)/libsynapse-mdns.so"

$(STATIC): $(OBJECTS)
	$(AR) rcsD "$@" $(OBJECTS)

$(PC): pkgconfig/synapse-mdns.pc.in tools/pc.py Makefile | $(BUILD_DIR)
	python3 -I -B tools/pc.py "$<" "$@" "$(PREFIX)" "$(LIBDIR)" "$(INCLUDEDIR)" "$(VERSION)"

$(PUBLIC_TEST): tests/public-api.c $(SHARED) include/synapse/mdns.h
	$(CC) $(BASE_CPPFLAGS) $(CPPFLAGS) $(BASE_CFLAGS) $(CFLAGS) -fPIE -Iinclude -o "$@" "$<" -L"$(BUILD_DIR)" -lsynapse-mdns -Wl,-rpath,'$$ORIGIN' -pie $(BASE_LDFLAGS) $(LDFLAGS)

$(STATIC_TEST): tests/public-api.c $(STATIC) include/synapse/mdns.h
	$(CC) $(BASE_CPPFLAGS) $(CPPFLAGS) $(BASE_CFLAGS) $(CFLAGS) -fPIE -Iinclude -o "$@" "$<" $(STATIC) $(AVAHI_LIBS) -pie $(BASE_LDFLAGS) $(LDFLAGS)

# Clock substitution and fake Avahi belong only to this separate test object.
# Never link avahi-client here; no normal test can announce on the network.
$(BUILD_DIR)/mdns-test.o: src/mdns.c include/synapse/mdns.h Makefile | $(BUILD_DIR)
	$(CC) $(BASE_CPPFLAGS) $(CPPFLAGS) $(BASE_CFLAGS) $(CFLAGS) -Dclock_gettime=synapse_mdns_test_clock -Iinclude $(AVAHI_CFLAGS) -c -o "$@" "$<"

$(FIXTURE): tests/mdns-fixture.c $(BUILD_DIR)/mdns-test.o include/synapse/mdns.h
	$(CC) $(BASE_CPPFLAGS) $(CPPFLAGS) $(BASE_CFLAGS) $(CFLAGS) -fPIE -Iinclude $(AVAHI_CFLAGS) -o "$@" "$<" $(BUILD_DIR)/mdns-test.o -lavahi-common -pie $(BASE_LDFLAGS) $(LDFLAGS)

abi-test: all
	python3 -I -B tests/abi.py "$(SHARED)"

sdk-test: all
	CC="$(CC)" TEST_CFLAGS="$(BASE_CFLAGS) $(CFLAGS)" TEST_LDFLAGS="$(BASE_LDFLAGS) $(LDFLAGS)" python3 -I -B tests/sdk.py "$(abspath $(BUILD_DIR))"

isa-test:
	python3 -I -B tests/isa-fixture.py

build-flags-test:
	python3 -I -B tests/build-flags.py

target-test:
	python3 -I -B tests/target-fixture.py

test: $(PUBLIC_TEST) $(STATIC_TEST) $(FIXTURE) abi-test sdk-test isa-test build-flags-test target-test
	"$(PUBLIC_TEST)"
	"$(STATIC_TEST)"
	"$(FIXTURE)"

# Opt-in real-network consumer: build only, never automatically run or install.
$(PILOT): tests/mdns-pilot.c $(SHARED) include/synapse/mdns.h
	$(CC) $(BASE_CPPFLAGS) $(CPPFLAGS) $(BASE_CFLAGS) $(CFLAGS) -fPIE -Iinclude -o "$@" "$<" -L"$(BUILD_DIR)" -lsynapse-mdns -Wl,-rpath,'$$ORIGIN' -pie $(BASE_LDFLAGS) $(LDFLAGS)

rig-build: $(PILOT)

# Deliberately faulty producer for consumer fail-closed tests; never installed.
$(BUILD_DIR)/wrong-major.o: tests/wrong-major.c include/synapse/mdns.h | $(BUILD_DIR)
	$(CC) $(BASE_CPPFLAGS) $(CPPFLAGS) $(BASE_CFLAGS) $(CFLAGS) -fPIC -fvisibility=hidden -Iinclude -c -o "$@" "$<"

$(WRONG): $(BUILD_DIR)/mdns.o $(BUILD_DIR)/wrong-major.o src/exports.map
	install -d -m 0755 "$(dir $@)"
	$(CC) -shared -Wl,-soname,libsynapse-mdns.so.$(SOVERSION) -Wl,--version-script=src/exports.map -Wl,--no-undefined -o "$@" $(BUILD_DIR)/mdns.o $(BUILD_DIR)/wrong-major.o $(BASE_LDFLAGS) $(LDFLAGS) $(AVAHI_LIBS)

wrong-major: $(WRONG)

install: all
	@test -n "$(DESTDIR)" || { echo 'install requires an isolated nonempty DESTDIR; live deployment is not authorized' >&2; exit 1; }
	install -d -m 0755 "$(DESTDIR)$(LIBDIR)" "$(DESTDIR)$(INCLUDEDIR)/synapse" "$(DESTDIR)$(PKGCONFIGDIR)"
	install -m 0755 "$(SHARED)" "$(DESTDIR)$(LIBDIR)/libsynapse-mdns.so.$(ABI_VERSION)"
	ln -sfn "libsynapse-mdns.so.$(ABI_VERSION)" "$(DESTDIR)$(LIBDIR)/libsynapse-mdns.so.$(SOVERSION)"
	ln -sfn "libsynapse-mdns.so.$(SOVERSION)" "$(DESTDIR)$(LIBDIR)/libsynapse-mdns.so"
	install -m 0644 "$(STATIC)" "$(DESTDIR)$(LIBDIR)/libsynapse-mdns.a"
	install -m 0644 include/synapse/mdns.h "$(DESTDIR)$(INCLUDEDIR)/synapse/mdns.h"
	install -m 0644 "$(PC)" "$(DESTDIR)$(PKGCONFIGDIR)/synapse-mdns.pc"
	install -D -m 0644 LICENSE "$(DESTDIR)$(DATADIR)/licenses/libsynapse-mdns/LICENSE"
	install -D -m 0644 README.md "$(DESTDIR)$(DATADIR)/doc/libsynapse-mdns/README.md"
	install -D -m 0644 PROVENANCE.md "$(DESTDIR)$(DATADIR)/doc/libsynapse-mdns/PROVENANCE.md"
	install -D -m 0644 docs/api.md "$(DESTDIR)$(DATADIR)/doc/libsynapse-mdns/api.md"

$(CPU_PROBE): tests/isa-cpu.c | $(BUILD_DIR)
	$(CC) $(BASE_CPPFLAGS) $(CPPFLAGS) $(BASE_CFLAGS) $(CFLAGS) -fPIE -o "$@" "$<" -pie $(BASE_LDFLAGS) $(LDFLAGS)

# ELF DSO and PIE notes differ. Undeclared DSO needs explicit isolated baseline
# execution; ISA-used/source flags alone never produce runtime qualification.
isa-check: $(SHARED) $(PUBLIC_TEST) $(STATIC_TEST) $(FIXTURE) $(CPU_PROBE)
	python3 -I -B tests/isa.py $(ISA_EXECUTION_ARGS) "$(SHARED)" "$(PUBLIC_TEST)" "$(STATIC_TEST)" "$(FIXTURE)"

# Explicit native ARM64/GB10 artifact gate. It does not acquire Avahi, a GPU or
# an engine lease and does not relabel native execution as Armv8-A baseline proof.
dgx-check: $(SHARED) $(PUBLIC_TEST) $(STATIC_TEST) $(FIXTURE)
	python3 -I -B tests/target.py --profile dgx-spark "$(SHARED)" "$(PUBLIC_TEST)" "$(STATIC_TEST)" "$(FIXTURE)"

clean:
	rm -rf "$(BUILD_DIR)"
