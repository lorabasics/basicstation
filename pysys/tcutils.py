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

from typing import Any,Dict,List,Optional,Tuple
import time
import re
import base64
import os
import sys
from datetime import datetime
import struct
import json
import asyncio
import aiohttp
from aiohttp import web
import websockets
import ssl
from zlib import crc32
import logging
from id6 import Id6
import glob

logger = logging.getLogger('_tcutils')

base_regions = {
    "EU863" : {
        'msgtype': 'router_config',
        'region': 'EU868',
        'DRs': [(12, 125, 0),
            (11, 125, 0),
            (10, 125, 0),
            (9, 125, 0),
            (8, 125, 0),
            (7, 125, 0),
            (7, 250, 0),
            (0, 0, 0),
            (-1, 0, 0),
            (-1, 0, 0),
            (-1, 0, 0),
            (-1, 0, 0),
            (-1, 0, 0),
            (-1, 0, 0),
            (-1, 0, 0),
            (-1, 0, 0)],
        'max_eirp': 16.0,
        'protocol': 1,
        'freq_range': [863000000, 870000000]
    },
    "US902": {
        'msgtype': 'router_config',
        'region': 'US915',
        'DRs': [(10, 125, 0),
            (9, 125, 0),
            (8, 125, 0),
            (7, 125, 0),
            (8, 500, 0),
            (-1, 0, 0),
            (-1, 0, 0),
            (-1, 0, 0),
            (12, 500, 1),
            (11, 500, 1),
            (10, 500, 1),
            (9, 500, 1),
            (8, 500, 1),
            (7, 500, 1),
            (-1, 0, 0),
            (-1, 0, 0)],
        'max_eirp': 30.0,
        'protocol': 1,
        'freq_range': [902000000, 928000000]
    }
}
base_regions["KR920"] = {
        'msgtype': 'router_config',
        'region': 'KR920',
        'DRs': base_regions["EU863"]["DRs"],
        'max_eirp': 23.0,
        'protocol': 1,
        'freq_range': [920900000, 923300000],
    }


router_config_EU863_6ch = {
    **base_regions['EU863'],
    'JoinEui': None,
    'NetID': None,
    'bcning': None,
    'config': {},
    'hwspec': 'sx1301/1',
    'sx1301_conf': [{'chan_FSK': {'enable': False},
                     'chan_Lora_std':  {'enable': False},
                     'chan_multiSF_0': {'enable': True, 'if': -375000, 'radio': 0},
                     'chan_multiSF_1': {'enable': True, 'if': -175000, 'radio': 0},
                     'chan_multiSF_2': {'enable': True, 'if': 25000, 'radio': 0},
                     'chan_multiSF_3': {'enable': True, 'if': 375000, 'radio': 0},
                     'chan_multiSF_4': {'enable': True, 'if': -237500, 'radio': 1},
                     'chan_multiSF_5': {'enable': True, 'if': 237500, 'radio': 1},
                     'chan_multiSF_6': {'enable': False},
                     'chan_multiSF_7': {'enable': False},
                     'radio_0': {'enable': True, 'freq': 868475000},
                     'radio_1': {'enable': True, 'freq': 869287500}}],
    'upchannels': [[868100000, 0, 5],
                   [868300000, 0, 5],
                   [868500000, 0, 5],
                   [868850000, 0, 5],
                   [869050000, 0, 5],
                   [869525000, 0, 5]]
}

router_config_US902_8ch = {
    **base_regions['US902'],
    'JoinEui': None,
    'NetID': None,
    'bcning': None,
    'config': {},
    'hwspec': 'sx1301/1',
    'sx1301_conf': [{'chan_FSK': {'enable': False},
                     'chan_Lora_std': {'enable': True, 'if':   300000, 'radio': 0},
                     'chan_multiSF_0': {'enable': True, 'if': -400000, 'radio': 0},
                     'chan_multiSF_1': {'enable': True, 'if': -200000, 'radio': 0},
                     'chan_multiSF_2': {'enable': True, 'if':  0, 'radio': 0},
                     'chan_multiSF_3': {'enable': True, 'if':  200000, 'radio': 0},
                     'chan_multiSF_4': {'enable': True, 'if': -200000, 'radio': 1},
                     'chan_multiSF_5': {'enable': True, 'if':  0, 'radio': 1},
                     'chan_multiSF_6': {'enable': True, 'if':  200000, 'radio': 1},
                     'chan_multiSF_7': {'enable': True, 'if':  400000, 'radio': 1},
                     'radio_0': {'enable': True, 'freq': 902700000},
                     'radio_1': {'enable': True, 'freq': 903300000}}],
    'upchannels': [[902300000, 0, 5],
                   [902500000, 0, 5],
                   [902700000, 0, 5],
                   [902900000, 0, 5],
                   [903100000, 0, 5],
                   [903300000, 0, 5],
                   [903500000, 0, 5],
                   [903700000, 0, 5]]
}


