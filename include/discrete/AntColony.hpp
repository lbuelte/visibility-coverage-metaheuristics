
#ifndef GISCUPBONN_ANTCOLONYALGORITHM_HPP
#define GISCUPBONN_ANTCOLONYALGORITHM_HPP

#include "geometry/DiscreteCoverageInstance.hpp"
#include "utility/Solution.hpp"
#include "discrete/AbstractIntervalCoverageAlgorithm.hpp"
#include <vector>
#include <random>
#include "discrete/DiscreteCoverageNeighbourhoodStructure.hpp"

class AntColony: public IntervalCoverageSolver{
public:
    AntColony(){}
    Solution run(DiscreteCoverageInstance const &instance, const unsigned k, const double tau, 
                         const unsigned timelimit_in_ms, 
                         const unsigned seed,
                         std::vector<unsigned> const &partial_solution_antenna_ids = std::vector<unsigned>())const override;
};

class AntColonyRun{
public:
    AntColonyRun(const DiscreteCoverageInstance &instance,
         const int k, const double tau,unsigned seed = 0):instance(instance),k(k),tau(tau),seed(seed),pherhormones(instance.get_number_antennas(),0.1){}
    Solution run(double timelimit_in_ms);
    [[nodiscard]] const std::vector<unsigned> get_solution_antenna_ids() const;
    [[nodiscard]] int get_number_of_covered_polygons() const;

private:
    const DiscreteCoverageInstance &instance;
    const int k;
    const double tau;
    const double seed;
    double best_current_solution = 0;
    double global_solution_value;
    std::vector<unsigned> best_global_solution;
    int iteration_count = 0;
    std::vector<DiscreteCoverageNeighbourhoodStructure> population;
    std::vector<double> pherhormones;
    double apriori(double coverage,double service,int progress);
    double antenna_probability(DiscreteCoverageNeighbourhoodStructure& ant, unsigned antenna);
    unsigned select_next_antenna(DiscreteCoverageNeighbourhoodStructure& ant);
    void construct_population();
    void evaporate();
    void reinforce();
    std::mt19937 rng;
    //Hyperparameters
    const int population_size = 20;
    const double evaporation = 0.1;
    const double prior_weight = 1;
    const double pheromone_weight = 1;
};


class AntColonyFast: public IntervalCoverageSolver{
public:
    AntColonyFast(){}
    Solution run(DiscreteCoverageInstance const &instance, const unsigned k, const double tau, 
                         const unsigned timelimit_in_ms, 
                         const unsigned seed,
                         std::vector<unsigned> const &partial_solution_antenna_ids = std::vector<unsigned>())const override;
};

class AntColonyFastRun{
public:
    AntColonyFastRun(const DiscreteCoverageInstance &instance,
        const int k, const double tau,unsigned seed = 0):instance(instance),k(k),tau(tau),seed(seed),pherhormones(instance.get_number_antennas(),0.1),
        heuristic(instance.get_number_antennas(),0){
            DiscreteCoverageNeighbourhoodStructure cover_structure(instance,k,tau);
            for(unsigned a = 0; a < instance.get_number_antennas();a++){
                heuristic[a] = cover_structure.peek_antennas({a}).absolute_coverage;
            }
            population_values = std::vector<double>(population_size);
        }
    Solution run(double timelimit_in_ms);

private:
    const DiscreteCoverageInstance &instance;
    const int k;
    const double tau;
    const double seed;
    double best_current_solution = 0;
    double global_solution_value;
    std::vector<unsigned> best_global_solution;
    int iteration_count = 0;
    std::vector<std::vector<unsigned>> population;
    std::vector<double> population_values;
    std::vector<double> pherhormones;
    std::vector<double> heuristic;
    double apriori(double coverage,double service,int progress);
    double antenna_probability(unsigned antenna);
    unsigned select_next_antenna(int population_id);
    void construct_population();
    void evaporate();
    void reinforce();
    std::mt19937 rng;
    //Hyperparameters
    const int population_size = 100;
    const double evaporation = 0.1;
    const double prior_weight = 1;
    const double pheromone_weight = 1.0;
};

#endif
