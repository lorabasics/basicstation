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

from typing import Any,Awaitable,Callable,Dict,List,Mapping,Optional
import struct
import asyncio
import datetime
import logging
import json
import base64

import router_config
from id6 import Id6

logger = logging.getLogger('ts2pktfwd')

PKFWD_VER = 2
DFLT_KEEPALIVE_INTVL = 10.0
DFLT_STAT_INTVL      = 6
PUSH_DATA = 0
PUSH_ACK  = 1
PULL_DATA = 2
PULL_RESP = 3
PULL_ACK  = 4
TX_ACK    = 5    # protocol version 2 only

MSGTYPE2NAME = {
    PUSH_DATA : 'PUSH_DATA',
    PUSH_ACK  : 'PUSH_ACK',
    PULL_DATA : 'PULL_DATA',
    PULL_RESP : 'PULL_RESP',
    PULL_ACK  : 'PULL_ACK',
    TX_ACK    : 'TX_ACK',
}


class PkFwdC():
    def __init__(self, pkfwduri:str, routerid:Id6, config:router_config.RouterConfig, on_pull_resp:Any, get_stat:Any) -> None:
        self.host = pkfwduri.hostname
        self.port = pkfwduri.port
        logger.info('PkFwdC: %s %d' % (self.host, self.port))
        self.routerid = routerid
        self.rid = routerid.id
        # the id as reported to the remote packet forarder process
        self.pkfwdgwid = config.get_pktfwd_gateway_ID()   # self.rid & 0x0000FFFFFFFFFFFF
        self.on_pull_resp = on_pull_resp
        self.get_stat = get_stat
        self.keepalive_intvl = DFLT_KEEPALIVE_INTVL
        self.push_data_token = 0
        self.push_data_counter = 0
        self.push_ack_counter = 0
        self.pull_data_task = None
        self.pull_data_token = 0
        self.pull_data_counter = 0
        self.pull_ack_token = 0


    def __str__(self) -> None:
        return 'PkFwdC'


    async def shutdown(self):
        if self.transport:
            try:
                self.transport.close()
            except:
                pass  # pragma:nocover


    async def start(self) -> None:
        loop = asyncio.get_event_loop()
        self.transport, self.protocol = await loop.create_datagram_endpoint(lambda: self, remote_addr=(self.host, self.port))


    async def pause(self) -> None:
        if self.pull_data_task:
            t = self.pull_data_task
            self.pull_data_task = None
            t.cancel()


    async def resume(self) -> None:
        assert self.pull_data_task is None
        self.pull_data_task = asyncio.ensure_future(self.pull_data_task_func())


    def connection_made(self, transport):
        self.transport = transport


    def datagram_received(self, data, addr):
        #logger.info("%s: received packet: %s" % (self, data.hex()))
        pver, token, t = struct.unpack('>BHB', data[0:4])
        if pver != PKFWD_VER:
            logger.info("%s: received invalid packet: %s" % (self, data.hex()))
        if t == PULL_ACK:
            self.on_pull_ack(token)
            return
        if t == PUSH_ACK:
            self.on_push_ack(token)
            return
        if t == PULL_RESP:
            o = json.loads(data[4:].decode())
            logger.info("%s: PULL_RESP: token %d, object %s" % (self, token, o))
            self.on_pull_resp(token, o)
            return
        logger.info("%s: received unknown packet: %s" % (self, data.hex()))


    def error_received(self, exc):
        logger.info("%s: received error: %s" % (self, exc))


    def connection_lost(self, exc):
        logger.error("%s: socket unexpextedly closed" % (self))


    def sendto(self, message:bytes) -> None:
        self.transport.sendto(message)


    def push_rxpk(self, rxtime:float, tmst:int, chan:int, rfch:int, Freq:int, datr:str, rssi:float, snr:float, pdu_ba:bytes) -> None:
        time = datetime.datetime.utcfromtimestamp(rxtime).isoformat() + 'Z'
        pdu_b64 = base64.b64encode(pdu_ba).decode('ascii')
        assert Freq > 100000000
        freq = Freq/1000000.0
        pkt = {
            'rxpk': [{
                "time": time,
                "tmst": tmst,
                "chan": chan,
                "rfch": rfch,
                "freq": freq,
                "stat": 1,
                "modu": "LORA",
                "datr": datr,
                'codr': "4/5",
                "rssi": rssi,
                "lsnr": snr,
                "size": len(pdu_ba),
                "data": pdu_b64
            }]
        }
        logger.info('%s: rxpk: %s' % (self, pkt))
        self.push_data(pkt)


    def push_txack(self, token:int) -> None:
        hdr = struct.pack('>BHBq', PKFWD_VER, token, TX_ACK, self.pkfwdgwid)
        logger.info('%s: TX_ACK: %d %s' % (self, token, hdr))
        self.sendto(hdr)


    def push_data(self, pkt:Any) -> None:
        self.push_data_counter += 1
        self.push_data_token = self.push_data_counter % 65536
        hdr = struct.pack('>BHBq', PKFWD_VER, self.push_data_token, PUSH_DATA, self.pkfwdgwid)
        data = hdr + bytes(json.dumps(pkt), 'utf-8')
        logger.debug('%s: push_data: %s %s' % (self, hdr.hex(), data.hex()))
        self.sendto(data)


    def on_push_ack(self, token:int) -> None:
        self.push_ack_counter += 1
        logger.debug('%s: on_push_ack: %d' % (self, token))


    def pull_data(self) -> None:
        self.pull_data_counter += 1
        self.pull_data_token = self.pull_data_counter % 65536
        ba = struct.pack('>BHBq', PKFWD_VER, self.pull_data_token, PULL_DATA, self.pkfwdgwid)
        self.sendto(ba)


    def on_pull_ack(self, token:int) -> None:
        self.pull_ack_token = token
        logger.debug('%s: on_pull_ack: %d' % (self, token))


    async def pull_data_task_func(self) -> None:
        while True:
            try:
                self.pull_data()
            except asyncio.CancelledError:
                logger.error('%s: pull_data_task_func cancelled.' % (self))
                raise
            except Exception as exc:
                logger.error('%s: pull_data_task_func failed: %s', self, exc, exc_info=True)

            if ((self.pull_data_counter - 1) % DFLT_STAT_INTVL) == 0:
                await asyncio.sleep(0.3)
                stat = self.get_stat()
                pkt = { 'stat': stat }
                pkt['stat']['time'] = datetime.datetime.utcnow().isoformat() + 'Z'
                if self.push_data_counter == 0:
                    pkt['stat']['ackr'] = 0.0
                else:
                    pkt['stat']['ackr'] = round((100*self.push_ack_counter)/self.push_data_counter, 1)
                logger.info('%s: send_stats: %s' % (self, pkt))
                self.push_data(pkt)

            await asyncio.sleep(self.keepalive_intvl)

            if self.pull_ack_token != self.pull_ack_token:
                logger.warn('PULL_DATA/_ACK mismatch, disconnected?')