router_config_KR920 = {
    **base_regions['KR920'],
    'JoinEui': None,
    'NetID': None,
    'bcning': None,
    'config': {},
    'hwspec': 'sx1301/1',
    'sx1301_conf': [{'chan_FSK': {'enable': False},
                     'chan_Lora_std': {'enable': False},
                     'chan_multiSF_0': {'enable': True, 'if': -200000, 'radio': 0},
                     'chan_multiSF_1': {'enable': True, 'if': 0, 'radio': 0},
                     'chan_multiSF_2': {'enable': True, 'if': 200000, 'radio': 0},
                     'chan_multiSF_3': {'enable': False},
                     'chan_multiSF_4': {'enable': False},
                     'chan_multiSF_5': {'enable': False},
                     'chan_multiSF_6': {'enable': False},
                     'chan_multiSF_7': {'enable': False},
                     'radio_0': {'enable': True, 'freq': 922300000},
                     'radio_1': {'enable': False, 'freq': 0}}],
    'upchannels': [(922100000, 0, 5),
                   (922300000, 0, 5),
                   (922500000, 0, 5)]
}

GPS_EPOCH=datetime(1980,1,6)
UPC_EPOCH=datetime(1970,1,1)
UTC_GPS_LEAPS=18

class ServerABC:
    def __init__(self, port:int=6000, tlsidentity:Optional[str]=None, tls_no_ca=False):
        self.server = None
        self.ws = None
        self.port = port
        self.tls_no_ca = tls_no_ca
        self.tlsctx = self.make_tlsctx(tlsidentity)

    def make_tlsctx(self, tlsidentity:Optional[str]):
        if tlsidentity is None:
            return {}
        tlsctx = ssl.SSLContext(ssl.PROTOCOL_TLSv1_2)
        tlsctx.load_verify_locations(tlsidentity+'.trust')
        crtfile = tlsidentity+'.crt'
        keyfile = tlsidentity+'.key'
        tlsctx.load_cert_chain(crtfile, keyfile)
        if not self.tls_no_ca:
            tlsctx.verify_mode = ssl.CERT_REQUIRED
        return { 'ssl':tlsctx }

    async def start_server(self):
        self.server = await websockets.serve(self.handle_ws, host='0.0.0.0', port=self.port, **self.tlsctx)

    async def handle_ws(self, ws, path):
        pass


class Infos(ServerABC):
    def __init__(self, muxsuri='ws://localhost:6039/router', tlsidentity:Optional[str]=None, tls_no_ca=False, homedir='.'):
        super().__init__(port=6038, tlsidentity=homedir+'/'+tlsidentity if tlsidentity else None, tls_no_ca=tls_no_ca)
        self.muxsuri = muxsuri
        self.homedir = homedir
        self.tlsidentity = tlsidentity

    async def start_server(self):
        logger.debug("  Starting INFOS (%s/%s) on Port %d (muxsuri=%s)" %(self.homedir, self.tlsidentity or "", self.port, self.muxsuri))
        await super().start_server()

    async def handle_ws(self, ws, path):
        logger.debug('. INFOS connect: %s from %r' % (path, ws.remote_address))
        try:
            while True:
                msg = json.loads(await ws.recv())
                logger.debug('> INFOS: %r' % msg)
                r = msg['router']
                resp = {
                    'router': r,
                    'muxs'  : 'muxs-::0',
                    'uri'   : self.muxsuri,
                }
                resp = self.router_info_response(resp)
                await ws.send(json.dumps(resp))
                logger.debug('< INFOS: %r' % resp)
        except websockets.exceptions.ConnectionClosed as exc:
            if exc.code != 1000:
                logger.error('x INFOS close: code=%d reason=%r', exc.code, exc.reason)
        except Exception as exc:
            logger.error('x INFOS exception: %s', exc, exc_info=True)
            try:
                await ws.close()
            except: pass


    def router_info_response(self, resp):
        return resp


