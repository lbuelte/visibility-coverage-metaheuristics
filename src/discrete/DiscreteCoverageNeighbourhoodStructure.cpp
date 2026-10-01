//
// Created by Michael on 16.06.2026
// 

#include "discrete/DiscreteCoverageNeighbourhoodStructure.hpp"

#include <algorithm>
#include <cassert>
#include <deque>
#include <iostream>
#include <omp.h>
#include <stdexcept>
#include <vector>

#include "DiscreteCoverageInstance.hpp"
#include "Interval.hpp"

DiscreteCoverageNeighbourhoodStructure::DiscreteCoverageNeighbourhoodStructure(const DiscreteCoverageInstance &instance, unsigned k, double tau, std::vector<unsigned> antenna_candidates)
                                         : instance(instance), antenna_candidates(antenna_candidates), k(k), tau(tau) {
    num_polygons = instance.polygon_covered_by_antennas().size();
    antenna_id_bound = instance.antenna_covers_polygons().size();

    reset_to_empty();
}

DiscreteCoverageNeighbourhoodStructure::DiscreteCoverageNeighbourhoodStructure(const DiscreteCoverageInstance &instance, unsigned k, double tau, std::vector<unsigned> antenna_candidates, std::function<double(double)> coverage_eval_function, std::function<double(double, unsigned)> global_objective_function) : instance(instance), k(k), tau(tau), coverage_eval_function(coverage_eval_function), global_objective_function(global_objective_function) {
    num_polygons = instance.polygon_covered_by_antennas().size();
    antenna_id_bound = instance.antenna_covers_polygons().size();

    reset_to_empty();
}

void DiscreteCoverageNeighbourhoodStructure::set_new_global_objectve_function(std::function<double(double, unsigned)> p_global_objective_function) {
    global_objective_function = p_global_objective_function;
    objective_by_current_function = global_objective_function(total_weighted_coverage, num_polygons_covered);
    objective_by_current_function_peek = global_objective_function(total_weighted_coverage_peek, num_polygons_covered_peek);
}


void DiscreteCoverageNeighbourhoodStructure::set_new_polygon_coverage_function(std::function<double(double)> p_coverage_eval_function) {
    if (!clean)
        throw std::runtime_error("Objective function can only be changed when data structure is clean. Call clean_structure() first");

    coverage_eval_function = p_coverage_eval_function;
    total_weighted_coverage = 0.0;

    for (unsigned i = 0; i < k; i++) {
        unique_weighted_coverage[i] = 0.0;
    }

    for (unsigned i = 0; i < num_polygons; i++) {
        total_weighted_coverage += coverage_eval_function(current_coverage[i]);

        for (const SeenByAntennaInformation &antenna_info : currently_seen_by_antennas[i]) {
            unique_weighted_coverage[antenna_info.antenna_ID] += weighted_unique_coverage(current_coverage[i], antenna_info.unique_coverage);
        }
    }
}

double DiscreteCoverageNeighbourhoodStructure::objective_value() const {
    if (!clean_for_peek)
        throw std::runtime_error("Objective value can only be read when data structure is clean for peek. Call clean_structure_for_peek() first");

    return objective_by_current_function_peek;
}

unsigned DiscreteCoverageNeighbourhoodStructure::num_covered_polygons() const {
    if (!clean_for_peek)
        throw std::runtime_error("Objective value can only be read when data structure is clean for peek. Call clean_structure_for_peek() first");

    return num_polygons_covered_peek;
}

double DiscreteCoverageNeighbourhoodStructure::unweighted_coverage() const {
    if (!clean_for_peek)
        throw std::runtime_error("Objective value can only be read when data structure is clean for peek. Call clean_structure_for_peek() first");

    return total_coverage_peek;
}

double DiscreteCoverageNeighbourhoodStructure::weighted_coverage() const {
    if (!clean_for_peek)
        throw std::runtime_error("Objective value can only be read when data structure is clean for peek. Call clean_structure_for_peek() first");

    return total_weighted_coverage_peek;
}

std::vector<unsigned> DiscreteCoverageNeighbourhoodStructure::currentlySelectedAntennas() const {
    std::vector<unsigned> ret;

    for (unsigned i = 0; i < k; i++) {
        if (original_index[i] != none) {
            ret.emplace_back(original_index[i]);
        }
    }

    return ret;
}

unsigned DiscreteCoverageNeighbourhoodStructure::num_selected_antennas() const {
    return k - unused_indicies.size();
}

bool DiscreteCoverageNeighbourhoodStructure::antenna_selected(unsigned antenna) const {
    return reduced_antenna_ID(antenna) != none;
}

