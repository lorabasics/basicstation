
import asyncio
import slaveutils as su

slave = None

class TestSlave(su.Slave):
    txh = None
    fcnt = 0
    expected_rctx = [0,1,2,3,4,5,10]
    fail = False

    async def slave_eof(self):
        pass

    async def ral_config(self, hwspec, config):
        await super().ral_config(hwspec, config)
        self.txh = asyncio.get_event_loop().call_later(1, self.send_updf)

    async def ral_tx(self, rctx, txpow_eirp, rps, freq, txtime, txdata):
        await super().ral_tx(rctx, txpow_eirp, rps, freq, txtime, txdata)
        rctx >>= 8
        if not self.expected_rctx or rctx != self.expected_rctx[0]:
            print('UNEXPECTED: rctx=%r - expected: %r' % (rctx, self.expected_rctx))
            self.fail = True
        else:
            del self.expected_rctx[0]

    def send_updf(self):
        print('PySlave - UPDF FCnt=%d' % (self.fcnt,))
        if self.fcnt < 5:
            # First we're sending on 10% band and expect a reply for every frame
            freq = 869.525
            port = 1
        elif self.fcnt < 10:
            # Send on a .1% band - only 1st reply, other blocked by DC
            freq = 867.100
            port = 2
        else:
            freq = 869.525
            port = 3 if not self.fail else 4  # signal termination
        self.send_rx(rctx=(self.fcnt<<8), freq=freq, rxdata=su.makeDF(fcnt=self.fcnt, port=port))
        self.fcnt += 1
        self.txh = asyncio.get_event_loop().call_later(1, self.send_updf)


async def test_start():
    global slave
    slave = TestSlave()
    await slave.start_slave()


asyncio.ensure_future(test_start())
asyncio.get_event_loop().run_forever()
