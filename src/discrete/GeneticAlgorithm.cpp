//Created by Daniel 23.06
#include "discrete/GeneticAlgorithm.hpp"
#include <random>
#include <chrono>
#include "discrete/DiscreteCoverageNeighbourhoodStructure.hpp"
#include <algorithm>
#include "discrete/ConstrainedGreedyAlgorithm.hpp"
#include "discrete/LocalSearch.hpp"

int plane_side(const Point2& p, const Hyperplane& h){
    return h.normal_vector.x()*p.x()+h.normal_vector.y()*p.y() <= h.offset;
}
void remove_duplicates(std::vector<unsigned>& list){
    std::sort(list.begin(),list.end());
    for(int i = list.size()-1; i >= 1;i--){
        if(list[i] == list[i-1]){
            list[i] = list.back();
            list.pop_back();
        }
    }
}
Hyperplane hyperplane_through_points(const Point2& a, const Point2& b){
    Point2 normal = Point2(-b.y()+a.y(),b.x()-a.x());
    Coordinate support = b.x()*a.y()-a.x()*b.y();
    return Hyperplane(normal,support);
}

double jaccard_coefficient(std::vector<unsigned> a, std::vector<unsigned> b){
    if(a.size() == 0  && b.size()==0) return 1;
    std::vector<unsigned> setunion,setintersect;
    std::sort(a.begin(),a.end());
    std::sort(b.begin(),b.end());
    std::set_union(a.begin(),a.end(),b.begin(),b.end(),std::back_inserter(setunion));
    std::set_intersection(a.begin(),a.end(),b.begin(),b.end(),std::back_inserter(setintersect));
    return static_cast<double>(setintersect.size())/static_cast<double>(setunion.size());
}

bool check_duplicates(std::vector<unsigned> individual){
    std::sort(individual.begin(),individual.end());
    for(int i = 0; i< individual.size()-1;i++){
        if(individual[i] == individual[i+1]) return false;
    }
    return true;
}

GeneticAlgorithmRun::GeneticAlgorithmRun(const DiscreteCoverageInstance &instance,
         const unsigned k, const double tau,const std::vector<std::vector<unsigned>>& init_population, unsigned seed):
         instance(instance),k(k),tau(tau),population(),seed(seed),current_best_fitness(0),rng(seed){ 
        for(const auto& sol:init_population){
            population.push_back({fitness(sol),sol});
            if(fitness(sol) > current_best_fitness){
                current_best_fitness = fitness(sol);
                current_best_solution = sol;
            }
        }
    }
    //Constructor without init population. the default case generates pop_count random subsets of size k
    GeneticAlgorithmRun::GeneticAlgorithmRun(const DiscreteCoverageInstance &instance,
         const int k, const double tau,unsigned seed):
         instance(instance),k(k),tau(tau),population(),seed(seed){
        std::vector<unsigned> shuffled_indices(instance.get_number_antennas());
        std::iota(shuffled_indices.begin(),shuffled_indices.end(),0);
        for(unsigned i = 0; i< survivor_count;i++){
            std::shuffle(shuffled_indices.begin(),shuffled_indices.end(),rng);
            std::vector<unsigned> rand_sol(shuffled_indices.begin(),shuffled_indices.begin()+k);
            double eval = fitness(rand_sol);
            population.push_back({eval,rand_sol});
            if(eval > current_best_fitness){
                current_best_fitness = eval;
                current_best_solution = rand_sol;
            }
        }
    }

//Currently the only implemented crossover is splitting into four quadrants and recombining
std::vector<std::vector<unsigned>> GeneticAlgorithmRun::crossover(const std::vector<std::vector<unsigned>>& parents){
    //return random_crossover(parents);
    assert(parents.size() == 9);
    //return nine_crossover(parents);
    return nine_crossover(parents);
}

