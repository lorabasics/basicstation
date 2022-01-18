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
logger = logging.getLogger('test4-cups')

import tcutils as tu
import simutils as su
import testutils as tstu


restarts = 0
station = None
infos = None
muxs = None
cups = None
sim = None


class TestLgwSimServer(su.LgwSimServer):
    fcnt = 0
    updf_task = None

    def __init__(self) -> None:
        super().__init__(path='_shome/spidev')

    async def on_connected(self, lgwsim:su.LgwSim) -> None:
        self.fcnt = 0
        self.updf_task = asyncio.ensure_future(self.send_updf())

    async def on_close(self):
        self.updf_task.cancel()
        self.updf_task = None
        logger.debug('LGWSIM - close')

    async def send_updf(self) -> None:
        try:
            await asyncio.sleep(3)
            while True:
                logger.debug('LGWSIM - UPDF FCnt=%d' % (self.fcnt,))
                if self.fcnt < 5:
                    # First we're sending on 10% band and expect a reply for every frame
                    freq = 869.525
                    port = 1
                elif self.fcnt < 10:
                    # Send on a .1% band - only 1st reply, other blocked by DC
                    freq = 867.100
                    port = 2
                else:
                    freq = 869.525
                    port = 3 if cups.qcnt >= 2 else 4  # signal termination
                if 0 not in self.units:
                    return
                lgwsim = self.units[0]
                await lgwsim.send_rx(rps=(7,125), freq=freq, frame=su.makeDF(fcnt=self.fcnt, port=port))
                self.fcnt += 1
                await asyncio.sleep(1.0)
        except asyncio.CancelledError as err:
            logger.warning('send_updf task canceled.')
        except Exception as exc:
            logger.error('send_updf failed!', exc_info=True)

class TestMuxs(tu.Muxs):
    exp_seqno = []
    station_args = ['station','-p', '--temp', './_shome', '-h', './_shome']
    #station_args = ['valgrind', '--leak-check=full', 'station','-p', '--temp', '.']
    restart_station_handle = None

    async def testDone(self, status):
        if self.restart_station_handle:
            self.restart_station_handle.cancel()
            self.restart_station_handle = None
        global station
        if station:
            station.terminate()
            await station.wait()
            station = None
        sys.stdout.flush()
        os._exit(status)

    async def restart_station(self):
        global station
        while True:
            if not station:
                station = await subprocess.create_subprocess_exec(*self.station_args)
            retcode = await station.wait()
            logger.debug('STATION EXIT: code=%d' % (retcode,))
            if not restarts:
                sys.stdout.flush()
                sys.exit(1 if restarts==0 else 0)
            restarts -= 1
            logger.debug('RESTARTING STATION...: code=%d' % (retcode,))
            station = None

    async def handle_connection(self, ws):
        self.exp_seqno = []
        await super().handle_connection(ws)

    async def handle_dntxed(self, ws, msg):
        if [msg['seqno']] != self.exp_seqno[0:1]:
            logger.debug('MUXS DNTXED: %r\nbut expected seqno=%r' % (msg, self.exp_seqno))
            # Lift the reliable downlink requirement for now.
            # Reason: The CUPS connection establishment is blocking. During that time no
            # downlinks can be scheduled which may result in unreliable downlinks.
            # await self.testDone(2)
        logger.debug('MUXS DNTXED: expected %r - ok' % (msg['seqno'],))
        del self.exp_seqno[0]

    async def handle_updf(self, ws, msg):
        fcnt = msg['FCnt']
        logger.debug('MUXS UPDF: rctx=%r Fcnt=%d Freq=%.3fMHz FPort=%d' % (msg['upinfo']['rctx'], fcnt, msg['Freq']/1e6, msg['FPort']))
        port = msg['FPort']
        if port >= 3:
            await self.testDone(0 if port == 3 else 1)
        dnframe = {
            'msgtype': 'dnframe',
            'DR'     : msg['DR'],
            'Freq'   : msg['Freq'],
            'DevEui' : '00-00-00-00-11-00-00-01',
            'xtime'  : msg['upinfo']['xtime']+1000000,
            'seqno'  : fcnt,
            'MuxTime': time.time(),
            'rctx'   : msg['upinfo']['rctx'],
            'pdu'    : '0A0B0C0D0E0F',
        }
        # 6..9 not TXed due to DC limits
        if fcnt <= 5 or fcnt >= 10:
            self.exp_seqno.append(dnframe['seqno'])
        await ws.send(json.dumps(dnframe))


class TestCups(tu.Cups):
    qcnt = 0

    def on_response(self, r_cupsUri:bytes, r_tcUri:bytes, r_cupsCred:bytes, r_tcCred:bytes, r_sig:bytes, r_updbin:bytes) -> bytes:
        logger.debug("cupsUri={}, cupsCred={} ({})".format(r_cupsUri, r_cupsCred[2:6].hex(), r_cupsCred[0:2].hex()))
        logger.debug("tcUri={}, tcCred={} ({})".format(r_tcUri, r_tcCred[2:6].hex(), r_cupsCred[:2].hex()))
        try:
            b = super().on_response(r_cupsUri, r_tcUri, r_cupsCred, r_tcCred, r_sig, r_updbin)
            if b == b'\x00'*14:
                self.qcnt += 1
            return b
        except Exception as exc:
            logger.error('on_response failed: %s', exc, exc_info=True)


tls_mode  = (sys.argv[1:2] == ['tls'])
tls_no_ca = (sys.argv[2:3] == ['no_ca'])

isTLS = 's' if tls_mode else ''

with open("_shome/cups.uri","w") as f:
    f.write('http://localhost:%d' % (6041 if isTLS else 6040,))
with open("_cups/cups-router-1.cfg","w") as f:
    f.write(
        ('{'
        '"cupsUri": "http%s://localhost:6040",'
        '"tcUri"  : "ws%s://localhost:6038",'
        '"version": "v1"'
        '}') % (isTLS, isTLS))

async def test_start():
    global station, infos, muxs, cups, sim
    sim = TestLgwSimServer()
    infos = tu.Infos(muxsuri = ('ws%s://localhost:6039/router' % isTLS),
                     homedir = '_tc',
                     tlsidentity = ('infos-0' if tls_mode else None),
                     tls_no_ca = tls_no_ca)
    muxs = TestMuxs(homedir = '_tc',
                    tlsidentity = ('muxs-0' if tls_mode else None),
                    tls_no_ca = tls_no_ca)
    cups = TestCups(homedir = '_cups', tcdir = '_tc',
                    tlsidentity = ('cups-0' if tls_mode else None),
                    tls_no_ca = tls_no_ca)
    cups2 = TestCups(homedir = '_cups', tcdir = '_tc', tlsidentity=None)
    cups2.port = 6041
    await sim.start_server()
    await infos.start_server()
    await muxs.start_server()
    await cups.start_server()
    await cups2.start_server()
    await asyncio.sleep(0.3)    # give python some time to start up
    muxs.restart_station_handle = asyncio.ensure_future(muxs.restart_station())


tstu.setup_logging()

asyncio.ensure_future(test_start())
asyncio.get_event_loop().run_forever()
