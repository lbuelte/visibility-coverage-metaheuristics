#ifndef GISCUPBONN_LOCALSEARCHBACKEND
#define GISCUPBONN_LOCALSEARCHBACKEND

#include <chrono>
#include <random>
#include <vector>

#include "AbstractIntervalCoverageAlgorithm.hpp"
#include "geometry/DiscreteCoverageInstance.hpp"
#include "discrete/DCNS2.hpp"

class LocalSearchBackend {
public:
    LocalSearchBackend() = default;

protected:
    //! Sentinel for "no antenna". Member scope, so it does not collide with the
    //! namespace scope `none` of the old neighbourhood structure.
    static constexpr unsigned none = static_cast<unsigned>(-1);

    std::mt19937 rng;

    unsigned antenna_id_upper_bound, num_polygons, num_antennas;
    unsigned k;
    double tau;
    std::vector<unsigned> antenna_candidates;

    unsigned min_tabu_duration = 1, max_tabu_duration = 10;
    unsigned num_tabu_steps = 200;

    //! Cache of what peeking an antenna would gain, relative to the current selection.
    //! Both entries are raw deltas, so they stay valid when `progress` changes.
    std::vector<double> weighted_coverage_gain;
    std::vector<int> num_polygons_serviced_gain;
    std::vector<bool> dirty_antenna;

    std::chrono::time_point<std::chrono::steady_clock> t0;
    unsigned time_limit;

    double progress = 0.0;

    std::vector<unsigned> tabu_until;

    //! Counters over all calls to `iterate_one_opt`: how many 1-opt steps were tried at all,
    //! and how many of those found an improving swap and were applied
    unsigned long long num_one_opt_steps_tested = 0;
    unsigned long long num_one_opt_steps_improving = 0;

    void setup(const DiscreteCoverageInstance &instance, unsigned k, double tau, unsigned timelimit_in_ms, unsigned seed);
    void set_progress_to_lexicographic();
    void resize_vectors_etc();

    //! Scalarizes a coverage - or a coverage delta - under the current progress weighting
    [[nodiscard]] double objective(Coverage coverage) const;

    //! -------------------------------------------------------------------------
    //! Initial solution functions
    //! -------------------------------------------------------------------------
    //! Random function
    DCNS2 start_with_random_solution(const DiscreteCoverageInstance &instance, const std::vector<unsigned> &partial_solution);

    //! -------------------------------------------------------------------------
    //! Improve existing solution
    //! -------------------------------------------------------------------------
    //! Runs a lot of local search steps. If a local optimum is reached, returns the highest value progress could take (that is below the current progress) such that the solution would not be locally optimum
    //! Otherwise returns -1.0
    double iterate_one_opt(const DiscreteCoverageInstance &instance, DCNS2 &data_structure, unsigned max_iterations);

    //! Runs one two-opt step in which only the removal of some (low performing) antennas and their replacement with already good performing antennas is considered
    bool one_step_two_opt(const DiscreteCoverageInstance &instance, DCNS2 &data_structure, unsigned num_removal_candidates, unsigned num_insertion_candidates);
    //! Helper for two-opt
    bool one_step_two_opt_fixed_removed_antennas(const DiscreteCoverageInstance &instance, DCNS2 &data_structure, unsigned antenna_a, unsigned antenna_b, const std::vector<unsigned> &replacement_candidates);


    //! -------------------------------------------------------------------------
    //! Break out of local minima
    //! -------------------------------------------------------------------------
    //! Break out of local optima by temporarily weighting more coverage vs more service
    void breakout_by_objective_change(const DiscreteCoverageInstance &instance, DCNS2 &data_structure, double new_weight_param, double progress_steps, unsigned iterations_per_step);
    //! Break out of local optima by removing l antennas and replacing them with random ones
    void breakout_by_random_replacement(const DiscreteCoverageInstance &instance, DCNS2 &data_structure, unsigned num_to_replace);
    //! Use tabu tenure
    void breakout_by_one_opt_tabu(const DiscreteCoverageInstance &instance, DCNS2 &data_structure, unsigned max_tabu_steps);
    //! Remove some kmeans clusters and replace by good solution
    void breakout_by_removing_clusters(const DiscreteCoverageInstance &instance, DCNS2 &data_structure, unsigned num_clusters, unsigned num_clusters_removed, const IntervalCoverageSolver &replacement_algo, unsigned algo_time_in_ms = 1000);

    //! markes all antennas that share a seen polygon with antenna as dirty
    void mark_neighbouring_antennas_dirty(const DiscreteCoverageInstance &instance, unsigned antenna);

    void ensure_antenna_clean(DCNS2 &data_structure, unsigned antenna);

    [[nodiscard]] bool within_time() const;
};

#endif // GISCUPBONN_LOCALSEARCHBACKEND
