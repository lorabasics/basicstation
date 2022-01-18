#!/bin/bash

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

. ../testlib.sh

function workerPid () {
    # Process started by daemon to do the work
    #  - All station processes
    #  - the one whose parent pid is not 1
    #  - column 2 is pid
    local pid=$(cat station.pid)
    ps -eo ppid,pid,pgid,command | \
	grep 'station[ ]' | \
	grep -P "^ *$pid " | \
	awk '{print $2}'
}
function validatePid () {
    # Make sure all running processes named station and /proc/self/exe (slave procs)
    # match all processes in process group listed in station.pid
    local pid=$(cat station.pid)
    cmp <(ps -eo pid,ppid,pgid,command | grep -P "station[ ]|/proc/self/ex[e]") \
	<(ps -eo pid,ppid,pgid,command | grep "$pid[ ]")
}


# Startup fails - no uri files
rm -f *.uri
if station --temp . -d; then
    echo "Should have failed"
    exit 1
else
    xcode=$?
    [ $xcode -eq 1 ] || (echo "Should have failed with exit code 1: $xcode"; exit 1)
fi

collect_gcda _notcuri

echo "ws://localhost:6038" > tc.uri

# No other station running - start new daemon
rm -f station.log
banner 'Starting first daemon'
station --temp . -d
sleep 0.5
pid=$(cat station.pid)
echo "Daemon station.pid=$pid"
validatePid

collect_gcda _1

# No other station running - start new daemon
banner 'Trying to start 2nd daemon (no force)'
if station --temp . -d; then
    echo "ERROR: Should not succeed starting another daemon"
    exit 1
else
    xcode=$?
    if [[ $xcode -ne 6 ]]; then
	echo "ERROR: Wrong exit code: $xcode"
	exit 1
    fi
fi

wpid=$(workerPid)
grep "$wpid started" station.log || (echo "Missing wpid: $wpid started"; exit 1)

collect_gcda _2

# Force start - kill old daemon
banner 'Trying to start 3rd daemon with force - kill old one'
pid=$(cat station.pid)
echo " - old pid=$pid"
station --temp . -d -f
sleep 0.5
validatePid

if (ps -eo pid,ppid,pgid,command | grep "$pid[ ]"); then
    echo "ERROR: old daemon pid=$pid still around!"
    exit 1
fi

wpid1=$(workerPid)
banner "Kill worker process $wpid1"
kill -9 $wpid1
sleep 2  # wait for restart
wpid2=$(workerPid)

grep "$wpid1 died"    station.log || (echo "Missing wpid1: $wpid1 died";    exit 1)
grep "$wpid2 started" station.log || (echo "Missing wpid2: $wpid2 started"; exit 1)


# Kill process group
banner 'Kill daemon process group'
([ -f station.pid ] && kill -9 -- -$(cat station.pid)) || true
if (ps -eo pid,ppid,pgid,command | grep "station[ ]"); then
    echo "Kill process group failed"
    exit 1
fi

collect_gcda _f
