#include "gtest/gtest.h"

#include <algorithm>
#include <iomanip>
#include <random>
#include <set>
#include <span>
#include <utility>
#include <vector>

#include "DCNS2.hpp"
#include "DiscreteCoverageInstance.hpp"
#include "Interval.hpp"

namespace {

// The input format of the synthetic DiscreteCoverageInstance constructor:
// per antenna, a list of (building, covered intervals).
using Coverages = std::vector<std::vector<std::pair<unsigned, std::vector<std::pair<double, double>>>>>;

constexpr double THRESHOLD = 0.5;

// The instance from local_search_data_structure_test.cpp: 3 buildings, 4 antennas.
const Coverages SMALL_COVERAGES = {
    { // Antenna 0
        {0, {{0.0, 0.2}, {0.8, 1.0}}},
        {1, {{0.2, 0.6}}},
        {2, {{0.0, 0.5}}},
    },
    { // Antenna 1
        {0, {{0.1, 0.4}}},
        {1, {{0.8, 1.0}}},
        {2, {{0.5, 0.8}}},
    },
    { // Antenna 2
        {0, {{0.3, 0.9}}},
        {1, {{0.0, 0.1}, {0.2, 0.3}, {0.5, 0.9}}},
        {2, {{0.8, 1.0}}},
    },
    { // Antenna 3
        {0, {{0.1, 0.2}, {0.6, 0.8}}},
        {1, {{0.3, 0.6}}},
        {2, {{0.4, 0.9}}},
    }
};

void expectCoverage(Coverage actual, int serviced, double total) {
    EXPECT_EQ(actual.serviced, serviced);
    EXPECT_DOUBLE_EQ(actual.total, total);
}

// Recomputed from scratch, the way the rest of the codebase evaluates a solution.
// The incrementally maintained coverage of DCNS2 must always agree with it.
Coverage referenceCoverage(const DiscreteCoverageInstance &instance,
    const std::set<AntennaId> &antennas, double threshold)
{
    std::vector<std::vector<Interval>> per_building(instance.get_number_polygons());
    for (const AntennaId antenna : antennas)
        for (const auto &poly_cov : instance.antenna_covers_polygons()[antenna])
            for (const auto &interval : poly_cov.intervals)
                per_building[poly_cov.polygon].push_back(interval);

    Coverage res;
    for (auto &intervals : per_building) {
        std::sort(intervals.begin(), intervals.end());
        const double total = computeUnionLength(intervals);
        res.serviced += total >= threshold;
        res.total += std::min(total, threshold);
    }
    return res;
}

// Endpoints are multiples of 1/8, so every length, difference and partial sum below is
// exact in double: the incremental result must match the reference bit for bit, and
// comparisons against the threshold cannot be decided by rounding noise.
// The coarse grid also produces plenty of touching and empty intervals, which is what
// the DCI2 preprocessing has to clean up.
//
// Like the real coverage of an antenna, the intervals of one antenna on one building are
// generated pairwise disjoint (they stem from distinct polygon edges), but they may touch
// and may be empty, so that DCI2 has both cases to clean up.
Coverages randomCoverages(std::mt19937 &rng, unsigned n_antennas, unsigned n_buildings) {
    constexpr int STEPS = 8;
    std::uniform_int_distribution<int> point_dist(0, STEPS);
    std::uniform_int_distribution<int> count_dist(1, 3);

    Coverages coverages(n_antennas);
    for (unsigned antenna = 0; antenna < n_antennas; ++antenna) {
        for (unsigned building = 0; building < n_buildings; ++building) {
            if (rng() % 3 == 0) // an antenna only sees some of the buildings
                continue;
            // 2n sorted grid points, paired up into n disjoint intervals.
            const int count = count_dist(rng);
            std::vector<int> points;
            for (int i = 0; i < 2 * count; ++i)
                points.push_back(point_dist(rng));
            std::sort(points.begin(), points.end());

            std::vector<std::pair<double, double>> intervals;
            for (int i = 0; i < count; ++i)
                intervals.emplace_back((double) points[2 * i] / STEPS,
                    (double) points[2 * i + 1] / STEPS);
            // The order within one antenna is arbitrary in the real instance, too.
            std::shuffle(intervals.begin(), intervals.end(), rng);
            coverages[antenna].emplace_back(building, std::move(intervals));
        }
    }
    return coverages;
}

// The counterpart to randomCoverages(): endpoints are arbitrary doubles, so no length,
// difference or partial sum is exact and a building's coverage can land arbitrarily close
// to the threshold. That is the case real coverage produces and the grid above excludes,
// and it is the only way to catch a >= threshold decision being settled by rounding.
Coverages randomCoveragesOffGrid(std::mt19937 &rng, unsigned n_antennas, unsigned n_buildings) {
    std::uniform_real_distribution<double> point_dist(0.0, 1.0);
    std::uniform_int_distribution<int> count_dist(1, 3);

    Coverages coverages(n_antennas);
    for (unsigned antenna = 0; antenna < n_antennas; ++antenna) {
        for (unsigned building = 0; building < n_buildings; ++building) {
            if (rng() % 3 == 0) // an antenna only sees some of the buildings
                continue;
            const int count = count_dist(rng);
            std::vector<double> points;
            for (int i = 0; i < 2 * count; ++i)
                points.push_back(point_dist(rng));
            std::sort(points.begin(), points.end());

            std::vector<std::pair<double, double>> intervals;
            for (int i = 0; i < count; ++i)
                intervals.emplace_back(points[2 * i], points[2 * i + 1]);
            std::shuffle(intervals.begin(), intervals.end(), rng);
            coverages[antenna].emplace_back(building, std::move(intervals));
        }
    }
    return coverages;
}

// The union length of one building under `antennas`, computed the way the final solution
// is scored: collect, sort, sweep. DCNS2 instead maintains it incrementally.
double buildingCoverage(const DiscreteCoverageInstance &instance,
    const std::set<AntennaId> &antennas, unsigned building)
{
    std::vector<Interval> intervals;
    for (const AntennaId antenna : antennas)
        for (const auto &poly_cov : instance.antenna_covers_polygons()[antenna])
            if (poly_cov.polygon == building)
                intervals.insert(intervals.end(),
                    poly_cov.intervals.begin(), poly_cov.intervals.end());
    std::sort(intervals.begin(), intervals.end());
    return computeUnionLength(intervals);
}

// What DCI2 has to hand to DCNS2: on each building, the intervals of one antenna are
// non-empty and pairwise disjoint (touching ones have been merged), sorted by start.
// The multiset bookkeeping of DCNS2 relies on it, in particular on the resulting
// distinct starts within one antenna.
void expectCleanedCoverage(const DCI2 &preprocessed, AntennaId antenna) {
    for (const auto &poly_cov : preprocessed.getCoveredBuildings(antenna)) {
        const auto intervals = preprocessed.getIntervals(&poly_cov);
        for (size_t i = 0; i < intervals.size(); ++i) {
            const Interval interval = intervals[i];
            EXPECT_GT(interval.length(), 0) << "antenna " << antenna << ", interval " << i;
            if (i > 0)
                EXPECT_LT(intervals[i - 1].end, interval.start)
                    << "antenna " << antenna << ", interval " << i;
        }
    }
}

} // namespace

