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

LGWLIB=${TD}/${BD}/lib/liblgw.a
LGWINC=${TD}/${BD}/include/lgw

LGWVERSION.default = 0.2.2
LGWVERSION ?= $(or ${LGWVERSION.${platform}}, ${LGWVERSION.default})

HFILES = $(wildcard ${PLT}/libloragw/inc/*.h)
SRCS = $(wildcard ${PLT}/libloragw/src/*.c)
ARTEFACTS = ${LGWLIB} ${LGWINC} $(patsubst %, ${LGWINC}/%, $(notdir ${HFILES}) config.h)

LGW_EXTRA_CFLAGS = -DSTATIONLOG  # Enable station log integration

all: $(if ${HFILES},${ARTEFACTS},${PLT})

${LGWINC}:
	@mkdir -p $@

${LGWLIB}: ${PLT}/libloragw/libloragw.a
	@mkdir -p ${@D}
	@echo "  CP    ${<F} -> $@"
	@cp $< $@

${PLT}/libloragw/libloragw.a: ${SRCS} ${HFILES}
	${MAKE} -C ${PLT}/libloragw libloragw.a CC="${CC}" AR="${AR}" LD="${LD}" TDfull="${TDfull}" EXTRA_CFLAGS="${LGW_EXTRA_CFLAGS}"

${PLT}/libloragw/inc/config.h: ${PLT}/libloragw/library.cfg
	${MAKE} -C ${PLT}/libloragw inc/config.h CC="${CC}" AR="${AR}" LD="${LD}" TDfull="${TDfull}"

${LGWINC}/%.h: ${PLT}/libloragw/inc/%.h | ${LGWINC}
	@echo "  CP    ${<F} -> $@"
	@cp $< $@

${PLT}: prep.sh
	platform=${platform} variant=${variant} lgwversion=${LGWVERSION} ./prep.sh
	$(MAKE) --no-print-directory all platform=${platform} variant=${variant}

clean:
	rm -rf ${ARTEFACTS}
	if [ -d ${PLT} ]; then ${MAKE} -C ${PLT}/libloragw clean; fi

super-clean: clean
	rm -rf git-repo platform-*

.PHONY: all clean super-clean platform-${platform}
