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

function fs () {
    station --fscd /s2 --fscmd "$*"
}

function fs_native () {
    station --fscd . --fscmd "$*"
}


rm -f station.flash

fs ?
fs info
[[ ! $(fs fsck) ]]
fs dump

fs write  cups.uri      < data.txt
fs write  sig-0.key     < data.txt
fs write  cups-temp.cpy < /dev/null
fs write  cups-bak.uri  < data.txt
fs write  cups-bak.done < /dev/null
fs unlink cups-temp.cpy

fs write  tc-temp.uri     < data.txt
fs write  cups-temp.trust < data.txt
fs write  cups-temp.crt   < data.txt
fs write  cups-temp.key   < data.txt
fs write  tc-temp.trust   < data.txt
fs write  tc-temp.crt     < data.txt
fs write  tc-temp.key     < data.txt

fs write  cups-temp.upd   < /dev/null
fs rename cups-temp.trust cups.trust
fs rename cups-temp.crt   cups.crt
fs rename cups-temp.key   cups.key
fs unlink cups-temp.upd

fs write  tc-temp.upd   < /dev/null
fs rename tc-temp.trust tc.trust
fs rename tc-temp.crt   tc.crt
fs rename tc-temp.key   tc.key
fs rename tc-temp.uri   tc.uri
fs unlink tc-temp.upd

fs write  tc-temp.cpy   < /dev/null
fs write  tc-temp.trust < data.txt
fs write  tc-temp.crt   < data.txt
fs write  tc-temp.key   < data.txt
fs write  tc-temp.uri   < data.txt
fs write  tc-temp.done  < /dev/null
fs unlink tc-temp.cpy

fs dump
fs gc
fs dump

for f in cups.trust cups.crt cups.key tc.trust tc.crt tc.key; do
    fs read $f 2>/dev/null | cmp - data.txt
done

fs access tc-temp.key
fs access tc-temp.cpy | grep "File tc-temp.cpy does not exist" && echo "Failed as expected"
fs stat tc-temp.key
fs stat tc-temp.cpy | grep "Failed" && echo "Failed as expected"

fs foocmd >/dev/null 2>&1 | grep "Unknown command" && echo "Failed as expected"
fs unlink >/dev/null 2>&1 | grep "usage" && echo "Failed as expected"
fs rename >/dev/null 2>&1 | grep "usage" && echo "Failed as expected"
fs access >/dev/null 2>&1 | grep "usage" && echo "Failed as expected"
fs stat >/dev/null 2>&1 | grep "usage" && echo "Failed as expected"
fs read >/dev/null 2>&1 | grep "usage" && echo "Failed as expected"
fs write >/dev/null 2>&1 | grep "usage" && echo "Failed as expected"

fs write  somedata.txt   < data.txt
fs info | grep "active: section B" && echo "Auto-GC due to dirt at the end"
fs read somedata.txt 2>/dev/null | cmp - data.txt

VERYLONGNAME=verylooooooooooooooooooooooooooooooooooooooooooooooooooooongnameverylooooooooooooooooooooooooooooooooooooooooooooooooooooongnameverylooooooooooooooooooooooooooooooooooooooooooooooooooooongnameverylooooooooooooooooooooooooooooooooooooooooooooooooooooongname

fs unlink $VERYLONGNAME  2>&1 | grep "File name too long"
fs rename $VERYLONGNAME $VERYLONGNAME  2>&1 | grep "File name too long"
fs rename cups.uri $VERYLONGNAME  2>&1 | grep "File name too long"
fs write $VERYLONGNAME < data.txt  2>&1 | grep "File name too long"

fs read " " || echo "Expected failure"

fs_native read data.txt
fs_native write /tmp/data2.txt < data.txt
fs_native stat /tmp/data2.txt
fs_native access /tmp/data2.txt
fs_native rename /tmp/data2.txt /tmp/data3.txt
fs_native read /tmp/data3.txt
fs_native unlink /tmp/data3.txt
fs_native access /tmp/data3.txt | grep "File /tmp/data3.txt  does not exist" && echo "Failed as expected"

fs gc # move back to section A
fs info | grep "active: section A"

echo "==== TEST: Dirt at the end ======"
OFFSET=$(fs info 2>/dev/null | sed -n 's/fbase=\([^ ]\+\).*/\1/p')
USED=$(fs info 2>/dev/null | sed -n 's/used=\([^ ]\+\) bytes$/\1/p')
printf '\xee\xdd\xcc\xbb' | dd of=station.flash bs=1 seek=$(($OFFSET + $USED)) count=4 conv=notrunc

fs dump

fs gc # move to section A
fs info | grep "active: section A"

echo " ==== TEST: Corrupt magic  ==========="
fs gc # Move to section B
# Mess with section A magic
printf '\x10\x00\xb5\xa4' | dd of=station.flash bs=1 seek=$(($OFFSET)) count=4 conv=notrunc
fs info 2>&1 | grep "FSCK discovered strange magics"

echo " ==== TEST: Corrupt data  ==========="
printf 'SCHMUTZZ' | dd of=station.flash bs=1 seek=$(($OFFSET+64)) count=8 conv=notrunc
fs info

echo "TEST: Random flash init"
dd if=/dev/urandom of=station.flash bs=1M count=4 conv=notrunc
fs info 2>&1 | grep "initializing pristine flash"

fs erase

collect_gcda
