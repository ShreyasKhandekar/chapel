ifndef CHPL_MAKE_HOME
export CHPL_MAKE_HOME=$(shell pwd)/../..
endif

CHPL_MAKE_HOST_TARGET = --target
include $(CHPL_MAKE_HOME)/make/Makefile.base

#
# Cray builds are cross-compilations.
#
ifneq (, $(filter cray-x%,$(CHPL_MAKE_TARGET_PLATFORM)))
CHPL_HWLOC_CFG_OPTIONS += --host=$(CHPL_MAKE_TARGET_ARCH)-cle-linux-gnu
else ifneq (, $(filter cray-cs hpe-cray-ex,$(CHPL_MAKE_TARGET_PLATFORM)))
CHPL_HWLOC_CFG_OPTIONS += --host=$(CHPL_MAKE_TARGET_ARCH)-unknown-linux-gnu
endif

CHPL_HWLOC_CFG_OPTIONS += --enable-static \
                          --disable-shared \
                          --disable-readme \
                          --disable-cairo \
                          --disable-libxml2 \
                          --disable-libudev \
                          --disable-cuda \
                          --disable-nvml \
                          --disable-opencl \
                          --disable-gl \
                          --disable-rsmi

ifeq ($(CHPL_MAKE_HWLOC_PCI), disable)
	CHPL_HWLOC_CFG_OPTIONS += --disable-pci
endif

ifdef CHPL_HWLOC_PREFIX
   CHPL_HWLOC_CFG_OPTIONS += --with-hwloc-symbol-prefix=$(CHPL_HWLOC_PREFIX)
endif

ifeq ($(CHPL_LIB_PIC),pic)
CFLAGS += $(SHARED_LIB_CFLAGS)
endif

CHPL_HWLOC_CFG_OPTIONS += $(CHPL_HWLOC_MORE_CFG_OPTIONS)

default: all

all: hwloc

clean: FORCE
	rm -rf $(HWLOC_BUILD_SUBDIR)

cleanall: FORCE
	rm -rf build

clobber: FORCE
	rm -rf build install


hwloc-config: FORCE
#
# These first few lines touch a bunch of autoconf-oriented files in a
# certain order to prevent autoconf from running again; otherwise, we
# ran into issues if a user's autoconf environment was not as far
# ahead in version numbers as that which was used when packaging the
# Qthreads release
#
	cd $(HWLOC_SUBDIR) && touch -c configure.ac
	cd $(HWLOC_SUBDIR) && find . -name "*.m4" | xargs touch 
	sleep 1
	cd $(HWLOC_SUBDIR) && touch -c aclocal.m4
	sleep 1
	cd $(HWLOC_SUBDIR) && touch configure
	cd $(HWLOC_SUBDIR) && find . -name "*.in" | xargs touch
#
# Then configure
#
	mkdir -p $(HWLOC_BUILD_DIR)
	cd $(HWLOC_BUILD_DIR) && $(HWLOC_SUBDIR)/configure CC='$(CC)' \
		CFLAGS='$(CFLAGS)' CXX='$(CXX)' CXXFLAGS='$(CFLAGS)' \
		LDFLAGS='$(RUNTIME_LFLAGS) $(LDFLAGS)' --prefix=$(HWLOC_INSTALL_DIR) \
		$(CHPL_HWLOC_CFG_OPTIONS)

hwloc-build: FORCE
	cd $(HWLOC_BUILD_DIR) && $(MAKE)
	cd $(HWLOC_BUILD_DIR) && $(MAKE) install

hwloc: hwloc-config hwloc-build

hwloc-reconfig:
	cd $(HWLOC_SUBDIR) && autoreconf -f -i

FORCE:

.NOTPARALLEL:
