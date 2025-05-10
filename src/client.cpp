// client.cpp

#include "local_exchange.h"      // Exchange, OrderParser
#include "types.h"               // ORDER_ID_LEN, TICKER_LEN
#include <nlohmann/json.hpp>     // JSON parsing
#include <fstream>
#include <vector>
#include <string>
#include <thread>
#include <unordered_map>
#include <algorithm>
#include <chrono>
#include <ctime>
<<<<<<< HEAD
#include <cstring>
#include <arpa/inet.h>
=======
#include <cstring> // For memcpy
#include <arpa/inet.h> // For htonl, potentially __BYTE_ORDER related macros
#include <stdexcept> // For std::runtime_error in new timestamp parser

// For __BYTE_ORDER, __LITTLE_ENDIAN (common on Linux/glibc)
#if defined(__linux__) || defined(__GLIBC__)
#include <endian.h>
#endif

// For bswap_64 / _byteswap_uint64
#if defined(__GNUC__) || defined(__clang__)
#include <byteswap.h> // For bswap_64 (often available on Linux)
#define HAS_BSWAP64_INTRINSIC
#elif defined(_MSC_VER)
#include <stdlib.h> // For _byteswap_uint64
#define HAS_BSWAP64_INTRINSIC
#endif


// Performance statistics
>>>>>>> 7869ddf3464af199487394fa355fb37c1aad6dc6
#include <mutex>
#include <atomic>
#include <numeric>
#include <iostream>
#include <vector> // For thread-local storage vector

using json = nlohmann::json;
using namespace std::chrono;

// Helper: host-to-network 64-bit (optimized)
static inline uint64_t htonll_optimized(uint64_t v) {
// __BYTE_ORDER is a common macro, but not universally standard.
// Check for little endian systems. If __BYTE_ORDER is not defined,
// we might fall back or use a runtime check, but compile-time is preferred.
#if defined(__BYTE_ORDER) && __BYTE_ORDER == __LITTLE_ENDIAN || \
    defined(_WIN32) // Windows is typically little-endian
    #if defined(HAS_BSWAP64_INTRINSIC)
        #if defined(__GNUC__) || defined(__clang__)
            return bswap_64(v);
        #elif defined(_MSC_VER)
            return _byteswap_uint64(v);
        #endif
    #else // Fallback to manual swap if no intrinsic
        return (static_cast<uint64_t>(htonl(static_cast<uint32_t>(v & 0xFFFFFFFF))) << 32) |
                htonl(static_cast<uint32_t>(v >> 32));
    #endif
#elif defined(__BYTE_ORDER) && __BYTE_ORDER == __BIG_ENDIAN
    return v; // Big endian system, no swap needed
#else
    // Fallback if byte order unknown at compile time (less ideal)
    // Or default to the manual method assuming it needs swapping for network order
    // This part depends on how strictly one wants to handle unknown endianness.
    // For now, let's assume the original logic if intrinsics are not used.
    uint32_t high_part = htonl(static_cast<uint32_t>(v >> 32));
    uint32_t low_part = htonl(static_cast<uint32_t>(v & 0xFFFFFFFF));
    return (static_cast<uint64_t>(low_part) << 32) | high_part;
    // Correction for original logic:
    // return (static_cast<uint64_t>(htonl(static_cast<uint32_t>(v & 0xFFFFFFFF))) << 32)
    //      |  htonl(static_cast<uint32_t>(v >> 32));
    // The above was correctly handling little-endian. For big-endian it should be 'return v'.
    // If byte order is unknown, a runtime check or the original more verbose conditional on __BYTE_ORDER should be used.
    // For simplicity, if __BYTE_ORDER is not known, we'll stick to the original:
    const int num = 1; // Runtime check if compile-time macros aren't definitive
    if (*reinterpret_cast<const char*>(&num) == 1) { // Little endian
        return (static_cast<uint64_t>(htonl(static_cast<uint32_t>(v & 0xFFFFFFFF))) << 32)
             |  htonl(static_cast<uint32_t>(v >> 32));
    }
    return v; // Assume big endian if not little
#endif
}

<<<<<<< HEAD
struct Event {
    uint64_t ts;
    std::vector<uint8_t> buf;
};

