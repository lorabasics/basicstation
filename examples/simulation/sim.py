# --- Revised 3-Clause BSD License ---
# Copyright (C) 2016-2019, SEMTECH (International) AG.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without modification,
# are permitted provided that the following conditions are met:
#
#     * Redistributions of source code must retain the above copyright notice,
#       this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright notice,
#       this list of conditions and the following disclaimer in the documentation
#       and/or other materials provided with the distribution.
#     * Neither the name of the copyright holder nor the names of its contributors
#       may be used to endorse or promote products derived from this software
#       without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL SEMTECH BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
# PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
# LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
# OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
# ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import os
import sys
import time
import json
import asyncio
import random
from asyncio import subprocess

sys.path.append('../../pysys')
import tcutils as tu
import simutils as su

station = None
infos = None
muxs = None
sim = None

class ExampleLgwSimServer(su.LgwSimServer):
    '''Generate an uplink every 10 seconds'''

    fcnt = 0
    updf_task = None
    txcnt = 0

    async def on_connected(self, lgwsim:su.LgwSim) -> None:
        self.updf_task = asyncio.ensure_future(self.send_updf())

    async def on_close(self):
        self.updf_task.cancel()
        self.updf_task = None
        print('x LGWSIM - close')

    async def on_tx(self, lgwsim, pkt):
        print('< LGWSIM TX - %r' % (pkt,))
        self.txcnt += 1

    async def send_updf(self) -> None:
        '''Send uplinks periodically'''
        try:
            while True:
                freq = tu.router_config_EU863_6ch['upchannels'][random.randint(0,5)][0] / 1e6
                print('> LGWSIM RX - FCnt=%d Freq=%.3fMHz' % (self.fcnt, freq))
                if 0 not in self.units:
                    return
                lgwsim = self.units[0]
                await lgwsim.send_rx(rps=(7,125), freq=freq, frame=su.makeDF(fcnt=self.fcnt, port=1))
                self.fcnt += 1
                await asyncio.sleep(10.0)
        except Exception as exc:
            pass


class ExampleMuxs(tu.Muxs):
    '''Respond to uplink in the first downlink window'''

    async def handle_dntxed(self, ws, msg):
        pass

    async def handle_updf(self, ws, msg):
        fcnt = msg['FCnt']
        port = msg['FPort']
        dnframe = {
            'msgtype' : 'dnmsg',
            'dC'      : 0,
            'priority': 0,
            'RxDelay' : 0,
            'RX1DR'   : msg['DR'],
            'RX1Freq' : msg['Freq'],
            'DevEui'  : '00-00-00-00-11-00-00-01',
            'xtime'   : msg['upinfo']['xtime']+1000000, # First downlink window
            'seqno'   : fcnt,
            'MuxTime' : time.time(),
            'rctx'    : msg['upinfo']['rctx'],
            'pdu'     : '0A0B0C0D0E0F',
        }
        print("< MUXS: %r" %(dnframe))
        await ws.send(json.dumps(dnframe))

tls_mode = False
tls_no_ca = True
ws = 'wss' if tls_mode else 'ws'

async def start_tcsim():
    global infos
    global muxs
    infos = tu.Infos(muxsuri = ('%s://localhost:6039/router' % ws),
                     tlsidentity = ('infos-0' if tls_mode else None),
                     tls_no_ca = tls_no_ca)
    muxs = ExampleMuxs(tlsidentity = ('muxs-0' if tls_mode else None),
                    tls_no_ca = tls_no_ca)
    await infos.start_server()
    await muxs.start_server()

async def start_lgwsim():
    global sim
    sim = ExampleLgwSimServer()
    await sim.start_server()

async def start_station():
    global station
    # 'valgrind', '--leak-check=full',
    station_args = ['station','-p', '--temp', '.']
    station = await subprocess.create_subprocess_exec(*station_args)

async def start_sim():
    await start_tcsim()
    await start_lgwsim()

async def start_test():
    await start_sim()
    await start_station()

func = None
if len(sys.argv) > 1:
    func = {
        'tc': start_tcsim,
        'lgwsim': start_lgwsim,
        'sim': start_sim,
        'station': start_station
    }.get(sys.argv[1], None)

if not func:
    func = start_test

asyncio.ensure_future(func())
asyncio.get_event_loop().run_forever()
