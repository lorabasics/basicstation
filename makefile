TD=.
include ${TD}/setup.gmk

.PHONY: all
all:	build-local/bin/crc32 \
	src/kwcrc.h \
	deps \
	s-all

# Shortcuts to run station specific goals
.PHONY: s-all s-load s-clean
s-all s-load s-clean: ${BD}/s2core/makefile
	${MAKE} -C ${<D} ${@:s-%=%}

${BD}/%/makefile : makefile.%
	mkdir -p ${@D} ${BD}/bin ${BD}/lib
	cd ${@D} && (	echo "platform=${platform}"; \
			echo "variant=${variant}"; \
			echo "TD=../.."; \
			echo "-include ../../makefile.${platform}"; \
			echo "include ../../makefile.s2core") > makefile

src/kwcrc.h: build-local/bin/genkwcrcs src/kwlist.txt
	build-local/bin/genkwcrcs $$(cat src/kwlist.txt | sed -e '/^#/d;s/[ \t]\+#.*//') > build-local/temp-kwcrc.h
	mv build-local/temp-kwcrc.h $@

build-local/bin/genkwcrcs: src/genkwcrcs.c src/uj.h
	mkdir -p ${@D}
	gcc -std=gnu11 -Isrc -DCFG_prog_genkwcrcs $< -o $@

build-local/bin/crc32: src/crc32.c
	mkdir -p ${@D}
	gcc -std=gnu11 -Isrc -DCFG_prog_crc32 $< -o $@

DEPS.goals = $(patsubst %, deps/%, ${DEPS})

.PHONY: deps ${DEPS.goals}
deps: ${DEPS.goals}

${DEPS.goals}:
	platform=${platform} variant=${variant} ${MAKE} -C $@

.PHONY: build-clean
clean-build:
	for d in build-*/s2core; do \
	  if [ -d $$d ]; then ${MAKE} -C $$d clean; fi \
	done

.PHONY: clean super-clean
clean super-clean: clean-build
	for d in deps/*; do \
	  ${MAKE} -C $$d $@; \
	done
