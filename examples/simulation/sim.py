# Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
#
# This file is subject to the terms and conditions defined in file 'LICENSE',
# which is part of this source code package.

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
