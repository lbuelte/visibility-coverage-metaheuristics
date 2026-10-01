#ifndef GIDCUPBONN_SOLUTION_ANALYSIS
#define GIDCUPBONN_SOLUTION_ANALYSIS

#include "utility/Solution.hpp"
#include "geometry/DiscreteCoverageInstance.hpp"

namespace gis_utility {

    void compute_antenna_necessary_statistics(const std::string &filename, const DiscreteCoverageInstance &instance, const Solution &solution);

}
#endif
