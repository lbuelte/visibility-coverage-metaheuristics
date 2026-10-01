#include "AbstractIntervalCoverageAlgorithm.hpp"
#include "DiscreteCoverageInstance.hpp"

#include <CGAL/bounding_box.h>

struct LocalSearchBooster : IntervalCoverageSolver {
    std::unique_ptr<IntervalCoverageSolver> initial_solver;

    LocalSearchBooster(std::unique_ptr<IntervalCoverageSolver> &&initial_solver)
        : initial_solver(std::move(initial_solver)) {}

    Solution run(
        DiscreteCoverageInstance const &instance,
        const unsigned k,
        const double tau, 
        const unsigned timelimit_in_ms, 
        const unsigned seed,
        std::vector<unsigned> const &partial_solution_antenna_ids
    ) const override;
};
