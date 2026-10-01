#include "gtest/gtest.h"

#include "Interval.hpp"
#include "discrete/PersistentAntennaSet.hpp"

#include <memory>
#include <span>
#include <vector>

namespace {

// Building perimeters are normalized to 1, so coverage == covered length and
// every interval lives in [0, 1].
constexpr double kPerimeter = 1.0;
constexpr double kMinCoverage = 0.5;

// Building ids used in the scenarios.
constexpr size_t kB1 = 1;
constexpr size_t kB2 = 2;
constexpr size_t kB3 = 3;

using AntennaId = PersistentAntennaSet::AntennaId;
using Ids = std::vector<AntennaId>;

// Keeps test-data vectors alive so the spans that reference them don't dangle.
// (Antenna::covered is a std::span, so its storage has to outlive the antenna.)
class SpanArena {
public:
    template <typename T>
    std::span<const T> store(std::vector<T> values) {
        auto held = std::make_shared<std::vector<T>>(std::move(values));
        std::span<const T> span(*held);
        storage_.push_back(std::move(held));
        return span;
    }

private:
    std::vector<std::shared_ptr<void>> storage_;
};

CoveredBuilding covers(size_t building_id, std::vector<Interval> intervals) {
    return CoveredBuilding{
        .id = building_id,
        .intervals = std::move(intervals),
    };
}

Antenna antenna(SpanArena &arena, size_t id, std::vector<CoveredBuilding> covered) {
    return Antenna{
        .id = id,
        .covered = arena.store(std::move(covered)),
    };
}

// Persistent analogues of DynamicAntennaSet::add/removeAntenna: take a set by
// value (cheap, just shared_ptr copies), fold in / out the antenna's singleton,
// and return the resulting set. The input is left untouched.
PersistentAntennaSet withAntenna(PersistentAntennaSet set, const Antenna &a, double min_cov) {
    return set.add(PersistentAntennaSet::singleton(a, min_cov), min_cov);
}

PersistentAntennaSet withoutAntenna(PersistentAntennaSet set, const Antenna &a, double min_cov) {
    return set.remove(PersistentAntennaSet::singleton(a, min_cov), min_cov);
}

} // namespace

// Mirror of DynamicAntennaSet.TracksCoveredBuildingsAcrossAddRemove.
//
// Two antennas, three buildings (perimeter 1 each), min coverage 0.5:
//
//   Antenna A (id 1): B1 -> [0,0.6] (0.6), B2 -> [0,0.3] (0.3)
//   Antenna B (id 2): B2 -> [0.3,0.8],     B3 -> [0,0.7] (0.7)
//
// B2 is only covered once *both* antennas are present: [0,0.3] U [0.3,0.8] =
// [0,0.8] gives 0.8 >= 0.5, whereas A's piece alone is just 0.3.
TEST(PersistentAntennaSet, TracksCoveredBuildingsAcrossAddRemove) {
    SpanArena arena;
    const Antenna a = antenna(arena, 1, {
        covers(kB1, {{0.0, 0.6}}),
        covers(kB2, {{0.0, 0.3}}),
    });
    const Antenna b = antenna(arena, 2, {
        covers(kB2, {{0.3, 0.8}}),
        covers(kB3, {{0.0, 0.7}}),
    });

    PersistentAntennaSet set; // empty
    EXPECT_EQ(set.getCoverage().n_covered, 0u);
    EXPECT_EQ(set.getAntennaIds(), (Ids{}));

    // Add A: only B1 (0.6) clears the threshold; B2 sits at 0.3.
    set = withAntenna(set, a, kMinCoverage);
    EXPECT_EQ(set.getCoverage().n_covered, 1u);
    EXPECT_EQ(set.getAntennaIds(), (Ids{1}));

    // Add B: B2 jumps to 0.8 (now covered) and B3 at 0.7 (covered).
    set = withAntenna(set, b, kMinCoverage);
    EXPECT_EQ(set.getCoverage().n_covered, 3u);
    EXPECT_EQ(set.getAntennaIds(), (Ids{1, 2}));

    // Remove B: B2 falls back to 0.3 (uncovered), B3 drops to 0. Only B1 left.
    set = withoutAntenna(set, b, kMinCoverage);
    EXPECT_EQ(set.getCoverage().n_covered, 1u);
    EXPECT_EQ(set.getAntennaIds(), (Ids{1}));

    // Remove A: nothing left covered.
    set = withoutAntenna(set, a, kMinCoverage);
    EXPECT_EQ(set.getCoverage().n_covered, 0u);
    EXPECT_EQ(set.getAntennaIds(), (Ids{}));
}

