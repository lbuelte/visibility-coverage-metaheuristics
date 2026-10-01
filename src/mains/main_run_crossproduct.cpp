//
// Created by Laura on 7/8/26.
//

#include "RunUtilities.hpp"



/* -------- main -------- */

int main(int argc, char **argv) {
    Config cfg = parse_args(argc, argv);
    if (cfg.show_help) return 0;

    std::cout << "Input path  : " << cfg.input_path  << "\n"
              << "Output path : " << cfg.output_path << "\n";

    Countdown absolute_countdown(cfg.time_limit);

    ParameterSet params = get_parameter_set(cfg.crossproduct);
    if (cfg.crossproduct == 0) {
        params.ks   = {cfg.k};
        params.taus = {cfg.tau};
    }

    process_cross_product_with_per_run_timelimit(cfg, params.ks, params.taus, absolute_countdown);

    return 0;
}
