# bash/zsh shell script to set the Chapel environment variables

# Find out filepath depending on shell
if [ -n "${BASH_VERSION}" ]; then
    filepath=${BASH_SOURCE[0]}
elif [ -n "${ZSH_VERSION}" ]; then
    filepath=${(%):-%N}
else
    echo "Error: setchplenv.bash can only be sourced from bash and zsh"
    return 1
fi

# Directory of setchplenv script, will not work if script is a symlink
DIR=$(cd "$(dirname "${filepath}")" && pwd)

# Shallow test to see if we are in the correct directory
# Just probe to see if we have a few essential subdirectories --
# indicating that we are probably in a Chapel root directory.
chpl_home=$( cd $DIR/../../ && pwd )
if [ ! -d "$chpl_home/util" ] || [ ! -d "$chpl_home/compiler" ] || [ ! -d "$chpl_home/runtime" ] || [ ! -d "$chpl_home/modules" ]; then
    # Chapel home is assumed to be one directory up from setenvchpl.bash script
    echo "Error: \$CHPL_HOME is not where it is expected"
    return 1
fi

CHPL_PYTHON=`$chpl_home/util/config/find-python.sh`

# Remove any previously existing CHPL_HOME paths
MYPATH=`$CHPL_PYTHON $chpl_home/util/config/fixpath.py "$PATH"`
exitcode=$?
MYMANPATH=`$CHPL_PYTHON $chpl_home/util/config/fixpath.py "$MANPATH"`

# Double check $MYPATH before overwriting $PATH
if [ -z "${MYPATH}" -o "${exitcode}" -ne 0 ]; then
    echo "Error:  util/config/fixpath.py failed"
    echo "        Make sure you have Python 3.5+"
    return 1
fi

export CHPL_HOME=$chpl_home
echo "Setting CHPL_HOME to $CHPL_HOME"

CHPL_BIN_SUBDIR=`$CHPL_PYTHON "$CHPL_HOME"/util/chplenv/chpl_bin_subdir.py`

export PATH="$CHPL_HOME"/bin/$CHPL_BIN_SUBDIR:"$CHPL_HOME"/util:"$MYPATH"
echo "Updating PATH to include $CHPL_HOME/bin/$CHPL_BIN_SUBDIR"
echo "                     and $CHPL_HOME/util"

export MANPATH="$CHPL_HOME"/man:"$MYMANPATH"
echo "Updating MANPATH to include $CHPL_HOME/man"

echo "Setting CHPL_COMM to none"
export CHPL_COMM=none

echo "Setting CHPL_TASKS to fifo"
export CHPL_TASKS=fifo

echo "Setting CHPL_TARGET_MEM to cstdlib"
export CHPL_TARGET_MEM=cstdlib
unset CHPL_TARGET_JEMALLOC
unset CHPL_TARGET_MIMALLOC

echo "Setting CHPL_HOST_MEM to cstdlib"
export CHPL_HOST_MEM=cstdlib
unset CHPL_HOST_JEMALLOC
unset CHPL_HOST_MIMALLOC

echo "Setting CHPL_GMP to none"
export CHPL_GMP=none

echo "Setting CHPL_RE2 to none"
export CHPL_RE2=none

USE_LLVM=`$CHPL_PYTHON $chpl_home/util/chplenv/chpl_llvm.py --quickstart`
echo "Setting CHPL_LLVM to $USE_LLVM"
export CHPL_LLVM=$USE_LLVM