class Muxs(ServerABC):
    def __init__(self, tlsidentity:Optional[str]=None, tls_no_ca=False, homedir='.'):
        super().__init__(port=6039, tlsidentity=homedir+'/'+tlsidentity if tlsidentity else None, tls_no_ca=tls_no_ca)
        self.homedir = homedir
        self.tlsidentity = tlsidentity
        self.router_config = router_config_EU863_6ch

    async def start_server(self):
        logger.debug("  Starting MUXS (%s/%s) on Port %d" %(self.homedir, self.tlsidentity or "", self.port))
        await super().start_server()

    async def handle_ws(self, ws, path):
        logger.debug('. MUXS connect: %s' % (path,))
        if path != '/router':
            await ws.close(1020)
        self.ws = ws
        rconf = self.get_router_config()
        await ws.send(json.dumps(rconf))
        logger.debug('< MUXS: router_config.')
        await asyncio.sleep(0.1)           # give station some time to setup radio/timesync
        await self.handle_connection(ws)

    def get_router_config(self):
        return { **self.router_config, 'MuxTime': time.time() }

    async def handle_binaryData(self, ws, data:bytes) -> None:
        pass

    async def handle_connection(self, ws):
        try:
            while True:
                msgtxt = await ws.recv()
                #print('MUXS raw recv: %r' % (msgtxt,))
                if isinstance(msgtxt, bytes):
                    await self.handle_binaryData(ws, msgtxt)
                    continue
                msg = json.loads(msgtxt)
                msgtype = msg.get('msgtype')
                if msgtype:
                    fn = getattr(self, 'handle_'+msgtype, None)
                    if fn:
                        await fn(ws, msg)
                        continue
                logger.debug('  MUXS: ignored msgtype: %s\n%r' % (msgtype, msg))
        except (asyncio.CancelledError, SystemExit):
            raise
        except websockets.exceptions.ConnectionClosed as exc:
            if exc.code != 1000:
                logger.error('x MUXS close: code=%d reason=%r', exc.code, exc.reason)
        except Exception as exc:
            logger.error('x MUXS exception: %s', exc, exc_info=True)
            try:
                ws.close()
            except: pass

    async def handle_version(self, ws, msg):
        logger.debug('> MUXS: Station Version: %r' % (msg,))

    async def handle_timesync(self, ws, msg):
        logger.debug("> MUXS: %r", msg)
        await asyncio.sleep(0.05)
        reply = {
            'msgtype': 'timesync',
            'gpstime': int(((datetime.utcnow() - GPS_EPOCH).total_seconds() + UTC_GPS_LEAPS)*1e6),
            'txtime' : msg['txtime'],
            'MuxTime': time.time(),
        }
        await asyncio.sleep(0.05)
        logger.debug("< MUXS: %r", reply)
        await ws.send(json.dumps(reply))


