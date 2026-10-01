//
// Created by Laura on 8/4/26.
//

#include "RunUtilities.hpp"

struct RunAllocation {
    unsigned num_threads;
    unsigned num_seeds;
};

RunAllocation calculate_run_allocation(
    size_t num_runs,
    unsigned requested_threads)
{
    if (num_runs == 0) {
        throw std::runtime_error("Cannot allocate runs for empty crossproduct.");
    }
    if (requested_threads == 0) {
        throw std::runtime_error("Requested thread count must be positive.");
    }

    const int available_procs = omp_get_num_procs();
    if (available_procs <= 0) {
        throw std::runtime_error("OpenMP reports no available processors.");
    }
    const unsigned available = static_cast<unsigned>(available_procs);

    const unsigned threads = std::min(requested_threads, available);
    if (threads < num_runs) {
        throw std::runtime_error("Need at least " + std::to_string(num_runs) +
            " threads to execute one complete k-tau crossproduct concurrently, but only " +
            std::to_string(threads) +" are available.");
    }

    const unsigned seeds = threads / static_cast<unsigned>(num_runs);
    if (seeds == 0) {
        throw std::runtime_error("Thread allocation resulted in zero seeds.");
    }

    return {
        .num_threads = seeds * static_cast<unsigned>(num_runs),
        .num_seeds = seeds
    };
}

struct RunFailure {
    size_t seed_index;
    uint32_t seed;
    unsigned k;
    double tau;
    std::string message;
};

inline std::ostream& operator<<(std::ostream& os, const RunFailure& failure)
{
    return os
        << "seed=" << failure.seed
        << ", k=" << failure.k
        << ", tau=" << failure.tau
        << ": " << failure.message;
}

/* -------- main -------- */