TEST(DCNS2Test, emptyStructure) {
    const DiscreteCoverageInstance instance(3, SMALL_COVERAGES);
    const DCNS2 structure(instance, THRESHOLD);

    expectCoverage(structure.getCoverage(), 0, 0.0);
    for (AntennaId antenna = 0; antenna < 4; ++antenna)
        EXPECT_FALSE(structure.has(antenna));
}

TEST(DCNS2Test, addAndRemoveTrackCoverage) {
    const DiscreteCoverageInstance instance(3, SMALL_COVERAGES);
    DCNS2 structure(instance, THRESHOLD);

    // Antenna 0 covers 0.4 / 0.4 / 0.5 of the three buildings, so only the last one
    // reaches the threshold.
    expectCoverage(structure.peek(0), 1, 1.3);
    structure.add(0);
    EXPECT_TRUE(structure.has(0));
    expectCoverage(structure.getCoverage(), 1, 1.3);

    // Antenna 1 pushes the other two buildings over the threshold (0.6 each), which
    // only adds 0.1 apiece to the total because coverage is clamped at the threshold.
    expectCoverage(structure.peek(1), 2, 0.2);
    structure.add(1);
    expectCoverage(structure.getCoverage(), 3, 1.5);

    // peek() must not have modified anything, so removing gets us back exactly.
    structure.remove(1);
    EXPECT_FALSE(structure.has(1));
    expectCoverage(structure.getCoverage(), 1, 1.3);

    structure.remove(0);
    expectCoverage(structure.getCoverage(), 0, 0.0);
    EXPECT_FALSE(structure.has(0));
}

