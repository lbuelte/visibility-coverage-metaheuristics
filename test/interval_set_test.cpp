#include "gtest/gtest.h"

#include <algorithm>
#include <random>
#include <utility>
#include <vector>

#include "Interval.hpp"
#include "IntervalSet.hpp"

namespace {

constexpr unsigned MAX_INLINE = IntervalSet::MAX_INLINE;

std::vector<Interval> toVec(IntervalSpan intervals) {
    return {intervals.begin(), intervals.end()};
}

void expectSame(IntervalSpan actual, IntervalSpan expected) {
    ASSERT_EQ(actual.size(), expected.size());
    for (size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(actual[i].start, expected[i].start) << "at " << i;
        EXPECT_EQ(actual[i].end, expected[i].end) << "at " << i;
    }
}

std::vector<Interval> sortedByStartAndEnd(IntervalSpan intervals) {
    std::vector<Interval> res(intervals.begin(), intervals.end());
    std::sort(res.begin(), res.end(), [](const Interval &l, const Interval &r) {
        return std::pair(l.start, l.end) < std::pair(r.start, r.end);
    });
    return res;
}

// Sets are only sorted by start (Interval::operator<), so which of several intervals
// with the same start comes first is unspecified: compare those up to permutation.
void expectSameUpToEqualStarts(IntervalSpan actual, IntervalSpan expected) {
    ASSERT_EQ(actual.size(), expected.size());
    EXPECT_TRUE(std::is_sorted(actual.begin(), actual.end()));

    const std::vector<Interval> actual_sorted = sortedByStartAndEnd(actual);
    const std::vector<Interval> expected_sorted = sortedByStartAndEnd(expected);
    expectSame(actual_sorted, expected_sorted);
}

// The reference results that the rest of the codebase computes on plain spans.
std::vector<Interval> refAdd(IntervalSpan a, IntervalSpan b) {
    std::vector<Interval> res;
    unionIntervals(a, b, res);
    return res;
}

std::vector<Interval> refRemove(IntervalSpan a, IntervalSpan b) {
    std::vector<Interval> res;
    removeIntervals(a, b, res);
    return res;
}

// n intervals, sorted by start, with many ties in both start and end.
std::vector<Interval> randomIntervals(std::mt19937 &rng, unsigned n) {
    std::uniform_int_distribution<int> start_dist(0, 6), len_dist(0, 4);
    std::vector<Interval> res;
    res.reserve(n);
    for (unsigned i = 0; i < n; ++i) {
        const double start = start_dist(rng);
        res.emplace_back(start, start + len_dist(rng));
    }
    std::sort(res.begin(), res.end());
    return res;
}

} // namespace

TEST(IntervalSetTest, emptySet) {
    const IntervalSet set;
    EXPECT_EQ(set.size(), 0u);
    EXPECT_TRUE(set.empty());
    EXPECT_EQ(set.span().size(), 0u);
    EXPECT_EQ(set.begin(), set.end());
}

TEST(IntervalSetTest, construction) {
    // Both the inline and the heap representation must round trip.
    for (unsigned n = 0; n <= 3 * MAX_INLINE; ++n) {
        std::vector<Interval> intervals;
        for (unsigned i = 0; i < n; ++i)
            intervals.emplace_back(i, i + 1);

        const IntervalSet set{IntervalSpan(intervals)};
        EXPECT_EQ(set.size(), n);
        EXPECT_EQ(set.empty(), n == 0);
        expectSame(set.span(), intervals);
        EXPECT_EQ(set.end() - set.begin(), (ptrdiff_t) n);
    }
}

TEST(IntervalSetTest, initializerListConstruction) {
    const IntervalSet set{{0.0, 1.0}, {0.5, 2.0}, {3.0, 4.0}};
    const std::vector<Interval> expected{{0.0, 1.0}, {0.5, 2.0}, {3.0, 4.0}};
    expectSame(set.span(), expected);
}

