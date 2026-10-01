#include "Interval.hpp"

double computeUnionLength(IntervalSpan intervals) {
    double covered = 0;
    Interval cur_interval{0, 0};
    for (const auto &interval : intervals) {
        if (interval.start <= cur_interval.end)
            cur_interval.end = std::max<double>(cur_interval.end, interval.end);
        else {
            covered += cur_interval.length();
            cur_interval = interval;
        }
    }
    covered += cur_interval.length();
    return covered;
}

void unionIntervals(IntervalSpan intervals1, IntervalSpan intervals2,
    std::vector<Interval> &res)
{
    res.reserve(intervals1.size() + intervals2.size());
    auto it1 = intervals1.begin();
    auto it2 = intervals2.begin();
    while (it1 != intervals1.end() && it2 != intervals2.end()) {
        if (it1->start <= it2->start) {
            res.emplace_back(*it1);
            ++it1;
        } else {
            res.emplace_back(*it2);
            ++it2;
        }
    }
    res.insert(res.end(), it1, intervals1.end());
    res.insert(res.end(), it2, intervals2.end());
}

void removeIntervals(IntervalSpan intervals, IntervalSpan remove,
    std::vector<Interval> &res)
{
    auto it = intervals.begin();
    auto it_rem = remove.begin();
    while (it != intervals.end() && it_rem != remove.end()) {
        if (it->start == it_rem->start && it->end == it_rem->end) {
            // Same interval, remove one instance
            ++it;
            ++it_rem;
        } else {
            // Note: It must not happen that it2->start < it1->start,
            // which would imply that the interval we want to remove isn't there
            assert(it->start <= it_rem->start); // if equal, ends must differ
            // Keep the interval
            res.emplace_back(*it);
            ++it;
        }
    }
    res.insert(res.end(), it, intervals.end());
}