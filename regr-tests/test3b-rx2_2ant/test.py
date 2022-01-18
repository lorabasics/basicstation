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
logger = logging.getLogger('test3b-rx2_2ant')

import tcutils as tu
import simutils as su
import testutils as tstu


station = None
infos = None
muxs = None
sim = None

n_ant = 1 if os.environ['TEST_VARIANT']=='testsim' else 2
omni = (os.environ['ANTENNA_TYPE'] == 'omni')


class TestLgwSimServer(su.LgwSimServer):
    fcnt = 0
    updf_task = None
    txcnt = 0

    async def on_connected(self, lgwsim:su.LgwSim) -> None:
        if lgwsim.unitIdx == 0:
            self.updf_task = asyncio.ensure_future(self.send_updf())
            muxs.xticks = lgwsim.xticks

    async def on_close(self):
        if self.updf_task:
            self.updf_task.cancel()
            self.updf_task = None
        logger.debug('LGWSIM - close')

    async def on_tx(self, lgwsim, pkt):
        logger.debug('LGWSIM: TX %r' % (pkt,))
        self.txcnt += 1

    async def send_updf(self) -> None:
        try:
            await asyncio.sleep(1.0)
            lgwsim = self.units[0]
            await lgwsim.send_rx(rps=(7,125), freq=869.525, frame=su.makeDF(fcnt=0, port=1))
            self.updf_task = None
        except Exception as exc:
            logger.error('send_updf failed!', exc_info=True)


class TestMuxs(tu.Muxs):
    expected = []
    seqno = 0
    ws = None
    send_task = None
    ev = None
    xtime_ext = 0
    xticks = None

    async def handle_connection(self, ws):
        self.ws = ws
        self.ev = asyncio.Event()
        await super().handle_connection(ws)

    async def testDone(self, status):
        global station
        if station:
            station.terminate()
            await station.wait()
            station = None
        os._exit(status)

    async def handle_updf(self, ws, msg):
        logger.debug('UPDF: rctx=%r Fcnt=%d Freq=%.3fMHz FPort=%d' % (msg['upinfo']['rctx'], msg['FCnt'], msg['Freq']/1e6, msg['FPort']))
        xtime = msg['upinfo']['xtime']
        self.xtime_ext = xtime >> 32
        logger.debug('xtime_ext: 0x%X - xtime=0x%X xticks=0x%X' % (self.xtime_ext, xtime, self.xticks()))
        self.send_task = asyncio.ensure_future(self.send_classA())

    async def handle_dntxed(self, ws, msg):
        if (msg['seqno'], msg['rctx']) != (self.expected[0] if self.expected else None):
            logger.debug('DNTXED: %r\nbut expected seqno=%r' % (msg, self.expected))
            await self.testDone(2)
        logger.debug('DNTXED %d ant#%d' % (msg['seqno'], msg['rctx']))
        del self.expected[0]
        self.ev.set()


    def make_dnmsgA(self, rx1dr=0, rx1freq=869.525, rx2dr=0, rx2freq=869.525, dr=-1, plen=5,
                    rctx=0, delayms=0, xoff=0):
        if dr >= 0:
            rx1dr = rx2dr = dr
        dnmsg = {
            'msgtype' : 'dnmsg',
            'dC'      : 0,            # device class A
            'dnmode'  : 'updn',
            'priority': 0,
            'RxDelay' : 1,
            'RX1DR'   : rx1dr,
            'RX1Freq' : int(rx1freq*1e6),
            'RX2DR'   : rx2dr,
            'RX2Freq' : int(rx2freq*1e6),
            'DevEui'  : '00-00-00-00-11-00-00-01',
            'xtime'   : 0,  # replaced when actually sent
            'seqno'   : self.seqno,
            'MuxTime' : time.time(),
            'rctx'    : rctx,         # antenna
            'pdu'     : bytes(range(plen)).hex(),
            'delayms' : delayms,
        }
        if xoff:
            dnmsg['xoff'] = xoff
        self.seqno += 1
        return dnmsg

    async def send_classA_seq(self, dnmsgs, exp):
        self.expected.extend((dnmsgs[i]['seqno'], ant) for i, ant in exp)
        for dnmsg in dnmsgs:
            delayms = dnmsg.pop('delayms',0)
            if delayms: await asyncio.sleep(delayms/1e3)
            dnmsg['xtime'] = self.xticks() + dnmsg.pop('xoff', -500000) + (self.xtime_ext<<32)
            await self.ws.send(json.dumps(dnmsg))
        while self.expected:
            self.ev.clear()
            await asyncio.wait_for(self.ev.wait(), 5.0)
        await asyncio.sleep(4.0)  # assure no late dntxed is coming in and previous frames done with TX
        assert not self.expected
        logger.debug('%s ok: %r' % ('-'*30, exp))


    async def send_classA(self):
        try:
            if n_ant == 2 and omni:
                dnmsgs = [
                    self.make_dnmsgA(dr=0, plen=20),   # airtime: 1.3s - blocks ant#0
                    self.make_dnmsgA(dr=0, plen=20),   # airtime: 1.3s - switches to ant#1
                    self.make_dnmsgA(dr=0, plen=20) ]  # cannot be sent - blocked previous (for 1s)
                await self.send_classA_seq(dnmsgs, [(0,0),(1,1)])
                dnmsgs = [
                    self.make_dnmsgA(dr=1, plen=30),   # airtime: 0.91s - blocks ant#0
                    self.make_dnmsgA(dr=2, plen=30),   # airtime: 0.45s - blocks ant#1
                    self.make_dnmsgA(dr=0, plen=30),   # can be sent after switch to RX2 +1.0s ant#0
                    self.make_dnmsgA(dr=0, plen=20),   # can be sent after switch to RX2 +1.0s ant#1
                    self.make_dnmsgA(dr=0, plen=20) ]  # cannot be sent anymore
                # 1/3 has shorter txtime, dntxed arrives before 0/2
                await self.send_classA_seq(dnmsgs, [(0,0),(1,1),(2,0),(3,1)])
                # if dntxed sent at end of frame (old): await self.send_classA_seq(dnmsgs, [(1,1),(0,0),(3,1),(2,0)])
                await self.testDone(0)

            if n_ant == 1 or not omni:
                dnmsgs = [
                    self.make_dnmsgA(dr=1, plen=30, delayms=0),     # airtime: 0.91s - blocks ant#0
                    self.make_dnmsgA(dr=0, plen=20, delayms=0),     # airtime: 1.3s - switch to RX2
                    self.make_dnmsgA(dr=0, plen=20, delayms=0) ]    # cannot be sent by RX2
                await self.send_classA_seq(dnmsgs, [(0,0),(1,0)])
                await self.testDone(0)

            await self.testDone(1)

        except Exception as exc:
            logger.error('send_classA failed: %s', exc, exc_info=True)
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
