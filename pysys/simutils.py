# --- Revised 3-Clause BSD License ---
# Copyright (C) 2016-2019, SEMTECH (International) AG.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without modification,
# are permitted provided that the following conditions are met:
#
#     * Redistributions of source code must retain the above copyright notice,
#       this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright notice,
#       this list of conditions and the following disclaimer in the documentation
#       and/or other materials provided with the distribution.
#     * Neither the name of the copyright holder nor the names of its contributors
#       may be used to endorse or promote products derived from this software
#       without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL SEMTECH BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
# PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
# LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
# OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
# ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

from typing import Any,Dict,List,Tuple
import os
import sys
import asyncio
import socket
import struct
import time
import logging

handler = logging.StreamHandler(sys.stdout)
handler.setLevel(logging.DEBUG)
handler.setFormatter(logging.Formatter('%(asctime)s [%(name).8s:%(levelname).5s] %(message)s'))

logger = logging.getLogger('simutils')
logger.setLevel(logging.DEBUG)
logger.addHandler(handler)

STAT_CRC_OK = 0x10

MOD_LORA = 0x10
MOD_FSK  = 0x20

BW_500KHZ = 0x01
BW_250KHZ = 0x02
BW_125KHZ = 0x03

DR_LORA_SF7  = 0x02
DR_LORA_SF8  = 0x04
DR_LORA_SF9  = 0x08
DR_LORA_SF10 = 0x10
DR_LORA_SF11 = 0x20
DR_LORA_SF12 = 0x40

CR_LORA_4_5 = 0x01
CR_LORA_4_6 = 0x02
CR_LORA_4_7 = 0x03
CR_LORA_4_8 = 0x04

DR_MAP = {
    7:  DR_LORA_SF7 ,
    8:  DR_LORA_SF8 ,
    9:  DR_LORA_SF9 ,
    10: DR_LORA_SF10,
    11: DR_LORA_SF11,
    12: DR_LORA_SF12,
}
BW_MAP = {
    125: BW_125KHZ,
    250: BW_250KHZ,
    500: BW_500KHZ,
}


SIZE_PKT_RX = 300
SIZE_PKT_TX = 288
OFF_PKT_RX_PAYLOAD = 44
OFF_PKT_TX_PAYLOAD = 30
PPM = (1<<32)

MAX_CCA_INFOS  = 10  # keep in sync with lgwsim.c
MAGIC_CCA_FREQ = 0xCCAFCCAF  # ditto

class FrmType(object):
    JREQ = 0x00
    JACC = 0x20
    DAUP = 0x40  # data (unconfirmed) up
    DADN = 0x60  # data (unconfirmed) dn
    DCUP = 0x80  # data confirmed up
    DCDN = 0xA0  # data confirmed dn
    REJN = 0xC0  # rejoin for roaming
    PROP = 0xE0


def makeDF(mhdr=FrmType.DAUP, fctrl=0, fcnt=0, devaddr=1, fopts=b'', port=-1, payload=b'', mic=1):
    b = struct.pack('<BiBH', mhdr, devaddr, fctrl|len(fopts), fcnt) + fopts
    if port >= 0:
        b += struct.pack('B', port) + payload
    b += struct.pack('<i', mic)
    return b


def add_rps(pkt, rps):
    pkt['bandwidth'] = BW_MAP[rps[1]]
    pkt['datarate'] = DR_MAP[rps[0]]


