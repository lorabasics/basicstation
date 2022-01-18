# Changelog

## 2.0.6 - 2022-01-17

* deps: Updated sx1302_hal dependency to version 2.1.0 (no LBT yet) (#89, #103, #121, #130)
* deps: Added sx1302_hal patch for handling of latched xticks rollover
* deps: Updated mbedTLS dependency to version 2.28.0 (LTS)
* deps: Fixed lgw patch causing IQ inversion in 500kHz channel (#81)
* s2e: Added support for AU915 (#43)
* s2e: Added support for LoRaWAN Regional Parameters Common Names (#18)
* s2e: Fixed dnchnl2 issue (#79)
* s2e: Fixed class C backoff logic (#87)
* s2e: Fixed class B beacon format (#129, #131)
* s2e: Fixed DR range check in upchannels list parser (#141)
* ral: Changed handling of xticks for lgw1302
* ral: Fixed radio in use issue (#53, #62)
* ral: Fixed types in txpow assignment (master/slave) (#118)
* ral: Fixed class B beacon parameters (#132)
* sx130xconf: Fixed parsing of rssi_tcomp values for sx1302 (#144)
* tls: Fixed TLS cert parsing issue (#76)
* sys_linux: Added support for usb/spi prefix in radio devname
* sys_linux: Added mbedTLS version to startup log
* sys_linux: Changed version to be printed to stdout (#51)
* sys_linux: Changed default max dbuf size (#95)
* sys_linux: Fixed relative home path handling (#140)
* sys_linux: Fixed memory corruption during system command execution (#146)
* tc/cups: Fixed sync on credset file IO (#94)
* timesync: Fixed UTC to PPS alignment
* log: Changed verbosity of XDEBUG log level
* log: Changed logging experience for improved clarity
* log: Added HAL log integration into logging module
* make: Changed makefiles for more space-friendliness (#66)
* net: Changed strictness on line-endings in key files (#68)
* gps: Fixed parsing of ublox NAV-TIMEGPS message
* Restored LICENSE file (#63, #67)

## 2.0.5 - 2020-06-05

* Remove LICENSE & ROADMAP.md file
* Based on v2.0.4 with no source code/functional changes

## 2.0.4 - 2020-03-17

* cups: Added Content-Type header to CUPS request
* cups: Fixed nullify sig pointer after free
* cups: Added segment length checks
* cups: Fixed freeing the key buffer
* deps: Added sx1302 hal and integrated with corecell platform
* sys_linux: Fixed decoder pointer dereferencing (#39)
* sys_linux: Fixed cups update abort should unlink the right file
* sys_linux: Fixed truncate update file instead of append
* s2e: Fixed memory corruption bug in JoinEui filter parsing (#31)
* s2e: Added DR and Freq fields to dntxed message (#37)
* s2e: Added error message type for printing LNS error into Station's log (#33)
* s2e: Added fts field to updf message
* net: Added Websocket PONG (#29)
* net: Added option for TLS server name indication/verification (#57)
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
