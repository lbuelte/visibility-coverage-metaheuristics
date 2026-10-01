//
// Created by Laura on 6/24/26.
//

#include "discrete/SimulatedAnnealingAlgorithm.hpp"

#include "discrete/CoolingSchedules.hpp"
#include "discrete/GreedyAlgorithm.hpp"
#include "discrete/RandomTopKNeighborSelector.hpp"
#include "discrete/ClusterExchangeNeighborSelector.hpp"

#include <numeric>

#include <iomanip>



/* -------- Implementations for SimulatedAnnealingAlgorithm --------- */


Solution SimulatedAnnealingAlgorithm::run(DiscreteCoverageInstance const &instance, 
             const unsigned k, const double tau, 
             const unsigned global_timelimit_in_ms, 
             const unsigned seed,
             std::vector<unsigned> const &partial_solution_antenna_ids) const
{
    Countdown global_sa_countdown(global_timelimit_in_ms);
    auto initial_obj_func = initial_objective_function(instance);
    SimulatedAnnealingRun sa_run(instance, k, tau, global_sa_countdown, seed, partial_solution_antenna_ids, initial_obj_func, cfg);
    return sa_run.simulated_annealing();
}

std::function<double(double, unsigned)> SimulatedAnnealingAlgorithm::initial_objective_function(DiscreteCoverageInstance const &instance) const
{
    auto num_polygons = instance.get_number_polygons();
    return [num_polygons](double total_weighted_coverage, unsigned num_serviced_polygons) -> double {
        return num_serviced_polygons + (total_weighted_coverage / static_cast<double>(num_polygons)); };
}



/* -------- Implementations for SimulatedAnnealingRun --------- */


SimulatedAnnealingRun::SimulatedAnnealingRun(DiscreteCoverageInstance const &_instance, 
                                             const unsigned _k, 
                                             const double _tau, 
                                             Countdown const &_global_sa_countdown,
                                             const unsigned _seed,
                                             std::vector<unsigned> const &partial_solution_antenna_ids,
                                             std::function<double(double, unsigned)> const &_initial_obj_function,
                                             SimulatedAnnealingConfig const &_cfg)
    : instance(_instance)
    , k(_k)
    , tau(_tau)
    , seed(_seed)
    , dcn_structure(_instance, _tau)
    , global_sa_countdown(_global_sa_countdown)
    , cfg(_cfg)
    , sa_logger()
    , rng(seed)
    , acceptance_probability(0.0, 1.0)
    , acceptance_probability_threshold_function(compute_acceptance_threshold_function())
{
    objective = _initial_obj_function;
    antenna_candidates.resize(instance.antenna_covers_polygons().size());
    std::iota(antenna_candidates.begin(), antenna_candidates.end(), 0u);

    // Set log output paths
    std::filesystem::path log_output_filepath = (cfg.simann_output_path.string() + ".simann.log");
    sa_logger.set_default_stream(log_output_filepath);
    
    sa_logger.log("Running Simulated Annnealing\n", 
            "   for ", (_global_sa_countdown.remaining_ms() / 1000), " seconds\n",
            "   on ", instance.get_number_antennas(), " antenna candidates\n");
    sa_logger.log_parameters(k, tau, seed);
    sa_logger.log_config(cfg);

    // Be careful about order here. Initial solution is called with default dcn objective, and then initial objective is set
    sa_logger.log("Simulated Annealing: Setting initial solution ", cfg.initial_sol, " ...");    
    set_initial_solution(partial_solution_antenna_ids);
    
    const Coverage initial = dcn_structure.getCoverage();
    sa_logger.log("Initial solution has simulated annealing objective value of ",
                  objective(initial.total, (unsigned) initial.serviced),
                  " with ", initial.serviced, " serviced polygons.");
}


