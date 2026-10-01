#include "DiscreteCoverageInstance.hpp"
#include "GreedyAlgorithm.hpp"
#include <filesystem>
#include <iostream>
#include <random>
#include <vector>

#include "discrete/LocalSearch.hpp"


Solution LocalSearch::LocalSearchImpl::run(const DiscreteCoverageInstance &instance, unsigned k, double tau, unsigned timelimit_in_ms, unsigned seed, std::vector<unsigned> const &partial_solution) 
{
    std::filesystem::path log_output_path = (config.ls_log_path.string() + ".ls.log");
    logger.set_default_stream(log_output_path);

    setup(instance, k, tau, timelimit_in_ms, seed);

    std::uniform_real_distribution<float> prob_machine(0.0, 1.0);

    std::vector<unsigned> best_solution_so_far;
    double best_objective_so_far = -1.0;

    DCNS2 data_structure = start_with_random_solution(instance, partial_solution);

    unsigned steps_since_improvement = 0;

    // Set up stuff for breakout strategies
    std::uniform_int_distribution<unsigned> randomization_dist(k * config.randomization_config.min_removed_percentage, k * config.randomization_config.max_removed_percentage);
    min_tabu_duration = static_cast<double>(num_antennas) * config.tabu_config.min_tabu_percentage;
    max_tabu_duration = static_cast<double>(num_antennas) * config.tabu_config.max_tabu_percentage;
    unsigned num_clusters_high = static_cast<double>(k) * config.cluster_removal_config.frac_of_antennas_clusters;
    unsigned num_clusters_high_removed = static_cast<double>(num_clusters_high) * config.cluster_removal_config.frac_of_clusters_removed;
    GreedyAlgorithm cluster_replacement_algorithm;

    while (within_time()) {
        bool improved = true;
        double max_progress_to_escape = -1.0;

        set_progress_to_lexicographic();

        while (improved && within_time()) {
            max_progress_to_escape = iterate_one_opt(instance, data_structure, 1000000);
            improved = one_step_two_opt(instance, data_structure, config.num_2_opt_removal_candidates, config.num_2_opt_insertion_candidates);
            if (improved)
                logger.increment_2_opt_improvements();
        }

        steps_since_improvement++;

        logger.log("Currently ", objective(data_structure.getCoverage()), " of ", num_polygons, " are covered");
        if (data_structure.getCoverage().serviced > best_objective_so_far) {
            best_objective_so_far = data_structure.getCoverage().serviced;
            best_solution_so_far = data_structure.getSelectedAntennas();
            steps_since_improvement = 0;
            logger.log_solution(best_solution_so_far, best_objective_so_far);
            logger.inform_new_best_solution();
            write_best_solution_atomic(best_solution_so_far, instance, 
                    config.ls_log_path.string() + "_val_" + std::to_string(static_cast<int>(best_objective_so_far)) + ".ls.sol");
        }

        if (steps_since_improvement < config.iterations_without_improvement) {
            double breakout_strategy = prob_machine(rng);

            if (breakout_strategy < config.prob_objective_change) {
                if (max_progress_to_escape > 0.0) {
                    logger.log("Breakout by objective change");
                    double breakout_progress = std::max(max_progress_to_escape - config.objective_change_config.interpolation_width * prob_machine(rng), 0.0);
                    logger.log("breakout progress: ", breakout_progress, " and max_progress_to_escape ", max_progress_to_escape, "");
                    breakout_by_objective_change(instance, data_structure, breakout_progress, config.objective_change_config.interpolation_step_size, config.objective_change_config.num_iterations_per_step);
                    logger.set_last_breakout_strategy(LocalSearchLogging::OBJECTIVE_CHANGE);
                }
                else {
                    logger.log("Breakout by randomisation");
                    breakout_by_random_replacement(instance, data_structure, randomization_dist(rng));
                    logger.set_last_breakout_strategy(LocalSearchLogging::PARTIAL_RANDOMIZATION);
                }
            }
            else if (breakout_strategy < config.prob_objective_change + config.prob_cluster_removal) {
                logger.log("Breakout by cluster removal");
                if (prob_machine(rng) < 0.5) {
                    logger.log("Using ", config.cluster_removal_config.num_clusters, " big clusters");
                    breakout_by_removing_clusters(instance, data_structure, config.cluster_removal_config.num_clusters, 1, cluster_replacement_algorithm);
                    logger.set_last_breakout_strategy(LocalSearchLogging::CLUSTER_BIG);
                }
                else {
                    logger.log("Using ", config.cluster_removal_config.frac_of_antennas_clusters, "% of k clusters");
                    breakout_by_removing_clusters(instance, data_structure, num_clusters_high, num_clusters_high_removed, cluster_replacement_algorithm);
                    logger.set_last_breakout_strategy(LocalSearchLogging::CLUSTER_SMALL);
                }
            }
            else if (breakout_strategy < config.prob_objective_change + config.prob_cluster_removal + config.prob_tabu_search) {
                logger.log("Breakout by tabu search");
                breakout_by_one_opt_tabu(instance, data_structure, num_tabu_steps);
                logger.set_last_breakout_strategy(LocalSearchLogging::TABU);
            }
            else {
                logger.log("Breakout by randomization");
                breakout_by_random_replacement(instance, data_structure, randomization_dist(rng));
                logger.set_last_breakout_strategy(LocalSearchLogging::PARTIAL_RANDOMIZATION);
            }
        }
        else {
            logger.log("\n********************\nComplete reset\n********************\n");
            breakout_by_random_replacement(instance, data_structure, k);
            logger.set_last_breakout_strategy(LocalSearchLogging::FULL_RESET);
            logger.increment_num_full_resets();
            steps_since_improvement = 0;
        }
    }

    logger.log("\n\n****************************************\n*\n* Run Finished\n*\n****************************************\n");

    logger.log("%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%");
    logger.log("% Config");
    logger.log("%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%");
    logger.log(config);

    logger.log("%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%");
    logger.log("% Stats");
    logger.log("%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%");

    logger.log_local_search_stats();

    logger.log("%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%");
    logger.log("% Best Solution Found");
    logger.log("%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%");

    logger.log_solution(best_solution_so_far, best_objective_so_far);

    std::cout << "1-Opt steps tested    : " << num_one_opt_steps_tested << "\n"
              << "1-Opt steps improving : " << num_one_opt_steps_improving << std::endl;

    return Solution(instance, k, tau, best_solution_so_far);
}

