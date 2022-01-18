# --- Revised 3-Clause BSD License ---
# Copyright Semtech Corporation 2022. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without modification,
# are permitted provided that the following conditions are met:
#
#     * Redistributions of source code must retain the above copyright notice,
#       this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright notice,
#       this list of conditions and the following disclaimer in the documentation
#       and/or other materials provided with the distribution.
#     * Neither the name of the Semtech corporation nor the names of its
#       contributors may be used to endorse or promote products derived from this
#       software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL SEMTECH CORPORATION. BE LIABLE FOR ANY DIRECT,
# INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
# BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
# LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
# OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
# ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

from typing import Any,List,Mapping,MutableMapping,Optional
from pathlib import Path
import yaml
import copy
import logging
import pprint
import struct
from id6 import Id6, Eui

logger = logging.getLogger('ts2pktfwd')

REGION_CONFIG_KEYWORDS = [ 'upchannels', 'DRs' ]

STATION_CONFIG_KEYWORDS = [ 'JoinEui', 'NetID', 'bcning', 'regionid' ]  # and more ..

class Region:
    def __init__(self, o:Mapping[str,Any]):
        self.name = o['name']
        self.config = o['config']
        for kw in REGION_CONFIG_KEYWORDS:
            assert kw in self.config, 'Missing region config key: %s' % (kw)

    def __str__(self):
        return 'Region:%s' % (self.name)


class RouterConfig:
    def __init__(self, routerid:Id6, config:MutableMapping[str,Any]):
        self.routerid = routerid
        station = config['station']
        self.station = station
        for kw in STATION_CONFIG_KEYWORDS:
            assert kw in station, 'Missing station config key in router config: %s' % (kw)
        regionid = station['regionid']
        if regionid not in regionid2region:
            raise Exception('Inexisting or invalid regionid in router configuration of %s' % (routerid))
        region = regionid2region[station['regionid']]
        station['DRs'] = region.config['DRs']
        station['upchannels'] = region.config['upchannels']
        self.region = region
        logger.debug('%s: station config:\n%s' % (self, pprint.pformat(self.station)))

        self.dr2sfbw = DR2SFBW(station)
        self.sfbw2dr = SFBW2DR(station)

        self.RxDelay  = 1

        region = station['region']
        if region == 'EU863':
            self.RX2DR = 0
            self.RX2Freq = 869525000
        elif region == 'US902':
            self.RX2DR = 8
            self.RX2Freq = 923300000
        else:
            raise Exception('Unsupported region: %s' % (region))

        pktfwd = config['pktfwd']
        self.pktfwd = pktfwd
        if 'gateway_ID' not in pktfwd:
            self.pktfwd['gateway_ID'] = self.routerid.id
        else:
            self.pktfwd['gateway_ID'] = struct.unpack('>q', bytes.fromhex(pktfwd['gateway_ID']))[0]


    def get_station_config_message(self) -> MutableMapping[str,Any]:
        return copy.deepcopy(self.station)

    def get_pktfwd_gateway_ID(self) -> int:
        return self.pktfwd['gateway_ID']

    def get_hwspec(self) -> str:
        return self.station['hwspec']

    def get_regionid(self) -> Any:
        return self.station['regionid']

    def __str__(self) -> str:
        return 'RouterConfig:%s' % (self.routerid)



def DR2SFBW(rconfig) -> Mapping[int,str]:
    return { i:'SF%dBW%d' % (t[0], t[1]) for i,t in enumerate(rconfig['DRs']) }

def SFBW2DR(rconfig) -> Mapping[int,str]:
    return { 'SF%dBW%d' % (t[0], t[1]):i for i,t in enumerate(rconfig['DRs']) }


routerid2config = {}  # type:Mapping[Id6,RouterConfig]
regionid2region = {}  # type:Mapping[Id6,Region]

def ini(paths:List[str]) -> None:
    for s in paths:
        p = Path(s)
        if not p.is_dir():
            raise Exception('Not a directory: %s' % (s))
    for s in paths:
        p = Path(s)
        f = p.joinpath('regions.yaml')
        if f.exists():
            regions = yaml.load(f.read_text(), Loader=yaml.SafeLoader)
            for regionid,o in regions.items():
                regionid2region[regionid] = Region(o)
            logger.info('router_config.ini: loaded regions from %s.' % (f))
            break
    for s in paths:
        p = Path(s)
        for f in p.glob('router-*.yaml'):
            name = f.name
            if name.endswith('.yaml'):
                try:
                    routerid = Id6(name[:-5])
                    if routerid.cat == 'router':
                        rc = RouterConfig(routerid, yaml.load(f.read_text(), Loader=yaml.SafeLoader))
                        routerid2config[routerid] = rc
                        logger.info('router_config.ini: loaded router configuration from %s.' % (f))
                    else:
                        logger.info('router_config.ini: ignore file %s.' % (f))
                except:
                    logger.info('router_config.ini: ignore file %s.' % (f), exc_info=True)
                    pass


def get_router_config(routerid:Id6) -> RouterConfig:
    if routerid not in routerid2config:
        raise Exception('No configuration found for router %s' % (routerid))
    return routerid2config[routerid]


ROUTER_CONFIG_EU863_TRACKNET8_AS_YAML = '''
JoinEui: null
NetID: null
bcning: null
config: {}
freq_range: [863000000, 870000000]
hwspec: sx1301/1
max_eirp: 16
protocol: 1
region: EU863
regionid: 1000
sx1301_conf:
- chan_FSK: {enable: false}
  chan_Lora_std: {enable: false}
  chan_multiSF_0: {enable: true, if: -375000, radio: 0}
  chan_multiSF_1: {enable: true, if: -175000, radio: 0}
  chan_multiSF_2: {enable: true, if: 25000, radio: 0}
  chan_multiSF_3: {enable: true, if: 375000, radio: 0}
  chan_multiSF_4: {enable: true, if: -237500, radio: 1}
  chan_multiSF_5: {enable: true, if: 237500, radio: 1}
  chan_multiSF_6: {enable: false}
  chan_multiSF_7: {enable: false}
  radio_0: {enable: true, freq: 868475000}
  radio_1: {enable: true, freq: 869287500}
'''

ROUTER_CONFIG_US902_BLOCK0_AS_YAML = '''
JoinEui: null
NetID: null
bcning: null
config: {}
freq_range: [902000000, 928000000]
hwspec: sx1301/1
max_eirp: 30.0
protocol: 1
region: US902
regionid: 1001
sx1301_conf:
- chan_FSK: {enable: false}
  chan_Lora_std: {bandwidth: 500000, enable: true, if: 300000, radio: 0, spread_factor: 8}
  chan_multiSF_0: {enable: true, if: -400000, radio: 0}
  chan_multiSF_1: {enable: true, if: -200000, radio: 0}
  chan_multiSF_2: {enable: true, if: 0, radio: 0}
  chan_multiSF_3: {enable: true, if: 200000, radio: 0}
  chan_multiSF_4: {enable: true, if: 400000, radio: 0}
  chan_multiSF_5: {enable: true, if: -200000, radio: 1}
  chan_multiSF_6: {enable: true, if: 0, radio: 1}
  chan_multiSF_7: {enable: true, if: 200000, radio: 1}
  radio_0: {enable: true, freq: 902700000}
  radio_1: {enable: true, freq: 903500000}
'''


