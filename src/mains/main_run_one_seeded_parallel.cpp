//
// Created by Laura on 7/16/26.
//

#include "RunUtilities.hpp"


/* -------- main -------- */

int main(int argc, char **argv) {
    Config cfg = parse_args(argc, argv);
    if (cfg.show_help) return 0;

    std::cout << "Input path  : " << cfg.input_path  << "\n"
              << "Output path : " << cfg.output_path << "\n";

    Countdown absolute_countdown(cfg.time_limit);

    auto seeds = make_child_seeds(cfg.master_seed, cfg.num_threads);

    /* -------- Instance loading -------- */
    Countdown instance_countdown(absolute_countdown.remaining_ms());
    std::optional<DiscreteCoverageInstance> pre_instance(std::in_place, get_polygons(cfg.input_path));
    initialize_and_prune_candidates(cfg, pre_instance);    
    DiscreteCoverageInstance final_instance(pre_instance.value(), cfg.visualize);
    std::cout << "antennas in final instance: "<< final_instance.get_number_antennas() << std::endl;
    pre_instance.reset();
    long const runtime_instance_ms = instance_countdown.passed_ms();

    omp_set_num_threads(std::min(omp_get_num_procs(), static_cast<int>(cfg.num_threads)));
    #pragma omp parallel for collapse(1) schedule(dynamic)
    for (size_t s = 0; s < seeds.size(); ++s){
        Countdown run_countdown(absolute_countdown.remaining_ms());
        run_one_seeded(cfg, final_instance, cfg.k, cfg.tau, 
                seeds[s], runtime_instance_ms, run_countdown);
    } 

    return 0;
}