void SimulatedAnnealingRun::set_initial_solution(std::vector<unsigned> const &partial_solution_antenna_ids)
{
    if (partial_solution_antenna_ids.size() == k){
        for (unsigned const antenna : partial_solution_antenna_ids)
            dcn_structure.add(antenna);
        return;
    } 

    switch(cfg.initial_sol)
    {
        case SimAnnInitialSolution::Greedy : {
            GreedyAlgorithm greedy;
            Solution sol = greedy.run(instance, k, tau, global_sa_countdown.remaining_ms(), seed, partial_solution_antenna_ids);
            for (unsigned const antenna : sol.get_solution_antenna_ids())
                dcn_structure.add(antenna);
            break;
        }
        case SimAnnInitialSolution::Random : {
            for (unsigned const antenna : partial_solution_antenna_ids)
                dcn_structure.add(antenna);

            std::uniform_int_distribution<unsigned> pick_random_candidate(0, instance.get_number_antennas() - 1);
            int num_missing = k - partial_solution_antenna_ids.size();
            while (num_missing > 0){
                auto antenna_id = pick_random_candidate(rng);
                if (not dcn_structure.has(antenna_id)){
                    dcn_structure.add(antenna_id);
                    --num_missing;
                }
            }
            break;
        }
        default : {
            std::cerr << "Warning: Exception in Simulated Annealing: Unhandled enum case for SimAnnInitialSolution: " 
                    + std::to_string(static_cast<int>(cfg.initial_sol));
            // Default back to Random initialization
            for (unsigned const antenna : partial_solution_antenna_ids)
                dcn_structure.add(antenna);

            std::uniform_int_distribution<unsigned> pick_random_candidate(0, instance.get_number_antennas() - 1);
            int num_missing = k - partial_solution_antenna_ids.size();
            while (num_missing > 0){
                auto antenna_id = pick_random_candidate(rng);
                if (not dcn_structure.has(antenna_id)){
                    dcn_structure.add(antenna_id);
                    --num_missing;
                }
            }
            break;           
        }
    }

    current_best_solution = SA_Solution(dcn_structure);
    write_best_solution_atomic(cfg.simann_output_path.string() + "_val_" 
            + std::to_string(current_best_solution.num_serviced_polygons) + ".simann.sol");
}


std::function<double(double,double)> SimulatedAnnealingRun::compute_acceptance_threshold_function() const 
{
    AcceptanceThresholdFunctionGenerator atf_gen(instance);
    CoolingParamsForTopKNeighborSelector cooling_params{
            cfg.simann_topk_params.max_num_antennas_to_remove.init, 
            cfg.simann_topk_params.buffer_size_top_r.init,
            cfg.simann_topk_params.num_samples_for_adding.init};
    // Run cooling simulation for at most 5min, but never more than 20% of the simann timelimit
    unsigned cooling_timelimit_in_ms = std::min(300000u, static_cast<unsigned>(global_sa_countdown.remaining_ms() / 5.0));
    sa_logger.log("Initializing cooling function for ", (cooling_timelimit_in_ms / 1000), " seconds\n",
            "Cooling parameters: max_num_antennas_to_remove (", cooling_params.max_num_antennas_to_remove.value, 
            ") , buffer_size (", cooling_params.top_r_buffer_size.value,
            ") , num_samples_for_adding (", cooling_params.num_samples_for_adding.value, ")");
    return atf_gen.run_for_top_r_selector(k, tau, cooling_params, cooling_timelimit_in_ms);
}