// Mirror of DynamicAntennaSet.CoverageWithBuildingSplitAcrossLayers.
//
// B1 is incrementally covered by three antennas added in separate steps, while
// B2 is covered by one. B1 must be counted exactly once throughout (min coverage
// 0.5, perimeter 1):
//
//   add A1{B1:[0,0.3]}              -> B1 = 0.3 (uncovered)
//   add A2{B1:[0.3,0.8],B2:[0,0.6]} -> B1 = 0.8 (covered), B2 = 0.6 (covered)
//   add A3{B1:[0.8,1.0]}            -> B1 = 1.0, still just B1 and B2 covered
TEST(PersistentAntennaSet, CoverageWithBuildingSplitAcrossLayers) {
    SpanArena arena;
    const Antenna a1 = antenna(arena, 1, {
        covers(kB1, {{0.0, 0.3}}),
    });
    const Antenna a2 = antenna(arena, 2, {
        covers(kB1, {{0.3, 0.8}}),
        covers(kB2, {{0.0, 0.6}}),
    });
    const Antenna a3 = antenna(arena, 3, {
        covers(kB1, {{0.8, 1.0}}),
    });

    PersistentAntennaSet set;

    set = withAntenna(set, a1, kMinCoverage);
    EXPECT_EQ(set.getCoverage().n_covered, 0u); // B1 at 0.3 only
    EXPECT_EQ(set.getAntennaIds(), (Ids{1}));

    set = withAntenna(set, a2, kMinCoverage);
    EXPECT_EQ(set.getCoverage().n_covered, 2u); // B1 -> 0.8, B2 -> 0.6
    EXPECT_EQ(set.getAntennaIds(), (Ids{1, 2}));

    set = withAntenna(set, a3, kMinCoverage);
    // B1 (now fully covered) and B2: still exactly two covered buildings.
    EXPECT_EQ(set.getCoverage().n_covered, 2u);
    EXPECT_EQ(set.getAntennaIds(), (Ids{1, 2, 3}));
}

// Mirror of DynamicAntennaSet.BulkConstructorWithMultiAntennaBuildings, folding
// singletons instead of using a bulk constructor. The antennas are folded out of
// id order (a1=5 before a2=2) to exercise antenna_id sorting in getAntennaIds;
// per contract each antenna's `covered` is sorted by building_id.
//
//   B1: [0,0.2] (a2) U [0.2,0.5] (a1)  = [0,0.5] -> 0.5  (covered, exactly tau)
//   B2: [0,0.6] (a2)                    = [0,0.6] -> 0.6  (covered)
//   B3: [0.4,0.7] (a2) U [0,0.4] (a1)  = [0,0.7] -> 0.7  (covered)
//   B4: [0.6,0.8] (a1)                  = [0.6,0.8] -> 0.2  (uncovered)
//
// n_covered = 3, total = 0.5 + 0.6 + 0.7 + 0.2 = 2.0
TEST(PersistentAntennaSet, MultiAntennaBuildings) {
    SpanArena arena;
    constexpr size_t kB4 = 4;
    const Antenna a1 = antenna(arena, 5, {
        covers(kB1, {{0.2, 0.5}}),
        covers(kB3, {{0.0, 0.4}}),
        covers(kB4, {{0.6, 0.8}}),
    });
    const Antenna a2 = antenna(arena, 2, {
        covers(kB1, {{0.0, 0.2}}),
        covers(kB2, {{0.0, 0.6}}),
        covers(kB3, {{0.4, 0.7}}),
    });

    PersistentAntennaSet set;
    set = withAntenna(set, a1, kMinCoverage);
    set = withAntenna(set, a2, kMinCoverage);

    EXPECT_EQ(set.getCoverage().n_covered, 3u);
    EXPECT_DOUBLE_EQ(set.getCoverage().total, 2.0);
    EXPECT_EQ(set.getAntennaIds(), (Ids{2, 5}));

    // Folding in the opposite order must produce the same set.
    PersistentAntennaSet reversed;
    reversed = withAntenna(reversed, a2, kMinCoverage);
    reversed = withAntenna(reversed, a1, kMinCoverage);
    EXPECT_EQ(reversed.getCoverage().n_covered, set.getCoverage().n_covered);
    EXPECT_DOUBLE_EQ(reversed.getCoverage().total, set.getCoverage().total);
    EXPECT_EQ(reversed.getAntennaIds(), set.getAntennaIds());
}

