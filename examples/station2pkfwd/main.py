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
import sys
import os
import traceback
import asyncio
import websockets
import logging
import json
import argparse
from urllib.parse import urlparse
from websockets.server import WebSocketServerProtocol as WSSP

import router_config
from router import Router
from id6 import Id6

logger = logging.getLogger('ts2pktfwd')

# All track stations managed by this process. Every web socket connection
# on muxs is forwarded to the router registered here. This map is populated
# at startup time.
routerid2router = {}    # type:Mapping[Id6,Router]

async def add_router(routerid:'Id6', rconfig:Mapping[str,Any], pkfwduri:str) -> Router:
    assert routerid not in routerid2router
    r = Router(routerid, rconfig, pkfwduri)
    await r.start()
    routerid2router[routerid] = r
    return r

async def websocket_send_error(websocket: WSSP, router:Optional[str], message:str) -> None:
    await websocket.send(json.dumps({ 'router': router if router else '0', 'error': message }))


class Infos():
    ''' Simple info server to handle router info requests. '''

    def __init__(self, host:str, port:int, muxs_uri:str) -> None:
        self.host = host
        self.port = port
        self.muxs_uri = muxs_uri
        self.ws_server = None    # type: Optional[websockets.server.WebSocketServer]

    async def start(self):
        #self.ws_server = await websockets.serve(self.accept, host=self.host, port=self.port)
        self.ws_server = await websockets.serve(self.accept, port=self.port)

    def __str__(self):
        return 'Infos'

    async def accept(self, websocket:WSSP, path:str) -> None:
        logger.info('%s: accept: path %s' % (self, path))
        router = None  # type:Optional[str]
        errmsg = None  # type:Optional[str]
        try:
            s = json.loads(await websocket.recv())
            logger.info('%s: read: %s' % (self, s))
            if 'router' not in s:
                errmsg = 'Invalid request data'
            else:
                router = s['router']
                routerid = Id6(router, 'router')
                if routerid not in routerid2router:
                    errmsg = 'Router not provisioned'
                else:
                    logger.info('%s: respond: %s' % (self, self.muxs_uri+'/'+str(routerid)))
                    resp = { 'router': router, 'muxs': 'muxs-::0', 'uri': self.muxs_uri+'/'+str(routerid) }
                    await websocket.send(json.dumps(resp))
                    return
        except asyncio.CancelledError:
            raise
        except Exception as exc:
            errmsg = 'Could not handle request'
            logger.error('%s: server socket failed: %s', self, exc, exc_info=True)
        await websocket_send_error(websocket, router, errmsg)

        resp = { 'error': errmsg }
        await websocket.send(json.dumps(resp))

    async def shutdown(self) -> None:
        ws_server = self.ws_server
        if ws_server:
            self.ws_server = None
            ws_server.close()
            await ws_server.wait_closed()

class Muxs():
    ''' Simple muxs server to accept router connects. '''

    def __init__(self, host:str, port:int) -> None:
        self.host = host
        self.port = port
        self.ws_server = None    # type: Optional[websockets.server.WebSocketServer]

    async def start(self):
        #self.ws_server = await websockets.serve(self.accept, host=self.host, port=self.port)
        self.ws_server = await websockets.serve(self.accept, port=self.port)

    def __str__(self):
        return 'Muxs'

    async def accept(self, websocket:WSSP, path:str) -> None:
        logger.info('%s: accept: %s' % (self, path))
        try:
            s = path[1:]
            routerid = Id6(s, 'router')
            if routerid not in routerid2router:
                await websocket_send_error(websocket, s, 'Router not provisioned')
                return
            r = routerid2router[routerid]
            await r.on_ws_connect(websocket)
        except Exception as exc:
            logger.error('%s: server socket failed: %s', self, exc, exc_info=True)
            return


    async def shutdown(self) -> None:
        ws_server = self.ws_server
        if ws_server:
            self.ws_server = None
            ws_server.close()
            await ws_server.wait_closed()


