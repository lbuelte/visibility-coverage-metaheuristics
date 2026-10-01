#include "gtest/gtest.h"

#include "Interval.hpp"
#include "discrete/AntennaSet.hpp"

#include <memory>
#include <span>
#include <vector>

namespace {

// Building perimeters are normalized to 1, so coverage == covered length and
// every interval lives in [0, 1].
constexpr double kPerimeter = 1.0;
constexpr double kMinCoverage = 0.5;

// Building ids used in the scenario.
constexpr size_t kB1 = 1;
constexpr size_t kB2 = 2;
constexpr size_t kB3 = 3;

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

} // namespace

// Sanity check for the incremental coverage tracking of DynamicAntennaSet.
//
// Two antennas, three buildings (perimeter 1 each), min coverage 0.5:
//
//   Antenna A (id 1): B1 -> [0,0.6] (0.6), B2 -> [0,0.3] (0.3)
//   Antenna B (id 2): B2 -> [0.3,0.8],     B3 -> [0,0.7] (0.7)
//
// B2 is only covered once *both* antennas are present: [0,0.3] U [0.3,0.8] =
// [0,0.8] gives 0.8 >= 0.5, whereas A's piece alone is just 0.3.
TEST(DynamicAntennaSet, TracksCoveredBuildingsAcrossAddRemove) {
    SpanArena arena;
    const Antenna a = antenna(arena, 1, {
        covers(kB1, {{0.0, 0.6}}),
        covers(kB2, {{0.0, 0.3}}),
    });
    const Antenna b = antenna(arena, 2, {
        covers(kB2, {{0.3, 0.8}}),
        covers(kB3, {{0.0, 0.7}}),
    });

    DynamicAntennaSet set(kMinCoverage);
    EXPECT_EQ(set.getCoverage().n_covered, 0u);
    EXPECT_EQ(set.getAntennaIds(), (std::vector<size_t>{}));

    // Add A: only B1 (0.6) clears the threshold; B2 sits at 0.3.
    set.addAntenna(a);
    EXPECT_EQ(set.getCoverage().n_covered, 1u);
    EXPECT_EQ(set.getAntennaIds(), (std::vector<size_t>{1}));

    // Add B: B2 jumps to 0.8 (now covered) and B3 at 0.7 (covered).
    set.addAntenna(b);
    EXPECT_EQ(set.getCoverage().n_covered, 3u);
    EXPECT_EQ(set.getAntennaIds(), (std::vector<size_t>{1, 2}));

    // Remove B: B2 falls back to 0.3 (uncovered), B3 drops to 0. Only B1 left.
    set.removeAntenna(b);
    EXPECT_EQ(set.getCoverage().n_covered, 1u);
    EXPECT_EQ(set.getAntennaIds(), (std::vector<size_t>{1}));

    // Remove A: nothing left covered.
    set.removeAntenna(a);
    EXPECT_EQ(set.getCoverage().n_covered, 0u);
    EXPECT_EQ(set.getAntennaIds(), (std::vector<size_t>{}));
}

// B1 is incrementally covered by three antennas added in separate steps, while
// B2 is covered by one. B1 must be counted exactly once throughout (min coverage
// 0.5, perimeter 1):
//
//   add A1{B1:[0,0.3]}            -> B1 = 0.3 (uncovered)
//   add A2{B1:[0.3,0.8],B2:[0,0.6]} -> B1 = 0.8 (covered), B2 = 0.6 (covered)
//   add A3{B1:[0.8,1.0]}         -> B1 = 1.0, still just B1 and B2 covered
TEST(DynamicAntennaSet, CoverageWithBuildingSplitAcrossLayers) {
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

    DynamicAntennaSet set(kMinCoverage);

    set.addAntenna(a1);
    EXPECT_EQ(set.getCoverage().n_covered, 0u); // B1 at 0.3 only
    EXPECT_EQ(set.getAntennaIds(), (std::vector<size_t>{1}));

    set.addAntenna(a2);
    EXPECT_EQ(set.getCoverage().n_covered, 2u); // B1 -> 0.8, B2 -> 0.6
    EXPECT_EQ(set.getAntennaIds(), (std::vector<size_t>{1, 2}));

    set.addAntenna(a3);
    // B1 (now fully covered) and B2: still exactly two covered buildings.
    EXPECT_EQ(set.getCoverage().n_covered, 2u);
    EXPECT_EQ(set.getAntennaIds(), (std::vector<size_t>{1, 2, 3}));
}

