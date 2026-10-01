//
// Created by Laura on 6/24/26.
//

#ifndef GISCUPBONN_ABSTRACTNEIGHBORSELECTOR_HPP
#define GISCUPBONN_ABSTRACTNEIGHBORSELECTOR_HPP

#include "discrete/DCNS2.hpp"

#include <functional>
#include <stdexcept>


// The objective the search is optimising, as a function of (weighted coverage, number of
// serviced polygons). Owned by the caller rather than by the coverage structure, so that
// the structure only has to report coverage.
using SearchObjective = std::function<double(double, unsigned)>;


struct Neighbor {

    struct ObjectiveResult {

        ObjectiveResult() = default;

        // `absolute` is the coverage the solution would have, i.e. the current coverage
        // plus what the peek reports. DCNS2's Coverage::total is already clamped per
        // building, so it is the weighted coverage the objective expects.
        ObjectiveResult(Coverage const absolute, SearchObjective const &objective)
            : objective_value(objective(absolute.total, (unsigned) absolute.serviced))
            , num_serviced_polygons((unsigned) absolute.serviced)
            , total_weighted_coverage(absolute.total)
        {}

        double objective_value = 0;
        unsigned num_serviced_polygons = 0;
        double total_weighted_coverage = 0;
    };

    Neighbor() = default;

    Neighbor(std::vector<unsigned> _removed_antennas, std::vector<unsigned> _added_antennas, Neighbor::ObjectiveResult _result)
        : removed_antennas(_removed_antennas)
        , added_antennas(_added_antennas)
        , result(_result)
    {}

    std::vector<unsigned> removed_antennas = std::vector<unsigned>();
    std::vector<unsigned> added_antennas = std::vector<unsigned>();
    ObjectiveResult result;
};


class NeighborSelector {
public:

    // Gets a reference to dcn_structure, and changes it for proposing, accepting or rejecting neighbors.
    // The candidate pool and the objective are supplied by the caller, so that neither is
    // read back out of the coverage structure.
    NeighborSelector(DCNS2 &_dcn_structure,
                     std::vector<unsigned> const &_antenna_candidates,
                     SearchObjective _objective)
        : dcn_structure(_dcn_structure)
        , antenna_candidates(_antenna_candidates)
        , objective(std::move(_objective))
    {}

    // Proposes a neighbor.
    // For efficient usage of the coverage structure, we always first fix a set of antennas to be removed.
    // Then many sets of antennas to be added can be evaluated cheaply, and the 'best' returned as a proposed neighbor.
    [[nodiscard]] Neighbor propose_neighbor();

    // Adds the proposed antennas.
    void accept_neighbor();

    // Adds the previously removed antennas.
    void reject_neighbor();


    /*    !!!!!!!!!!! IMMPORTANT !!!!!!!!!!!!!    */

    // A proposed neighbor always has to be either accepted or rejected.
    // This is enforced for the next proposal, but not outside this class!

    // It is not enforced that a neighbor removes and adds an equal amount of antennas.

    /*    !!!!!!!!!!! IMMPORTANT !!!!!!!!!!!!!    */


protected:

    // These three subroutines need to be implemented for propose_neighbor().
    // How to select the antennas to be removed and added is up to the specific application and should be implemented accordingly.

    // Should return a vector of antenna ids to be removed.
    // Should not yet remove them, that is handled by propose_neighbor().
    // The antennas need to be chosen from the ones that are currently selected by dcn_structure.
    [[nodiscard]] virtual std::vector<unsigned> select_antennas_to_be_removed() = 0;

    // Should return a set of antenna id vectors that could possibly be added.
    // The antennas need to be chosen from antenna_candidates.
    // It is not a problem if a removed antenna id is also part of this vector.
    [[nodiscard]] virtual std::vector<std::vector<unsigned>> select_antenna_sets_to_be_added_from_candidates() = 0;

    // Should select one of the given antenna id vectors (by whatever criterion).
    // Should not add any antennas yet, this is handled by propose_neighbor().
    // Should then construct a Neighbor from these antenna ids using peek or something equivalent.
    // For this e.g. the below helper function can be used.
    [[nodiscard]] virtual Neighbor select_neighbor(std::vector<std::vector<unsigned>> const &candidate_antenna_sets_for_adding) = 0;


    DCNS2 &dcn_structure;

    // The antennas that may be added. Owned by the caller.
    std::vector<unsigned> const &antenna_candidates;

    SearchObjective objective;

    // Reused across every peek of this selector, so it warms up once and then stops
    // allocating. Held per selector rather than per structure, so peeks stay const.
    PeekWorkspace workspace;

    // Stores the removed antennas after they were actually removed
    std::vector<unsigned> removed_antennas;

    // Helper function to get a neighbor from the set of removed antennas and a specified set of antennas to be added.
    // Evaluates the antenna set using peek.
    [[nodiscard]] Neighbor get_neighbor_from_antennas_to_be_added(const std::vector<unsigned> &antennas_to_be_added)
    {
        if (not are_antennas_removed){
            throw std::runtime_error("Exception in NeighborSelector: Trying to evaluate adding antennas before removing antennas.");
        }
        const Coverage absolute =
            dcn_structure.getCoverage() + dcn_structure.peek(antennas_to_be_added, workspace);
        return Neighbor(removed_antennas, antennas_to_be_added,
                        Neighbor::ObjectiveResult(absolute, objective));
    }

private:

    // booleans to ensure that nothing is removed or added improperly
    bool are_antennas_removed = false;
    bool is_currently_proposed_handled = true;

    // Stores the neighbor once the antennas to be added for the proposal were chosen
    Neighbor currently_proposed;

};


inline Neighbor NeighborSelector::propose_neighbor()
{
    // Ensures that no neighbor can be proposed unless the previously proposed was either accepted or rejected
    if (not is_currently_proposed_handled){
        throw std::runtime_error("Exception in NeighborSelector: Proposing neighbor before properly handling last proposed neighbor.");
    }
    // Remove antennas
    removed_antennas = select_antennas_to_be_removed();
    for (unsigned const antenna : removed_antennas){
        dcn_structure.remove(antenna);
    }
    are_antennas_removed = true;

    // Select a neighbor to be proposed from a candidate sets of antennas to be added
    currently_proposed = select_neighbor(select_antenna_sets_to_be_added_from_candidates());
    is_currently_proposed_handled = false;
    return currently_proposed;
}

inline void NeighborSelector::accept_neighbor()
{
    // Ensures that a neighbor can only be accepted (and antennas added) after antennas were removed.
    if (not are_antennas_removed){
        throw std::runtime_error("Exception in NeighborSelector: Trying to accept neighbor before removing antennas.");
    }
    for (auto const &antenna : currently_proposed.added_antennas){
        dcn_structure.add(antenna);
    }
    is_currently_proposed_handled = true;
    are_antennas_removed = false;
}

inline void NeighborSelector::reject_neighbor()
{
    // Ensures that a neighbor can only be rejected (and antennas added) after antennas were removed.
    if (not are_antennas_removed){
        throw std::runtime_error("Exception in NeighborSelector: Trying to reject neighbor before removing antennas.");
    }
    for (auto const &antenna : currently_proposed.removed_antennas){
        dcn_structure.add(antenna);
    }
    is_currently_proposed_handled = true;
    are_antennas_removed = false;
}

#endif // GISCUPBONN_ABSTRACTNEIGHBORSELECTOR_HPP