const std::vector<unsigned> & DiscreteCoverageNeighbourhoodStructure::get_antenna_candidates() {
    return antenna_candidates;
}

double DiscreteCoverageNeighbourhoodStructure::uniqueCoverageByAntenna(unsigned antenna) const {
    if (!clean)
        throw std::runtime_error("Antenna details can only be read when data structure is clean. Call clean_structure() first");

    if (reduced_antenna_ID(antenna) == none)
        return 0.0;

    return unique_coverage[reduced_antenna_ID(antenna)];
}

double DiscreteCoverageNeighbourhoodStructure::weightedCoverageByAntenna(unsigned antenna) const {
    if (!clean)
        throw std::runtime_error("Antenna details can only be read when data structure is clean. Call clean_structure() first");

    if (reduced_antenna_ID(antenna) == none)
        return 0.0;

    return unique_weighted_coverage[reduced_antenna_ID(antenna)];
}

unsigned DiscreteCoverageNeighbourhoodStructure::polygonsDependentOnAntenna(unsigned antenna) const {
    if (!clean)
        throw std::runtime_error("Antenna details can only be read when data structure is clean. Call clean_structure() first");

    if (reduced_antenna_ID(antenna) == none)
        return 0;

    return num_polygons_necessary_support[reduced_antenna_ID(antenna)];
}

const std::vector<double> &DiscreteCoverageNeighbourhoodStructure::coverage_by_polygon() const {
    if (!clean_for_peek)
        throw std::runtime_error("Coverage by polygon can only be read when data structure is clean for peek. Call clean_structure_for_peek() first");

    return current_coverage_peek;
}

void DiscreteCoverageNeighbourhoodStructure::reset_to_empty() {
    total_coverage = 0.0;
    total_weighted_coverage = 0.0;
    num_polygons_covered = 0;
    objective_by_current_function = 0.0;
    total_coverage_peek = 0.0;
    total_weighted_coverage_peek = 0.0;
    num_polygons_covered_peek = 0;
    objective_by_current_function_peek = 0.0;

    if (antenna_candidates.empty() || antenna_candidates.size() > (antenna_id_bound >> 4)) {
        reduced_antenna_index_vec.resize(antenna_id_bound, none);
        reduced_index_is_vector = true;
    }
    else {
        for (unsigned candidate : antenna_candidates) {
            reduced_antenna_index_map.emplace(candidate, none);
        }
        reduced_index_is_vector = false;
    }

    if (antenna_candidates.empty()) {
        antenna_candidates.resize(antenna_id_bound);
        for (unsigned i = 0; i < antenna_id_bound; i++) {
            antenna_candidates[i] = i;
        }
    }

    original_index.assign(k, none);
    unused_indicies.clear();
    for (unsigned i = 0; i < k; i++) {
        unused_indicies.emplace_back(i);
    }

    unique_coverage.assign(k, 0.0);
    unique_weighted_coverage.assign(k, 0.0);
    num_polygons_necessary_support.assign(k, 0);
    
    current_coverage.assign(num_polygons, 0.0);
    current_coverage_peek.assign(num_polygons, 0.0);
    currently_seen_by_antennas.assign(num_polygons, {});
    union_of_coverages.assign(num_polygons, {});
    visibility_regions.assign(num_polygons, {});

    dirty_polygons_queue.clear();
    dirty_polygon.assign(num_polygons, false);
    dirty_polygons_queue_peek.clear();
    dirty_polygon_peek.assign(num_polygons, false);

    unsigned temp_size = std::max(num_polygons, k);
    temp_bool.assign(temp_size, false);
    temp_double.assign(temp_size, 0.0);
    temp_int.assign(temp_size, 0);

    peek_coverage.assign(num_polygons, {});
}