TEST(DCNS2Test, peekPredictsAddForEveryAntenna) {
    const DiscreteCoverageInstance instance(3, SMALL_COVERAGES);

    // Every antenna, on top of every subset of the other ones.
    for (unsigned subset = 0; subset < 16; ++subset) {
        for (AntennaId antenna = 0; antenna < 4; ++antenna) {
            if (subset & (1u << antenna))
                continue;
            DCNS2 structure(instance, THRESHOLD);
            for (AntennaId other = 0; other < 4; ++other)
                if (subset & (1u << other))
                    structure.add(other);

            const Coverage before = structure.getCoverage();
            const Coverage delta = structure.peek(antenna);
            // peek() is a pure query.
            expectCoverage(structure.getCoverage(), before.serviced, before.total);

            structure.add(antenna);
            expectCoverage(structure.getCoverage(), before.serviced + delta.serviced,
                before.total + delta.total);
        }
    }
}

TEST(DCNS2Test, coverageDoesNotDependOnTheOrderOfOperations) {
    const DiscreteCoverageInstance instance(3, SMALL_COVERAGES);
    std::vector<AntennaId> order = {0, 1, 2, 3};

    do {
        DCNS2 structure(instance, THRESHOLD);
        for (const AntennaId antenna : order)
            structure.add(antenna);
        // All four antennas together cover every building beyond the threshold.
        EXPECT_EQ(structure.getCoverage().serviced, 3);
        EXPECT_NEAR(structure.getCoverage().total, 1.5, 1e-12);

        // Removing everything again in the same order must undo it exactly.
        for (const AntennaId antenna : order)
            structure.remove(antenna);
        EXPECT_EQ(structure.getCoverage().serviced, 0);
        EXPECT_NEAR(structure.getCoverage().total, 0.0, 1e-12);
    } while (std::next_permutation(order.begin(), order.end()));
}

TEST(DCNS2Test, clampsAtThresholdAndCountsServicedBuildings) {
    const Coverages coverages = {
        {
            {0, {{0.0, 0.375}}}, // below the threshold
            {1, {{0.0, 0.5}}},   // exactly at the threshold: serviced
            {2, {{0.0, 1.0}}},   // above: contributes only the threshold to the total
        }
    };
    const DiscreteCoverageInstance instance(3, coverages);
    DCNS2 structure(instance, THRESHOLD);

    structure.add(0);
    expectCoverage(structure.getCoverage(), 2, 0.375 + 0.5 + 0.5);
}

TEST(DCNS2Test, antennaWithoutCoverageChangesNothing) {
    const Coverages coverages = {
        {{0, {{0.0, 0.5}}}},
        {},                        // sees no building at all
        {{0, {{0.25, 0.25}}}},     // sees one, but covers nothing of it
    };
    const DiscreteCoverageInstance instance(1, coverages);
    DCNS2 structure(instance, THRESHOLD);

    for (AntennaId antenna : {1u, 2u}) {
        expectCoverage(structure.peek(antenna), 0, 0.0);
        structure.add(antenna);
        EXPECT_TRUE(structure.has(antenna));
        expectCoverage(structure.getCoverage(), 0, 0.0);
    }

    structure.add(0);
    expectCoverage(structure.getCoverage(), 1, 0.5);
    structure.remove(2);
    structure.remove(1);
    expectCoverage(structure.getCoverage(), 1, 0.5);
}

TEST(DCNS2Test, duplicateIntervalsAreRemovedOneInstanceAtATime) {
    // Two antennas covering a building identically: removing one must leave the
    // building covered by the other.
    const Coverages coverages = {
        {{0, {{0.0, 0.5}}}},
        {{0, {{0.0, 0.5}}}},
    };
    const DiscreteCoverageInstance instance(1, coverages);
    DCNS2 structure(instance, THRESHOLD);

    structure.add(0);
    structure.add(1);
    expectCoverage(structure.getCoverage(), 1, 0.5);

    structure.remove(0);
    expectCoverage(structure.getCoverage(), 1, 0.5);

    structure.remove(1);
    expectCoverage(structure.getCoverage(), 0, 0.0);
}

