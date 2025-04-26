#define CATCH_CONFIG_MAIN

#include <catch2/catch_all.hpp>
#include <chrono>
#include <thread>
#include <random>
#include "logger.h"
#include "types.h"
#include "orderbook.h"

/*
  Global logger pointer used across tests.
  In a real-world scenario, you might want
  a separate logger per test or at least
  per test suite. For simplicity here,
  we'll share one logger pointing to a single file.
*/
logger* g_test_logger = new logger("../logs/test_orderbook.log");

/**
 * Returns the current system time in nanoseconds since epoch
 */
static inline uint64_t get_current_time_ns() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<nanoseconds>(
            steady_clock::now().time_since_epoch()
        ).count()
    );
}

/**
 * Helper function to create an order_t struct with a real-time timestamp.
 */
order_t make_order(
    uint64_t timestamp,
    const char* order_id_str,
    const char* ticker_str,
    order_kind kind,
    order_side side,
    order_status status,
    uint32_t price,
    size_t qty,
    bool post_only
)
{
    return order_t(
        timestamp,
        order_id_str,
        ticker_str,
        kind,
        side,
        status,
        price,
        qty,
        post_only
    );
}

/**
 * Helper function to create an order_id_key struct
 * from a 16-byte character array (e.g. ID1[16]).
 */
order_id_key make_key(const char (&id)[16])
{
    order_id_key k;
    std::memcpy(k.order_id, id, ORDER_ID_LEN);
    return k;
}


/**
 * ==========================
 *  EXISTING TESTS
 * ==========================
 */

TEST_CASE("Orderbook: add() basic tests", "[orderbook][add]")
{
    orderbook ob(g_test_logger);

    // 4-byte ticker "ABCD"
    char TICKER_ABCD[4] = { 'A','B','C','D' };

    // 16-byte ID #1
    char ID1[16] = {
        'O','R','D','0','0','0','0','0','0','0','0','0','0','0','0','1'
    };

    // Use get_current_time_ns() instead of a fixed value
    order_t o1 = make_order(
        get_current_time_ns(),
        ID1,
        TICKER_ABCD,
        order_kind::LMT,
        order_side::BUY,
        order_status::NEW,
        100,
        10,
        false
    );
    REQUIRE(ob.add(o1) == order_result::SUCCESS);

    auto key1 = make_key(ID1);
    REQUIRE(ob.contains(key1) == true);

    auto bb = ob.best_bid();
    REQUIRE(bb.has_value());
    REQUIRE(bb.value() == 100);

    // 16-byte ID #2
    char ID2[16] = {
        'O','R','D','0','0','0','0','0','0','0','0','0','0','0','0','2'
    };
    order_t o2 = make_order(
        get_current_time_ns(),
        ID2,
        TICKER_ABCD,
        order_kind::LMT,
        order_side::SELL,
        order_status::NEW,
        105,
        5,
        false
    );
    REQUIRE(ob.add(o2) == order_result::SUCCESS);

    auto key2 = make_key(ID2);
    REQUIRE(ob.contains(key2) == true);

    auto ba = ob.best_ask();
    REQUIRE(ba.has_value());
    REQUIRE(ba.value() == 105);
}

TEST_CASE("Orderbook: add() duplicate ID", "[orderbook][add]")
{
    orderbook ob(g_test_logger);

    char TICKER_XYZ[4] = { 'X','Y','Z',' ' };
    char ID[16] = {
        'D','U','P','L','I','D','0','0','0','0','0','0','0','0','0','1'
    };

    // First order
    order_t o1 = make_order(
        get_current_time_ns(),
        ID,
        TICKER_XYZ,
        order_kind::LMT,
        order_side::BUY,
        order_status::NEW,
        50,
        10,
        false
    );
    REQUIRE(ob.add(o1) == order_result::SUCCESS);

    // Same ID again => DUPLICATE_ID
    order_t o2 = make_order(
        get_current_time_ns(),
        ID,  // same ID
        TICKER_XYZ,
        order_kind::LMT,
        order_side::BUY,
        order_status::NEW,
        51,
        20,
        false
    );
    REQUIRE(ob.add(o2) == order_result::DUPLICATE_ID);
}

