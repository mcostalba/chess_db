### ==========================================================================
### Section 1. General Configuration
### ==========================================================================

### Establish the operating system name
UNAME = $(shell uname)

### Executable name
EXE = parser

### Installation dir definitions
PREFIX = /usr/local
BINDIR = $(PREFIX)/bin

### Built-in benchmark for pgo-builds
PGOBENCH = ./$(EXE) bench

### Object files
OBJS = bitboard.o main.o misc.o parser.o position.o

### ==========================================================================
### Section 2. High-level Configuration
### ==========================================================================
#
# flag                --- Comp switch --- Description
# ----------------------------------------------------------------------------
#
# debug = yes/no      --- -DNDEBUG         --- Enable/Disable debug mode
# optimize = yes/no   --- (-O3/-fast etc.) --- Enable/Disable optimizations
# arch = (name)       --- (-arch)          --- Target architecture
# bits = 64/32        --- -DIS_64BIT       --- 64-/32-bit operating system
# prefetch = yes/no   --- -DUSE_PREFETCH   --- Use prefetch asm-instruction
# popcnt = yes/no     --- -DUSE_POPCNT     --- Use popcnt asm-instruction
# sse = yes/no        --- -msse            --- Use Intel Streaming SIMD Extensions
# pext = yes/no       --- -DUSE_PEXT       --- Use pext x86_64 asm-instruction
#
# Note that Makefile is space sensitive, so when adding new architectures
# or modifying existing flags, you have to make sure there are no extra spaces
# at the end of the line for flag values.

### 2.1. General and architecture defaults
optimize = yes
debug = no
bits = 32
prefetch = no
popcnt = no
sse = no
pext = no

### 2.2 Architecture specific

ifeq ($(ARCH),general-32)
	arch = any
endif

ifeq ($(ARCH),x86-32-old)
	arch = i386
endif

ifeq ($(ARCH),x86-32)
	arch = i386
	prefetch = yes
	sse = yes
endif

ifeq ($(ARCH),general-64)
	arch = any
	bits = 64
endif

ifeq ($(ARCH),x86-64)
	arch = x86_64
	bits = 64
	prefetch = yes
	sse = yes
endif

ifeq ($(ARCH),x86-64-modern)
	arch = x86_64
	bits = 64
	prefetch = yes
	popcnt = yes
	sse = yes
endif

ifeq ($(ARCH),x86-64-bmi2)
	arch = x86_64
	bits = 64
	prefetch = yes
	popcnt = yes
	sse = yes
	pext = yes
endif

ifeq ($(ARCH),armv7)
	arch = armv7
	prefetch = yes
endif

ifeq ($(ARCH),ppc-32)
	arch = ppc
endif

ifeq ($(ARCH),ppc-64)
	arch = ppc64
	bits = 64
endif


### ==========================================================================
### Section 3. Low-level configuration
### ==========================================================================

### 3.1 Selecting compiler (default = gcc)

CXXFLAGS += -Wall -Wcast-qual -fno-exceptions -fno-rtti -std=c++11 $(EXTRACXXFLAGS)
DEPENDFLAGS += -std=c++11
LDFLAGS += $(EXTRALDFLAGS)

ifeq ($(COMP),)
	COMP=gcc
endif

ifeq ($(COMP),gcc)
	comp=gcc
	CXX=g++
	CXXFLAGS += -pedantic -Wextra -Wshadow -m$(bits)
	ifneq ($(UNAME),Darwin)
	   LDFLAGS += -Wl,--no-as-needed
	endif
endif

ifeq ($(COMP),mingw)
	comp=mingw

	ifeq ($(UNAME),Linux)
		ifeq ($(bits),64)
			ifeq ($(shell which x86_64-w64-mingw32-c++-posix),)
				CXX=x86_64-w64-mingw32-c++
			else
				CXX=x86_64-w64-mingw32-c++-posix
			endif
		else
			ifeq ($(shell which i686-w64-mingw32-c++-posix),)
				CXX=i686-w64-mingw32-c++
			else
				CXX=i686-w64-mingw32-c++-posix
			endif
		endif
	else
		CXX=g++
	endif

	CXXFLAGS += -Wextra -Wshadow
	LDFLAGS += -static
endif

ifeq ($(COMP),icc)
	comp=icc
	CXX=icpc
	CXXFLAGS += -diag-disable 1476,10120 -Wcheck -Wabi -Wdeprecated -strict-ansi