TEST(DCNS2Test, tracksSelectedAntennasAndRecomputesTheSameCoverage) {
    const DiscreteCoverageInstance instance(3, SMALL_COVERAGES);
    DCNS2 structure(instance, THRESHOLD);
    EXPECT_TRUE(structure.getSelectedAntennas().empty());

    structure.add(2);
    structure.add(0);
    structure.add(3);
    EXPECT_EQ(structure.getSelectedAntennas(), (std::vector<AntennaId>{0, 2, 3}));
    structure.remove(2);
    EXPECT_EQ(structure.getSelectedAntennas(), (std::vector<AntennaId>{0, 3}));

    const Coverage incremental = structure.getCoverage();
    structure.recomputeBuildingCoverages();
    expectCoverage(structure.getCoverage(), incremental.serviced, incremental.total);
    expectCoverage(structure.getCoverage(),
        referenceCoverage(instance, {0, 3}, THRESHOLD).serviced,
        referenceCoverage(instance, {0, 3}, THRESHOLD).total);
}

TEST(DCNS2Test, preprocessingSortsMergesAndDropsEmptyIntervals) {
    const Coverages coverages = {
        { // Antenna 0
            // Unsorted, with a run of touching intervals and two empty ones in between.
            {0, {{0.25, 0.5}, {0.5, 0.5}, {0.0, 0.25}, {0.9, 0.9}, {0.5, 0.75}}},
            {1, {{0.5, 0.5}}}, // nothing but an empty interval
        },
        { // Antenna 1: two intervals that do not touch
            {0, {{0.5, 0.75}, {0.0, 0.25}}},
        }
    };
    const DiscreteCoverageInstance instance(2, coverages);
    const DCI2 preprocessed(instance);

    const auto antenna0 = preprocessed.getCoveredBuildings(0);
    ASSERT_EQ(antenna0.size(), 2u);
    EXPECT_EQ(antenna0[0].polygon, 0u);
    // The three touching intervals collapse into one, the empty ones are gone.
    const auto antenna0_building0 = preprocessed.getIntervals(&antenna0[0]);
    ASSERT_EQ(antenna0_building0.size(), 1u);
    EXPECT_EQ(antenna0_building0[0].start, 0.0);
    EXPECT_EQ(antenna0_building0[0].end, 0.75);
    EXPECT_EQ(antenna0[1].polygon, 1u);
    EXPECT_TRUE(preprocessed.getIntervals(&antenna0[1]).empty());

    const auto antenna1 = preprocessed.getCoveredBuildings(1);
    ASSERT_EQ(antenna1.size(), 1u);
    const auto antenna1_building0 = preprocessed.getIntervals(&antenna1[0]);
    ASSERT_EQ(antenna1_building0.size(), 2u);
    EXPECT_EQ(antenna1_building0[0].start, 0.0);
    EXPECT_EQ(antenna1_building0[0].end, 0.25);
    EXPECT_EQ(antenna1_building0[1].start, 0.5);
    EXPECT_EQ(antenna1_building0[1].end, 0.75);
}

TEST(DCNS2Test, mergedIntervalsBehaveLikeTheOriginalOnes) {
    // Antenna 0 reaches the threshold through touching intervals only, antenna 1
    // through a single one; the two must be interchangeable.
    const Coverages coverages = {
        {{0, {{0.0, 0.125}, {0.125, 0.25}, {0.25, 0.5}}}},
        {{0, {{0.0, 0.5}}}},
    };
    const DiscreteCoverageInstance instance(1, coverages);
    DCNS2 structure(instance, THRESHOLD);

    expectCoverage(structure.peek(0), 1, 0.5);
    structure.add(0);
    expectCoverage(structure.getCoverage(), 1, 0.5);

    // Nothing left to gain for the second antenna.
    expectCoverage(structure.peek(1), 0, 0.0);
    structure.add(1);
    expectCoverage(structure.getCoverage(), 1, 0.5);

    structure.remove(0);
    expectCoverage(structure.getCoverage(), 1, 0.5);
    structure.remove(1);
    expectCoverage(structure.getCoverage(), 0, 0.0);
}

