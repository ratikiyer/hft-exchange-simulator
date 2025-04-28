// client.cpp

#include "local_exchange.h"      // Exchange, OrderParser
#include "types.h"               // ORDER_ID_LEN, TICKER_LEN
#include <nlohmann/json.hpp>      // single-header from single_include/nlohmann/json.hpp
#include <fstream>
#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <unordered_map>
#include <chrono>
#include <ctime>
#include <cstring>
#include <arpa/inet.h>

using json = nlohmann::json;

// Helper: host-to-network 64-bit
static inline uint64_t htonll(uint64_t v) {
#if __BYTE_ORDER == __LITTLE_ENDIAN
    return (uint64_t(htonl(uint32_t(v & 0xFFFFFFFF))) << 32)
         |  htonl(uint32_t(v >> 32));
#else
    return v;
#endif
}

// Parse ISO8601 timestamp → nanoseconds since epoch
uint64_t parse_timestamp_ns(const std::string &ts) {
    std::tm tm = {};
    tm.tm_year = std::stoi(ts.substr(0,4)) - 1900;
    tm.tm_mon  = std::stoi(ts.substr(5,2)) - 1;
    tm.tm_mday = std::stoi(ts.substr(8,2));
    tm.tm_hour = std::stoi(ts.substr(11,2));
    tm.tm_min  = std::stoi(ts.substr(14,2));
    tm.tm_sec  = std::stoi(ts.substr(17,2));
    uint64_t frac_ns = 0;
    if (ts.size()>20 && ts[19]=='.') {
        std::string usec = ts.substr(20,6);
        if (usec.size()<6) usec.append(6-usec.size(),'0');
        frac_ns = std::stoul(usec) * 1000ULL;
    }
  #ifdef _WIN32
    time_t sec = _mkgmtime(&tm);
  #else
    time_t sec = timegm(&tm);
  #endif
    return uint64_t(sec)*1000000000ULL + frac_ns;
}

int main() {
    using namespace std::chrono_literals;

    // 1) Read & pack all events, grouping by user_id
    std::ifstream in("../iex_python/all_events_with_users.txt");
    if (!in) {
        std::cerr << "Cannot open events file\n";
        return 1;
    }

    // Map user_id -> list of binary messages
    std::unordered_map<int, std::vector<std::vector<uint8_t>>> by_user;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        auto j = json::parse(line);

        std::string type = j["type"].get<std::string>();
        if (type == "modify") {
            // ignore pure modify heuristics
            continue;
        }

        // timestamp
        uint64_t ts_ns  = parse_timestamp_ns(j["timestamp"].get<std::string>());
        uint64_t net_ts = htonll(ts_ns);

        // user_id (null -> -1)
        int uid = j["user_id"].is_null() ? -1 : j["user_id"].get<int>();

        // msg_type mapping
        char side_ch = j["side"].get<std::string>()[0];  // 'B' or 'S'
        uint8_t msg_type;
        using namespace detail;
        if (type == "limit_add") {
            msg_type = (side_ch=='B' ? TYPE_LIMIT_BUY : TYPE_LIMIT_SELL);
        }
        else if (type.find("cancel") != std::string::npos) {
            msg_type = TYPE_CANCEL;
        }
        else {
            // any other event (visible_fill, hidden_fill, large_hidden, level_cleared, multi_level_sweep)
            msg_type = TYPE_UPDATE;
        }

        // build binary message
        std::vector<uint8_t> buf;
        buf.reserve(8 + 1 + ORDER_ID_LEN + TICKER_LEN + 9);

        // timestamp
        buf.insert(buf.end(),
                   (uint8_t*)&net_ts,
                   (uint8_t*)&net_ts + sizeof(net_ts));
        // type tag
        buf.push_back(msg_type);
        
        // order_id
        std::string oid = j["order_id"].is_null() ? "" : j["order_id"].get<std::string>();
        uint8_t oid_b[ORDER_ID_LEN] = {0};
        std::memcpy(oid_b, oid.data(), std::min(oid.size(), size_t(ORDER_ID_LEN)));
        buf.insert(buf.end(), oid_b, oid_b + ORDER_ID_LEN);

        // ticker
        std::string sym = j["symbol"].get<std::string>();
        uint8_t tkr_b[TICKER_LEN] = {0};
        std::memcpy(tkr_b, sym.data(), std::min(sym.size(), size_t(TICKER_LEN)));
        buf.insert(buf.end(), tkr_b, tkr_b + TICKER_LEN);

        // optional payload
        if (msg_type == TYPE_LIMIT_BUY || msg_type == TYPE_LIMIT_SELL) {
            uint32_t px = htonl(uint32_t(j["price"].get<double>() * 100));
            uint32_t sz = htonl(j["size"].get<uint32_t>());
            buf.insert(buf.end(), (uint8_t*)&px, (uint8_t*)&px + 4);
            buf.insert(buf.end(), (uint8_t*)&sz, (uint8_t*)&sz + 4);
        }
        else if (msg_type == TYPE_UPDATE) {
            uint32_t px = htonl(uint32_t(j["price"].get<double>() * 100));
            uint32_t sz = htonl(j["size"].get<uint32_t>());
            buf.insert(buf.end(), (uint8_t*)&px, (uint8_t*)&px + 4);
            buf.insert(buf.end(), (uint8_t*)&sz, (uint8_t*)&sz + 4);
            buf.push_back(uint8_t(side_ch));
        }

        by_user[uid].push_back(std::move(buf));
    }

    // 2) Set up Exchange
    OrderParser parser;
    logger      log("client.log");
    Exchange    exch(&log, &parser);
    exch.start();

    // 3) Spawn one thread per user_id
    std::vector<std::thread> clients;
    clients.reserve(by_user.size());

    for (auto &kv : by_user) {
        int uid = kv.first;
        auto &bufs = kv.second;

        clients.emplace_back([&exch, uid, &bufs]() {
            for (auto &msg : bufs) {
                std::cout << "[user " << uid 
                          << " | thread " << std::this_thread::get_id() 
                          << "] sending\n";
                exch.on_msg_received(msg.data(), msg.size());
                std::this_thread::sleep_for(1ms);
            }
        });
    }

    for (auto &t : clients) t.join();

    // 4) Tear down
    std::this_thread::sleep_for(50ms);
    exch.stop();

    // 5) Dump exchange log
    std::ifstream log_in("client.log");
    std::cout << "=== EXCHANGE LOG ===\n";
    while (std::getline(log_in, line))
        std::cout << line << "\n";

    return 0;
}