std::vector<std::vector<unsigned>>GeneticAlgorithmRun::four_crossover(
    const std::vector<unsigned>& a,
    const std::vector<unsigned>& b, 
    const std::vector<unsigned>& c, 
    const std::vector<unsigned>& d){
    auto quad_hyperplanes = quad_randombbox_section(a,b,c,d);
    auto vertical = quad_hyperplanes[0];
    auto horizontal = quad_hyperplanes[1];
    std::vector<std::vector<unsigned>> children(4,std::vector<unsigned>());
    int sol_offset = 0;
    /*
        Quadrants have indices like:
        0|1
        ---
        2|3
    */
    auto get_quadrant_idx =[vertical,horizontal](Point2 p){
        return plane_side(p,vertical)+2*plane_side(p,horizontal);
    };
    for(const auto& sol:{a,b,c,d}){
        for(unsigned ant:sol){
            int base_idx = (get_quadrant_idx(instance.get_antenna_data(ant).position)+sol_offset)%4;
            children[base_idx].push_back(ant);
        }
        sol_offset ++;
    }
    //std::cout << "Parent values " << fitness(a) << " " << fitness(b) << " " << fitness(c) << " " << fitness(d) << "\n";
    //std::cout << "Children values " << fitness(children[0]) << " " << fitness(children[1]) << " " << fitness(children[2]) << " " << fitness(children[3]) << "\n";
    return children;
}

std::vector<std::vector<unsigned>>GeneticAlgorithmRun::two_crossover(
    const std::vector<unsigned>& a,
    const std::vector<unsigned>& b){
    std::uniform_real_distribution<> vert_or_hor(0,1);
    Hyperplane hyperplane;
    if(vert_or_hor(rng) < 0.5){
        hyperplane = randombbox_hyperplanes({a,b},1,0)[0];
    }
    else{
        hyperplane = randombbox_hyperplanes({a,b},0,1)[0];
    }
    std::vector<std::vector<unsigned>> children(2,std::vector<unsigned>());
    int sol_offset = 0;
    /*
        Halves have indices:
        0|1
    */
    for(const auto& sol:{a,b}){
        for(unsigned ant:sol){
            int base_idx = (plane_side(instance.get_antenna_data(ant).position,hyperplane)+sol_offset)%2;
            children[base_idx].push_back(ant);
        }
        sol_offset ++;
    }
    return children;
}

std::vector<std::vector<unsigned>>GeneticAlgorithmRun::four_crossover_max_antenna(
    const std::vector<unsigned>& a,
    const std::vector<unsigned>& b, 
    const std::vector<unsigned>& c, 
    const std::vector<unsigned>& d){
    auto quad_hyperplanes = quad_randombbox_section(a,b,c,d);
    auto vertical = quad_hyperplanes[0];
    auto horizontal = quad_hyperplanes[1];
    std::vector<std::vector<unsigned>> children(4,std::vector<unsigned>());
    int sol_offset = 0;
    /*
        Quadrants have indices like:
        0|1
        ---
        2|3
    */
    auto get_quadrant_idx =[vertical,horizontal](Point2 p){
        return plane_side(p,vertical)+2*plane_side(p,horizontal);
    };
    for(const auto& sol:{a,b,c,d}){
        for(unsigned ant:sol){
            //std::cout << "Antenna Position " << instance.get_antenna_data(ant).position.x() << " " << instance.get_antenna_data(ant).position.y() << "\n"; 
            //std::cout << "Hyperplane Vertical " << vertical.normal_vector << " " << vertical.offset << "\n"; 
            //std::cout << ant << " QIdx: " << plane_side(instance.get_antenna_data(ant).position,vertical) << " " << plane_side(instance.get_antenna_data(ant).position,horizontal)<<"\n";
            int base_idx = (get_quadrant_idx(instance.get_antenna_data(ant).position)+sol_offset)%4;
            children[base_idx].push_back(ant);
        }
        sol_offset ++;
    }
    int max_size = 0;
    int max_idx = 0;
    for(int i = 0; i< 4;i++){
        if(children[i].size() > max_size){
            max_size = children[i].size();
            max_idx = i;
        }
    }
    assert(children[max_idx].size() >= k);
    return {children[max_idx]};
}