TEST_CASE("Orderbook: add() invalid side", "[orderbook][add]")
{
    orderbook ob(g_test_logger);

    char TICKER_ABC[4] = { 'A','B','C',' ' };
    char ID[16] = {
        'I','N','V','S','I','D','E','0','0','0','0','0','0','0','0','1'
    };

    // side=-1 is invalid (only 0=BUY,1=SELL are valid)
    order_t invalid_side_order = {};
    std::memcpy(invalid_side_order.order_id, ID, 16);
    std::memcpy(invalid_side_order.ticker, TICKER_ABC, 4);
    invalid_side_order.timestamp = get_current_time_ns();
    invalid_side_order.price = 100;
    invalid_side_order.qty = 10;
    invalid_side_order.side = -1;  // invalid
    REQUIRE(ob.add(invalid_side_order) == order_result::INVALID_SIDE);
}

TEST_CASE("Orderbook: add() invalid price", "[orderbook][add]")
{
    orderbook ob(g_test_logger);

    char TICKER_ABC[4] = { 'A','B','C',' ' };
    char ID[16] = {
        'I','N','V','P','R','I','C','E','0','0','0','0','0','0','0','1'
    };

    // Price > MAX_PRICE (which is 20000)
    order_t invalid_price_order = make_order(
        get_current_time_ns(),
        ID,
        TICKER_ABC,
        order_kind::LMT,
        order_side::BUY,
        order_status::NEW,
        30000,  // invalid
        10,
        false
    );
    REQUIRE(ob.add(invalid_price_order) == order_result::INVALID_PRICE);
}

TEST_CASE("Orderbook: cancel() basic", "[orderbook][cancel]")
{
    orderbook ob(g_test_logger);

    char TICKER_ABC[4] = { 'A','B','C',' ' };

    char ID1[16] = {
        'C','N','C','L','0','0','0','0','0','0','0','0','0','0','0','1'
    };
    auto o1 = make_order(
        get_current_time_ns(),
        ID1,
        TICKER_ABC,
        order_kind::LMT,
        order_side::SELL,
        order_status::NEW,
        150,
        25,
        false
    );
    REQUIRE(ob.add(o1) == order_result::SUCCESS);

    auto key1 = make_key(ID1);
    REQUIRE(ob.contains(key1) == true);

    REQUIRE(ob.cancel(key1) == order_result::SUCCESS);
    REQUIRE(ob.contains(key1) == false);

    // best_ask should now be empty
    REQUIRE_FALSE(ob.best_ask().has_value());
    // best_bid also empty
    REQUIRE_FALSE(ob.best_bid().has_value());
}

TEST_CASE("Orderbook: cancel() order not found", "[orderbook][cancel]")
{
    orderbook ob(g_test_logger);

    char ID[16] = {
        'N','O','T','_','F','O','U','N','D','0','0','0','0','0','0','0'
    };
    auto key = make_key(ID);

    // Canceling a non-existent order should fail
    REQUIRE(ob.cancel(key) == order_result::ORDER_NOT_FOUND);
}

TEST_CASE("Orderbook: modify() same price", "[orderbook][modify]")
{
    orderbook ob(g_test_logger);

    // Create a BUY order
    char TICKER_ABC[4] = { 'A','B','C',' ' };
    char ID1[16] = {
        'M','O','D','-','S','A','M','E','-','P','R','I','C','E','-','1'
    };

    order_t o1 = make_order(
        get_current_time_ns(),
        ID1,
        TICKER_ABC,
        order_kind::LMT,
        order_side::BUY,
        order_status::NEW,
        200,
        10,
        false
    );
    REQUIRE(ob.add(o1) == order_result::SUCCESS);

    // Modify the existing order with the same price but different qty
    order_t modified_o1 = o1; // copy
    modified_o1.qty = 20;
    modified_o1.timestamp = get_current_time_ns(); // new real-time stamp

    auto key1 = make_key(ID1);
    REQUIRE(ob.modify(key1, modified_o1) == order_result::SUCCESS);

    // best_bid should remain 200
    auto bb = ob.best_bid();
    REQUIRE(bb.has_value());
    REQUIRE(bb.value() == 200);

    // Confirm that the order is still there under the same ID
    REQUIRE(ob.contains(key1) == true);
}