class Cups(ServerABC):
    def __init__(self, tlsidentity:Optional[str]=None, tls_no_ca=False, homedir='.', tcdir='.'):
        super().__init__(port=6040, tlsidentity=homedir+"/"+tlsidentity if tlsidentity else None, tls_no_ca=tls_no_ca)
        self.homedir = homedir
        self.tcdir = tcdir
        self.tlsidentity = tlsidentity
        self.app = web.Application()
        for args in [ ('POST', '/update-info', self.handle_update_info), ]:
            self.app.router.add_route(*args)

    async def start_server(self):
        logger.debug("  Starting CUPS (%s/%s) on Port %d" %(self.homedir, self.tlsidentity or "", self.port))
        handler = self.app.make_handler()
        self.server = await self.app.loop.create_server(handler, host='0.0.0.0', port=self.port, **self.tlsctx)


    LEND=b'\\s*\r?\n'
    PEM_REX = re.compile(b'-+BEGIN (?P<key>[^-]+)-+' + LEND +
                         b'(([0-9A-Za-z+/= ]+' + LEND + b')+)' +
                         b'-+END (?P=key)-+' + LEND)

    # Since router and cups compare CRCs it is crucial that input to the CRC process
    # is excatly the same. Therefore, normalize according the rules below.
    #
    # E.g. resilient again pasting or editing one the files
    # and thereby introducing white space triggered changes the CRC.
    def normalizePEM(self, data:bytes, fmt="PEM") -> List[bytes]:
        norm = []
        for pem in Cups.PEM_REX.finditer(data):
            if fmt == "DER":
                out = base64.b64decode(re.sub(Cups.LEND, b'\n', pem.group(2)))
                #out += b'\x00' * (4-len(out)&3)
            else:
                out = re.sub(Cups.LEND, b'\n', pem.group(0))
            norm.append(out)
        return norm

    def rdPEM(self, fn, fmt="PEM"):
        if not os.path.exists(fn):
            return b'\x00'*4
        with open(fn,'rb') as f:
            return self.normalizePEM(f.read(), fmt)[0]

    def rdToken(self, fn):
        if not os.path.exists(fn):
            return b'\x00'*4
        with open(fn,'rb') as f:
            token = f.read().decode('ascii')
            return token.strip().encode('ascii') + b'\r\n'

    def normalizeId (self, id:Any) -> str:
        # For tests use a shorter representation
        # For production use str(Id6(id))
        return str(Id6(id).id)

    def readCupsCred(self, routerid, cupsid, fmt="PEM"):
        return (self.rdPEM('%s/cups.ca' % cupsid, fmt) +
                self.rdPEM('%s/cups-router-%s.crt' % (cupsid,routerid), fmt) +
                self.rdPEM('%s/cups-router-%s.key' % (cupsid,routerid), fmt))

    def readTcCred(self, routerid, fmt="PEM"):
        tcTrust = self.rdPEM('%s/tc-router-%s.trust' % (self.tcdir,routerid), fmt)
        if tcTrust == b'\x00\x00\x00\x00':
            tcTrust = self.rdPEM('%s/tc.ca' % self.tcdir, fmt)
        tcCert = self.rdPEM('%s/tc-router-%s.crt' % (self.tcdir,routerid), fmt)
        if tcCert == b'\x00\x00\x00\x00':
            tcKey = self.rdToken('%s/tc-router-%s.key' % (self.tcdir,routerid))
        else:
            tcKey = self.rdPEM('%s/tc-router-%s.key' % (self.tcdir,routerid), fmt)
        return tcTrust + tcCert + tcKey

    def readRouterConfig(self, id:str) -> Dict[str,Any]:
        with open('%s/cups-router-%s.cfg' % (self.homedir, id) ) as f:
            d = json.loads(f.read())
        version = d.get('version', None)
        fwBin = ''
        if version:
            with open(self.homedir+'/'+version+'.bin', 'rb') as f:
                fwBin = f.read()
            logger.debug('  CUPS: Target version: %s (%s)', version, self.homedir+'/'+version+'.bin')
        else:
            logger.debug('  CUPS: No target version configured for this router. No update.')
        d['fwBin'] = fwBin
        try:
            d['fwSig'] = []
            for sigkey in glob.iglob(self.homedir+'/sig*.key', recursive=True):
                try:
                    with open(sigkey,'rb') as f:
                        key = f.read()
                    crc = crc32(key)
                    logger.debug('  CUPS: Found signing key %s -> CRC %08X' % (sigkey,crc))
                    sigf = self.homedir+'/'+version+'.bin.'+sigkey.split("/")[1][:-4]
                    with open(sigf, 'rb') as f:
                        fwSig = f.read()
                    logger.debug('  CUPS: Found signature %s' % sigf)
                    d['fwSig'].append((crc,fwSig))
                except Exception as ex:
                    logger.error("x CUPS: Failed reading signin key %s: %s", sigkey, esc, exc_info=True)
        except:
            d['fwSig'] = [(b'', b'\x00'*4)]
        d['cupsCred'] = self.readCupsCred(id, d.get('cupsId') or self.homedir, d.get("credfmt", "DER"))
        d['tcCred']   = self.readTcCred(id, d.get("credfmt", "DER"))
        d['cupsCredCrc'] = crc32(d['cupsCred']) & 0xFFFFFFFF
        d['tcCredCrc']   = crc32(d['tcCred'])   & 0xFFFFFFFF
        return d

    def encodeUri(self, key:str, req:Dict[str,Any], cfg:Dict[str,Any]) -> bytes:
        k = key+'Uri'
        if not cfg.get(k) or req[k] == cfg[k]:
            return b'\x00'
        s = cfg[k].encode('ascii')
        return struct.pack('<B', len(s)) + s

    def encodeCred(self, key:str, req:Dict[str,Any], cfg:Dict[str,Any]) -> bytes:
        k = key+'CredCrc'
        if not cfg.get(k) or req[k] == cfg[k]:
            return b'\x00\x00'
        d = cfg[key+'Cred']
        return struct.pack('<H', len(d)) + d

    def encodeFw(self, req:Dict[str,Any], cfg:Dict[str,Any]) -> bytes:
        if not cfg.get('version') or req['version'] == cfg['version']:
            logger.debug('  CUPS: No fw update required')
            return b'\x00\x00\x00\x00'
        fwbin = cfg['fwBin']
        return struct.pack('<I', len(fwbin)) + fwbin

    def encodeSig(self, req:Dict[str,Any], cfg:Dict[str,Any]) -> Tuple[bytes, int]:
        if not cfg.get('version') or req['version'] == cfg['version']:
            return (b'\x00\x00\x00\x00',0)
        sc = req.get('keys')
        if sc is None:
            logger.debug('x CUPS: Request does not contain a signing key CRC!')
            return (b'\x00\x00\x00\x00',0)
        for (c,s) in cfg['fwSig']:
            for scn in sc:
                if c == int(scn):
                    logger.debug('  CUPS: Found matching signing key with CRC %08X', c)
                    return (struct.pack('<II', len(s)+4, c) + s, c)
        logger.debug('x CUPS: Unable to encode matching signature!')
        return (b'\x00'*4,0)

    def on_response(self, r_cupsUri:bytes, r_tcUri:bytes, r_cupsCred:bytes, r_tcCred:bytes, r_sig:bytes, r_fwbin:bytes) -> bytes:
        return r_cupsUri + r_tcUri + r_cupsCred + r_tcCred + r_sig + r_fwbin


    async def handle_update_info(self, request) -> web.Response:
        req = await request.json()
        logger.debug('> CUPS Request: %r' % req)

        routerid  = self.normalizeId(req['router'])
        cfg = self.readRouterConfig(routerid)

        version = req.get('package')
        if not version:
            logger.debug('x CUPS: router %s reported nil/unknown firmware!' % (routerid))
            return web.Response(status=404, text='Nil/unknown firmware')
        req['version'] = version

        cupsCrc   = req['cupsCredCrc']
        tcCrc     = req['tcCredCrc']
        cupsUri   = req['cupsUri']
        tcUri     = req['tcUri']

        r_cupsUri         = self.encodeUri ('cups', req, cfg)
        r_cupsCred        = self.encodeCred('cups', req, cfg)
        r_tcUri           = self.encodeUri ('tc'  , req, cfg)
        r_tcCred          = self.encodeCred('tc'  , req, cfg)
        (r_sig, r_sigCrc) = self.encodeSig(req, cfg)
        r_fwbin           = self.encodeFw(req, cfg)

        logger.debug('< CUPS Response:\n'
              '  cupsUri : %s %s\n'
              '  tcUri   : %s %s\n'
              '  cupsCred: %3d bytes -- %s\n'
              '  tcCred  : %3d bytes -- %s\n'
              '  sigCrc  : %08X\n'
              '  sig     : %3d bytes\n'
              '  fw      : %3d bytes -- %s'
              ,  r_cupsUri[1:], ("<- " if r_cupsUri[1:] else "-- ") + "[%s]" % cupsUri,
                 r_tcUri[1:], ("<- " if r_tcUri[1:] else "-- ") + "[%s]" % tcUri,
                 len(r_cupsCred)-2, ("[%08X] <- " % cfg['cupsCredCrc'] if len(r_cupsCred)-2 else "") + "[%08X]" % (cupsCrc),
                 len(r_tcCred)-2  , ("[%08X] <- " % cfg['tcCredCrc'] if len(r_tcCred)-2 else "") + "[%08X]" % (tcCrc),
                 r_sigCrc,
                 len(r_sig)-4, # includes CRC
                 len(r_fwbin)-4, ("[%s] <- " % cfg.get('version') if len(r_fwbin)-4 else "") + "[%s]" % (req['version']))

        body = self.on_response(r_cupsUri, r_tcUri, r_cupsCred, r_tcCred, r_sig, r_fwbin)
        return web.Response(body=body)