int main(int argc, char **argv) 
{
    try {
        // ---------------------------------------------------------------------
        // Configuration
        // ---------------------------------------------------------------------

        Config cfg = parse_final_config(argc, argv);
        if (cfg.show_help) return 0;

        // Reading final parameters from file
        if (not cfg.final_params_path.has_value()){
            throw std::runtime_error(("Calling final main, but no input path to the final parameter .toml was provided." 
                "Please give a valid argument to -f final_params_path"));
        }
        FinalInput final_input = FinalInput::parse_final_input_toml(cfg.final_params_path.value());

        // Updating final parameters in config
        cfg.input_path = final_input.instance_file;
        ParameterSet params = {final_input.k, final_input.tau};

        // ---------------------------------------------------------------------
        // Validate the complete final configuration
        // ---------------------------------------------------------------------

        validate_config(cfg);

        if (params.ks.empty()) {
            throw std::runtime_error("Final input contains no k values.");
        }

        if (params.taus.empty()) {
            throw std::runtime_error("Final input contains no tau values.");
        }

        const size_t num_runs = params.ks.size() * params.taus.size();

        if (num_runs == 0) {
            throw std::runtime_error("Final crossproduct contains no runs.");
        }

        // ---------------------------------------------------------------------
        // Time budget
        // ---------------------------------------------------------------------

        Countdown absolute_countdown(cfg.time_limit);
        if (absolute_countdown.remaining_ms() <= 0) {
            throw std::runtime_error("Final run has no available time.");
        }

        // ---------------------------------------------------------------------
        // Thread / seed allocation
        // ---------------------------------------------------------------------

        const RunAllocation run_allocation = calculate_run_allocation(num_runs, cfg.num_threads);
        

        // ---------------------------------------------------------------------
        // Seeds
        // ---------------------------------------------------------------------

        auto seeds = make_child_seeds(cfg.master_seed, run_allocation.num_seeds);
        if (seeds.size() != run_allocation.num_seeds) {
            throw std::runtime_error(
                "Seed generator returned an unexpected number of seeds.");
        }

        // ---------------------------------------------------------------------
        // Print final configuration before starting expensive work
        // ---------------------------------------------------------------------

        std::cout
            << "========================================\n"
            << "             FINAL RUN\n"
            << "========================================\n"
            << "Input path       : " << cfg.input_path << '\n'
            << "Output path      : " << cfg.output_path << '\n'
            << "Crossproduct     : " << params << '\n'
            << "Crossproduct size: " << num_runs << '\n'
            << "Requested threads: " << cfg.num_threads << '\n'
            << "Actual threads   : " << run_allocation.num_threads << '\n'
            << "Number of seeds  : " << run_allocation.num_seeds << '\n'
            << "Time limit       : "
            << absolute_countdown.remaining_ms() / (1000.0 * 60.0)
            << " min\n"
            << "========================================\n"
            << std::endl;


        // ---------------------------------------------------------------------
        // Instance loading
        // ---------------------------------------------------------------------
        std::cout
            << "========================================\n"
            << "             Instance Loading\n"
            << "========================================\n";

        if (absolute_countdown.remaining_ms() <= 0) {
            throw std::runtime_error("Time limit exhausted before instance loading.");
        }

        Countdown instance_countdown(absolute_countdown.remaining_ms());
        std::optional<DiscreteCoverageInstance> pre_instance(std::in_place, get_polygons(cfg.input_path));
        initialize_and_prune_candidates(cfg, pre_instance);

        if (!pre_instance.has_value()) {
            throw std::runtime_error("Instance preprocessing produced no instance.");
        }

        DiscreteCoverageInstance final_instance(pre_instance.value(), cfg.visualize);
        const long runtime_instance_ms = instance_countdown.passed_ms();
        pre_instance.reset();

        std::cout 
            << "\nInstance preprocessing took "
            << runtime_instance_ms
            << " ms\n"
            << "Antennas in final instance: "
            << final_instance.get_number_antennas()
            << std::endl;

        // ---------------------------------------------------------------------
        // Final runs
        // ---------------------------------------------------------------------

        if (absolute_countdown.remaining_ms() <= 0) {
            throw std::runtime_error("Time limit exhausted before final runs started.");
        }

        std::cout 
            << "\n========================================\n"
            << "           Running Crossproduct\n"
            << "========================================\n"
            << "Remaining time: " << absolute_countdown.remaining_ms() / (1000 * 60) << " minutes.\n" 
            << "Running crossproduct of size " << num_runs
                << " for " << run_allocation.num_seeds << " seeds ...\n"
                << std::endl;

        omp_set_num_threads(run_allocation.num_threads);

        std::vector<RunFailure> failures;
        failures.reserve(num_runs);

#pragma omp parallel for collapse(3) schedule(dynamic)
        for (size_t s = 0; s < seeds.size(); ++s) {
            for (size_t i = 0; i < params.taus.size(); ++i) {
                for (size_t j = 0; j < params.ks.size(); ++j) {

                    const double tau = params.taus[i];
                    const unsigned k = params.ks[j];

                    try {
                        // Set run countdown
                        const double remaining = absolute_countdown.remaining_ms();
                        if (remaining <= 0.0) {
                            #pragma omp critical(run_failures)
                            failures.push_back({s, seeds[s], k, tau, "Skipped: time budget exhausted"});
                            continue;
                        }
                        Countdown run_countdown(remaining);       

                        run_one_seeded(cfg, final_instance, k, tau, seeds[s], runtime_instance_ms, run_countdown);
                    }
                    catch (const std::exception& e) {                    
                        RunFailure failure {
                            .seed_index = s,
                            .seed = seeds[s],
                            .k = k,
                            .tau = tau,
                            .message = e.what()
                        };
                        #pragma omp critical(run_failures)
                        {
                            failures.push_back(std::move(failure));
                        }
                    }
                    catch (...) {
                        RunFailure failure {
                            .seed_index = s,
                            .seed = seeds[s],
                            .k = k,
                            .tau = tau,
                            .message = "Unknown exception"
                        };
                        #pragma omp critical(run_failures)
                        {
                            failures.push_back(std::move(failure));
                        }
                    }
                }
            }
        }

        if (!failures.empty()) {
            std::cerr
                << "\n========================================\n"
                << "             RUN FAILURES\n"
                << "========================================\n";
            for (const auto& failure : failures) {
                std::cerr << failure << '\n';
            }
            std::cerr << "Total failed runs: " << failures.size() << std::endl;
        }

        std::cout
            << "========================================\n"
            << "             FINAL RUN DONE\n"
            << "========================================\n"
            << std::flush;

    }
    catch (const std::exception& e) {
        std::cerr << "FATAL: " << e.what() << std::endl;
        return 1;
    }
    catch (...) {
        std::cerr << "FATAL: unknown exception" << std::endl;
        return 1;
    }

    return 0;
}
