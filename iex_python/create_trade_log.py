import json
import uuid
from collections import defaultdict, deque
from datetime import datetime, timedelta

# Heuristic tags
HEUR_LIMIT_ADD = 'limit_add'
HEUR_CANCEL = 'cancel'
HEUR_VISIBLE_FILL = 'visible_fill'
HEUR_HIDDEN_FILL = 'hidden_fill'
HEUR_LARGE_HIDDEN = 'large_hidden'
HEUR_LEVEL_CLEARED = 'level_cleared'
HEUR_CANCEL_NO_TRADE = 'cancel_no_trade'
HEUR_MODIFY = 'modify'
HEUR_MULTI_LEVEL_SWEEP = 'multi_level_sweep'

# Timing windows in milliseconds
SWEEP_WINDOW_MS = 5
TRADE_MATCH_WINDOW_MS = 1

class OrderBook:
    def __init__(self):
        self.book = {'B': defaultdict(int), 'S': defaultdict(int)}
        self.best = {'B': None, 'S': None}

    def update(self, side, price, size):
        prev = self.book[side].get(price, 0)
        self.book[side][price] = size
        levels = [p for p, s in self.book[side].items() if s > 0]
        if side == 'B':
            best = max(levels) if levels else None
        else:
            best = min(levels) if levels else None
        old_best = self.best[side]
        self.best[side] = best
        return prev, size, old_best, best

