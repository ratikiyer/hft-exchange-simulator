import json
from collections import defaultdict

def write(source, ticker):
    with open(source, "r") as file:
        for line in file:
            try:
                msg = json.loads(line.strip())
            except json.JSONDecodeError:
                continue
            if msg.get("symbol") != ticker:
                continue
            if msg.get("type") not in {"price_level_update", "trade_report"}:
                continue
            else:
                with open(f"{ticker}.txt", "a") as ticker_file:
                    ticker_file.write(line)
                    ticker_file.flush()
                    ticker_file.close()
            # print(f"Processing line {counter}")
            # counter += 1
            # try:
            
write("20250415_113602_iexdata.txt", "AAPL")