#include "shop_counter.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace ark::stronghold;

namespace {
int failures = 0;
#define CHECK(value) do { if (!(value)) { \
    std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #value); ++failures; \
} } while (0)

IntelShopGood good(int slot, const char* id, const char* name, bool frozen = false) {
    IntelShopGood value;
    value.slot_id = slot;
    value.chess_id = id;
    value.operator_name = name;
    value.frozen = frozen;
    value.operator_available = true;
    return value;
}

ShopObservation observation(int session, int round, int player, int refresh_seq,
                            std::vector<IntelShopGood> goods) {
    ShopObservation value;
    value.session_id = session;
    value.round = round;
    value.player_index = player;
    value.refresh_seq = refresh_seq;
    value.refresh_seq_available = true;
    value.goods_available = true;
    value.goods = std::move(goods);
    return value;
}

ShopObservation observation_without_sequence(int session, int round, int player,
                                             std::vector<IntelShopGood> goods) {
    ShopObservation value = observation(session, round, player, 0, std::move(goods));
    value.refresh_seq_available = false;
    value.slot_ids_available = false;
    return value;
}

const PlayerOperatorRow* find_operator(const std::vector<PlayerOperatorRow>& rows,
                                       int player, const std::string& key) {
    for (const auto& row : rows)
        if (row.player_index == player && row.key == key) return &row;
    return nullptr;
}

int frequency_total(const ShopCounter& counter, const std::string& key,
                    bool by_bond = false) {
    for (const auto& row : counter.frequencies(by_bond))
        if (row.key == key) return row.total;
    return 0;
}

void test_shelf_slots_bonds_and_frozen_goods() {
    ShopCounter counter;
    auto a0 = good(0, "char-a", "干员甲", true);
    a0.bond_ids = {"bond-a", "bond-b"};
    a0.bond_names = {"盟约甲", "盟约乙"};
    auto a1 = good(1, "char-a", "干员甲");
    a1.bond_ids = a0.bond_ids;
    a1.bond_names = a0.bond_names;
    counter.apply(observation(10, 1, 1, 0, {a0, a1}));
    counter.apply(observation(10, 1, 1, 0, {a0, a1}));
    CHECK(counter.frequencies(false).size() == 1);
    CHECK(counter.frequencies(false)[0].total == 2);
    CHECK(counter.frequencies(false)[0].players[0] == 2);

    auto b1 = good(1, "char-b", "干员乙");
    b1.bond_ids = {"bond-a"};
    b1.bond_names = {"盟约甲"};
    counter.apply(observation(10, 1, 1, 1, {a0, b1}));
    const auto operators = counter.frequencies(false);
    CHECK(operators.size() == 2);
    CHECK(operators[0].key == "char-a" && operators[0].total == 2);
    CHECK(operators[1].key == "char-b" && operators[1].total == 1);
    const auto bonds = counter.frequencies(true);
    CHECK(bonds.size() == 2);
    CHECK(bonds[0].key == "bond-a" && bonds[0].total == 3);
    CHECK(bonds[1].key == "bond-b" && bonds[1].total == 2);
    CHECK(counter.frequencies(false, 2).empty());

    counter.apply(observation(10, 1, 1, 3, {a0, b1}));
    CHECK(counter.incomplete());
    std::puts("PASS shop slots / all bonds / frozen persistence / refresh gaps / player filter");
}

void test_frequency_counts_and_order_follow_player_scope() {
    ShopCounter counter;
    auto a = good(0, "char-a", "干员甲");
    auto b = good(1, "char-b", "干员乙");
    auto c = good(1, "char-c", "干员丙");
    a.bond_ids = {"bond-a"};
    b.bond_ids = {"bond-b"};
    c.bond_ids = {"bond-c"};
    counter.apply(observation(11, 1, 1, 0, {a, b}));
    counter.apply(observation(11, 1, 1, 1, {b}));
    counter.apply(observation(11, 1, 4, 0, {a, c}));
    counter.apply(observation(11, 1, 4, 1, {a}));
    counter.apply(observation(11, 1, 4, 2, {a}));
    counter.apply(observation(11, 1, 4, 3, {a}));

    for (const bool by_bond : {false, true}) {
        const std::string prefix = by_bond ? "bond-" : "char-";
        const auto all = counter.frequencies(by_bond);
        CHECK(all.size() == 3 && all[0].key == prefix + "a" && all[0].total == 5);
        CHECK(all.size() == 3 && all[0].players[0] == 1 && all[0].players[3] == 4);
        const auto p1 = counter.frequencies(by_bond, 1);
        CHECK(p1.size() == 2 && p1[0].key == prefix + "b" && p1[0].total == 2);
        CHECK(p1.size() == 2 && p1[1].key == prefix + "a" && p1[1].total == 1);
        const auto p4 = counter.frequencies(by_bond, 4);
        CHECK(p4.size() == 2 && p4[0].key == prefix + "a" && p4[0].total == 4);
        CHECK(p4.size() == 2 && p4[1].key == prefix + "c" && p4[1].total == 1);
        CHECK(counter.frequencies(by_bond, 2).empty());
    }
}

