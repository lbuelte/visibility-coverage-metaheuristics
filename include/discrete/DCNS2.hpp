#pragma once

#include "DiscreteCoverageInstance.hpp"
#include "Interval.hpp"
#include "IntervalSet.hpp"
#include <cstdint>
#include <span>

// Assumptions:
// - Every building is seen by only a few selected antennas (think 2~3 usually).
// - Consequently, only a few intervals are covered on the outline of any building.

// Idea of this data structure: Keep everything as simple as possible.
// Local search needs to be able to efficiently add or remove antennas.
// Operations can be undone easily: An added antenna can be removed again, a removed antenna can be added back.

struct Coverage {
    int serviced = 0;
    double total = 0; // note: sum of coverages clamped to threshold

    Coverage operator +(Coverage other) const {
        return {
            .serviced = serviced + other.serviced,
            .total = total + other.total
        };
    }

    Coverage operator -(Coverage other) const {
        return {
            .serviced = serviced - other.serviced,
            .total = total - other.total
        };
    }

    void operator +=(Coverage other) {
        *this = *this + other;
    }
};

// Scratch for a joint peek of several antennas. Passed in explicitly so that peek() stays
// const and two threads can peek the same structure with workspaces of their own.
//
// Most buildings are covered by exactly one of the peeked antennas (~93% measured), and
// those are recorded by reference into the instance's flat interval array -- nothing is
// copied. Only the shared ones get their contributions unioned into `merged`.
struct PeekWorkspace {

    // One entry per distinct building the current peek touches.
    struct Contribution {
        unsigned building;
        unsigned begin;   // into DCI2's interval array, or into `merged` if merged_run
        unsigned count;
        bool merged_run;
    };

    // Per building, kept in one 8-byte record so that a building visit costs a single
    // random access. The common case reads `stamp` alone; `slot` is only needed for a
    // building that several of the peeked antennas cover.
    struct BuildingSlot {
        uint32_t stamp = 0; // epoch that last touched this building; 0 means never
        uint32_t slot = 0;  // index into `contrib`, valid while stamp == the current epoch
    };
    static_assert(sizeof(BuildingSlot) == 8, "BuildingSlot must not be padded");

    std::vector<BuildingSlot> slots;
    uint32_t epoch = 0;

    std::vector<Contribution> contrib;
    std::vector<Interval> merged;

    void begin(size_t n_buildings) {
        if (slots.size() != n_buildings)
            slots.assign(n_buildings, BuildingSlot{});
        if (++epoch == 0) { // wrapped after 4 billion peeks; epoch 0 means "never touched"
            slots.assign(n_buildings, BuildingSlot{});
            epoch = 1;
        }
        // All three hold trivially destructible elements, so this is just a size reset;
        // the buffers keep their capacity and stop reallocating after the first few peeks.
        contrib.clear();
        merged.clear();
    }
};


class DCNS2 {
protected:

    Coverage coverage;

    struct alignas(64) BuildingCoverage {
        double total = 0;
        IntervalSet intervals;
    };

    // The disjoint (merged, non-touching) coverage of every building, with its total
    // length. This is all peek() reads: on a disjoint set the union length with one more
    // interval follows from inclusion-exclusion and needs no merge at all.
    std::vector<BuildingCoverage> disjoint_coverages;

    // The multiset of intervals the selected antennas contribute, kept because remove()
    // has to take one antenna's contribution back out exactly. Only add() and remove()
    // touch it, and they refresh the disjoint form from it afterwards.
    std::vector<BuildingCoverage> multiset_coverages;

    std::vector<Interval> disjoint_scratch; // reused by rebuildDisjoint()

    // Membership as a bitmap: has() sits on the hot path of candidate sampling, where a
    // bit test beats a hash lookup and the whole map stays L1-resident. The selected ids
    // are kept beside it so they can be handed out without scanning it.
    std::vector<bool> is_selected;
    std::vector<AntennaId> selected_antennas;

    const DCI2 &dci;

    const double threshold = 0; // MUST be initialized!