TEST(IntervalSetTest, addKeepsSortedOrder) {
    const IntervalSet set{{0.0, 2.0}, {3.0, 4.0}};
    const std::vector<Interval> added{{1.0, 5.0}, {3.5, 3.5}};

    const IntervalSet sum = set.add(IntervalSpan(added));
    const std::vector<Interval> expected{{0.0, 2.0}, {1.0, 5.0}, {3.0, 4.0}, {3.5, 3.5}};
    expectSame(sum.span(), expected);
    // The original is untouched: add() returns a new set.
    EXPECT_EQ(set.size(), 2u);
}

TEST(IntervalSetTest, addSpillsToHeap) {
    // 3 + 3 intervals crosses the inline capacity.
    const IntervalSet set{{0.0, 1.0}, {2.0, 3.0}, {4.0, 5.0}};
    const std::vector<Interval> added{{0.5, 1.5}, {2.5, 3.5}, {4.5, 5.5}};

    const IntervalSet sum = set.add(IntervalSpan(added));
    ASSERT_GT(sum.size(), MAX_INLINE);
    expectSame(sum.span(), refAdd(set.span(), IntervalSpan(added)));
}

TEST(IntervalSetTest, addEmpty) {
    const IntervalSet set{{0.0, 1.0}, {2.0, 3.0}};
    expectSame(set.add(IntervalSpan{}).span(), set.span());

    const IntervalSet empty;
    const std::vector<Interval> added{{0.0, 1.0}};
    expectSame(empty.add(IntervalSpan(added)).span(), added);
}

TEST(IntervalSetTest, removeDropsOneInstancePerDuplicate) {
    // The same interval occurs three times; removing it twice must leave one.
    const IntervalSet set{{1.0, 2.0}, {1.0, 2.0}, {1.0, 2.0}, {5.0, 6.0}};
    const std::vector<Interval> removed{{1.0, 2.0}, {1.0, 2.0}};

    const IntervalSet diff = set.remove(IntervalSpan(removed));
    const std::vector<Interval> expected{{1.0, 2.0}, {5.0, 6.0}};
    expectSame(diff.span(), expected);
}

TEST(IntervalSetTest, removeDistinguishesEqualStarts) {
    // Equal starts, different ends: the right instance has to be dropped.
    const IntervalSet set{{1.0, 5.0}, {1.0, 3.0}, {1.0, 9.0}};
    const std::vector<Interval> removed{{1.0, 3.0}};

    const IntervalSet diff = set.remove(IntervalSpan(removed));
    const std::vector<Interval> expected{{1.0, 5.0}, {1.0, 9.0}};
    expectSame(diff.span(), expected);
}

TEST(IntervalSetTest, removeFallsBackToInline) {
    // Shrinking from the heap representation back below the inline capacity.
    const IntervalSet set{{0.0, 1.0}, {1.0, 2.0}, {2.0, 3.0}, {3.0, 4.0},
        {4.0, 5.0}, {5.0, 6.0}, {6.0, 7.0}};
    ASSERT_GT(set.size(), MAX_INLINE);
    const std::vector<Interval> removed{{0.0, 1.0}, {2.0, 3.0}, {4.0, 5.0}, {6.0, 7.0}};

    const IntervalSet diff = set.remove(IntervalSpan(removed));
    const std::vector<Interval> expected{{1.0, 2.0}, {3.0, 4.0}, {5.0, 6.0}};
    ASSERT_LE(diff.size(), MAX_INLINE);
    expectSame(diff.span(), expected);
}

TEST(IntervalSetTest, removeEverything) {
    const std::vector<Interval> intervals{{0.0, 1.0}, {1.0, 2.0}, {2.0, 3.0},
        {3.0, 4.0}, {4.0, 5.0}, {5.0, 6.0}};
    const IntervalSet set{IntervalSpan(intervals)};

    const IntervalSet diff = set.remove(IntervalSpan(intervals));
    EXPECT_TRUE(diff.empty());
}

