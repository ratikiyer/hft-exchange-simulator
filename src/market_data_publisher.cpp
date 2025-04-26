// market_data_publisher.cpp
#include "market_data_publisher.h"
#include <thread>
#include <chrono>

// Constructor: just initialize members
MarketDataPublisher::MarketDataPublisher(
    boost::asio::io_context& ctx,
    const std::string& multicast_ip,
    unsigned short port
) : io_context_(ctx),
    socket_(ctx),
    multicast_endpoint_(boost::asio::ip::make_address(multicast_ip), port),
    running_(false)
{}

// Destructor: ensure we stop the thread
MarketDataPublisher::~MarketDataPublisher() {
    stop();
}

// start() spawns the background thread
void MarketDataPublisher::start() {
    bool expected = false;
    if (running_.compare_exchange_strong(expected, true)) {
        thread_ = std::thread(&MarketDataPublisher::run, this);
    }
}

// stop() signals the thread and joins
void MarketDataPublisher::stop() {
    bool expected = true;
    if (running_.compare_exchange_strong(expected, false)) {
        if (thread_.joinable()) {
            thread_.join();
        }
    }
}

// Each of these just enqueues an event
void MarketDataPublisher::publish_price_level_update(const PriceLevelUpdateMD& plu) {
    updateQueue_.enqueue(MarketDataEvent{MarketInfoType::PRICE_LEVEL_UPDATE, plu});
}
void MarketDataPublisher::publish_trade_report(const TradeReportMD& tr) {
    updateQueue_.enqueue(MarketDataEvent{MarketInfoType::TRADE_REPORT, tr});
}
void MarketDataPublisher::publish_modify_event(const ModifyMD& me) {
    updateQueue_.enqueue(MarketDataEvent{MarketInfoType::MODIFY, me});
}
void MarketDataPublisher::publish_cancel_event(const CancelMD& ce) {
    updateQueue_.enqueue(MarketDataEvent{MarketInfoType::CANCEL, ce});
}

// The background run loop just sleeps and drains the queue (no real sending)
void MarketDataPublisher::run() {
    while (running_) {
        MarketDataEvent ev;
        while (updateQueue_.try_dequeue(ev)) {
            // In a real implementation we'd serialize & send via socket_
            // For now, do nothing
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    // Drain any remaining events
    MarketDataEvent ev;
    while (updateQueue_.try_dequeue(ev)) {}
}
