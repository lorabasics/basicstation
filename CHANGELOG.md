# Changelog

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