std::vector<std::vector<unsigned>>GeneticAlgorithmRun::random_crossover(const std::vector<std::vector<unsigned>>& parents){
    std::vector<unsigned> shuffled_antennas;
    for(const auto& p:parents){
        shuffled_antennas.insert(shuffled_antennas.begin(),p.begin(),p.end());
    }
    std::shuffle(shuffled_antennas.begin(),shuffled_antennas.end(),rng);
    shuffled_antennas.resize(k);
    return {shuffled_antennas};
}

unsigned GeneticAlgorithmRun::pps_sampling(const std::vector<double>& probabilities){
    if(probabilities.size() < 2) return 0;
    std::vector<double> cumsum(probabilities.size()+1);
    cumsum[0] = 0;
    for(int i = 1; i< probabilities.size();i++){
        cumsum[i] = cumsum[i-1]+probabilities[i-1];
    }
    //Binary search
    std::uniform_real_distribution<double> rnd_double(0,cumsum.back());
    double selected = rnd_double(rng);
    int lb = 0;
    int ub = cumsum.size();
    unsigned pivot = static_cast<unsigned>(cumsum.size()/2);
    while(!(selected < cumsum[pivot] && selected >= cumsum[pivot-1])){
        if(selected < cumsum[pivot-1]){
            ub = pivot-1;
        }
        else{
            lb = pivot;
        }
        pivot = static_cast<int>(lb+(ub-lb)/2);
    }
    return pivot;
}
std::vector<Hyperplane> GeneticAlgorithmRun::quad_median_section(
    const std::vector<unsigned>& a,
    const std::vector<unsigned>& b, 
    const std::vector<unsigned>& c, 
    const std::vector<unsigned>& d){
    std::vector<Coordinate> xcoords;
    std::vector<Coordinate> ycoords;
    for(const auto& sol:{a,b,c,d}){
        for(int a:sol){
            xcoords.push_back(instance.get_antenna_data(a).position.x());
            ycoords.push_back(instance.get_antenna_data(a).position.y());
        }
    }
    std::sort(xcoords.begin(),xcoords.end());
    std::sort(ycoords.begin(),ycoords.end());
    Coordinate xmedian = xcoords[static_cast<int>(xcoords.size()/2)];
    Coordinate ymedian = ycoords[static_cast<int>(ycoords.size()/2)];
    Hyperplane vertical(Point2(1,0),xmedian);
    Hyperplane horizontal(Point2(0,1),ymedian);
    return {vertical,horizontal};
}

std::vector<Hyperplane> GeneticAlgorithmRun::quad_randombbox_section(
    const std::vector<unsigned>& a,
    const std::vector<unsigned>& b, 
    const std::vector<unsigned>& c, 
    const std::vector<unsigned>& d){
    Coordinate xmin = instance.get_antenna_data(a[0]).position.x();
    Coordinate xmax = instance.get_antenna_data(a[0]).position.x();
    Coordinate ymin = instance.get_antenna_data(a[0]).position.y();
    Coordinate ymax = instance.get_antenna_data(a[0]).position.y();
    for(const auto& sol:{a,b,c,d}){
        for(int ant:sol){
            xmin = std::min(xmin,instance.get_antenna_data(ant).position.x());
            xmax = std::max(xmax,instance.get_antenna_data(ant).position.x());
            ymin = std::min(ymin,instance.get_antenna_data(ant).position.y());
            ymax = std::max(ymax,instance.get_antenna_data(ant).position.y());
        }
    }
    auto vertical_distribution = std::uniform_real_distribution<>(CGAL::to_double(xmin),CGAL::to_double(xmax));
    auto horizontal_distribution = std::uniform_real_distribution<>(CGAL::to_double(ymin),CGAL::to_double(ymax));
    Point2 upper = Point2(Coordinate(vertical_distribution(rng)),ymax);
    Point2 lower = Point2(Coordinate(vertical_distribution(rng)),ymin);
    Point2 left = Point2(xmin,Coordinate(horizontal_distribution(rng)));
    Point2 right = Point2(xmax,Coordinate(horizontal_distribution(rng)));
    return {hyperplane_through_points(left,right),hyperplane_through_points(lower,upper)};
}

