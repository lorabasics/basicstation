# LoRa Basics™ Station using balena.io with RAK2245 or RAK 2287 concentrators

This project deploys a LoRaWAN gateway with Basics Station Packet Forward protocol with balena. It runs on a Raspberry Pi or balenaFin with a RAK2245 and RAK2287 concentrator with a Pi Hat. 


## Introduction

Deploy a The Things Network (TTN), The Things Industries (TTI) or The Things Stack (TTS) LoRaWAN gateway running the Basics Station Semtech Packet Forward protocol. We are using balena.io and RAK to reduce fricition for the LoRa gateway fleet owners.

The Basics Station protocol enables the LoRa gateways with a reliable and secure communication between the gateways and the cloud and it is becoming the standard Packet Forward protocol used by most of the LoRaWAN operators.


## Getting started

### Hardware

* Raspberry Pi 4 or [balenaFin](https://www.balena.io/fin/)
* SD card in case of the RPi 4

#### LoRa Concentrators

* [RAK 2287 Concentrator](https://store.rakwireless.com/products/rak2287-lpwan-gateway-concentrator-module)
* [RAK 2287 Pi Hat](https://store.rakwireless.com/products/rak2287-pi-hat)

or

* [RAK 2245 pi hat](https://store.rakwireless.com/products/rak2245-pi-hat)

### Software

* A TTN account ([sign up here](https://console.thethingsnetwork.org))
* A balenaCloud account ([sign up here](https://dashboard.balena-cloud.com/))
* [balenaEtcher](https://balena.io/etcher)

Once all of this is ready, you are able to deploy this repository following instructions below.

## Deploy the code

### Via [Balena Deploy](https://www.balena.io/docs/learn/deploy/deploy-with-balena-button/)

Running this project is as simple as deploying it to a balenaCloud application. You can do it in just one click by using the button below:

[![](https://www.balena.io/deploy.png)](https://dashboard.balena-cloud.com/deploy?repoUrl=https://github.com/balenalabs/basicstation)

Follow instructions, click Add a Device and flash an SD card with that OS image dowloaded from balenaCloud. Enjoy the magic 🌟Over-The-Air🌟!


### Via [Balena-Cli](https://www.balena.io/docs/reference/balena-cli/)

If you are a balena CLI expert, feel free to use balena CLI.

- Sign up on [balena.io](https://dashboard.balena.io/signup)
- Create a new application on balenaCloud.
- Clone this repository to your local workspace.
- Using [Balena CLI](https://www.balena.io/docs/reference/cli/), push the code with `balena push <application-name>`
- See the magic happening, your device is getting updated 🌟Over-The-Air🌟!


## Configure the Gateway

### Define your MODEL

In case that your LoRa concentrator is a ```RAK2287```, it's important to change the Device Variable with the correct ```MODEL```. The default ```MODEL``` on the balena Application is the ```RAK2245```.

1. Go to balenaCloud dashboard and get into your LoRa gateway device site.
2. Click "Device Variables" button on the left menu and change the ```MODEL``` variable to ```RAK2287```.

That enables a fleet of LoRa gateways with both ```RAK2245``` and ```RAK2287``` together under the same app.

Before starting check the region where you are going to deploy the gateway, de facto configuration is for EU gateways. Change the ```TC_URI```if you are in a different zone than European.


### Get the EUI of the LoRa Gateway

The LoRa gateways are manufactured with a unique 64 bits (8 bytes) identifier, called EUI, which can be used to register the gateway on The Things Network. To get the EUI from your board it’s important to know the Ethernet MAC address of it. The TTN EUI will be the Ethernet mac address (6 bytes), which is unique, expanded with 2 more bytes (FFFE). This is a standard way to increment the MAC address from 6 to 8 bytes.

To get the EUI, copy the TAG of the device which will be generated automatically when the device gets provisioned on balenaCloud.

If that does not work, go to the terminal box and click "Select a target", then “HostOS”. Once you are inside the shell, type:

```cat /sys/class/net/eth0/address | sed -r 's/[:]+//g' | sed -e 's#\(.\{6\}\)\(.*\)#\1FFFE\2#g' ```

Copy the result and you are ready to register your gateway with this EUI.


### Configure your The Things Network gateway

1. Sign up at [The Things Network console](https://console.thethingsnetwork.org/). 
2. Click Gateways button.
3. Click the "Register gateway" link.
4. Check “I’m using the legacy packet forwarder” checkbox.
5. Paste the EUI from the balenaCloud tag or the Ethernet mac address of the board (calculated above)
6. Complete the form and click Register gateway.
7. Copy the Key generated on the gateway page.


### Configure your The Things Stack gateway (The Things Conference 2021)

1. Sign up at [The Things Stack console](https://ttc.eu1.cloud.thethings.industries/console/). 
2. Click "Go to Gateways" icon.
3. Click the "Add gateway" button.
4. Introduce the data for the gateway.
5. Paste the EUI from the balenaCloud tags.
6. Complete the form and click Register gateway.
7. Once the gateway is created, click "API keys" link.
8. Click "Add API key" button.
9. Select "Grant individual rights" and then "Link as Gateway to a Gateway Server for traffic exchange ..." and then click "Create API key".
10. Copy the API key generated. and bring it to balenaCloud as ```TC_KEY```.



### Balena LoRa Basics Station Service Variables

Once successfully registered:

1. Go to balenaCloud dashboard and get into your LoRa gateway device site.
2. Click "Device Variables" button on the left menu and add these variables.

#### The Things Network Variables

Remember to copy the The Things Network gateway KEY and ID to configure your board variables on balenaCloud.

The `GW_ID`and `GW_KEY` variables have been generated automatically when the Application has been created with the Deploy with Balena button. Replace the values with the KEY and ID from the TTN console.


Variable Name | Value | Description | Default
------------ | ------------- | ------------- | -------------
**`GW_GPS`** | `STRING` | Enables GPS | true or false
**`GW_ID`** | `STRING` | TTN Gateway EUI | (EUI)
**`GW_KEY`** | `STRING` | Unique TTN Gateway Key | (Key pasted from TTN console)
**`GW_RESET_PIN`** | `STRING` | Pin number that resets | 11 
**`TC_URI`** | `STRING` | basics station TC URI to get connected. If you are in the EU region use ```wss://lns.{eu-us-in-au}.thethings.network:443``` | ```wss://lns.eu.thethings.network:443```
**`MODEL`** | `STRING` | ```RAK2245``` or ```RAK2287``` | ```RAK2245```


#### The Things Stack Variables

Remember to generate an API Key and copy it. It will be the ```TC_KEY```.


Variable Name | Value | Description | Default
------------ | ------------- | ------------- | -------------
**`GW_GPS`** | `STRING` | Enables GPS | true or false
**`TC_KEY`** | `STRING` | Unique TTN Gateway Key | (Key pasted from TTN console)
**`GW_RESET_PIN`** | `STRING` | Pin number that resets | 11 
**`TC_URI`** | `STRING` | Gateway Server address. If you are in the EU region use ```wss://ttc.eu1.cloud.thethings.industries:8887```
**`MODEL`** | `STRING` | ```RAK2245``` or ```RAK2287``` | ```RAK2245```



At this moment your LoRaWAN gateway should be up and running. Check on the TTN or TTS console if it shows the connected status.


## Troubleshoothing

It's possible that on the TTN Console the gateway appears as Not connected if it's not receiving any LoRa message. Sometimes the websockets connection among the LoRa Gateway and the server can get broken. However a new LoRa package will re-open the websocket between the Gateway and TTN or TTI. This issue should be solved with the TTN v3.


## Attribution

- This is an adaptation of the [Semtech Basics Station repository](https://github.com/lorabasics/basicstation). Documentation [here](https://doc.sm.tc/station).
- This is in part working thanks of the work of Jose Marcelino from RAK Wireless and Marc Pous from balena.io.
- This is in part based on excellent work done by Rahul Thakoor from the Balena.io Hardware Hackers team.

