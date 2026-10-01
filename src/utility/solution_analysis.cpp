#include "utility/solution_analysis.hpp"
#include "geometry/DiscreteCoverageInstance.hpp"
#include "geometry/Interval.hpp"

#include <algorithm>
#include <iostream>
#include <vector>

namespace gis_utility {

    struct EssentialAntennasForPolygonStruct {
        unsigned num_antennas_observing_polygon;
        unsigned num_antennas_necessary_for_service_upper_bound;
    };

    struct CoverageByAntenna {
        std::vector<Interval> intervals;
        unsigned antenna_id;
    };

    struct MinAntennaUsageStatistics {
        std::vector<unsigned> antenna_appearance_in_min_solution;
        std::vector<EssentialAntennasForPolygonStruct> polygin_min_solution_values;
        std::vector<double> polygon_visibility_in_min_solution;
    };

    double coverage_with_additional_antenna(const std::vector<Interval> &covered_intervals, const CoverageByAntenna &coverage) {
        double covered_area = 0.0;
        double current_interval_start = 0.0, current_interval_end = 0.0;

        unsigned i = 0, j = 0;

        while (i < covered_intervals.size() &&  j < coverage.intervals.size()) {
            double earlier_begin = std::min(covered_intervals[i].start, coverage.intervals[j].start);
            if (earlier_begin > current_interval_end) {
                covered_area += current_interval_end - current_interval_start;
                current_interval_start = earlier_begin;
            }

            if (earlier_begin == covered_intervals[i].start) {
                current_interval_end = std::max(current_interval_end, covered_intervals[i].end);
                i++;
            }
            else {
                current_interval_end = std::max(current_interval_end, coverage.intervals[j].end);
                j++;
            }
        }

        covered_area += current_interval_end - current_interval_start;
        current_interval_start = current_interval_end;

        for (; i < covered_intervals.size(); i++) {
            if (covered_intervals[i].start > current_interval_end) {
                covered_area += current_interval_end - current_interval_start;
                current_interval_start = covered_intervals[i].start;
            }

            current_interval_end = std::max(current_interval_end, covered_intervals[i].end);
        }

        for (; j < coverage.intervals.size(); j++) {
            if (coverage.intervals[j].start > current_interval_end) {
                covered_area += current_interval_end - current_interval_start;
                current_interval_start = coverage.intervals[j].start;
            }

            current_interval_end = std::max(current_interval_end, coverage.intervals[j].end);
        }

        covered_area += current_interval_end - current_interval_start;

        return covered_area;
    }

    std::vector<unsigned> greedy_antennas_needed(const std::vector<CoverageByAntenna> &coverages, double tau) {
        std::vector<Interval> already_covered;
        double current_coverage = 0.0;

        std::vector<unsigned> antennas_selected;

        while (current_coverage < tau) {
            double best_next_coverage = -1.0;
            CoverageByAntenna best_next_antenna;

            for (const CoverageByAntenna &coverage : coverages) {
                double coverage_with_antenna = coverage_with_additional_antenna(already_covered, coverage);
                if (coverage_with_antenna > best_next_coverage) {
                    best_next_coverage = coverage_with_antenna;
                    best_next_antenna = coverage;
                }
            }

            for (const Interval &interval : best_next_antenna.intervals) {
                already_covered.emplace_back(interval);
            }
            current_coverage = best_next_coverage;
            std::sort(already_covered.begin(), already_covered.end());

            antennas_selected.emplace_back(best_next_antenna.antenna_id);
        }

        return antennas_selected;
    }

    MinAntennaUsageStatistics analyse_solution_for_essential_antennas(const DiscreteCoverageInstance &instance, const Solution &solution)  {
        std::vector<std::vector<CoverageByAntenna>> covered_intervals_by_antenna(instance.get_number_polygons());

        std::vector<unsigned> antenna_needed_how_often(instance.get_number_antennas(), 0);

        std::vector<double> coverage_of_selected_antennas;

        for (unsigned antenna : solution.get_solution_antenna_ids()) {
            for (const PolygonCoverage &coverage : instance.antenna_covers_polygons()[antenna]) {
                covered_intervals_by_antenna[coverage.polygon].emplace_back(coverage.intervals, antenna);
            }
        }

        std::vector<EssentialAntennasForPolygonStruct> polygon_coverage_analysis(instance.get_number_polygons()); 

        for (unsigned i : solution.get_serviced_polygon_ids()) {
            for (CoverageByAntenna&coverage : covered_intervals_by_antenna[i]) {
                std::sort(coverage.intervals.begin(), coverage.intervals.end());            
            }

            std::vector<unsigned> greedy_minimal_needed_antenna_set = greedy_antennas_needed(covered_intervals_by_antenna[i], solution.get_tau());

            polygon_coverage_analysis.emplace_back(covered_intervals_by_antenna[i].size(), greedy_minimal_needed_antenna_set.size());

            for (unsigned antenna : greedy_minimal_needed_antenna_set) {
                antenna_needed_how_often[antenna]++;

                double antenna_coverage_of_polygon = 0.0;
                unsigned j;
                for (j = 0; instance.antenna_covers_polygons()[antenna][j].polygon != i; j++);

                for (const Interval &interval : instance.antenna_covers_polygons()[antenna][j].intervals) {
                    antenna_coverage_of_polygon += interval.length();
                }

                coverage_of_selected_antennas.emplace_back(antenna_coverage_of_polygon);
            }
        }

        return {antenna_needed_how_often, polygon_coverage_analysis, coverage_of_selected_antennas};
    }

