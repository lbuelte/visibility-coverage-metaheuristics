#include <bit>
#include <cassert>
#include <cstddef>
#include <memory>
#include <optional>
#include <vector>
#include <span>
#include <algorithm>
#include <map>

#include "discrete/AntennaSet.hpp"
#include "Interval.hpp"

AntennaSet::AntennaSet(double min_coverage, std::span<const Antenna> antennas) {
    coverage.min = min_coverage;

    // Ordered map so buildings are added in sorted building_id order,
    // as findBuilding (binary search) and merge (sorted merge) require.
    // TODO a bit of a hack, there are better ways to do this
    std::map<size_t, std::vector<Interval>> building_coverage; // building id -> intervals
    for (const auto &antenna : antennas) {
        antenna_ids.push_back(antenna.id);
        for (const auto &ab : antenna.covered) {
            auto &intervals = building_coverage[ab.id];
            intervals.insert(intervals.end(), ab.intervals.begin(), ab.intervals.end());
        }
    }
    // antenna_ids must stay sorted: merge/getAntennaIds rely on it.
    // TODO alternatively demand this of the caller?
    std::sort(antenna_ids.begin(), antenna_ids.end());
    for (auto &[b_id, intervals] : building_coverage) {
        std::sort(intervals.begin(), intervals.end(), [](const auto &a, const auto &b) {
            return a.start < b.start;
        });
        addBuilding(b_id, intervals);
    }
    finalize();
}

AntennaSet AntennaSet::merge(const AntennaSet &base) const {
    assert(coverage.min == base.coverage.min);
    AntennaSet res(coverage.min);

    // Sorted merge
    size_t i = 0;
    size_t j = 0;
    while (i < building_coverages.size() && j < base.building_coverages.size()) {
        if (building_ids[i] < base.building_ids[j]) {
            res.copyBuilding(*this, i);
            ++i;
        } else if (base.building_ids[j] < building_ids[i]) {
            res.copyBuilding(base, j);
            ++j;
        } else {
            // same id, this wins over base
            res.copyBuilding(*this, i);
            ++i;
            ++j;
        }
    }

    for (; i < building_coverages.size(); ++i)
        res.copyBuilding(*this, i);
    for (; j < base.building_coverages.size(); ++j)
        res.copyBuilding(base, j);

    std::vector<size_t> base_antenna_ids;
    std::vector<size_t> remaining_removal_ids;
    {
        auto it1 = base.antenna_ids.begin();
        auto it2 = removed_antenna_ids.begin();
        while (it1 != base.antenna_ids.end() && it2 != removed_antenna_ids.end()) {
            if (*it1 < *it2) {
                base_antenna_ids.emplace_back(*it1); // keep
                ++it1;
            } else if (*it1 == *it2) {
                // Do nothing, the removal has been applied.
                ++it1;
                ++it2;
            } else {
                remaining_removal_ids.emplace_back(*it2); // propagate removal
                ++it2;
            }
        }
        base_antenna_ids.insert(base_antenna_ids.end(), it1, base.antenna_ids.end());
        remaining_removal_ids.insert(remaining_removal_ids.end(), it2, removed_antenna_ids.end());
    }
    std::set_union(
        base_antenna_ids.begin(), base_antenna_ids.end(),
        antenna_ids.begin(), antenna_ids.end(),
        std::back_inserter(res.antenna_ids)
    );
    
    std::merge(
        remaining_removal_ids.begin(), remaining_removal_ids.end(),
        base.removed_antenna_ids.begin(), base.removed_antenna_ids.end(),
        std::back_inserter(res.removed_antenna_ids)
    );
    res.finalize();
    return res;
}

std::optional<size_t> AntennaSet::findBuilding(size_t building_id) const {
    auto it = std::lower_bound(building_ids.begin(), building_ids.end(), building_id);
    if (it == building_ids.end() || *it != building_id)
        return std::nullopt;
    return std::distance(building_ids.begin(), it);
}

void AntennaSet::addBuilding(
    size_t b_id,
    double b_coverage,
    std::span<const Interval> b_intervals)
{
    building_ids.emplace_back(b_id);
    building_coverages.emplace_back(b_coverage);
    coverage.update(b_coverage);
    building_interval_offsets.emplace_back(intervals.size());
    intervals.insert(intervals.end(), b_intervals.begin(), b_intervals.end());
}

std::span<const Interval> AntennaSet::getIntervals(size_t i) const {
    size_t j = building_interval_offsets[i];
    size_t k = building_interval_offsets[i+1];
    return {intervals.data() + j, k - j};
}

void AntennaSet::copyBuilding(const AntennaSet &other, size_t i) {
    addBuilding(
        other.building_ids[i],
        other.building_coverages[i],
        other.getIntervals(i)
    );
}

void AntennaSet::gc() {
    size_t i = 0;
    for (size_t j = 0; j < building_ids.size(); ++j) {
        if (building_coverages[j] == 0)
            continue;
        building_ids[i] = building_ids[j];
        building_coverages[i] = building_coverages[j];
        building_interval_offsets[i] = building_interval_offsets[j];
        ++i;
    }
    building_ids.resize(i);
    building_coverages.resize(i);
    building_interval_offsets[i] = intervals.size();
    building_interval_offsets.resize(i + 1);
}