void test_acquisition_shop_link_sales_and_session_reset() {
    ShopCounter counter;
    auto a = good(0, "char-a", "干员甲");
    auto baseline = observation(20, 1, 1, 0, {a});
    baseline.owned_available = true;
    baseline.owned.push_back({101, 1, "char-a", "干员甲"});
    counter.apply(baseline);
    auto rows = counter.player_operators();
    CHECK(find_operator(rows, 1, "char-a") != nullptr);
    CHECK(find_operator(rows, 1, "char-a")->held == 1);
    CHECK(find_operator(rows, 1, "char-a")->gained == 0);

    auto bought = observation(20, 1, 1, 0, {});
    bought.owned_available = true;
    bought.owned.push_back({101, 1, "char-a", "干员甲"});
    bought.gained_available = true;
    bought.gained.push_back({101, 1, "char-a", "干员甲"});
    counter.apply(bought);
    rows = counter.player_operators();
    const auto* gained = find_operator(rows, 1, "char-a");
    CHECK(gained && gained->gained == 1 && gained->shop_gained == 1 && gained->held == 1);

    auto other_source = observation(20, 1, 1, 0, {});
    other_source.owned_available = true;
    other_source.owned.push_back({101, 2, "char-a", "干员甲"});
    other_source.gained_available = true;
    other_source.gained.push_back({101, 2, "char-a", "干员甲"});
    counter.apply(other_source);
    rows = counter.player_operators();
    gained = find_operator(rows, 1, "char-a");
    CHECK(gained && gained->gained == 2 && gained->shop_gained == 1 && gained->held == 2);

    auto sold = observation(20, 2, 1, 0, {});
    sold.owned_available = true;
    sold.owned.push_back({101, 1, "char-a", "干员甲"});
    sold.gained_available = true;
    sold.gained.push_back({101, 0, "char-a", "干员甲"});
    counter.apply(sold);
    rows = counter.player_operators();
    gained = find_operator(rows, 1, "char-a");
    CHECK(gained && gained->estimated_sold == 1 && gained->held == 1);

    auto teammate = observation(20, 2, 2, 0, {a});
    counter.apply(teammate);
    CHECK(counter.frequencies(false, 1).size() == 1);
    CHECK(counter.frequencies(false, 2).size() == 1);

    counter.apply(observation(21, 1, 1, 0, {good(0, "char-c", "干员丙")}));
    CHECK(counter.session_id() == 21);
    CHECK(counter.frequencies(false).size() == 1);
    CHECK(counter.frequencies(false)[0].key == "char-c");
    CHECK(counter.player_operators().empty());
    std::puts("PASS baseline inventory / all-source gains / inferred shop gains and sales / session reset");
}

void test_supported_player_slots() {
    CHECK(!is_shop_player_slot(0));
    CHECK(is_shop_player_slot(1));
    CHECK(is_shop_player_slot(4));
    CHECK(!is_shop_player_slot(5));

    ShopCounter counter;
    counter.apply(observation(30, 1, 4, 0, {good(0, "char-p4", "P4干员")}));
    const auto rows = counter.frequencies(false);
    CHECK(rows.size() == 1 && rows[0].players[3] == 1);
    std::puts("PASS raw player slots 1-4 include P4 and reject out-of-range values");
}