TEST_CASE("Orderbook: modify() change price", "[orderbook][modify]")
{
    orderbook ob(g_test_logger);

    char TICKER_ABC[4] = { 'A','B','C',' ' };
    char ID1[16] = {
        'M','O','D','C','H','G','P','R','I','C','E','0','0','0','0','1'
    };

    // Add BUY at price=150
    order_t o1 = make_order(
        get_current_time_ns(),
        ID1,
        TICKER_ABC,
        order_kind::LMT,
        order_side::BUY,
        order_status::NEW,
        150,
        10,
        false
    );
    REQUIRE(ob.add(o1) == order_result::SUCCESS);

    // Modify it to price=180
    order_t modified_o1 = o1;
    modified_o1.price = 180;
    modified_o1.qty   = 15;
    modified_o1.timestamp = get_current_time_ns(); // new real-time

    auto key1 = make_key(ID1);
    REQUIRE(ob.modify(key1, modified_o1) == order_result::SUCCESS);

    // The best bid should now be 180
    auto bb = ob.best_bid();
    REQUIRE(bb.has_value());
    REQUIRE(bb.value() == 180);

    // We have no ask orders in the book, so best_ask() is std::nullopt
    REQUIRE_FALSE(ob.best_ask().has_value());
}

TEST_CASE("Orderbook: modify() nonexistent order", "[orderbook][modify]")
{
    orderbook ob(g_test_logger);

    char ID[16] = {
        'N','O','N','E','X','I','S','T','0','0','0','0','0','0','0','1'
    };

    order_t some_order;
    std::memcpy(some_order.order_id, ID, 16);
    some_order.price = 100;
    some_order.qty   = 10;
    some_order.timestamp = get_current_time_ns();

    auto key = make_key(ID);
    // Attempt to modify an order not in the book
    REQUIRE(ob.modify(key, some_order) == order_result::ORDER_NOT_FOUND);
}

TEST_CASE("Orderbook: execute() basic match", "[orderbook][execute]")
{
    orderbook ob(g_test_logger);

    /*
       Create a simple scenario:
         - BUY:  Price=100, Qty=10
         - SELL: Price=90,  Qty=5
       We expect immediate match since best_bid_price_ >= best_ask_price_.
       The match should partially fill the BUY if SELL has less quantity.
    */

    char ID_BUY[16] = {
        'E','X','E','C','-','B','A','S','I','C','-','B','U','Y','-','1'
    };
    order_t buy_o = make_order(
        get_current_time_ns(),
        ID_BUY,
        "ABCD",
        order_kind::LMT,
        order_side::BUY,
        order_status::NEW,
        100,   // price
        10,    // qty
        false
    );
    REQUIRE(ob.add(buy_o) == order_result::SUCCESS);

    // SELL at 90, qty=5
    char ID_SELL[16] = {
        'E','X','E','C','-','B','A','S','I','C','-','S','E','L','L','1'
    };
    order_t sell_o = make_order(
        get_current_time_ns(),
        ID_SELL,
        "ABCD",
        order_kind::LMT,
        order_side::SELL,
        order_status::NEW,
        90,   // price
        5,    // qty
        false
    );
    REQUIRE(ob.add(sell_o) == order_result::SUCCESS);

    // best_bid = 100, best_ask = 90 => match possible
    ob.execute();

    // The SELL order should fully fill (qty=5). The BUY remains partially filled (5 left).
    auto key_sell = make_key(ID_SELL);
    REQUIRE(ob.contains(key_sell) == false); // fully filled => removed

    auto key_buy = make_key(ID_BUY);
    REQUIRE(ob.contains(key_buy) == true);   // partially filled => remains

    // best_ask should now be empty
    REQUIRE_FALSE(ob.best_ask().has_value());

    // best_bid should still be 100 (the partial remainder)
    REQUIRE(ob.best_bid().has_value());
    REQUIRE(ob.best_bid().value() == 100);
}

