//
// Created by Laura on 7/10/26.
//

#ifndef GISCUPBONN_SIMULATEDANNEALINGWITHLOCALSEARCHFINISH_HPP
#define GISCUPBONN_SIMULATEDANNEALINGWITHLOCALSEARCHFINISH_HPP

#include "discrete/AbstractIntervalCoverageAlgorithm.hpp"

#include "utility/ConfigParser.hpp"
#include "utility/Solution.hpp"
#include "utility/TimelimitCountdown.hpp"

#include "discrete/SimulatedAnnealingAlgorithm.hpp"
#include "discrete/LocalSearch.hpp"



class SimulatedAnnealingWithLocalSearchFinish : public IntervalCoverageSolver {
public:
    SimulatedAnnealingWithLocalSearchFinish(Config const &_cfg, 
            std::optional<std::filesystem::path> const &_log_output_path = std::nullopt, 
            std::optional<std::string> const &_run_identifier = std::nullopt) 
        : cfg(_cfg) 
        , log_output_path(std::move(_log_output_path))
        , run_identifier(std::move(_run_identifier))
    {}

    Solution run(DiscreteCoverageInstance const &instance, 
                 const unsigned k, const double tau, 
                 const unsigned timelimit_in_ms, 
                 const unsigned seed,
                 std::vector<unsigned> const &partial_solution_antenna_ids = std::vector<unsigned>()) const override;

private:
    Config const &cfg;
    std::optional<std::filesystem::path> log_output_path;
    std::optional<std::string> run_identifier;  
};


inline Solution SimulatedAnnealingWithLocalSearchFinish::run(
        DiscreteCoverageInstance const &instance, 
        const unsigned k, const double tau, 
        const unsigned timelimit_in_ms, 
        const unsigned seed,
        std::vector<unsigned> const &partial_solution_antenna_ids) const
{
    Countdown countdown(timelimit_in_ms);

    SimulatedAnnealingConfig simann_cfg = cfg.simann;
    if (run_identifier.has_value()){
        simann_cfg.set_output_filepath(run_identifier.value(), log_output_path);
    } else {
        simann_cfg.set_output_filepath(k, tau, cfg.input_path);
    }
    simann_cfg.set_num_clusters(k);
    SimulatedAnnealingAlgorithm simann(simann_cfg);

    // Time for Local Search is 5% of total runtime, but no more than 30min
    unsigned time_for_ls = std::min(1800000u, static_cast<unsigned>(std::floor(countdown.remaining_ms() / 20)));
    unsigned time_for_simann = static_cast<unsigned>(std::floor(countdown.remaining_ms()) - time_for_ls);

    std::cout << "Running Simulated Annealing for " << time_for_simann / 1000 << " seconds ..." << std::endl;    
    Solution simann_sol = simann.run(instance, k, tau, time_for_simann, seed, partial_solution_antenna_ids);

    LocalSearchConfig::LocalSearchConfig ls_config = cfg.ls_config;
    if (run_identifier.has_value()){
        ls_config.set_output_path(run_identifier.value(), log_output_path);
    } 
    std::cout << "Local search will be written to " << ls_config.ls_log_path << std::endl;
    LocalSearch ls(ls_config);

    std::cout << "Running Local Search for " << time_for_ls / 1000 << " seconds ..." << std::endl;
    return ls.run(instance, k, tau, time_for_ls, seed, simann_sol.get_solution_antenna_ids());
}


#endif // GISCUPBONN_SIMULATEDANNEALINGWITHLOCALSEARCHFINISH_HPP
