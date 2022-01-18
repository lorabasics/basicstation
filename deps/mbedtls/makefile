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

TD=../..
include ${TD}/setup.gmk
PLT=platform-${platform}

HFILES = $(notdir $(wildcard ${PLT}/include/mbedtls/*.h))
MBEDINC = ${TD}/${BD}/include/mbedtls
ARTEFACTS = $(patsubst -l%, ${TD}/${BD}/lib/lib%.a, ${MBEDLIBS}) \
	${MBEDINC} \
	$(patsubst %, ${TD}/${BD}/include/mbedtls/%, ${HFILES})

all: $(if ${HFILES},${ARTEFACTS},${PLT})

${MBEDINC}:
	@mkdir -p $@

${PLT}/library/%.a: ${PLT}/Makefile
	${MAKE} -C ${PLT} lib CC="${CC}" AR="${AR}" LD="${LD}" CFLAGS="-Wall -std=gnu11"

${TD}/${BD}/lib/%.a: ${PLT}/library/%.a
	@mkdir -p ${@D}
	@echo "  CP    ${<F} -> $@"
	@cp $< $@

${MBEDINC}/%.h: ${PLT}/include/mbedtls/%.h
	@echo "  CP    ${<F} -> $@"
	@cp $< $@

${PLT}: prep.sh
	platform=${platform} variant=${variant} ./prep.sh
	$(MAKE) --no-print-directory all platform=${platform} variant=${variant}

clean:
	rm -rf ${ARTEFACTS}
	if [ -d ${PLT} ]; then ${MAKE} -C ${PLT} clean; fi

super-clean: clean
	rm -rf git-repo platform-*

.PHONY: prepare all clean super-clean