void DiscreteCoverageNeighbourhoodStructure::add_antenna_lazy(unsigned antenna) {
    assert(!unused_indicies.empty());
    assert(antenna < antenna_id_bound);
    assert(antenna >= 0);

    if (antenna_selected(antenna)) {
        throw std::runtime_error("Do not add an antenna that is already selected\n");
    }

    unsigned reduced_id = unused_indicies.back();
    unused_indicies.pop_back();

    assert(reduced_id >= 0);
    assert(reduced_id < k);
    assert(original_index[reduced_id] == none);

    assert(unique_coverage[reduced_id] == 0.0);
    assert(unique_weighted_coverage[reduced_id] == 0.0);
    assert(num_polygons_necessary_support[reduced_id] == 0.0);

    original_index[reduced_id] = antenna;
    set_reduced_antenna_ID(antenna, reduced_id);

    for (const PolygonCoverage &cov : instance.antenna_covers_polygons()[antenna]) {
        remove_dirty_polygon_from_objective(cov.polygon);

        for (const Interval& interval : cov.intervals) {
            assert(interval.start >= -0.00001);
            assert(interval.end > interval.start);
            assert(interval.end <= 1.00001);
            visibility_regions[cov.polygon].emplace_back(interval.start, reduced_id, true);
            visibility_regions[cov.polygon].emplace_back(interval.end, reduced_id, false);
        }

        currently_seen_by_antennas[cov.polygon].emplace_back(reduced_id, 0.0);
    }

    // TODO Debug remove
    if (num_polygons >= 693) {
        if (!visibility_regions[693].empty()) {
            assert(visibility_regions[693].front().position >= -0.0001);
            assert(visibility_regions[693].front().position <= 1.0001);
        }
    }

    check_internal_consistency();
}

void DiscreteCoverageNeighbourhoodStructure::add_antennas_lazy(const std::vector<unsigned> &antennas) {
    for (unsigned antenna : antennas)
        add_antenna_lazy(antenna);
}

void DiscreteCoverageNeighbourhoodStructure::remove_antenna_lazy(unsigned antenna) {
    remove_antennas_lazy({antenna});
}

void DiscreteCoverageNeighbourhoodStructure::remove_antennas_lazy(const std::vector<unsigned> &antennas) {
    std::deque<unsigned> polygons_to_update;

    // Find all polygons for which the coverage has to be updated
    for (unsigned antenna : antennas) {
        assert(antenna >= 0);
        assert(antenna < antenna_id_bound);
        assert(reduced_antenna_ID(antenna) != none);

        unsigned reduced_id = reduced_antenna_ID(antenna);
        unique_coverage[reduced_id] = 0.0;
        unique_weighted_coverage[reduced_id] = 0.0;
        num_polygons_necessary_support[reduced_id] = 0;

        for (const PolygonCoverage &coverage : instance.antenna_covers_polygons()[antenna]) {
            assert(coverage.polygon >= 0);
            assert(coverage.polygon < num_polygons);
            if (!temp_bool[coverage.polygon]) {
                temp_bool[coverage.polygon] = true;
                polygons_to_update.emplace_back(coverage.polygon);
            }
        }
    }

    for (unsigned polygon : polygons_to_update) {
        temp_bool[polygon] = false;
    }

    // Mark all antennas that are now gone for more efficient removal from coverage
    for (unsigned antenna : antennas) {
        temp_bool[reduced_antenna_ID(antenna)] = true;
    }

    // Remove all antennas marked for removal from all affected polygons and mark as dirty
    for (unsigned polygon : polygons_to_update) {
        unsigned i = 0;
        while (i < visibility_regions[polygon].size()) {
            while (i < visibility_regions[polygon].size() && temp_bool[visibility_regions[polygon][i].antenna_ID]) {
                visibility_regions[polygon][i] = visibility_regions[polygon].back();
                visibility_regions[polygon].pop_back();
            }
            i++;
        }

        i = 0;
        while (i < currently_seen_by_antennas[polygon].size()) {
            while (i < currently_seen_by_antennas[polygon].size() && temp_bool[currently_seen_by_antennas[polygon][i].antenna_ID]) {
                currently_seen_by_antennas[polygon][i] = currently_seen_by_antennas[polygon].back();
                currently_seen_by_antennas[polygon].pop_back();
            }
            i++;
        }

        remove_dirty_polygon_from_objective(polygon);
    }
    
    // Remove antennas from antennas currently in use and clean up
    for (unsigned antenna : antennas) {
        unsigned reduced_id = reduced_antenna_ID(antenna);
        set_reduced_antenna_ID(antenna, none);
        original_index[reduced_id] = none;
        unused_indicies.emplace_back(reduced_id);

        temp_bool[reduced_id] = false;
    }

    check_internal_consistency();
}

