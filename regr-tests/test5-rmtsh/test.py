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
from pprint import pformat
import asyncio
from asyncio import subprocess

import logging
logger = logging.getLogger('test5-rmtsh')

import tcutils as tu
import simutils as su
import testutils as tstu


station = None
infos = None
muxs = None
sim = None


class TestMuxs(tu.Muxs):
    exp_seqno = []
    seqno = 0
    ws = None
    send_task = None
    ev = None
    rmtsh_status = None
    rmtsh_status_ev = asyncio.Event()
    output = b''

    async def handle_connection(self, ws):
        self.ws = ws
        self.ev = asyncio.Event()
        self.send_task = asyncio.ensure_future(self.run_rmtsh())
        await super().handle_connection(ws)

    async def testDone(self, status):
        global station
        if station:
            station.terminate()
            await station.wait()
            station = None
        os._exit(status)

    async def handle_binaryData(self, ws, data):
        if not data:
            return
        rmtsh_idx = data[0]
        logger.debug('RMTSH %d binary data:<<<%s>>>' % (rmtsh_idx, data[1:].decode('utf-8')))
        self.output += data[1:]
        #sys.stdout.write(data.decode('utf-8'))
        #sys.stdout.flush()

    async def handle_rmtsh(self, ws, msg):
        logger.debug('RMTSH: %s' % (pformat(msg),))
        self.rmtsh_status = msg['rmtsh']
        self.rmtsh_status_ev.set()

    async def waitForRmtshStatus(self):
        self.rmtsh_status = None
        self.rmtsh_status_ev.clear()
        await asyncio.wait_for(self.rmtsh_status_ev.wait(), 3.0)
        return self.rmtsh_status

    async def run_rmtsh(self):
        try:
            await asyncio.sleep(1.0)    # let station start up

            dnmsg = {'msgtype':'rmtsh'}
            await self.ws.send(json.dumps(dnmsg))
            await self.waitForRmtshStatus()
            assert (self.rmtsh_status[0]['started'] == False and
                    self.rmtsh_status[1]['started'] == False)

            dnmsg = b'\x00Some stuff'
            await self.ws.send(dnmsg)
            await asyncio.sleep(1.0)

            dnmsg = {'msgtype':'rmtsh','start':0,'user':'Test session'}
            await self.ws.send(json.dumps(dnmsg))
            await self.waitForRmtshStatus()
            assert (self.rmtsh_status[0]['started'] == True and
                    self.rmtsh_status[1]['started'] == False)
            assert  self.rmtsh_status[0]['user'] == 'Test session'

            dnmsg = b'\x00ls\n'
            await self.ws.send(dnmsg)
            await asyncio.sleep(1.0)
            assert b'spidev' in self.output
            self.output = b''

            txt = b'%f' % time.time()
            dnmsg = b'\x00echo "%s" > test.txt; ls\n' % txt
            await self.ws.send(dnmsg)
            await asyncio.sleep(1.0)
            assert b'test.txt' in self.output
            self.output = b''
            with open('test.txt','rb') as f:
                assert f.read() == txt+b'\n'

            dnmsg = {'msgtype':'rmtsh','stop':0}
            await self.ws.send(json.dumps(dnmsg))
            await self.waitForRmtshStatus()
            assert (self.rmtsh_status[0]['started'] == False and
                    self.rmtsh_status[1]['started'] == False)
            assert  self.rmtsh_status[0]['user'] == 'Test session'

            await asyncio.sleep(1.0)

            await self.ws.send(json.dumps({'msgtype':'rmtsh','start':1,'user':'Test2','term':'xterm','foo':2}))
            await self.waitForRmtshStatus()
            assert (self.rmtsh_status[0]['started'] == False and
                    self.rmtsh_status[1]['started'] == True)

            await self.ws.send(b'\x0Fls\n')
            await self.ws.send(b'\x01head -c 1024 /dev/urandom | hd\n')
            await asyncio.sleep(1.0)

            assert (self.rmtsh_status[0]['started'] == False and
                    self.rmtsh_status[1]['started'] == True)

            dnmsg = {'msgtype':'rmtsh','stop':1}
            await self.ws.send(json.dumps(dnmsg))
            await self.waitForRmtshStatus()
            assert (self.rmtsh_status[0]['started'] == False and
                    self.rmtsh_status[1]['started'] == False)
            assert  self.rmtsh_status[0]['user'] == 'Test session'

            await asyncio.sleep(1.0)

            await self.testDone(0)
        except Exception as exc:
            logger.error('run_rmtsh failed: %s', exc, exc_info=True)
            await self.testDone(1)


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
