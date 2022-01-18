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

function curlit () {
    # $1 -> allow empty responses
    local path=$2
    local ref=ref.$(echo $path | tr / .)
    local msg=" CURL    $path vs $ref"
    local resp=`curl --noproxy 127.0.0.1 -sD - http://127.0.0.1:8080/$path`
    if [ "$resp" != "" ]; then
        echo "$resp" | diff - $ref \
            || (echo "[FAILED] $msg" && (cat station.log) && false)
    else
        echo "[WRN] Empty response to $path" && $1
    fi
}

export -f curlit # make function available to GNU parallel

station -k
station --temp . -f -L station.log -l DEBUG &
sleep 1

echo "---- Testing Proper HTTP Requests"

TFILES="test.js test.json a sub/b.txt 404"

for f in $TFILES; do
    curlit false $f
done

curl --noproxy 127.0.0.1 -sD - http://127.0.0.1:8080/config -o /dev/null
curl --noproxy 127.0.0.1 -sD - -X POST http://127.0.0.1:8080/config -o /dev/null
curl --noproxy 127.0.0.1 -sD - http://127.0.0.1:8080/version -o /dev/null
curl --noproxy 127.0.0.1 -sD - -X POST http://127.0.0.1:8080/version -o /dev/null
curl --noproxy 127.0.0.1 -sD - http://127.0.0.1:8080/api -o /dev/null


echo "---- Testing Big Resource HTTP Requests"
head -c 60k /dev/urandom | gzip > web/toobig.gz
curlit false toobig.gz
rm web/toobig.gz

echo "---- Testing Broken HTTP Requests"

function ncit () {
    # $1 -> allow empty responses
    # $2 -> reqlist entry
    local REQ="${2%%~*}"
    local REF="${2##*~}"
    local resp="`printf "$REQ" | nc -N 127.0.0.1 8080`"
    if [ "$REF" == "DONTCARE" ]; then
        return 0
    fi
    if [ "$resp" == "" ] && [ "$REF" != "ref.empty" ]; then
        echo "[INFO] empty response to $REQ -> expected $REF" && $1
    else
        diff <(echo -n "$resp") <( echo -n "`cat $REF`" ) \
            || (echo "[FAILED] $REQ -> $REF" && false)
    fi
}

export -f ncit # make function available to GNU parallel

# - Invalid HTTP headers

REQLIST=(
"GET / HTTP/1.1\r\n \r\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~DONTCARE"
"GET\0 HTTP/1.1\n\r\n\r\n\r\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~DONTCARE"
"SCHMUTZ SCHMUTZ SCHMUTZ\rSCHMUTZ\nSCHMUTZ\r\n~~~~~~~~~~~~~~~~~~~~DONTCARE"
"SCHMUTZ SCHMUTZ SCHMUTZ\r\n\r\nSCHMUTZ\nSCHMUTZ~~~~~~~~~~~~~~~~~~DONTCARE"
"\r\n\r\nSCHMUTZ SCHMUTZ SCHMUTZ\r\n\r\nSCHMUTZ\nSCHMUTZ~~~~~~~~~~DONTCARE"
"\r\n\r\nSCHMUTZ\0SCHMUTZ SCHMUTZ\r\0\n\r\nSCHMUTZ\nSCHMUTZ~~~~~~~DONTCARE"
"\\//\/\../\\/..//\/\/./\.\~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~DONTCARE"
)

NREQ=${#REQLIST[@]}
ROUNDS=500

for ((n=0;n<$ROUNDS;n++)); do
    RND=$((RANDOM%NREQ))
    # echo "${REQLIST[$RND]}"
    ncit false "${REQLIST[$RND]}"
done

# Flush any remaining read buffer in station
for i in {1..3}; do
    ncit false "GET /a HTTP/1.1\r\nHost: localhost:8080\r\nAccept: */*\r\n\r\n~~~DONTCARE"
done

# - Valid HTTP headers

REQLIST=(
"GET /a HTTP/1.1\r\nHost: localhost:8080\r\nAccept: */*\r\n\r\n~~~ref.a"
"GET / HTTP/1.1\r\nAuthorization: xxxxxxxxxxxxxxxxxx\r\n\r\n~~~~~~ref.index.html"
"GET / HTTP/1.1\r\n\r\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ref.index.html"
"GET /index.html HTTP/1.1\r\n\r\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ref.index.html"
"GET /sub/b.txt HTTP/1.1\r\n\r\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ref.sub.b.txt"
"POST SCHMUTZ HTPP/1.1\r\n\r\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ref.400"
"POST_/SCHMUTZ HTTP/1.1\r\n\r\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ref.400"
"POST SCHMUTZ HTTP/1.1\r\n\r\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ref.400"
"GET /not_existing_file HTTP/1.1\r\n\r\n~~~~~~~~~~~~~~~~~~~~~~~~~~ref.404"
"POST /version HTTP/1.1\r\n\r\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ref.405"
"POP /version HTTP/1.1\r\n\r\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ref.405"
"VERYLONGMETHOD1_@!#_!@# /version HTTP/1.1\r\n\r\n~~~~~~~~~~~~~~~~ref.405"
"GET / HTTP//1.1\r\n\r\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ref.400"
"GET / HTTP/1.1 \r\n\r\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ref.400"
"GET /\n HTTP/1.1\r\n\r\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ref.400"
"GET /\n\r HTTP/1.1\r\n\r\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ref.400"
)

NREQ=${#REQLIST[@]}
ROUNDS=500

for ((n=0;n<$ROUNDS;n++)); do
    RND=$((RANDOM%NREQ))
    # echo "${REQLIST[$RND]}"
    ncit false "${REQLIST[$RND]}"
done

# echo "---- Testing Parallel HTTP Requests"

# parallel --halt now,fail=1 curlit true ::: `printf "$TFILES %.0s" {1..100}`
# for k in `shuf -i 0-$((NREQ-1)) -n $ROUNDS -r`; do echo ${REQLIST[$k]}; done \
#     | parallel --halt now,fail=1 ncit true

kill -SIGTERM $(cat station.pid)

sleep 0.5

collect_gcda _web
