from iex_parser import Parser, DEEP_1_0
from datetime import datetime
from decimal import Decimal
import json

# Path to your PCAP file
DEEP_SAMPLE_DATA_FILE = '/Users/kevinxu/hft-exchange/iex_python/iex_logs/data_feeds_20220801_20220801_IEXTP1_DEEP1.0.pcap.gz'

# Output file with timestamp
timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')
output_file = f'{timestamp}_iexdata.txt'

# Buffer size before writing to disk
BATCH_SIZE = 1000
buffer = []

# Helper function to recursively clean messages
from decimal import Decimal

def make_json_safe(obj):
    if isinstance(obj, dict):
        return {k: make_json_safe(v) for k, v in obj.items()}
    elif isinstance(obj, list):
        return [make_json_safe(v) for v in obj]
    elif isinstance(obj, bytes):
        try:
            return obj.decode('utf-8')
        except:
            return obj.hex()
    elif isinstance(obj, datetime):
        return obj.isoformat()
    elif isinstance(obj, Decimal):
        return float(obj)  
    else:
        return obj

# Main logic
try:
    with Parser(DEEP_SAMPLE_DATA_FILE, DEEP_1_0) as reader:
        for i, message in enumerate(reader):
            buffer.append(message)

            if len(buffer) >= BATCH_SIZE:
                with open(output_file, 'a') as f:
                    for msg in buffer:
                        safe_msg = make_json_safe(msg)
                        f.write(json.dumps(safe_msg) + '\n')
                print(f'[{i}] Wrote {len(buffer)} messages to {output_file}')
                buffer.clear()

    # Write remaining messages
    if buffer:
        with open(output_file, 'a') as f:
            for msg in buffer:
                safe_msg = make_json_safe(msg)
                f.write(json.dumps(safe_msg) + '\n')
        print(f'[END] Wrote remaining {len(buffer)} messages to {output_file}')

except Exception as e:
    print(f"❌ Error occurred: {e}")