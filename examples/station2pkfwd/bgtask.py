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

from typing import Awaitable, Callable, Generic, List, Optional, Tuple, TypeVar
import asyncio
import time
import copy
import logging

logger = logging.getLogger('ts2pktfwd')

"""Background task process forever watching and processing some work queue"""

T = TypeVar('T')

class BgService():
    def start(self) -> None:
        pass

    def cancel(self) -> None:
        pass

    async def stop(self) -> None:
        pass


class BgTask(BgService, Generic[T]):
    def __init__(self,
                 target:Callable[[T],Optional[Awaitable[None]]],
                 empty_queue:Callable[[],T],
                 name:str="unknown",
                 stop_delay:float=0.3) -> None:
        self.target = target
        self.empty_queue = empty_queue
        self.event = asyncio.Event()
        self.queue = empty_queue()
        assert not self.queue         # need __bool__()
        self.task = None              # type: Optional[asyncio.Future]
        self.name = name
        self.stats_fn = None          # type: Optional[Callable[[str,float,float,int],None]]
        self.pend_stop = None         # type: Optional[asyncio.Event]
        self.stop_delay = stop_delay


    def start (self):
        if not self.task:
            self.task = asyncio.ensure_future(self._run())


    def notify (self) -> None:
        self.event.set()


    def cancel (self) -> None:
        if self.task:
            task = self.task
            self.task = None
            if not task.done():
                task.cancel()


    async def stop (self):
        if not self.task:
            return
        self.event.set()
        if self.pend_stop is None:
            self.pend_stop = asyncio.Event()
        try:
            await asyncio.wait_for(self.pend_stop.wait(), self.stop_delay)
        except asyncio.TimeoutError:
            logger.error('Task %s: Waiting for stop timed out - cancelling!', self.name)
            self.cancel()


    async def _run (self):
        while True:
            try:
                await self.event.wait()
                self.event.clear()
                queue = self.queue
                if not queue:
                    if self.pend_stop:
                        self.pend_stop.set()
                        logger.info("Task '%s' stopped", self.name)
                        return
                    continue
                self.queue = self.empty_queue()
                start = time.time()
                qlen = len(queue)
                coro = self.target(queue)
                if asyncio.iscoroutine(coro):
                    await coro
                stats_fn = self.stats_fn
                if stats_fn:
                    stats_fn(self.name, start, time.time(), qlen)
            except asyncio.CancelledError:
                pass
            except Exception as exc:
                logger.error("Task '%s' failed: %s", self.name, exc, exc_info=True)

