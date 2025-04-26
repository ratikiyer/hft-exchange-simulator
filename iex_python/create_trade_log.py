import json
from collections import defaultdict, deque
from datetime import datetime, timedelta

# Heuristic tags
HEUR_LIMIT_ADD         = 'limit_add'
HEUR_CANCEL            = 'cancel'
HEUR_VISIBLE_FILL      = 'visible_fill'
HEUR_HIDDEN_FILL       = 'hidden_fill'
HEUR_LARGE_HIDDEN      = 'large_hidden'
HEUR_LEVEL_CLEARED     = 'level_cleared'
HEUR_CANCEL_NO_TRADE   = 'cancel_no_trade'
HEUR_MODIFY            = 'modify'
HEUR_MULTI_LEVEL_SWEEP = 'multi_level_sweep'

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
        best = max(levels) if side == 'B' and levels else min(levels) if side == 'S' and levels else None
        old_best = self.best[side]
        self.best[side] = best
        return prev, size, old_best, best

class EventReconstructor:
    def __init__(self):
        self.books = {}
        self.events = defaultdict(list)
        self.recent_trades = deque()

    def get_book(self, symbol):
        if symbol not in self.books:
            self.books[symbol] = OrderBook()
        return self.books[symbol]

    def record(self, symbol, side, price, size, typ, hidden, timestamp):
        event = {
            'symbol': symbol,  # <-- Add symbol here
            'side': side,
            'price': price,
            'size': size,
            'type': typ,
            'hidden': hidden,
            'timestamp': timestamp
        }
        self.events[symbol].append(event)

        if typ in [HEUR_VISIBLE_FILL, HEUR_HIDDEN_FILL]:
            self.recent_trades.append((timestamp, symbol, side, price))
            self._detect_sweep(symbol, side, timestamp)

    def _detect_sweep(self, symbol, side, now_ts_str):
        now_ts = datetime.fromisoformat(now_ts_str)
        while self.recent_trades and (now_ts - datetime.fromisoformat(self.recent_trades[0][0]) > timedelta(milliseconds=SWEEP_WINDOW_MS)):
            self.recent_trades.popleft()
        relevant = [t for t in self.recent_trades if t[1] == symbol and t[2] == side]
        prices = [t[3] for t in relevant]
        if len(set(prices)) > 1:
            for ts, sym, sd, pr in relevant:
                self.events[sym].append({
                    'symbol': sym,   # <-- Add symbol
                    'side': sd,
                    'price': pr,
                    'size': None,
                    'type': HEUR_MULTI_LEVEL_SWEEP,
                    'hidden': False,
                    'timestamp': ts
                })

    def _trade_occurred_near(self, symbol, price, ts_str):
        ts = datetime.fromisoformat(ts_str)
        for event in self.events[symbol]:
            if event['type'] in [HEUR_VISIBLE_FILL, HEUR_HIDDEN_FILL] and event['price'] == price:
                event_ts = datetime.fromisoformat(event['timestamp'])
                if abs((event_ts - ts).total_seconds() * 1000) <= TRADE_MATCH_WINDOW_MS:
                    return True
        return False

    def process_price_update(self, msg):
        symbol = msg['symbol']
        side = msg['side']
        price = msg['price']
        size = msg['size']
        ts = msg['timestamp']
        book = self.get_book(symbol)
        prev, new, old_best, new_best = book.update(side, price, size)

        if new > prev:
            self.record(symbol, side, price, new - prev, HEUR_LIMIT_ADD, False, ts)
        elif new < prev:
            if new == 0:
                self.record(symbol, side, price, prev, HEUR_LEVEL_CLEARED, False, ts)
            else:
                self.record(symbol, side, price, prev - new, HEUR_CANCEL, False, ts)

        old_best_size = book.book[side].get(old_best, 0)

        if old_best and new_best and old_best != new_best:
            ts_dt = datetime.fromisoformat(ts)
            recent = [e for e in self.events[symbol]
                      if e['type'] in [HEUR_VISIBLE_FILL, HEUR_HIDDEN_FILL]
                      and e['price'] == old_best
                      and abs(datetime.fromisoformat(e['timestamp']) - ts_dt) <= timedelta(milliseconds=1)]
            if not recent and old_best_size > 0:
                self.record(symbol, side, old_best, old_best_size, HEUR_CANCEL_NO_TRADE, False, ts)

    def process_trade(self, msg):
        symbol = msg['symbol']
        price = msg['price']
        size = msg['size']
        ts = msg['timestamp']
        book = self.get_book(symbol)
        available = book.book['S'].get(price, 0)
        hidden = size > available
        book.book['S'][price] = max(0, available - size)
        heuristic = HEUR_HIDDEN_FILL if hidden else HEUR_VISIBLE_FILL
        self.record(symbol, 'S', price, size, heuristic, hidden, ts)

def main(file_path, max_lines=100000, target_symbol='AAPL', all_symbols=False):
    recon = EventReconstructor()
    with open(file_path, 'r') as f:
        for idx, line in enumerate(f):
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

    if not all_symbols:
        print(f"--- Inferred trade events for {target_symbol} ---")
    else:
        print(f"--- Inferred trade events for all symbols ---")

    file_name = f"{target_symbol}_events.txt" if not all_symbols else "all_events.txt"
    with open(file_name, "w") as f:
        if not all_symbols:
            for event in recon.events.get(target_symbol, []):
                f.write(json.dumps(event) + "\n")
                f.flush()
        else:
            for sym, evlist in recon.events.items():
                for event in evlist:
                    f.write(json.dumps(event) + "\n")
                    f.flush()

    if not all_symbols:
        for event in recon.events.get(target_symbol, []):
            print(event)
    else:
        for sym, evlist in recon.events.items():
            for event in evlist:
                print(event)

# Example usage:
main("20250415_113602_iexdata.txt", all_symbols=True)