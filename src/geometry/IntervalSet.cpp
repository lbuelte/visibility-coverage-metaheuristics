#include "IntervalSet.hpp"

IntervalSet IntervalSet::add(IntervalSpan intervals) const {
    const auto intervals1 = span();
    const auto intervals2 = intervals;

    IntervalSet res((unsigned) (intervals1.size() + intervals2.size()), Uninit{});
    auto out_it = res.mutableData();

    auto it1 = intervals1.begin();
    auto it2 = intervals2.begin();
    while (it1 != intervals1.end() && it2 != intervals2.end()) {
        if (it1->start <= it2->start)
            *out_it = *it1++;
        else
            *out_it = *it2++;
        ++out_it;
    }
    // Only one of the two tails is non-empty.
    out_it = std::copy(it1, intervals1.end(), out_it);
    out_it = std::copy(it2, intervals2.end(), out_it);

    CHEAP_ASSERT(out_it == res.mutableData() + res.size());
    return res;
}

IntervalSet IntervalSet::remove(IntervalSpan intervals) const {
    const auto intervals1 = span();
    const auto intervals2 = intervals;

    CHEAP_ASSERT(intervals2.size() <= intervals1.size());
    IntervalSet res((unsigned) (intervals1.size() - intervals2.size()), Uninit{});
    auto out_it = res.mutableData();

    auto it1 = intervals1.begin();
    auto it2 = intervals2.begin();
    while (it1 != intervals1.end() && it2 != intervals2.end()) {
        if (it1->start == it2->start && it1->end == it2->end) {
            // Same interval, remove one instance
            ++it1;
            ++it2;
        } else {
            // Note: It must not happen that it2->start < it1->start,
            // which would imply that the interval we want to remove isn't there
            CHEAP_ASSERT(it1->start <= it2->start); // if equal, ends must differ
            // Keep the interval
            *out_it = *it1++;
            ++out_it;
        }
    }
    CHEAP_ASSERT(it2 == intervals2.end());
    out_it = std::copy(it1, intervals1.end(), out_it);

    CHEAP_ASSERT(out_it == res.mutableData() + res.size());
    return res;
}