def pack_pkt_rx (pkt:Dict[str,Any], xticks):
    count_us = xticks & 0xFFFFFFFF
    p = pkt.get('payload',b'')
    f = \
    (pkt.get('freq_hz'   ,           0), # central frequency of the IF chain */
     pkt.get('if_chain'  ,           0), # by which IF chain was packet received */
     pkt.get('status'    , STAT_CRC_OK), # status of the received packet */
     pkt.get('count_us'  ,    count_us), # internal concentrator counter for timestamping, 1 microsecond resolution */
     pkt.get('rf_chain'  ,           0), # through which RF chain the packet was received */
     pkt.get('modulation',    MOD_LORA), # modulation used by the packet */
     pkt.get('bandwidth' ,   BW_125KHZ), # modulation bandwidth (LoRa only) */
     pkt.get('datarate'  , DR_LORA_SF7), # RX datarate of the packet (SF for LoRa) */
     pkt.get('coderate'  , CR_LORA_4_5), # error-correcting code of the packet (LoRa only) */
     pkt.get('rssi'      ,       -50.0), # average packet RSSI in dB */
     pkt.get('snr'       ,         9.0), # average packet SNR, in dB (LoRa only) */
     pkt.get('snr_min'   ,         8.7), # minimum packet SNR, in dB (LoRa only) */
     pkt.get('snr_max'   ,         9.3), # maximum packet SNR, in dB (LoRa only) */
     pkt.get('crc'       ,           0), # CRC that was received in the payload */
     pkt.get('size'      ,      len(p)), # payload size in bytes */
    )
    data = struct.pack("@IBBIBBBIBffffHH", *f)
    return data + p + b'\x00' * (SIZE_PKT_RX-OFF_PKT_RX_PAYLOAD-len(p))


def unpack_pkt_tx (data):
    assert len(data) == SIZE_PKT_TX
    fields = \
    ('freq_hz'   , # central frequency of the IF chain */
     'tx_mode'   , # select on what event/time the TX is triggered */
     'count_us'  , # internal concentrator counter for timestamping, 1 microsecond resolution */
     'rf_chain'  , # through which RF chain the packet was received */
     'rf_power'  , # TX power, in dBm */
     'modulation', # modulation used by the packet */
     'bandwidth' , # modulation bandwidth (LoRa only) */
     'datarate'  , # RX datarate of the packet (SF for LoRa) */
     'coderate'  , # error-correcting code of the packet (LoRa only) */
     'invert_pol', # invert signal polarity, for orthogonal downlinks (LoRa only) */
     'f_dev'     , # frequency deviation, in kHz (FSK only) */
     'preamble'  , # set the preamble length, 0 for default */
     'no_crc'    , # if true, do not send a CRC in the packet */
     'no_header' , # if true, enable implicit header mode (LoRa), fixed length (FSK) */
     'size'        # payload size in bytes */
    )
    elems = struct.unpack_from("@IBIBbBBIBBBHBBH", data, 0)
    pkt = dict(zip(fields, elems))
    pkt['payload'] = data[OFF_PKT_TX_PAYLOAD:OFF_PKT_TX_PAYLOAD+pkt['size']]
    return pkt


class LgwSimServer:
    def __init__(self, path:str='spidev') -> None:
        self.path = path
        self.units = {}

    async def start_server(self):
        logger.debug('  LgwSimServer starting...')
        if os.path.exists(self.path):
            os.unlink(self.path)   # avoid "address already in use" if file exists
        self.sock = await asyncio.start_unix_server(self.connected, self.path)

    def close(self):
        for lgwsim in self.units.values():
            lgwsim.close()
        self.units = {}

    async def connected(self, reader, writer) -> None:
        p = await reader.read(SIZE_PKT_TX)
        assert len(p) == SIZE_PKT_TX
        pkt = unpack_pkt_tx(p)
        assert pkt['tx_mode'] == 255
        timeOffset = (pkt['freq_hz']<<32) + pkt['count_us']
        unitIdx = pkt['f_dev']
        lgwsim = self.make_lgwsim(unitIdx, timeOffset, reader, writer)
        logger.debug('  LgwSimServer: SPI device #%d connected (timeOffset=0x%X xticksNow=0x%X)' % (unitIdx, timeOffset, lgwsim.xticks()))
        self.units[unitIdx] = lgwsim
        await self.on_connected(lgwsim)

    def make_lgwsim(self, unitIdx, timeOffset, reader, writer) -> 'LgwSim':
        return LgwSim(self, unitIdx, timeOffset, reader, writer)

    async def on_connected(self, lgwsim:'LgwSim') -> None:
        await lgwsim.on_connected()

    async def on_tx(self, lgwsim, pkt):
        pass


