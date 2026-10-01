//
// Created by Laura on 7/7/26.
//

#ifndef GISCUPBONN_CLUSTEREXCHANGENEIGHBORSELECTOR_HPP
#define GISCUPBONN_CLUSTEREXCHANGENEIGHBORSELECTOR_HPP

#include "discrete/AbstractNeighborSelector.hpp"
#include "utility/k_means.hpp"
#include "utility/SimulatedAnnealingConfig.hpp"
#include "discrete/GreedyAlgorithm.hpp"



class ClusterExchangeNeighborSelector final : public NeighborSelector {
public:
    ClusterExchangeNeighborSelector(DCNS2 &_dcn_structure,
                                    std::vector<unsigned> const &_antenna_candidates,
                                    SearchObjective _objective,
                                    DiscreteCoverageInstance const &_instance,
                                    unsigned const _k,
                                    double const _tau,
                                    unsigned _seed,
                                    unsigned _num_clusters,
                                    const MaxNumAntennasToRemove _max_num_antennas_to_remove = {1},
                                    const BufferSizeTopR _top_r = {1},
                                    const NumSamplesForAdding _number_of_sample_neighbors = {1})
        : NeighborSelector(_dcn_structure, _antenna_candidates, std::move(_objective))
        , instance(_instance)
        , k(_k)
        , tau(_tau)
        , seed(_seed)
        , num_clusters(_num_clusters)
        , antenna_ids_in_clusters(_num_clusters)
        , rng(_seed)
        , max_num_antennas_to_remove(_max_num_antennas_to_remove)
        , top_r(_top_r)
        , number_of_sample_neighbors(_number_of_sample_neighbors)
    {
        if (num_clusters <= 0){
            throw std::runtime_error("ERROR: In CLusterExchangeNeighborSelector(): num_clusters must be > 0");
        }
        compute_and_set_clusters(_instance, _seed);
    }

    void set_removal_mode(RemovalMode const &_mode){
        removal_mode = _mode;
    }
    void set_adding_mode(AddingMode const &_mode){
        adding_mode = _mode;
    }
    [[nodiscard]] RemovalMode get_removal_mode() const {
        return removal_mode;
    }
    [[nodiscard]] AddingMode get_adding_mode() const {
        return adding_mode;
    }

private:
    [[nodiscard]] std::vector<unsigned> select_antennas_to_be_removed() override;
    [[nodiscard]] std::vector<std::vector<unsigned>> select_antenna_sets_to_be_added_from_candidates() override;
    [[nodiscard]] Neighbor select_neighbor(std::vector<std::vector<unsigned>> const &candidate_antenna_sets_for_adding) override;

    const unsigned num_clusters;

    std::vector<unsigned> cluster_id_by_reduced_antenna_id;
    std::vector<std::vector<unsigned>> antenna_ids_in_clusters;

    RemovalMode removal_mode;
    AddingMode adding_mode;
    std::mt19937 rng;

    // This parameter is just used as a guideline for removing small clusters
    // It is not guaranteed as a hard constraint on the number of removed antennas
    MaxNumAntennasToRemove max_num_antennas_to_remove;

    // These are used in the case of AddingMode::RandomTopKSampling
    BufferSizeTopR top_r;    
    NumSamplesForAdding number_of_sample_neighbors;

    unsigned num_removed_antennas = 0;

    void compute_and_set_clusters(DiscreteCoverageInstance const &instance, unsigned seed);
    std::vector<unsigned> compute_one_sample_of_added_antennas();

    const unsigned k;
    const double tau;
    const DiscreteCoverageInstance &instance;
    const unsigned seed;
};


