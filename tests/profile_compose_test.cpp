// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

// Profile-merge composition tests: the pure base-anchored merge the engine and
// the editor must resolve identically. Focus: natural source order, duplicate
// preservation with per-instance provenance, the order-override claim rules
// (self-healing by value+source, unmatched dropped, unlisted appended), and the
// runtime value projection (duplicates kept, empty bases dropped).

#include "core/profile_compose.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace schnelle_zeichen;

static int failures = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);        \
            ++failures;                                                        \
        }                                                                      \
    } while (0)

namespace {

// Plain C-string constants: static-duration std::string would trip
// bugprone-throwing-static-initialization; these feed only into std::string
// fields, which construct from them implicitly.
constexpr const char *kBaseRef = "mappings.txt";
constexpr const char *kEmojiRef = "profiles/emoji.txt";
constexpr const char *kMathRef = "profiles/math.txt";

// ---------------------------------------------------------- composeBase

// Natural order: every source in click order, all its variants for the base,
// each instance tagged with its source File.
void testComposeBaseNaturalOrder() {
    const VariantMap base{{"a", {"ä", "á"}}};
    const VariantMap emoji{{"a", {"@"}}};
    const std::vector<ComposeSource> sources{{kBaseRef, &base},
                                             {kEmojiRef, &emoji}};
    const std::vector<Variant> out = composeBase("a", sources, nullptr);
    const std::vector<Variant> want{
        {"ä", kBaseRef}, {"á", kBaseRef}, {"@", kEmojiRef}};
    CHECK(out == want);
}

// A base absent from a source contributes nothing; a null map is skipped.
void testComposeBaseMissingAndNullSources() {
    const VariantMap base{{"a", {"ä"}}};
    const std::vector<ComposeSource> sources{
        {kBaseRef, &base}, {kEmojiRef, nullptr}, {kMathRef, &base}};
    const std::vector<Variant> out = composeBase("a", sources, nullptr);
    const std::vector<Variant> want{{"ä", kBaseRef}, {"ä", kMathRef}};
    CHECK(out == want);
    // A base present in no source yields an empty list.
    CHECK(composeBase("z", sources, nullptr).empty());
}

// Duplicates are preserved with distinct provenance: the same value from two
// sources yields two instances (a dead cycle slot the editor flags).
void testComposeBaseDuplicatesPreserved() {
    const VariantMap base{{"a", {"ä"}}};
    const VariantMap emoji{{"a", {"ä"}}}; // same value, different source
    const std::vector<ComposeSource> sources{{kBaseRef, &base},
                                             {kEmojiRef, &emoji}};
    const std::vector<Variant> out = composeBase("a", sources, nullptr);
    CHECK(out.size() == 2);
    CHECK((out[0] == Variant{"ä", kBaseRef}));
    CHECK((out[1] == Variant{"ä", kEmojiRef}));
}

// Order override: a best-effort stable reorder. Each override entry claims the
// next unclaimed natural instance with the same (value, sourceRef); the
// remaining naturals append in natural order.
void testComposeBaseOrderOverrideReorders() {
    const VariantMap base{{"a", {"ä", "á", "à"}}};
    const std::vector<ComposeSource> sources{{kBaseRef, &base}};
    const std::vector<Variant> order{{"à", kBaseRef}, {"ä", kBaseRef}};
    const std::vector<Variant> out = composeBase("a", sources, &order);
    const std::vector<Variant> want{
        {"à", kBaseRef}, {"ä", kBaseRef}, {"á", kBaseRef}}; // á appended
    CHECK(out == want);
}

// Override entries that match no natural instance (a stale value or wrong
// source) are dropped; the natural instances still all appear.
void testComposeBaseOrderOverrideUnmatchedDropped() {
    const VariantMap base{{"a", {"ä", "á"}}};
    const std::vector<ComposeSource> sources{{kBaseRef, &base}};
    const std::vector<Variant> order{
        {"á", kBaseRef},     // matches
        {"ä", kEmojiRef},    // wrong source: dropped
        {"gone", kBaseRef}}; // stale value: dropped
    const std::vector<Variant> out = composeBase("a", sources, &order);
    const std::vector<Variant> want{{"á", kBaseRef}, {"ä", kBaseRef}};
    CHECK(out == want);
}

// Each override entry claims only ONE natural instance: with two identical
// naturals and one override entry, the second natural still appends.
void testComposeBaseOrderOverrideClaimsOnce() {
    const VariantMap base{{"a", {"ä"}}};
    const VariantMap emoji{{"a", {"ä"}}};
    const std::vector<ComposeSource> sources{{kBaseRef, &base},
                                             {kEmojiRef, &emoji}};
    const std::vector<Variant> order{{"ä", kEmojiRef}}; // claim the emoji one
    const std::vector<Variant> out = composeBase("a", sources, &order);
    const std::vector<Variant> want{{"ä", kEmojiRef}, {"ä", kBaseRef}};
    CHECK(out == want);
}

// An empty override list is a no-op (natural order stands).
void testComposeBaseEmptyOverrideIsNatural() {
    const VariantMap base{{"a", {"ä", "á"}}};
    const std::vector<ComposeSource> sources{{kBaseRef, &base}};
    const std::vector<Variant> empty;
    CHECK(composeBase("a", sources, &empty) ==
          composeBase("a", sources, nullptr));
}

// ------------------------------------------------------------- compose

// The full map: every base in any source appears once, each composed with its
// own override; a base only in an appended source is still included.
void testComposeFullMap() {
    const VariantMap base{{"a", {"ä"}}, {"o", {"ö"}}};
    const VariantMap emoji{{"a", {"@"}}, {"e", {"€"}}}; // 'e' only here
    const std::vector<ComposeSource> sources{{kBaseRef, &base},
                                             {kEmojiRef, &emoji}};
    OrderOverride overrides;
    const auto out = compose(sources, overrides);
    CHECK(out.size() == 3); // a, o, e
    const std::vector<Variant> aWant{{"ä", kBaseRef}, {"@", kEmojiRef}};
    CHECK(out.at("a") == aWant);
    CHECK((out.at("o") == std::vector<Variant>{{"ö", kBaseRef}}));
    CHECK((out.at("e") == std::vector<Variant>{{"€", kEmojiRef}}));
}

// A per-base override in the full compose is applied to that base only.
void testComposeAppliesPerBaseOverride() {
    const VariantMap base{{"a", {"ä", "á"}}};
    const std::vector<ComposeSource> sources{{kBaseRef, &base}};
    OrderOverride overrides;
    overrides["a"] = {{"á", kBaseRef}};
    const auto out = compose(sources, overrides);
    const std::vector<Variant> want{{"á", kBaseRef}, {"ä", kBaseRef}};
    CHECK(out.at("a") == want);
}

// ---------------------------------------------------------- projectValues

// Instance list -> value list: order and duplicates preserved.
void testProjectValuesInstances() {
    const std::vector<Variant> instances{
        {"ä", kBaseRef}, {"ä", kEmojiRef}, {"á", kBaseRef}};
    const std::vector<std::string> want{"ä", "ä", "á"};
    CHECK(projectValues(instances) == want);
}

// Whole-map projection keeps duplicates and drops a base with no instances.
void testProjectValuesMapDropsEmpty() {
    std::unordered_map<std::string, std::vector<Variant>> composed;
    composed["a"] = {{"ä", kBaseRef}, {"ä", kEmojiRef}};
    composed["o"] = {}; // empty: dropped from the runtime map
    const VariantMap out = projectValues(composed);
    CHECK(out.size() == 1);
    CHECK((out.at("a") == std::vector<std::string>{"ä", "ä"}));
    CHECK(out.find("o") == out.end());
}

} // namespace

int main() {
    testComposeBaseNaturalOrder();
    testComposeBaseMissingAndNullSources();
    testComposeBaseDuplicatesPreserved();
    testComposeBaseOrderOverrideReorders();
    testComposeBaseOrderOverrideUnmatchedDropped();
    testComposeBaseOrderOverrideClaimsOnce();
    testComposeBaseEmptyOverrideIsNatural();
    testComposeFullMap();
    testComposeAppliesPerBaseOverride();
    testProjectValuesInstances();
    testProjectValuesMapDropsEmpty();
    if (failures == 0) {
        std::printf("profile_compose_test: all checks passed\n");
        return 0;
    }
    std::printf("profile_compose_test: %d check(s) failed\n", failures);
    return 1;
}