TEST_CASE("Orderbook: execute() multiple matches", "[orderbook][execute]")
{
    /*
      Scenario:
        - 2 BUY orders:  (price=100, qty=5),  (price=95, qty=10)
        - 2 SELL orders: (price=90,  qty=6),  (price=85, qty=10)

      best_bid=100, best_ask=85 => everything will keep matching until
      no more cross can occur.
    */
    orderbook ob(g_test_logger);

    // BUY #1
    char IDB1[16] = { 'M','U','L','T','I','-','B','U','Y','-','0','0','0','0','0','1' };
    auto b1 = make_order(
        get_current_time_ns(),
        IDB1,
        "ABCD",
        order_kind::LMT,
        order_side::BUY,
        order_status::NEW,
        100,
        5,
        false
    );
    REQUIRE(ob.add(b1) == order_result::SUCCESS);

    // BUY #2
    char IDB2[16] = { 'M','U','L','T','I','-','B','U','Y','-','0','0','0','0','0','2' };
    auto b2 = make_order(
        get_current_time_ns(),
        IDB2,
        "ABCD",
        order_kind::LMT,
        order_side::BUY,
        order_status::NEW,
        95,
        10,
        false
    );
    REQUIRE(ob.add(b2) == order_result::SUCCESS);

    // SELL #1
    char IDS1[16] = { 'M','U','L','T','I','-','S','E','L','L','-','0','0','0','0','1' };
    auto s1 = make_order(
        get_current_time_ns(),
        IDS1,
        "ABCD",
        order_kind::LMT,
        order_side::SELL,
        order_status::NEW,
        90,
        6,
        false
    );
    REQUIRE(ob.add(s1) == order_result::SUCCESS);

    // SELL #2
    char IDS2[16] = { 'M','U','L','T','I','-','S','E','L','L','-','0','0','0','0','2' };
    auto s2 = make_order(
        get_current_time_ns(),
        IDS2,
        "ABCD",
        order_kind::LMT,
        order_side::SELL,
        order_status::NEW,
        85,
        10,
        false
    );
    REQUIRE(ob.add(s2) == order_result::SUCCESS);

    // Confirm best_bid=100, best_ask=85
    REQUIRE(ob.best_bid().has_value());
    REQUIRE(ob.best_bid().value() == 100);

    REQUIRE(ob.best_ask().has_value());
    REQUIRE(ob.best_ask().value() == 85);

    // Execute all matches
    ob.execute();

    // After execution:
    //  - b1(100@5), b2(95@10), s2(85@10), s1(90@6)
    //
    //   b1 -> fully fills vs s2 partial => leftover s2=5
    //   b2 -> partial fill with remaining s2 => leftover b2=5, s2 fully filled
    //   b2 -> then tries s1 => leftover s1=1, b2 fully filled
    //   => b1 gone, b2 gone, s2 gone, s1 remains with qty=1

    auto kb1  = make_key(IDB1);
    auto kb2  = make_key(IDB2);
    auto ks1  = make_key(IDS1);
    auto ks2  = make_key(IDS2);

    REQUIRE_FALSE(ob.contains(kb1)); // fully executed
    REQUIRE_FALSE(ob.contains(kb2)); // fully executed
    REQUIRE_FALSE(ob.contains(ks2)); // fully executed
    REQUIRE(ob.contains(ks1));       // partial fill => remains

    // best_ask should now be 90 (from s1 leftover)
    REQUIRE(ob.best_ask().has_value());
    REQUIRE(ob.best_ask().value() == 90);

    // best_bid should not exist (no more buys)
    REQUIRE_FALSE(ob.best_bid().has_value());
}

TEST_CASE("Orderbook: empty best_bid / best_ask", "[orderbook]")
{
    orderbook ob(g_test_logger);

    REQUIRE_FALSE(ob.best_bid().has_value());
    REQUIRE_FALSE(ob.best_ask().has_value());

    // Add a single BUY
    char ID_B[16] = { 'E','M','P','T','Y','-','B','I','D','-','T','E','S','T','0','1' };
    auto b = make_order(
        get_current_time_ns(),
        ID_B,
        "EFGH",
        order_kind::LMT,
        order_side::BUY,
        order_status::NEW,
        500,
        10,
        false
    );
    REQUIRE(ob.add(b) == order_result::SUCCESS);

    REQUIRE(ob.best_bid().has_value());
    REQUIRE(ob.best_bid().value() == 500);
    REQUIRE_FALSE(ob.best_ask().has_value()); // no SELL

    // Cancel the BUY
    auto kb = make_key(ID_B);
    REQUIRE(ob.cancel(kb) == order_result::SUCCESS);

    REQUIRE_FALSE(ob.best_bid().has_value());
    REQUIRE_FALSE(ob.best_ask().has_value());
}