std::vector<Hyperplane> GeneticAlgorithmRun::randombbox_hyperplanes(const std::vector<std::vector<unsigned>>& children, int num_v, int num_h){
    Coordinate xmin = instance.get_antenna_data(children[0][0]).position.x();
    Coordinate xmax = instance.get_antenna_data(children[0][0]).position.x();
    Coordinate ymin = instance.get_antenna_data(children[0][0]).position.y();
    Coordinate ymax = instance.get_antenna_data(children[0][0]).position.y();
    for(const auto& sol:children){
        for(int ant:sol){
            xmin = std::min(xmin,instance.get_antenna_data(ant).position.x());
            xmax = std::max(xmax,instance.get_antenna_data(ant).position.x());
            ymin = std::min(ymin,instance.get_antenna_data(ant).position.y());
            ymax = std::max(ymax,instance.get_antenna_data(ant).position.y());
        }
    }
    auto vertical_distribution = std::uniform_real_distribution<>(CGAL::to_double(xmin),CGAL::to_double(xmax));
    auto horizontal_distribution = std::uniform_real_distribution<>(CGAL::to_double(ymin),CGAL::to_double(ymax));
    std::vector<Hyperplane> hyperplanes;
    for(int i = 0; i< num_v;i++){
        Point2 upper = Point2(Coordinate(vertical_distribution(rng)),ymax);
        Point2 lower = Point2(Coordinate(vertical_distribution(rng)),ymin);
        hyperplanes.push_back(hyperplane_through_points(lower,upper));
    }
    for(int i = 0; i< num_h;i++){
        Point2 left = Point2(xmin,Coordinate(horizontal_distribution(rng)));
        Point2 right = Point2(xmax,Coordinate(horizontal_distribution(rng)));
        hyperplanes.push_back(hyperplane_through_points(left,right));
    }
    return hyperplanes;
}


std::vector<std::vector<unsigned>> GeneticAlgorithmRun::select_parents(int p_count){
    std::vector<unsigned> shuffled_indices(population.size());
    std::iota(shuffled_indices.begin(),shuffled_indices.end(),0);
    std::vector<std::vector<unsigned>> parents;
    std::shuffle(shuffled_indices.begin(),shuffled_indices.end(),rng);
    for(int i = 0; i< p_count;i++){
        parents.push_back(population[shuffled_indices[i]].second);
    }
    return parents;
    
}



std::vector<std::vector<unsigned>> GeneticAlgorithmRun::select_parents_tournament(int p_count){
    std::vector<unsigned> shuffled_indices(population.size());
    std::iota(shuffled_indices.begin(),shuffled_indices.end(),0);
    std::vector<std::vector<unsigned>> parents;
    for(int i = 0; i< p_count;i++){
        std::shuffle(shuffled_indices.begin(),shuffled_indices.end(),rng);
        std::vector<unsigned> winner;
        double winner_fitness = -1;
        for(int t = 0; t < tournament_size;t++){
            if(population[shuffled_indices[t]].first > winner_fitness){
                winner_fitness = population[shuffled_indices[t]].first;
                winner = population[shuffled_indices[t]].second ;
            }
        }
        parents.push_back(winner);
    }
    return parents;
    
}

