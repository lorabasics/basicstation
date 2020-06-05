#!/bin/sh

# --- Revised 3-Clause BSD License ---
# Copyright Semtech Corporation 2020. All rights reserved.
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


RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m' # No Color

# A POSIX variable
OPTIND=1         # Reset in case getopts has been used previously in the shell.

# Initialize our own variables:
lns_config=""
variant=std

show_help()
{
   printf "$GREEN"
   printf "\tUsage: ./start-station.sh -l {lns-home} -d\n"
   printf "\t-l : LNS configuration folder \n"
   printf "\t-d : To run debug variant of station\n"
   printf "$NC"
   printf "\t\t e.g: ./start-station.sh -l ./lns-ttn\n"
   printf "\t\t      ./start-station.sh -dl ./lns-ttn\n"
   exit
}


while getopts "h?dl:" opt; do
    case "$opt" in
    h|\?)
        show_help
        exit 0
        ;;
    d)  variant=debug
        ;;
    l)  lns_config=$OPTARG
        ;;
    esac
done

shift $((OPTIND-1))

[ "${1:-}" = "--" ] && shift


if [ -z "$lns_config" ]; then
	printf "$RED"
	printf "$RED \tError: No LNS home folder provided$NC\n"
	printf "$NC"
	show_help
fi

STATION_BIN="../../build-corecell-$variant/bin/station"


if [ -f "$STATION_BIN" ]; then
	printf "Using variant=$variant, lns_config='$lns_config'\n"
	printf "$GREEN Starting Station ... $NC\n"
	$STATION_BIN -h $lns_config
else
	printf "$RED [ERROR]: Binary not found @ $STATION_BIN $NC\n"
fi