endif

ifeq ($(COMP),clang)
	comp=clang
	CXX=clang++
	CXXFLAGS += -pedantic -Wextra -Wshadow -m$(bits)
	LDFLAGS += -m$(bits)
	ifeq ($(UNAME),Darwin)
		CXXFLAGS += -stdlib=libc++
		DEPENDFLAGS += -stdlib=libc++
	endif
endif

ifeq ($(UNAME),Darwin)
	CXXFLAGS += -arch $(arch) -mmacosx-version-min=10.9
	LDFLAGS += -arch $(arch) -mmacosx-version-min=10.9
endif

### Travis CI script uses COMPILER to overwrite CXX
ifdef COMPILER
	COMPCXX=$(COMPILER)
endif

### Allow overwriting CXX from command line
ifdef COMPCXX
	CXX=$(COMPCXX)
endif

### On mingw use Windows threads, otherwise POSIX
ifneq ($(comp),mingw)
	# On Android Bionic's C library comes with its own pthread implementation bundled in
	ifneq ($(arch),armv7)
		# Haiku has pthreads in its libroot, so only link it in on other platforms
		ifneq ($(UNAME),Haiku)
			LDFLAGS += -lpthread
		endif
	endif
endif

### 3.2 Debugging
ifeq ($(debug),no)
	CXXFLAGS += -DNDEBUG
else
	CXXFLAGS += -g
endif

### 3.3 Optimization
ifeq ($(optimize),yes)

	CXXFLAGS += -O3

	ifeq ($(comp),gcc)

		ifeq ($(UNAME),Darwin)
			ifeq ($(arch),i386)
				CXXFLAGS += -mdynamic-no-pic
			endif
			ifeq ($(arch),x86_64)
				CXXFLAGS += -mdynamic-no-pic
			endif
		endif

		ifeq ($(arch),armv7)
			CXXFLAGS += -fno-gcse -mthumb -march=armv7-a -mfloat-abi=softfp
		endif
	endif

	ifeq ($(comp),icc)
		ifeq ($(UNAME),Darwin)
			CXXFLAGS += -mdynamic-no-pic
		endif
	endif

	ifeq ($(comp),clang)
		ifeq ($(UNAME),Darwin)
			ifeq ($(pext),no)
				CXXFLAGS += -flto
				LDFLAGS += $(CXXFLAGS)
			endif
			ifeq ($(arch),i386)
				CXXFLAGS += -mdynamic-no-pic
			endif
			ifeq ($(arch),x86_64)
				CXXFLAGS += -mdynamic-no-pic
			endif
		endif
	endif
endif

### 3.4 Bits
ifeq ($(bits),64)
	CXXFLAGS += -DIS_64BIT
endif

### 3.5 prefetch
ifeq ($(prefetch),yes)
	ifeq ($(sse),yes)
		CXXFLAGS += -msse
		DEPENDFLAGS += -msse
	endif
else
	CXXFLAGS += -DNO_PREFETCH
endif

### 3.6 popcnt
ifeq ($(popcnt),yes)
	ifeq ($(comp),icc)
		CXXFLAGS += -msse3 -DUSE_POPCNT
	else
		CXXFLAGS += -msse3 -mpopcnt -DUSE_POPCNT
	endif
endif

### 3.7 pext
ifeq ($(pext),yes)
	CXXFLAGS += -DUSE_PEXT
	ifeq ($(comp),$(filter $(comp),gcc clang mingw))
		CXXFLAGS += -mbmi2
	endif
endif

### 3.8 Link Time Optimization, it works since gcc 4.5 but not on mingw under Windows.
### This is a mix of compile and link time options because the lto link phase
### needs access to the optimization flags.
ifeq ($(comp),gcc)
	ifeq ($(optimize),yes)
	ifeq ($(debug),no)
		CXXFLAGS += -flto
		LDFLAGS += $(CXXFLAGS)
	endif
	endif
endif

ifeq ($(comp),mingw)
	ifeq ($(UNAME),Linux)
	ifeq ($(optimize),yes)
	ifeq ($(debug),no)
		CXXFLAGS += -flto
		LDFLAGS += $(CXXFLAGS)
	endif
	endif
	endif
endif

### 3.9 Android 5 can only run position independent executables. Note that this
### breaks Android 4.0 and earlier.
ifeq ($(arch),armv7)
	CXXFLAGS += -fPIE
	LDFLAGS += -fPIE -pie
