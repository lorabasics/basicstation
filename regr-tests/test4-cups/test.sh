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
    rm -f /tmp/update.bin*
}

. ../testlib.sh

PKI_DATA=../pki-data

export UPDTEST=$$
function check_updtest() {
    updtest="$(cat _shome/updtest.txt)"
    if [ "$UPDTEST" != "$updtest" ]; then
	echo "$1: updtest.txt='$updtest'  != '$UPDTEST'"
	exit 1
    fi
}

function setup_A () {
    # Note: How to create a signing key and create a signature:
    #    openssl ecparam -name prime256v1 -genkey | openssl ec -out sig-0.pem     # Create private key
    #    openssl ec -in sig-0.pem -pubout -out sig-0.pub                          # Create public key
    #    openssl ec -in sig-0.pub -inform PEM -outform DER -pubin | tail -c 64 > sig-0.key # Convert public key to compact binary format
    #    openssl dgst -sha512 -sign sig-0.pem v1.bin > v1.bin.sig-0               # Create signature
    rm -rf /tmp/update.bin* _shome _cups _tc
    killall -q station || true
    mkdir -p _shome _cups _tc
    cp v1.bin* sig-0.key                   _cups/
    cp station.conf slave-0.conf sig-0.key _shome/
}

function setup_B () {
    cp $PKI_DATA/cups-0.{crt,key}          _cups/
    cp $PKI_DATA/cups-router-1.ca          _cups/cups-0.trust
    cp $PKI_DATA/cups-0.ca                 _cups/cups.ca
    cp $PKI_DATA/{muxs,infos}-0.{crt,key}  _tc/
    cp $PKI_DATA/muxs-0.ca                 _tc/tc.ca
    cp $PKI_DATA/tc-router-1.ca            _tc/infos-0.trust
    cp $PKI_DATA/tc-router-1.ca            _tc/muxs-0.trust
}

function setup_C () {
    cp $PKI_DATA/cups-router-1.{crt,key}   _cups/
    cp $PKI_DATA/tc-router-1.{crt,key}     _tc/
}

function test_plain() {
    # Plain TCP/ws =====================
    banner TCP/ws - Starting...
    setup_A

    echo "v0" > _shome/version.txt
    rm -f _shome/updtest.txt /tmp/update.bin
    python test.py
    banner TCP/ws done
    collect_gcda _tcp_ws
    check_updtest TCP
}

function test_tls_noauth() {
    # TLS/wss without client auth =========
    banner TLS/wss NO client auth - Starting...
    setup_A
    setup_B

    echo "v0" > _shome/version.txt
    rm -f _shome/updtest.txt /tmp/update.bin
    python test.py tls no_ca
    banner TLS/wss NO client auth - DONE
    collect_gcda _tls_no_ca
    check_updtest TLS
}

function test_tls_auth() {
    # TLS/wss with client auth ============
    banner TLS/wss with client auth - Starting...
    setup_A
    setup_B
    setup_C

    echo "v0" > _shome/version.txt
    rm -f _shome/updtest.txt /tmp/update.bin
    python test.py tls
    banner TLS/wss with client auth - DONE
    collect_gcda _tls_ca
    check_updtest TLS
}

test_plain
test_tls_noauth
test_tls_auth
