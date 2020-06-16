# CUPS Protocol Example

Station implements a gateway management protocol for managing credentials and firmware updates. Read more about it [here](https://doc.sm.tc/station/cupsproto.html).

This example demonstrates the common scenario of credentials bootstrapping: In the factory, a set of gateways are personalized with initial credentials to a central CUPS server instance (say `cups-0`). Their final destination is not yet known at the time of manufacturing. After one of these gateways is deployed and powered-on the first time, it has to be pointed to and provided credentials for its final LNS (say `tc-0`) in a secure way. Since `cups-0` is operated by an independent third party, it does not have access to the credentials needed to connect to `tc-0`. Therefore, the gateway is first redirected to the CUPS server (`cups-1`) which is operated by the owner of `tc-0` and can therefore provide the final credentials and potentially the latest firmware update.

Using the Station simulation environment, this example implements the two CUPS servers `cups-0` and `cups-1`, as well as the LNS server `tc-0` and configures them to perform the credentials bootstrapping procedure described above. For the purpose of this example, the identities of the three entities are rooted each in a different CA. Client and server credentials are signed by the same CA.

```
CUPS "A" CA
 ├── cups-0.{crt,key}         server auth
 └── cups-router-1.{crt,key}  client auth
CUPS "B" CA
 ├── cups-1.{crt,key}         server auth
 └── cups-router-1.{crt,key}  client auth
TC CA
 ├── muxs-0.{crt,key}         server auth
 ├── infos-0.{crt,key}        server auth
 └── tc-router-1.{crt,key}    client auth
```

**Disclaimer:** The focus of this example is to demonstrate the capabilities of the CUPS protocol based on a common usage scenario for gateway management. For this purpose and the sake of simplicity, the PKI employed in this example is setup in very simple way and does not resemble a production grade PKI.

## Relevant Files

* `sim.py`: Mockup server-side implementations of the CUPS and LNS Station protocols.
* `prep.sh`: Setup script for generating keys, certificates and configuration files for the credentials bootstrapping scenario.
* `emulate.sh`: A script containing a sequence of cURL requests to CUPS, emulating the behavior of Station under this scenario. This can be used to investigate the exact payloads exchanged during the call sequence.
* `makefile`: Top level makefile which can be used to setup and run the simulation.

## Prerequisites

The simulation example requires

* Python 3.5+
* Python packages according to `requirements.txt`
* make, bash, curl, openssl, gzip, xxd
* Optional: tmux

A possible way to setup a compatible python environment is using `virtualenv` and `pip`:

```
virtualenv -p python3.6 pyenv
. pyenv/bin/activate
pip install -r requirements.txt
```

## Usage

The CUPS example is controlled via a makefile with multiple targets:

* `make station`: Build the `testsim` station variant.
* `make prep`: Setup the expected directory structure (keys, certificates, config) without executing the simulation.
* `make sim`: Run the simulation in a single process. Log output of all components is interleaved in one terminal
* `make tmux`: Run the simulation in multiple processes inside different panes of a tmux split window.
* `make emulate`: Execute the call sequence via a set of cURL commands, emulating the behavior of Station in this scenario.
* `make clean`: Bring local directory into initial state.

## Explanation

In order to understand what is going on, let's dissect the log output produced by the `emulate` target. For the sake of clarity the log output shown here has slightly reduced verbosity.

### Startup
```
# make emulate

./prep.sh
== Build PKI ==
cups-0/cups-0.crt: OK
cups-0/cups-router-1.crt: OK
cups-1/cups-1.crt: OK
cups-1/cups-router-1.crt: OK
tc-0/muxs-0.crt: OK
tc-0/infos-0.crt: OK
tc-0/tc-router-1.crt: OK
== Prepare FW Update 1.0.0 -> 2.0.0 ==
```

The `emulate` target first executes the `prep.sh` script which sets up the folder structure as expected by the simulation. First, the three self-signed CA certificates are generated and used to sign the certificates of all our server entities `cups-0`, `cups-1`, `tc-0`, as well as the client certificates for `router-1` expected by each of them. A code signing key pair is generated and a signed update is placed in `cups-1`. The public part of the signing key is copied to the Station home directory `./shome`. The final directory structure will look like this:

```
.
├── cups-0
│   ├── cups-0.crt
│   ├── cups-0.key
│   ├── cups-0.trust
│   ├── cups.ca
│   ├── cups-router-1.cfg
│   ├── cups-router-1.crt
│   ├── cups-router-1.key
│   └── cups-router-1.trust
├── cups-1
│   ├── 2.0.0.bin
│   ├── 2.0.0.bin.sig-0
│   ├── cups-1.crt
│   ├── cups-1.key
│   ├── cups-1.trust
│   ├── cups.ca
│   ├── cups-router-1.cfg
│   ├── cups-router-1.crt
│   ├── cups-router-1.key
│   ├── cups-router-1.trust
│   └── sig-0.key
├── shome
│   ├── cups.crt
│   ├── cups.key
│   ├── cups.trust
│   ├── cups.uri
│   ├── sig-0.key
│   ├── station.conf
│   └── version.txt
├── tc-0
│   ├── infos-0.crt
│   ├── infos-0.key
│   ├── infos-0.trust
│   ├── muxs-0.crt
│   ├── muxs-0.key
│   ├── muxs-0.trust
│   ├── tc.ca
│   ├── tc-router-1.crt
│   ├── tc-router-1.key
│   └── tc-router-1.trust
└── upd-sig
    ├── sig-0.prime256v1.pem
    └── sig-0.prime256v1.pub
```

Once the `prep.sh` script completed, the `emulate.sh` script will start the server simulation if no running CUPS services were detected on localhost.

```
./emulate.sh
Starting CUPS.
LgwSimServer starting...
Starting INFOS (tc-0/infos-0) on Port 6038 (muxsuri=wss://localhost:6039/router)
Starting MUXS (tc-0/muxs-0) on Port 6039
Starting CUPS (cups-0/cups-0) on Port 6040
Starting CUPS (cups-1/cups-1) on Port 6041
```

Before the initial request, a list of client credentials and their CRC32 hashes is shown. These CRC32 hashes serve as identifiers for the credentials sets in the CUPS protocol. Since all keys are generated by `prep.sh`, these hashes will differ in your case:

```
CRC32  0xF13E8FC2 (4047409090) shome/sig-0.key
CRC32  0xAFC40ED3 (2948861651) cups-0/cups-router-1.{trust,crt,key}
CRC32  0x22220992 (572656018)  cups-1/cups-router-1.{trust,crt,key}
CRC32  0xE0F91F83 (3774422915) tc-0/tc-router-1.{trust,crt,key}
```

An empty credentials set corresponds to a zero-byte sequence of length 12, which yields a non-zero CRC32.

```
CRC32  0x7BD5C66F (2077607535) _empty_set_
```

### Request 1: CUPS Redirection

```
==== #1  [CUPS-0] REQUEST ===================
{
    "router":      "::1",
    "model":       "linux",
    "package":     "1.0.0",
    "station":     "Emulated Station",
    "cupsUri":     "https://localhost:6040",
    "tcUri":       "",
    "cupsCredCrc": 2948861651,
    "tcCredCrc":   2077607535,
    "keys":        [ 4047409090 ]
}
==== #1  [CUPS-0] RESPONSE ===================
  CUPS: No target version configured for this router. No update.
  CUPS: No fw update required
< CUPS Response:
  cupsUri : b'https://localhost:6041' <- [https://localhost:6040]
  tcUri   : b'' -- []
  cupsCred: 911 bytes -- [22220992] <- [AFC40ED3]
  tcCred  :   0 bytes -- [7BD5C66F]
  sigCrc  : 00000000
  sig     :   0 bytes
  fw      :   0 bytes -- [1.0.0]

HTTP/1.1 200 OK
Content-Length: 947
Content-Type: application/octet-stream
Server: Python/3.6 aiohttp/2.3.9

[ Binary dump of response body ]
```

In the first request to `cups-0`, Station receives a new CUPS URI and new CUPS credentials for connecting to `cups-1`.

### Request 2: LNS Credentials and Firmware Update

```
==== #2  [CUPS-1] REQUEST ===================
{
    "router":      "::1",
    "model":       "linux",
    "package":     "1.0.0",
    "station":     "Emulated Station",
    "cupsUri":     "https://localhost:6041",
    "tcUri":       "",
    "cupsCredCrc": 572656018,
    "tcCredCrc":   2077607535,
    "keys":        [ 4047409090 ]
}
==== #2  [CUPS-1] RESPONSE ===================
  CUPS: Target version: 2.0.0 (cups-1/2.0.0.bin)
  CUPS: Found signing key cups-1/sig-0.key -> CRC F13E8FC2
  CUPS: Found signature cups-1/2.0.0.bin.sig-0
  CUPS: Found matching signing key with CRC F13E8FC2
< CUPS Response:
  cupsUri : b'' -- [https://localhost:6041]
  tcUri   : b'wss://localhost:6038' <- []
  cupsCred:   0 bytes -- [22220992]
  tcCred  : 900 bytes -- [E0F91F83] <- [7BD5C66F]
  sigCrc  : F13E8FC2
  sig     :  74 bytes
  fw      :  83 bytes -- [2.0.0] <- [1.0.0]

HTTP/1.1 200 OK
Content-Length: 1091
Content-Type: application/octet-stream
Server: Python/3.6 aiohttp/2.3.9

[ Binary dump of response body ]
```

The second request is directed to `cups-1`. The response contains a firmware update (in this case a bash script) together with a signature. The signature originates from the private key which corresponds to the one of the public keys identified by the key CRCs in the `keys` list of the request. This mechanism allows the server to choose out of multiple signatures the one which can be verified by the gateway. The response also contains the URI and credentials of the LNS endpoint (`tcUri` and `tcCred`).

### Request 3: Steady State

```
==== #3  [CUPS-1] REQUEST ===================
{
    "router":      "::1",
    "model":       "linux",
    "package":     "2.0.0",
    "station":     "Emulated Station",
    "cupsUri":     "https://localhost:6041",
    "tcUri":       "wss://localhost:6038",
    "cupsCredCrc": 572656018,
    "tcCredCrc":   3774422915,
    "keys":        [ 4047409090 ]
}
==== #3  [CUPS-1] RESPONSE ===================
  CUPS: Target version: 2.0.0 (cups-1/2.0.0.bin)
  CUPS: Found signing key cups-1/sig-0.key -> CRC F13E8FC2
  CUPS: Found signature cups-1/2.0.0.bin.sig-0
  CUPS: No fw update required
< CUPS Response:
  cupsUri : b'' -- [https://localhost:6041]
  tcUri   : b'' -- [wss://localhost:6038]
  cupsCred:   0 bytes -- [22220992]
  tcCred  :   0 bytes -- [E0F91F83]
  sigCrc  : 00000000
  sig     :   0 bytes
  fw      :   0 bytes -- [2.0.0]

HTTP/1.1 200 OK
Content-Length: 14
Content-Type: application/octet-stream
Server: Python/3.6 aiohttp/2.3.9

00000000: 0000 0000 0000 0000 0000 0000 0000       ..............
```

The third request is answered with an all-zero response, meaning that the condition of the gateway corresponds to the target state expected by the CUPS server. The all-zero response puts Station into a low-dutycycle re-sync mode.