// A joint peek is not the sum of the individual peeks: where two antennas overlap on the
// same building, their contributions have to be unioned, not added.
TEST(DCNS2Test, jointPeekUnionsOverlappingAntennasInsteadOfSummingThem) {
    const Coverages coverages = {
        {{0, {{0.0, 0.6}}}}, // Antenna 0
        {{0, {{0.4, 1.0}}}}, // Antenna 1
    };
    const DiscreteCoverageInstance instance(1, coverages);
    DCNS2 structure(instance, THRESHOLD);
    PeekWorkspace workspace;

    const std::vector<AntennaId> both = {0, 1};
    const Coverage joint = structure.peek(std::span<const AntennaId>(both), workspace);

    // The union is [0, 1], so 1.0 clamped to the threshold -- not 0.6 + 0.6.
    EXPECT_EQ(joint.serviced, 1);
    EXPECT_DOUBLE_EQ(joint.total, THRESHOLD);

    const Coverage separate = structure.peek(0) + structure.peek(1);
    EXPECT_GT(separate.total, joint.total) << "the test case must actually overlap";
}

TEST(DCNS2Test, jointPeekMatchesAddingAllAntennas) {
    constexpr unsigned N_ANTENNAS = 10, N_BUILDINGS = 5;
    std::mt19937 rng(20260802);
    PeekWorkspace workspace;

    for (int instance_iter = 0; instance_iter < 50; ++instance_iter) {
        const DiscreteCoverageInstance instance(N_BUILDINGS,
            randomCoverages(rng, N_ANTENNAS, N_BUILDINGS));
        DCNS2 structure(instance, THRESHOLD);

        for (int op = 0; op < 100; ++op) {
            // Keep the current solution moving, so the peeks run against varied states.
            const AntennaId toggle = rng() % N_ANTENNAS;
            if (structure.has(toggle))
                structure.remove(toggle);
            else
                structure.add(toggle);

            std::vector<AntennaId> to_peek;
            for (AntennaId antenna = 0; antenna < N_ANTENNAS; ++antenna)
                if (!structure.has(antenna))
                    to_peek.push_back(antenna);
            if (to_peek.size() < 2)
                continue;
            std::shuffle(to_peek.begin(), to_peek.end(), rng);
            to_peek.resize(2 + rng() % std::min<size_t>(4, to_peek.size() - 1));

            const Coverage before = structure.getCoverage();
            const Coverage delta = structure.peek(std::span<const AntennaId>(to_peek), workspace);

            for (const AntennaId antenna : to_peek)
                structure.add(antenna);
            const Coverage after = structure.getCoverage();
            for (const AntennaId antenna : to_peek)
                structure.remove(antenna);

            ASSERT_EQ(delta.serviced, after.serviced - before.serviced)
                << "instance " << instance_iter << ", op " << op << ", j = " << to_peek.size();
            ASSERT_NEAR(delta.total, after.total - before.total, 1e-9)
                << "instance " << instance_iter << ", op " << op << ", j = " << to_peek.size();

            // Adding and removing the whole set again must leave the state where it was.
            ASSERT_EQ(structure.getCoverage().serviced, before.serviced);
            ASSERT_NEAR(structure.getCoverage().total, before.total, 1e-9);
        }
    }
}

TEST(DCNS2Test, matchesReferenceUnderRandomOperations) {
    constexpr unsigned N_ANTENNAS = 10, N_BUILDINGS = 5;
    std::mt19937 rng(20260801);

    for (int instance_iter = 0; instance_iter < 50; ++instance_iter) {
        const DiscreteCoverageInstance instance(N_BUILDINGS,
            randomCoverages(rng, N_ANTENNAS, N_BUILDINGS));
        const DCI2 preprocessed(instance);
        for (AntennaId antenna = 0; antenna < N_ANTENNAS; ++antenna)
            expectCleanedCoverage(preprocessed, antenna);

        DCNS2 structure(instance, THRESHOLD);
        std::set<AntennaId> present;

        for (int op = 0; op < 200; ++op) {
            const AntennaId antenna = rng() % N_ANTENNAS;
            const Coverage before = structure.getCoverage();
            if (structure.has(antenna)) {
                structure.remove(antenna);
                present.erase(antenna);
            } else {
                const Coverage delta = structure.peek(antenna);
                structure.add(antenna);
                present.insert(antenna);
                ASSERT_EQ(structure.getCoverage().serviced, before.serviced + delta.serviced)
                    << "peek mispredicted at op " << op;
                ASSERT_DOUBLE_EQ(structure.getCoverage().total, before.total + delta.total)
                    << "peek mispredicted at op " << op;
            }
            ASSERT_EQ(structure.has(antenna), present.count(antenna) != 0);

            const Coverage expected = referenceCoverage(instance, present, THRESHOLD);
            ASSERT_EQ(structure.getCoverage().serviced, expected.serviced)
                << "instance " << instance_iter << ", op " << op;
            ASSERT_DOUBLE_EQ(structure.getCoverage().total, expected.total)
                << "instance " << instance_iter << ", op " << op;
        }

        // After 200 incremental updates, the recomputation must not correct anything.
        const Coverage incremental = structure.getCoverage();
        ASSERT_EQ(structure.getSelectedAntennas(),
            std::vector<AntennaId>(present.begin(), present.end()));
        structure.recomputeBuildingCoverages();
        ASSERT_EQ(structure.getCoverage().serviced, incremental.serviced)
            << "instance " << instance_iter;
        ASSERT_DOUBLE_EQ(structure.getCoverage().total, incremental.total)
            << "instance " << instance_iter;
    }
}