void test_unresolved_operator_is_still_counted() {
    ShopCounter counter;
    IntelShopGood unresolved;
    unresolved.slot_id = 0;
    unresolved.chess_identifier = 4242;
    unresolved.frozen = false;
    unresolved.operator_available = false;
    counter.apply(observation(40, 1, 1, 0, {unresolved}));
    const auto rows = counter.frequencies(false);
    CHECK(rows.size() == 1);
    CHECK(rows[0].key == "#4242");
    CHECK(rows[0].total == 1 && rows[0].players[0] == 1);
    CHECK(rows[0].label == "干员名称未读取");
    std::puts("PASS unresolved shop identifiers remain countable");
}

void test_protocol_slot_normalization_helper() {
    CHECK(canonical_shop_player_slot(0, true) == 1);
    CHECK(canonical_shop_player_slot(3, true) == 4);
    CHECK(canonical_shop_player_slot(1, false) == 1);
    CHECK(canonical_shop_player_slot(4, false) == 4);
    CHECK(canonical_shop_player_slot(4, true) == -1);
    std::puts("PASS zero-based protocol slots map to P1-P4");
}

void test_shelf_change_fallback_without_refresh_sequence() {
    ShopCounter counter;
    auto frozen = good(0, "char-a", "干员甲", true);
    auto first = observation_without_sequence(50, 1, 1,
        {frozen, good(1, "char-b", "干员乙")});
    counter.apply(first);
    counter.apply(first);
    auto rows = counter.frequencies(false);
    CHECK(rows.size() == 2);
    CHECK(counter.incomplete());
    for (const auto& row : rows) CHECK(row.total == 1);

    counter.apply(observation_without_sequence(50, 1, 1,
        {frozen, good(1, "char-c", "干员丙")}));
    rows = counter.frequencies(false);
    CHECK(rows.size() == 3);
    for (const auto& row : rows) CHECK(row.total == 1);

    counter.apply(observation_without_sequence(50, 1, 1,
        {frozen, good(1, "char-b", "干员乙")}));
    rows = counter.frequencies(false);
    CHECK(rows.size() == 3);
    for (const auto& row : rows) {
        if (row.key == "char-b") CHECK(row.total == 2);
        if (row.key == "char-a") CHECK(row.total == 1);
    }
    std::puts("PASS shelf-content fallback counts changed stock and avoids duplicate/frozen observations");
}

void test_polling_and_callback_same_sequence_dedup() {
    ShopCounter counter;
    const auto a = good(0, "char-a", "干员甲");
    const auto b = good(1, "char-b", "干员乙");

    counter.apply(observation(60, 1, 1, 7, {a, b}));
    counter.apply(observation(60, 1, 1, 7, {b, a}));
    CHECK(frequency_total(counter, "char-a") == 1);
    CHECK(frequency_total(counter, "char-b") == 1);
    CHECK(!counter.incomplete());

    // A partial callback and the next complete poll describe the same refresh.
    counter.apply(observation(60, 1, 1, 7, {a}));
    counter.apply(observation(60, 1, 1, 7, {a, b}));
    CHECK(frequency_total(counter, "char-a") == 1);
    CHECK(frequency_total(counter, "char-b") == 1);
    CHECK(!counter.incomplete());

    const auto c = good(1, "char-c", "干员丙");
    counter.apply(observation(60, 1, 1, 7, {a, c}));
    counter.apply(observation(60, 1, 1, 7, {a, c}));
    counter.apply(observation(60, 1, 1, 7, {a, b}));
    CHECK(frequency_total(counter, "char-a") == 1);
    CHECK(frequency_total(counter, "char-b") == 1);
    CHECK(frequency_total(counter, "char-c") == 1);
    CHECK(counter.incomplete());
    std::puts("PASS polling/callback same-sequence identities and partial snapshots are deduplicated");
}

void test_constant_sequence_dedup_and_real_advance() {
    ShopCounter counter;
    const auto a = good(0, "char-a", "干员甲");
    const auto b = good(1, "char-b", "干员乙");
    const auto c = good(1, "char-c", "干员丙");

    counter.apply(observation(70, 1, 1, 0, {a, b}));
    counter.apply(observation(70, 1, 1, 0, {a, c}));
    counter.apply(observation(70, 1, 1, 0, {a, b}));
    CHECK(frequency_total(counter, "char-a") == 1);
    CHECK(frequency_total(counter, "char-b") == 1);
    CHECK(frequency_total(counter, "char-c") == 1);
    CHECK(counter.incomplete());

    // A sequence advance is a new shelf generation, so the same slot/operator
    // pair legitimately counts again exactly once.
    counter.apply(observation(70, 1, 1, 1, {a, b}));
    counter.apply(observation(70, 1, 1, 1, {a, b}));
    CHECK(frequency_total(counter, "char-a") == 2);
    CHECK(frequency_total(counter, "char-b") == 2);
    CHECK(frequency_total(counter, "char-c") == 1);
    std::puts("PASS constant refresh sequence never re-counts seen identities; "
              "true advance starts a new generation");
}