SimulatedAnnealingLogger::ProposalAcceptanceStatus SimulatedAnnealingRun::process_neighbor_proposal(
        NeighborSelector &neighbor_selector,
        double &current_obj_value,
        unsigned &size_of_proposal,
        double time_progress,
        double cooling_progress,
        bool verbose_logging)
{
    Neighbor proposal = neighbor_selector.propose_neighbor();
    size_of_proposal = proposal.removed_antennas.size();
    double gain = proposal.result.objective_value - current_obj_value;

    if (gain >= 0){
        // Always accept a proposal if it improves the objective
        neighbor_selector.accept_neighbor();
        current_obj_value = computeObjValue();
        if (verbose_logging){ 
            sa_logger.log("Proposed a neighbor exchange of ", proposal.removed_antennas.size(),
                          " antennas with an objective gain of ", gain, " : Accepted");
        }
        if (proposal.result.num_serviced_polygons > current_best_solution.num_serviced_polygons){
            // If the proposal improves global objective of the best solution, we update
            current_best_solution = SA_Solution(dcn_structure);
            sa_logger.log_new_best_solution_value(current_best_solution.num_serviced_polygons, time_progress);
            write_best_solution_atomic(cfg.simann_output_path.string() + "_val_" 
                    + std::to_string(current_best_solution.num_serviced_polygons) + ".simann.sol");
            return SimulatedAnnealingLogger::ProposalAcceptanceStatus::BestImproved;
        } else {
            return SimulatedAnnealingLogger::ProposalAcceptanceStatus::Improved;
        }                    
    }
    else if (acceptance_probability(rng) <= acceptance_probability_threshold_function(gain, cooling_progress)){
        // Accept a proposal with an objective decrease if the acceptance probability 
        // is below the acceptance threshold given by the cooling function
        neighbor_selector.accept_neighbor();
        current_obj_value = computeObjValue();
        if (verbose_logging){ 
            sa_logger.log("Proposed a neighbor exchange of ", proposal.removed_antennas.size(),
                          " antennas with an objective gain of ", gain, " : Accepted");
        }
        return SimulatedAnnealingLogger::ProposalAcceptanceStatus::AcceptedWithDecrease;
    }
    else {
        // Otherwise, reject the proposal
        neighbor_selector.reject_neighbor();
        // probably not strictly necessary, but shouldn't hurt
        current_obj_value = computeObjValue();
        if (verbose_logging){ 
            sa_logger.log("Proposed a neighbor exchange of ", proposal.removed_antennas.size(),
                          " antennas with an objective gain of ", gain, " : Rejected");
        }
        return SimulatedAnnealingLogger::ProposalAcceptanceStatus::Rejected;
    }
}


