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

INFOS=$(shell find . -name 'test*.info')

all:
	./run-regression-tests --nohw

# teirich: my VM is a bit slow causing some TX to miss the deadline.
# Cut the tests some slack...
slow-vm:
	TX_AIM_GAP='"40ms"' PPSTHRES=300 ./run-regression-tests --nohw

ci:
	./run-regression-tests --nohw --verbose --ghactions

# Reset test state
reset:
	rm -rf t.log

# Pack all info files together into a single report
s2core.info: ${INFOS}
	@echo "::group::Generate coverage report"
	lcov -t "s2core" $(patsubst %, -a %, ${INFOS}) -o temp1.info
	genhtml -o s2core-html temp1.info
#	lcov -r temp.info '*/test.c' '*/log.c' -o temp.2.info
#	rm temp.info
	@echo "::endgroup::"
	lcov -l temp1.info
# We're not close to 100% currently
#	lcov -l temp.2.info | grep -P 'Total:.\s+100%\s+\d+.\s+100%\s+'
	mv temp1.info $@

clean:
	rm -rf s2core-html s2core.info
	for m in $$(find test* -name makefile); do \
	  printf "####################\n%s\n####################" $$m; \
	  ${MAKE} -C $$(dirname $$m) $@; \
	done

	variant=testsim make -C .. s-clean
	variant=testms make -C .. s-clean

super-clean: clean

.PHONY: all reset clean