std::vector<std::vector<unsigned>> GeneticAlgorithmRun::select_parents_diverse(int p_count){
    std::vector<std::vector<unsigned>> parents;
    std::uniform_int_distribution<> rand_idx(0,population.size()-1);
    while(parents.size() < p_count){
        int i = rand_idx(rng);
        int j = rand_idx(rng);
        if(i == j || jaccard_coefficient(population[i].second,population[j].second) > 0.9) continue;
        parents.push_back(population[i].second);
        parents.push_back(population[j].second);
    }
    return parents;
    
}
/*
void GeneticAlgorithmRun::iteration(){
    for(int i = 0; i < mating_cycles;i++){
        auto parents = select_parents();
        auto children = crossover(parents);
        repair(children,parents);
        mutate(children);
        //After each iteration, best solution must be maintained
        add_children(children);
    }
    survivor_selection_elitist_wis(elite_count,survivor_count-elite_count);
}
*/
void GeneticAlgorithmRun::iteration(){
    auto elite_population = select_elite(elite_count);
    std::uniform_real_distribution<> perform_crossover(0,1);
    std::vector<std::vector<unsigned>> all_children;
    while(all_children.size() < survivor_count-elite_count){
        //std::cout << all_children.size() << "\n";
        auto parents = select_parents_tournament(9);
        std::vector<std::vector<unsigned>> children;
        if(perform_crossover(rng) <= p_crossover){
            children = crossover(parents);
            repair(children,parents);
        }
        else{
            children = parents;
        }
        
        mutate(children);
        //After each iteration, best solution must be maintained
        all_children.insert(all_children.end(),children.begin(),children.end());
    }
    
    population = std::move(elite_population);
    //boost_children(all_children);
    add_children(all_children);
}

void GeneticAlgorithmRun::generation_change(){
    std::vector<std::vector<unsigned>> all_children;
    for(int i = 0; i < mating_cycles;i++){
        auto parents = select_parents_diverse(9);
        auto children = crossover(parents);
        repair(children,parents);
        mutate(children,0.1);
        //After each iteration, best solution must be maintained
        all_children.insert(all_children.end(),children.begin(),children.end());
    }
    population = std::vector<std::pair<double,std::vector<unsigned>>>();
    add_children(all_children);
}

void GeneticAlgorithmRun::repair(std::vector<std::vector<unsigned>>& children, const std::vector<std::vector<unsigned>>& parents){
    for(auto& child:children){
        std::shuffle(child.begin(),child.end(),rng);
        if(child.size() >= k){
            child.resize(k);
            continue;
        }
        //Fill children up with remaining parent antennas
        //This is currently very inefficient
        int increment = 0;
        while(child.size() < k){
            for(const auto& parent:parents){
                if(parent.size() <= increment) continue;
                if(std::find(child.begin(),child.end(),parent[increment]) != child.end()) continue;
                child.push_back(parent[increment]);
                if(child.size() == k) break;
            }
            increment++;
        }
    }
}

void GeneticAlgorithmRun::greedy_repair(std::vector<std::vector<unsigned>>& children, const std::vector<std::vector<unsigned>>& parents){
    for(auto& child:children){
        if(child.size() == k)continue;
        DiscreteCoverageNeighbourhoodStructure cover_structure(instance,k,tau,child);
        if(child.size() > k){
            while(cover_structure.num_selected_antennas() > k){
                for(auto a: cover_structure.currentlySelectedAntennas()){
                    
                }
            }
        }
        //Fill children up with remaining parent antennas
        //This is currently very inefficient
        int increment = 0;
        while(child.size() < k){
            for(const auto& parent:parents){
                if(parent.size() <= increment) continue;
                if(std::find(child.begin(),child.end(),parent[increment]) != child.end()) continue;
                child.push_back(parent[increment]);
                if(child.size() == k) break;
            }
            increment++;
        }
    }
}