endif


### ==========================================================================
### Section 4. Public targets
### ==========================================================================

help:
	@echo ""
	@echo "To compile pgn parser, type: "
	@echo ""
	@echo "make target ARCH=arch [COMP=compiler] [COMPCXX=cxx]"
	@echo ""
	@echo "Supported targets:"
	@echo ""
	@echo "build                   > Standard build"
	@echo "strip                   > Strip executable"
	@echo "install                 > Install executable"
	@echo "clean                   > Clean up"
	@echo ""
	@echo "Supported archs:"
	@echo ""
	@echo "x86-64                  > x86 64-bit"
	@echo "x86-64-modern           > x86 64-bit with popcnt support"
	@echo "x86-64-bmi2             > x86 64-bit with pext support"
	@echo "x86-32                  > x86 32-bit with SSE support"
	@echo "x86-32-old              > x86 32-bit fall back for old hardware"
	@echo "ppc-64                  > PPC 64-bit"
	@echo "ppc-32                  > PPC 32-bit"
	@echo "armv7                   > ARMv7 32-bit"
	@echo "general-64              > unspecified 64-bit"
	@echo "general-32              > unspecified 32-bit"
	@echo ""
	@echo "Supported compilers:"
	@echo ""
	@echo "gcc                     > Gnu compiler (default)"
	@echo "mingw                   > Gnu compiler with MinGW under Windows"
	@echo "clang                   > LLVM Clang compiler"
	@echo "icc                     > Intel compiler"
	@echo ""
	@echo "Simple examples. If you don't know what to do, you likely want to run: "
	@echo ""
	@echo "make build ARCH=x86-64    (This is for 64-bit systems)"
	@echo "make build ARCH=x86-32    (This is for 32-bit systems)"
	@echo ""
	@echo "Advanced examples, for experienced users: "
	@echo ""
	@echo "make build ARCH=x86-64-modern COMP=clang"
	@echo ""


.PHONY: build
build:
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) config-sanity
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) all

strip:
	strip $(EXE)

install:
	-mkdir -p -m 755 $(BINDIR)
	-cp $(EXE) $(BINDIR)
	-strip $(BINDIR)/$(EXE)

clean:
	$(RM) $(EXE) $(EXE).exe *.o .depend *~ core bench.txt *.gcda

default:
	help

### ==========================================================================
### Section 5. Private targets
### ==========================================================================

all: $(EXE) .depend

config-sanity:
	@echo ""
	@echo "Config:"
	@echo "debug: '$(debug)'"
	@echo "optimize: '$(optimize)'"
	@echo "arch: '$(arch)'"
	@echo "bits: '$(bits)'"
	@echo "prefetch: '$(prefetch)'"
	@echo "popcnt: '$(popcnt)'"
	@echo "sse: '$(sse)'"
	@echo "pext: '$(pext)'"
	@echo ""
	@echo "Flags:"
	@echo "CXX: $(CXX)"
	@echo "CXXFLAGS: $(CXXFLAGS)"
	@echo "LDFLAGS: $(LDFLAGS)"
	@echo ""
	@echo "Testing config sanity. If this fails, try 'make help' ..."
	@echo ""
	@test "$(debug)" = "yes" || test "$(debug)" = "no"
	@test "$(optimize)" = "yes" || test "$(optimize)" = "no"
	@test "$(arch)" = "any" || test "$(arch)" = "x86_64" || test "$(arch)" = "i386" || \
	 test "$(arch)" = "ppc64" || test "$(arch)" = "ppc" || test "$(arch)" = "armv7"
	@test "$(bits)" = "32" || test "$(bits)" = "64"
	@test "$(prefetch)" = "yes" || test "$(prefetch)" = "no"
	@test "$(popcnt)" = "yes" || test "$(popcnt)" = "no"
	@test "$(sse)" = "yes" || test "$(sse)" = "no"
	@test "$(pext)" = "yes" || test "$(pext)" = "no"
	@test "$(comp)" = "gcc" || test "$(comp)" = "icc" || test "$(comp)" = "mingw" || test "$(comp)" = "clang"

$(EXE): $(OBJS)
	$(CXX) -o $@ $(OBJS) $(LDFLAGS)

.depend:
	-@$(CXX) $(DEPENDFLAGS) -MM $(OBJS:.o=.cpp) > $@ 2> /dev/null

-include .depend