int main() {
    // 1) Read & pack events
=======

// Helper to parse N digits from a character pointer
// Advances the pointer past the parsed digits. Assumes valid format.
static inline int parse_digits(const char*& ptr, int count) {
    int val = 0;
    for (int i = 0; i < count; ++i) {
        if (*ptr < '0' || *ptr > '9') throw std::runtime_error("Timestamp format error: Expected digit");
        val = val * 10 + (*ptr - '0');
        ptr++;
    }
    return val;
}

// Optimized timestamp parser: ISO8601 string -> nanoseconds since epoch
// Format: YYYY-MM-DDTHH:MM:SS[.ffffff...] (fractional part up to 6 digits for microseconds)
static uint64_t parse_timestamp_ns_optimized(const std::string& ts_str) {
    const char* ptr = ts_str.c_str();
    std::tm t{}; // Zero-initialize

    try {
        t.tm_year = parse_digits(ptr, 4) - 1900; // YYYY
        if (*ptr != '-') throw std::runtime_error("Timestamp format error: Expected '-' after year");
        ptr++; 
        t.tm_mon = parse_digits(ptr, 2) - 1;    // MM (0-11)
        if (*ptr != '-') throw std::runtime_error("Timestamp format error: Expected '-' after month");
        ptr++; 
        t.tm_mday = parse_digits(ptr, 2);       // DD
        if (*ptr != 'T') throw std::runtime_error("Timestamp format error: Expected 'T' after day");
        ptr++; 
        t.tm_hour = parse_digits(ptr, 2);       // HH
        if (*ptr != ':') throw std::runtime_error("Timestamp format error: Expected ':' after hour");
        ptr++; 
        t.tm_min = parse_digits(ptr, 2);        // mm
        if (*ptr != ':') throw std::runtime_error("Timestamp format error: Expected ':' after minute");
        ptr++; 
        t.tm_sec = parse_digits(ptr, 2);        // SS
    } catch (const std::runtime_error& e) {
        throw std::runtime_error(std::string("Error parsing date/time part of '") + ts_str + "': " + e.what());
    }
    
    // tm_isdst should be 0 for UTC. Zero-initialization of 't' handles this.
    time_t seconds_since_epoch;
#ifdef _WIN32
    seconds_since_epoch = _mkgmtime(&t);
#else
    seconds_since_epoch = timegm(&t);
#endif

    if (seconds_since_epoch == (time_t)-1) {
        throw std::runtime_error("Failed to convert calendar time to epoch seconds for: " + ts_str);
    }
    
    uint64_t total_ns = static_cast<uint64_t>(seconds_since_epoch) * 1'000'000'000ULL;

    if (*ptr == '.') {
        ptr++; // Skip '.'
        uint32_t fractional_us = 0; 
        int num_frac_digits_processed = 0;
        for (int i = 0; i < 6; ++i) { // Try to read up to 6 digits for microseconds
            if (*ptr >= '0' && *ptr <= '9') {
                fractional_us = fractional_us * 10 + (*ptr - '0');
                ptr++;
                num_frac_digits_processed++;
            } else {
                // Not a digit, or end of string. Pad remaining with zeros.
                for (int j = num_frac_digits_processed; j < 6; ++j) {
                    fractional_us *= 10;
                }
                break; 
            }
        }
        // If loop finished due to 6 digits read, ensure any further padding for less than 6 original digits is done.
        if (num_frac_digits_processed < 6 && num_frac_digits_processed > 0) {
             // This case is covered by the inner loop's break condition.
             // If all 6 iterations completed because there were >=6 digits:
        } else if (num_frac_digits_processed == 0 && *ptr != '\0' && !(*ptr >= '0' && *ptr <= '9')) {
            // Case like "2023-01-01T00:00:00." (dot but no digits) -> fractional_us remains 0, correctly becomes 000000us.
        }


        total_ns += static_cast<uint64_t>(fractional_us) * 1000ULL; // Convert microseconds to nanoseconds
    }
    return total_ns;
}


struct Event { uint64_t ts; std::vector<uint8_t> buf; };

int main() {
    using namespace std::chrono_literals;

    // Statistics containers
    std::vector<uint64_t> all_proc_times_ns; // Main vector to store all times after threads finish
    std::atomic<uint64_t> total_events{0};

    // 1) Read & pack events grouped by user, track minimum timestamp
>>>>>>> 7869ddf3464af199487394fa355fb37c1aad6dc6
    std::ifstream in("../iex_python/all_events_with_users.txt");
    if (!in) {
        std::cerr << "Cannot open events file\n";
        return 1;
    }

    std::unordered_map<int, std::vector<Event>> by_user;
    uint64_t global_min_ts = UINT64_MAX;
    std::string line;
<<<<<<< HEAD
=======

    std::cout << "Reading and parsing events...\n";
    auto read_start_time = steady_clock::now();

>>>>>>> 7869ddf3464af199487394fa355fb37c1aad6dc6
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        try {
            auto j = json::parse(line);
            // Use .at("field").get_ref<const std::string&>() for required string fields
            const auto& type_json_val = j.at("type");
            const std::string& type = type_json_val.get_ref<const std::string&>();

            if (type == "modify") continue;

            const auto& ts_json_val = j.at("timestamp");
            uint64_t ts_ns = parse_timestamp_ns_optimized(ts_json_val.get_ref<const std::string&>());
            global_min_ts = std::min(global_min_ts, ts_ns);
            
            int uid = j.at("user_id").is_null() ? -1 : j.at("user_id").get<int>();

            const auto& side_json_val = j.at("side");
            char side_ch = side_json_val.get_ref<const std::string&>()[0];
            
            uint8_t msg_type;
            // Assuming TYPE_... constants are in global scope or types.h properly includes them
            // Or, if they are in `detail` namespace from `local_exchange.h`
            // using namespace detail; // If types are in detail namespace
            // For now, assuming they are globally accessible after includes.
            // The original code had `using namespace detail;` before this block. Let's assume that's correct.
            // If these constants are in local_exchange.h, then they may be in a namespace like `LocalExchange::detail`
            // For this example, let's imagine they are defined globally or correctly namespaced.
            // This example uses TYPE_LIMIT_BUY directly as if it's defined.
            // If they are in `detail` namespace from `local_exchange.h` (as `using namespace detail;` implies),
            // this should be fine.
             using namespace detail; // From original, for TYPE_ constants

            if (type == "limit_add") {
                msg_type = (side_ch == 'B' ? TYPE_LIMIT_BUY : TYPE_LIMIT_SELL);
            } else if (type.find("cancel") != std::string::npos) {
                msg_type = TYPE_CANCEL;
            } else { // Assuming "update" type
                msg_type = TYPE_UPDATE;
            }

            std::vector<uint8_t> buf;
            // Max possible size: timestamp + type + order_id + ticker + price + size + side_char (for update)
            buf.reserve(sizeof(uint64_t) + sizeof(uint8_t) + ORDER_ID_LEN + TICKER_LEN + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint8_t));

            uint64_t net_ts = htonll_optimized(ts_ns); // Use optimized htonll
            buf.insert(buf.end(), reinterpret_cast<uint8_t*>(&net_ts), reinterpret_cast<uint8_t*>(&net_ts) + sizeof(net_ts));
            buf.push_back(msg_type);

            // order_id
            const auto& oid_json = j.at("order_id");
            std::string oid_str; // Default empty
            if (!oid_json.is_null()) {
                oid_str = oid_json.get<std::string>(); // Copy, as it's conditional
            }
            uint8_t oid_b[ORDER_ID_LEN] = {0}; // Zero-initialize
            size_t oid_len_to_copy = std::min(oid_str.size(), static_cast<size_t>(ORDER_ID_LEN -1)); // original logic for null term
            std::memcpy(oid_b, oid_str.data(), oid_len_to_copy);
            buf.insert(buf.end(), oid_b, oid_b + ORDER_ID_LEN);

            // ticker
            const auto& sym_json_val = j.at("symbol");
            const std::string& sym_str = sym_json_val.get_ref<const std::string&>();
            uint8_t tkr_b[TICKER_LEN] = {0}; // Zero-initialize
            size_t sym_len_to_copy = std::min(sym_str.size(), static_cast<size_t>(TICKER_LEN -1)); // original logic for null term
            std::memcpy(tkr_b, sym_str.data(), sym_len_to_copy);
            buf.insert(buf.end(), tkr_b, tkr_b + TICKER_LEN);

            if (msg_type == TYPE_LIMIT_BUY || msg_type == TYPE_LIMIT_SELL || msg_type == TYPE_UPDATE) {
                // Price and size are common to limit_add and update
                uint32_t px = htonl(static_cast<uint32_t>(j.at("price").get<double>() * 100.0));
                uint32_t sz = htonl(j.at("size").get<uint32_t>());
                buf.insert(buf.end(), reinterpret_cast<uint8_t*>(&px), reinterpret_cast<uint8_t*>(&px) + sizeof(px));
                buf.insert(buf.end(), reinterpret_cast<uint8_t*>(&sz), reinterpret_cast<uint8_t*>(&sz) + sizeof(sz));
                if (msg_type == TYPE_UPDATE) {
                    buf.push_back(static_cast<uint8_t>(side_ch));
                }
            }
            by_user[uid].push_back({ts_ns, std::move(buf)});
        } catch (const json::parse_error& e) {
            std::cerr << "JSON parsing error: " << e.what() << " on line: " << line << std::endl;
        } catch (const json::out_of_range& e) { // For .at() if key missing
            std::cerr << "JSON key error: " << e.what() << " on line: " << line << std::endl;
        } catch (const std::runtime_error& e) { // For timestamp parsing errors
            std::cerr << "Timestamp processing error: " << e.what() << " on line: " << line << std::endl;
        }
<<<<<<< HEAD

        std::vector<uint8_t> buf;
        buf.reserve(8 + 1 + ORDER_ID_LEN + TICKER_LEN + 9);

        // timestamp
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

        by_user[uid].push_back({ts_ns, std::move(buf)});
=======
>>>>>>> 7869ddf3464af199487394fa355fb37c1aad6dc6
    }
    auto read_end_time = steady_clock::now();
    std::cout << "Finished reading and parsing events in " 
              << duration_cast<milliseconds>(read_end_time - read_start_time).count() << " ms.\n";
    
    std::cout << "Sorting events per user...\n";
    for (auto &kv : by_user) {
<<<<<<< HEAD
        std::sort(kv.second.begin(), kv.second.end(),
                  [](auto &a, auto &b){ return a.ts < b.ts; });
=======
        auto &evs = kv.second;
        std::sort(evs.begin(), evs.end(), [](const Event &a, const Event &b) { return a.ts < b.ts; });
>>>>>>> 7869ddf3464af199487394fa355fb37c1aad6dc6
    }
    std::cout << "Finished sorting events.\n";

    // Pre-allocate stats vector
    size_t total_event_count = 0;
    for (auto &kv : by_user) total_event_count += kv.second.size();

    std::mutex    stats_mutex;
    std::vector<uint64_t> proc_times_ns;
    proc_times_ns.reserve(total_event_count);
    std::atomic<uint64_t> total_events{0};

    // 2) Start exchange
    OrderParser parser; // Assuming OrderParser is default constructible
    logger log("client.log"); // Assuming logger is constructible this way
    Exchange exch(&log, &parser);
    exch.start();

