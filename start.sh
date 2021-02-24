#!/usr/bin/env bash

TAG_KEY="EUI"
TTN_EUI=$(cat /sys/class/net/eth0/address | sed -r 's/[:]+//g' | sed -e 's#\(.\{6\}\)\(.*\)#\1fffe\2#g')

echo "Gateway EUI: $TTN_EUI"

ID=$(curl -sX GET "https://api.balena-cloud.com/v5/device?\$filter=uuid%20eq%20'$BALENA_DEVICE_UUID'" \
-H "Content-Type: application/json" \
-H "Authorization: Bearer $BALENA_API_KEY" | \
jq ".d | .[0] | .id")

TAG=$(curl -sX POST \
"https://api.balena-cloud.com/v5/device_tag" \
-H "Content-Type: application/json" \
-H "Authorization: Bearer $BALENA_API_KEY" \
--data "{ \"device\": \"$ID\", \"tag_key\": \"$TAG_KEY\", \"value\": \"$TTN_EUI\" }" > /dev/null)



if [ -z ${MODEL} ] ;
 then
    echo -e "\033[91mWARNING: MODEL variable not set.\n Set the model of the gateway you are using.\033[0m"
    balena-idle
 else
    echo "Using MODEL: $MODEL"
    if [ "$MODEL" = "RAK2245" ] || [ "$MODEL" = "iC880a" ];then
        ./start_rak2245.sh

    fi
    if [ "$MODEL" = "RAK2287" ];then
        ./start_rak2287.sh
    fi
fi
