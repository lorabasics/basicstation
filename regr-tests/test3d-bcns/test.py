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
logger = logging.getLogger('test3d-bcns')

import tcutils as tu
import simutils as su
import testutils as tstu


station = None
infos = None
muxs = None
sim = None

REGION = os.environ.get('REGION','KR920')
PPM    = 1000000
BINTV  = 2
PPSTHRES = 10  # measure for timing accuracy of test/sim platform

class TestLgwSimServer(su.LgwSimServer):
    fcnt = 0
    updf_task = None
    txcnt = 0
    seen_bcnfreqs = set()
    mono2utc = 0.0
    last_secs = 0
    test_muxs = None
    last_chnl = -1

    async def on_connected(self, lgwsim:su.LgwSim) -> None:
        self.mono2utc = int((time.time() - time.monotonic()) * 1e6)

    async def on_close(self):
        self.updf_task.cancel()
        self.updf_task = None
        logger.debug('LGWSIM - close')

    async def on_tx(self, lgwsim, pkt):
        try:
            if pkt['tx_mode'] != lgwsim.hal.ON_GPS:
                logger.debug('LGWSIM: tx_mode!=ON_GPS (ignored): %r', pkt)
                return
            xticks = pkt['count_us']
            mono = lgwsim.xticks2mono(xticks)
            utc = mono + self.mono2utc
            logger.info('LGWSIM: ON_GPS xticks=%d/%X mono=%d/%X utc=%d/%X',
                        xticks, xticks, mono, mono, utc, utc)
            logger.debug('LGWSIM: ON_GPS %r', pkt)
            d = utc % PPM
            if d >= PPM//2: d -= PPM
            assert abs(d) < PPSTHRES, f'Failed abs(d)={abs(d)} < PPSTHRES={PPSTHRES}'
            assert (utc - d) % PPM == 0
            secs = (utc - d) // PPM
            assert self.last_secs==0 or self.last_secs+BINTV == secs
            self.last_secs = secs
            if REGION == 'KR920':
                assert pkt['freq_hz'] == 923100000
                assert pkt['size'] == 17
            if REGION == 'US915':
                chnl = (pkt['freq_hz'] - 923300000) / 600000
                assert self.last_chnl < 0 or (self.last_chnl + 1) % 8 == chnl
                self.last_chnl = chnl
                assert pkt['size'] == 23
    
            self.txcnt += 1
            if self.txcnt >= 10:
                await self.test_muxs.testDone(0)
        except Exception as exc:
            logger.error('FAILED: %s', exc, exc_info=True)
            await self.test_muxs.testDone(1)


class TestMuxs(tu.Muxs):
    exp_seqno = []
    seqno = 0
    ws = None
    send_task = None
    ev = None

    def get_router_config(self):
        if REGION == 'US915':
            conf = tu.router_config_US902_8ch
            conf['bcning'] = {
                'DR': 8,
                'layout': [5,11,23],
                'freqs': [923300000 + chx * 600000 for chx in range(8)]
            }
        else:
            conf = tu.router_config_KR920
            conf['bcning'] = {
                'DR': 3,
                'layout': [2,8,17],
                'freqs': [923100000]
            }
        return conf

    async def handle_connection(self, ws):
        self.ws = ws
        self.ev = asyncio.Event()
        #XXX:old: self.send_task = asyncio.ensure_future(self.send_classC())
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


#XXX:old:    # airtime: dr=4 (SF8) plen=12  <83ms
#XXX:old:    def make_dnmsgC(self, rx2dr=4, rx2freq=FREQ1, plen=12):
#XXX:old:        dnmsg = {
#XXX:old:            'msgtype' : 'dnmsg',
#XXX:old:            'dC'      : 2,          # device class C
#XXX:old:            'dnmode'  : 'dn',
#XXX:old:            'priority': 0,
#XXX:old:            'RX2DR'   : rx2dr,
#XXX:old:            'RX2Freq' : int(rx2freq*1e6),
#XXX:old:            'DevEui'  : '00-00-00-00-11-00-00-01',
#XXX:old:            #'xtime'  : 0,  # not required
#XXX:old:            'seqno'   : self.seqno,
#XXX:old:            'MuxTime' : time.time(),
#XXX:old:            'rctx'    : 0,                   # antenna#0
#XXX:old:            'pdu'     : bytes(range(plen)).hex(),
#XXX:old:        }
#XXX:old:        self.seqno += 1
#XXX:old:        return dnmsg
#XXX:old:
#XXX:old:    async def send_classC(self):
#XXX:old:        try:
#XXX:old:            await asyncio.sleep(1.0)
#XXX:old:            assert self.seqno & 1 == 0
#XXX:old:
#XXX:old:            for f in (FREQ1,FREQ2,FREQ3,FREQ2):
#XXX:old:                dnmsg = self.make_dnmsgC(rx2freq=f)
#XXX:old:                if f != FREQ2:
#XXX:old:                    self.exp_seqno.append(dnmsg['seqno'])
#XXX:old:                    sim.exp_txfreq.append(dnmsg['RX2Freq'])
#XXX:old:                await self.ws.send(json.dumps(dnmsg))
#XXX:old:
#XXX:old:            while self.exp_seqno:
#XXX:old:                self.ev.clear()
#XXX:old:                await asyncio.wait_for(self.ev.wait(), 5.0)
#XXX:old:
#XXX:old:            await asyncio.sleep(2.0)
#XXX:old:            assert sim.exp_txfreq == []
#XXX:old:            await self.testDone(0)
#XXX:old:        except asyncio.CancelledError:
#XXX:old:            logger.debug('send_classC canceled.')
#XXX:old:        except Exception as exc:
#XXX:old:            logger.error('send_classC failed: %s', exc, exc_info=True)
#XXX:old:            await self.testDone(1)

if 'PPSTHRES' in os.environ:
    PPSTHRES = int(os.environ['PPSTHRES'])

with open("tc.uri","w") as f:
    f.write('ws://localhost:6038')

async def test_start():
    global station, infos, muxs, sim
    infos = tu.Infos()
    muxs = TestMuxs()
    sim = TestLgwSimServer()
    sim.test_muxs = muxs

    await infos.start_server()
    await muxs.start_server()
    await sim.start_server()

    # 'valgrind', '--leak-check=full',
    station_args = ['station','-p', '--temp', '.']
    station = await subprocess.create_subprocess_exec(*station_args)

tstu.setup_logging()

asyncio.ensure_future(test_start())
asyncio.get_event_loop().run_forever()