Solution SimulatedAnnealingRun::simulated_annealing()
{
    // Initialize best objective value
    double current_obj_value = computeObjValue();

    // Set randomness
    int topk_neighbor_selector_seed = seed + 1;
    std::bernoulli_distribution use_cluster_exchange_neighbor(cfg.probability_for_cluster_exchange);
    int cluster_exchange_neighbor_selector_seed = seed + 2;

    // Initialize topk neighbor selector
    sa_logger.log("Initializing neighbor selector with parameters: max_num_antennas_to_remove (", 
            cfg.simann_topk_params.max_num_antennas_to_remove.init.value, ") , buffer_size (", cfg.simann_topk_params.buffer_size_top_r.init.value,
            ") , num_samples_for_adding (", cfg.simann_topk_params.num_samples_for_adding.init.value, ")");
    RandomTopKNeighborSelector topk_neighbor_selector(
            dcn_structure,
            antenna_candidates,
            objective,
            topk_neighbor_selector_seed,
            cfg.simann_topk_params.max_num_antennas_to_remove.init,             
            cfg.simann_topk_params.buffer_size_top_r.init, 
            cfg.simann_topk_params.num_samples_for_adding.init);

    // Helper variables to track when to report the proposal statistics
    unsigned progress_reported = 0;
    unsigned progress_to_report = 0;

    bool reheat_active = false;
    double reheat_start_progress = 0.0;
    
    // Track time for the simulated annealing main loop
    Countdown sa_run_countdown(global_sa_countdown.remaining_ms());
    sa_logger.log("Started Simulated Annealing main loop with a remaining timelimit of ",
                  std::floor(sa_run_countdown.remaining_ms() / 1000.0), " seconds.");
    auto time_of_last_best_improvement = global_sa_countdown.passed_ms();

    int num_iterations = 0;
    while (sa_run_countdown.progress() < 1){

        double time_progress = sa_run_countdown.progress();

        // Check whether we should start reheating
        if (!reheat_active &&
            time_progress >= cfg.min_reheat_start_progress &&
            time_progress <= cfg.max_reheat_start_progress &&
            (global_sa_countdown.passed_ms() - time_of_last_best_improvement) > cfg.stagnation_threshold_seconds * 1000)
        {
            reheat_active = true;
            reheat_start_progress = time_progress;
            sa_logger.log("Starting reheating at progress ", time_progress, ", cooling progress reset to ",
                    time_progress - cfg.reheat_amount, ", stagnation time ", 
                    (global_sa_countdown.passed_ms() - time_of_last_best_improvement) / 1000.0, " seconds");
        }

        double cooling_progress = [&](){
            if (not reheat_active){
                return time_progress;
            }
            double recovery = std::clamp(
                    (time_progress - reheat_start_progress) / (1.0 - reheat_start_progress),
                    0.0,
                    1.0);
            double offset = cfg.reheat_amount * (1.0 - recovery);
            return time_progress - offset;
        }();

        // Reporting proposal statistics for every 10% of progress
        progress_to_report = static_cast<unsigned>(std::floor(time_progress * 10));
        if (progress_to_report != progress_reported){
            sa_logger.log_proposal_statistics(time_progress);
            sa_logger.log_parameters(
                    get_updated_max_num_removed_antennas(time_progress),
                    get_updated_buffer_size_top_r(time_progress), 
                    get_updated_num_samples_for_adding(time_progress));
            progress_reported = progress_to_report;
        }

        unsigned size_of_proposal = 0;

        // With some (low) probability, kick a whole cluster from the solution
        if (use_cluster_exchange_neighbor(rng)){
            unsigned num_clusters = cfg.num_clusters;
            ClusterExchangeNeighborSelector cluster_exchange_neighbor_selector(
                    dcn_structure,
                    antenna_candidates,
                    objective,
                    instance, k, tau, 
                    cluster_exchange_neighbor_selector_seed + (num_iterations % 1000),
                    num_clusters,
                    get_updated_max_num_removed_antennas(time_progress),
                    get_updated_buffer_size_top_r(time_progress),
                    get_updated_num_samples_for_adding(time_progress));

            cluster_exchange_neighbor_selector.set_removal_mode(cfg.removal_mode);

            // With more progress do greedy adding with higher probability
            std::bernoulli_distribution do_greedy_adding(cfg.probability_for_progress_weighted_greedy_adding * time_progress);
            if (do_greedy_adding(rng)){
                cluster_exchange_neighbor_selector.set_adding_mode(AddingMode::Greedy);
            } else {
                cluster_exchange_neighbor_selector.set_adding_mode(cfg.adding_mode);
            }

            auto proposal_acceptance_status = 
                process_neighbor_proposal(cluster_exchange_neighbor_selector, current_obj_value, size_of_proposal, time_progress, cooling_progress);
                if (proposal_acceptance_status == SimulatedAnnealingLogger::ProposalAcceptanceStatus::BestImproved){
                    time_of_last_best_improvement = global_sa_countdown.passed_ms();
                }
            sa_logger.update_all_current_num_proposals(proposal_acceptance_status);
            sa_logger.update_cluster_exchange_stats(cluster_exchange_neighbor_selector, size_of_proposal, proposal_acceptance_status);
        } 
        else {
            // Set the topk neighbor selector hyperparameters depending on the progress made
            topk_neighbor_selector.set_new_hyperparameters(
                    get_updated_max_num_removed_antennas(time_progress),
                    get_updated_buffer_size_top_r(time_progress), 
                    get_updated_num_samples_for_adding(time_progress));
            auto proposal_acceptance_status = 
                process_neighbor_proposal(topk_neighbor_selector, current_obj_value, size_of_proposal, time_progress, cooling_progress);    
            if (proposal_acceptance_status == SimulatedAnnealingLogger::ProposalAcceptanceStatus::BestImproved){
                time_of_last_best_improvement = global_sa_countdown.passed_ms();
            }
            sa_logger.update_all_current_num_proposals(proposal_acceptance_status);        
        }
        ++num_iterations;
    }

    sa_logger.log_proposal_statistics(1);
    sa_logger.log_cluster_exchange_statistics();
    sa_logger.log_solution(current_best_solution);
    return Solution(instance, k, tau, current_best_solution.antenna_ids);
}


void SimulatedAnnealingRun::write_best_solution(std::ostream &stream) const
{
    if (not stream) return;

    stream << std::setprecision(std::numeric_limits<double>::max_digits10);

    stream << "(" << tau << ", " << k << ")\n";

    for (int i = 0; i < current_best_solution.antenna_ids.size(); ++i){
        const auto antenna_id = current_best_solution.antenna_ids[i];
        const auto& pos = instance.get_antenna_data(antenna_id).position;
        stream << "(" << pos.x() << "," << pos.y() << ")"; 
        if (i < current_best_solution.antenna_ids.size() - 1){
            stream << ",";
        }
    }
    stream << '\n';

    auto const serviced_polygons = instance.evaluate_antenna_vector(current_best_solution.antenna_ids, tau);
    for (int i = 0; i < serviced_polygons.size(); ++i){
        stream << instance.get_original_id_for_polygon(serviced_polygons[i]);
        if (i < serviced_polygons.size() - 1){
            stream << ",";
        }
    }    
    stream << '\n';
}