std::vector<std::vector<unsigned>> GeneticAlgorithmRun::greedy_crossover(const std::vector<std::vector<unsigned>>& parents, double greedy_rate){
    DiscreteCoverageNeighbourhoodStructure cover_structure(instance,k,tau);
    std::vector<unsigned> all_candidates;
    for(const auto& p:parents){
        all_candidates.insert(all_candidates.end(),p.begin(),p.end());
    }
    remove_duplicates(all_candidates);
    while(cover_structure.num_selected_antennas() < k){
        std::uniform_real_distribution<> greedy_step(0,1);
        unsigned best_candidate;
        int best_can_idx = 0;
        if(greedy_step(rng) <= greedy_rate){
            std::pair<int,double> best_delta = {0,0};
            int can_idx = 0;
            for(auto c: all_candidates){
                if(cover_structure.antenna_selected(c)) continue;
                auto peek = cover_structure.peek_antennas({c});
                std::pair<int,double> delta = {peek.delta_serviced_polygons,peek.delta_coverage};
                if(delta > best_delta){
                    best_delta = delta;
                    best_candidate = c;
                    best_can_idx = can_idx;
                }
                can_idx++;
            }
        }
        else{
            std::uniform_int_distribution<> rand_a(0,all_candidates.size()-1);
            best_can_idx = rand_a(rng);
            best_candidate = all_candidates[best_can_idx];
        }
        cover_structure.add_antenna_lazy(best_candidate);
        cover_structure.clean_structure();
        all_candidates[best_can_idx] = all_candidates.back();
        all_candidates.pop_back();
        
    }
    return {cover_structure.currentlySelectedAntennas()};
}

void GeneticAlgorithmRun::mutate(std::vector<std::vector<unsigned>>& children,double p_mut){
    std::uniform_real_distribution<> mutate_antenna(0,1);
    std::uniform_int_distribution<std::mt19937::result_type> new_rand_ant(0,instance.get_number_antennas()-1);
    for(auto& child:children){
        assert(child.size() == k);
        for(int i = 0; i< k;i++){
            if(mutate_antenna(rng) > p_mut)continue;
            //Switch one random antenna for another
            child[i] = child[k-1];
            child.pop_back();

            int new_antenna = new_rand_ant(rng);
            while(std::find(child.begin(),child.end(),new_antenna) != child.end()){
                new_antenna = new_rand_ant(rng);
            }
            child.push_back(new_antenna);
            assert(new_antenna < instance.get_number_antennas());
        }
    }
}
void GeneticAlgorithmRun::mutate(std::vector<std::vector<unsigned>>& children){
    mutate(children,mutation_probability);
}

void GeneticAlgorithmRun::add_children(std::vector<std::vector<unsigned>>& children){
    //Best solution must be maintained
    for(const auto& child:children){
        double cfitness = fitness(child);
        if(cfitness > current_best_fitness){
            current_best_fitness = cfitness;
            current_best_solution = child;
        }
        population.push_back({cfitness,child});
    }
}
void GeneticAlgorithmRun::survivor_selection_best(int survivors){
    std::sort(population.begin(),population.end(),[](const auto& a, const auto& b){
        return a.first > b.first;
    });
    population.resize(survivors);
}
std::vector<std::pair<double,std::vector<unsigned>>> GeneticAlgorithmRun::select_elite(int elite){
    std::sort(population.begin(),population.end(),[](const auto& a, const auto& b){
        return a.first > b.first;
    });
    return std::vector<std::pair<double,std::vector<unsigned>>> (population.begin(),population.begin()+elite);
}
std::vector<std::pair<double,std::vector<unsigned>>> GeneticAlgorithmRun::select_elite_probabilistic(int elite){
    double total_weight = 0;
    for(const auto& pop:population){
        total_weight += pop.first;
    }
    std::uniform_real_distribution<> choose_antenna(0,1);
    std::vector<std::pair<double,std::vector<unsigned>>> new_pop;
    for(const auto& pop:population){
        double probability = elite*pop.first/total_weight;
        if(choose_antenna(rng) < probability){
            new_pop.push_back(pop);
        }
    }
    return new_pop;
}
void GeneticAlgorithmRun::survivor_selection_weighted_indep_sampling(int survivors){
    double total_weight = 0;
    for(const auto& pop:population){
        total_weight += pop.first;
    }
    std::uniform_real_distribution<> choose_antenna(0,1);
    std::vector<std::pair<double,std::vector<unsigned>>> new_pop;
    for(const auto& pop:population){
        double probability = survivors*pop.first/total_weight;
        if(choose_antenna(rng) < probability){
            new_pop.push_back(pop);
        }
    }
    population = std::move(new_pop);
}
void GeneticAlgorithmRun::survivor_selection_elitist_wis(int elite, int rand_survivors){
    std::sort(population.begin(),population.end(),[](const auto& a, const auto& b){
        return a.first > b.first;
    });
    double total_weight = 0;
    for(const auto& pop:population){
        total_weight += pop.first;
    }
    std::uniform_real_distribution<> choose_antenna(0,1);
    for(int i = elite; i < population.size();i++){
        double probability = rand_survivors*population[i].first/total_weight;
        if(choose_antenna(rng) > probability){
            //Delete antenna
            population[i] = population.back();
            population.pop_back();
        }
    }
}

