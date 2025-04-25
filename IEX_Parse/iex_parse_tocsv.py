import pandas as pd
from decimal import Decimal
from iex_parser import Parser, DEEP_1_0
from tqdm import tqdm
from itertools import islice

DEEP_SAMPLE_DATA_FILE = 'data_feeds_20250416_20250416_IEXTP1_DEEP1.0.pcap.gz'
OUTPUT_CSV            = 'deep_data.csv'
MAX_MESSAGES          = 10_000_000

def main():
    rows = []

    with Parser(DEEP_SAMPLE_DATA_FILE, DEEP_1_0) as reader:
        limited = islice(reader, MAX_MESSAGES)

        it = tqdm(
            limited,
            total=MAX_MESSAGES,
            desc='Parsing DEEP messages',
            unit='msg',
            ncols=80
        )

        for msg in it:
            rec = msg.copy()

            for fld in ('symbol','side','status','reason','price_type',
                        'halt_status','detail','security_event','auction_type','imbalance_side'):
                if fld in rec and isinstance(rec[fld], (bytes, bytearray)):
                    rec[fld] = rec[fld].decode('ascii')

            if isinstance(rec.get('price'), Decimal):
                rec['price'] = float(rec['price'])

            rows.append(rec)

    df = pd.DataFrame(rows)

    df.set_index('timestamp', inplace=True)

    df = df[['type', 'side', 'flags', 'symbol', 'size', 'price']]

    df.to_csv(OUTPUT_CSV, index=True)
    print(f"\nWrote {len(df)} rows to {OUTPUT_CSV}")

if __name__ == '__main__':
    main()