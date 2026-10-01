#include "GreedyAlgorithm.hpp"
#include "DiscreteCoverageInstance.hpp"
#include "DiscreteCoverageNeighbourhoodStructure.hpp"
#include <functional>
#include <vector>

Solution GreedyAlgorithm::run(DiscreteCoverageInstance const &instance, const unsigned k, const double tau, 
                     const unsigned timelimit_in_ms, 
                     const unsigned seed,
                     std::vector<unsigned> const &partial_solution_antenna_ids) const {
    GreedyAlgorithmImpl algo(interpolation_start, interpolation_end);

    return algo.run(instance, k, tau, partial_solution_antenna_ids);
}

Solution GreedyAlgorithm::GreedyAlgorithmImpl::run(DiscreteCoverageInstance const &instance, 
                              const unsigned k, const double tau, 
                              std::vector<unsigned> const &partial_solution_antenna_ids) {
    double lexicographic_interpolation = 1.0 - 1.0 / static_cast<double>(instance.get_number_polygons() + 1);
    if (interpolation_start + interpolation_delta > lexicographic_interpolation) {
        interpolation_delta = lexicographic_interpolation - interpolation_start;
    }
    
    DiscreteCoverageNeighbourhoodStructure neighbourhood_structure(instance, k, tau, instance.get_relevant_antennas());

    for (unsigned antenna : partial_solution_antenna_ids) {
        neighbourhood_structure.add_antenna_lazy(antenna);
    }

    num_antennas = instance.antenna_covers_polygons().size();
    num_polygons = instance.polygon_covered_by_antennas().size();

    weighted_coverage_gain.assign(num_antennas, 0.0);
    num_polygons_serviced_gain.assign(num_antennas, 0);
    dirty_antenna.assign(num_antennas, true);

    std::cout << "Running for " << k - partial_solution_antenna_ids.size() << " antennas\n";

    for (unsigned i = partial_solution_antenna_ids.size(); i < k; i++) {
        progress = interpolation_start + interpolation_delta * static_cast<double>(i) / static_cast<double>(k);
        unsigned next_antenna = find_next_best_antenna(neighbourhood_structure);
        // std::cout << ", adding antenna " << next_antenna << " as the next antenna\n";
        if (next_antenna == none)
            break;

        neighbourhood_structure.add_antenna_lazy(next_antenna);
        neighbourhood_structure.clean_structure_for_peek();
        neighbourhood_structure.set_new_global_objectve_function([this](double coverage, unsigned num_polygons_covered){return (1.0 - progress) * coverage + progress * static_cast<double>(num_polygons_covered);});

        for (const auto &polygon : instance.antenna_covers_polygons()[next_antenna]) {
            for (const auto &antenna : instance.polygon_covered_by_antennas()[polygon.polygon]) {
                dirty_antenna[antenna.antenna] = true;
            }
        }
    }

    return Solution(instance, k, tau, neighbourhood_structure.currentlySelectedAntennas());
}

unsigned GreedyAlgorithm::GreedyAlgorithmImpl::find_next_best_antenna(DiscreteCoverageNeighbourhoodStructure &lc_structure) {
    unsigned best_antenna = none;
    double best_objective_gain = -1.0;

    lc_structure.clean_structure_for_peek();

    for (auto i: lc_structure.get_antenna_candidates()){
        if (!lc_structure.antenna_selected(i)) {
            assert(i >= 0);
            assert(i < num_antennas);
            if (dirty_antenna[i]) {
                auto peek_result = lc_structure.peek_antennas({static_cast<unsigned>(i)});
                weighted_coverage_gain[i] = peek_result.delta_weighted_coverage;
                num_polygons_serviced_gain[i] = peek_result.delta_serviced_polygons;
                dirty_antenna[i] = false;
            }

            double peek_objective_gain = (1.0 - progress) * weighted_coverage_gain[i] + progress * static_cast<double>(num_polygons_serviced_gain[i]);
            
            if (peek_objective_gain > best_objective_gain) {
                best_antenna = i;
                best_objective_gain = peek_objective_gain;
            }
        }
    }

    return best_antenna;
}