// The bulk constructor that takes a whole span of antennas at once must agree
// with adding those antennas one by one. The scenario mixes a building that
// only clears the threshold once two antennas overlap (B2) with one that stays
// below it (B3 at 0.3), so both n_covered and the running total are exercised.
TEST(DynamicAntennaSet, BulkConstructorMatchesIncrementalAdds) {
    SpanArena arena;
    const Antenna a = antenna(arena, 1, {
        covers(kB1, {{0.0, 0.6}}), // 0.6, covered on its own
        covers(kB2, {{0.0, 0.3}}), // 0.3 alone
    });
    const Antenna b = antenna(arena, 2, {
        covers(kB2, {{0.3, 0.8}}), // with A: [0,0.8] -> 0.8, covered
        covers(kB3, {{0.0, 0.3}}), // 0.3, stays uncovered
    });

    // Reference: add the antennas incrementally.
    DynamicAntennaSet incremental(kMinCoverage);
    incremental.addAntenna(a);
    incremental.addAntenna(b);
    EXPECT_EQ(incremental.getCoverage().n_covered, 2u); // B1 and B2

    // Build the same set in one shot.
    const auto antennas = arena.store(std::vector<Antenna>{a, b});
    DynamicAntennaSet bulk(kMinCoverage, antennas);

    EXPECT_EQ(bulk.getCoverage().n_covered, incremental.getCoverage().n_covered);
    EXPECT_DOUBLE_EQ(bulk.getCoverage().total, incremental.getCoverage().total);
    EXPECT_EQ(bulk.getAntennaIds(), (std::vector<size_t>{1, 2}));
    EXPECT_EQ(bulk.getAntennaIds(), incremental.getAntennaIds());
}


// A building covered by the union of several antennas. The antenna span itself
// is given out of id order (a1=5 before a2=2) to exercise antenna_id sorting;
// per contract each antenna's `covered` is sorted by building_id.
//
//   B1: [0,0.2] (a2) U [0.2,0.5] (a1)      = [0,0.5] -> 0.5  (covered, exactly tau)
//   B2: [0,0.6] (a2)                        = [0,0.6] -> 0.6  (covered)
//   B3: [0.4,0.7] (a2) U [0,0.4] (a1)      = [0,0.7] -> 0.7  (covered)
//   B4: [0.6,0.8] (a1)                      = [0.6,0.8] -> 0.2  (uncovered)
//
// n_covered = 3, total = 0.5 + 0.6 + 0.7 + 0.2 = 2.0
TEST(DynamicAntennaSet, BulkConstructorWithMultiAntennaBuildings) {
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

    const auto antennas = arena.store(std::vector<Antenna>{a1, a2});
    DynamicAntennaSet bulk(kMinCoverage, antennas);

    // Absolute expectations (independent of the incremental path).
    EXPECT_EQ(bulk.getCoverage().n_covered, 3u);
    EXPECT_DOUBLE_EQ(bulk.getCoverage().total, 2.0);
    EXPECT_EQ(bulk.getAntennaIds(), (std::vector<size_t>{2, 5}));

    // And it must agree with adding the same antennas incrementally.
    DynamicAntennaSet incremental(kMinCoverage);
    incremental.addAntenna(a1);
    incremental.addAntenna(a2);
    EXPECT_EQ(bulk.getCoverage().n_covered, incremental.getCoverage().n_covered);
    EXPECT_DOUBLE_EQ(bulk.getCoverage().total, incremental.getCoverage().total);
}

// Exercises the bulk path with enough buildings that the base AntennaSet lands
// on a high layer index (std::bit_width(n_buildings)). Two antennas cover every
// building; the second only reaches even-id buildings, so exactly half clear tau.
//
//   even i: [0,0.3] U [0.3,0.8] = [0,0.8] -> 0.8 (covered)
//   odd  i: [0,0.3]            -> 0.3          (uncovered)
//
// With N buildings: n_covered = N/2, total = (N/2)*0.8 + (N/2)*0.3.
TEST(DynamicAntennaSet, BulkConstructorScalesAcrossManyBuildings) {
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

    const auto antennas = arena.store(std::vector<Antenna>{base_antenna, extra_antenna});
    DynamicAntennaSet bulk(kMinCoverage, antennas);

    EXPECT_EQ(bulk.getCoverage().n_covered, kN / 2);
    EXPECT_DOUBLE_EQ(bulk.getCoverage().total, (kN / 2) * 0.8 + (kN / 2) * 0.3);
    EXPECT_EQ(bulk.getAntennaIds(), (std::vector<size_t>{100, 101}));

    DynamicAntennaSet incremental(kMinCoverage);
    incremental.addAntenna(base_antenna);
    incremental.addAntenna(extra_antenna);
    EXPECT_EQ(bulk.getCoverage().n_covered, incremental.getCoverage().n_covered);
    EXPECT_DOUBLE_EQ(bulk.getCoverage().total, incremental.getCoverage().total);
}
