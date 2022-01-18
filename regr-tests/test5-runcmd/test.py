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
logger = logging.getLogger('test5-runcmd')

import tcutils as tu
import simutils as su
import testutils as tstu

station = None
infos = None
muxs = None
sim = None


class TestMuxs(tu.Muxs):
    alarms = 0

    async def testDone(self, status):
        global station
        if station:
            station.terminate()
            await station.wait()
            logger.debug('Station exe code:%d' % (station.returncode,))
            station = None
        logger.debug('Test Done: %d' % (status,))
        os._exit(status)

    def get_router_config(self):
        return {
            **self.router_config,
            'MuxTime': time.time(),
            'nodc': True,
            'nocca': True,
            'nodwell': True,
            }

    async def handle_connection(self, ws):
        asyncio.ensure_future(self.send_test_runcmd(ws))
        await super().handle_connection(ws)


    async def send_test_runcmd(self, ws):
        try:
            await ws.send(json.dumps({
                # 'msgtype' : 'runcmd', # no msgtype
                'arguments': ["1", "2"],
                'superfluous_filed': 1
            }))
            await ws.send(json.dumps({
                'msgtype' : 'unknown_msgtype', # unknown msgtype
                'arguments': ["1", "2"],
                'superfluous_filed': 1
            }))
            await ws.send(json.dumps({
                'msgtype' : 'runcmd',
                # 'command' : 'echo "Inline script - $TEST - $@"', # command missing
                'arguments': ["1", "2"],
                'superfluous_filed': 1
            }))
            await ws.send(json.dumps({
                'msgtype' : 'runcmd',
                'command' : 'echo "DEBUG" > cmd.fifo',
            }))
            runcmd = {
                'msgtype' : 'runcmd',
                'command' : 'echo "Inline script - $TEST - $@"',
                'arguments': ['x11','x22','x33']
            }
            await ws.send(json.dumps(runcmd))
            await asyncio.sleep(1)

            runcmd = {
                'msgtype' : 'runcmd',
                'command' : './cmd.sh',
                'arguments': ['a11','a22','a33']
            }
            await ws.send(json.dumps(runcmd))
            await asyncio.sleep(1)

            runcmd = {
                'msgtype' : 'runcmd',
                'command' : 'cmd2.sh',
                'arguments': ['b11','b22','b33']
            }
            await ws.send(json.dumps(runcmd))
            await asyncio.sleep(4)

            await self.testDone(0 if self.alarms==3 else 1)

        except Exception as exc:
            logger.error('send_test_runcmd exception: %s', exc, exc_info=True)
            raise


    async def handle_alarm(self, ws, msg):
        logger.debug('MUXS alarm: %r' % (msg,))
        self.alarms |= msg.get('id',0)


with open("tc.uri","w") as f:
    f.write('ws://localhost:6038')


async def timeout():
    await asyncio.sleep(20)
    await muxs.testDone(2)


async def test_start():
    global station, infos, muxs
    infos = tu.Infos()
    muxs = TestMuxs()
    sim = su.LgwSimServer()
    await infos.start_server()
    await muxs.start_server()
    await sim.start_server()

    station_args = ['station', '-p', '--temp', '.']
    station = await subprocess.create_subprocess_exec(*station_args)

    asyncio.ensure_future(timeout())

tstu.setup_logging()

asyncio.ensure_future(test_start())
asyncio.get_event_loop().run_forever()