void DiscreteCoverageNeighbourhoodStructure::remove_dirty_polygon_from_objective(unsigned polygon) {
    assert(polygon >= 0);
    assert(polygon < num_polygons);
    clean = false;
    clean_for_peek = false;

    if (!dirty_polygon_peek[polygon]) {
        dirty_polygon_peek[polygon] = true;
        dirty_polygons_queue_peek.emplace_back(polygon);

        if(current_coverage_peek[polygon] >= tau) {
            num_polygons_covered_peek--;
        }

        total_coverage_peek -= current_coverage_peek[polygon];
        total_weighted_coverage_peek -= coverage_eval_function(current_coverage_peek[polygon]);
        current_coverage_peek[polygon] = 0.0;
    }
    
    if (dirty_polygon[polygon])
        return;

    dirty_polygon[polygon] = true;
    dirty_polygons_queue.emplace_back(polygon);
    
    total_coverage -= current_coverage[polygon];
    total_weighted_coverage -= coverage_eval_function(current_coverage[polygon]);

    if (current_coverage[polygon] >= tau) {
        num_polygons_covered--;
    }

    for (SeenByAntennaInformation &antenna_info : currently_seen_by_antennas[polygon]) {
        assert(antenna_info.antenna_ID >= 0);
        assert(antenna_info.antenna_ID < k);
        unique_coverage[antenna_info.antenna_ID] -= antenna_info.unique_coverage;
        unique_weighted_coverage[antenna_info.antenna_ID] -= weighted_unique_coverage(current_coverage[polygon], antenna_info.unique_coverage);

        if (current_coverage[polygon] >= tau && current_coverage[polygon] - antenna_info.unique_coverage < tau) {
            num_polygons_necessary_support[antenna_info.antenna_ID]--;
        }
    }

    current_coverage[polygon] = 0.0;
}

double DiscreteCoverageNeighbourhoodStructure::polygon_values_with_peek(unsigned polygon) {
    return polygon_values_with_peek(polygon, peek_coverage[polygon]);
}

double DiscreteCoverageNeighbourhoodStructure::polygon_values_with_peek(unsigned polygon, std::vector<Interval> &polygon_peek_coverage) {
    assert(polygon >= 0);
    assert(polygon < num_polygons);
    double coverage = 0.0;
    double current_interval_start = 0.0, current_interval_end = 0.0;

    std::sort(polygon_peek_coverage.begin(), polygon_peek_coverage.end());

    unsigned i = 0, j = 0;

    while (i < union_of_coverages[polygon].size() &&  j < polygon_peek_coverage.size()) {
        double earlier_begin = std::min(union_of_coverages[polygon][i].start, polygon_peek_coverage[j].start);
        if (earlier_begin > current_interval_end) {
            coverage += current_interval_end - current_interval_start;
            current_interval_start = earlier_begin;
        }

        if (earlier_begin == union_of_coverages[polygon][i].start) {
            current_interval_end = std::max(current_interval_end, union_of_coverages[polygon][i].end);
            i++;
        }
        else {
            current_interval_end = std::max(current_interval_end, polygon_peek_coverage[j].end);
            j++;
        }
    }

    coverage += current_interval_end - current_interval_start;
    current_interval_start = current_interval_end;

    for (; i < union_of_coverages[polygon].size(); i++) {
        if (union_of_coverages[polygon][i].end> current_interval_end) {
            coverage += union_of_coverages[polygon][i].end - std::max(union_of_coverages[polygon][i].start, current_interval_end); // We use that these intervals are disjoint
        }
    }

    for (; j < polygon_peek_coverage.size(); j++) {
        if (polygon_peek_coverage[j].start > current_interval_end) {
            coverage += current_interval_end - current_interval_start;
            current_interval_start = polygon_peek_coverage[j].start;
        }

        current_interval_end = std::max(current_interval_end, polygon_peek_coverage[j].end);
    }

    coverage += current_interval_end - current_interval_start;

    return coverage;
}

PeekResult DiscreteCoverageNeighbourhoodStructure::peek_antennas(const std::vector<unsigned> &antennas) {
    if (!clean_for_peek)
        throw std::runtime_error("Peek called without data structure being clear for peek. Call clear_structure_for_peek() first");
    
    std::deque<unsigned> affected_polygons;
    unsigned num_additional_polygons_covered = 0;
    double gain_in_coverage = 0.0, gain_in_weighted_coverage = 0.0;

    for (unsigned antenna : antennas) {
        assert(antenna >= 0);
        assert(antenna < antenna_id_bound);
        for (const PolygonCoverage &coverage : instance.antenna_covers_polygons()[antenna]) {
            assert(coverage.polygon >= 0);
            assert(coverage.polygon < num_polygons);
            if (!temp_bool[coverage.polygon]) {
                affected_polygons.emplace_back(coverage.polygon);
                temp_bool[coverage.polygon] = true;
            }

            for (const Interval &interval : coverage.intervals) {
                peek_coverage[coverage.polygon].emplace_back(interval);
            }
        }
    }

    for (unsigned polygon : affected_polygons) {
        assert(polygon >= 0);
        assert(polygon < num_polygons);
        
        double coverage_with_peek = polygon_values_with_peek(polygon);

        gain_in_coverage += coverage_with_peek - current_coverage_peek[polygon];
        gain_in_weighted_coverage += coverage_eval_function(coverage_with_peek) - coverage_eval_function(current_coverage_peek[polygon]);

        if (coverage_with_peek >= tau && current_coverage_peek[polygon] < tau) {
            num_additional_polygons_covered++;
        }

        temp_bool[polygon] = false;
        peek_coverage[polygon].clear();
    }

    return {
        gain_in_coverage,
        gain_in_weighted_coverage,
        global_objective_function(total_weighted_coverage_peek + gain_in_weighted_coverage, num_polygons_covered_peek + num_additional_polygons_covered),
        static_cast<int>(num_additional_polygons_covered),
        num_polygons_covered_peek + num_additional_polygons_covered,
        total_coverage_peek + gain_in_coverage,
        total_weighted_coverage_peek + gain_in_weighted_coverage
    };
}