inline std::vector<unsigned> ClusterExchangeNeighborSelector::select_antennas_to_be_removed()
{   
    if (dcn_structure.numSelected() < k){
        std::cerr << "WARNING: In ClusterExchangeNeighborSelector::select_antennas_to_be_removed() : "
                  << "Less than k antennas are selected by the data structure before removing." 
                  << std::endl;
        if (dcn_structure.numSelected() == 0){
            std::cerr << "Error: In ClusterExchangeNeighborSelector::select_antennas_to_be_removed() : "
                      << "Trying to remove antennas from a data structure with no selected antennas." 
                      << std::endl;
            return std::vector<unsigned>();
        }
    }
    assert (dcn_structure.numSelected() == k);

    switch (removal_mode)
    {
        case RemovalMode::ClusterOfRandomAntennaID : {            
            std::uniform_int_distribution<unsigned> random_antenna_id(0, dcn_structure.numSelected() - 1);
            unsigned cluster_to_remove = cluster_id_by_reduced_antenna_id[random_antenna_id(rng)];
            num_removed_antennas = antenna_ids_in_clusters[cluster_to_remove].size();
            return antenna_ids_in_clusters[cluster_to_remove];
        }
        case RemovalMode::RandomCluster : {
            std::uniform_int_distribution<unsigned> random_cluster_id(0, num_clusters - 1);
            auto cluster_to_remove = random_cluster_id(rng);
            num_removed_antennas = antenna_ids_in_clusters[cluster_to_remove].size();
            return antenna_ids_in_clusters[cluster_to_remove];
        }
        case RemovalMode::LargestCluster : {
            unsigned largest_cluster_id = 0;
            unsigned largest_cluster_size = 0;
            for (int id = 0; id < num_clusters; ++id){
                if (antenna_ids_in_clusters[id].size() > largest_cluster_size){
                    largest_cluster_id = id;
                    largest_cluster_size = antenna_ids_in_clusters[id].size();
                }
            }
            num_removed_antennas = largest_cluster_size;
            return antenna_ids_in_clusters[largest_cluster_id];
        }
        case RemovalMode::SmallestClusters : {
            std::vector<unsigned> cluster_ids_sorted_by_size_increasing(num_clusters);
            std::iota(cluster_ids_sorted_by_size_increasing.begin(), 
                    cluster_ids_sorted_by_size_increasing.end(), 
                    0);
            std::sort(cluster_ids_sorted_by_size_increasing.begin(), 
                    cluster_ids_sorted_by_size_increasing.end(), 
                    [&](unsigned a, unsigned b){
                        assert(a < antenna_ids_in_clusters.size() and b < antenna_ids_in_clusters.size());
                        return antenna_ids_in_clusters[a].size() < antenna_ids_in_clusters[b].size();
                    });
            std::vector<unsigned> antennas_to_remove;

            // Always remove the smallest cluster
            auto cluster_id = cluster_ids_sorted_by_size_increasing.front();
            num_removed_antennas += antenna_ids_in_clusters[cluster_id].size();
            antennas_to_remove.insert(antennas_to_remove.end(),
                                      antenna_ids_in_clusters[cluster_id].begin(),
                                      antenna_ids_in_clusters[cluster_id].end());

            // Remove next smallest clusters as long as the total number of 
            // removed antennas does not exceed max_num_antennas_to_remove
            for (unsigned i = 1; i < cluster_ids_sorted_by_size_increasing.size(); ++i){
                cluster_id = cluster_ids_sorted_by_size_increasing[i];
                if (num_removed_antennas + antenna_ids_in_clusters[cluster_id].size() > max_num_antennas_to_remove.value){
                    break;
                }
                num_removed_antennas += antenna_ids_in_clusters[cluster_id].size();
                antennas_to_remove.insert(antennas_to_remove.end(),
                                        antenna_ids_in_clusters[cluster_id].begin(),
                                        antenna_ids_in_clusters[cluster_id].end());
            }
            num_removed_antennas = antennas_to_remove.size();
            return antennas_to_remove;
        }
        default: {
            throw std::runtime_error("Exception in ClusterExchangeNeighborSelector: Unknown RemovalMode in switch.");
            break;
        }
    }
}


inline std::vector<std::vector<unsigned>> 
ClusterExchangeNeighborSelector::select_antenna_sets_to_be_added_from_candidates()
{
    switch (adding_mode)
    {
        case AddingMode::Greedy : {
            GreedyAlgorithm greedy;
            Solution sol = greedy.run(instance, k, tau, 300000, seed, dcn_structure.getSelectedAntennas());

            std::vector<unsigned> antenna_ids;
            for (auto const antenna_id : sol.get_solution_antenna_ids()){
                if (not dcn_structure.has(antenna_id)){
                    antenna_ids.push_back(antenna_id);
                }
            }
            return {antenna_ids};
        }
        case AddingMode::RandomTopKSampling : {
            std::vector<std::vector<unsigned>> neighbors;
            neighbors.reserve(number_of_sample_neighbors.value);

            for (unsigned i = 0; i < number_of_sample_neighbors.value; ++i) {
                std::vector<unsigned> added = compute_one_sample_of_added_antennas();
                neighbors.push_back(added);
            }
            return neighbors;
        }
        case AddingMode::BestSampling : {
            std::vector<std::vector<unsigned>> neighbors;
            neighbors.reserve(number_of_sample_neighbors.value);

            for (unsigned i = 0; i < number_of_sample_neighbors.value; ++i) {
                std::vector<unsigned> added = compute_one_sample_of_added_antennas();
                neighbors.push_back(added);
            }
            return neighbors;
        }
        case AddingMode::Random : {
            return {compute_one_sample_of_added_antennas()};
        }
        default : {
            throw std::runtime_error("Exception in ClusterExchangeNeighborSelector: Unknown AddingMode in switch.");
            break;
        }
    }
}