// Every rejected simulated annealing proposal is a remove/add round trip: propose_neighbor()
// takes the antennas out, reject_neighbor() puts the same ones back. The search's running
// objective is not resynchronised in between, so anything this leaks accumulates silently
// until the next accepted proposal.
TEST(DCNS2Test, removeThenAddRestoresCoverageExactly) {
    constexpr unsigned N_ANTENNAS = 40, N_BUILDINGS = 25;
    std::mt19937 rng(20260805);

    for (int instance_iter = 0; instance_iter < 40; ++instance_iter) {
        const DiscreteCoverageInstance instance(N_BUILDINGS,
            randomCoveragesOffGrid(rng, N_ANTENNAS, N_BUILDINGS));
        DCNS2 structure(instance, THRESHOLD);

        for (AntennaId antenna = 0; antenna < N_ANTENNAS; ++antenna)
            if (rng() % 2)
                structure.add(antenna);

        const std::vector<AntennaId> selected = structure.getSelectedAntennas();
        if (selected.empty())
            continue;

        for (int op = 0; op < 200; ++op) {
            // A whole proposal's worth of antennas, as RandomTopKNeighborSelector removes them.
            std::vector<AntennaId> batch = structure.getSelectedAntennas();
            std::shuffle(batch.begin(), batch.end(), rng);
            batch.resize(1 + rng() % std::min<size_t>(20, batch.size()));

            const Coverage before = structure.getCoverage();
            for (const AntennaId antenna : batch)
                structure.remove(antenna);
            for (const AntennaId antenna : batch)
                structure.add(antenna);
            const Coverage after = structure.getCoverage();

            ASSERT_EQ(after.serviced, before.serviced)
                << "instance " << instance_iter << ", op " << op << ", batch " << batch.size();
            ASSERT_DOUBLE_EQ(after.total, before.total)
                << "instance " << instance_iter << ", op " << op << ", batch " << batch.size();
        }
    }
}

// jointPeekMatchesAddingAllAntennas() on the grid, but at the size simulated annealing
// actually peeks (max_num_antennas_to_remove starts at 20) and with off-grid coverage.
TEST(DCNS2Test, jointPeekMatchesAddingAllAntennasOffGrid) {
    constexpr unsigned N_ANTENNAS = 40, N_BUILDINGS = 25;
    std::mt19937 rng(20260806);
    PeekWorkspace workspace; // reused across peeks, as a selector does

    for (int instance_iter = 0; instance_iter < 40; ++instance_iter) {
        const DiscreteCoverageInstance instance(N_BUILDINGS,
            randomCoveragesOffGrid(rng, N_ANTENNAS, N_BUILDINGS));
        DCNS2 structure(instance, THRESHOLD);

        for (int op = 0; op < 100; ++op) {
            const AntennaId toggle = rng() % N_ANTENNAS;
            if (structure.has(toggle))
                structure.remove(toggle);
            else
                structure.add(toggle);

            std::vector<AntennaId> to_peek;
            for (AntennaId antenna = 0; antenna < N_ANTENNAS; ++antenna)
                if (!structure.has(antenna))
                    to_peek.push_back(antenna);
            if (to_peek.size() < 2)
                continue;
            std::shuffle(to_peek.begin(), to_peek.end(), rng);
            to_peek.resize(2 + rng() % std::min<size_t>(19, to_peek.size() - 1));

            const Coverage before = structure.getCoverage();
            const Coverage delta = structure.peek(std::span<const AntennaId>(to_peek), workspace);

            for (const AntennaId antenna : to_peek)
                structure.add(antenna);
            const Coverage after = structure.getCoverage();
            for (const AntennaId antenna : to_peek)
                structure.remove(antenna);

            ASSERT_EQ(delta.serviced, after.serviced - before.serviced)
                << "instance " << instance_iter << ", op " << op << ", j = " << to_peek.size();
            // peek() sweeps the union in one pass while add() re-derives it per antenna, so
            // the two agree only up to rounding. That gap is what flips a >= threshold test.
            ASSERT_NEAR(delta.total, after.total - before.total, 1e-9)
                << "instance " << instance_iter << ", op " << op << ", j = " << to_peek.size();
        }
    }
}