void test_missing_sequence_to_known_sequence_dedup() {
    ShopCounter counter;
    const auto a = good(0, "char-a", "干员甲");
    const auto b = good(1, "char-b", "干员乙");
    const auto c = good(1, "char-c", "干员丙");

    counter.apply(observation_without_sequence(80, 1, 1, {a, b}));
    counter.apply(observation_without_sequence(80, 1, 1, {a, b}));
    CHECK(frequency_total(counter, "char-a") == 1);
    CHECK(frequency_total(counter, "char-b") == 1);
    CHECK(counter.incomplete());

    // Acquiring the sequence later synchronizes with the already counted shelf.
    counter.apply(observation(80, 1, 1, 5, {a, b}));
    CHECK(frequency_total(counter, "char-a") == 1);
    CHECK(frequency_total(counter, "char-b") == 1);

    counter.apply(observation(80, 1, 1, 6, {a, c}));
    CHECK(frequency_total(counter, "char-a") == 2);
    CHECK(frequency_total(counter, "char-b") == 1);
    CHECK(frequency_total(counter, "char-c") == 1);
    std::puts("PASS missing-to-known sequence transition does not duplicate the synchronized shelf");
}

void test_stale_sequence_rollback_is_ignored() {
    ShopCounter counter;
    const auto a = good(0, "char-a", "干员甲");
    const auto b = good(1, "char-b", "干员乙");
    const auto c = good(0, "char-c", "干员丙");
    const auto d = good(1, "char-d", "干员丁");

    counter.apply(observation(90, 1, 1, 5, {a, b}));
    counter.apply(observation(90, 1, 1, 4, {c, d}));
    CHECK(counter.incomplete());
    CHECK(frequency_total(counter, "char-a") == 1);
    CHECK(frequency_total(counter, "char-b") == 1);
    CHECK(frequency_total(counter, "char-c") == 0);
    CHECK(frequency_total(counter, "char-d") == 0);

    // The stale snapshot must not replace the newer baseline.
    counter.apply(observation(90, 1, 1, 5, {a, b}));
    CHECK(frequency_total(counter, "char-a") == 1);
    CHECK(frequency_total(counter, "char-b") == 1);

    counter.apply(observation(90, 1, 1, 7, {a, b}));
    CHECK(frequency_total(counter, "char-a") == 2);
    CHECK(frequency_total(counter, "char-b") == 2);

    counter.apply(observation(90, 1, 1, 6, {c, d}));
    CHECK(frequency_total(counter, "char-c") == 0);
    CHECK(frequency_total(counter, "char-d") == 0);
    CHECK(counter.frequencies(false).size() == 2);
    std::puts("PASS stale refresh-sequence rollbacks are diagnosed and ignored without baseline regression");
}

void test_late_names_update_without_counting_again() {
    ShopCounter counter;
    auto item = good(0, "char_example", "char_example");
    item.bond_ids = {"bond_example"};
    item.bond_names = {"bond_example"};
    auto first = observation(91, 1, 1, 1, {item});
    first.owned_available = true;
    IntelChessRecord owned;
    owned.chess_id = "char_example";
    owned.count = 1;
    first.owned.push_back(owned);
    counter.apply(first);
    CHECK(counter.frequencies(false).front().label == "干员名称未读取");
    CHECK(counter.frequencies(true).front().label == "盟约名称未读取");
    CHECK(counter.player_operators().front().label == "干员名称未读取");

    item.operator_name = "示例干员";
    item.bond_names = {"示例盟约"};
    counter.apply(observation(91, 1, 1, 1, {item}));
    counter.apply(first);
    CHECK(counter.frequencies(false).front().label == "示例干员");
    CHECK(counter.frequencies(false).front().total == 1);
    CHECK(counter.frequencies(true).front().label == "示例盟约");
    CHECK(counter.frequencies(true).front().total == 1);
    CHECK(counter.player_operators().front().label == "示例干员");
}