inline Neighbor ClusterExchangeNeighborSelector::select_neighbor(
    std::vector<std::vector<unsigned>> const &candidate_antenna_sets_for_adding)
{
    if (candidate_antenna_sets_for_adding.empty()){
        std::cerr << "ERROR: In ClusterExchangeNeighborSelector::select_neighbor() : "
                  << "Trying to select neighbor from empty set of candidate vectors." 
                  << std::endl;
    }
    assert(not candidate_antenna_sets_for_adding.empty());

    switch (adding_mode)
    {
        case AddingMode::Greedy : {
            return get_neighbor_from_antennas_to_be_added(candidate_antenna_sets_for_adding.front());
        }
        case AddingMode::RandomTopKSampling : {
            std::vector<Neighbor> neighbors;
            neighbors.reserve(candidate_antenna_sets_for_adding.size());
            for (auto const &candidate : candidate_antenna_sets_for_adding) {
                neighbors.push_back(get_neighbor_from_antennas_to_be_added(candidate));
            }

            std::sort(neighbors.begin(), neighbors.end(), [](const Neighbor &a, const Neighbor &b) {
                return a.result.objective_value > b.result.objective_value;
            });

            const unsigned r = std::min<unsigned>(top_r.value, neighbors.size());
            std::uniform_int_distribution<unsigned> dist(0, r - 1);

            return neighbors[dist(rng)];
        }
        case AddingMode::BestSampling : {
            std::vector<Neighbor> neighbors;
            neighbors.reserve(candidate_antenna_sets_for_adding.size());
            for (auto const &candidate : candidate_antenna_sets_for_adding) {
                neighbors.push_back(get_neighbor_from_antennas_to_be_added(candidate));
            }
            return *std::max_element(neighbors.begin(), neighbors.end(), [](const Neighbor &a, const Neighbor &b) {
                return a.result.objective_value < b.result.objective_value;
            });
        }
        case AddingMode::Random : {
            return get_neighbor_from_antennas_to_be_added(candidate_antenna_sets_for_adding.front());
        }
        default : {
            throw std::runtime_error("Exception in ClusterExchangeNeighborSelector: Unknown AddingMode in switch.");
            break;
        }
    }
}


inline void ClusterExchangeNeighborSelector::compute_and_set_clusters(DiscreteCoverageInstance const &instance, unsigned seed)
{
    std::vector<std::pair<double, double>> current_antenna_positions;
    std::map<unsigned, unsigned> reduced_id;

    unsigned next_id = 0;
    for (unsigned antenna_id : dcn_structure.getSelectedAntennas()) {
        current_antenna_positions.emplace_back(
                CGAL::to_double(instance.get_antenna_data(antenna_id).position.x()), 
                CGAL::to_double(instance.get_antenna_data(antenna_id).position.y()));
        reduced_id[antenna_id] = next_id;
        next_id++;
    }

    KMeansClustering cluster_algo(current_antenna_positions, num_clusters, seed);
    cluster_algo.run();

    cluster_id_by_reduced_antenna_id = cluster_algo.clusters_by_ids();
    for (unsigned antenna_id : dcn_structure.getSelectedAntennas()) {
        unsigned reduced_antenna_id = reduced_id[antenna_id];
        antenna_ids_in_clusters[cluster_id_by_reduced_antenna_id[reduced_antenna_id]].push_back(antenna_id);
    }
}

inline std::vector<unsigned> ClusterExchangeNeighborSelector::compute_one_sample_of_added_antennas()
{
    std::unordered_set<unsigned> added;
    added.reserve(num_removed_antennas);

    std::uniform_int_distribution<unsigned> dist(0, antenna_candidates.size() - 1);

    while (added.size() < num_removed_antennas) {
        unsigned candidate = antenna_candidates[dist(rng)];
        if (!dcn_structure.has(candidate) && !added.contains(candidate)) {
            added.insert(candidate);
        }
    }
    return std::vector(added.begin(), added.end());
}

#endif // GISCUPBONN_CLUSTEREXCHANGENEIGHBORSELECTOR_HPP