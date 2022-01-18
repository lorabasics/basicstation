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
logger = logging.getLogger('test2-pps')

import tcutils as tu
import simutils as su
import testutils as tstu

station = None
infos = None
muxs = None
sim = None
timeout = None

def nmea_cksum(b:bytes) -> bytes:
    v = 0
    for bi in b:
        v ^= bi
    return b'$' + b + b'*%02X\r\n' % (v&0xFF)


class TestMuxs(tu.Muxs):
    tscnt = 0
    first = None

    async def testDone(self, status, msg=''):
        global station
        if station:
            station.terminate()
            await station.wait()
            station = None
        if status:
            print(f'TEST FAILED code={status} ({msg})', file=sys.stderr)
        os._exit(status)

    async def handle_timesync(self, ws, msg):
        t = int(time.time()*1e6)
        if not self.first:
            self.first = t
        if t < self.first + 3e6:
            await asyncio.sleep(2.01)
        else:
            if self.tscnt >= 3:
                print('FAILED to fix after %d tries in 2nd volley of timesync messages' % (self.tscnt,))
                await self.testDone(0)
            self.tscnt += 1
        msg['servertime'] = t
        await ws.send(json.dumps(msg))

    async def handle_alarm(self, ws, msg):
        print('ALARM: %r' % (msg,))


async def timeout():
    await asyncio.sleep(50)
    await muxs.testDone(2, 'TIMEOUT')


async def test_start():
    global station, infos, muxs, sim, timeout
    infos = tu.Infos()
    muxs = TestMuxs()
    sim = su.LgwSimServer()
    await infos.start_server()
    await muxs.start_server()
    await sim.start_server()

    station_args = ['station','-p', '--temp', '.']
    station = await subprocess.create_subprocess_exec(*station_args)

    asyncio.ensure_future(timeout())
    with open("./gps.fifo", "wb", 0) as f:
        # Send an NMEA sentence every 1sec
        # These are not used to sync time in any way - they are only indicative of
        # having a fix and being able to produce a PPS signal
        with open("./cmd.fifo", "wb", 0) as c:
            await asyncio.sleep(1.0)
            for i in range(30):
                print('Writing GPGGA...')
                fixquality = (i&4)*2  # 4x 0 then 4x 2
                f.write(nmea_cksum(b'GPGGA,165848.000,4714.7671,N,00849.8387,E,%d,9,1.01,480.0,M,48.0,M,0000,0000' % fixquality))
                print('Writing cmd.fifo...')
                c.write(b'{"msgtype":"alarm","text":"CMD test no.%d"}\n' % (i,))
                await asyncio.sleep(1)
    if muxs.tscnt > 0:
        await muxs.testDone(0)

    print('No 2nd volley of timesync messages')
    await muxs.testDone(1)

tstu.setup_logging()

asyncio.ensure_future(test_start())
asyncio.get_event_loop().run_forever()
