#include "discrete/GeneticLSHybrid.hpp"

Solution GeneticLSHybridRun::run(double timelimit_in_ms){
    std::cout << "STARTING RUN \n";
    std::vector<unsigned> best;
    double best_val = 0;
    const int phase_time = 5E4;
    const int ls_time = 5E3;
    GeneticAlgorithmRun genetic(instance,k,tau);
    genetic.run(phase_time);
    ImproveWithOneOpt ls(10);
    std::vector<std::vector<unsigned>> population;
    int indiv_count = 0;
    for(const auto& indiv: genetic.return_population()){
        if(indiv_count >= 20)break;
        std::cout << indiv_count++ << " " << genetic.return_population().size() << "\n";
        auto sol = ls.run(instance,k,tau,ls_time,0,indiv.second);
        population.push_back(sol.get_solution_antenna_ids());
        if(sol.get_number_of_serviced_polygons() > best_val){
            best_val = sol.get_number_of_serviced_polygons();
            best = sol.get_solution_antenna_ids();
        }
    }
    auto start = std::chrono::system_clock::now();
    while(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - start).count() < timelimit_in_ms){
        GeneticAlgorithmRun genetic(instance,k,tau,population);
        genetic.run(phase_time);
        population = {};
        std::vector<std::pair<double,double>> improved_vals;
        ImproveWithOneOpt ls(10);
        indiv_count = 0;
        for(const auto& [val,indiv]: genetic.return_population()){
            if(indiv_count >= 20)break;
            std::cout << indiv_count++ << " " << genetic.return_population().size() << "\n";
            auto sol = ls.run(instance,k,tau,ls_time,0,indiv);
            population.push_back(sol.get_solution_antenna_ids());
            improved_vals.push_back({val,sol.get_number_of_serviced_polygons()});
        }
        for(auto [prev,after]:improved_vals){
            std::cout << prev << " -> " << after << "\n";
        }
    }
    return Solution(instance,k,tau,best);
}

Solution GeneticLSHybrid::run(DiscreteCoverageInstance const &instance, const unsigned k, const double tau, 
                         const unsigned timelimit_in_ms, 
                         const unsigned seed,
                         std::vector<unsigned> const &partial_solution_antenna_ids)const{
        
    GeneticLSHybridRun algo(instance,k,tau);
    return algo.run(timelimit_in_ms);

}
