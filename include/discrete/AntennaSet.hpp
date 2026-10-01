#pragma once

#include <cassert>
#include <cstddef>
#include <memory>
#include <optional>
#include <vector>
#include <span>

#include "Interval.hpp"

struct Coverage {
    // Number of buildings with sufficient coverage
    size_t n_covered = 0;
    // Sum of the relative coverage of all buildings
    double total = 0;
    // Necessary coverage to count as covered
    double min = 0;

    void update(double new_coverage) {
        total += new_coverage;
        if (new_coverage >= min)
            ++n_covered;
    }

    // Inverse of update
    void downdate(double old_coverage) {
        total -= old_coverage;
        if (old_coverage >= min)
            --n_covered;
    }

    Coverage merge(Coverage other) const {
        assert(min == other.min);
        return {
            .n_covered = n_covered + other.n_covered,
            .total = total + other.total,
            .min = min,
        };
    }

    bool improves(Coverage other) const {
        if (n_covered != other.n_covered)
            return n_covered > other.n_covered;
        return total > other.total;
    }
};

// A building that is covered partially by an antenna
struct CoveredBuilding {
    size_t id = 0;
    // TODO VERY high chance this is only like 1, 2 or so intervals. should optimize that case.
    std::vector<Interval> intervals;
};

struct Antenna {
    size_t id = 0;
    // All buildings this antenna sees, must be sorted by building id
    std::span<const CoveredBuilding> covered;
};

// A set of antennas (a candidate solution)
struct AntennaSet {
    AntennaSet(double min_coverage)
    { coverage.min = min_coverage; }

    AntennaSet(double min_coverage, std::span<const Antenna> antennas);

    // Merge two antenna sets, applying `this` on top of `base`.
    [[nodiscard]] AntennaSet merge(const AntennaSet &base) const;

    std::optional<size_t> findBuilding(size_t building_id) const;

    void addBuilding(size_t id, std::span<const Interval> b_intervals) {
        addBuilding(id, computeUnionLength(b_intervals), b_intervals);
    }

    void addBuilding(
        size_t id,
        double coverage,
        std::span<const Interval> b_intervals);

    void finalize() {
        building_interval_offsets.emplace_back(intervals.size());
    }

    // Get the intervals of the i-th building in this set
    std::span<const Interval> getIntervals(size_t i) const;

protected:

    void copyBuilding(const AntennaSet &other, size_t i);

    // Drop zero-coverage buildings
    void gc();

    // Referenced by all buildings, see getIntervals()
    std::vector<Interval> intervals;

    // Assumed to be disjoint.
    std::vector<size_t> antenna_ids;
    std::vector<size_t> removed_antenna_ids; // these apply to the *next* layer

    // Covered buildings and their IDs
    std::vector<size_t> building_ids;
    std::vector<double> building_coverages;
    std::vector<size_t> building_interval_offsets; // n+1 many

    Coverage coverage;

    friend class DynamicAntennaSet;
};

// This class is cheap to copy, and reasonably cheap to update.
struct DynamicAntennaSet {
    // Empty set with given coverage requirement
    DynamicAntennaSet(double min_coverage)
    { coverage.min = min_coverage; }

    // Faster alternative to populating an empty set incrementally
    DynamicAntennaSet(
        double min_coverage,
        std::span<const Antenna> antennas);

    // Get the number of covered buildings
    const Coverage &getCoverage() const
    { return coverage; }

    // Get the IDs of all antennas in the final set.
    std::vector<size_t> getAntennaIds() const;

    void addAntenna(const Antenna &antenna);

    void removeAntenna(const Antenna &antenna);

protected:

    std::optional<std::pair<const AntennaSet &, size_t>> findBuilding(size_t building_id);

    void merge(AntennaSet set);

    void gc();

    // TODO add a small "staging" buffer
    std::vector<std::shared_ptr<const AntennaSet>> layers; // nullptrs for absent layers
    Coverage coverage;

    size_t n_entries = 0;
    size_t n_dead = 0;
};
