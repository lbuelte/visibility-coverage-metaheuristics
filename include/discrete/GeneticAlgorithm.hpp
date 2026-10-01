//Created by Daniel 23.06

#ifndef GISCUPBONN_GENETICALGORITHM_HPP
#define GISCUPBONN_GENETICALGORITHM_HPP

#include "geometry/DiscreteCoverageInstance.hpp"
#include "utility/Solution.hpp"
#include "discrete/AbstractIntervalCoverageAlgorithm.hpp"
#include <vector>
#include <random>
#include "discrete/DiscreteCoverageNeighbourhoodStructure.hpp"

using Coordinate =  Kernel::FT;
struct Hyperplane{
    Point2 normal_vector;
    Coordinate offset; 
    Hyperplane(Point2 n,Coordinate o):normal_vector(n),offset(o){}
    Hyperplane():normal_vector(0,0),offset(0){}
};
int plane_side(const Point2& p, const Hyperplane& h);
Hyperplane hyperplane_through_points(const Point2& a, const Point2& b);
class GeneticAlgorithm: public IntervalCoverageSolver{
private:
public:
    GeneticAlgorithm();
    Solution run(DiscreteCoverageInstance const &instance, const unsigned k, const double tau, 
                         const unsigned timelimit_in_ms, 
                         const unsigned seed,
                         std::vector<unsigned> const &partial_solution_antenna_ids = std::vector<unsigned>())const override;

};

class GeneticAlgorithmRun {
public:
    GeneticAlgorithmRun(const DiscreteCoverageInstance &instance,
         const unsigned k, const double tau,const std::vector<std::vector<unsigned>>& init_population, unsigned seed = 0);
    //Constructor without init population. the default case generates pop_count random subsets of size k
    GeneticAlgorithmRun(const DiscreteCoverageInstance &instance,
         const int k, const double tau,unsigned seed = 0);
    Solution run(double timelimit_in_ms);
    [[nodiscard]] const std::vector<int> get_solution_antenna_ids() const;
    [[nodiscard]] int get_number_of_covered_polygons() const;
    std::vector<std::pair<double,std::vector<unsigned>>> return_population(){
        return population;
    }

private:
    const DiscreteCoverageInstance &instance;
    const int k;
    const double tau;
    const double seed;
    int iteration_count = 0;
    //Hyperparameters
    const double mutation_probability = 0.05;
    const int survivor_count = 200;
    const int elite_count = 5;
    const int mating_cycles = 10;
    const int reset_frequency = 100;
    double p_crossover = 0.9;
    int tournament_size = 2;
    std::mt19937 rng;
    //Stores for each solution in the population the corresponding fitness
    std::vector<std::pair<double,std::vector<unsigned>>> population;
    std::vector<unsigned> current_best_solution;
    double current_best_fitness;
    double fitness(const std::vector<unsigned>& child);
    void iteration();
    void generation_change();
    std::vector<std::vector<unsigned>> select_parents(int p_count);
    std::vector<std::vector<unsigned>> select_parents_tournament(int p_count);
    std::vector<std::vector<unsigned>> select_parents_diverse(int p_count);
    void repair(std::vector<std::vector<unsigned>>& children, const std::vector<std::vector<unsigned>>& parents);
    void greedy_repair(std::vector<std::vector<unsigned>>& children, const std::vector<std::vector<unsigned>>& parents);
    void mutate(std::vector<std::vector<unsigned>>& children);
    void mutate(std::vector<std::vector<unsigned>>& children,double p_mut);
    void greedy_mutate(std::vector<DiscreteCoverageNeighbourhoodStructure>& children);
    void add_children(std::vector<std::vector<unsigned>>& children);
    void survivor_selection_best(int survivors);
    void survivor_selection_elitist_wis(int elite, int rand_survivors);
    std::vector<std::pair<double,std::vector<unsigned>>> select_elite_probabilistic(int elite);
    std::vector<std::pair<double,std::vector<unsigned>>> select_elite(int elite);
    void survivor_selection_weighted_indep_sampling(int survivors);
    unsigned pps_sampling(const std::vector<double>& probabilities);
    void gene_boosting();
    void boost_children(std::vector<std::vector<unsigned>>& children);
    //Get Hyperplanes to define inherited regions
    /*Perform crossover between subset of population
    */
    [[nodiscard]] std::vector<std::vector<unsigned>> crossover(const std::vector<std::vector<unsigned>>& parents);
    [[nodiscard]] std::vector<std::vector<unsigned>> greedy_crossover(const std::vector<std::vector<unsigned>>& parents, double greedy_rate);
    [[nodiscard]] std::vector<Hyperplane> quad_median_section(
    const std::vector<unsigned>& a,
    const std::vector<unsigned>& b, 
    const std::vector<unsigned>& c, 
    const std::vector<unsigned>& d);
    [[nodiscard]] std::vector<Hyperplane> quad_randombbox_section(
    const std::vector<unsigned>& a,
    const std::vector<unsigned>& b, 
    const std::vector<unsigned>& c, 
    const std::vector<unsigned>& d);
    [[nodiscard]] std::vector<Hyperplane> randombbox_hyperplanes(const std::vector<std::vector<unsigned>>& children, int num_v, int num_h);
    [[nodiscard]] std::vector<std::vector<unsigned>> four_crossover(
    const std::vector<unsigned>& a,
    const std::vector<unsigned>& b, 
    const std::vector<unsigned>& c, 
    const std::vector<unsigned>& d);
    [[nodiscard]] std::vector<std::vector<unsigned>> nine_crossover(const std::vector<std::vector<unsigned>>& parents);
    [[nodiscard]] std::vector<std::vector<unsigned>> two_crossover(
    const std::vector<unsigned>& a,
    const std::vector<unsigned>& b);
    [[nodiscard]] std::vector<std::vector<unsigned>> random_crossover(const std::vector<std::vector<unsigned>>& parents);

    [[nodiscard]] std::vector<std::vector<unsigned>> four_crossover_max_antenna(
    const std::vector<unsigned>& a,
    const std::vector<unsigned>& b, 
    const std::vector<unsigned>& c, 
    const std::vector<unsigned>& d);
    std::pair<double,std::vector<unsigned>> cheap_local_search(const std::vector<unsigned>& solution,int iter_count);
};



#endif
