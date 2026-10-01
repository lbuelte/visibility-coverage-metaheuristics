#include "ConstrainedGreedyAlgorithm.hpp"
#include "DiscreteCoverageInstance.hpp"
#include "DiscreteCoverageNeighbourhoodStructure.hpp"
#include <functional>
#include <vector>
using Coordinate =  Kernel::FT;
Solution ConstrainedGreedyAlgorithm::run(DiscreteCoverageInstance const &instance, const unsigned k, const double tau, 
                     const unsigned timelimit_in_ms, 
                     const unsigned seed,const std::vector<unsigned>& candidates,
                     std::vector<unsigned> const &partial_solution_antenna_ids) const {
    GreedyAlgorithmImpl algo;

    return algo.run(instance, k, tau, candidates, partial_solution_antenna_ids);
}

Solution ConstrainedGreedyAlgorithm::GreedyAlgorithmImpl::run(DiscreteCoverageInstance const &instance, 
                              const unsigned k, const double tau, const std::vector<unsigned>& candidates,
                              std::vector<unsigned> const &partial_solution_antenna_ids) {
    DiscreteCoverageNeighbourhoodStructure neighbourhood_structure(instance, k, tau);

    for (unsigned antenna : partial_solution_antenna_ids) {
        neighbourhood_structure.add_antenna_lazy(antenna);
    }

    num_antennas = instance.antenna_covers_polygons().size();
    num_polygons = instance.polygon_covered_by_antennas().size();

    weighted_coverage_gain.assign(num_antennas, 0.0);
    num_polygons_serviced_gain.assign(num_antennas, 0);
    dirty_antenna.assign(num_antennas, false);

    //std::cout << "Running for " << k << " antennas\n";

    for (unsigned i = partial_solution_antenna_ids.size(); i < k; i++) {
        progress = interpolation_start + interpolation_delta * static_cast<double>(i) / static_cast<double>(k);
        unsigned next_antenna = find_next_best_antenna(neighbourhood_structure,candidates);
        //std::cout << ", adding antenna " << next_antenna << " as the next antenna\n";
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

unsigned ConstrainedGreedyAlgorithm::GreedyAlgorithmImpl::find_next_best_antenna(DiscreteCoverageNeighbourhoodStructure &lc_structure, const std::vector<unsigned>& candidates) {
    unsigned best_antenna = none;
    double best_objective_gain = -1.0;

    lc_structure.clean_structure_for_peek();

    for (unsigned i:candidates){
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

    //std::cout << "Objective gain of best antenna: " << best_objective_gain << " for a new objective of " << lc_structure.objective_value() + best_objective_gain;

    return best_antenna;
}



void ProduceLocallyConstrainedSolutions::run(DiscreteCoverageInstance const &instance, const unsigned k, const double tau){
    Coordinate xmin = instance.get_antenna_data(0).position.x();
    Coordinate xmax= instance.get_antenna_data(0).position.x();
    Coordinate ymin = instance.get_antenna_data(0).position.y();
    Coordinate ymax = instance.get_antenna_data(0).position.y();

    for(unsigned ant = 0; ant < instance.get_number_antennas();ant++){
        xmin = std::min(xmin,instance.get_antenna_data(ant).position.x());
        xmax = std::max(xmax,instance.get_antenna_data(ant).position.x());
        ymin = std::min(ymin,instance.get_antenna_data(ant).position.y());
        ymax = std::max(ymax,instance.get_antenna_data(ant).position.y());
    }
    Coordinate hor_step = (xmax-xmin)/division_vert;
    Coordinate vert_step = (ymax-ymin)/division_hor;
    ConstrainedGreedyAlgorithm greedy;
    for(int i = 0; i <= division_vert;i++){
        for(int j = 0; j <= division_hor;j++){
            std::cout << i  << " " << j << "\n";
            std::cout << division_vert  << " " << division_hor << "\n";
            Coordinate currentxmin = xmin+i*hor_step;
            Coordinate currentxmax = xmin+(i+1.5)*hor_step;
            Coordinate currentymin = ymin+j*vert_step;
            Coordinate currentymax = ymin+(j+1.5)*vert_step;
            std::vector<unsigned> candidates;
            for(unsigned ant = 0; ant < instance.get_number_antennas();ant++){
                Coordinate xpos = instance.get_antenna_data(ant).position.x();
                Coordinate ypos = instance.get_antenna_data(ant).position.y();
                if(xpos < currentxmin) continue;
                if(xpos > currentxmax) continue;
                if(ypos < currentymin) continue;
                if(ypos < currentymax) continue;
                candidates.push_back(ant);
            }
            Solution sol = greedy.run(instance,k,tau,100000,0,candidates);
            if(sol.get_solution_antenna_ids().size() < k) continue;
            solutions.push_back(sol.get_solution_antenna_ids());
            solution_values.push_back(sol.get_number_of_serviced_polygons());
        }
    }
}
std::vector<std::vector<unsigned>> ProduceLocallyConstrainedSolutions::get_solutions(){
    return solutions;
}
std::vector<double> ProduceLocallyConstrainedSolutions::get_solution_values(){
    return solution_values;
}
