#!/usr/bin/env bash 

cd examples/corecell

# Default to TTN server
TC_URI=${TC_URI:-"wss://lns.eu.thethings.network:443"} 
TC_TRUST=${TC_TRUST:-$(curl https://letsencrypt.org/certs/trustid-x3-root.pem.txt)}


# Setup TC files from environment
echo $TC_URI > ./lns-ttn/tc.uri
echo "$TC_TRUST" > ./lns-ttn/tc.trust

./start-station.sh -l ./lns-ttn

balena-idle