infos  = None  # type:Optional[Infos]
muxs   = None  # type:Optional[Muxs]


LOG_LEVELS = [ 'ERROR', 'WARNING', 'INFO', 'DEBUG' ]

LOG_STR2LEVEL = {
    'ERROR':   logging.ERROR,
    'WARNING': logging.WARNING,
    'INFO':    logging.INFO,
    'DEBUG':   logging.DEBUG
}

def ap_routerid(s:str) -> Id6:
    return Id6(s,'router')


def handle_exc(exc, exit_code=2):
    osenv = os.environ
    if 'stacktrace' in osenv and osenv['stacktrace']:
        print('Execution failed:')
        print(traceback.format_exc())
    else:
        print('Execution failed: %s' % (exc))
    sys.exit(exit_code)


async def main(args):
    global infos, muxs

    root = logging.getLogger()
    ll = logging.INFO
    if args.loglevel:
        ll = LOG_STR2LEVEL[args.loglevel]
    root.setLevel(ll)
    formatter = logging.Formatter('%(asctime)s - %(name)s - %(levelname)s - %(message)s')
    if args.logfile:
        fh = logging.FileHandler(args.logfile)
        fh.setLevel(logging.DEBUG)
        root.addHandler(fh)
    else:
        ch = logging.StreamHandler(sys.stdout)
        ch.setLevel(logging.DEBUG)
        ch.setFormatter(formatter)
        root.addHandler(ch)

    infosuri = urlparse(args.infosuri)
    infosport = infosuri.port
    infoshost = infosuri.hostname
    muxsuri = ''
    if args.muxsuri:
        muxsuri = urlparse(args.muxsuri)
    else:
        muxsuri = infosuri._replace(netloc="{}:{}".format(infoshost, infosport+2))
    muxsport = muxsuri.port
    muxshost = muxsuri.hostname
    pkfwduri = urlparse(args.pkfwduri)

    logger.info('Connection details: infosuri %s, muxsuri %s, pkfwduri %s' % (infosuri.geturl(), muxsuri.geturl(), pkfwduri.geturl()))

    infos = Infos(infoshost, infosport, muxsuri.geturl())
    muxs = Muxs(muxshost, muxsport)

    await infos.start()
    logger.info('Infos started.')

    await muxs.start()
    logger.info('Muxs started.')

    router_config.ini([ args.confdir ])
    routers = args.routerids if args.routerids else router_config.routerid2config.keys()
    for s in routers:
        routerid = Id6(s, 'router')
        logger.info("Instantiating %s" % (routerid))
        rc = router_config.get_router_config(routerid)
        await add_router(routerid, rc, pkfwduri)


if __name__ == '__main__':  # pragma:nocover
    parser = argparse.ArgumentParser(description='''ts2pkdfwd.''')
    parser.add_argument("--infosuri", type=str, default="ws://localhost:6090", help="Info server base URI.")
    parser.add_argument("--muxsuri", type=str, default=None, help="Mux server base URI, by default Info server port plus 2.")
    parser.add_argument("--pkfwduri", type=str, help="Packet forwarder destination URI.", default="udp://localhost:1680")
    parser.add_argument("--confdir", type=str, help="Directory where to load region and router configuration.", default=".")
    parser.add_argument("--logfile", type=str, help="Log file, by default logged to stdout.", default=None)
    parser.add_argument("--loglevel", type=str, choices=LOG_LEVELS, help="Log level: %s" % LOG_LEVELS, default='INFO')
    parser.add_argument("routerids", type=ap_routerid, nargs='*', help='Router ids', default= None)
    try:
        args = parser.parse_args()
    except Exception as exc:
        handle_exc(exc, 1)

    loop = asyncio.get_event_loop()
    try:
        loop.run_until_complete(main(args))
        asyncio.get_event_loop().run_forever()
    except Exception as ex:
        handle_exc(ex, 2)
    finally:
        loop.close()