void test_frozen_goods_carry_into_the_next_round() {
    for (bool has_refresh_seq : {false, true}) {
        ShopCounter counter;
        auto a = good(0, "char-a", "干员甲");
        a.bond_ids = {"bond-a"};
        const auto b = good(1, "char-b", "干员乙");
        const auto sample = [&](int session, int round, int player, int seq,
                                std::vector<IntelShopGood> goods) {
            auto snapshot = observation(session, round, player, seq, std::move(goods));
            snapshot.refresh_seq_available = has_refresh_seq;
            counter.apply(snapshot);
        };

        sample(101, 1, 1, 8, {a, b});
        a.frozen = true;
        sample(101, 1, 1, 8, {a, b});
        // The opening shelf may arrive in pieces. The same unfrozen operator
        // can legitimately appear again even when the round resets refreshSeq.
        sample(101, 2, 1, 0, {b});
        sample(101, 2, 1, 0, {a, b});
        sample(101, 2, 1, 0, {a, b});
        sample(101, 3, 1, 0, {a});
        CHECK(frequency_total(counter, "char-a") == 1);
        CHECK(frequency_total(counter, "bond-a", true) == 1);
        CHECK(frequency_total(counter, "char-b") == 2);

        sample(101, 3, 2, 0, {a}); // A different player's first observation.
        auto moved = a;
        moved.slot_id = 2;
        sample(101, 4, 1, 0, {moved}); // The original slot did not persist.
        CHECK(frequency_total(counter, "char-a") == 3);
        CHECK(frequency_total(counter, "bond-a", true) == 3);
        sample(101, 6, 1, 0, {moved}); // The intervening round was unobserved.
        CHECK(frequency_total(counter, "char-a") == 4);

        auto unfrozen = moved;
        unfrozen.frozen = false;
        sample(101, 7, 1, 0, {unfrozen});
        CHECK(frequency_total(counter, "char-a") == 5);
        sample(102, 8, 1, 0, {moved});
        CHECK(frequency_total(counter, "char-a") == 1);
    }
    std::puts("PASS cross-round frozen shop retention / partial opening shelf / slot and player isolation");
}

void test_cross_round_freeze_baseline_expires() {
    const auto frozen = good(0, "char-a", "干员甲", true);
    const auto replacement = good(0, "char-b", "干员乙");

    ShopCounter with_sequence;
    with_sequence.apply(observation(103, 1, 1, 9, {frozen}));
    with_sequence.apply(observation(103, 2, 1, 0, {}));
    with_sequence.apply(observation(103, 2, 1, 1, {frozen}));
    CHECK(frequency_total(with_sequence, "char-a") == 2);

    ShopCounter without_sequence;
    without_sequence.apply(observation_without_sequence(104, 1, 1, {frozen}));
    without_sequence.apply(observation_without_sequence(104, 2, 1, {}));
    // A late previous-round snapshot cannot change the opening comparison.
    without_sequence.apply(observation_without_sequence(104, 1, 1, {}));
    without_sequence.apply(observation_without_sequence(104, 2, 1, {frozen}));
    CHECK(frequency_total(without_sequence, "char-a") == 1);
    // Once observed, the carryover must not suppress any subsequent appearance.
    without_sequence.apply(observation_without_sequence(104, 2, 1, {replacement}));
    without_sequence.apply(observation_without_sequence(104, 2, 1, {frozen}));
    CHECK(frequency_total(without_sequence, "char-a") == 2);
    CHECK(frequency_total(without_sequence, "char-b") == 1);
    std::puts("PASS stable opening baseline expires on refresh or observed slot replacement");
}
}

int main() {
    test_shelf_slots_bonds_and_frozen_goods();
    test_frequency_counts_and_order_follow_player_scope();
    test_acquisition_shop_link_sales_and_session_reset();
    test_supported_player_slots();
    test_unresolved_operator_is_still_counted();
    test_protocol_slot_normalization_helper();
    test_shelf_change_fallback_without_refresh_sequence();
    test_polling_and_callback_same_sequence_dedup();
    test_constant_sequence_dedup_and_real_advance();
    test_missing_sequence_to_known_sequence_dedup();
    test_stale_sequence_rollback_is_ignored();
    test_late_names_update_without_counting_again();
    test_frozen_goods_carry_into_the_next_round();
    test_cross_round_freeze_baseline_expires();
    return failures ? 1 : 0;
}
