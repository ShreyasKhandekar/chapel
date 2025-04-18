CHAPEL_GIT_REPO   ?= chapel-lang/chapel
CHAPEL_GIT_COMMIT ?= main# was 'master' prior to June 21, 2021
CHAPEL_MAKE_ARGS  ?= -j4
CHAPEL_BLDDIR     ?= $(HARNESS_WORKDIR)/chapel
CHAPEL_TMPDIR     ?= $(HARNESS_WORKDIR)/chapel-tmp
CHAPEL_HOME       ?= $(HARNESS_WORKDIR)/chapel-inst
CHAPEL_CONDUIT    ?= $(NETWORK) # Default to same conduit as the enclosing harness run
CHAPEL_PATH_PRE   ?=# empty - if set must end with :
CHAPEL_PATH_POST  ?=# empty - if set must start with :
CHAPEL_SHELL      ?= $(shell test -n "$${ZSH_VERSION+set}$${BASH_VERSION+set}" && echo bash || echo sh)

# legacy harness support
GASNET_SRCDIR ?= $(TOP_SRCDIR)/gasnet

# Since some time in late April 2021, unset is no longer acceptable in general
CHPL_LLVM ?= none

HARNESS_LOGDIR    ?= .
LOGFILE           ?= $(HARNESS_LOGDIR)/chapel.log

# Paths and options for standard tools
PERL              ?= perl
WGET              ?= wget -nv
GZCAT             ?= gzip -cd
TAR               ?= tar
UNZIP             ?= unzip
P7ZIP             ?= 7za
GIT               ?= git

# Must not inherit these from harness environment:
unexport LD LDFLAGS LIBS
unexport CC CFLAGS
unexport CXX CXXFLAGS
unexport AR RANLIB

# Parametrized support for download/unpack
CHAPEL_DOWNLOAD ?=# one of "unzip", "p7zip" or "tgz", default to auto-detect from downloaded archive suffix
chapel_dl_suffix_unzip := .zip
chapel_dl_suffix_p7zip := .zip
chapel_dl_suffix_tgz   := .tar.gz
chapel_dl_suffix=$(chapel_dl_suffix_$(strip $(CHAPEL_DOWNLOAD)))
ifeq ($(strip $(chapel_dl_suffix)),)
chapel_dl_suffix := .zip
endif

