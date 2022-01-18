# --- Revised 3-Clause BSD License ---
# Copyright Semtech Corporation 2022. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without modification,
# are permitted provided that the following conditions are met:
#
#     * Redistributions of source code must retain the above copyright notice,
#       this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright notice,
#       this list of conditions and the following disclaimer in the documentation
#       and/or other materials provided with the distribution.
#     * Neither the name of the Semtech corporation nor the names of its
#       contributors may be used to endorse or promote products derived from this
#       software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL SEMTECH CORPORATION. BE LIABLE FOR ANY DIRECT,
# INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
# BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
# LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
# OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
# ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

set -e

R=$(echo -e "\033[31m") # red
G=$(echo -e "\033[32m") # green
B=$(echo -e "\033[34m") # blue
HB=$(echo -e "\033[30;43m")  # b/f reversed - headline
X=$(echo -e "\033[0m")

TD=${TDfull:-../..}
export TEST_VARIANT=${TEST_VARIANT:-testsim}
export BUILD_DIR=${TD}/build-${platform:-linux}-${TEST_VARIANT}

export PATH=$BUILD_DIR/bin:$PATH
# export PYTHONPATH=${TD}/regr-tests/pysys:..:$PYTHONPATH
export PYTHONPATH=${TD}/pysys:$PYTHONPATH

TEST_NAME=$(basename $(dirname $(realpath $0)))

# Cleanup any coverage file
GCDA_DIR=$BUILD_DIR/s2core
rm -f $GCDA_DIR/*.gcda

function collect_gcda () {
    echo "Collecting GCDA from $GCDA_DIR into $TEST_VARIANT$1.info"
    lcov -c -d $GCDA_DIR -o $TEST_VARIANT$1.info
    rm -f $GCDA_DIR/*.gcda
}

function disable_test () {
    # run-regression-tests will check ..N/A.. marker if we fail
    banner "--- Test N/A --- temporarily ${1:-disabled}"
    exit 1
}

function banner () {
    s="======================================================================"
    echo -e "\n${s}\n${HB}[${TEST_NAME}]${X} -- ${HB}${@}${X}\n${s}\n"
}

function default_cleanup () {
    echo "Default cleanup..."
    exec 1>/dev/null 2>&1    # hide noisy output
    # Kill any station process
    killall -q station || true
    sleep 1
    killall -q -s9 station || true
    # Kill any other processes related to test script
    pids="$timerpid $(jobs -p)"
    kill    $pids || true
    sleep 1
    kill -9 $pids || true
    # Remove files unless requested to keep them
    if [[ $keep_files = 0 ]]; then
        rm -rf $(cat .gitignore | grep -v '\.info$') || true
    fi
    exec 1>&3 2>&4              # restore stdout.stderr for caller
}
# typically overwritten in script
if [[ "$(type -t cleanup)" = "" ]]; then
    # If not overriden by sourceing test script
    function cleanup () {
        default_cleanup
    }
fi

if [[ "$IGNORE" != "" ]] && [[ "$TEST_NAME" =~ $IGNORE ]]; then
    disable_test ignored
    exit 1
fi

trap cleanup EXIT
exec 3>&1 4>&2      # save stdout/stderr
cleanup

#  Start after cleasnup - otherwise timerpid is killed immediately
testpid=$$
timeout=${timeout:-120}
echo "Timeout is ${timeout}s on PID $testpid"

if [[ "$timeout" != "" ]]; then
    (cnt=0
     while ((cnt++ < $timeout)); do
         sleep 1;
         if [[ ! -d /proc/$timeout ]]; then
             exit 0
         fi
     done
     echo -e "\n\n${R}${TEST_NAME} - Timeout - KILLING $testpid!${X}" >&3
     kill $testpid
    ) &
    timerpid=$!
fi

# ------------------------------------------------------------
# if 1st argument is
#    --keep
# then don't delete .gitignore files
#
keep_files=0
if [[ "$1" == "--keep" ]]; then
    shift
    keep_files=1
fi