TEST_CASE("Orderbook: boundary test at max price", "[orderbook][boundary]")
{
    orderbook ob(g_test_logger);

    /*
      Since MAX_PRICE = 20000, let's place a BUY or SELL exactly at 20000
      and confirm it succeeds. Then see if a SELL with price=20001 fails.
    */

    char ID_BMax[16] = { 'B','M','A','X','P','R','I','C','E','0','0','0','0','0','0','B' };
    char ID_SInv[16] = { 'S','I','N','V','P','R','I','C','E','0','0','0','0','0','0','S' };

    // Valid boundary
    order_t bmax = make_order(
        get_current_time_ns(),
        ID_BMax,
        "ZZZZ",
        order_kind::LMT,
        order_side::BUY,
        order_status::NEW,
        20000,
        10,
        false
    );
    REQUIRE(ob.add(bmax) == order_result::SUCCESS);

    // Attempt SELL at price=20001 (invalid)
    order_t sinv = make_order(
        get_current_time_ns(),
        ID_SInv,
        "ZZZZ",
        order_kind::LMT,
        order_side::SELL,
        order_status::NEW,
        20001,  // invalid
        5,
        false
    );
    REQUIRE(ob.add(sinv) == order_result::INVALID_PRICE);
}


/**
 * ==========================
 *  ADDITIONAL STRENUOUS TESTS
 * ==========================
 */

/**
 * Tests multiple price levels on both sides, verifying best_bid/ask
 * updates as we add/cancel orders in mid levels.
 */
TEST_CASE("Orderbook: multi-level scenario", "[orderbook][multi-level]")
{
    orderbook ob(g_test_logger);

    // We'll create multiple BUY orders at 100, 98, 95
    // and multiple SELL orders at 105, 107, 110
    // Then we cancel some orders to see if the best_* logic updates properly.

    // Create BUY @100
    char BID1[16] = { 'B','U','Y','1','0','0','x','x','x','x','x','x','x','x','x','1' };
    auto b100 = make_order(
        get_current_time_ns(),
        BID1,
        "ABCD",
        order_kind::LMT,
        order_side::BUY,
        order_status::NEW,
        100,
        5,
        false
    );
    REQUIRE(ob.add(b100) == order_result::SUCCESS);

    // Create BUY @98
    char BID2[16] = { 'B','U','Y','9','8','x','x','x','x','x','x','x','x','x','x','2' };
    auto b98 = make_order(
        get_current_time_ns(),
        BID2,
        "ABCD",
        order_kind::LMT,
        order_side::BUY,
        order_status::NEW,
        98,
        10,
        false
    );
    REQUIRE(ob.add(b98) == order_result::SUCCESS);

    // Create BUY @95
    char BID3[16] = { 'B','U','Y','9','5','x','x','x','x','x','x','x','x','x','x','3' };
    auto b95 = make_order(
        get_current_time_ns(),
        BID3,
        "ABCD",
        order_kind::LMT,
        order_side::BUY,
        order_status::NEW,
        95,
        20,
        false
    );
    REQUIRE(ob.add(b95) == order_result::SUCCESS);

    // Check best_bid => 100
    auto bb = ob.best_bid();
    REQUIRE(bb.has_value());
    REQUIRE(bb.value() == 100);

    // Create SELL @105
    char SID1[16] = { 'S','E','L','L','1','0','5','x','x','x','x','x','x','x','x','1' };
    auto s105 = make_order(
        get_current_time_ns(),
        SID1,
        "ABCD",
        order_kind::LMT,
        order_side::SELL,
        order_status::NEW,
        105,
        5,
        false
    );
    REQUIRE(ob.add(s105) == order_result::SUCCESS);

    // Create SELL @107
    char SID2[16] = { 'S','E','L','L','1','0','7','x','x','x','x','x','x','x','x','2' };
    auto s107 = make_order(
        get_current_time_ns(),
        SID2,
        "ABCD",
        order_kind::LMT,
        order_side::SELL,
        order_status::NEW,
        107,
        10,
        false
    );
    REQUIRE(ob.add(s107) == order_result::SUCCESS);

    // Create SELL @110
    char SID3[16] = { 'S','E','L','L','1','1','0','x','x','x','x','x','x','x','x','3' };
    auto s110 = make_order(
        get_current_time_ns(),
        SID3,
        "ABCD",
        order_kind::LMT,
        order_side::SELL,
        order_status::NEW,
        110,
        25,
        false
    );
    REQUIRE(ob.add(s110) == order_result::SUCCESS);

    // Check best_ask => 105
    auto ba = ob.best_ask();
    REQUIRE(ba.has_value());
    REQUIRE(ba.value() == 105);

    // Now cancel the BUY@100
    auto kb1 = make_key(BID1);
    REQUIRE(ob.cancel(kb1) == order_result::SUCCESS);

    // best_bid should now be 98
    bb = ob.best_bid();
    REQUIRE(bb.has_value());
    REQUIRE(bb.value() == 98);

    // Cancel SELL@105
    auto ks1 = make_key(SID1);
    REQUIRE(ob.cancel(ks1) == order_result::SUCCESS);

    // best_ask should now be 107
    ba = ob.best_ask();
    REQUIRE(ba.has_value());
    REQUIRE(ba.value() == 107);
}

