import asyncio
from bleak import BleakClient, BleakError

ADDRESS = "10:20:BA:0D:02:B6"
CHAR_UUID = "0000abcd-0000-1000-8000-00805f9b34fb"

async def control_lock():
    client = BleakClient(ADDRESS)
    try:
        await client.connect()
        print("Connected to SmartLock!")

        await asyncio.sleep(0.5)  # korte delay voor stabiele connectie

        while True:
            cmd = input("Command (LOCK/UNLOCK/EXIT): ").strip().upper()
            if cmd == "EXIT":
                print("Exiting...")
                break
            elif cmd in ["LOCK", "UNLOCK"]:
                try:
                    await client.write_gatt_char(CHAR_UUID, cmd.encode(), response=True)
                    print(f"Sent: {cmd}")
                except BleakError as e:
                    print(f"Failed to send {cmd}: {e}")
            else:
                print("Unknown command. Use LOCK, UNLOCK or EXIT.")

    except BleakError as e:
        print("BLE error:", e)
    finally:
        await client.disconnect()
        print("Disconnected.")

asyncio.run(control_lock())