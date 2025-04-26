import os
import sys
import json
import datetime
import requests
from tqdm import tqdm
from urllib.parse import unquote

def create_logs_dir():
    logs_dir = "iex_logs"
    os.makedirs(logs_dir, exist_ok=True)
    return logs_dir

def fetch_available_files():
    url = "https://iextrading.com/api/1.0/hist"
    response = requests.get(url)
    if response.status_code != 200:
        raise Exception("Failed to fetch available files list. Endpoint may be down.")
    return json.loads(response.content)

def find_deep_file(files_json, target_date):
    files = []
    for date, entries in files_json.items():
        for entry in entries:
            if entry['feed'] == 'DEEP' and entry['date'] == target_date:
                files.append(entry['link'])
    return files

def download_file(url, output_path):
    response = requests.get(url, stream=True, allow_redirects=True)
    total_size = int(response.headers.get('content-length', 0))

    with open(output_path, 'wb') as f, tqdm(
        desc=output_path,
        total=total_size,
        unit='B',
        unit_scale=True,
        unit_divisor=1024,
    ) as bar:
        for chunk in response.iter_content(chunk_size=1024):
            if chunk:
                f.write(chunk)
                bar.update(len(chunk))

def main():
    year = input("Enter year (e.g., 2020): ")
    month = input("Enter month (1-12): ")
    day = input("Enter day (1-31): ")

    target_date = f"{year}{str(month).zfill(2)}{str(day).zfill(2)}"

    logs_dir = create_logs_dir()

    print(f"Fetching available DEEP files for {target_date}...")
    try:
        files_json = fetch_available_files()
        deep_files = find_deep_file(files_json, target_date)

        if not deep_files:
            print(f"No DEEP files found for {target_date}")
            return

        for file_url in deep_files:
            filename = unquote(file_url.split('/')[-1].split('?')[0]).replace('/', '_')
            output_path = os.path.join(logs_dir, filename)
            print(f"Downloading {file_url} to {output_path}")
            download_file(file_url, output_path)
            print(f"Downloaded {filename}")

    except Exception as e:
        print(f"Error: {e}")

if __name__ == "__main__":
    main()
