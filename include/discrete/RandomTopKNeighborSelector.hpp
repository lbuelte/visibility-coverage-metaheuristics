//
// Created by philip on 6/25/26.
//

#ifndef GISCUPBONN_RANDOMTOPKNEIGHBORSELECTOR_HPP
#define GISCUPBONN_RANDOMTOPKNEIGHBORSELECTOR_HPP

#include "discrete/AbstractNeighborSelector.hpp"
#include "discrete/DCNS2.hpp"
#include "utility/SimulatedAnnealingConfig.hpp"

#include <vector>
#include <random>



// Removes a random set of antennas, then considers the top-k "best" candidate
// antennas (by some scoring/heuristic exposed by DCNS2)
// as sets to add back, evaluating each via peek and picking the best resulting neighbor.
class RandomTopKNeighborSelector final : public NeighborSelector {
public:
    RandomTopKNeighborSelector(DCNS2 &_dcn_structure,
                               std::vector<unsigned> const &_antenna_candidates,
                               SearchObjective _objective,
                               const unsigned seed,
                               const MaxNumAntennasToRemove _max_num_antennas_to_remove = {1},
                               const BufferSizeTopR _top_r = {1},
                               const NumSamplesForAdding _number_of_sample_neighbors = {1})
        : NeighborSelector(_dcn_structure, _antenna_candidates, std::move(_objective))
        , top_r(_top_r)
        , max_num_antennas_to_remove(_max_num_antennas_to_remove)
        , number_of_sample_neighbors(_number_of_sample_neighbors)
        , rng(seed) 
    {
        if (max_num_antennas_to_remove.value >= dcn_structure.numSelected()){
            max_num_antennas_to_remove.value = dcn_structure.numSelected() -1;
        }
        if (not check_validity_of_parameters()){
            throw std::runtime_error("Exception in RandomTopKNeighborSelector: Parameters are not valid!");
        }
    };

    void set_new_hyperparameters(const MaxNumAntennasToRemove _max_num_antennas_to_remove, 
                                 const BufferSizeTopR _top_r, 
                                 const NumSamplesForAdding _number_of_sample_neighbors) {
        top_r = _top_r;
        max_num_antennas_to_remove = _max_num_antennas_to_remove;
        number_of_sample_neighbors = _number_of_sample_neighbors;
    }

private:
    [[nodiscard]] std::vector<unsigned> select_antennas_to_be_removed() override;
    [[nodiscard]] std::vector<std::vector<unsigned>> select_antenna_sets_to_be_added_from_candidates() override;
    [[nodiscard]] Neighbor select_neighbor(std::vector<std::vector<unsigned>> const &candidate_antenna_sets_for_adding) override;

    [[nodiscard]] std::vector<unsigned> compute_one_sample_of_added_antennas();

    bool check_validity_of_parameters() const;

    unsigned num_removed_antennas = 0;

    BufferSizeTopR top_r;
    MaxNumAntennasToRemove max_num_antennas_to_remove;
    NumSamplesForAdding number_of_sample_neighbors;
    std::mt19937 rng;
};


inline std::vector<unsigned> RandomTopKNeighborSelector::select_antennas_to_be_removed() 
{
    std::vector<unsigned> current_antennas = dcn_structure.getSelectedAntennas();

    std::uniform_int_distribution<unsigned> count_dist(1, max_num_antennas_to_remove.value);
    num_removed_antennas = std::min<unsigned>(count_dist(rng),current_antennas.size());

    std::vector<unsigned> selected;
    selected.reserve(num_removed_antennas);
    std::sample(current_antennas.begin(), current_antennas.end(),
                std::back_inserter(selected),
                num_removed_antennas,
                rng);

    return selected;
}


inline std::vector<std::vector<unsigned>> RandomTopKNeighborSelector::
select_antenna_sets_to_be_added_from_candidates() 
{
    std::vector<std::vector<unsigned>> neighbors;
    neighbors.reserve(number_of_sample_neighbors.value);

    for (unsigned i = 0; i < number_of_sample_neighbors.value; ++i) {
        std::vector<unsigned> added = compute_one_sample_of_added_antennas();
        neighbors.push_back(added);
    }

    return neighbors;
}


inline Neighbor RandomTopKNeighborSelector::select_neighbor(
    std::vector<std::vector<unsigned>> const &candidate_antenna_sets_for_adding) 
{
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


inline std::vector<unsigned> RandomTopKNeighborSelector::compute_one_sample_of_added_antennas() 
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


inline bool RandomTopKNeighborSelector::check_validity_of_parameters() const
{
    if (max_num_antennas_to_remove.value > dcn_structure.getSelectedAntennas().size()){
        std::cout << "Error in RandomTopKNeighborSelector: num_antennas_to_remove > k." << std::endl;
        return false;
    }
    if (top_r.value > number_of_sample_neighbors.value){
        std::cout << "Error in RandomNeighborSelector: top_r > num_samples." << std::endl;
        return false;
    }
    return true;
}

#endif // GISCUPBONN_RANDOMTOPKNEIGHBORSELECTOR_HPP
