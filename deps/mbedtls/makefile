
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

${PLT}/library/%.a: ${PLT}
	${MAKE} -C $< lib CC="${CC}" AR="${AR}" LD="${LD}"

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
