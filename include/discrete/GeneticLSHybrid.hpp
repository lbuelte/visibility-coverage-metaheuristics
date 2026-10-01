#ifndef GISCUPBONN_GENETICHYBRID_HPP
#define GISCUPBONN_GENETICHYBRID_HPP

#include "discrete/GeneticAlgorithm.hpp"
#include "discrete/LocalSearch.hpp"


class GeneticLSHybrid: public IntervalCoverageSolver{
public:
    GeneticLSHybrid(){}
    Solution run(DiscreteCoverageInstance const &instance, const unsigned k, const double tau, 
                         const unsigned timelimit_in_ms, 
                         const unsigned seed,
                         std::vector<unsigned> const &partial_solution_antenna_ids = std::vector<unsigned>())const override;
};
class GeneticLSHybridRun {
public:
    GeneticLSHybridRun(const DiscreteCoverageInstance &instance,
         const int k, const double tau,unsigned seed = 0):instance(instance),k(k),tau(tau),seed(seed){}
    Solution run(double timelimit_in_ms);
    [[nodiscard]] const std::vector<int> get_solution_antenna_ids() const;
    [[nodiscard]] int get_number_of_covered_polygons() const;

private:
    const DiscreteCoverageInstance &instance;
    const int k;
    const double tau;
    const double seed;
    int iteration_count = 0;
    //Hyperparameters
    const double mutation_probability = 0.02;
    const int survivor_count = 200;
    const int elite_count = 10;
    const int mating_cycles = 10;
    const int reset_frequency = 100;
    double p_crossover = 0.9;
    int tournament_size = 2;
    std::mt19937 rng;
    //Stores for each solution in the population the corresponding fitness
    std::vector<std::pair<double,std::vector<unsigned>>> population;
    std::vector<unsigned> current_best_solution;
    double current_best_fitness;
    void iteration();
};

#endif

