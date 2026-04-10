from bleak import BleakScanner

async def scan():
    devices = await BleakScanner.discover()
    for d in devices:
        print(d.address, d.name)

import asyncio
asyncio.run(scan())