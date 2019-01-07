TD=../..
platform?=linux
variant?=std

all:
	platform=${platform} variant=${variant} make -C ${TD}
	${TD}/build-${platform}-${variant}/bin/station

rpi:
	platform=rpi variant=std make

kerlink:
	platform=kerlink variant=std make

clean:
	rm -rf cups.* cups-bak.* tc.* tc-bak.* station.log station.pid

.PHONY: all clean