    double clampCov(double cov) const {
        return std::min(cov, threshold);
    }

    // Collapses a building's multiset into its disjoint union, installs it, and returns
    // the total covered length -- which on a disjoint set is just the sum of the lengths.
    double rebuildDisjoint(unsigned building) {
        double total = 0;
        const auto &intervals = multiset_coverages[building].intervals.span();
        if (intervals.empty()) {
            disjoint_coverages[building] = {
                .total = 0,
                .intervals = IntervalSet(),
            };
            return 0;
        }

        disjoint_scratch.clear();
        Interval cur_interval = intervals[0];
        for (unsigned i = 1; i < intervals.size(); ++i) {
            const auto &iv = intervals[i];
            if (iv.start <= cur_interval.end) { // touching
                cur_interval.end = std::max(iv.end, cur_interval.end); // extend?
            } else {
                total += cur_interval.length();
                disjoint_scratch.emplace_back(cur_interval);
                cur_interval = iv;
            }
        }
        total += cur_interval.length();
        disjoint_scratch.emplace_back(cur_interval);

        auto &d_cov = disjoint_coverages[building];
        d_cov.intervals = IntervalSet(IntervalSpan(disjoint_scratch));
        d_cov.total = total;
        return total;
    }

    // |D u {a}| = |D| + |a| - |D n a|. D being disjoint is what makes this work: the
    // intersection is then just the sum of the clamped pairwise overlaps, so the loop is
    // two mins, a max, a subtract and an add per interval -- no compares, no selects, no
    // branches, and nothing loop-carried but the accumulator.
    double unionLengthWithOne(const BuildingCoverage &d_cov, Interval a) const {
        double overlap = 0;
        for (const Interval &iv : d_cov.intervals)
            overlap += std::max(0.0, std::min(iv.end, a.end) - std::max(iv.start, a.start));
        return d_cov.total + (a.end - a.start) - overlap;
    }

    double unionLengthWithOne2(const BuildingCoverage &d_cov, Interval a) const {
        double total = 0;
        const Interval *iv = d_cov.intervals.data();
        const Interval *iv_end = iv + d_cov.intervals.size();

        // Intervals preceding a
        for (; iv != iv_end && iv->end < a.start; ++iv)
            total += iv->length();

        // Up to 2 intervals merging with a
        Interval iv_mid = a;
        for (; iv != iv_end && iv->start <= a.end; ++iv) {
            iv_mid.start = std::min(iv_mid.start, iv->start);
            iv_mid.end = std::max(iv_mid.end, iv->end);
        }
        total += iv_mid.length();

        // Intervals succeeding a
        for (; iv != iv_end; ++iv)
            total += iv->length();

        return total;
    }

    // Union length of two sorted interval sequences, recomputed from scratch. Only needed
    // when an antenna contributes something other than exactly one interval to a building,
    // which is rare (~1.25 intervals per covered building measured on small_1).
    static double unionLengthMerged(IntervalSpan disjoint, IntervalSpan added) {
        double new_total = 0;
        auto it1 = disjoint.data();
        const auto end1 = it1 + disjoint.size();
        auto it2 = added.data();
        const auto end2 = it2 + added.size();

        double cur_end = 0, cur_start = 0;
        while (it1 != end1 && it2 != end2) {
            const double s1 = it1->start, s2 = it2->start;
            const bool it1_first = __builtin_expect_with_probability(s1 <= s2, 1, 0.5);
            const double iv_start = std::min(s1, s2);
            const double iv_end = it1_first ? it1->end : it2->end;
            const bool new_iv = iv_start > cur_end;
            new_total += new_iv ? (cur_end - cur_start) : 0;
            cur_start = new_iv ? iv_start : cur_start;
            cur_end = std::max(cur_end, iv_end);
            it1 += it1_first;
            it2 += !it1_first;
        }
        const auto add_remaining = [&](auto it, const auto end) {
            for (; it != end && it->start <= cur_end; ++it) {
                cur_end = std::max(cur_end, it->end);
            }
            new_total += cur_end - cur_start;
            for (; it != end; ++it)
                new_total += it->length();
        };
        if (it1 != end1) {
            add_remaining(it1, end1);
        } else {
            add_remaining(it2, end2);
        }
        return new_total;
    }

