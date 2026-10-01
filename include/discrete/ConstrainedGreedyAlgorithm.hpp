#ifndef GISCUPBONN_CONSTRAINEDGREEDYALGORITHM_HPP
#define GISCUPBONN_CONSTRAINEDGREEDYALGORITHM_HPP

#include "geometry/DiscreteCoverageInstance.hpp"
#include "discrete/DiscreteCoverageNeighbourhoodStructure.hpp"
#include "discrete/AbstractIntervalCoverageAlgorithm.hpp"

#include <vector>

class ProduceLocallyConstrainedSolutions{
    private:
        int division_hor = 0;
        int division_vert = 0;
        std::vector<std::vector<unsigned>> solutions;
        std::vector<double> solution_values;
    public:
    ProduceLocallyConstrainedSolutions(int dhor, int dvert):division_hor(dhor),division_vert(dvert){}
    void run(DiscreteCoverageInstance const &instance, const unsigned k, const double tau);
    std::vector<std::vector<unsigned>> get_solutions();
    std::vector<double> get_solution_values();
};


class ConstrainedGreedyAlgorithm {
public:
    ConstrainedGreedyAlgorithm(double interpolation_start = 0.0, double interpolation_end = 1.0) : interpolation_start(interpolation_start), interpolation_end(interpolation_end) {}

    Solution run(DiscreteCoverageInstance const &instance, const unsigned k, const double tau, 
                     const unsigned timelimit_in_ms, 
                     const unsigned seed, const std::vector<unsigned>& candidates,
                     std::vector<unsigned> const &partial_solution_antenna_ids = std::vector<unsigned>()) const;

private:
    double interpolation_start = 0.0, interpolation_end = 1.0;

    class GreedyAlgorithmImpl {
    public:
        GreedyAlgorithmImpl(double interpolation_start = 0.0, double interpolation_end = 1.0) : interpolation_start(interpolation_start), interpolation_delta(interpolation_end - interpolation_start) {}

        Solution run(DiscreteCoverageInstance const &instance, const unsigned k, const double tau, const std::vector<unsigned>& candidates,
                     std::vector<unsigned> const &partial_solution_antenna_ids = std::vector<unsigned>());

    private:
        int num_antennas, num_polygons;

        double progress = 0.0;
        double interpolation_start = 0.0, interpolation_delta = 1.0;

        std::vector<double> weighted_coverage_gain;
        std::vector<unsigned> num_polygons_serviced_gain;
        std::vector<bool> dirty_antenna;

        [[nodiscard]] unsigned find_next_best_antenna(DiscreteCoverageNeighbourhoodStructure &lc_structure, const std::vector<unsigned>& candidates);
    };

};

#endif // GISCUPBONN_GREEDYALGORITHM_HPP
