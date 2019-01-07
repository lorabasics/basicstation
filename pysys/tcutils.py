# Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
#
# This file is subject to the terms and conditions defined in file 'LICENSE',
# which is part of this source code package.

from typing import Any,Dict,List,Optional,Tuple
import time
import re
import base64
import os
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

logger = logging.getLogger('test')


router_config_EU863_6ch = {
    'DRs': [[12, 125, 0],
            [11, 125, 0],
            [10, 125, 0],
            [9, 125, 0],
            [8, 125, 0],
            [7, 125, 0],
            [7, 250, 0],
            [0, 0, 0],
            [-1, 0, 0],
            [-1, 0, 0],
            [-1, 0, 0],
            [-1, 0, 0],
            [-1, 0, 0],
            [-1, 0, 0],
            [-1, 0, 0],
            [-1, 0, 0]],
    'JoinEui': None,
    'NetID': None,
    'bcning': None,
    'config': {},
    'nodc': True,
    'freq_range': [863000000, 870000000],
    'hwspec': 'sx1301/1',
    'max_eirp': 16.0,
    'msgtype': 'router_config',
    'protocol': 1,
    'region': 'EU863',
    'regionid': 1002,
    'sx1301_conf': [{'chan_FSK': {'enable': False},
                     'chan_Lora_std': {'enable': False},
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

router_config_KR920 = {
    'DRs': [(12, 125, 0),
            (11, 125, 0),
            (10, 125, 0),
            (9, 125, 0),
            (8, 125, 0),
            (7, 125, 0),
            (-1, 0, 0),
            (-1, 0, 0),
            (-1, 0, 0),
            (-1, 0, 0),
            (-1, 0, 0),
            (-1, 0, 0),
            (-1, 0, 0),
            (-1, 0, 0),
            (-1, 0, 0),
            (-1, 0, 0)],
    'JoinEui': None,
    'NetID': None,
    'bcning': None,
    'config': {},
    'freq_range': [920900000, 923300000],
    'hwspec': 'sx1301/1',
    'max_eirp': 23.0,
    'msgtype': 'router_config',
    'protocol': 1,
    'region': 'KR920',
    'regionid': 8,
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
    def __init__(self, muxsuri='ws://localhost:6039/router', tlsidentity:Optional[str]=None, tls_no_ca=False):
        super().__init__(port=6038, tlsidentity=tlsidentity, tls_no_ca=tls_no_ca)
        print("  INFOS port %d" %(self.port))
        self.muxsuri = muxsuri

    async def handle_ws(self, ws, path):
        print('. INFOS connect: %s from %r' % (path, ws.remote_address))
        try:
            while True:
                msg = json.loads(await ws.recv())
                print('> INFOS: %r' % msg);
                r = msg['router']
                resp = {
                    'router': r,
                    'muxs'  : 'muxs-::0',
                    'uri'   : self.muxsuri,
                }
                resp = self.router_info_response(resp)
                await ws.send(json.dumps(resp))
                print('< INFOS: %r' % resp);
        except websockets.exceptions.ConnectionClosed as exc:
            if exc.code != 1000:
                logger.error('x INFOS close: code=%d reason=%r', exc.code, exc.reason)
        except Exception as exc:
            logger.error('x INFOS exception: %s', exc, exc_info=True)
            try:
                ws.close()
            except: pass


    def router_info_response(self, resp):
        return resp


class Muxs(ServerABC):
    def __init__(self, tlsidentity:Optional[str]=None, tls_no_ca=False):
        super().__init__(port=6039, tlsidentity=tlsidentity, tls_no_ca=tls_no_ca)
        print("  MUXS port %d" %(self.port))
        self.router_config = router_config_EU863_6ch

    async def handle_ws(self, ws, path):
        print('. MUXS connect: %s' % (path,))
        if path != '/router':
            await ws.close(1020)
        rconf = self.get_router_config()
        await ws.send(json.dumps(rconf))
        print('< MUXS: router_config.')
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
                print('> MUXS: %r' % (msg,))
                msgtype = msg.get('msgtype')
                if msgtype:
                    fn = getattr(self, 'handle_'+msgtype, None)
                    if fn:
                        await fn(ws, msg)
                        continue
                print('  MUXS: ignored msgtype: %s\n%r' % (msgtype, msg))
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
        print('> MUXS: Station Version: %r' % (msg,))


class Cups(ServerABC):
    def __init__(self, tlsidentity:Optional[str]=None, tls_no_ca=False):
        super().__init__(port=6040, tlsidentity=tlsidentity, tls_no_ca=tls_no_ca)
        self.app = web.Application()
        print("  CUPS port %d" %(self.port))
        for args in [ ('POST', '/update-info', self.handle_update_info), ]:
            self.app.router.add_route(*args)

    async def start_server(self):
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
            return b''
        with open(fn,'rb') as f:
            return self.normalizePEM(f.read(), fmt)[0]

    def normalizeId (self, id:Any) -> str:
        # For tests use a shorter representation
        # For production use str(Id6(id))
        return str(Id6(id).id)

    def readCupsCred(self, routerid, fmt="PEM"):
        return (self.rdPEM('cups.ca', fmt) +
                self.rdPEM('cups-router-%s.crt' % routerid, fmt) +
                self.rdPEM('cups-router-%s.key' % routerid, fmt))

    def readTcCred(self, routerid, fmt="PEM"):
        return (self.rdPEM('tc.ca', fmt) +
                self.rdPEM('tc-router-%s.crt' % routerid, fmt) +
                self.rdPEM('tc-router-%s.key' % routerid, fmt))

    def readRouterConfig(self, id:str) -> Dict[str,Any]:
        with open('cups-router-%s.cfg' % id) as f:
            d = json.loads(f.read())
        version = d['version']
        with open(version+'.bin', 'rb') as f:
            fwBin = f.read()
        d['fwBin'] = fwBin
        try:
            d['fwSig'] = []
            for sigkey in glob.iglob('sig*.key', recursive=True):
                try:
                    with open(sigkey,'rb') as f:
                        key = f.read()
                    crc = crc32(key)
                    print('Key: %08X %s ' % (crc, sigkey))
                    sigf = version+'.bin.'+sigkey[:-4]
                    print(sigf)
                    with open(sigf, 'rb') as f:
                        fwSig = f.read()
                    print(len(fwSig))
                    d['fwSig'].append((crc,fwSig))
                except Exception as ex:
                    print("Failed to process sign key %s" % sigkey)
                    print(ex)
        except:
            d['fwSig'] = [(b'', b'\x00'*4)]
        d['cupsCred'] = self.readCupsCred(id, d.get("credfmt", "DER"))
        d['tcCred']   = self.readTcCred(id, d.get("credfmt", "DER"))
        d['cupsCredCrc'] = crc32(d['cupsCred']) & 0xFFFFFFFF
        d['tcCredCrc']   = crc32(d['tcCred'])   & 0xFFFFFFFF
        return d

    def encodeUri(self, key:str, req:Dict[str,Any], cfg:Dict[str,Any]) -> bytes:
        k = key+'Uri'
        if req[k] == cfg[k]:
            return b'\x00'
        s = cfg[k].encode('ascii')
        return struct.pack('<B', len(s)) + s

    def encodeCred(self, key:str, req:Dict[str,Any], cfg:Dict[str,Any]) -> bytes:
        k = key+'CredCrc'
        if req[k] == cfg[k]:
            return b'\x00\x00'
        d = cfg[key+'Cred']
        return struct.pack('<H', len(d)) + d

    def encodeFw(self, req:Dict[str,Any], cfg:Dict[str,Any]) -> bytes:
        if req['version'] == cfg['version']:
            return b'\x00\x00\x00\x00'
        fwbin = cfg['fwBin']
        return struct.pack('<I', len(fwbin)) + fwbin

    def encodeSig(self, req:Dict[str,Any], cfg:Dict[str,Any]) -> Tuple[bytes, int]:
        if req['version'] == cfg['version']:
            return (b'\x00'*4,0)
        sc = req.get('fwSigCrc')
        if sc is None:
            print('Request does not have a signature CRC!')
        for (c,s) in cfg['fwSig']:
            if sc is None or c == int(req['fwSigCrc']):
                print(len(s))
                return (struct.pack('<II', len(s)+4, c) + s, c)
        print('Unable to encode matching signature!')
        return (b'\x00'*4,0)

    def on_response(self, r_cupsUri:bytes, r_tcUri:bytes, r_cupsCred:bytes, r_tcCred:bytes, r_sig:bytes, r_fwbin:bytes) -> bytes:
        return r_cupsUri + r_tcUri + r_cupsCred + r_tcCred + r_sig + r_fwbin


    async def handle_update_info(self, request) -> web.Response:
        req = await request.json()

        routerid  = self.normalizeId(req['router'])
        cfg = self.readRouterConfig(routerid)

        version = req.get('package')
        if not version:
            print('router %s reported nil/unknown firmware!' % (routerid))
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
        print(r_sig)
        r_fwbin           = self.encodeFw(req, cfg)

        print('Request from %s:\n'
              '  %r\n'
              'Response:\n'
              '  cupsUri : %r\n'
              '  tcUri   : %r\n'
              '  cupsCred: %d bytes\n'
              '  tcCred  : %d bytes\n'
              '  sigCrc  : %08X\n'
              '  sig     : %d bytes\n'
              '  fw      : %d bytes -- %s\n'
              % (routerid, req,
                 r_cupsUri[1:], r_tcUri[1:],
                 len(r_cupsCred)-2, len(r_tcCred)-2,
                 r_sigCrc,
                 len(r_sig)-4, # includes CRC
                 len(r_fwbin)-4, cfg['version']))

        body = self.on_response(r_cupsUri, r_tcUri, r_cupsCred, r_tcCred, r_sig, r_fwbin)
        return web.Response(body=body)
