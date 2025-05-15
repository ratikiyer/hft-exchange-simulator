// client.cpp

#include "local_exchange.h"      // Exchange, OrderParser
#include "types.h"               // ORDER_ID_LEN, TICKER_LEN
#include <nlohmann/json.hpp>     // JSON parsing
#include <fstream>
#include <vector>
#include <string>
#include <thread>
#include <queue>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <cstring>
#include <arpa/inet.h>
#include <numeric>
#include <iostream>

using json = nlohmann::json;
using namespace std::chrono;

// Helper: host-to-network 64-bit
static inline uint64_t htonll(uint64_t v) {
#if __BYTE_ORDER == __LITTLE_ENDIAN
    return (uint64_t(htonl(uint32_t(v & 0xFFFFFFFF))) << 32)
         |  htonl(uint32_t(v >> 32));
#else
    return v;
#endif
}

// Parse ISO8601 timestamp → seconds since epoch
time_t parse_time(const std::string &s) {
    std::tm tm = {};
    tm.tm_year = std::stoi(s.substr(0,4)) - 1900;
    tm.tm_mon  = std::stoi(s.substr(5,2)) - 1;
    tm.tm_mday = std::stoi(s.substr(8,2));
    tm.tm_hour = std::stoi(s.substr(11,2));
    tm.tm_min  = std::stoi(s.substr(14,2));
    tm.tm_sec  = std::stoi(s.substr(17,2));
#ifndef _WIN32
    return timegm(&tm);
#else
    return _mkgmtime(&tm);
#endif
}

// Convert timestamp string with fractional seconds → nanoseconds since epoch
uint64_t parse_timestamp_ns(const std::string &ts) {
    size_t pos = ts.find('.');
    std::string core = pos == std::string::npos ? ts : ts.substr(0, pos);
    time_t sec = parse_time(core);
    uint64_t ns = uint64_t(sec) * 1'000'000'000ULL;
    if (pos != std::string::npos) {
        std::string frac = ts.substr(pos + 1, 6);
        if (frac.size() < 6) frac.append(6 - frac.size(), '0');
        ns += std::stoul(frac) * 1000ULL;
    }
    return ns;
}

struct Event {
    uint64_t ts;
    std::vector<uint8_t> buf;
};