std::vector<PeekResult> DiscreteCoverageNeighbourhoodStructure::peek_antenna_sets_parallel(const std::vector<std::vector<unsigned>> &antenna_sets, unsigned num_threads) {
    if (!clean_for_peek)
        throw std::runtime_error("Peek called without data structure being clear for peek. Call clear_structure_for_peek() first");
    if (num_threads == 0)
        throw std::runtime_error("peek_antenna_sets_parallel can not be called with 0 threads");
    if (num_threads > omp_get_num_threads())
        std::cerr << "peek_antenna_sets_parallel called for " << num_threads << " threads, which is more than the number of threads of the system\n";

    if (num_threads > temp_bool_parallel.size())
        temp_bool_parallel.resize(num_threads, std::vector<bool>(num_polygons, false));
    if (num_threads > peek_coverage_parallel.size())
        peek_coverage_parallel.resize(num_threads, std::vector<std::vector<Interval>>(num_polygons));

    std::vector<std::vector<PeekResult>> thread_results(num_threads);
    #pragma omp parallel for 
    for (unsigned i = 0; i < num_threads; i++) {
        thread_results[i] = peek_antenna_sets_parallel_one_thread(antenna_sets, i, num_threads);
    }

    std::vector<PeekResult> ret;
    ret.reserve(antenna_sets.size());

    for (unsigned i = 0; i < num_threads; i++) {
        ret.insert(ret.end(), thread_results[i].begin(), thread_results[i].end());
    }

    return ret;
}

std::vector<PeekResult> DiscreteCoverageNeighbourhoodStructure::peek_antenna_sets_parallel_one_thread(const std::vector<std::vector<unsigned>> &antenna_sets, unsigned thread_id, unsigned num_threads) {
    unsigned start = thread_id * antenna_sets.size() / num_threads;
    unsigned end = (thread_id + 1) * antenna_sets.size() / num_threads;

    std::vector<PeekResult> thread_region_result;
    thread_region_result.reserve(end - start);

    for (unsigned i = start; i < end; i++) {
        thread_region_result.emplace_back(peek_specific_antenna_set_parallel(antenna_sets[i], thread_id));
    }

    return thread_region_result;
}

PeekResult DiscreteCoverageNeighbourhoodStructure::peek_specific_antenna_set_parallel(const std::vector<unsigned> &antennas, unsigned thread_id) {
    std::deque<unsigned> affected_polygons;
    unsigned num_additional_polygons_covered = 0;
    double gain_in_coverage = 0.0, gain_in_weighted_coverage = 0.0;

    for (unsigned antenna : antennas) {
        assert(antenna >= 0);
        assert(antenna < antenna_id_bound);
        for (const PolygonCoverage &coverage : instance.antenna_covers_polygons()[antenna]) {
            assert(coverage.polygon >= 0);
            assert(coverage.polygon < num_polygons);
            if (!temp_bool_parallel[thread_id][coverage.polygon]) {
                affected_polygons.emplace_back(coverage.polygon);
                temp_bool_parallel[thread_id][coverage.polygon] = true;
            }

            for (const Interval &interval : coverage.intervals) {
                peek_coverage_parallel[thread_id][coverage.polygon].emplace_back(interval);
            }
        }
    }

    for (unsigned polygon : affected_polygons) {
        assert(polygon >= 0);
        assert(polygon < num_polygons);
        
        double coverage_with_peek = polygon_values_with_peek(polygon, peek_coverage_parallel[thread_id][polygon]);

        gain_in_coverage += coverage_with_peek - current_coverage_peek[polygon];
        gain_in_weighted_coverage += coverage_eval_function(coverage_with_peek) - coverage_eval_function(current_coverage_peek[polygon]);

        if (coverage_with_peek >= tau && current_coverage_peek[polygon] < tau) {
            num_additional_polygons_covered++;
        }

        temp_bool[polygon] = false;
        peek_coverage[polygon].clear();
    }

    return {
        gain_in_coverage,
        gain_in_weighted_coverage,
        global_objective_function(total_weighted_coverage_peek + gain_in_weighted_coverage, num_polygons_covered_peek + num_additional_polygons_covered),
        static_cast<int>(num_additional_polygons_covered),
        num_polygons_covered_peek + num_additional_polygons_covered,
        total_coverage_peek + gain_in_coverage,
        total_weighted_coverage_peek + gain_in_weighted_coverage
    };
}