/**
 * Tests a "side-changing" modify: convert a BUY order into a SELL order
 * (by changing side, price, etc.). Ensures we remove from old side and re-insert in new side.
 */
TEST_CASE("Orderbook: modify side BUY->SELL", "[orderbook][modify_side]")
{
    orderbook ob(g_test_logger);

    // Original is BUY@100
    char ID[16] = { 'C','H','G','-','S','I','D','E','-','B','U','Y','-','T','E','S' };
    order_t buy_o = make_order(
        get_current_time_ns(),
        ID,
        "CHNG",
        order_kind::LMT,
        order_side::BUY,
        order_status::NEW,
        100,
        10,
        false
    );
    REQUIRE(ob.add(buy_o) == order_result::SUCCESS);

    // Confirm best_bid=100, no best_ask
    auto bb = ob.best_bid();
    REQUIRE(bb.has_value());
    REQUIRE(bb.value() == 100);
    REQUIRE_FALSE(ob.best_ask().has_value());

    // Modify it to SELL@105
    order_t new_o = buy_o;
    new_o.price   = 105;
    new_o.side    = static_cast<uint8_t>(order_side::SELL);
    new_o.qty     = 15;
    new_o.timestamp = get_current_time_ns();

    auto key = make_key(ID);
    REQUIRE(ob.modify(key, new_o) == order_result::SUCCESS);

    // Now it should appear on the SELL side at price=105
    REQUIRE_FALSE(ob.best_bid().has_value());
    auto ba = ob.best_ask();
    REQUIRE(ba.has_value());
    REQUIRE(ba.value() == 105);

    // Confirm the order is still tracked
    REQUIRE(ob.contains(key) == true);
}

/**
 * A test with mid-sequence cancels during partial matches.
 */
TEST_CASE("Orderbook: partial match + mid-sequence cancel", "[orderbook][partial_cancel]")
{
    orderbook ob(g_test_logger);

    // We'll have a BUY@100 qty=10, SELL@95 qty=20 => immediate cross,
    // but we cancel the SELL after a partial fill.

    // BUY@100
    char ID_Buy[16] = { 'P','A','R','T','-','C','N','C','L','-','B','U','Y','-','0','1' };
    auto b = make_order(
        get_current_time_ns(),
        ID_Buy,
        "PART",
        order_kind::LMT,
        order_side::BUY,
        order_status::NEW,
        100,
        10,
        false
    );
    REQUIRE(ob.add(b) == order_result::SUCCESS);

    // SELL@95 => cross with 100
    char ID_Sell[16] = { 'P','A','R','T','-','C','N','C','L','-','S','E','L','L','0','1' };
    auto s = make_order(
        get_current_time_ns(),
        ID_Sell,
        "PART",
        order_kind::LMT,
        order_side::SELL,
        order_status::NEW,
        95,
        20,  // bigger qty => partial fill
        false
    );
    REQUIRE(ob.add(s) == order_result::SUCCESS);

    // Attempt match
    ob.execute();
    // The SELL will partially fill the BUY => leftover SELL qty=10, BUY is fully filled
    // Actually, BUY(10) vs SELL(20) => leftover SELL=10, buy is removed
    auto kb = make_key(ID_Buy);
    auto ks = make_key(ID_Sell);
    REQUIRE_FALSE(ob.contains(kb));  // fully filled => removed
    REQUIRE(ob.contains(ks));       // partially filled => remains

    // Now best_bid is empty
    REQUIRE_FALSE(ob.best_bid().has_value());
    // best_ask is 95
    auto ba = ob.best_ask();
    REQUIRE(ba.has_value());
    REQUIRE(ba.value() == 95);

    // Let's cancel the SELL leftover
    REQUIRE(ob.cancel(ks) == order_result::SUCCESS);
    REQUIRE_FALSE(ob.contains(ks));
    REQUIRE_FALSE(ob.best_ask().has_value());
}

