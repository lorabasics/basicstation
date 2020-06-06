# Changelog

## 2.0.5 - 2020-06-05
* README.md - Updated with supported platform and Travis Banner
* Remove LICENSE & ROADMAP.md file
* Based on v2.0.4 with no source code/functional changes

## 2.0.4 - 2020-03-17
* cups - Add Content-Type header to CUPS request
* sys_linux - truncate update file instead of append
* cups - nullify sig pointer after free
* cups - symbol for signature crc length
* cups - Add segment length checks
* sys_linux - cups update abort should unlink the right file
* cups - free the key buffer
* lgw1302: Added sx1302 hal and integrated with corecell platform
* sys_linux: Fixed decoder pointer dereferencing (#39)
* s2e: Fixed memory corruption bug in JoinEui filter parsing (#31)
* s2e: Added DR and Freq fields to dntxed message (#37)
* s2e: Added error message type for printing LNS error into Station's log (#33)
* s2e: Added fts field to updf message
* net: Added Websocket PONG (#29)
* net: Added option for TLS server name indication/verification
* rt: Added MCU clock drift compensation for UTC time offset
* ral: Fixed dntxed message for short transmissions
* ral: Added Automatic channel allocation feature
* ral: Added fine timestamping in lgw2
* ral: Added automatic AES key derivation in lgw2
* ral: Added support for smtcpico platform (experimental) (#16)
* timesync: Correct UTC offset in case PPS offset is known
* lgwsim: Added lgw2 support
* pysys: Fixed Id6 category parsing (#28)
* tests: Added regression tests
* tests: Added Dockerfile

## 2.0.3 - 2019-03-14

* sys_linux: Fixed stdout/stderr redirection for logging
* sys_linux: Added detection of implicit no-cups mode by uri files
* net: Fixed authtoken cleanup in http close
* cups: Fixed skipping credentials during rotation
* ral: Changed pipe read strategy in ral_master to allow partial reads
* tc: Fixed last-resort CUPS triggering from TC backoff
* lgwsim: Changed socket read strategy in lgwsim
* examples: Added CUPS example

## 2.0.2 - 2019-01-30

* cups: Fixed CUPS HTTP POST request. Now contains `Hosts` header.
* tc: Changed backoff strategy of LNS connection.
* ral: Fixed FSK parameters for TX
* ral: Added starvation prevention measure in lgw1 rxpolling loop.
* net: Fixed large file delivery in httpd/web.
* net: Fixed gzip detection heuristic in httpd/web

## 2.0.1 - 2019-01-07

* Initial public release.