class EventReconstructor:
    def __init__(self, num_users=10):
        self.books = {}
        self.events = defaultdict(list)
        self.recent_trades = deque()
        self.num_users = num_users
        self.current_user = 0
        # Store tuples: (user_id, size, order_id)
        self.order_book_users = defaultdict(lambda: {'B': defaultdict(deque), 'S': defaultdict(deque)})

    def get_book(self, symbol):
        if symbol not in self.books:
            self.books[symbol] = OrderBook()
        return self.books[symbol]

    def record(self, symbol, side, price, size, typ, hidden, timestamp, user_id=None, order_id=None):
        event = {
            'symbol': symbol,
            'side': side,
            'price': price,
            'size': size,
            'type': typ,
            'hidden': hidden,
            'timestamp': timestamp,
            'user_id': user_id,
            'order_id': order_id
        }
        self.events[symbol].append(event)
        # For sweep detection
        if typ in [HEUR_VISIBLE_FILL, HEUR_HIDDEN_FILL]:
            self.recent_trades.append((timestamp, symbol, side, price))
            self._detect_sweep(symbol, side, timestamp)

    def _detect_sweep(self, symbol, side, now_ts_str):
        now_ts = datetime.fromisoformat(now_ts_str)
        # Remove old trades
        while self.recent_trades and (now_ts - datetime.fromisoformat(self.recent_trades[0][0]) > timedelta(milliseconds=SWEEP_WINDOW_MS)):
            self.recent_trades.popleft()
        relevant = [t for t in self.recent_trades if t[1] == symbol and t[2] == side]
        prices = [t[3] for t in relevant]
        if len(set(prices)) > 1:
            # Multi-level sweep detected
            for ts, sym, sd, pr in relevant:
                self.events[sym].append({
                    'symbol': sym,
                    'side': sd,
                    'price': pr,
                    'size': None,
                    'type': HEUR_MULTI_LEVEL_SWEEP,
                    'hidden': False,
                    'timestamp': ts,
                    'user_id': None,
                    'order_id': None
                })

    def record_limit_add(self, symbol, side, price, size, ts):
        user_id = self.current_user
        self.current_user = (self.current_user + 1) % self.num_users
        order_id = uuid.uuid4().hex
        self.order_book_users[symbol][side][price].append((user_id, size, order_id))
        self.record(symbol, side, price, size, HEUR_LIMIT_ADD, False, ts, user_id, order_id)

    def record_cancel(self, symbol, side, price, size, ts, event_type):
        if self.order_book_users[symbol][side][price]:
            user_id, user_size, order_id = self.order_book_users[symbol][side][price][0]
            if size >= user_size:
                self.order_book_users[symbol][side][price].popleft()
                cancel_size = user_size
            else:
                cancel_size = size
                self.order_book_users[symbol][side][price][0] = (user_id, user_size - size, order_id)
            hidden = False
        else:
            user_id = None
            order_id = None
            cancel_size = size
            hidden = True
        self.record(symbol, side, price, cancel_size, event_type, hidden, ts, user_id, order_id)

    def record_fill(self, symbol, price, size, ts, hidden):
        if hidden:
            # Hidden fills have no associated order_id
            self.record(symbol, 'S', price, size, HEUR_HIDDEN_FILL, True, ts, None, None)
            return
        # Match fills to existing orders on the sell side
        while size > 0 and self.order_book_users[symbol]['S'][price]:
            user_id, user_size, order_id = self.order_book_users[symbol]['S'][price][0]
            if size >= user_size:
                self.order_book_users[symbol]['S'][price].popleft()
                fill_size = user_size
            else:
                fill_size = size
                self.order_book_users[symbol]['S'][price][0] = (user_id, user_size - size, order_id)
            size -= fill_size
            self.record(symbol, 'S', price, fill_size, HEUR_VISIBLE_FILL, False, ts, user_id, order_id)

    def process_price_update(self, msg):
        symbol = msg['symbol']
        side = msg['side']
        price = msg['price']
        size = msg['size']
        ts = msg['timestamp']
        book = self.get_book(symbol)
        prev, new, old_best, new_best = book.update(side, price, size)

        if new > prev:
            self.record_limit_add(symbol, side, price, new - prev, ts)
        elif new < prev:
            if new == 0:
                self.record_cancel(symbol, side, price, prev, ts, HEUR_LEVEL_CLEARED)
            else:
                self.record_cancel(symbol, side, price, prev - new, ts, HEUR_CANCEL)

        old_best_size = book.book[side].get(old_best, 0)
        # Detect cancellations without trades when best price moves
        if old_best and new_best and old_best != new_best:
            ts_dt = datetime.fromisoformat(ts)
            recent = [e for e in self.events[symbol]
                      if e['type'] in [HEUR_VISIBLE_FILL, HEUR_HIDDEN_FILL]
                      and e['price'] == old_best
                      and abs(datetime.fromisoformat(e['timestamp']) - ts_dt) <= timedelta(milliseconds=TRADE_MATCH_WINDOW_MS)]
            if not recent and old_best_size > 0:
                self.record_cancel(symbol, side, old_best, old_best_size, ts, HEUR_CANCEL_NO_TRADE)

    def process_trade(self, msg):
        symbol = msg['symbol']
        price = msg['price']
        size = msg['size']
        ts = msg['timestamp']
        book = self.get_book(symbol)
        available = book.book['S'].get(price, 0)
        hidden = size > available
        book.book['S'][price] = max(0, available - size)
        self.record_fill(symbol, price, size, ts, hidden)

def main(file_path, max_lines=500000, target_symbol='AAPL', all_symbols=False, num_users=10):
    recon = EventReconstructor(num_users=num_users)
    with open(file_path, 'r') as f:
        for idx, line in enumerate(f):
            print("Processing line", idx)
            if idx >= max_lines:
                break
            try:
                msg = json.loads(line)
            except json.JSONDecodeError:
                continue
            if msg.get('symbol') != target_symbol and not all_symbols:
                continue
            if msg['type'] == 'price_level_update':
                recon.process_price_update(msg)
            elif msg['type'] == 'trade':
                recon.process_trade(msg)

    output_file = f"{target_symbol}_events_with_users.txt" if not all_symbols else "all_events_with_users2.txt"
    with open(output_file, 'w') as f:
        if not all_symbols:
            for event in recon.events.get(target_symbol, []):
                f.write(json.dumps(event) + "\n")
        else:
            for evlist in recon.events.values():
                for event in evlist:
                    f.write(json.dumps(event) + "\n")
    print(f"Done! Wrote output to {output_file}")

if __name__ == "__main__":
    # Example usage: adjust path and parameters as needed
    main("20250415_113602_iexdata.txt", all_symbols=True, num_users=50)