/*void GeneticAlgorithmRun::gene_boosting(){
    std::shuffle(population.begin(),population.end(),rng);
    for(int i = 0; i < 10;i++){
        NaiveLocalSearch localsearch(1,0,1);
        for(int l = 0; l< 5;l++){
            std::cout << population[i].second[l] << " ";
        }
        std::cout << "Before fitness " << population[i].first << "\n";
        std::cout << "\n";
        auto sol = localsearch.run(instance,k,tau,10000,seed,population[i].second);
        population[i] = {fitness(sol.get_solution_antenna_ids()),sol.get_solution_antenna_ids()};
        std::cout << "After fitness " << population[i].first << "\n";
    }
}*/


void GeneticAlgorithmRun::gene_boosting(){
    std::shuffle(population.begin(),population.end(),rng);
    for(int i = 0; i < 10;i++){
        std::cout << "Before fitness " << population[i].first << "\n";
        std::cout << "\n";
        auto sol = cheap_local_search(population[i].second,5);
        population[i] = sol;
        std::cout << "After fitness " << population[i].first << "\n";
    }
}

void GeneticAlgorithmRun::boost_children(std::vector<std::vector<unsigned>>& individuals){
    std::uniform_real_distribution<> p_boost(0,1);
    for(int i = 0; i < individuals.size();i++){
        double rand_val = p_boost(rng);
        if(rand_val > 0.005) continue;
        auto sol = cheap_local_search(individuals[i],2);
        individuals[i] = sol.second;
        //std::cout << "Boosted solution: " <<i << " "<< sol.first << "\n"; 
    }
}