void SimulatedAnnealingRun::write_best_solution_atomic(std::string const &filepath) const
{
    namespace fs = std::filesystem;
    try {
        fs::path target(filepath);

        if (target.has_parent_path()) {
            std::error_code ec;
            fs::create_directories(target.parent_path(), ec);
            if (ec) {
                std::cerr << "Warning: write_best_solution: Could not create directories for path '"
                            << filepath << "': " << ec.message() << '\n';
                return;
            }
        }

        // Write into a temp file in the SAME directory as the target, so the
        // later rename is on one filesystem and therefore atomic.
        std::ostringstream tid;
        tid << std::this_thread::get_id();
        fs::path temp = target;
        temp += ".tmp-" + tid.str();

        std::ofstream stream(temp, std::ios::trunc);
        if (not stream) {
            std::cerr << "Warning: write_best_solution: Could not open temp file '"
                        << temp.string() << "' for writing. Skipping this backup.\n";
            return;
        }
        write_best_solution(stream);
        stream.flush();
        if (stream.fail()) {
            std::error_code ec;
            fs::remove(temp, ec);
            std::cerr << "Warning: write_best_solution: Write error occurred on temp file '"
                        << temp.string() << "'. Skipping this backup.\n";
            return;
        }

        // Atomically publish the new file under its final name. Until this
        // succeeds, the previous best solution (if any) is untouched.
        std::error_code rename_ec;
        fs::rename(temp, target, rename_ec);
        if (rename_ec) {
            std::error_code ec;
            fs::remove(temp, ec);
            std::cerr << "Warning: write_best_solution: Could not rename '"
                        << temp.string() << "' to '" << target.string() << "': "
                        << rename_ec.message() << ". Skipping this backup.\n";
            return;
        }

        // Only now delete the previous best-value file, since the new one
        // is confirmed safely in place.
        if (last_written_solution_path.has_value() and *last_written_solution_path != target) {
            std::error_code remove_ec;
            fs::remove(*last_written_solution_path, remove_ec);
            // Not fatal if this fails — stale files are a cleanliness issue,
            // not a correctness one.
        }

        last_written_solution_path = target;
    }
    catch (const std::exception& e) {
        std::cerr << "Warning: write_best_solution: Unexpected error while writing backup solution: "
                  << e.what() << ". Keeping previous backup.\n";
    }
    catch (...) {
        std::cerr << "Warning: write_best_solution: Unknown error while writing backup solution. "
                  << "Keeping previous backup.\n";
    }
}


/* -------- Implementations for SimulatedAnnealingLogger --------- */


void SimulatedAnnealingLogger::update_all_current_num_proposals(ProposalAcceptanceStatus const status)
{
    switch (status) {
        case ProposalAcceptanceStatus::Rejected:
            ++current_stats.num_rejected_proposals;
            break;
        case ProposalAcceptanceStatus::AcceptedWithDecrease:
            ++current_stats.num_accepted_proposals;
            ++current_stats.num_accepted_decreasing_proposals;
            break;
        case ProposalAcceptanceStatus::Improved:
            ++current_stats.num_accepted_proposals;
            ++current_stats.num_improving_proposals;
            break;
        case ProposalAcceptanceStatus::BestImproved:
            ++current_stats.num_accepted_proposals;
            ++current_stats.num_improving_proposals;
            ++current_stats.num_best_improvals;
            break;
        default:
            std::cerr << "Warning: Exception in Simulated Annealing: Unhandled enum case: " 
                    + std::to_string(static_cast<int>(status));
            return;
    }
    ++current_stats.num_total_proposals;
}


