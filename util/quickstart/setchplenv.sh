# POSIX-standard compatibility shell script to set the Chapel environment variables
# Source this for POSIX-standard shells such as 'sh' and 'dash'
# Due to POSIX-standard limitations, this must be sourced from $CHPL_HOME


# Shallow test to see if we are in the correct directory
# Just probe to see if we have a few essential subdirectories --
# indicating that we are probably in a Chapel root directory.
if [ ! -d "util" ] || [ ! -d "compiler" ] || [ ! -d "runtime" ] || [ ! -d "modules" ]; then
    echo "Error: You must use '. util/setchplenv.sh' from within the chapel root directory."
    return 1
fi

echo "Setting CHPL_HOME..."
CHPL_HOME=`pwd`
export CHPL_HOME
echo "                    ...to $CHPL_HOME"
echo " "

CHPL_PYTHON=`"$CHPL_HOME"/util/config/find-python.sh`

MYPATH=`$CHPL_PYTHON "$CHPL_HOME"/util/config/fixpath.py "$PATH"`
exitcode=$?
MYMANPATH=`$CHPL_PYTHON "$CHPL_HOME"/util/config/fixpath.py "$MANPATH"`

# Double check $MYPATH before overwriting $PATH
if [ -z "${MYPATH}" -o "${exitcode}" -ne 0 ]; then
    echo "Error:  util/config/fixpath.py failed"
    echo "        Make sure you have Python 3.5+"
    return 1
fi

CHPL_BIN_SUBDIR=`$CHPL_PYTHON "$CHPL_HOME"/util/chplenv/chpl_bin_subdir.py`

echo "Updating PATH to include..."
PATH="$CHPL_HOME"/bin/$CHPL_BIN_SUBDIR:"$CHPL_HOME"/util:"$MYPATH"
export PATH
echo "                           ...$CHPL_HOME"/bin/$CHPL_BIN_SUBDIR
echo "                       and ...$CHPL_HOME"/util
echo " "

echo "Updating MANPATH to include..."
MANPATH="$CHPL_HOME"/man:"$MYMANPATH"
export MANPATH
echo "                           ...$CHPL_HOME"/man
echo " "

echo "Setting CHPL_COMM to..."
CHPL_COMM=none
export CHPL_COMM
echo "                           ...none"
echo ""

echo "Setting CHPL_TASKS to..."
CHPL_TASKS=fifo
export CHPL_TASKS
echo "                           ...fifo"
echo " "

echo "Setting CHPL_TARGET_MEM to..."
CHPL_TARGET_MEM=cstdlib
export CHPL_TARGET_MEM
unset CHPL_TARGET_JEMALLOC
unset CHPL_TARGET_MIMALLOC
echo "                           ...cstdlib"
echo " "

echo "Setting CHPL_HOST_MEM to..."
CHPL_HOST_MEM=cstdlib
export CHPL_HOST_MEM
unset CHPL_HOST_JEMALLOC
unset CHPL_HOST_MIMALLOC
echo "                           ...cstdlib"
echo " "

echo "Setting CHPL_GMP to..."
CHPL_GMP=none
export CHPL_GMP
echo "                           ...none"
echo " "

echo "Setting CHPL_RE2 to..."
CHPL_RE2=none
export CHPL_RE2
echo "                           ...none"
echo " "

USE_LLVM=`$CHPL_PYTHON "$CHPL_HOME"/util/chplenv/chpl_llvm.py --quickstart`
echo "Setting CHPL_LLVM to..."
CHPL_LLVM=$USE_LLVM
export CHPL_LLVM
echo "                           ...$USE_LLVM"
echo " "