TEST(IntervalSetTest, valueSemantics) {
    const IntervalSet inline_set{{0.0, 1.0}, {2.0, 3.0}};
    const IntervalSet heap_set{{0.0, 1.0}, {1.0, 2.0}, {2.0, 3.0},
        {3.0, 4.0}, {4.0, 5.0}, {5.0, 6.0}};
    ASSERT_LE(inline_set.size(), MAX_INLINE);
    ASSERT_GT(heap_set.size(), MAX_INLINE);

    // Copies are deep and independent of the source's representation.
    IntervalSet copy = heap_set;
    expectSame(copy.span(), heap_set.span());
    EXPECT_NE(copy.data(), heap_set.data());

    // Assignment across the inline/heap boundary, in both directions.
    IntervalSet a = inline_set;
    a = heap_set;
    expectSame(a.span(), heap_set.span());
    a = inline_set;
    expectSame(a.span(), inline_set.span());

    // Moving takes ownership and leaves a valid, empty set behind.
    IntervalSet source = heap_set;
    const IntervalSet moved = std::move(source);
    expectSame(moved.span(), heap_set.span());
    EXPECT_TRUE(source.empty());

    IntervalSet move_target = inline_set;
    IntervalSet move_source = heap_set;
    move_target = std::move(move_source);
    expectSame(move_target.span(), heap_set.span());
    EXPECT_TRUE(move_source.empty());

    // A moved-from set is reusable.
    move_source = inline_set;
    expectSame(move_source.span(), inline_set.span());

    // Self assignment must not corrupt or free the buffer.
    IntervalSet self = heap_set;
    self = self;
    expectSame(self.span(), heap_set.span());
    self = std::move(self);
    expectSame(self.span(), heap_set.span());

    // Reallocation inside a vector exercises the move constructor.
    std::vector<IntervalSet> sets;
    for (int i = 0; i < 32; ++i)
        sets.push_back(i % 2 ? heap_set : inline_set);
    for (int i = 0; i < 32; ++i)
        expectSame(sets[i].span(), i % 2 ? heap_set.span() : inline_set.span());
}

TEST(IntervalSetTest, matchesReferenceImplementation) {
    std::mt19937 rng(20250731);
    std::uniform_int_distribution<unsigned> size_dist(0, 3 * MAX_INLINE);

    for (int iter = 0; iter < 20000; ++iter) {
        const std::vector<Interval> base = randomIntervals(rng, size_dist(rng));
        const std::vector<Interval> other = randomIntervals(rng, size_dist(rng));
        const IntervalSet set{IntervalSpan(base)};

        // add() must agree with unionIntervals().
        const IntervalSet sum = set.add(IntervalSpan(other));
        ASSERT_EQ(sum.size(), base.size() + other.size());
        expectSame(sum.span(), refAdd(IntervalSpan(base), IntervalSpan(other)));

        // remove() must agree with removeIntervals() on an actual sub-multiset,
        // which we build as a random subsequence so the sorted order is kept.
        std::vector<Interval> to_remove, expected;
        for (const auto &interval : base) {
            if (rng() % 2)
                to_remove.push_back(interval);
            else
                expected.push_back(interval);
        }
        const IntervalSet diff = set.remove(IntervalSpan(to_remove));
        ASSERT_EQ(diff.size(), expected.size());
        expectSame(diff.span(), refRemove(IntervalSpan(base), IntervalSpan(to_remove)));
        // ... and the reference itself must be the complement we held back. Both drop
        // one instance per duplicate, but not necessarily the same one, so the surviving
        // intervals with equal start can end up in a different order.
        expectSameUpToEqualStarts(diff.span(), expected);

        // Adding then removing the same intervals is the identity (again up to the
        // order within equal starts).
        expectSameUpToEqualStarts(sum.remove(IntervalSpan(other)).span(), base);

        // Copies of both representations survive a round trip.
        const IntervalSet copy = sum;
        expectSame(copy.span(), toVec(sum.span()));
    }
}
