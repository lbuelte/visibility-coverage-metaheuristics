#ifndef GISCUPBONN_ABSTRACTINTERVALCOVERAGEALGORITHM
#define GISCUPBONN_ABSTRACTINTERVALCOVERAGEALGORITHM

#include "utility/Solution.hpp"
#include <vector>

class IntervalCoverageSolver {
public:
    virtual ~IntervalCoverageSolver();

    IntervalCoverageSolver() = default;

    virtual Solution run(DiscreteCoverageInstance const &instance, const unsigned k, const double tau, 
                         const unsigned timelimit_in_ms, 
                         const unsigned seed,
                         std::vector<unsigned> const &partial_solution_antenna_ids = std::vector<unsigned>()) const = 0;    
};

inline IntervalCoverageSolver::~IntervalCoverageSolver() = default; // This is because pure virtual classes are weird

#endif // GISCUPBONN_ABSTRACTINTERVALCOVERAGEALGORITHM
