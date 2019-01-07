# Station to Packet Forwarder Protocol Bridge (st2pkfwd) Example

This example is a minimal implementation of a protocol bridge which translates packets between the WebSocket-based [Station Protocol](https://doc.sm.tc/station/tcproto.html) (st) and the UDP-based [Packet Forwarder Protocol](pkfwd-protocol.txt) (pkfwd). This protocol bridge allows to quickly attach Station-based hardware (or the [Station Simulation](../simulation/)) to existing network server implementations which support the pkfwd protocol.

## Prerequisites

The st2pkfwd example requires

* Python 3.5+
* Python packages according to `requirements.txt`

A possible way to setup a compatible python environment is using `virtualenv` and `pip`:

```
virtualenv -p python3.6 pyenv
. pyenv/bin/activate
pip install -r requirements.txt
```

## Configuration

Region information is loaded from `regions.yaml`. Each region is defined there with its regionid, name and configuration. `upchannels` and `DRs` are injected into the router configurations which references this region with the regionid.

Routers are configured in `router-<ID>.yaml` files. This router configuration is merged with the region information and sent to Station on connect.

Region and router configurations are loaded at startup.

## Usage

```
usage: main.py [-h] [--infosuri INFOSURI] [--muxsuri MUXSURI]
               [--pkfwduri PKFWDURI] [--confdir CONFDIR] [--logfile LOGFILE]
               [--loglevel {ERROR,WARNING,INFO,DEBUG}]
               [routerids [routerids ...]]

positional arguments:
  routerids             Router ids

optional arguments:
  -h, --help            show this help message and exit
  --infosuri INFOSURI   Info server base URI.
  --muxsuri MUXSURI     Mux server base URI, by default Infos port plus 2.
  --pkfwduri PKFWDURI   Packet forwarder destination URI.
  --confdir CONFDIR     Directory where to load region and router
                        configuration.
  --logfile LOGFILE     Log file, by default logged to stdout.
  --loglevel {ERROR,WARNING,INFO,DEBUG}
                        Log level: ['ERROR', 'WARNING', 'INFO', 'DEBUG']
```

Example:

```
python ./main.py --infosuri ws://localhost:6090 --pkfwduri udp://localhost:1680 router-1
```

This will start a Service Discovery Server instance (internally referred to as INFOS) for incoming Station connections at `ws://localhost:6090`. A Gateway Server instance (internally referred to as MUXS) is implicitly started at 6090 + 2 if not overridden by the `--muxsuri` option. For more information about INFOS and MUXS, please refer to the [Station LNS Protocol Documentation](https://doc.sm.tc/station/tcproto.html). A packet forwarder protocol compatible LNS endpoint is expected at `udp://localhost:1680` in this example. For inspection purposes `nc -l -u 1680 | hexdump` will do.

Parameter `--confdir` specifies the directory where regions and router configurations are loaded from.

## Code

- `Id6.py`:           EUI, Id6 helper class.
- `bgtask.py`:        Helper class.
- `main.py`:          Implements contact points for Station (INFOS and MUXS) and starts service.
- `router.py`:        Bridges traffic from Station to pkfwd network server and vice versa.
- `pkfwdc.py`:        UDP bridge to packet forwarder server.
- `router_config.py`: Handles router and region configurations.
- yaml files:         Samples for router and region configurations.


## Missing features

- Only one radio unit supported
- pkfwd stats missing