void SimulatedAnnealingLogger::log_proposal_statistics(double progress, std::ostream *_stream)
{
    auto &out = _stream ? *_stream : stream();
    out << "\n ------ Simulated Annealing progress report ------- \n"
        << "Progress: " << progress << "\n"
        << "Proposal statistic    | absolute numbers   (diff to last progress report)\n" 
        << " #total               | " << current_stats.num_total_proposals << "   (" 
        << (current_stats.num_total_proposals - previously_logged_stats.num_total_proposals) << ")\n"
        << " #accepted            | " << current_stats.num_accepted_proposals << "   ("
        << (current_stats.num_accepted_proposals - previously_logged_stats.num_accepted_proposals) << ")\n"
        << " #improving           | " << current_stats.num_improving_proposals << "   ("
        << (current_stats.num_improving_proposals - previously_logged_stats.num_improving_proposals) << ")\n"
        << " #accepted decreasing | " << current_stats.num_accepted_decreasing_proposals << "   ("
        << (current_stats.num_accepted_decreasing_proposals - previously_logged_stats.num_accepted_decreasing_proposals) << ")\n"
        << " #rejected            | " << current_stats.num_rejected_proposals << "   ("
        << current_stats.num_rejected_proposals - previously_logged_stats.num_rejected_proposals << ")\n"
        << " #best improvals      | " << current_stats.num_best_improvals << "   ("
        << current_stats.num_best_improvals - previously_logged_stats.num_best_improvals << ")\n" << std::flush;

    previously_logged_stats = current_stats;
}


void SimulatedAnnealingLogger::update_cluster_exchange_stats(ClusterExchangeNeighborSelector const &selector, unsigned size_of_proposal, ProposalAcceptanceStatus const status)
{
    switch (status) {
        case ProposalAcceptanceStatus::Rejected:
            ++cluster_exchange_stats.num_rejected_cluster_exchanges;
            cluster_exchange_stats.acc_size_of_rejected_clusters += size_of_proposal;
            break;
        case ProposalAcceptanceStatus::AcceptedWithDecrease:
            ++cluster_exchange_stats.num_accepted_decreasing_cluster_exchanges;
            cluster_exchange_stats.acc_size_of_accepted_decreasing_clusters += size_of_proposal;
            break;
        case ProposalAcceptanceStatus::Improved:
            ++cluster_exchange_stats.num_improving_cluster_exchanges;
            cluster_exchange_stats.acc_size_of_improving_clusters += size_of_proposal;
            break;
        case ProposalAcceptanceStatus::BestImproved:
            ++cluster_exchange_stats.num_improving_cluster_exchanges;
            cluster_exchange_stats.acc_size_of_improving_clusters += size_of_proposal;
            break;
        default:
            std::cerr << "Warning: Exception in Simulated Annealing: Unhandled enum case: " 
                    + std::to_string(static_cast<int>(status));
            return;
    }
    if (selector.get_adding_mode() == AddingMode::Greedy){
        ++cluster_exchange_stats.num_greedy_adding;
    }
}

void SimulatedAnnealingLogger::log_cluster_exchange_statistics(std::ostream *_stream)
{
    auto &out = _stream ? *_stream : stream();

    auto total_num_clusters = 
            cluster_exchange_stats.num_accepted_decreasing_cluster_exchanges 
            + cluster_exchange_stats.num_improving_cluster_exchanges 
            + cluster_exchange_stats.num_rejected_cluster_exchanges;
    auto total_acc_size = 
            cluster_exchange_stats.acc_size_of_rejected_clusters
            + cluster_exchange_stats.acc_size_of_accepted_decreasing_clusters
            + cluster_exchange_stats.acc_size_of_improving_clusters;

    out << "\n ------ Cluster Exchange Statistics ------- \n"
        << "Proposal statistic    | absolute numbers   | average size of proposal \n" 
        << " #total               | " << total_num_clusters << "   | ";
    if (total_num_clusters > 0){
    out << (total_acc_size / total_num_clusters) << "\n"
        << " #improving           | " << cluster_exchange_stats.num_improving_cluster_exchanges << "   |";
        if (cluster_exchange_stats.num_improving_cluster_exchanges > 0) {
            out << (cluster_exchange_stats.acc_size_of_improving_clusters / cluster_exchange_stats.num_improving_cluster_exchanges) << "\n";
        }
        out << " #accepted decreasing | " << cluster_exchange_stats.num_accepted_decreasing_cluster_exchanges << "   |";
        if (cluster_exchange_stats.num_accepted_decreasing_cluster_exchanges > 0){
            out << (cluster_exchange_stats.acc_size_of_accepted_decreasing_clusters / cluster_exchange_stats.num_accepted_decreasing_cluster_exchanges) << "\n";
        }
        out << " #rejected            | " << cluster_exchange_stats.num_rejected_cluster_exchanges << "   |";
        if (cluster_exchange_stats.num_rejected_cluster_exchanges > 0){
            out << (cluster_exchange_stats.acc_size_of_rejected_clusters / cluster_exchange_stats.num_rejected_cluster_exchanges) << "\n";
        }
        out << '\n'
            << "Total number of greedy adding proposals : " << cluster_exchange_stats.num_greedy_adding << '\n'
            << std::flush;
    }
}