void DiscreteCoverageNeighbourhoodStructure::clean_structure() {
    while (!dirty_polygons_queue.empty()) {
        assert(dirty_polygon[dirty_polygons_queue.back()]);
        clean_polygon(dirty_polygons_queue.back());
        dirty_polygons_queue.pop_back();
    }

    for (unsigned polygon : dirty_polygons_queue_peek) {
        assert(!dirty_polygon_peek[polygon]);
    }

    dirty_polygons_queue_peek.clear();

    objective_by_current_function = global_objective_function(total_weighted_coverage, num_polygons_covered);

    objective_by_current_function_peek = objective_by_current_function;
    total_coverage_peek = total_coverage;
    total_weighted_coverage_peek = total_weighted_coverage;
    num_polygons_covered_peek = num_polygons_covered;

    clean = true;
    clean_for_peek = true;

    check_internal_consistency();
}

void DiscreteCoverageNeighbourhoodStructure::clean_polygon(unsigned polygon) {
    assert(polygon >= 0);
    assert(polygon < num_polygons);
    if (!dirty_polygon[polygon])
        return;

    dirty_polygon[polygon] = false;
    dirty_polygon_peek[polygon] = false;

    current_coverage[polygon] = 0.0;
    union_of_coverages[polygon].clear();

    std::sort(visibility_regions[polygon].begin(), visibility_regions[polygon].end());

    std::vector<unsigned> antennas_currently_seeing;
    double last_pos = 0.0;
    double current_interval_start = 0.0;

    for (const VisibilityIntervalBoundary &boundary : visibility_regions[polygon]) {
        if (antennas_currently_seeing.size() == 1) {
            // If there is currently only one antenna observing, it is uniquely observing everything from the last boundary to this one
            temp_double[antennas_currently_seeing[0]] += boundary.position - last_pos;
        }
        else if (antennas_currently_seeing.empty()) {
            // Mark that new interval for total coverage starts
            assert(boundary.start);
            current_interval_start = boundary.position;
        }
        last_pos = boundary.position;
        
        if (boundary.start) {
            temp_int[boundary.antenna_ID] = antennas_currently_seeing.size();
            antennas_currently_seeing.emplace_back(boundary.antenna_ID);
        }
        else {
            unsigned pos_in_seeing_list = temp_int[boundary.antenna_ID];
            antennas_currently_seeing[pos_in_seeing_list] = antennas_currently_seeing.back();
            temp_int[antennas_currently_seeing.back()] = pos_in_seeing_list;
            antennas_currently_seeing.pop_back();
            temp_int[boundary.antenna_ID] = 0;
        }

        // If no antenna sees after this boundary we can add an interval to the union of visibility regions
        if (antennas_currently_seeing.empty()) {
            union_of_coverages[polygon].emplace_back(current_interval_start, boundary.position);
            current_coverage[polygon] += boundary.position - current_interval_start;
        }
    }

    for (SeenByAntennaInformation &antenna_info : currently_seen_by_antennas[polygon]) {
        antenna_info.unique_coverage = temp_double[antenna_info.antenna_ID];
        temp_double[antenna_info.antenna_ID] = 0.0;

        unique_coverage[antenna_info.antenna_ID] += antenna_info.unique_coverage;
        unique_weighted_coverage[antenna_info.antenna_ID] += weighted_unique_coverage(current_coverage[polygon], antenna_info.unique_coverage);
        if (current_coverage[polygon] >= tau && current_coverage[polygon] - antenna_info.unique_coverage < tau)
            num_polygons_necessary_support[antenna_info.antenna_ID]++;
    }

    if (current_coverage[polygon] >= tau)
        num_polygons_covered++;

    total_coverage += current_coverage[polygon];
    total_weighted_coverage += coverage_eval_function(current_coverage[polygon]);

    current_coverage_peek[polygon] = current_coverage[polygon];
}