// The incrementally maintained coverage must still agree with a from-scratch scoring after
// a long off-grid operation sequence, which is what a multi-hour run subjects it to.
TEST(DCNS2Test, matchesReferenceUnderRandomOperationsOffGrid) {
    constexpr unsigned N_ANTENNAS = 40, N_BUILDINGS = 25;
    std::mt19937 rng(20260807);

    for (int instance_iter = 0; instance_iter < 40; ++instance_iter) {
        const DiscreteCoverageInstance instance(N_BUILDINGS,
            randomCoveragesOffGrid(rng, N_ANTENNAS, N_BUILDINGS));
        const DCI2 preprocessed(instance);
        for (AntennaId antenna = 0; antenna < N_ANTENNAS; ++antenna)
            expectCleanedCoverage(preprocessed, antenna);

        DCNS2 structure(instance, THRESHOLD);
        std::set<AntennaId> present;

        for (int op = 0; op < 400; ++op) {
            const AntennaId antenna = rng() % N_ANTENNAS;
            if (structure.has(antenna)) {
                structure.remove(antenna);
                present.erase(antenna);
            } else {
                structure.add(antenna);
                present.insert(antenna);
            }

            const Coverage expected = referenceCoverage(instance, present, THRESHOLD);
            ASSERT_EQ(structure.getCoverage().serviced, expected.serviced)
                << "instance " << instance_iter << ", op " << op;
            ASSERT_NEAR(structure.getCoverage().total, expected.total, 1e-9)
                << "instance " << instance_iter << ", op " << op;
        }
    }
}

// Put the threshold exactly on a coverage the instance can reach, so that every >= test on
// that building is settled by the last bits. DCNS2 sums a building's union incrementally
// while the final solution is scored by a from-scratch sweep; if those disagree by an ulp
// on a building sitting at the threshold, the serviced count differs by a whole polygon.
TEST(DCNS2Test, thresholdOnAnAchievableCoverageIsCountedConsistently) {
    constexpr unsigned N_ANTENNAS = 40, N_BUILDINGS = 25;
    std::mt19937 rng(20260808);

    for (int instance_iter = 0; instance_iter < 60; ++instance_iter) {
        const DiscreteCoverageInstance instance(N_BUILDINGS,
            randomCoveragesOffGrid(rng, N_ANTENNAS, N_BUILDINGS));

        // A coverage some subset actually produces, used verbatim as the threshold.
        std::set<AntennaId> sample;
        for (AntennaId antenna = 0; antenna < N_ANTENNAS; ++antenna)
            if (rng() % 2)
                sample.insert(antenna);
        const unsigned building = rng() % N_BUILDINGS;
        const double threshold = buildingCoverage(instance, sample, building);
        if (threshold <= 0.0)
            continue;

        DCNS2 structure(instance, threshold);
        std::set<AntennaId> present;

        // Walk into the sampled subset, so the knife-edge building is hit on the way.
        for (int op = 0; op < 300; ++op) {
            const AntennaId antenna = rng() % N_ANTENNAS;
            if (structure.has(antenna)) {
                structure.remove(antenna);
                present.erase(antenna);
            } else {
                structure.add(antenna);
                present.insert(antenna);
            }

            const Coverage expected = referenceCoverage(instance, present, threshold);
            ASSERT_EQ(structure.getCoverage().serviced, expected.serviced)
                << "instance " << instance_iter << ", op " << op
                << ", threshold " << std::setprecision(17) << threshold;
            ASSERT_NEAR(structure.getCoverage().total, expected.total, 1e-9)
                << "instance " << instance_iter << ", op " << op;
        }
    }
}
