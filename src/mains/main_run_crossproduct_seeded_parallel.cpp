//
// Created by Laura on 7/22/26.
//

#include "RunUtilities.hpp"

/* -------- main -------- */

int main(int argc, char **argv) {
    Config cfg = parse_args(argc, argv);
    if (cfg.show_help) return 0;

    std::cout << "Input path  : " << cfg.input_path  << "\n"
              << "Output path : " << cfg.output_path << "\n";

    Countdown absolute_countdown(cfg.time_limit);

    /* -------- Instance loading -------- */
    Countdown instance_countdown(absolute_countdown.remaining_ms());
    std::optional<DiscreteCoverageInstance> pre_instance(std::in_place, get_polygons(cfg.input_path));
    initialize_and_prune_candidates(cfg, pre_instance);    
    DiscreteCoverageInstance final_instance(pre_instance.value(), cfg.visualize);
    std::cout << "antennas in final instance: "<< final_instance.get_number_antennas() << std::endl;
    pre_instance.reset();
    long const runtime_instance_ms = instance_countdown.passed_ms();

    /* ---- Set crossproduct parameters ---- */
    ParameterSet params = get_parameter_set(cfg.crossproduct);
    if (cfg.crossproduct == 0) {
        std::cerr << "Warning! Running RunCrossproduct with flag -c 0. Why don't you run RunOne instead?" << std::endl;
    }

    unsigned num_runs = params.taus.size() * params.ks.size();
    if (omp_get_num_procs() < num_runs) {
        throw std::runtime_error("Not enough cores to run in parallel");
    }
    if (cfg.num_threads < num_runs) {
        throw std::runtime_error("The set number of threads is not enough to even run a whole crossporduct, nevermind for multiple seeds!");
    }

    unsigned num_seeds = static_cast<unsigned>(std::floor(
            std::min (omp_get_num_procs(), static_cast<int>(cfg.num_threads))
            / static_cast<double>(num_runs)));
    auto seeds = make_child_seeds(cfg.master_seed, num_seeds);
       
    /* ------- Running cross product in parallel -------- */

    std::cout << "Running crossproduct of size " << num_runs << " for " << num_seeds << " seeds." << std::endl;

    omp_set_num_threads(num_seeds * num_runs);
    #pragma omp parallel for collapse(3) schedule(dynamic)
    for (size_t s = 0; s < seeds.size(); ++s){
        for (size_t i = 0; i < params.taus.size(); ++i) {
            for (size_t j = 0; j < params.ks.size(); ++j) {
                double const tau = params.taus[i];
                unsigned const k = params.ks[j];

                Countdown run_countdown(absolute_countdown.remaining_ms());
                run_one_seeded(cfg, final_instance, k, tau, seeds[s],
                        runtime_instance_ms, run_countdown);
            }
        }
    }

    return 0;
}
