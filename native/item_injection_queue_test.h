// Generated from the module queue tests; never invokes uploaded game code.
#define AUDIT_REQUIRE(value) do { if (!(value)) return false; } while (0)
#include "item_injection_queue.h"
#include <cassert>
#include <iostream>

bool audit_injection_queue() {
    using namespace item_injection;
    std::vector<Entry> entries;
    std::string error;
    AUDIT_REQUIRE(parse_items("[564000003:3, 221008025\n564000003:2;288015014 x 4]", 7, entries, error));
    AUDIT_REQUIRE(entries.size() == 3 && entries[0].id == 564000003 && entries[0].quantity == 5);
    AUDIT_REQUIRE(entries[1].quantity == 7 && entries[2].quantity == 4);
    for (const char *bad : {"", "0", "-1", "123abc", "1:0", "1:-2", "2147483648",
                            "1:18446744073709551616", "1:18446744073709551615,1:1"}) {
        AUDIT_REQUIRE(!parse_items(bad, 1, entries, error));
        AUDIT_REQUIRE(!error.empty());
    }
    std::string large;
    for (unsigned i = 1; i <= 20000; ++i) large += std::to_string(i) + ":2\n";
    AUDIT_REQUIRE(large.size() > 8192 && parse_items(large, 1, entries, error));
    AUDIT_REQUIRE(entries.size() == 20000 && entries.back().id == 20000 && entries.back().quantity == 2);

    Batch batch;
    AUDIT_REQUIRE(!batch.active() && batch.start("1:10,2:3,3:2", 1));
    auto generation = batch.generation;
    AUDIT_REQUIRE(!batch.start("9:999", 1) && batch.generation == generation && batch.entries.size() == 3);
    batch.reject(batch.entries[1], "unsupported item");
    batch.phase = Phase::Running;
    AUDIT_REQUIRE(batch.acknowledge(4, 20, 24));
    AUDIT_REQUIRE(batch.entries[0].granted == 4 && batch.completed == 0);
    AUDIT_REQUIRE(batch.acknowledge(6, 24, 30));
    AUDIT_REQUIRE(batch.index == 2 && batch.completed == 1); // unavailable ID is reported, not silently lost
    AUDIT_REQUIRE(batch.acknowledge(2, 5, 7) && batch.phase == Phase::Partial && batch.unavailable == 1);
    AUDIT_REQUIRE(batch.start("4:10", 1) && batch.generation != generation);
    batch.phase = Phase::Running;
    AUDIT_REQUIRE(batch.acknowledge(3, 10, 13));
    batch.stop();
    AUDIT_REQUIRE(!batch.active() && batch.entries[0].granted == 3 && batch.phase == Phase::Stopped);
    AUDIT_REQUIRE(!batch.acknowledge(7, 13, 20));
    AUDIT_REQUIRE(batch.start("4:10", 1));
    batch.phase = Phase::Running;
    AUDIT_REQUIRE(!batch.acknowledge(3, 10, 27)); // a conflicting multiplier/write cannot be accepted as success
    AUDIT_REQUIRE(batch.phase == Phase::Failed && batch.entries[0].granted == 0);
    AUDIT_REQUIRE(batch.start("5:1", 1));
    batch.phase = Phase::Running;
    AUDIT_REQUIRE(batch.acknowledge(1, 0, 1) && batch.phase == Phase::Complete);
    AUDIT_REQUIRE(!batch.acknowledge(1, 1, 2)); // terminal requests never repeat
    return true;
}

#undef AUDIT_REQUIRE
