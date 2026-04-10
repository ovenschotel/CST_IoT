import socket


HOST = "192.168.1.100"   # <-- AANPASSEN!
PORT = 8888

def main():
    print("=== SmartLock Client ===")

    try:
        # Maak verbinding met MITM laptop
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            print(f"Connecting to MITM at {HOST}:{PORT}...")
            s.connect((HOST, PORT))
            print("Connected to MITM!")

            while True:
                cmd = input("Command (LOCK/UNLOCK/EXIT): ").strip().upper()

                if cmd == "EXIT":
                    print("Exiting...")
                    break

                elif cmd in ["LOCK", "UNLOCK"]:
                    # stuur command naar MITM
                    s.sendall(cmd.encode())
                    print(f"Sent: {cmd}")

                else:
                    print("Unknown command. Use LOCK, UNLOCK or EXIT.")

    except ConnectionRefusedError:
        print("❌ Could not connect to MITM. Check IP and server.")
    except Exception as e:
        print(f"❌ Error: {e}")

if __name__ == "__main__":
    main()