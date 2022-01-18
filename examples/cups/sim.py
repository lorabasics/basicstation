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
import signal
import time
import json
import random
import asyncio
from asyncio import subprocess

sys.path.append('../../pysys')
import tcutils as tu
import testutils as tstu
import simutils as su

import logging
logger = logging.getLogger('cups-sim')

station = None
infos = None
muxs = None
cups = None
sim = None


class TestLgwSimServer(su.LgwSimServer):
    fcnt = 0
    updf_task = None

    async def on_connected(self, lgwsim:su.LgwSim) -> None:
        self.fcnt = 0
        self.updf_task = asyncio.ensure_future(self.send_updf())

    async def on_close(self):
        self.updf_task.cancel()
        self.updf_task = None
        logger.debug('x LGWSIM - close')

    async def on_tx(self, lgwsim, pkt):
        logger.debug('< LGWSIM TX - %r', pkt)

    async def send_updf(self) -> None:
        try:
            await asyncio.sleep(1)
            while True:
                freq = tu.router_config_EU863_6ch['upchannels'][random.randint(0,5)][0] / 1e6
                logger.debug('> LGWSIM RX - FCnt=%d Freq=%.3fMHz', self.fcnt, freq)
                if 0 not in self.units:
                    return
                lgwsim = self.units[0]
                await lgwsim.send_rx(rps=(7,125), freq=freq, frame=su.makeDF(fcnt=self.fcnt, port=1))
                self.fcnt += 1
                await asyncio.sleep(5.0)
        except asyncio.CancelledError:
            pass
        except Exception as exc:
            logger.error('send_updf failed!', exc_info=True)

class TestMuxs(tu.Muxs):
    station_args = ['station','-p', '--temp', './shome', '--home', './shome']
    restart_station_handle = None
    restarts = 1

    async def restart_station(self):
        global station
        try:
            while True:
                if not station:
                    station = await subprocess.create_subprocess_exec(*self.station_args)
                retcode = await station.wait()
                logger.debug('STATION EXIT: code=%d' % (retcode,))
                if not self.restarts:
                    sys.stdout.flush()
                    sys.exit(1 if self.restarts==0 else 0)
                self.restarts -= 1
                logger.debug('RESTARTING STATION...: code=%d' % (retcode,))
                station = None
        except Exception as exc:
            logger.error('Error during restart_station: %s', exc, exc_info=True)

    async def handle_connection(self, ws):
        await super().handle_connection(ws)

    async def handle_dntxed(self, ws, msg):
        logger.debug("> MUXS: %r", msg)

    async def handle_updf(self, ws, msg):
        fcnt = msg['FCnt']
        logger.debug("> MUXS: %r", msg)
        port = msg['FPort']
        dnframe = {
            'msgtype': 'dnmsg',
            'dC'     : 0,
            'RX1DR'  : msg['DR'],
            'RxDelay': 1,
            'RX1Freq': msg['Freq'],
            'DevEui' : '00-00-00-00-11-00-00-01',
            'xtime'  : msg['upinfo']['xtime'],
            'diid'   : fcnt,
            'priority': 0,
            'MuxTime': time.time(),
            'rctx'   : msg['upinfo']['rctx'],
            'pdu'    : '0A0B0C0D0E0F',
        }
        logger.debug("< MUXS: %r", dnframe)
        await ws.send(json.dumps(dnframe))

class TestCups(tu.Cups):
    qcnt = 0

    def on_response(self, r_cupsUri:bytes, r_tcUri:bytes, r_cupsCred:bytes, r_tcCred:bytes, r_sig:bytes, r_updbin:bytes) -> bytes:
        try:
            b = super().on_response(r_cupsUri, r_tcUri, r_cupsCred, r_tcCred, r_sig, r_updbin)
            if b == b'\x00'*14:
                self.qcnt += 1
            return b
        except Exception as exc:
            logger.error('on_response failed: %s', exc, exc_info=True)

async def test_start():
    global station, infos, muxs, cups, sim
    if not os.path.isfile("prep.done"):
        logger.warning("Expected file structure not there. Runing prep.sh.")
        prep = await subprocess.create_subprocess_exec("./prep.sh")
        ret = await prep.wait()
        if ret is not 0:
            logger.debug("Prep.sh script exited with error code: %d. Stopping", ret)
            os._exit(1)
    sim = TestLgwSimServer(path='shome/spidev')
    infos = tu.Infos(muxsuri = ('wss://localhost:6039/router'),
                     homedir='tc-0',   tlsidentity = 'infos-0')
    muxs  = TestMuxs(homedir='tc-0',   tlsidentity = 'muxs-0')
    cups0 = TestCups(homedir='cups-0', tlsidentity = 'cups-0')
    cups1 = TestCups(homedir='cups-1', tlsidentity = 'cups-1', tcdir='tc-0')
    cups1.port = 6041
    await sim.start_server()
    await infos.start_server()
    await muxs.start_server()
    await cups0.start_server()
    await cups1.start_server()
    if len(sys.argv) > 1 and sys.argv[1] == "runstation":
        await asyncio.sleep(0.3)    # give python some time to start up
        muxs.restart_station_handle = asyncio.ensure_future(muxs.restart_station())

def sigHandler(signum, frame):
    logger.debug("Exiting.")
    if sim.updf_task:
        sim.updf_task.cancel()
        sim.updf_task = None
    if muxs.restart_station_handle:
        muxs.restart_station_handle.cancel()
        muxs.restart_station_handle = None
    global station
    if station:
        station.terminate()
        station = None
    task.cancel()
    sys.stdout.flush()
    quit()

tstu.setup_logging()
signal.signal(signal.SIGINT, sigHandler)

task = asyncio.ensure_future(test_start())
asyncio.get_event_loop().run_forever()
