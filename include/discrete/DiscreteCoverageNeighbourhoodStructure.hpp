//
// Created by Michael on 15.06.2026
// 

#ifndef GISCUPBONN_DISCRETECOVERAGENEIGHBOURHOODSTRUCTURE
#define GISCUPBONN_DISCRETECOVERAGENEIGHBOURHOODSTRUCTURE

#include "DiscreteCoverageInstance.hpp"
#include "GeometricInformation.hpp"
#include "Interval.hpp"
#include <deque>
#include <functional>
#include <map>
#include <vector>

constexpr unsigned none = -1;

struct SeenByAntennaInformation {
    //! Stores the antenna ID in reduced indicies
    unsigned antenna_ID;
    //! Stores how much coverage would be lost if the antenna was removed from the solution
    double unique_coverage;
};

struct VisibilityIntervalBoundary {
    double position = -1.0;
    //! Uses reduced antenna IDs
    unsigned antenna_ID = none;
    bool start = false;

    friend bool operator<(const VisibilityIntervalBoundary &l, const VisibilityIntervalBoundary &r) {
        if (l.position == r.position) {
            return !l.start && r.start;
        }

        return l.position < r.position;
    }
};

struct PeekResult {
    double delta_coverage;
    double delta_weighted_coverage;
    double new_objective;
    int delta_serviced_polygons;
    unsigned absolute_num_serviced_polygons;
    double absolute_coverage;
    double absolute_weighted_coverage;
};

class DiscreteCoverageNeighbourhoodStructure {
public:
    DiscreteCoverageNeighbourhoodStructure(const DiscreteCoverageInstance &instance, unsigned k, double tau,
                                           std::vector<unsigned> antenna_candidates = {}
                                           );

    DiscreteCoverageNeighbourhoodStructure(const DiscreteCoverageInstance &instance, unsigned k, double tau,
                                           std::vector<unsigned> antenna_candidates, 
                                           std::function<double(double)> coverage_eval_function,
                                           std::function<double(double, unsigned)> global_objective_function
                                       );

    //! Sets a new polygon evaluation function
    void set_new_polygon_coverage_function(std::function<double(double)> coverage_eval_function);

    //! Sets a new polygon evaluation function, requires data structure to be clean
    void set_new_global_objectve_function(std::function<double(double, unsigned)> global_objective_function);

    //! Returns current objective value
    double objective_value() const;

    //! Returns the number of polygons with coverage over threshold
    unsigned num_covered_polygons() const;

    //! Returns the total coverage (IMPORTANT: not clamped with tau)
    double unweighted_coverage() const;
    
    //! Returns the total weighted coverage
    double weighted_coverage() const;

    //! Returns all (up to k) antennas currently selected
    std::vector<unsigned> currentlySelectedAntennas() const;

    //! Returns the number of selected antennas
    unsigned num_selected_antennas() const;

    //! Returns if an antenna is currently selected
    bool antenna_selected(unsigned antenna) const;

    //! Returns read access to the set of antenna candidates
    const std::vector<unsigned> &get_antenna_candidates();

    //! Returns sum of all coverages lost if antenna was removed (not clamped by tau)
    double uniqueCoverageByAntenna(unsigned antenna) const;

    //! Returns the loss of coverage weighted by the coverage weight function if antenna was removed
    double weightedCoverageByAntenna(unsigned antenna) const;

    //! Returns the number of polygons that would no longer be sufficiently covered if antenna was removed
    unsigned polygonsDependentOnAntenna(unsigned antenna) const;

    //! Returns a vector indexed by polygon IDs of how much any given polygon is covered
    const std::vector<double> &coverage_by_polygon() const;

    //! Adds the antenna to the antennas currently in use
    void add_antenna_lazy(unsigned antenna);
    //! adds a set of antennas. Equivalent to calling add_antenna_lazy once per antenna
    void add_antennas_lazy(const std::vector<unsigned> &antennas);

    //! Removes one antenna
    void remove_antenna_lazy(unsigned antenna);
    //! Removes the antennas from the antennas currently in use, preferrable over valling remove_antenna_lazy multiple times
    void remove_antennas_lazy(const std::vector<unsigned> &antennas);

    //! Return the objective value of the current solution + antennas
    PeekResult peek_antennas(const std::vector<unsigned> &antennas);

    //! Returns a vector of peek results for each set of antennas peeked. Uses up to num_threads many threads
    std::vector<PeekResult> peek_antenna_sets_parallel(const std::vector<std::vector<unsigned>> &antenna_sets, unsigned num_threads);

    //! Cleans the data structure so that the objective etc. is properly calculated again
    void clean_structure();
    bool is_clean() const;

    //! Cleans structure for solution peeks, i.e. to check the objective value of current solution + some additional polygons
    void clean_structure_for_peek();
    bool is_clean_for_peek() const;

private:
    const DiscreteCoverageInstance &instance;
    std::vector<unsigned> antenna_candidates;

    unsigned k;
    double tau;
    unsigned num_polygons, antenna_id_bound;
    std::function<double(double)> coverage_eval_function = [this](double coverage){return std::min(coverage, tau);};
    std::function<double(double, unsigned)> global_objective_function = [](double total_coverage, unsigned num_polygons_covered){return static_cast<double>(num_polygons_covered);};

