# Setting up a LNS connection

To setup a LNS connection, you will need some combination of the following:

- `tc.key`
- `tc.trust`
- `tc.uri`

For what these files are, see the [LNS documentation](https://doc.sm.tc/station/authmodes.html)

## Connecting to The Things Network

To connect to The Things Network, you should

1. Create a new gateway in the console, just use any random gateway EUI would work, we would change it later
2. Set a frequency plan. For example, `Asia 923-925`. Prefer those with "Used by TTN" in the description. It has been tested with `Asia 923-925 (Used by TTN Australia - Secondary Channels)`.
3. Obtain the LNS URI, for example `wss://au1.cloud.thethings.network:8887`. See [LNS Server Address](https://www.thethingsindustries.com/docs/hardware/gateways/concepts/lora-basics-station/lns/#lns-server-address). Alternatively look at your console's gateway URL. For example, if the URL is `https://au1.cloud.thethings.network/console/gateways/add`, then the LNS URI would be `wss://au1.cloud.thethings.network:8887`
4. Get the LNS Key by ticking the "Require authenticated connections" box and then clicking "Generate API key for LNS". You will be prompted with a token and a file to download after you register the gateway. Copy its contents into `tc.key` next to `tc.key.example`.
5. Get the CA certificate. See `tc.trust.example` for instructions.
6. Untick "share gateway infomation" if you wish to
7. After registering, start the station with `./start-station.sh -l ./lns-ttn`. Expecxt it to not connect, but you should see `Station EUI` in the logs. Copy the EUI and paste it into the console's gateway EUI field. The EUI might be missing a leading zeros in each of the sections, so make sure to add those if necessary. For example, if the EUI is `ab:cd:ef:12`, then the zero-padded version would be `00 ab 00 cd 00 ef 00 12`.
8. Stop the station and start it again. It should connect to TTN and you should see `connected to LNS` in the logs.

<!-- TODO: Add connection guides to other platforms -->

## Troubleshooting

### Connection issues

- "The peer notified us that the connection is going to be closed"
    - Normal behaviour. Just wait for it to reconnect. If it doesn't reconnect after a few minutes, then there might be an issue with your configuration.
- Connection hangs
    - Check `tc.uri` is correct
    - Check your network connection

### LoraWan issues
- "Unrecognized region: XXXXX - ignored"
    - It was tested on `Asia 923-925 (Used by TTN Australia - Secondary Channels)`. A patch has to be applied for it to work in this region. 
    - If you use another region that also have this issue, see [this discussion](https://forum.chirpstack.io/t/downlink-packets-get-dropped-in-the-gateway/14568/5).
- "Received 'dnmsg' before 'router_config' - dropped"
    - If you look carefully, you should see "Unrecognized region: XXXXX - ignored" earlier. Same solution as above.
- "Beaconing suspend - missing GPS data: time"
    - This is normal if you don't have a GPS connected. Just ignore it. It will still work without GPS, but it won't be able to beacon or do time synchronization.
    - Unless you need to operate in Class B, this doesn't matter. 

