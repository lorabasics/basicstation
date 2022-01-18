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

# prep.sh - Prepare the environment for the CUPS example.


function ecdsaKey() {
    # Key not password protected for simplicity
    openssl ecparam -name prime256v1 -genkey | openssl ec -out $1
}

function rootCA() {
    mkdir -p ca
    ecdsaKey $2/"${1,,}"-ca.key
    openssl req -new -key $2/"${1,,}"-ca.key -out $2/"${1,,}"-ca.csr -subj "/OU=Example/O=BasicStation/C=CH/CN=$1 Root CA"
    openssl x509 -req -set_serial 1 -days 365 -in $2/"${1,,}"-ca.csr -signkey $2/"${1,,}"-ca.key -out $2/"${1,,}"-ca.crt
    rm $2/"${1,,}"-ca.csr
}

function cert() {
    # $1 -> name, $2 -> signing CA, $3 -> target dir, $4 ext
    mkdir -p $3
    ecdsaKey $3/$1.key
    openssl req -new -key $3/$1.key -out $3/$1.csr -subj "/OU=Example/O=BasicStation/C=CH/CN=$1"
    openssl x509 -req -set_serial 1 -days 365 -CA $2-ca.crt -CAkey $2-ca.key -in $3/$1.csr -out $3/$1.crt -extfile <(printf "$4")
    openssl verify -CAfile $2-ca.crt $3/$1.crt && rm $3/$1.csr
    cp $2-ca.crt $3/$1.trust
}

make clean

echo "== Build PKI =="

SAN_LOCALHOST="subjectAltName=DNS:localhost"

# CUPS "A" CA
rootCA CUPS_A ca                                    #  CUPS "A" CA
cert cups-0        ca/cups_a cups-0 $SAN_LOCALHOST  #   +- cups-0         server auth
cert cups-router-1 ca/cups_a cups-0                 #   +- cups-router-1  client auth
cp ca/cups_a-ca.crt cups-0/cups.ca

# CUPS CA "B"
rootCA CUPS_B ca                                    #  CUPS "B" CA
cert cups-1 ca/cups_b cups-1 $SAN_LOCALHOST         #   +- cups-1         server auth
cert cups-router-1 ca/cups_b cups-1                 #   +- cups-router-1  client auth
cp ca/cups_b-ca.crt cups-1/cups.ca

# TC CA
rootCA TC ca                                        #  TC CA
cert muxs-0      ca/tc tc-0 $SAN_LOCALHOST          #   +- muxs-0         server auth
cert infos-0     ca/tc tc-0 $SAN_LOCALHOST          #   +- infos-0        server auth
cert tc-router-1 ca/tc tc-0                         #   +- tc-router-1    client auth
cp ca/tc-ca.crt tc-0/tc.ca

rm -r ca

# Station initially connects to cups-0 on port 6040.
# Copy initial credentials to Station home dir.
cp cups-0/cups-0.trust      shome/cups.trust
cp cups-0/cups-router-1.crt shome/cups.crt
cp cups-0/cups-router-1.key shome/cups.key
echo "https://localhost:6040" > shome/cups.uri

echo "== Prepare FW Update 1.0.0 -> 2.0.0 =="

# Initial version 1.0.0
echo "1.0.0" > shome/version.txt

# Update script
echo "#!/bin/bash" > cups-1/2.0.0.bin
echo "echo '2.0.0' > shome/version.txt" >> cups-1/2.0.0.bin
echo "rm -f /tmp/update.bin" >> cups-1/2.0.0.bin
echo "killall station" >> cups-1/2.0.0.bin
rm -f /tmp/update.bin

# Signing keys
mkdir -p upd-sig
ecdsaKey upd-sig/sig-0.prime256v1.pem
openssl ec -in upd-sig/sig-0.prime256v1.pem -pubout -out upd-sig/sig-0.prime256v1.pub
openssl ec -in upd-sig/sig-0.prime256v1.pub -inform PEM -outform DER -pubin | tail -c 64 > cups-1/sig-0.key
openssl ec -in upd-sig/sig-0.prime256v1.pub -inform PEM -outform DER -pubin | tail -c 64 > shome/sig-0.key

openssl dgst -sha512 -sign upd-sig/sig-0.prime256v1.pem cups-1/2.0.0.bin > cups-1/2.0.0.bin.sig-0

echo '{"cupsUri": "https://localhost:6041","cupsId":"cups-1"}' > cups-0/cups-router-1.cfg
echo '{"cupsUri": "https://localhost:6041","tcUri":"wss://localhost:6038","version": "2.0.0"}' > cups-1/cups-router-1.cfg

touch prep.done