bool DiscreteCoverageNeighbourhoodStructure::is_clean() const {
    return clean;
}

void DiscreteCoverageNeighbourhoodStructure::clean_structure_for_peek() {
    if (clean_for_peek)
        return;
    
    while (!dirty_polygons_queue_peek.empty()) {
        unsigned polygon = dirty_polygons_queue_peek.back();
        dirty_polygons_queue_peek.pop_back();
        clean_polygon_for_peek(polygon);
    }

    objective_by_current_function_peek = global_objective_function(total_weighted_coverage_peek, num_polygons_covered_peek);
    
    clean_for_peek = true;

    check_internal_consistency();
}

bool DiscreteCoverageNeighbourhoodStructure::is_clean_for_peek() const {
    return clean_for_peek;
}

void DiscreteCoverageNeighbourhoodStructure::clean_polygon_for_peek(unsigned polygon) {
    assert(polygon >= 0);
    assert(polygon < num_polygons);
    if (!dirty_polygon_peek[polygon])
        return;

    dirty_polygon_peek[polygon] = false;

    union_of_coverages[polygon].clear();

    std::sort(visibility_regions[polygon].begin(), visibility_regions[polygon].end());

    double current_interval_start = 0.0;
    unsigned antennas_currently_seeing = 0;
    double coverage = 0.0;

    for (const VisibilityIntervalBoundary &boundary : visibility_regions[polygon]) {
        if (antennas_currently_seeing == 0)
            current_interval_start = boundary.position;
        
        if (boundary.start) {
            antennas_currently_seeing++;
        }
        else {
            antennas_currently_seeing--;
        }

        // If no antenna sees after this boundary we can add an interval to the union of visibility regions
        if (antennas_currently_seeing == 0) {
            union_of_coverages[polygon].emplace_back(current_interval_start, boundary.position);
            coverage += boundary.position - current_interval_start;
        }
    }

    // std::cout << "Polygon " << polygon << " has visibility regions:\n";
    // for(const VisibilityIntervalBoundary &boundary : visibility_regions[polygon]) {
    //     std::cout << boundary.position << " start: " << boundary.start << " for antenna " << boundary.antenna_ID << "\n";
    // }
    // std::cout << "Get compressed to:\n";
    // for(const Interval& interval : union_of_coverages[polygon]) {
    //     std::cout << "[" << interval.start << ", " << interval.end << "]\n";
    // }

    // std::cout << "Related coverage: " << coverage << "\n";

    total_coverage_peek += coverage;
    total_weighted_coverage_peek += coverage_eval_function(coverage);
    if (coverage >= tau)
        num_polygons_covered_peek++;

    current_coverage_peek[polygon] = coverage;
}

double DiscreteCoverageNeighbourhoodStructure::weighted_unique_coverage(double total_coverage, double unique_coverage) const {
    return coverage_eval_function(total_coverage) - coverage_eval_function(total_coverage - unique_coverage);
}


unsigned DiscreteCoverageNeighbourhoodStructure::reduced_antenna_ID(unsigned antenna) const {
    if (reduced_index_is_vector)
        return reduced_antenna_index_vec[antenna];

    if (reduced_antenna_index_map.contains(antenna))
        return reduced_antenna_index_map.at(antenna);

    return none;
}

void DiscreteCoverageNeighbourhoodStructure::set_reduced_antenna_ID(unsigned antenna, unsigned reduced_id) {
    if (reduced_index_is_vector) {
        reduced_antenna_index_vec[antenna] = reduced_id;
    }
    else {
        reduced_antenna_index_map[antenna] = reduced_id;
    }
}

