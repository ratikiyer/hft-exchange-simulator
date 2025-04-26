# send_order.py
import sys, socket, struct, time

def send_order(msg_type, order_id, ticker, price, qty,
               host="127.0.0.1", port=12345):
    ts = int(time.time() * 1e9)
    buf = struct.pack("!QB", ts, msg_type)
    buf += order_id.encode("ascii").ljust(16, b'\x00')
    buf += ticker.encode("ascii").ljust(4, b'\x00')
    buf += struct.pack("!II", price, qty)

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.connect((host, port))
        s.sendall(buf)

if __name__ == "__main__":
    if len(sys.argv) != 6:
        print("Usage: python3 send_order.py <msg_type> <order_id> <ticker> <price> <qty>")
        print("  msg_type: 1=LIMIT_BUY, 2=LIMIT_SELL, 3=MARKET_BUY, 4=MARKET_SELL, 6=CANCEL")
        sys.exit(1)

    msg_type = int(sys.argv[1], 0)
    order_id = sys.argv[2]
    ticker   = sys.argv[3]
    price    = int(sys.argv[4])
    qty      = int(sys.argv[5])
    send_order(msg_type, order_id, ticker, price, qty)
