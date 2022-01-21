#!/usr/bin/env bash

TAG_KEY="EUI"

if [ -z ${EUI_ADDRESS} ] ;
 then
    TTN_EUI=$(cat /sys/class/net/eth0/address | sed -r 's/[:]+//g' | sed -e 's#\(.\{6\}\)\(.*\)#\1fffe\2#g')
 else
    echo "Using DEVICE: $EUI_ADDRESS"
    TTN_EUI=$(cat /sys/class/net/wlan0/address | sed -r 's/[:]+//g' | sed -e 's#\(.\{6\}\)\(.*\)#\1fffe\2#g')
fi


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
    echo -e "\033[91mWARNING: MODEL variable not set.\n Set the model of the gateway you are using (SX1301 or SX1302).\033[0m"
    balena-idle
 else
    echo "Using MODEL: $MODEL"
    if [ "$MODEL" = "SX1301" ] || [ "$MODEL" = "RAK2245" ] || [ "$MODEL" = "iC880a" ];then
        ./start_sx1301.sh
    fi
    if [ "$MODEL" = "SX1302" ] || [ "$MODEL" = "RAK2287" ];then
        ./start_sx1302.sh
    fi
fi

#balena-idle
