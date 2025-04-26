import json
from collections import defaultdict
import matplotlib.pyplot as plt
import matplotlib.ticker as mtick

filename = "20250415_113602_iexdata.txt"

# Grouping logic: split high-volume letters into finer buckets
def get_bucket(symbol):
    if not symbol or len(symbol) < 1:
        return "Other"
    first = symbol[0].upper()

    if first == "S" and len(symbol) >= 2:
        second = symbol[1].upper()
        if second <= "E":
            return "SA–E"
        elif second <= "N":
            return "SF–N"
        else:
            return "SO–Z"
    elif first == "P" and len(symbol) >= 2:
        second = symbol[1].upper()
        if second <= "E":
            return "PA–E"
        else:
            return "PF–Z"
    elif first == "I" and len(symbol) >= 2:
        second = symbol[1].upper()
        if second <= "E":
            return "IA–E"
        else:
            return "IF–Z"
    elif first == "E" and len(symbol) >= 2:
        second = symbol[1].upper()
        if second <= "E":
            return "EA–E"
        else:
            return "EF–Z"

    # All others stay grouped by first letter
    if first in {"S", "P", "I", "E"}: return None
    return first

# Bucketed volume dictionary
volume_by_bucket = defaultdict(lambda: {"buy": 0, "sell": 0})

counter = 0

with open(filename, "r") as file:
    for line in file:
        print(f"Processing line {counter}")
        counter += 1
        try:
            msg = json.loads(line.strip())
        except json.JSONDecodeError:
            continue  # skip invalid JSON

        if msg.get("type") != "price_level_update":
            continue

        symbol = msg.get("symbol")
        side = msg.get("side")
        size = msg.get("size")

        # Skip incomplete or malformed records
        if not symbol or not isinstance(size, int) or size <= 0:
            continue

        bucket = get_bucket(symbol)
        
        if not bucket: # Skip if bucket is None (e.g., 'S', 'P', 'I', 'E')
            continue
        
        if side == "B":
            volume_by_bucket[bucket]["buy"] += size
        elif side == "S":
            volume_by_bucket[bucket]["sell"] += size

# === Plotting ===
# Remove any stale buckets (e.g. 'S', 'P') with 0 volume
volume_by_bucket = {
    k: v for k, v in volume_by_bucket.items()
    if v["buy"] > 0 or v["sell"] > 0
}

buckets = sorted(volume_by_bucket.keys())
buy_volumes = [volume_by_bucket[b]["buy"] / 1e6 for b in buckets]
sell_volumes = [volume_by_bucket[b]["sell"] / 1e6 for b in buckets]

x = range(len(buckets))
width = 0.35

plt.figure(figsize=(14, 6))
plt.bar(x, buy_volumes, width=width, label='Buy Volume')
plt.bar([i + width for i in x], sell_volumes, width=width, label='Sell Volume')

plt.xlabel("Ticker Bucket")
plt.ylabel("Volume (in millions)")
plt.title("Buy vs Sell Volume Grouped by Refined Ticker Buckets")
plt.xticks([i + width / 2 for i in x], buckets, rotation=30, ha='right')
plt.legend()
plt.tight_layout()
plt.grid(True, linestyle='--', alpha=0.5)
plt.gca().yaxis.set_major_formatter(mtick.FuncFormatter(lambda x, _: f"{x:.0f}M"))

plt.show()