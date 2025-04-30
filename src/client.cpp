// client.cpp

#include "local_exchange.h"      // Exchange, OrderParser
#include "types.h"               // ORDER_ID_LEN, TICKER_LEN
#include <nlohmann/json.hpp>      // JSON parsing
#include <fstream>
#include <vector>
#include <string>
#include <thread>
#include <unordered_map>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <cstring>
#include <arpa/inet.h>

// Performance statistics
#include <mutex>
#include <atomic>
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
#ifdef _WIN32
    return _mkgmtime(&tm);
#else
    return timegm(&tm);
#endif
}

// Convert timestamp string with fractional seconds → nanoseconds since epoch
uint64_t parse_timestamp_ns(const std::string &ts) {
    size_t pos = ts.find('.');
    std::string core = (pos == std::string::npos ? ts : ts.substr(0, pos));
    time_t sec = parse_time(core);
    uint64_t ns = uint64_t(sec) * 1'000'000'000ULL;
    if (pos != std::string::npos) {
        std::string frac = ts.substr(pos + 1, 6);
        if (frac.size() < 6) frac.append(6 - frac.size(), '0');
        ns += std::stoul(frac) * 1000ULL;
    }
    return ns;
}

struct Event { uint64_t ts; std::vector<uint8_t> buf; };

int main() {
    using namespace std::chrono_literals;

    // Statistics containers
    std::mutex stats_mutex;
    std::vector<uint64_t> proc_times_ns;
    std::atomic<uint64_t> total_events{0};

    // 1) Read & pack events grouped by user, track minimum timestamp
    std::ifstream in("../iex_python/all_events_with_users.txt");
    if (!in) {
        std::cerr << "Cannot open events file\n";
        return 1;
    }

    std::unordered_map<int, std::vector<Event>> by_user;
    uint64_t global_min_ts = UINT64_MAX;
    std::string line;

    while (std::getline(in, line)) {
        if (line.empty()) continue;
        auto j = json::parse(line);
        std::string type = j["type"].get<std::string>();
        if (type == "modify") continue;

        uint64_t ts_ns = parse_timestamp_ns(j["timestamp"].get<std::string>());
        global_min_ts = std::min(global_min_ts, ts_ns);
        int uid = j["user_id"].is_null() ? -1 : j["user_id"].get<int>();

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
        buf.insert(buf.end(), reinterpret_cast<uint8_t*>(&net_ts), reinterpret_cast<uint8_t*>(&net_ts) + sizeof(net_ts));
        buf.push_back(msg_type);

        // order_id
        std::string oid = j["order_id"].is_null() ? "" : j["order_id"].get<std::string>();
        uint8_t oid_b[ORDER_ID_LEN] = {0};
        size_t oid_len = std::min(oid.size(), size_t(ORDER_ID_LEN - 1));
        std::memcpy(oid_b, oid.data(), oid_len);
        buf.insert(buf.end(), oid_b, oid_b + ORDER_ID_LEN);

        // ticker
        std::string sym = j["symbol"].get<std::string>();
        uint8_t tkr_b[TICKER_LEN] = {0};
        size_t sym_len = std::min(sym.size(), size_t(TICKER_LEN - 1));
        std::memcpy(tkr_b, sym.data(), sym_len);
        buf.insert(buf.end(), tkr_b, tkr_b + TICKER_LEN);

        if (msg_type == TYPE_LIMIT_BUY || msg_type == TYPE_LIMIT_SELL) {
            uint32_t px = htonl(uint32_t(j["price"].get<double>() * 100));
            uint32_t sz = htonl(j["size"].get<uint32_t>());
            buf.insert(buf.end(), reinterpret_cast<uint8_t*>(&px), reinterpret_cast<uint8_t*>(&px) + 4);
            buf.insert(buf.end(), reinterpret_cast<uint8_t*>(&sz), reinterpret_cast<uint8_t*>(&sz) + 4);
        } else if (msg_type == TYPE_UPDATE) {
            uint32_t px = htonl(uint32_t(j["price"].get<double>() * 100));
            uint32_t sz = htonl(j["size"].get<uint32_t>());
            buf.insert(buf.end(), reinterpret_cast<uint8_t*>(&px), reinterpret_cast<uint8_t*>(&px) + 4);
            buf.insert(buf.end(), reinterpret_cast<uint8_t*>(&sz), reinterpret_cast<uint8_t*>(&sz) + 4);
            buf.push_back(uint8_t(side_ch));
        }

        by_user[uid].push_back({ts_ns, std::move(buf)});
    }

    // sort each user's events
    for (auto &kv : by_user) {
        auto &evs = kv.second;
        std::sort(evs.begin(), evs.end(), [](auto &a, auto &b) { return a.ts < b.ts; });
    }

    // 2) Start exchange
    OrderParser parser;
    logger log("client.log");
    Exchange exch(&log, &parser);
    exch.start();

    // 3) Replay events
    auto sim_start = steady_clock::now();
    std::vector<std::thread> clients;
    for (auto &kv : by_user) {
        int uid = kv.first;
        auto events = kv.second;
        clients.emplace_back([&, uid, events]() {
            for (auto &ev : events) {
                auto target = sim_start + nanoseconds(ev.ts - global_min_ts);
                std::this_thread::sleep_until(target);
                auto start = steady_clock::now();
                exch.on_msg_received(ev.buf.data(), ev.buf.size());
                auto end = steady_clock::now();
                uint64_t dur = duration_cast<nanoseconds>(end - start).count();
                {
                    std::lock_guard<std::mutex> lg(stats_mutex);
                    proc_times_ns.push_back(dur);
                }
                ++total_events;
            }
        });
    }
    for (auto &t : clients) t.join();

    // 4) Tear down
    std::this_thread::sleep_for(50ms);
    exch.stop();

    // 5) Print performance statistics
    {
        std::lock_guard<std::mutex> lg(stats_mutex);
        size_t n = proc_times_ns.size();
        if (n > 0) {
            std::sort(proc_times_ns.begin(), proc_times_ns.end());
            uint64_t sum_ns = std::accumulate(proc_times_ns.begin(), proc_times_ns.end(), uint64_t(0));
            double total_sec = sum_ns / 1e9;
            double avg_us = sum_ns / double(n) / 1e3;
            uint64_t min_us = proc_times_ns.front() / 1e3;
            uint64_t max_us = proc_times_ns.back() / 1e3;
            uint64_t p95_us = proc_times_ns[(n * 95) / 100] / 1e3;
            double tp = n / total_sec;

            std::cout << "\n=== PERFORMANCE STATISTICS ===\n";
            std::cout << "Events processed:  " << n << "\n";
            std::cout << "Total time:        " << total_sec << " s\n";
            std::cout << "Avg per-event:     " << avg_us   << " μs\n";
            std::cout << "Min / Max:         " << min_us   << " μs / " << max_us << " μs\n";
            std::cout << "95th percentile:   " << p95_us   << " μs\n";
            std::cout << "Throughput:        " << tp       << " events/s\n";
        }
    }

    return 0;
}
