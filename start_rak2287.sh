#!/usr/bin/env bash 

cd examples/corecell

# Default to TTN server
TC_URI=${TC_URI:-"wss://lns.eu.thethings.network:443"} 
TC_TRUST=${TC_TRUST:-$(curl --silent "https://letsencrypt.org/certs/{trustid-x3-root.pem.txt,isrgrootx1.pem}"))}


# Setup TC files from environment
echo "$TC_URI" > ./lns-ttn/tc.uri
echo "$TC_TRUST" > ./lns-ttn/tc.trust

if [ ! -z ${TC_KEY} ]; then
	echo "Authorization: Bearer $TC_KEY" | perl -p -e 's/\r\n|\n|\r/\r\n/g'  > ./lns-ttn/tc.key
fi


./start-station.sh -l ./lns-ttn

balena-idle