void DiscreteCoverageNeighbourhoodStructure::check_internal_consistency() const {
#ifndef NDEBUG
    unsigned temp_size = std::max(num_polygons, k);

    assert(temp_bool.size() == temp_size);
    assert(temp_int.size() == temp_size);
    assert(temp_double.size() == temp_size);
    for (unsigned i = 0; i < temp_size; i++) {
        assert(temp_int[i] == 0);
        assert(temp_bool[i] == false);
        assert(temp_double[i] == 0.0);
    }

    assert(unused_indicies.size() <= k);
    assert(original_index.size() == k);
    assert(unique_coverage.size() == k);
    assert(unique_weighted_coverage.size() == k);
    assert(num_polygons_necessary_support.size() == k);
    
    for (unsigned i : unused_indicies) {
        assert(i < k);
        assert(original_index[i] == none);
    }

    for (unsigned i = 0; i < antenna_id_bound; i++) {
        if (reduced_antenna_ID(i) != none) {
            assert(reduced_antenna_ID(i) < k);
            assert(original_index[reduced_antenna_ID(i)] == i);
        }
    }

    for (unsigned i = 0; i < k; i++) {
        if (original_index[i] != none) {
            assert(original_index[i] < antenna_id_bound);
            assert(reduced_antenna_ID(original_index[i]) == i);
        }
        assert(unique_coverage[i] >= -0.001);
        assert(unique_weighted_coverage[i] >= -0.001);
        assert(num_polygons_necessary_support[i] >= 0);
        assert(unique_coverage[i] <= num_polygons);
        assert(num_polygons_necessary_support[i] <= num_polygons);
    }

    assert(current_coverage.size() == num_polygons);
    assert(currently_seen_by_antennas.size() == num_polygons);
    assert(union_of_coverages.size() == num_polygons);
    assert(visibility_regions.size() == num_polygons);
    for (unsigned i = 0; i < num_polygons; i++) {
        assert(currently_seen_by_antennas[i].size() <= k);        

        for (const auto &ref : currently_seen_by_antennas[i]) {
            assert(ref.antenna_ID < k);
            assert(original_index[ref.antenna_ID] != none);
        }

        for (const auto &ref : union_of_coverages[i]) {
            assert(ref.start >= -0.001);
            assert(ref.end <= 1.001);
        }

        for (const auto &ref : visibility_regions[i]) {
            assert(ref.antenna_ID < k);
            assert(ref.position >= -0.001);
            assert(ref.position <= 1.001);
        }
    }

    assert(dirty_polygons_queue.size() <= num_polygons);

    for (unsigned i : dirty_polygons_queue) {
        assert (i < num_polygons);
        assert(dirty_polygon[i]);
    }

    for (unsigned i = 0; i < num_polygons; i++) {
        std::vector<VisibilityIntervalBoundary> visible_on_polygon = visibility_regions[i];
        std::sort(visible_on_polygon.begin(), visible_on_polygon.end());

        unsigned num_antennas_currently_seeing = 0;
        double current_interval_start = 0.0;
        double coverage = 0.0;

        for (const VisibilityIntervalBoundary &boundary : visible_on_polygon) {
            if (num_antennas_currently_seeing == 1 && !boundary.start) {
                coverage += boundary.position - current_interval_start;
            }
            if (num_antennas_currently_seeing == 0) {
                assert(boundary.start);
                current_interval_start = boundary.position;
            }

            if (boundary.start)
                num_antennas_currently_seeing++;
            else
                num_antennas_currently_seeing--;
        }

        if (!dirty_polygon[i]) {
            assert(coverage == current_coverage[i]);
        }
        if (!dirty_polygon_peek[i]) {
            assert(coverage == current_coverage_peek[i]);

            for (unsigned j = 0; j + 1 < union_of_coverages[i].size(); j++) {
                assert(union_of_coverages[i][j].end <= union_of_coverages[i][j + 1].start);
            }

            unsigned j = 0;
            for (VisibilityIntervalBoundary &boundary : visible_on_polygon) {
                while (union_of_coverages[i][j].end < boundary.position)
                    j++;
                assert(boundary.position >= union_of_coverages[i][j].start);
                assert(boundary.position <= union_of_coverages[i][j].end);
            }

            j = 0;
            for (const Interval &interval : union_of_coverages[i]) {
                while (visible_on_polygon[j].position < interval.start)
                    j++;
                assert(visible_on_polygon[j].position == interval.start);
            }

            j = 0;
            for (const Interval &interval : union_of_coverages[i]) {
                while (visible_on_polygon[j].position < interval.end)
                    j++;
                assert(visible_on_polygon[j].position == interval.end);
            }
        }

    }

    if (clean_for_peek) {
        assert(dirty_polygons_queue_peek.empty());
        double check_total_coverage = 0.0, check_weighted_coverage = 0.0;

        for (unsigned i = 0; i < num_polygons; i++) {
            assert(!dirty_polygon_peek[i]);
            check_total_coverage += current_coverage_peek[i];
            check_weighted_coverage += coverage_eval_function(current_coverage_peek[i]);
        }

        assert(std::abs(total_coverage_peek - check_total_coverage) < 0.0001);
        assert(std::abs(total_weighted_coverage_peek- check_weighted_coverage) < 0.0001);
    }
#endif
}