    double unionLengthWith(const BuildingCoverage &d_cov, IntervalSpan added) const {
        if (added.size() == 1) {
            double total = unionLengthWithOne(d_cov, added[0]);
            // For "critical" buildings close to the threshold, recompute using the exact same summation
            // as the slow unionLengthMerged path to achieve consistency 
            if (std::abs(total - threshold) < 1e-12)
                return unionLengthWithOne2(d_cov, added[0]);
            return total;
        }
        return unionLengthMerged(d_cov.intervals.span(), added);
    }

    // Appends the disjoint union of two sorted, individually disjoint runs to `out`, and
    // returns where it starts. Both inputs are tiny in practice (one or two intervals):
    // this only runs for a building that several of the peeked antennas cover.
    static unsigned appendDisjointUnion(std::vector<Interval> &out, IntervalSpan a, IntervalSpan b) {
        const unsigned run_begin = (unsigned) out.size();
        auto it1 = a.data();
        const auto end1 = it1 + a.size();
        auto it2 = b.data();
        const auto end2 = it2 + b.size();

        while (it1 != end1 || it2 != end2) {
            const bool take1 = (it2 == end2) || (it1 != end1 && it1->start <= it2->start);
            const Interval iv = take1 ? *it1++ : *it2++;
            // Guard on run_begin, not on emptiness: `out` still holds earlier buildings'
            // runs, and this one must not be merged into them.
            if (out.size() > run_begin && iv.start <= out.back().end) {
                out.back().end = std::max(out.back().end, iv.end);
            } else {
                out.emplace_back(iv);
            }
        }
        return run_begin;
    }

    IntervalSpan resolve(const PeekWorkspace &ws, const PeekWorkspace::Contribution &c) const {
        const Interval *base = c.merged_run ? ws.merged.data() : dci.intervalData();
        return IntervalSpan(base + c.begin, c.count);
    }

public:

    DCNS2(const DiscreteCoverageInstance &dci, double threshold)
        : disjoint_coverages(dci.get_number_polygons())
        , multiset_coverages(dci.get_number_polygons())
        , is_selected(dci.get_number_antennas(), false)
        , dci(dci.getDCI2())
        , threshold(threshold)
    {}

    bool has(AntennaId antenna) const {
        return is_selected[antenna];
    }

    unsigned numSelected() const { return (unsigned) selected_antennas.size(); }

    Coverage getCoverage() const { return coverage; }

    // returns the change in coverage
    Coverage peek(AntennaId antenna) const {
        assert(!has(antenna));
        Coverage delta;
        for (const auto &cov_build : dci.getCoveredBuildings(antenna)) {
            const auto &d_cov = disjoint_coverages[cov_build.polygon];
            const double old_total = d_cov.total;
            const double new_total = unionLengthWith(d_cov, dci.getIntervals(&cov_build));
            delta.serviced += (old_total < threshold && new_total >= threshold);
            delta.total += clampCov(new_total) - clampCov(old_total);
        }
        return delta;
    }