/**
 * Tests adding a bunch of random orders, verifying no errors occur,
 * best_bid and best_ask remain valid, and we do some random cancels.
 *
 * This is a mini "stress" test, still deterministic but covers more variety.
 */
TEST_CASE("Orderbook: bulk random stress test", "[orderbook][stress]")
{
    orderbook ob(g_test_logger);

    // We'll add 50 random orders, either BUY or SELL,
    // price in [50..150], qty in [1..20].
    // We'll also randomly cancel 10 of them.

    std::mt19937 rng(12345); // fixed seed for deterministic test
    std::uniform_int_distribution<int> side_dist(0, 1);    // 0=BUY,1=SELL
    std::uniform_int_distribution<int> price_dist(50, 150);
    std::uniform_int_distribution<int> qty_dist(1, 20);
    std::uniform_int_distribution<int> cancel_dist(0, 49);

    std::vector<std::string> active_ids;  // track IDs we actually added & not canceled
    active_ids.reserve(50);

    for(int i=0; i<50; i++) {
        // build an ID
        char ID[16];
        snprintf(ID, sizeof(ID), "STRESS-%04d", i);

        order_side sd = (side_dist(rng) == 0) ? order_side::BUY : order_side::SELL;
        uint32_t pr = (uint32_t)price_dist(rng);
        size_t qt = (size_t)qty_dist(rng);

        auto ord = make_order(
            get_current_time_ns(),
            ID,
            "STES",
            order_kind::LMT,
            sd,
            order_status::NEW,
            pr,
            qt,
            false
        );

        auto res = ob.add(ord);
        REQUIRE(res == order_result::SUCCESS);
        active_ids.push_back(std::string(ID));
    }

    // randomly cancel 10 orders from the active list
    // pick random indices, if already canceled skip
    for(int c=0; c<10; c++) {
        int idx = cancel_dist(rng);
        if(idx < (int)active_ids.size() && !active_ids[idx].empty()) {
            char ID[16];
            std::memset(ID, 0, sizeof(ID));
            std::memcpy(ID, active_ids[idx].data(), std::min<size_t>(active_ids[idx].size(), 15));
            auto key = make_key(ID);

            // If it exists, cancel it
            if(ob.contains(key)) {
                ob.cancel(key);
            }
            // Mark as canceled
            active_ids[idx].clear();
        }
    }

    // We won't do a full set of REQUIREs on best_bid/best_ask because we don't know
    // the random distribution exactly. But let's ensure we can call them safely
    // and they don't crash. We'll just confirm they are within [0, 150], if they exist.
    auto bb = ob.best_bid();
    if(bb.has_value()) {
        REQUIRE(bb.value() <= 150);
        REQUIRE(bb.value() >= 0);
    }

    auto ba = ob.best_ask();
    if(ba.has_value()) {
        REQUIRE(ba.value() >= 0);
        REQUIRE(ba.value() <= 20000); 
    }

    // We can do a partial execution test
    ob.execute();
    // no error => pass
    // The final best_bid/best_ask remain valid
    bb = ob.best_bid();
    ba = ob.best_ask();
    if(bb.has_value()) {
        REQUIRE(bb.value() <= 150);
    }
    if(ba.has_value()) {
        REQUIRE(ba.value() <= 20000);
    }
}