CHAPEL_URL      ?= https://github.com/$(CHAPEL_GIT_REPO)/archive/$(CHAPEL_GIT_COMMIT)$(chapel_dl_suffix)
CHAPEL_ARCHIVE  ?= $(notdir $(CHAPEL_URL))
chapel-download: force
	rm -Rf $(CHAPEL_BLDDIR) $(CHAPEL_TMPDIR) && mkdir $(CHAPEL_TMPDIR)
	if ( expr $(CHAPEL_URL) : file:/ || expr /$(CHAPEL_URL) : // ) >/dev/null; then \
	  set -x ; cd $(CHAPEL_TMPDIR) && ln -s $(CHAPEL_URL:file:%=%) . ; \
	else \
	  set -x ; cd $(CHAPEL_TMPDIR) && $(WGET) $(CHAPEL_URL) 2>&1 || ( cat wget-log 2> /dev/null ; exit 3 ) || exit $$? ; \
	fi
	cd $(CHAPEL_TMPDIR) && cat wget-log 2> /dev/null ; rm -f wget-log ; exit 0
	@CHAPEL_TESTDIR=`pwd` && \
	 cd $(CHAPEL_TMPDIR) && CHAPEL_ARCHIVE=`/bin/ls` && CHAPEL_DOWNLOAD="$(strip $(CHAPEL_DOWNLOAD))" && \
	 if test -z "$$CHAPEL_DOWNLOAD" ; then \
	   case "$$CHAPEL_ARCHIVE" in \
	     *.zip)          CHAPEL_DOWNLOAD=unzip ;; \
	     *.p7z|*.7z)     CHAPEL_DOWNLOAD=p7zip ;; \
	     *.tar.gz|*.tgz) CHAPEL_DOWNLOAD=tgz   ;; \
	     *) echo "ERROR: Unknown archive suffix on '$$CHAPEL_ARCHIVE': Set CHAPEL_DOWNLOAD=(unzip,p7zip,tgz)" ; exit 1; \
	   esac \
	 fi && \
	 echo Fetched $$CHAPEL_ARCHIVE : CHAPEL_DOWNLOAD=$$CHAPEL_DOWNLOAD && \
	 $(MAKE) -f $$CHAPEL_TESTDIR/Makefile chapel-unpack-$$CHAPEL_DOWNLOAD CHAPEL_ARCHIVE="$$CHAPEL_ARCHIVE" && \
	 rm -f $(CHAPEL_TMPDIR)/$$CHAPEL_ARCHIVE
	mv $(CHAPEL_TMPDIR)/* $(CHAPEL_BLDDIR) # archive's root dir will vary
	rmdir $(CHAPEL_TMPDIR)
# Three ways to unpack the archive:
#  Option 1: "unzip" - .zip w/ unzip
#   This is the favored approach because it gives us the hash and uses a widely available utility.
chapel-unpack-unzip: force; $(UNZIP) -z $(CHAPEL_ARCHIVE) && $(UNZIP) -q $(CHAPEL_ARCHIVE)
#  Option 2: "p7zip" - .zip w/ 7za (from p7zip package)
#   This also gives us the hash, but uses a less widely available utility.
#   However, it is sometimes necessary because many unzip installations contain a bug w.r.t. symlinks
chapel-unpack-p7zip: force; $(P7ZIP) x -bd $(CHAPEL_ARCHIVE)
#  Option 3: "tgz" - tar + gzip
#   This is the most portable, but cannot extract the hash unless git is available
chapel-unpack-tgz:   force
	-$(GZCAT) $(CHAPEL_ARCHIVE) | $(GIT) get-tar-commit-id; exit 0
	$(GZCAT) $(CHAPEL_ARCHIVE) | $(TAR) xf -
####
.PHONY: chapel-download chapel-unpack-unzip chapel-unpack-p7zip chapel-unpack-tgz

ifneq ($(strip $(CHPL_COMM_DEBUG)),)
CHPL_GASNET_MORE_CFG_OPTIONS += --enable-debug
CHAPEL_MAKE_RUNTIME_ARGS += DEBUG=1 WARNINGS=1 ASSERTS=1
endif
CHAPEL_MAKE_ARGS += CHPL_GASNET_MORE_CFG_OPTIONS="$(CHPL_GASNET_MORE_CFG_OPTIONS)"

# Define CHPL_LAUNCHER for some conduits without our desired defaults.
# Note that these have not necessarily all been tested.
chapel_launcher_opt = $(chapel_launcher_$(strip $(CHAPEL_CONDUIT)))

# Kludge some launchers that can be constructed as a simple delta from another:
chapel-launcher: force
	@true  # currently no special cases to deal with here
clone-launcher: force
	src=gasnetrun_$(SRC_CONDUIT); dst=gasnetrun_$(DST_CONDUIT); \
	cd $(CHAPEL_BLDDIR)/runtime/src/launch && \
	if test -d $$dst; then \
	  echo ERROR: target $$dst of $@ exists >&2; exit 1; \
	fi && \
	cp -Rp $$src $$dst && \
	mv $$dst/launch-$$src.c $$dst/launch-$$dst.c && \
	$(PERL) -pi -e 's/_$(SRC_CONDUIT)/_$(DST_CONDUIT)/g' -- $$dst/*
.PHONY: chapel-launcher clone-launcher

COMMON_ENV = PATH=$(CHAPEL_PATH_PRE)$$PATH$(CHAPEL_PATH_POST) \
             CHPL_LLVM=$(CHPL_LLVM)

CONFIG_ENV = $(COMMON_ENV) $(chapel_launcher_opt) \
             CHPL_COMM=gasnet CHPL_COMM_SUBSTRATE=$(CHAPEL_CONDUIT)
ifneq ($(strip $(CHPL_COMM_DEBUG)),)
COMMON_ENV += CHPL_COMM_DEBUG=$(CHPL_COMM_DEBUG)
endif

# Apply upsteam or local patches, if any
chapel_patches =
chapel-patch: force
	cd $(CHAPEL_BLDDIR) && \
	for p in $(chapel_patches); do $(WGET) $$p && patch -p1 < `basename $$p`; done
.PHONY: chapel-patch

# Build and (optionally) install Chapel compiler and runtime
_chapel: force
	rm -f chapel-built
	$(call safe_tee_dance, $(MAKE) chapel-download)
	$(call safe_tee_dance, $(MAKE) chapel-patch)
	$(MAKE) chapel-launcher
	cd $(CHAPEL_BLDDIR)/third-party/gasnet && mv gasnet-src gasnet-src.dist
	cp -Rp $(GASNET_SRCDIR) $(CHAPEL_BLDDIR)/third-party/gasnet/gasnet-src
	mkdir -p "$(CHAPEL_HOME)"
	cd $(CHAPEL_BLDDIR) && . util/setchplenv.$(CHAPEL_SHELL) && (                       \
	  env $(CONFIG_ENV) $(CHAPEL_BUILD_ENV) ./configure --chpl-home="$(CHAPEL_HOME)" && \
	  env $(COMMON_ENV) $(CHAPEL_BUILD_ENV) $(MAKE) $(CHAPEL_MAKE_ARGS) compiler &&     \
	  env $(COMMON_ENV) $(CHAPEL_BUILD_ENV) $(MAKE) $(CHAPEL_MAKE_ARGS) $(CHAPEL_MAKE_RUNTIME_ARGS) runtime && \
	  env $(COMMON_ENV) $(CHAPEL_BUILD_ENV) $(MAKE) $(CHAPEL_MAKE_ARGS) all             \
	) >> $(LOGFILE) 2>&1
	if test "$(CHAPEL_HOME)" != "$(CHAPEL_BLDDIR)"; then \
	  cd $(CHAPEL_BLDDIR) && . util/setchplenv.$(CHAPEL_SHELL) &&                   \
	  env $(COMMON_ENV) $(CHAPEL_BUILD_ENV) $(MAKE) $(CHAPEL_MAKE_ARGS) install &&  \
	  env $(COMMON_ENV) $(CHAPEL_BUILD_ENV) $(MAKE) $(CHAPEL_MAKE_ARGS) clean;      \
	fi >> $(LOGFILE) 2>&1
	@echo '#!/bin/sh' > $@ ; chmod +x $@
	@touch chapel-built

_chapel_clean: force
	cd $(CHAPEL_BLDDIR) && . util/setchplenv.$(CHAPEL_SHELL) && \
	  env $(COMMON_ENV) $(CHAPEL_BUILD_ENV) \
	    $(MAKE) $(CHAPEL_MAKE_ARGS) clean >> $(LOGFILE) 2>&1
	@echo '#!/bin/sh' > $@ ; chmod +x $@

# Not reliable other than on UDP, and wants to *run* the test
check: chapel-built
	cd $(CHAPEL_BLDDIR) && . util/setchplenv.$(CHAPEL_SHELL) && \
	  env $(COMMON_ENV) $(CHAPEL_CHECK_ENV) \
	    $(MAKE) check
	@echo '#!/bin/sh' > $@ ; chmod +x $@

#
# Build one test
#
test_common: chapel-built
	rm -f $(TEST_EXE) $(TEST_EXE)_real
	cd $(CHAPEL_HOME) && . util/setchplenv.$(CHAPEL_SHELL) && cd $(CURDIR) && \
	  env $(COMMON_ENV) $(CHAPEL_TEST_ENV) \
	    $(MAKE) -C $(CHAPEL_BLDDIR)/$(TEST_DIR) $(REAL_EXE) CHPL_FLAGS+="$(chpl_flags_$(TEST_EXE))" 2>&1
	mv $(CHAPEL_BLDDIR)/$(TEST_DIR)/$(REAL_EXE) $(TEST_EXE)
	if test -e $(CHAPEL_BLDDIR)/$(TEST_DIR)/$(REAL_EXE)_real; then \
	  mv $(CHAPEL_BLDDIR)/$(TEST_DIR)/$(REAL_EXE)_real $(TEST_EXE)_real; \
	fi
one_test: force  # Using the tests natural name
	$(MAKE) test_common REAL_EXE=$(TEST_EXE)
alt_test: force  # Using an alternate test name
	$(MAKE) test_common
.PHONY: test_common one_test alt_test


# top-level examples
EXAMPLES = hello hello2-module hello3-datapar hello4-datapar-dist hello5-taskpar hello6-taskpar-dist
$(EXAMPLES): force; $(MAKE) one_test TEST_EXE=$@ TEST_DIR=examples

# directory examples/benchmarks
BENCHMARKS = lcals lulesh miniMD
$(BENCHMARKS): force; $(MAKE) one_test TEST_EXE=$@ TEST_DIR=examples/benchmarks/$@

# directory examples/benchmarks/hpcc
HPCC = stream stream-ep ra ra-atomics
$(HPCC): force; $(MAKE) one_test TEST_EXE=$@ TEST_DIR=examples/benchmarks/hpcc
chpl_flags_ra = -O -s useOn=true
chpl_flags_ra-atomics = -O
unexport $(addprefix chpl_flags_,$(HPCC))

# directory examples/benchmarks/isx
ISX = isx-strong isx-weak isx-weakISO
$(ISX): force; $(MAKE) alt_test TEST_EXE=$@ REAL_EXE=isx TEST_DIR=examples/benchmarks/isx

# directory examples/primers
PRIMERS = \
        arrays arrayVectorOps associative atomics chpldoc.doc classes         \
        distributions domains fileIO genericClasses iterators locales         \
        modules opaque parIters procedures randomNumbers ranges reductions    \
        slices sparse syncsingle taskParallel timers varargs variables        \
        voidVariables
$(PRIMERS): force; $(MAKE) one_test TEST_EXE=$@ TEST_DIR=examples/primers/$@

# directory examples/programs
PROGRAMS = beer genericStack jacobi linkedList norm prodCons quicksort tree
$(PROGRAMS): force; $(MAKE) one_test TEST_EXE=$@ TEST_DIR=examples/programs/$@

force:

.PHONY: force

# Run a command, with output to both stdout and $(LOGFILE), preserving exit code of the command
# Based on https://unix.stackexchange.com/questions/14270/get-exit-status-of-process-thats-piped-to-another/70675#70675
safe_tee_dance = (((($1; echo $$? >&3) 2>&1 | tee -a $(LOGFILE) >&4) 3>&1) | (read rc; exit $$rc)) 4>&1