    // Change in coverage if all of `antennas` were added at once. Not the sum of the
    // individual peeks: a building several of them cover has to see their union.
    Coverage peek(std::span<const AntennaId> antennas, PeekWorkspace &ws) const {
        if (antennas.size() == 1)
            return peek(antennas[0]); // no collision possible, skip the workspace entirely

        ws.begin(disjoint_coverages.size());

        for (const AntennaId antenna : antennas) {
            assert(!has(antenna));
            for (const auto &cov_build : dci.getCoveredBuildings(antenna)) {
                const IntervalSpan added = dci.getIntervals(&cov_build);
                const unsigned building = cov_build.polygon;
                const unsigned begin = (unsigned) (added.data() - dci.intervalData());

                if (ws.slots[building].stamp != ws.epoch) {
                    ws.slots[building] = {ws.epoch, (uint32_t) ws.contrib.size()};
                    ws.contrib.emplace_back(PeekWorkspace::Contribution{
                        .building = building,
                        .begin = begin,
                        .count = (unsigned) added.size(),
                        .merged_run = false
                    });
                } else {
                    // Seen before in this peek: union what we have with the new run. The
                    // existing run may itself live in ws.merged, so reserve the (bounded)
                    // output first -- otherwise appending could reallocate underneath it.
                    auto &c = ws.contrib[ws.slots[building].slot];
                    ws.merged.reserve(ws.merged.size() + c.count + added.size());
                    const IntervalSpan existing = resolve(ws, c);
                    const unsigned run_begin = appendDisjointUnion(ws.merged, existing, added);
                    c.begin = run_begin;
                    c.count = (unsigned) (ws.merged.size() - run_begin);
                    c.merged_run = true;
                }
            }
        }

        Coverage delta;
        for (const auto &c : ws.contrib) {
            const auto &d_cov = disjoint_coverages[c.building];
            const double old_total = d_cov.total;
            const double new_total = unionLengthWith(d_cov, resolve(ws, c));
            delta.serviced += (old_total < threshold && new_total >= threshold);
            delta.total += clampCov(new_total) - clampCov(old_total);
        }
        return delta;
    }

    void remove(AntennaId antenna) {
        assert(has(antenna));
        Coverage delta;
        const auto covered_buildings = dci.getCoveredBuildings(antenna);
        for (const auto *cov_build = covered_buildings.data();
                cov_build != covered_buildings.data() + covered_buildings.size();
                ++cov_build)
        {
            auto &b_cov = multiset_coverages[cov_build->polygon];
            const double old_total = disjoint_coverages[cov_build->polygon].total;
            b_cov.intervals = b_cov.intervals.remove(dci.getIntervals(cov_build));
            const double new_total = rebuildDisjoint(cov_build->polygon);
            b_cov.total = new_total;
            delta.serviced -= (old_total >= threshold && new_total < threshold);
            delta.total += clampCov(new_total) - clampCov(old_total);
        }
        coverage += delta;

        is_selected[antenna] = false;
        // Order is irrelevant here, getSelectedAntennas() sorts on the way out.
        const auto it = std::find(selected_antennas.begin(), selected_antennas.end(), antenna);
        assert(it != selected_antennas.end());
        *it = selected_antennas.back();
        selected_antennas.pop_back();
    }

    void add(AntennaId antenna) {
        assert(!has(antenna));
        // basically same as peek, but installs new intervals
        Coverage delta;
        for (const auto &cov_build : dci.getCoveredBuildings(antenna)) {
            auto &b_cov = multiset_coverages[cov_build.polygon];
            const double old_total = disjoint_coverages[cov_build.polygon].total;
            b_cov.intervals = b_cov.intervals.add(dci.getIntervals(&cov_build));
            const double new_total = rebuildDisjoint(cov_build.polygon);
            b_cov.total = new_total;
            delta.serviced += (old_total < threshold && new_total >= threshold);
            delta.total += clampCov(new_total) - clampCov(old_total);
        }
        coverage += delta;

        is_selected[antenna] = true;
        selected_antennas.emplace_back(antenna);
    }

    // Sorted on demand: the set is kept unordered so that remove() stays cheap.
    std::vector<AntennaId> getSelectedAntennas() const {
        std::vector<AntennaId> res = selected_antennas;
        std::sort(res.begin(), res.end());
        return res;
    }

    // TEMP
    // *Might* help with floating-point drift; should only be called
    // after *many, many* add() and remove() calls (maybe decide that automatically)
    void recomputeBuildingCoverages() {
        coverage = Coverage{0, 0};
        for (unsigned building = 0; building < multiset_coverages.size(); ++building) {
            const double total = rebuildDisjoint(building);
            multiset_coverages[building].total = total;
            coverage.serviced += total >= threshold;
            coverage.total += clampCov(total);
        }
    }
};