class LgwSim:
    def __init__(self, server, unitIdx:int , timeOffset:int, reader:asyncio.StreamReader, writer:asyncio.StreamWriter) -> None:
        self.unitIdx = unitIdx
        self.server = server
        self.reader = reader
        self.writer = writer
        self.timeOffset = timeOffset
        # self.timeOffset = int(time.monotonic()*1e6) - 0x10200000
        self.read_task = asyncio.ensure_future(self.read_loop())

    def xticks(self) -> int:
        return int(time.monotonic()*1e6) - self.timeOffset

    def xticks2mono(self, xticks:int) -> int:
        return self.timeOffset + xticks

    def mono2xticks(self, mono:int) -> int:
        return mono - self.timeOffset

    def close(self):
        self.writer.close()
        self.writer = None
        self.read = None

    async def read_loop(self):
        try:
            while True:
                p = await self.reader.read(SIZE_PKT_TX)
                if p == b'':  # EOF
                    logger.debug('  LGWSIM(%d) - read EOF' % self.unitIdx)
                    break
                else:
                    pkt = unpack_pkt_tx(p)
                    await self.on_tx(pkt)
        except BrokenPipeError:
            pass
        except ConnectionResetError:
            pass
        except Exception as exc:
            logger.error('  LGWSIM(%d): Exception: %s', self.unitIdx, exc, exc_info=True)
        logger.debug('  LGWSIM(%d): Closing.', self.unitIdx)
        await self.server.on_close()
        self.server.units.pop(self.unitIdx,None)

    async def send_rx(self, rps:Tuple[int,int], freq=869.515, rxtime=None, frame=b''):
        pkt = {
            'freq_hz': int(freq*1e6),
            'payload': frame
        }
        add_rps(pkt, rps)
        p = pack_pkt_rx(pkt, rxtime or self.xticks())
        self.writer.write(p)
        await self.writer.drain()

    async def send_cca(self, cca_infos:List[Tuple[int,int,int]]):
        assert len(cca_infos) < MAX_CCA_INFOS
        cca_infos = cca_infos + [(0,0,0)] * (MAX_CCA_INFOS - len(cca_infos))
        p = (struct.pack("@II", MAGIC_CCA_FREQ, 0) +
             b''.join(struct.pack("@IQQ", int(i[0]*1e6), i[1], i[2])
                      for i in cca_infos))
        p += b'\x00' * (SIZE_PKT_RX - len(p))
        self.writer.write(p)
        await self.writer.drain()

    async def on_connected(self) -> None:
        pass

    async def on_tx(self, pkt):
        await self.server.on_tx(self, pkt)

    async def on_close(self):
        pass


class LgwSimLoopbackSetup:
    def __init__(self) -> None:
        self.router_side = LgwSimServer('spidev.router')
        self.device_side = LgwSimServer('spidev.device')

    async def close(self) -> None:
        self.router_side.close()
        self.router_side = None
        self.device_side.close()
        self.device_side = None

    async def forward(self, src_side, dst_side, src_lgwsim, pkt):
        unitIdx = src_lgwsim.unitIdx
        if unitIdx not in dst_side.units:
            logger.error('Unit %d of %s not yet connected - dropping TX frame', unitIdx, dst_side.path)
            return
        dst_lgwsim = dst_side.units[unitIdx]
        xticks = dst_side.mono2xticks(src_side.xticks2mono(pkt['count_us']))
        dst_lgwsim.writer.write(pack_pkt_rx(pkt, xticks))
        await dst_lgwsim.writer.drain()


    async def on_connected_router(self, lgwsim:'LgwSim') -> None:
        await lgwsim.on_connected()
        lgwsim.on_close = self.close
        lgwsim.on_tx = lambda lgwsim, pkt: self.forward(self.router_side, self.device_side, lgwsim, pkt)


    async def start(self) -> None:
        await self.router_side.start_server()
        await self.device_side.start_server()
