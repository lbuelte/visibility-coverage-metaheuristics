//
// Created by philip on 6/10/26.
//

#ifndef GISCUPBONN_NAIVEGREEDYALGORITHM_HPP
#define GISCUPBONN_NAIVEGREEDYALGORITHM_HPP
#include "geometry/DiscreteCoverageInstance.hpp"
#include "utility/Solution.hpp"

class NaiveGreedyAlgorithm {
public:
    NaiveGreedyAlgorithm(const DiscreteCoverageInstance &instance, const unsigned k, const double tau)
    : nr_of_polygons(instance.polygon_covered_by_antennas().size()), instance(instance), k(k),
      tau(tau),
      current_coverage_for_polygons(nr_of_polygons, 0.0),
      current_coverage_intervals_for_polygons(nr_of_polygons) {}


    Solution run(DiscreteCoverageInstance const &instance, const unsigned k, const double tau, 
                 const unsigned timelimit_in_ms, 
                 const unsigned seed,
                 std::vector<unsigned> const &partial_solution_antenna_ids = std::vector<unsigned>());

    [[nodiscard]] std::vector<unsigned> get_polygons_with_coverage_above_tau() const;

    [[nodiscard]] std::vector<unsigned> get_polygons_with_coverage_below_tau() const;

    [[nodiscard]] const std::vector<unsigned> get_solution_antenna_ids() const;

    [[nodiscard]] const std::vector<std::pair<AntennaData,Arrangement>> get_antenna_and_arrangement_of_solution() const;

    [[nodiscard]] const std::vector<std::pair<Poly2, double>> get_covered_polygons_with_coverage() const;

    [[nodiscard]] const std::vector<std::pair<Poly2, double>> get_uncovered_polygons_with_coverage() const;

    [[nodiscard]] unsigned get_number_of_covered_polygons() const;

private:
    unsigned nr_of_polygons;
    const DiscreteCoverageInstance &instance;
    unsigned k;
    double tau;

    std::vector<unsigned> selected_antennas;
    std::vector<double> current_coverage_for_polygons;
    std::vector<std::vector<Interval>> current_coverage_intervals_for_polygons;

    // std::vector<unsigned> antenna_candidates;


    [[nodiscard]] unsigned find_best_antenna() const;

    [[nodiscard]] double compute_antenna_gain(unsigned antenna) const;

    [[nodiscard]] double combined_gain(double numeric_modified_gain, double newly_covered_polygons) const;

    [[nodiscard]] double combined_gain_interpolated(double numeric_gain, double covered_polygon_gain) const;

    [[nodiscard]] double combined_gain_only_numeric(const double numeric_gain, double covered_polygon_gain);

    [[nodiscard]] std::pair<double, int> compute_polygon_gain(const unsigned antenna, const unsigned polygon) const;

    void commit_antenna(const unsigned antenna);

    [[nodiscard]] std::vector<Interval> polygon_interval_after_adding_antenna(unsigned antenna, unsigned polygon) const;

    [[nodiscard]] double modified_numeric_gain(const double old_coverage,const  double new_coverage) const;

    [[nodiscard]] double linear_gain(double old_coverage, double new_coverage) const;

    //sweep algorithm to compute the union
    static std::pair<double, std::vector<Interval>> union_coverage(std::vector<Interval> intervals);
};


#endif //GISCUPBONN_NAIVEGREEDYALGORITHM_HPP