// Mirror of DynamicAntennaSet.BulkConstructorScalesAcrossManyBuildings: enough
// buildings to exercise a deeper trie. Two antennas cover every building; the
// second only reaches even-id buildings, so exactly half clear tau.
//
//   even i: [0,0.3] U [0.3,0.8] = [0,0.8] -> 0.8 (covered)
//   odd  i: [0,0.3]            -> 0.3          (uncovered)
//
// With N buildings: n_covered = N/2, total = (N/2)*0.8 + (N/2)*0.3.
TEST(PersistentAntennaSet, ScalesAcrossManyBuildings) {
    SpanArena arena;
    constexpr size_t kN = 20;

    std::vector<CoveredBuilding> base_cover;
    std::vector<CoveredBuilding> extra_cover;
    for (size_t id = 0; id < kN; ++id) {
        base_cover.push_back(covers(id, {{0.0, 0.3}}));
        if (id % 2 == 0)
            extra_cover.push_back(covers(id, {{0.3, 0.8}}));
    }
    const Antenna base_antenna = antenna(arena, 100, std::move(base_cover));
    const Antenna extra_antenna = antenna(arena, 101, std::move(extra_cover));

    PersistentAntennaSet set;
    set = withAntenna(set, base_antenna, kMinCoverage);
    set = withAntenna(set, extra_antenna, kMinCoverage);

    EXPECT_EQ(set.getCoverage().n_covered, kN / 2);
    EXPECT_DOUBLE_EQ(set.getCoverage().total, (kN / 2) * 0.8 + (kN / 2) * 0.3);
    EXPECT_EQ(set.getAntennaIds(), (Ids{100, 101}));
}

// Persistence: add() yields a new set without disturbing the one it was derived
// from. The earlier handle must keep reporting its own coverage and ids, and
// removing the just-added antenna must reproduce that earlier set.
TEST(PersistentAntennaSet, AddLeavesEarlierVersionsIntact) {
    SpanArena arena;
    const Antenna a = antenna(arena, 1, {
        covers(kB1, {{0.0, 0.6}}),
        covers(kB2, {{0.0, 0.3}}),
    });
    const Antenna b = antenna(arena, 2, {
        covers(kB2, {{0.3, 0.8}}),
        covers(kB3, {{0.0, 0.7}}),
    });

    const PersistentAntennaSet just_a = withAntenna(PersistentAntennaSet{}, a, kMinCoverage);
    const PersistentAntennaSet a_and_b = withAntenna(just_a, b, kMinCoverage);

    // just_a is unchanged by deriving a_and_b from it.
    EXPECT_EQ(just_a.getCoverage().n_covered, 1u);
    EXPECT_EQ(just_a.getAntennaIds(), (Ids{1}));

    // a_and_b is the extended version.
    EXPECT_EQ(a_and_b.getCoverage().n_covered, 3u);
    EXPECT_EQ(a_and_b.getAntennaIds(), (Ids{1, 2}));

    // Removing b from a_and_b reproduces the earlier set.
    const PersistentAntennaSet back_to_a = withoutAntenna(a_and_b, b, kMinCoverage);
    EXPECT_EQ(back_to_a.getCoverage().n_covered, just_a.getCoverage().n_covered);
    EXPECT_DOUBLE_EQ(back_to_a.getCoverage().total, just_a.getCoverage().total);
    EXPECT_EQ(back_to_a.getAntennaIds(), just_a.getAntennaIds());
}
