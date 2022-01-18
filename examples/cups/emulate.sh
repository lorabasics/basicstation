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

# emulate.sh
#
# Emualte the behavior of Station by subsequent curl requests to CUPS.
# Tries to guess CUPS services (sim.py) are listening on localhost ports 6040/6041.

R="\e[31m"
G="\e[32m"
B="\e[34m"
C="\e[0m"

ROOTDIR=$(dirname $0)

function crc32() {
    # CRC32 of stdin
    # $1 output format (optional) [Default: 'u'->decimal]
    gzip -1 | tail -c 8 | od -t ${1:-u}4 -N 4 -An --endian=little | xargs echo
}

function dumpcredset() {
    # Concatenate credentials in DER format. Order: trust-cert-key
    # Ouptut (u4_t)0x00000000 in case of empty file
    # $1 Server entity name (e.g. 'cups', 'muxs', etc.)
    # $2 Server entity ID
    # 32 Client ID (e.g. 'router-1')
    for c in trust crt key; do
        openssl asn1parse -in $ROOTDIR/$1-$2/$1-$3.$c -noout -out - 2>/dev/null || printf "\0\0\0\0"
    done
}

function banner() {
    echo -e "$1==== #$2  [CUPS-$3] $4 ===================\e[0m"
}

function build_req() {
    # $1 package version, e.g. 1.0.0
    # $2 CUPS URI
    # $3 TC URI
    # $4 CUPS cred crc
    # $5 TC cred crc
    # $6 Signing key crc (comma separated)
    cat << EOM
{
    "router":      "::1",
    "model":       "linux",
    "package":     "$1",
    "station":     "Emulated Station",
    "cupsUri":     "$2",
    "tcUri":       "$3",
    "cupsCredCrc": $4,
    "tcCredCrc":   $5,
    "keys":        [ $6 ]
}
EOM
}

rcnt=0
function do_curl() {
    # $1 CUPS instance ID (also last digit of port)
    # $2 Client ID (e.g. 'router-1')
    rcnt=$((rcnt+1))
    {
        >&2 banner $B $rcnt $1 REQUEST;
        tee /dev/stderr;
        >&2 banner $G $rcnt $1 RESPONSE;
    } | \
    if ! curl -s -S -f --noproxy localhost \
        -X POST https://localhost:604$1/update-info \
        --cacert $ROOTDIR/cups-$1/cups-$2.trust \
        --cert   $ROOTDIR/cups-$1/cups-$2.crt \
        --key    $ROOTDIR/cups-$1/cups-$2.key \
        -d @- -D /dev/stderr -o -;
    then
        >&2 echo -e "${R}ERROR: curl failed.${C}"
    fi | xxd
}

# Check if CUPS is running
if [ $(netstat -ant | grep LISTEN | grep -E "(6040|6041)" | wc -l) == "2" ]; then
    echo "CUPS is running."
else
    echo "Starting CUPS."
    python -u sim.py &
    sleep 1 # Wait for python to start up
fi

# Print CRC32 of signing key
echo -e "CRC32 " \
    "0x$(cat shome/sig-0.key | crc32 x | tr a-z A-Z)" \
    " ($(cat shome/sig-0.key | crc32 u))" \
    "\tshome/sig-0.key"

# Print CRC32s of credential sets
for c in cups-0 cups-1 tc-0; do
    echo -e "CRC32 " \
     "0x$(dumpcredset ${c%%-*} ${c##*-} router-1 | crc32 x | tr a-z A-Z)" \
     " ($(dumpcredset ${c%%-*} ${c##*-} router-1 | crc32 u))" \
     "\t$c/${c%%-*}-router-1.{trust,crt,key}"
done

# CUPS request sequences
do_curl 0 router-1 <<<"$(build_req \
    1.0.0 \
    https://localhost:6040 \
    '' \
    $(dumpcredset cups 0 router-1 | crc32) \
    $(dumpcredset x x x | crc32) \
    $(cat $ROOTDIR/shome/sig-0.key | crc32))"


do_curl 1 router-1 <<<"$(build_req \
    1.0.0 \
    https://localhost:6041 \
    '' \
    $(dumpcredset cups 1 router-1 | crc32) \
    $(dumpcredset x x x | crc32) \
    $(cat $ROOTDIR/shome/sig-0.key | crc32))"


do_curl 1 router-1 <<<"$(build_req \
    2.0.0 \
    https://localhost:6041 \
    wss://localhost:6038 \
    $(dumpcredset cups 1 router-1 | crc32) \
    $(dumpcredset tc 0 router-1 | crc32) \
    $(cat $ROOTDIR/shome/sig-0.key | crc32))"


kill -9 $(jobs -p) >/dev/null 2>&1 || true