void SimulatedAnnealingLogger::log_new_best_solution_value(unsigned value, double progress, std::ostream *_stream) const
{
    auto &out = _stream ? *_stream : stream();
    if (not out) return;
    out << "Progress " << progress << " : New best solution in Simulated Annealing with value " << value << std::endl;
}


void SimulatedAnnealingLogger::log_solution(SA_Solution const &solution, std::ostream *_stream) const
{
    auto &out = _stream ? *_stream : stream();
    if (not out) return;

    out << "\n ------- Final SimulatedAnnealing Solution -------\n"
        << "Solution value : " << solution.num_serviced_polygons << "\n"
        << "Antenna IDs    : \n";
    for (auto const antenna_id : solution.antenna_ids){
        out << antenna_id << " ";
    } 
    out << std::endl;
}


void SimulatedAnnealingLogger::log_parameters(unsigned k, double tau, unsigned seed, std::ostream *_stream) const
{
    auto &out = _stream ? *_stream : stream();
    if (not out) return;

    const int col1 = 30;
    const int col2 = 20;
    const char sep = '|';

    auto hline = [&](){
        out << std::string(col1 + col2 + 3, '-') << "\n";
    };
    auto row = [&](std::string const &name, auto const &value){
        out << sep << std::left  << std::setw(col1) << (" " + name)
            << sep << std::right << std::setw(col2) << value
            << sep << "\n";
    };

    out << "Basic Simulated Annealing parameters:\n";
    hline();
    out << sep << std::left  << std::setw(col1) << " Parameter"
        << sep << std::right << std::setw(col2) << "Value"
        << sep << "\n";
    hline();
    row("k",    k);
    row("tau",  tau);
    row("seed", seed);
    hline();
    out << std::endl;
}

void SimulatedAnnealingLogger::log_parameters(MaxNumAntennasToRemove const &max_num_antennas_to_remove,
                                              BufferSizeTopR const &top_r_buffer_size,
                                              NumSamplesForAdding const &num_samples,
                                              std::ostream *_stream) const
{
    auto &out = _stream ? *_stream : stream();
    if (not out) return;

    const int col1 = 30;
    const int col2 = 20;
    const char sep = '|';

    auto hline = [&](){
        out << std::string(col1 + col2 + 3, '-') << "\n";
    };
    auto row = [&](std::string const &name, unsigned value){
        out << sep << std::left  << std::setw(col1) << (" " + name)
            << sep << std::right << std::setw(col2) << value
            << sep << "\n";
    };

    out << "TopK neighbor selector parameters:\n";
    hline();
    out << sep << std::left  << std::setw(col1) << " Parameter"
        << sep << std::right << std::setw(col2) << "Value"
        << sep << "\n";
    hline();
    row("max_num_antennas_to_remove", max_num_antennas_to_remove.value);
    row("top_r_buffer_size",          top_r_buffer_size.value);
    row("num_samples_for_adding",     num_samples.value);
    hline();
    out << std::endl;
}

void SimulatedAnnealingLogger::log_config(SimulatedAnnealingConfig const &config, std::ostream *_stream) const
{
    auto &out = _stream ? *_stream : stream();
    if (not out) return;

    out << config;
}