<<<<<<< HEAD
    // 3) Replay events as fast as possible
    std::vector<std::thread> clients;
    clients.reserve(by_user.size());

    auto wall_start = steady_clock::now();

    for (auto &kv : by_user) {
        auto *events_ptr = &kv.second;
        clients.emplace_back([&, events_ptr]() {
            // Local timing buffer
            std::vector<uint64_t> local_times;
            local_times.reserve(events_ptr->size());

            for (auto &ev : *events_ptr) {
                auto t0 = steady_clock::now();
                exch.on_msg_received(ev.buf.data(), ev.buf.size());
                auto dt = duration_cast<nanoseconds>(steady_clock::now() - t0).count();
                local_times.push_back(dt);
                total_events.fetch_add(1, std::memory_order_relaxed);
            }

            // Merge once under lock
            {
                std::lock_guard<std::mutex> lg(stats_mutex);
                proc_times_ns.insert(proc_times_ns.end(),
                                     local_times.begin(), local_times.end());
=======
    // 3) Replay events
    std::cout << "Replaying events...\n";
    auto sim_start_time = steady_clock::now();
    std::vector<std::thread> clients;
    std::vector<std::vector<uint64_t>> thread_local_proc_times(by_user.size()); // storage for each thread
    
    size_t thread_idx = 0;
    for (auto &kv : by_user) {
        int uid = kv.first;
        // CRITICAL CHANGE: Pass events by reference to the thread lambda
        // Also, pass reference to this thread's specific proc_times vector
        clients.emplace_back([&exch, uid, &user_events = kv.second, global_min_ts, sim_start_time, &proc_times_for_this_thread = thread_local_proc_times[thread_idx], &total_events]() {
            // Reserve space in thread-local vector if number of events is known
            proc_times_for_this_thread.reserve(user_events.size());

            for (const auto &ev : user_events) { // Iterate over const& Event
                auto target_timepoint = sim_start_time + nanoseconds(ev.ts - global_min_ts);
                std::this_thread::sleep_until(target_timepoint);
                
                auto processing_start_time = steady_clock::now();
                exch.on_msg_received(ev.buf.data(), ev.buf.size()); // Pass const uint8_t*
                auto processing_end_time = steady_clock::now();
                
                uint64_t duration_ns = duration_cast<nanoseconds>(processing_end_time - processing_start_time).count();
                proc_times_for_this_thread.push_back(duration_ns);
                
                ++total_events; // Atomic increment
>>>>>>> 7869ddf3464af199487394fa355fb37c1aad6dc6
            }
        });
        thread_idx++;
    }

    for (auto &t : clients) {
        t.join();
    }
<<<<<<< HEAD
=======
    auto sim_end_time = steady_clock::now();
    std::cout << "Finished replaying events in " 
              << duration_cast<milliseconds>(sim_end_time - sim_start_time).count() << " ms.\n";
>>>>>>> 7869ddf3464af199487394fa355fb37c1aad6dc6

    // 4) Tear down
    std::this_thread::sleep_for(50ms); // Wait for any final processing in exchange
    exch.stop();

<<<<<<< HEAD
    auto wall_end = steady_clock::now();

    // 5) Print performance statistics
    {
        std::lock_guard<std::mutex> lg(stats_mutex);
        size_t n = proc_times_ns.size();
        if (n > 0) {
            std::sort(proc_times_ns.begin(), proc_times_ns.end());
            uint64_t sum_ns = std::accumulate(proc_times_ns.begin(),
                                              proc_times_ns.end(), uint64_t(0));
            double wall_sec = duration_cast<duration<double>>(wall_end - wall_start).count();
            double avg_us   = sum_ns / double(n) / 1e3;
            uint64_t min_us = proc_times_ns.front() / 1e3;
            uint64_t max_us = proc_times_ns.back()  / 1e3;
            uint64_t p95_us = proc_times_ns[(n * 95) / 100] / 1e3;
            double tp       = n / wall_sec;

            std::cout << "\n=== PERFORMANCE STATISTICS ===\n";
            std::cout << "Events processed:     " << n        << "\n";
            std::cout << "Wall-clock time:      " << wall_sec << " s\n";
            std::cout << "Avg per-event:        " << avg_us   << " μs\n";
            std::cout << "Min / Max:            " << min_us   << " μs / " << max_us << " μs\n";
            std::cout << "95th percentile:      " << p95_us   << " μs\n";
            std::cout << "Throughput:           " << tp       << " events/s\n";
        }
=======
    // Merge thread-local statistics into the main vector
    size_t total_capacity_needed = 0;
    for(const auto& vec : thread_local_proc_times) {
        total_capacity_needed += vec.size();
    }
    all_proc_times_ns.reserve(total_capacity_needed);
    for(const auto& vec : thread_local_proc_times) {
        all_proc_times_ns.insert(all_proc_times_ns.end(), vec.begin(), vec.end());
    }

    // 5) Print performance statistics
    size_t n = all_proc_times_ns.size();
    if (n > 0) {
        std::cout << "\n=== PERFORMANCE STATISTICS ===\n";
        std::cout << "Events processed:  " << total_events.load() << " (from atomic counter)\n";
        std::cout << "Durations recorded: " << n << " (from vector size)\n";
        
        std::sort(all_proc_times_ns.begin(), all_proc_times_ns.end());
        uint64_t sum_ns = std::accumulate(all_proc_times_ns.begin(), all_proc_times_ns.end(), uint64_t(0));
        
        double avg_us = static_cast<double>(sum_ns) / n / 1000.0;
        uint64_t min_us = all_proc_times_ns.front() / 1000;
        uint64_t max_us = all_proc_times_ns.back() / 1000;
        // Ensure index is valid for percentile calculation
        uint64_t p95_us = (n > 0) ? (all_proc_times_ns[static_cast<size_t>(static_cast<double>(n-1) * 0.95)] / 1000) : 0;
        
        // Throughput based on actual replay duration, not sum of event processing times
        double total_replay_duration_sec = duration_cast<duration<double>>(sim_end_time - sim_start_time).count();
        double tp = (total_replay_duration_sec > 0) ? (static_cast<double>(n) / total_replay_duration_sec) : 0;

        std::cout << "Total replay duration: " << total_replay_duration_sec << " s\n";
        std::cout << "Avg per-event processing: " << avg_us   << " µs\n";
        std::cout << "Min / Max processing:     " << min_us   << " µs / " << max_us << " µs\n";
        std::cout << "95th percentile processing: " << p95_us   << " µs\n";
        std::cout << "Throughput:            " << tp       << " events/s\n";
    } else {
        std::cout << "\nNo events processed or no timing data recorded.\n";
>>>>>>> 7869ddf3464af199487394fa355fb37c1aad6dc6
    }

    return 0;
}