double GeneticAlgorithmRun::fitness(const std::vector<unsigned>& child){
    return instance.evaluate_antenna_vector(child,tau).size();
}
std::pair<double,std::vector<unsigned>> GeneticAlgorithmRun::cheap_local_search(const std::vector<unsigned>& solution,int iter_count){
    DiscreteCoverageNeighbourhoodStructure data_structure(instance,k,tau);
    data_structure.add_antennas_lazy(solution);
    data_structure.clean_structure();
    
    for(int i = 0; i< iter_count ; i++){
        unsigned worst_antenna;
        int worst_value = 1E6;
        for(unsigned ant:data_structure.currentlySelectedAntennas()){
            int removal_value = data_structure.polygonsDependentOnAntenna(ant);
            if(removal_value < worst_value){
                worst_antenna = ant;
                worst_value = removal_value;
            }
        }
        data_structure.remove_antenna_lazy(worst_antenna);
        data_structure.clean_structure();
        unsigned best_antenna;
        int best_value = 0;
        for(unsigned a = 0; a < instance.get_number_antennas(); a++){
            int add_value = data_structure.peek_antennas({a}).absolute_num_serviced_polygons;
            if(add_value > best_value){
                best_antenna = a;
                best_value = add_value;
            }
        }
        data_structure.add_antenna_lazy(best_antenna);
        data_structure.clean_structure();
    }
    return {data_structure.num_covered_polygons(),data_structure.currentlySelectedAntennas()};
}
Solution GeneticAlgorithmRun::run(double timelimit_in_ms){
    auto start = std::chrono::system_clock::now();
    while(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - start).count() < timelimit_in_ms){
        if(iteration_count % 1 == 0){
            double iteration_value = current_best_fitness;
            std::cout << "Current best value: " << iteration_value << "\n";
            double total_fitness = 0;
            double min_fitness = population[0].first;
            double max_fitness = population[0].first;
            for(const auto& pop:population){
                total_fitness += pop.first;
                min_fitness = std::min(min_fitness,pop.first);
                max_fitness = std::max(max_fitness,pop.first);
            }
            total_fitness /= population.size();
            double variance = 0;
            for(const auto& pop:population){
                variance += std::pow(pop.first-total_fitness,2);
            }
            variance/= population.size();
            std::cout << "Population average: " << total_fitness<< " Variance: "<<variance<< " Min: "<<min_fitness << " Max: " << max_fitness<< "\n";
            std::cout << "Elapsed time: " << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - start).count()<< "/" << timelimit_in_ms << "\n";
        }
        iteration();
        /*if(iteration_count % 200 == 0){
            std::cout << "Perform Gene Boosting \n";
            gene_boosting();
        }*/
        /*if(iteration_count % 200 == 0){
            std::cout << "Generation Change \n";
            generation_change();
        }*/
        iteration_count++;
        if(iteration_count % 50 != 0)continue;
        //Check Jaccard coefficients of best solutions
        std::sort(population.begin(),population.end(),[](const auto& a, const auto& b){
            return a.first > b.first;
        });
        for(int i = 0; i< 10;i++){
            for(int j = 0; j< i;j++){
                std::cout <<"(" <<population[i].first << "," <<population[j].first<< ","<<jaccard_coefficient(population[i].second,population[j].second) << ") ";
            }
            std::cout << "\n";
        }
    }
    std::cout << "Iteration count: " << iteration_count << "\n";
    return Solution(instance,k,tau,current_best_solution);
}
GeneticAlgorithm::GeneticAlgorithm(){}
Solution GeneticAlgorithm::run(DiscreteCoverageInstance const &instance, const unsigned k, const double tau, 
                         const unsigned timelimit_in_ms, 
                         const unsigned seed,
                         std::vector<unsigned> const &partial_solution_antenna_ids)const{
    GeneticAlgorithmRun algorun(instance,k,tau,seed);
    algorun.run(timelimit_in_ms);
    return algorun.run(timelimit_in_ms);
}

 std::vector<std::vector<unsigned>> GeneticAlgorithmRun::nine_crossover(const std::vector<std::vector<unsigned>>& parents){
    std::vector<Hyperplane> hyperplanes_vertical = randombbox_hyperplanes(parents,3,0);
    std::vector<Hyperplane> hyperplanes_horizontal = randombbox_hyperplanes(parents,0,3);
    /*
        Sections have indices like:
        0|1|2
        -----
        3|4|5
        -----
        6|7|8
    */
    auto get_section_idx =[hyperplanes_vertical,hyperplanes_horizontal](Point2 p){
        int horizontal_index = 0;
        int vertical_index = 0;
        for(auto vert:hyperplanes_vertical){
            vertical_index += plane_side(p,vert);
        }
        for(auto hor:hyperplanes_horizontal){
            horizontal_index += plane_side(p,hor);
        }
        return horizontal_index+3*vertical_index;
    };

    std::vector<std::vector<unsigned>> children(9,std::vector<unsigned>());
    int sol_offset = 0;
    for(const auto& sol:parents){
        for(unsigned ant:sol){
            int base_idx = (get_section_idx(instance.get_antenna_data(ant).position)+sol_offset)%9;
            children[base_idx].push_back(ant);
        }
        sol_offset ++;
    }
    //Remove children that are too small
    /*std::cout << "prefilter \n";
    for(int i = 0; i< 9;i++){
        if(children[i].size() < k){
            children[i] = children.back();
            children.pop_back();
        }
    }
    std::cout << "postfilter \n";*/
    return children;

}
