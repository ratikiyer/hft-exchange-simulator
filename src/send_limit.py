# send_limit.py
import socket, struct, time

def send_limit(msg_type, order_id, ticker, price, qty,
               host="127.0.0.1", port=12345):
    ts = int(time.time() * 1e9)
    # !Q = big‐endian uint64, B = uint8
    buf = struct.pack("!QB", ts, msg_type)
    buf += order_id.encode("ascii").ljust(16, b'\x00')
    buf += ticker.encode("ascii").ljust(4, b'\x00')
    buf += struct.pack("!II", price, qty)

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.connect((host, port))
        s.sendall(buf)

if __name__ == "__main__":
    # 0x01 = TYPE_LIMIT_BUY
    send_limit(0x01, "order000000000001", "ABCD", 100, 10)