    //! Data stored for the whole thing
    //! Sum of all coverages over all buildings, clamped with tau
    double total_coverage;
    //! Sum of all coverages after applying weighting function
    double total_weighted_coverage;
    //! Number of polygons for which the coverage is >= tau
    unsigned num_polygons_covered;
    //! Value of the current solution under the given objective function
    double objective_by_current_function;
    //! Sum of all coverages over all buildings, clamped with tau (for peek)
    double total_coverage_peek;
    //! Sum of all coverages after applying weighting function (for peek)
    double total_weighted_coverage_peek;
    //! Number of polygons for which the coverage is >= tau (for peek)
    unsigned num_polygons_covered_peek;
    //! Value of the current solution under the given objective function (for peek)
    double objective_by_current_function_peek;

    //-------------------------------------------------------------------------------------------
    //! Information stored for the antennas
    //! Stores the reduced index of an antenna if it is in the current solution, none otherwise
    std::vector<unsigned> reduced_antenna_index_vec;
    std::map<unsigned, unsigned> reduced_antenna_index_map;
    bool reduced_index_is_vector;

    //--------------------------------------------------------------------------------------------
    //! Stored for reduced antenna indicies
    //! Stored actual antenna index, none if a reduced antenna index is currently not used
    std::vector<unsigned> original_index;
    //! Stored reduced indicies that are currently unused
    std::deque<unsigned> unused_indicies;

    //! Stores how much total coverage would be lost if this antenna was removed from the solution
    std::vector<double> unique_coverage;
    //! Stores how much total weighted coverage would be lost if this antenna was removed from the solution
    std::vector<double> unique_weighted_coverage;
    //! Stores how many polygons fall under the threshold if this antenna is removed from the solution
    std::vector<unsigned> num_polygons_necessary_support;

    //--------------------------------------------------------------------------------------------
    //! Stored for the polygons
    //! Stores by polygon ID how much of a polygon is currently seen
    std::vector<double> current_coverage;
    std::vector<double> current_coverage_peek;
    //! Stores which antennas of the current soolution (in reduced indicies) see this polygon
    std::vector<std::vector<SeenByAntennaInformation>> currently_seen_by_antennas;
    //! Stores by polygon ID the intervals currently covered by all active antennas
    std::vector<std::vector<Interval>> union_of_coverages;
    //! Stores by polygon ID the start and endpoints of visibility by currently selected antennas
    std::vector<std::vector<VisibilityIntervalBoundary>> visibility_regions;

    //--------------------------------------------------------------------------------------------
    //! Helper data structures to be used in between
    //! clean means all data is internally consistent, clean_for_peek means the global objective, polygon coverage and union_of_coverages are consistent
    bool clean = true, clean_for_peek = true;
    //! Stores queue of polygons for which polygon values need to be updated
    std::deque<unsigned> dirty_polygons_queue;
    //! Bool vector to quickly check if a polygon is already marked as dirty
    std::vector<bool> dirty_polygon;
    //! Stores queue of polygons for which polygon values need to be updated
    std::deque<unsigned> dirty_polygons_queue_peek;
    //! Bool vector to quickly check if a polygon is already marked as dirty
    std::vector<bool> dirty_polygon_peek;

    //! Temp bool vector for reduced antenna IDs and polygon IDs
    std::vector<bool> temp_bool;
    //! Temp bool vector vector for parallel peeks
    std::vector<std::vector<bool>> temp_bool_parallel;
    //! Temp double vector for reduced antenna IDs and polygon IDs
    std::vector<double> temp_double;
    //! Temp double vector for reduced antenna IDs and polygon IDs
    std::vector<unsigned> temp_int;

    std::vector<std::vector<Interval>> peek_coverage;
    std::vector<std::vector<std::vector<Interval>>> peek_coverage_parallel;

    //! Resets all data structures for an empty solution
    void reset_to_empty();

    //! Computes the coverage  polygon with current antennas + peek antennas
    double polygon_values_with_peek(unsigned polygon);
    double polygon_values_with_peek(unsigned polygon, std::vector<Interval> &polygon_peek_coverage);

    //! Ruthread of peek_antenna_sets_parallel
    std::vector<PeekResult> peek_antenna_sets_parallel_one_thread(const std::vector<std::vector<unsigned>> &antenna_sets, unsigned thread_it, unsigned num_threads);
    //!
    PeekResult peek_specific_antenna_set_parallel(const std::vector<unsigned> &antennas, unsigned thread_id);

    //! Removes a polygons contribution from total coverage and coverage by antenna
    //! To be used when a polygon becomes dirty
    void remove_dirty_polygon_from_objective(unsigned polygon);
    //! Makes a polygon clean again, updates all its values and re-adds it to the objective
    void clean_polygon(unsigned polygon);
    //! Cleans a polygon for peeking, that is updates union_of_coverages
    void clean_polygon_for_peek(unsigned polygon);
    
    //! Computes the weighted unique coverage
    double weighted_unique_coverage(double total_coverage, double unique_coverage) const;
    //! Returns the reduced antenna id from the appropriate structure
    unsigned reduced_antenna_ID(unsigned antenna) const;
    //! Sets the reduced antenna id for a specific antenna
    void set_reduced_antenna_ID(unsigned antenna, unsigned reduced_id);

    //! debug function to check internal consistency of all data structures
    void check_internal_consistency() const;
};

#endif // GISCUPBONN_DISCRETECOVERAGENEIGHBOURHOODSTRUCTURE
