//
// Created by Laura on 6/24/26.
//

#ifndef GISCUPBONN_SIMULATEDANNEALINGALGORITHM_HPP
#define GISCUPBONN_SIMULATEDANNEALINGALGORITHM_HPP

#include "geometry/DiscreteCoverageInstance.hpp"

#include "utility/SimulatedAnnealingConfig.hpp"
#include "utility/Solution.hpp"
#include "utility/TimelimitCountdown.hpp"
#include "utility/AbstractLogger.hpp"

#include "discrete/AbstractIntervalCoverageAlgorithm.hpp"
#include "discrete/DCNS2.hpp"
#include "discrete/AbstractNeighborSelector.hpp"
#include "discrete/ClusterExchangeNeighborSelector.hpp"



/* -------- SimulatedAnnealingAlgorithm --------- */


class SimulatedAnnealingAlgorithm : public IntervalCoverageSolver {
public:
    SimulatedAnnealingAlgorithm() : cfg(parse_simulated_annealing_config(std::filesystem::path())) {}
    SimulatedAnnealingAlgorithm(SimulatedAnnealingConfig const &_cfg) : cfg(_cfg) {}

    Solution run(DiscreteCoverageInstance const &instance, 
                 const unsigned k, const double tau, 
                 const unsigned timelimit_in_ms, 
                 const unsigned seed,
                 std::vector<unsigned> const &partial_solution_antenna_ids = std::vector<unsigned>()) const override;

private:

    std::function<double(double, unsigned)> initial_objective_function(DiscreteCoverageInstance const &instance) const;

    SimulatedAnnealingConfig const cfg;
};



/* -------- SimulatedAnnealing SA_Solution --------- */

struct SA_Solution {
    SA_Solution() : num_serviced_polygons(0) {}
    SA_Solution(DCNS2 const &_dcn_structure)
        : antenna_ids(_dcn_structure.getSelectedAntennas())
        , num_serviced_polygons(_dcn_structure.getCoverage().serviced)
    {}

    std::vector<unsigned> antenna_ids;
    unsigned num_serviced_polygons;
};



/* -------- SimulatedAnnealingLogger --------- */


class SimulatedAnnealingLogger : public Logger{
public:
    using Logger::Logger;

    enum class ProposalAcceptanceStatus {
        Rejected,
        AcceptedWithDecrease,
        Improved,
        BestImproved
    };

    struct SimulatedAnnealingProposalStatistics {
        unsigned num_total_proposals = 0;
        unsigned num_accepted_proposals = 0;
        unsigned num_improving_proposals = 0;
        unsigned num_accepted_decreasing_proposals = 0;
        unsigned num_rejected_proposals = 0;
        unsigned num_best_improvals = 0;
    };

    struct ClusterExchangeStatistics {
        unsigned num_accepted_decreasing_cluster_exchanges = 0;
        unsigned num_improving_cluster_exchanges = 0;
        unsigned num_rejected_cluster_exchanges = 0;
        unsigned acc_size_of_accepted_decreasing_clusters = 0;
        unsigned acc_size_of_improving_clusters = 0;
        unsigned acc_size_of_rejected_clusters = 0;
        unsigned num_greedy_adding = 0;
    };

    void update_all_current_num_proposals(ProposalAcceptanceStatus const status);
    void log_proposal_statistics(double progress, std::ostream *stream = nullptr);

    void update_cluster_exchange_stats(ClusterExchangeNeighborSelector const &selector, unsigned cluster_size, ProposalAcceptanceStatus const status);
    void log_cluster_exchange_statistics(std::ostream *stream = nullptr);

    void log_new_best_solution_value(unsigned value, double progress, std::ostream *stream = nullptr) const;
    void log_solution(SA_Solution const &solution, std::ostream *stream = nullptr) const;

    void log_parameters(unsigned k, double tau, unsigned seed, std::ostream *_stream = nullptr) const;
    void log_parameters(MaxNumAntennasToRemove const &max_num_antennas_to_remove, 
                        BufferSizeTopR const &top_r_buffer_size, 
                        NumSamplesForAdding const &num_samples, 
                        std::ostream *_stream = nullptr) const;
    void log_config(SimulatedAnnealingConfig const &config, std::ostream *_stream = nullptr) const;
    
private:
    SimulatedAnnealingProposalStatistics previously_logged_stats;
    SimulatedAnnealingProposalStatistics current_stats;
    ClusterExchangeStatistics cluster_exchange_stats;
};



/* -------- SimulatedAnnealingRun --------- */


class SimulatedAnnealingRun {
public:

    SimulatedAnnealingRun(DiscreteCoverageInstance const &_instance, 
                          const unsigned _k, 
                          const double _tau, 
                          Countdown const &_global_sa_countdown,
                          const unsigned _seed,
                          std::vector<unsigned> const &partial_solution_antenna_ids,
                          std::function<double(double, unsigned)> const &_initial_obj_function,
                          SimulatedAnnealingConfig const &_cfg);

    Solution simulated_annealing();

    void set_sa_logger(std::filesystem::path const &filepath){
        sa_logger.set_default_stream(filepath);
    }
    
private:

    SA_Solution current_best_solution;
    void write_best_solution(std::ostream &stream) const;
    void write_best_solution_atomic(std::string const &filepath) const;

    void set_initial_solution(std::vector<unsigned> const &partial_solution_antenna_ids);

    SimulatedAnnealingLogger::ProposalAcceptanceStatus process_neighbor_proposal(
            NeighborSelector &selector,
            double &current_obj_value,
            unsigned &size_of_proposal,
            double time_progress,
            double cooling_progress,
            bool verbose_logging = false);

    MaxNumAntennasToRemove get_updated_max_num_removed_antennas(double real_progress) {
        return cfg.simann_topk_params.max_num_antennas_to_remove.
                get_interpolated_value(real_progress, cfg.interpolation_cutoff_progress);
    }
    BufferSizeTopR get_updated_buffer_size_top_r(double real_progress){
        return cfg.simann_topk_params.buffer_size_top_r.
                get_interpolated_value(real_progress, cfg.interpolation_cutoff_progress);
    }
    NumSamplesForAdding get_updated_num_samples_for_adding(double real_progress){
        return cfg.simann_topk_params.num_samples_for_adding
                .get_interpolated_value(real_progress, cfg.interpolation_cutoff_progress);
    }

    std::function<double(double,double)> compute_acceptance_threshold_function() const;

    [[nodiscard]] double computeObjValue() const {
        const Coverage current = dcn_structure.getCoverage();
        return objective(current.total, (unsigned) current.serviced);
    }

    DiscreteCoverageInstance const &instance;
    const unsigned k;
    const double tau;
    const unsigned seed;

    Countdown const &global_sa_countdown;
    SimulatedAnnealingConfig const &cfg;
    
    DCNS2 dcn_structure;

    SimulatedAnnealingLogger sa_logger;
    mutable std::optional<std::filesystem::path> last_written_solution_path;
    // Owned by the run rather than read back out of dcn_structure, so that the selectors
    // depend on the coverage structure only for coverage.
    std::vector<unsigned> antenna_candidates;
    SearchObjective objective;

    mutable std::mt19937 rng;
    mutable std::uniform_real_distribution<double> acceptance_probability;
    std::function<double(double,double)> acceptance_probability_threshold_function;

};



#endif // GISCUPBONN_SIMULATEDANNEALINGALGORITHM_HPP