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

function cleanup () {
    default_cleanup
    echo "Extra cleanup.."
    rm -f tc.{crt,key,trust}
}

. ../testlib.sh

# radioinit args
if [[ "$TEST_VARIANT" = "testms" ]]; then
    riargs="./spidev 0"
else
    riargs="./spidev"
fi

# Plain TCP/ws
unset STATION_ARGS
unset STATION_RADIOINIT
python test.py
banner TCP/ws done
collect_gcda _tcp_ws

expect="1 ./spidev radioinit1.sh $riargs"
if [[ "$(cat radioinit.args)" != "$expect" ]]; then
    echo "radioinit1.sh failed"
    echo " ----------> $(cat radioinit.args)"
    echo "         vs> $expect"
    exit 1
fi

# Plain TLS/wss no client auth
export STATION_ARGS="-i radioinit2.sh"
ln -s ../pki-data/muxs-0.ca tc.trust
python test.py tls no_ca
banner TLS/wss no client auth done
collect_gcda _tls_no_ca
unset STATION_ARGS

expect="2 ./spidev radioinit2.sh $riargs"
if [[ "$(cat radioinit.args)" != "$expect" ]]; then
    echo "radioinit2.sh failed"
    echo " ----------> $(cat radioinit.args)"
    echo "         vs> $expect"
    exit 1
fi

# Plain TLS/wss with client auth
export STATION_RADIOINIT="radioinit3.sh"
ln -s ../pki-data/tc-router-1.key tc.key
ln -s ../pki-data/tc-router-1.crt tc.crt
python test.py tls
banner TLS/wss with client auth done
collect_gcda _tls_ca
unset STATION_RADIOINIT

expect="3 ./spidev radioinit3.sh $riargs"
if [[ "$(cat radioinit.args)" != "$expect" ]]; then
    echo "radioinit3.sh failed"
    echo " --------------> $(cat radioinit.args)"
    echo "             vs> $expect"
    exit 1
fi