    void compute_antenna_necessary_statistics(const std::string &filename, const DiscreteCoverageInstance &instance, const Solution &solution) {
        MinAntennaUsageStatistics stats = analyse_solution_for_essential_antennas(instance, solution);
        std::ofstream output_file(filename, std::ios::app);

        output_file << "\n\n--------------------\n\nSolution Analysis Results\n\n--------------------\n\n";
        std::vector<unsigned> antenna_needed_histogramm;
        unsigned antenna_needed_sum = 0;

        for (unsigned antenna : solution.get_solution_antenna_ids()) {
            unsigned usage_number = stats.antenna_appearance_in_min_solution[antenna];

            if (antenna_needed_histogramm.size() <= usage_number) {
                antenna_needed_histogramm.resize(usage_number + 1, 0);
            }

            antenna_needed_histogramm[usage_number]++;
            antenna_needed_sum += usage_number;
        }

        output_file << "Histogramm of antennas needed in (greedy) minimal cover\n";
        for (unsigned int i = 0; i < antenna_needed_histogramm.size(); i++) {
            output_file << i << ": " << antenna_needed_histogramm[i] << "\n";
        }

        output_file << "Average: " << static_cast<double>(antenna_needed_sum) / static_cast<double>(solution.get_k()) << "\n";

        unsigned num_antennas_seen_sum = 0, num_antennas_needed_sum = 0;
        unsigned num_antennas_seen_max = 0, num_antennas_needed_max = 0;
        for (const auto &stat_of_polygon : stats.polygin_min_solution_values) {
            num_antennas_seen_sum += stat_of_polygon.num_antennas_observing_polygon;
            num_antennas_needed_sum += stat_of_polygon.num_antennas_necessary_for_service_upper_bound;
            num_antennas_seen_max = std::max(num_antennas_seen_max, stat_of_polygon.num_antennas_observing_polygon);
            num_antennas_needed_max = std::max(num_antennas_needed_max, stat_of_polygon.num_antennas_necessary_for_service_upper_bound);
        }

        double average_antennas_seen = static_cast<double>(num_antennas_seen_sum) / static_cast<double>(solution.get_number_of_serviced_polygons());
        double average_antennas_needed = static_cast<double>(num_antennas_needed_sum) / static_cast<double>(solution.get_number_of_serviced_polygons());

        output_file << "\nNumber of antennas seeing the average serviced polygon: " << average_antennas_seen << "\n";
        output_file << "Upper bound on average of antennas in the solution needed to cover a polygon: " << average_antennas_needed << "\n";
        output_file << "\nMaximum number of antennas seeing a serviced polygon: " << num_antennas_seen_max<< "\n";
        output_file << "Upper bound on maximum of antennas in the solution needed to cover a polygon: " << num_antennas_needed_max << "\n";

        std::sort(stats.polygon_visibility_in_min_solution.begin(), stats.polygon_visibility_in_min_solution.end());

        output_file << "\nTop 10 lowest antenna sees polygon percentages that appeared in a minimum polygon cover:\n";
        for (unsigned i = 0; i < 10; i++) {
            output_file << i + 1 << ": " << stats.polygon_visibility_in_min_solution[i] << "\n";
        }

        output_file << "\nPercentile blocks as in 10%, 20% etc. of solution is lower than\n";
        unsigned sum_antennas_in_solutions = stats.polygon_visibility_in_min_solution.size() - 1;
        for (unsigned i = 1; i <= 9; i++) {
            output_file << i << "% lower than: " << stats.polygon_visibility_in_min_solution[sum_antennas_in_solutions * i / 100] << "\n";
        }
        for (unsigned i = 1; i <= 10; i++) {
            output_file << i * 10 << "% lower than: " << stats.polygon_visibility_in_min_solution[sum_antennas_in_solutions * i / 10] << "\n";
        }

        std::vector<double> epsilons = {0.005, 0.01, 0.02, 0.05, 0.1};
        std::vector<unsigned> num_polygons_within_epsilon(epsilons.size(), 0);
        std::vector<std::vector<unsigned>> affected_polygons(epsilons.size());

        unsigned j = 1;
        for (const auto &[polygon, coverage] : solution.get_all_polygons_with_coverage()) {
            for (unsigned i = 0; i < epsilons.size(); i++) {
                if (coverage < solution.get_tau() && coverage >= solution.get_tau() - epsilons[i]) {
                    num_polygons_within_epsilon[i]++;
                    affected_polygons[i].emplace_back(j);
                }
            }
            j++;
        }

        output_file << "Number of polygons very close to being serviced:\n";
        for (unsigned i = 0; i < epsilons.size(); i++) {
            output_file << "Number of polygons within " << epsilons[i] << " of being covered: " << num_polygons_within_epsilon[i] << "\n";
            output_file << "Affected polygons: ";
            for (unsigned pid : affected_polygons[i]) {
                output_file << pid << " ";
            }
            output_file << "\n";
        }
    }
}