void LocalSearch::LocalSearchImpl::write_best_solution(
    std::ostream &stream,
    const std::vector<unsigned> &antenna_ids,
    const DiscreteCoverageInstance &instance) const
{
    if (not stream) return;

    stream << std::setprecision(std::numeric_limits<double>::max_digits10);

    stream << "(" << tau << ", " << k << ")\n";

    for (std::size_t i = 0; i < antenna_ids.size(); ++i) {
        const auto& pos = instance.get_antenna_data(antenna_ids[i]).position;
        stream << "(" << pos.x() << "," << pos.y() << ")";
        if (i < antenna_ids.size() - 1) {
            stream << ",";
        }
    }
    stream << '\n';

    auto const serviced_polygons = instance.evaluate_antenna_vector(antenna_ids, tau);
    for (std::size_t i = 0; i < serviced_polygons.size(); ++i) {
        stream << instance.get_original_id_for_polygon(serviced_polygons[i]);
        if (i < serviced_polygons.size() - 1) {
            stream << ",";
        }
    }
    stream << '\n';
}

void LocalSearch::LocalSearchImpl::write_best_solution_atomic(
    const std::vector<unsigned> &antenna_ids,
    const DiscreteCoverageInstance &instance,
    const std::filesystem::path &path) const
{
    namespace fs = std::filesystem;
    try {
        fs::path target = path;

        if (target.has_parent_path()) {
            std::error_code ec;
            fs::create_directories(target.parent_path(), ec);
            if (ec) {
                std::cerr << "Warning: LocalSearch::write_best_solution: Could not create directories for path '"
                          << target.string() << "': " << ec.message() << '\n';
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
            std::cerr << "Warning: LocalSearch::write_best_solution: Could not open temp file '"
                      << temp.string() << "' for writing. Skipping this backup.\n";
            return;
        }
        write_best_solution(stream, antenna_ids, instance);
        stream.flush();
        if (stream.fail()) {
            std::error_code ec;
            fs::remove(temp, ec);
            std::cerr << "Warning: LocalSearch::write_best_solution: Write error occurred on temp file '"
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
            std::cerr << "Warning: LocalSearch::write_best_solution: Could not rename '"
                      << temp.string() << "' to '" << target.string() << "': "
                      << rename_ec.message() << ". Skipping this backup.\n";
            return;
        }

        // Only now delete the previous best-value file, since the new one
        // is confirmed safely in place.
        if (last_written_solution_path.has_value() and *last_written_solution_path != target) {
            std::error_code remove_ec;
            fs::remove(*last_written_solution_path, remove_ec);
        }

        last_written_solution_path = target;
    }
    catch (const std::exception& e) {
        std::cerr << "Warning: LocalSearch::write_best_solution: Unexpected error while writing backup solution: "
                  << e.what() << ". Keeping previous backup.\n";
    }
    catch (...) {
        std::cerr << "Warning: LocalSearch::write_best_solution: Unknown error while writing backup solution. "
                  << "Keeping previous backup.\n";
    }
}

Solution ImproveWithOneOpt::ImproveWithOneOptImpl::run(const DiscreteCoverageInstance &instance, unsigned p_k, double p_tau, unsigned timelimit_in_ms, unsigned seed, std::vector<unsigned> const &partial_solution) {
    setup(instance, p_k, p_tau, timelimit_in_ms, seed);
    
    DCNS2 data_structure = start_with_random_solution(instance, partial_solution);

    iterate_one_opt(instance, data_structure, num_steps);

    return Solution(instance, k, tau, data_structure.getSelectedAntennas());
} 
