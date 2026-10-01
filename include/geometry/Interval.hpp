#pragma once

#include <cassert>
#include <span>
#include <vector>

// Note: Intervals are usually assumed to be normalized to building perimeter.
struct Interval {
    double start;
    double end;

    // Not initialized!
    Interval() = default;

    Interval(const double s, const double e) : start(s), end(e) {
        assert(0 <= s && s <= e);
    }

    double length() const {
        return end - start;
    }

    friend bool operator<(const Interval &l, const Interval &r) {
        return l.start < r.start;
    }
};

// Multiset of intervals, sorted by min
using IntervalSpan = std::span<const Interval>;

// Total length covered by the union of all intervals.
double computeUnionLength(IntervalSpan intervals);

// Multiset union, preserves the sorted order
void unionIntervals(IntervalSpan intervals1, IntervalSpan intervals2,
    std::vector<Interval> &res);

// Multiset difference, preserves the sorted order
void removeIntervals(IntervalSpan intervals, IntervalSpan remove,
    std::vector<Interval> &res);