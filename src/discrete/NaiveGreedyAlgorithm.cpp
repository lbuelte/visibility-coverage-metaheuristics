//
// Created by philip on 6/10/26.
//

#include "discrete/NaiveGreedyAlgorithm.hpp"

Solution NaiveGreedyAlgorithm::run(DiscreteCoverageInstance const &instance, 
                                   const unsigned k, const double tau, 
                                   const unsigned timelimit_in_ms, 
                                   const unsigned seed,
                                   std::vector<unsigned> const &partial_solution_antenna_ids) {
    for (unsigned step = 0; step < k; ++step) {
        unsigned best = find_best_antenna();
        if (best == -1) break;
        const double gain = compute_antenna_gain(best);
        commit_antenna(best);
        std::cout << "Step " << step + 1 << ": antenna " << best
                << "  gain=" << gain << std::endl;
    }
    return Solution(instance, k, tau, get_solution_antenna_ids());
    std::cout << "Done with value: "<<get_number_of_covered_polygons() <<"\n";
}

std::vector<unsigned> NaiveGreedyAlgorithm::get_polygons_with_coverage_above_tau() const {
    std::vector<unsigned> result;
    for (unsigned p = 0; p < nr_of_polygons; ++p)
        if (current_coverage_for_polygons[p] >= tau)
            result.push_back(p);
    return result;
}

std::vector<unsigned> NaiveGreedyAlgorithm::get_polygons_with_coverage_below_tau() const {
    std::vector<unsigned> result;
    for (unsigned p = 0; p < nr_of_polygons; ++p)
        if (current_coverage_for_polygons[p] < tau)
            result.push_back(p);
    return result;
}

const std::vector<unsigned> NaiveGreedyAlgorithm::get_solution_antenna_ids() const {
    return selected_antennas;
}

const std::vector<std::pair<AntennaData, Arrangement>> NaiveGreedyAlgorithm::get_antenna_and_arrangement_of_solution() const {
    std::vector<std::pair<AntennaData,Arrangement>> result;
    result.reserve(selected_antennas.size());
    for (unsigned id : selected_antennas)
        result.push_back(instance.get_antenna_and_arrangement(id));
    return result;
}

const std::vector<std::pair<Poly2, double>> NaiveGreedyAlgorithm::get_covered_polygons_with_coverage() const {
    std::vector<std::pair<Poly2, double>> result;
    for (unsigned p : get_polygons_with_coverage_above_tau())
        result.emplace_back(instance.get_polygon_data(p).poly,
                            current_coverage_for_polygons[p]);
    return result;
}

const std::vector<std::pair<Poly2, double>> NaiveGreedyAlgorithm::get_uncovered_polygons_with_coverage() const {
    std::vector<std::pair<Poly2, double>> result;
    for (unsigned p : get_polygons_with_coverage_below_tau())
        result.emplace_back(instance.get_polygon_data(p).poly,
                            current_coverage_for_polygons[p]);
    return result;
}

unsigned NaiveGreedyAlgorithm::get_number_of_covered_polygons() const {
    return static_cast<unsigned>(get_polygons_with_coverage_above_tau().size());
}

unsigned NaiveGreedyAlgorithm::find_best_antenna() const {
    unsigned    best_antenna = 0;
    double best_gain    = -1.0;
    for (unsigned a = 0; a < instance.antenna_covers_polygons().size(); ++a) {
        double gain = compute_antenna_gain(a);
        if (gain > best_gain) {
            best_gain    = gain;
            best_antenna = a;
        }
    }
    return best_antenna;
}

double NaiveGreedyAlgorithm::compute_antenna_gain(unsigned antenna) const {
    double numeric_modified_gain = 0.0;
    double newly_covered_polygons=0;
    for (const auto &cov : instance.antenna_covers_polygons()[antenna]) {
        auto [mod_gain, is_covered]=compute_polygon_gain(antenna, cov.polygon);
        numeric_modified_gain += mod_gain;
        newly_covered_polygons+=is_covered;
    }
    return combined_gain( numeric_modified_gain,  newly_covered_polygons);
}

double NaiveGreedyAlgorithm::combined_gain(double numeric_modified_gain, double newly_covered_polygons) const {
    return combined_gain_interpolated(numeric_modified_gain,newly_covered_polygons);
}

double NaiveGreedyAlgorithm::combined_gain_interpolated(double numeric_gain, double covered_polygon_gain) const {
    double progress=(double)selected_antennas.size()/(double)k;
    return (1-progress)*numeric_gain + progress*covered_polygon_gain;
}

double NaiveGreedyAlgorithm::combined_gain_only_numeric(const double numeric_gain, double covered_polygon_gain) {
    return numeric_gain;
}

std::pair<double, int> NaiveGreedyAlgorithm::compute_polygon_gain(const unsigned antenna, const unsigned polygon) const {
    auto [new_coverage, _] = union_coverage(polygon_interval_after_adding_antenna(antenna, polygon));
    double gain = modified_numeric_gain(current_coverage_for_polygons[polygon], new_coverage);
    int newly_covered = (new_coverage >= tau && current_coverage_for_polygons[polygon] < tau) ? 1 : 0;
    return {gain, newly_covered};
}

void NaiveGreedyAlgorithm::commit_antenna(const unsigned antenna) {
    selected_antennas.push_back(antenna);
    for (unsigned p = 0; p < nr_of_polygons; ++p) {
        auto [new_coverage, new_intervals] = union_coverage(polygon_interval_after_adding_antenna(antenna, p));
        current_coverage_for_polygons[p]           = new_coverage;
        current_coverage_intervals_for_polygons[p] = std::move(new_intervals);
    }
}

//this is rather inefficient
std::vector<Interval> NaiveGreedyAlgorithm::polygon_interval_after_adding_antenna(unsigned antenna, unsigned polygon) const {
    std::vector<Interval> combined = current_coverage_intervals_for_polygons[polygon];
    for (const auto &cov : instance.antenna_covers_polygons()[antenna])
        if (cov.polygon == polygon)
            combined.insert(combined.end(), cov.intervals.begin(), cov.intervals.end());
    return combined;
}

double NaiveGreedyAlgorithm::modified_numeric_gain(const double old_coverage, const double new_coverage) const {
    return linear_gain(old_coverage, new_coverage);
}

double NaiveGreedyAlgorithm::linear_gain(double old_coverage, double new_coverage) const {
    if (new_coverage>tau) new_coverage=tau;
    if (old_coverage>tau) old_coverage=tau;
    return new_coverage - old_coverage;
}

std::pair<double, std::vector<Interval>> NaiveGreedyAlgorithm::union_coverage(std::vector<Interval> intervals) {
    if (intervals.empty()) return {0.0, {}};

    std::sort(intervals.begin(), intervals.end(),
              [](const Interval &a, const Interval &b) {
                  return a.start < b.start;
              });

    std::vector<Interval> merged;
    double coverage = 0.0;
    double sweep     = intervals[0].start;
    double sweep_end = intervals[0].end;

    for (const auto &iv : intervals) {
        if (iv.start > sweep_end) {
            coverage += sweep_end - sweep;
            merged.emplace_back(sweep, sweep_end);
            sweep     = iv.start;
            sweep_end = iv.end;
        } else {
            sweep_end = std::max(sweep_end, iv.end);
        }
    }
    coverage += sweep_end - sweep;
    merged.emplace_back(sweep, sweep_end);

    return {coverage, merged};
}
