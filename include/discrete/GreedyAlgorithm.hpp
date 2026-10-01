#ifndef GISCUPBONN_GREEDYALGORITHM_HPP
#define GISCUPBONN_GREEDYALGORITHM_HPP

#include "geometry/DiscreteCoverageInstance.hpp"
#include "discrete/DiscreteCoverageNeighbourhoodStructure.hpp"
#include "discrete/AbstractIntervalCoverageAlgorithm.hpp"

#include <vector>

class GreedyAlgorithm : public IntervalCoverageSolver {
public:
    //! This constructor always uses lexicographic interpolation
    GreedyAlgorithm() {
        interpolation_start = 1.0;
        interpolation_end = 1.0;
    }
    //! This constructor always uses the fixed interpolation and reduces it to lexicographic, if it was larger
    GreedyAlgorithm(double interpolation_fixed) : interpolation_start(interpolation_fixed), interpolation_end(interpolation_fixed) {}
    //! This constructor linearely moves from interpolation_start to interpolation_end and reduces the end to lexicographic, if it is larger
    GreedyAlgorithm(double interpolation_start, double interpolation_end) : interpolation_start(interpolation_start), interpolation_end(interpolation_end) {}

    Solution run(DiscreteCoverageInstance const &instance, const unsigned k, const double tau, 
                     const unsigned timelimit_in_ms, 
                     const unsigned seed,
                     std::vector<unsigned> const &partial_solution_antenna_ids = std::vector<unsigned>()) const override;

private:
    double interpolation_start = 0.0, interpolation_end = 1.0;

    class GreedyAlgorithmImpl {
    public:
        GreedyAlgorithmImpl(double interpolation_start = 0.0, double interpolation_end = 1.0) : interpolation_start(interpolation_start), interpolation_delta(interpolation_end - interpolation_start) {}

        Solution run(DiscreteCoverageInstance const &instance, const unsigned k, const double tau, 
                     std::vector<unsigned> const &partial_solution_antenna_ids = std::vector<unsigned>());

    private:
        int num_antennas, num_polygons;

        double progress = 0.0;
        double interpolation_start = 0.0, interpolation_delta = 1.0;

        std::vector<double> weighted_coverage_gain;
        std::vector<unsigned> num_polygons_serviced_gain;
        std::vector<bool> dirty_antenna;

        [[nodiscard]] unsigned find_next_best_antenna(DiscreteCoverageNeighbourhoodStructure &lc_structure);
    };

};

#endif // GISCUPBONN_GREEDYALGORITHM_HPP
