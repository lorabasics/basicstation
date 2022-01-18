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
logger = logging.getLogger('test3b-dnC_2ant')

import tcutils as tu
import simutils as su
import testutils as tstu


station = None
infos = None
muxs = None
sim = None

n_ant = 1 if os.environ['TEST_VARIANT']=='testsim' else 2
omni = (os.environ['ANTENNA_TYPE'] == 'omni')


class TestMuxs(tu.Muxs):
    expected = []
    seqno = 0
    ws = None
    send_task = None
    ev = None

    async def handle_connection(self, ws):
        self.ws = ws
        self.ev = asyncio.Event()
        self.send_task = asyncio.ensure_future(self.send_classC())
        await super().handle_connection(ws)

    async def testDone(self, status):
        global station
        if station:
            station.terminate()
            await station.wait()
            station = None
        os._exit(status)

    async def handle_dntxed(self, ws, msg):
        if (msg['seqno'], msg['rctx']) != (self.expected[0] if self.expected else None):
            logger.error('DNTXED: %r\nbut expected seqno=%r' % (msg, self.expected))
            await self.testDone(2)
        logger.debug('DNTXED %d ant#%d' % (msg['seqno'], msg['rctx']))
        del self.expected[0]
        self.ev.set()


    def make_dnmsgC(self, rx2dr=0, rx2freq=869.525, plen=5, rctx=0, delayms=0):
        dnmsg = {
            'msgtype' : 'dnmsg',
            'dC'      : 2,            # device class C
            'dnmode'  : 'dn',
            'priority': 0,
            'RxDelay' : 0,
            'RX2DR'   : rx2dr,
            'RX2Freq' : int(rx2freq*1e6),
            'DevEui'  : '00-00-00-00-11-00-00-01',
            'xtime'   : 0,        # not required
            'seqno'   : self.seqno,
            'MuxTime' : time.time(),
            'rctx'    : rctx,         # antenna
            'pdu'     : bytes(range(plen)).hex(),
            'delayms' : delayms,
        }
        self.seqno += 1
        return dnmsg

    async def send_classC_seq(self, dnmsgs, exp):
        self.expected.extend((dnmsgs[i]['seqno'], ant) for i, ant in exp)
        for dnmsg in dnmsgs:
            delayms = dnmsg.pop('delayms',0)
            if delayms: await asyncio.sleep(delayms/1e3)
            await self.ws.send(json.dumps(dnmsg))
        while self.expected:
            self.ev.clear()
            await asyncio.wait_for(self.ev.wait(), 5.0)
        await asyncio.sleep(3.0)  # assure no late dntxed is coming in and all frame are done with TX
        logger.debug('%s ok: %r' % ('-'*30, exp))


    async def send_classC(self):
        # Wait a while until station has synced time with SX130x
        # otherwise class C gets rejected
        await asyncio.sleep(3.0)
        try:
            if n_ant == 2 and omni:
                dnmsgs = [
                    self.make_dnmsgC(rx2dr=0, plen=20),   # airtime: 1.3s - blocks ant#0
                    self.make_dnmsgC(rx2dr=0, plen=20),   # airtime: 1.3s - switches to ant#1
                    self.make_dnmsgC(rx2dr=0, plen=20) ]  # cannot be sent - blocked previous (for 1s)
                await self.send_classC_seq(dnmsgs, [(0,0),(1,1)])

                dnmsgs = [
                    self.make_dnmsgC(rx2dr=1, plen=30),   # airtime: 0.91s - blocks ant#0
                    self.make_dnmsgC(rx2dr=2, plen=30),   # airtime: 0.45s - blocks ant#1
                    self.make_dnmsgC(rx2dr=0, plen=20) ]  # can be sent after push back by 0.5/1s
                await self.send_classC_seq(dnmsgs, [(0,0),(1,1),(2,1)])  # 1 has shorter txtime, dntxed arrives before 0
                # OLD: If dntxed is sent at txend then order of dntxed is determined by frame length
                # OLD: 1 has shorter txtime, dntxed arrives before 0
                # OLD: await self.send_classC_seq(dnmsgs, [(1,1),(0,0),(2,1)])
                await self.testDone(0)

            if n_ant == 1 or not omni:
                dnmsgs = [
                    self.make_dnmsgC(rx2dr=0, plen=20, delayms=0),     # airtime: 1.3s - blocks ant#0
                    self.make_dnmsgC(rx2dr=0, plen=20, delayms=0),     # airtime: 1.3s - blocked by previous
                    self.make_dnmsgC(rx2dr=0, plen=20, delayms=500) ]  # can be sent by push back
                await self.send_classC_seq(dnmsgs, [(0,0),(2,0)])
                await self.testDone(0)

            await self.testDone(1)

        except Exception as exc:
            logger.error('send_classC failed: %s', exc, exc_info=True)
            await self.testDone(1)


with open("tc.uri","w") as f:
    f.write('ws://localhost:6038')

async def test_start():
    global station, infos, muxs, sim
    infos = tu.Infos()
    muxs = TestMuxs()
    sim = su.LgwSimServer()

    await infos.start_server()
    await muxs.start_server()
    await sim.start_server()

    # 'valgrind', '--leak-check=full',
    station_args = ['station','-p', '--temp', '.']
    station = await subprocess.create_subprocess_exec(*station_args)

tstu.setup_logging()

asyncio.ensure_future(test_start())
asyncio.get_event_loop().run_forever()
