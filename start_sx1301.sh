#!/usr/bin/env bash 

# Load common variables
source ./start_common.sh

# Change to project folder
cd examples/live-s2.sm.tc

# Setup TC files from environment
echo "$TC_URI" > tc.uri
echo "$TC_TRUST" > tc.trust
if [ ! -z ${TC_KEY} ]; then
	echo "Authorization: Bearer $TC_KEY" | perl -p -e 's/\r\n|\n|\r/\r\n/g'  > tc.key
fi

# Reset gateway
echo "Resetting gateway concentrator on GPIO $GW_RESET_GPIO"
echo $GW_RESET_GPIO > /sys/class/gpio/export
echo out > /sys/class/gpio/gpio$GW_RESET_GPIO/direction
echo 0 > /sys/class/gpio/gpio$GW_RESET_GPIO/value
sleep 1
echo 1 > /sys/class/gpio/gpio$GW_RESET_GPIO/value
sleep 1
echo 0 > /sys/class/gpio/gpio$GW_RESET_GPIO/value
sleep 1
echo $GW_RESET_GPIO > /sys/class/gpio/unexport

RADIODEV=$LORAGW_SPI ../../build-rpi-std/bin/station
