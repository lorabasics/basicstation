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

import os
import sys
import time
import json
import asyncio
from asyncio import subprocess

import logging
logger = logging.getLogger('test3c-cca')

import tcutils as tu
import simutils as su
import testutils as tstu


station = None
infos = None
muxs = None
sim = None

FREQ1 = 922.1
FREQ2 = 922.3
FREQ3 = 922.5


class TestLgwSimServer(su.LgwSimServer):
    fcnt = 0
    updf_task = None
    txcnt = 0
    exp_txfreq = []

    async def on_connected(self, lgwsim:su.LgwSim) -> None:
        now = lgwsim.xticks()
        await lgwsim.send_cca([(FREQ2, now, now+int(20e6))])

    async def on_close(self):
        self.updf_task.cancel()
        self.updf_task = None
        logger.debug('LGWSIM - close')

    async def on_tx(self, lgwsim, pkt):
        logger.debug('LGWSIM: TX %r' % (pkt,))
        assert pkt['rf_power'] == 23
        self.txcnt += 1
        if [pkt['freq_hz']] != self.exp_txfreq[0:1]:
            logger.debug('LGWSIM: freq=%.3fMHz but expected %.3fMHz' % (pkt['freq_hz']/1e6, self.exp_txfreq[0]/1e6))
            await self.testDone(2)
        del self.exp_txfreq[0]


class TestMuxs(tu.Muxs):
    exp_seqno = []
    seqno = 0
    ws = None
    send_task = None
    ev = None

    def get_router_config(self):
        return tu.router_config_KR920

    async def handle_connection(self, ws):
        self.ws = ws
        self.ev = asyncio.Event()
        self.send_task = asyncio.ensure_future(self.send_classC())
        await super().handle_connection(ws)

    async def testDone(self, status):
        global station
        if station:
            try:
                station.terminate()
            except Exception as exc:
                logger.error('Shutting down station: %s', exc, exc_info=True)
            try:
                await station.wait()
                logger.error('Exit code station: %d', station.returncode)
                station = None
            except Exception as exc:
                logger.error('Failed to get exit code of station: %s', exc, exc_info=True)
            os._exit(status)

    async def handle_dntxed(self, ws, msg):
        if [msg['seqno']] != self.exp_seqno[0:1]:
            logger.debug('DNTXED: %r\nbut expected seqno=%r' % (msg, self.exp_seqno))
            await self.testDone(2)
        del self.exp_seqno[0]
        self.ev.set()


    # airtime: dr=4 (SF8) plen=12  <83ms
    def make_dnmsgC(self, rx2dr=4, rx2freq=FREQ1, plen=12):
        dnmsg = {
            'msgtype' : 'dnmsg',
            'dC'      : 2,          # device class C
            'dnmode'  : 'dn',
            'priority': 0,
            'RX2DR'   : rx2dr,
            'RX2Freq' : int(rx2freq*1e6),
            'DevEui'  : '00-00-00-00-11-00-00-01',
            #'xtime'  : 0,  # not required
            'seqno'   : self.seqno,
            'MuxTime' : time.time(),
            'rctx'    : 0,                   # antenna#0
            'pdu'     : bytes(range(plen)).hex(),
        }
        self.seqno += 1
        return dnmsg

    async def send_classC(self):
        # Wait a while until station has synced time with SX130x
        # otherwise class C gets rejected
        await asyncio.sleep(3.0)
        try:
            assert self.seqno & 1 == 0

            for f in (FREQ1,FREQ2,FREQ3,FREQ2):
                dnmsg = self.make_dnmsgC(rx2freq=f)
                if f != FREQ2:
                    self.exp_seqno.append(dnmsg['seqno'])
                    sim.exp_txfreq.append(dnmsg['RX2Freq'])
                await self.ws.send(json.dumps(dnmsg))

            while self.exp_seqno:
                self.ev.clear()
                await asyncio.wait_for(self.ev.wait(), 5.0)

            await asyncio.sleep(2.0)
            assert sim.exp_txfreq == []
            await self.testDone(0)
        except asyncio.CancelledError:
            logger.debug('send_classC canceled.')
        except Exception as exc:
            logger.error('send_classC failed: %s', exc, exc_info=True)
            await self.testDone(1)


with open("tc.uri","w") as f:
    f.write('ws://localhost:6038')

async def test_start():
    global station, infos, muxs, sim
    infos = tu.Infos()
    muxs = TestMuxs()
    sim = TestLgwSimServer()

    await infos.start_server()
    await muxs.start_server()
    await sim.start_server()

    # 'valgrind', '--leak-check=full',
    station_args = ['station','-p', '--temp', '.']
    station = await subprocess.create_subprocess_exec(*station_args)

tstu.setup_logging()

asyncio.ensure_future(test_start())
asyncio.get_event_loop().run_forever()
