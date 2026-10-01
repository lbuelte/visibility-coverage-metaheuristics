#pragma once

#include "Interval.hpp"

#include <algorithm>
#include <cassert>
#include <initializer_list>
#include <memory>

#ifndef CHEAP_ASSERT
#define CHEAP_ASSERT assert
#endif

/// A multiset of intervals, sorted by start (see IntervalSpan).
/// Tradeoff between avoiding heap allocations and keeping size low: up to MAX_INLINE
/// intervals live inside the object, larger sets use one exactly sized heap buffer.
///
/// Sets are immutable: add() and remove() return new sets instead of resizing in place.
class IntervalSet {
public:

    constexpr static unsigned MAX_INLINE = 3;

protected:

    unsigned count = 0;

    // The active member is inline_data iff count <= MAX_INLINE, so count is the only
    // discriminator; there is no separate tag that could get out of sync (and no
    // redundant check on it, as std::variant would do on every access).
    // Since sets are immutable, the heap case has no use for a vector's size/capacity
    // and stores the bare buffer instead.
    union {
        Interval inline_data[MAX_INLINE];
        std::unique_ptr<Interval[]> heap_data;
    };

    bool isInline() const { return count <= MAX_INLINE; }

    struct Uninit {};

    // Storage is uninitialized: the caller must fill in all `n` intervals.
    IntervalSet(unsigned n, Uninit) : count(n) {
        if (!isInline())
            std::construct_at(&heap_data, std::make_unique_for_overwrite<Interval[]>(n));
    }

    // Non-const access is kept private to protect the sorted order.
    Interval *mutableData() { return isInline() ? inline_data : heap_data.get(); }

public:

    IntervalSet() : count(0) {}

    explicit IntervalSet(IntervalSpan intervals)
        : IntervalSet((unsigned) intervals.size(), Uninit{})
    {
        std::copy(intervals.begin(), intervals.end(), mutableData());
    }

    IntervalSet(std::initializer_list<Interval> intervals)
        : IntervalSet(IntervalSpan(intervals.begin(), intervals.size()))
    {
    }

    IntervalSet(const IntervalSet &other) : IntervalSet(other.span()) {}

    IntervalSet(IntervalSet &&other) noexcept : count(other.count) {
        if (isInline()) {
            std::copy_n(other.inline_data, count, inline_data);
        } else {
            std::construct_at(&heap_data, std::move(other.heap_data));
            std::destroy_at(&other.heap_data);
            other.count = 0; // `other` falls back to the (empty) inline representation
        }
    }

    IntervalSet &operator=(const IntervalSet &other) {
        if (this != &other)
            *this = IntervalSet(other);
        return *this;
    }

    IntervalSet &operator=(IntervalSet &&other) noexcept {
        if (this == &other)
            return *this;
        if (!isInline())
            std::destroy_at(&heap_data);
        count = other.count;
        if (isInline()) {
            std::copy_n(other.inline_data, count, inline_data);
        } else {
            std::construct_at(&heap_data, std::move(other.heap_data));
            std::destroy_at(&other.heap_data);
            other.count = 0;
        }
        return *this;
    }

    ~IntervalSet() {
        if (!isInline())
            std::destroy_at(&heap_data);
    }

    unsigned size() const { return count; }

    bool empty() const { return count == 0; }

    const Interval *data() const { return isInline() ? inline_data : heap_data.get(); }

    const Interval *begin() const { return data(); }

    const Interval *end() const { return data() + count; }

    IntervalSpan span() const { return IntervalSpan(data(), count); }

    // Multiset union, preserves the sorted order.
    // Equivalent to unionIntervals(span(), intervals, ...).
    IntervalSet add(IntervalSpan intervals) const;

    // Multiset difference, preserves the sorted order.
    // Equivalent to removeIntervals(span(), intervals, ...).
    // Assumes all intervals exist in the multiset at least once!
    IntervalSet remove(IntervalSpan intervals) const;
};