DynamicAntennaSet::DynamicAntennaSet(
    double min_coverage,
    std::span<const Antenna> antennas)
{
    coverage.min = min_coverage;
    if (antennas.empty())
        return;
    AntennaSet base(min_coverage, antennas);
    size_t n_buildings = base.building_ids.size();
    const auto place = std::bit_width(n_buildings);
    layers.resize(place + 1);
    coverage = base.coverage;
    layers[place] = std::make_shared<const AntennaSet>(std::move(base));
    n_entries = n_buildings;
}

void DynamicAntennaSet::addAntenna(const Antenna &antenna) {
    // Basic idea: Update all affected buildings, then merge.
    AntennaSet affected(coverage.min);
    std::vector<Interval> m_intervals;
    for (const auto &ab : antenna.covered) {
        const auto pair = findBuilding(ab.id);
        if (!pair) {
            affected.addBuilding(ab.id, ab.intervals);
            ++n_entries;
        } else {
            const auto &as = pair->first;
            const auto bi = pair->second;
            // Undo the counting of the old coverage (so we don't count it twice when merging)
            coverage.downdate(as.building_coverages[bi]);
            ++n_dead;
            const auto intervals = as.getIntervals(bi);

            m_intervals.clear();
            unionIntervals(intervals, ab.intervals, m_intervals);

            affected.addBuilding(ab.id, m_intervals);
        }
    }
    affected.antenna_ids.emplace_back(antenna.id);
    affected.finalize();
    merge(std::move(affected));
}

void DynamicAntennaSet::removeAntenna(const Antenna &antenna) {
    AntennaSet affected(coverage.min);
    std::vector<Interval> m_intervals;
    for (const auto &ab : antenna.covered) {
        // Very similar to the logic in addAntenna, except we remove intervals in the sorted merge.
        const auto pair = findBuilding(ab.id).value();
        const auto &as = pair.first;
        const auto bi = pair.second;
        // Undo the counting of the old coverage (so we don't count it twice when merging)
        coverage.downdate(as.building_coverages[bi]);
        ++n_dead;

        const auto intervals = as.getIntervals(bi);
        m_intervals.clear();
        removeIntervals(intervals, ab.intervals, m_intervals);

        affected.addBuilding(ab.id, m_intervals);
        if (m_intervals.empty())
            --n_entries;
    }
    affected.removed_antenna_ids.emplace_back(antenna.id);
    affected.finalize();
    merge(std::move(affected));
}

std::vector<size_t> DynamicAntennaSet::getAntennaIds() const {
    std::vector<size_t> res;
    for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
        if (!*it)
            continue;
        // First remove
        const auto removed_antenna_ids = (*it)->removed_antenna_ids;
        std::vector<size_t> filtered_res;
        // TODO this could be done in place, same above
        std::set_difference(
            res.begin(), res.end(),
            removed_antenna_ids.begin(), removed_antenna_ids.end(),
            std::back_inserter(filtered_res)
        );
        // Then add
        res.clear();
        const auto antenna_ids = (*it)->antenna_ids;
        std::set_union(
            filtered_res.begin(), filtered_res.end(),
            antenna_ids.begin(), antenna_ids.end(),
            std::back_inserter(res)
        );
    }
    return res;
}

std::optional<std::pair<const AntennaSet &, size_t>> DynamicAntennaSet::findBuilding(size_t building_id) {
    for (const auto &layer : layers) {
        if (!layer)
            continue;
        auto i = layer->findBuilding(building_id);
        if (i)
            return std::pair<const AntennaSet &, size_t>(*layer, *i);
    }
    return std::nullopt;
}

void DynamicAntennaSet::merge(AntennaSet set) {
    // Precondition: Coverage of all the overwritten buildings has already been downdated()
    coverage = coverage.merge(set.coverage);
    // Can't just do std::bit_width(set.buildings.size()) to "skip to the right place"
    // since findBuilding() assumes that lower layers are more recent.
    // This should not be a big problem once we have a staging buffer.
    size_t place = 0;
    for (; place < layers.size() && layers[place]; ++place) {
        auto conflicting_set = layers[place]; // note: keep the shared ptr alive
        layers[place].reset();
        set = set.merge(*conflicting_set);
    }
    if (place >= layers.size())
        layers.emplace_back();
    layers[place] = std::make_shared<const AntennaSet>(std::move(set));
    if (n_dead >= n_entries/2)
        gc();
}

void DynamicAntennaSet::gc() {
    AntennaSet top(coverage.min); // TODO can init with first layer
    for (const auto &layer : layers) {
        if (!layer)
            continue;
        top = top.merge(*layer);
    }
    top.gc();
    assert(top.removed_antenna_ids.empty());
    assert(top.building_ids.size() == n_entries);
    const auto place = std::bit_width(top.building_ids.size());
    layers.clear();
    layers.resize(place + 1);
    coverage = top.coverage;
    layers[place] = std::make_shared<const AntennaSet>(std::move(top));
    n_dead = 0;
}
