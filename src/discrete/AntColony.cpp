#include "discrete/AntColony.hpp"

double AntColonyRun::apriori(double coverage,double service,int progress){
    return service*(progress/k)+coverage*(1-progress/k);
}


double AntColonyRun::antenna_probability(DiscreteCoverageNeighbourhoodStructure& ant, unsigned antenna){
    double delta_service = ant.peek_antennas({antenna}).delta_serviced_polygons;
    double delta_coverage = ant.peek_antennas({antenna}).delta_coverage;
    double heur = apriori(delta_coverage,delta_service,ant.num_selected_antennas()+1);
    return std::pow(pherhormones[antenna],pheromone_weight)*std::pow(heur,prior_weight);
}

unsigned AntColonyRun::select_next_antenna(DiscreteCoverageNeighbourhoodStructure& ant){
    std::vector<double> candidates_cumulative;
    std::vector<unsigned> candidates;
    double total_prob = 0;
    for(int a = 0; a < instance.get_number_antennas(); a++){
        if(ant.antenna_selected(a)) continue;
        candidates.push_back(a);
        total_prob += antenna_probability(ant,a);
        candidates_cumulative.push_back(total_prob);
    }
    std::uniform_real_distribution<> sampling(0,total_prob);
    double sample = sampling(rng);
    for(int a = 0; a < candidates.size(); a++){
        if(sample < candidates_cumulative[a]){
            return candidates[a];
        }
    }
    return candidates.back();
}

void AntColonyRun::construct_population(){
    best_current_solution = 0;
    population = std::vector<DiscreteCoverageNeighbourhoodStructure>();
    for(int i = 0; i<population_size;i++){
        //std::cout << "Construct Ant " << i << "\n";
        population.push_back(DiscreteCoverageNeighbourhoodStructure(instance,k,tau));
        while(population[i].num_selected_antennas() < k){
            unsigned next_a = select_next_antenna(population[i]);
            population[i].add_antenna_lazy(next_a);
            population[i].clean_structure();
            
        }
        best_current_solution = std::max(best_current_solution,static_cast<double>(population[i].num_covered_polygons()));
        if(best_current_solution > global_solution_value){
            global_solution_value = best_current_solution;
            best_global_solution = population[i].currentlySelectedAntennas();
        }
    }
}

void AntColonyRun::evaporate(){
    for(int i = 0; i< pherhormones.size();i++){
        pherhormones[i] -= evaporation;
        pherhormones[i] = std::max(0.1,pherhormones[i]);
    }
}

void AntColonyRun::reinforce(){
    for(int i = 0; i<population_size;i++){
        for(unsigned a:population[i].currentlySelectedAntennas()){
            double old_value = pherhormones[a];
            pherhormones[a] += population[i].num_covered_polygons()/(best_current_solution*population_size);
        }
    }
}

Solution AntColonyRun::run(double timelimit_in_ms){
    auto start = std::chrono::system_clock::now();
    while(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - start).count() < timelimit_in_ms){
        /*for(auto ph :pherhormones){
            std::cout << ph << ",";
        }*/
        construct_population();
        evaporate();
        reinforce();
        //std::cout << "\n";
        std::cout << "iteration " << iteration_count << ", best solution: " << global_solution_value << "\n";
        iteration_count++;
    }
    return Solution(instance,k,tau,best_global_solution);
}

Solution AntColony::run(DiscreteCoverageInstance const &instance, const unsigned k, const double tau, 
                         const unsigned timelimit_in_ms, 
                         const unsigned seed,
                         std::vector<unsigned> const &partial_solution_antenna_ids)const{
    AntColonyRun algorun(instance,k,tau,seed);
    algorun.run(timelimit_in_ms);
    return algorun.run(timelimit_in_ms);
}







double AntColonyFastRun::apriori(double coverage,double service,int progress){
    return service*(progress/k)+coverage*(1-progress/k);
}


double AntColonyFastRun::antenna_probability(unsigned antenna){
    return pherhormones[antenna];
    //return std::pow(pherhormones[antenna],pheromone_weight)*std::pow(heuristic[antenna],prior_weight);
}

unsigned AntColonyFastRun::select_next_antenna(int population_id){
    std::vector<double> candidates_cumulative;
    std::vector<unsigned> candidates;
    double total_prob = 0;
    for(int a = 0; a < instance.get_number_antennas(); a++){
        if(std::find(population[population_id].begin(),population[population_id].end(),a) != population[population_id].end()) continue;
        candidates.push_back(a);
        total_prob += antenna_probability(a);
        candidates_cumulative.push_back(total_prob);
    }
    std::uniform_real_distribution<> sampling(0,total_prob);
    double sample = sampling(rng);
    for(int a = 0; a < candidates.size(); a++){
        if(sample < candidates_cumulative[a]){
            return candidates[a];
        }
    }
    return candidates.back();
}

void AntColonyFastRun::construct_population(){
    best_current_solution = 0;
    population = std::vector<std::vector<unsigned>>(population_size,std::vector<unsigned>());
    for(int i = 0; i<population_size;i++){
        //std::cout << "Construct Ant " << i << "\n";
        while(population[i].size() < k){
            unsigned next_a = select_next_antenna(i);
            population[i].push_back(next_a);
        }
        double sol_value = instance.evaluate_antenna_vector(population[i],tau).size();
        population_values[i] = sol_value; 
        best_current_solution = std::max(best_current_solution,sol_value);
        if(best_current_solution > global_solution_value){
            global_solution_value = best_current_solution;
            best_global_solution = population[i];
        }
    }
}

void AntColonyFastRun::evaporate(){
    for(int i = 0; i< pherhormones.size();i++){
        pherhormones[i] -= 0.1*evaporation;
        pherhormones[i] = std::max(0.0,pherhormones[i]);
    }
}

void AntColonyFastRun::reinforce(){
    for(int i = 0; i<population_size;i++){
        for(unsigned a:population[i]){
            double old_value = pherhormones[a];
            pherhormones[a] += 100*std::pow(population_values[i]/best_current_solution,3)/population_size;
            //std::cout << old_value << " " << pherhormones[a] <<  population_values[i]/(best_current_solution*population_size)<<"\n";
        }
    }
}

Solution AntColonyFastRun::run(double timelimit_in_ms){
    auto start = std::chrono::system_clock::now();
    while(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - start).count() < timelimit_in_ms){
        /*for(auto ph :pherhormones){
            if(ph <= 1) continue;
            std::cout << ph << ",";
        }*/
        construct_population();
        evaporate();
        reinforce();
        std::cout << "\n";
        std::cout << "iteration " << iteration_count << ", best solution: " << best_current_solution<< " global best: " <<global_solution_value << "\n";
        iteration_count++;
        /*for(auto s:population_values){
            std::cout << s << ",";
        }
        std::cout << "\n";
        for(auto a:population.back()){
            std::cout << a << ", ";
        }
        std::cout << "\n";*/
    }
    return Solution(instance,k,tau,population.back());
}

Solution AntColonyFast::run(DiscreteCoverageInstance const &instance, const unsigned k, const double tau, 
                         const unsigned timelimit_in_ms, 
                         const unsigned seed,
                         std::vector<unsigned> const &partial_solution_antenna_ids)const{
    AntColonyFastRun algorun(instance,k,tau,seed);
    algorun.run(timelimit_in_ms);
    return algorun.run(timelimit_in_ms);
}