int main() {
    // 1) Read & pack events into one big vector
    std::ifstream in("../iex_python/all_events_with_users2.txt");
    if (!in) {
        std::cerr << "Cannot open events file\n";
        return 1;
    }

    std::vector<Event> all_events;
    all_events.reserve(1'000'000); // adjust if you know approximate size

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        auto j = json::parse(line);
        std::string type = j["type"].get<std::string>();
        if (type == "modify") continue;

        uint64_t ts_ns = parse_timestamp_ns(j["timestamp"].get<std::string>());

        char side_ch = j["side"].get<std::string>()[0];
        uint8_t msg_type;
        using namespace detail;
        if (type == "limit_add") {
            msg_type = (side_ch == 'B' ? TYPE_LIMIT_BUY : TYPE_LIMIT_SELL);
        } else if (type.find("cancel") != std::string::npos) {
            msg_type = TYPE_CANCEL;
        } else {
            msg_type = TYPE_UPDATE;
        }

        std::vector<uint8_t> buf;
        buf.reserve(8 + 1 + ORDER_ID_LEN + TICKER_LEN + 9);

        uint64_t net_ts = htonll(ts_ns);
        buf.insert(buf.end(),
                   reinterpret_cast<uint8_t*>(&net_ts),
                   reinterpret_cast<uint8_t*>(&net_ts) + sizeof(net_ts));
        buf.push_back(msg_type);

        // order_id
        std::string oid = j["order_id"].is_null() ? "" : j["order_id"].get<std::string>();
        uint8_t oid_b[ORDER_ID_LEN] = {0};
        std::memcpy(oid_b, oid.data(),
                    std::min(oid.size(), size_t(ORDER_ID_LEN - 1)));
        buf.insert(buf.end(), oid_b, oid_b + ORDER_ID_LEN);

        // ticker
        std::string sym = j["symbol"].get<std::string>();
        uint8_t tkr_b[TICKER_LEN] = {0};
        std::memcpy(tkr_b, sym.data(),
                    std::min(sym.size(), size_t(TICKER_LEN - 1)));
        buf.insert(buf.end(), tkr_b, tkr_b + TICKER_LEN);

        // price/size (and side byte for updates)
        if (msg_type == TYPE_LIMIT_BUY || msg_type == TYPE_LIMIT_SELL ||
            msg_type == TYPE_UPDATE) {
            uint32_t px = htonl(uint32_t(j["price"].get<double>() * 100));
            uint32_t sz = htonl(j["size"].get<uint32_t>());
            buf.insert(buf.end(),
                       reinterpret_cast<uint8_t*>(&px),
                       reinterpret_cast<uint8_t*>(&px) + 4);
            buf.insert(buf.end(),
                       reinterpret_cast<uint8_t*>(&sz),
                       reinterpret_cast<uint8_t*>(&sz) + 4);
            if (msg_type == TYPE_UPDATE) {
                buf.push_back(uint8_t(side_ch));
            }
        }

        all_events.push_back({ts_ns, std::move(buf)});
    }

    // 2) Sort globally by timestamp
    std::sort(all_events.begin(), all_events.end(),
              [](auto &a, auto &b){ return a.ts < b.ts; });

    // 3) Prepare shared queue
    std::queue<Event> queue;
    for (auto &ev : all_events) queue.push(std::move(ev));
    all_events.clear();

    std::mutex      queue_mutex;
    std::mutex      stats_mutex;
    std::mutex      exch_mutex;
    std::vector<uint64_t> proc_times_ns;
    proc_times_ns.reserve(queue.size());
    std::atomic<uint64_t> total_events{0};

    // 4) Start exchange
    OrderParser parser;
    logger log("client.log");
    Exchange exch(&log, &parser);
    exch.start();

    // 5) Launch worker pool
    unsigned W = std::thread::hardware_concurrency();
    std::vector<std::thread> workers;
    auto wall_start = steady_clock::now();

    for (unsigned i = 0; i < W; ++i) {
        workers.emplace_back([&]() {
            std::vector<uint64_t> local_times;
            local_times.reserve(10000);

            while (true) {
                // pop one event
                Event ev;
                {
                    std::lock_guard<std::mutex> lk(queue_mutex);
                    if (queue.empty()) break;
                    ev = std::move(queue.front());
                    queue.pop();
                }

                // process
                auto t0 = steady_clock::now();
                {
                    // protect on_msg_received if it's not internally thread-safe
                    std::lock_guard<std::mutex> lk(exch_mutex);
                    exch.on_msg_received(ev.buf.data(), ev.buf.size());
                }
                uint64_t dt = duration_cast<nanoseconds>(steady_clock::now() - t0).count();
                local_times.push_back(dt);
                total_events.fetch_add(1, std::memory_order_relaxed);
            }

            // merge stats
            {
                std::lock_guard<std::mutex> lg(stats_mutex);
                proc_times_ns.insert(proc_times_ns.end(),
                                     local_times.begin(), local_times.end());
            }
        });
    }

    // 6) Join & tear down
    for (auto &t : workers) t.join();
    std::this_thread::sleep_for(50ms);
    exch.stop();
    auto wall_end = steady_clock::now();

    // 7) Print stats
    {
        std::lock_guard<std::mutex> lg(stats_mutex);
        size_t n = proc_times_ns.size();
        if (n) {
            std::sort(proc_times_ns.begin(), proc_times_ns.end());
            uint64_t sum_ns = std::accumulate(proc_times_ns.begin(),
                                              proc_times_ns.end(), uint64_t(0));
            double wall_sec = duration_cast<duration<double>>(wall_end - wall_start).count();
            double avg_us   = sum_ns / double(n) / 1e3;
            uint64_t min_us = proc_times_ns.front() / 1e3;
            uint64_t max_us = proc_times_ns.back()  / 1e3;
            uint64_t p95_us = proc_times_ns[(n * 95) / 100] / 1e3;
            double tp       = n / wall_sec;

            std::cout << "\n=== PERFORMANCE STATISTICS ===\n"
                      << "Events processed:     " << n        << "\n"
                      << "Wall-clock time:      " << wall_sec << " s\n"
                      << "Avg per-event:        " << avg_us   << " μs\n"
                      << "Min / Max:            " << min_us   << " μs / " << max_us << " μs\n"
                      << "95th percentile:      " << p95_us   << " μs\n"
                      << "Throughput:           " << tp       << " events/s\n";
        }
    }

    return 0;
}