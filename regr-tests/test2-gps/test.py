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
import asyncio
from asyncio import subprocess

import logging
logger = logging.getLogger('test2-gps')

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
    cmdcnt = 0
    gpscnt = 0
    gpsmove = 0
    gpsnofix = 0

    async def testDone(self, status):
        global station
        if station:
            station.terminate()
            await station.wait()
            station = None
        os._exit(status)

    async def handle_event(self, ws, msg):
        logger.debug('EVENT: %r', msg)

    async def handle_alarm(self, ws, msg):
        logger.debug('ALARM: %r', msg)
        text = msg['text']
        if 'GPS' in text:
            self.gpscnt += 1
            if text.startswith('GPS move'):
                self.gpsmove += 1
            if text.startswith('No GPS fix'):
                self.gpsnofix += 1

        if text.startswith('CMD'):
            self.cmdcnt += 1
        logger.debug('ALARM(%d/%d): %s' % (self.gpscnt, self.cmdcnt, text))


async def timeout():
    await asyncio.sleep(30)
    await muxs.testDone(2)


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
        with open("./cmd.fifo", "wb", 0) as c:
            await asyncio.sleep(1.0)
            for i in range(20):
                logger.debug('Writing GPGGA...')
                fixquality = (i&4)*2  # 4x 0 then 4x 2
                lat = b'00849.8387' if i&1 else b'00848.8387'
                sentence = nmea_cksum(b'GPGGA,165848.000,4714.7671,N,%s,E,%d,9,1.01,480.0,M,48.0,M,0000,0000' % (lat, fixquality))
                f.write(sentence)
                logger.debug('Writing cmd.fifo: %s' % (sentence[:-2].decode('ascii'),))
                c.write(b'{"msgtype":"alarm","text":"CMD test no.%d"}\n' % (i,))
                sentence = nmea_cksum(b'GPGGA,165848.001,,,,,0,00,99.99,,,,,,')
                f.write(sentence)
                logger.debug('Writing cmd.fifo: %s' % (sentence[:-2].decode('ascii'),))
                await asyncio.sleep(1)
    notok = 1
    logger.debug('gpscnt=%d gpsmove=%d gpsnofix=%d cmdcnt=%d' % (muxs.gpscnt, muxs.gpsmove, muxs.gpsnofix, muxs.cmdcnt))
    if muxs.gpscnt >= 1 and muxs.gpsmove >= 1 and muxs.gpsnofix >= 1 and muxs.cmdcnt >= 15:
        notok = 0
    await muxs.testDone(notok)

tstu.setup_logging()

asyncio.ensure_future(test_start())
asyncio.get_event_loop().run_forever()
