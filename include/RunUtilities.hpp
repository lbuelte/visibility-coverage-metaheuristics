//
// Created by Laura on 7/9/26.
//

#ifndef GISCUPBONN_RUNUTILITIES_HPP
#define GISCUPBONN_RUNUTILITIES_HPP

#include "geometry/Writer.hpp"

#include "utility/ConfigParser.hpp"
#include "utility/TimelimitCountdown.hpp"
#include "utility/solution_analysis.hpp"

#include "discrete/GreedyAlgorithm.hpp"
#include "discrete/LocalSearch.hpp"
#include "discrete/LocalSearchBooster.hpp"
#include "discrete/SimulatedAnnealingAlgorithm.hpp"
#include "discrete/SimulatedAnnealingWithLocalSearchFinish.hpp"


namespace fs = std::filesystem;


/* ----- Run identifier factory ----- */

inline std::string identifier_string_thread_month_day_hour_minute_second_millisecond_random() {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()) % 1000;
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);

    int thread_num = omp_get_thread_num();

    std::tm local_tm;
#ifdef _WIN32
    localtime_s(&local_tm, &now_time);
#else
    localtime_r(&now_time, &local_tm);
#endif
    std::ostringstream oss;
    oss << "p" << thread_num << "_"
        << "t" << std::put_time(&local_tm, "%m_%d_%H_%M_%S")
        << "_" << std::setw(3) << std::setfill('0') << ms.count();

    // Random integer between 1 and 100
    static std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(1, 100);
    oss << "_r_" << dist(rng);

    return oss.str();
}


/* ----- Seed factory ----- */

std::vector<uint32_t> make_child_seeds(uint32_t master_seed, size_t n) {
    std::mt19937_64 gen(master_seed);
    std::uniform_int_distribution<uint32_t> dist(
        0, std::numeric_limits<uint32_t>::max());

    std::vector<uint32_t> seeds;
    seeds.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        seeds.push_back(dist(gen));
    }
    return seeds;
}


/* -------- Algorithm factory -------- */

inline std::shared_ptr<IntervalCoverageSolver> get_algorithm(Config const &cfg, unsigned k, double tau) {
    std::string algo_name = cfg.algo_name;

    if (algo_name == "greedy") {
        return std::make_shared<GreedyAlgorithm>();
    }
    if (algo_name == "greedy-boosted") {
        return std::make_shared<LocalSearchBooster>(std::make_unique<GreedyAlgorithm>());
    }
    if (algo_name == "local-search") {
        return std::make_shared<LocalSearch>(
                cfg.ls_config);
    }
    if (algo_name == "simulated-annealing") {
        SimulatedAnnealingConfig simann_cfg = cfg.simann;
        simann_cfg.set_num_clusters(k);
        simann_cfg.set_output_filepath(k, tau, cfg.input_path);
        return std::make_shared<SimulatedAnnealingAlgorithm>(simann_cfg);
    }
    if (algo_name == "simann-ls-finish") {
        return std::make_shared<SimulatedAnnealingWithLocalSearchFinish>(cfg);
    }

    throw std::runtime_error("Unknown algorithm '" + algo_name + "', please select known algorithm.");
}

inline std::shared_ptr<IntervalCoverageSolver> get_algorithm(Config const &cfg, unsigned k, fs::path const &log_output_path, std::string const &run_identifier) {
    std::string algo_name = cfg.algo_name;

    if (algo_name == "greedy") {
        return std::make_shared<GreedyAlgorithm>();
    }
    if (algo_name == "greedy-boosted") {
        return std::make_shared<LocalSearchBooster>(std::make_unique<GreedyAlgorithm>());
    }
    if (algo_name == "local-search") {
        LocalSearchConfig::LocalSearchConfig ls_config = cfg.ls_config;
        ls_config.set_output_path(run_identifier, log_output_path);
        return std::make_shared<LocalSearch>(
                ls_config);
    }
    if (algo_name == "simulated-annealing") {
        SimulatedAnnealingConfig simann_cfg = cfg.simann;
        simann_cfg.set_output_filepath(run_identifier, log_output_path);
        simann_cfg.set_num_clusters(k);
        return std::make_shared<SimulatedAnnealingAlgorithm>(simann_cfg);
    }
    if (algo_name == "simann-ls-finish") {
        return std::make_shared<SimulatedAnnealingWithLocalSearchFinish>(cfg, log_output_path, run_identifier);
    }

    throw std::runtime_error("Unknown algorithm '" + algo_name + "', please select known algorithm.");
}



/* -------- CSV logging -------- */

inline void log_run_to_csv(fs::path const &csv_path,
                           Config const &cfg,
                           unsigned k, double tau,
                           unsigned run_seed,
                           size_t num_polygons,
                           size_t num_antenna_candidates,
                           long runtime_instance_ms,
                           long runtime_algo_ms,
                           unsigned covered_polygons)
{
    bool const write_header = not fs::exists(csv_path);
    std::ofstream csv(csv_path, std::ios::app);
    if (not csv) {
        throw std::runtime_error("Could not open CSV log file: " + csv_path.string());
    }

    if (write_header) {
        csv << "input_path,output_path,algo_name,k,tau,"
               "polygons,antennas,"
               "runtime_instance_ms,runtime_algo_ms,"
               "time_limit,master_seed,run_seed,"
               "num_bisection_enrichment_candidates,"
               "num_rounds_multi_interval_enrichment,"
               "num_seen_polygons_pruning_threshold,"
               "covered_polygons,all_polygons\n";
    }

    csv << cfg.input_path                              << ","
        << cfg.output_path                             << ","
        << cfg.algo_name                               << ","
        << k                                           << ","
        << tau                                         << ","
        << num_polygons                                << ","
        << num_antenna_candidates                      << ","
        << runtime_instance_ms                         << ","
        << runtime_algo_ms                             << ","
        << cfg.time_limit                              << ","
        << cfg.master_seed                             << ","
        << run_seed                                    << ","
        << cfg.num_bisection_enrichment_candidates     << ","
        << cfg.num_rounds_multi_interval_enrichment    << ","
        << cfg.num_seen_polygons_pruning_threshold     << ","
        << covered_polygons                            << "\n";
}



/* -------- Solution visualization -------- */

inline void visualize_solution(Solution const &solution, fs::path const &output_path, std::string const &solution_identifier)
{
    write_solution(
        solution.get_antenna_and_arrangement_of_solution(),
        solution.get_serviced_polygons_with_coverage(),
        solution.get_unserviced_polygons_with_coverage(),
        output_path / solution_identifier);    
}



/* -------- Parameter sets -------- */

struct ParameterSet {
    std::vector<unsigned> ks;
    std::vector<double> taus;
};

inline ParameterSet get_parameter_set(int crossproduct) {
    switch (crossproduct) {
        case 0: return {};  // filled from cfg in main
        case 1: return {{15, 125, 250},             {0.25, 0.5, 0.75}};
        case 2: return {{50, 500, 1000},            {0.25, 0.5, 0.75}};
        case 3: return {{500, 1000, 5000, 10000},   {0.25, 0.5, 0.75}};
        case 4: return {{500, 1000, 5000},          {0.25, 0.5, 0.75}};
        default: throw std::runtime_error("Unknown crossproduct value: "
                                          + std::to_string(crossproduct));
    }
}

inline std::ostream& operator<<(std::ostream& os, const ParameterSet &params)
{
    os << "k = {";
    for (int i = 0; i < params.ks.size(); ++i){
        os << params.ks[i];
        if (i < params.ks.size() - 1){
            os << ", ";
        }
    }
    os << "}, tau = {";
        for (int i = 0; i < params.taus.size(); ++i){
        os << params.taus[i];
        if (i < params.taus.size() - 1){
            os << ", ";
        }
    }
    os << "}";

    return os;
}


/* -------- Initialize instance --------*/

inline std::vector<PolygonWithInputId> get_polygons(fs::path const &input_path)
{
    std::vector<PolygonWithInputId> polygons;
    // Some instances do not provide polygon ids
    std::string const path_str = input_path.string();
    return load_geojson_polygons(input_path);
}


inline void initialize_and_prune_candidates(Config const &cfg, std::optional<DiscreteCoverageInstance> &pre_instance)
{
    pre_instance->initialize_with_vertex_antenna_candidates();
    pre_instance->add_n_candidates_via_bisection(cfg.num_bisection_enrichment_candidates);
    pre_instance->add_candidates_via_top_r_multi_interval_boundaries(cfg.num_rounds_multi_interval_enrichment);

    pre_instance->prune_relevant_antenna_by_min_seen_polygon(cfg.num_seen_polygons_pruning_threshold);
    std::cout << "antennas in pre instance after pruning: "<< pre_instance->get_relevant_antennas().size() << std::endl;
}



/* -------- Single run -------- */

inline void run_one_seeded(Config const &cfg,
                           DiscreteCoverageInstance const &instance,
                           unsigned k, double tau,
                           unsigned run_seed,
                           long runtime_instance_ms,
                           Countdown &run_countdown)
{
    Countdown algorithm_countdown(run_countdown.remaining_ms());

    // Setting name for run-specific log files and log config
    std::string instance_name = cfg.input_path.stem().string();
    std::string run_identifier = instance_name + "_par_" + std::to_string(k) + "_" + std::to_string(tau)
                                 + "_id_"+ identifier_string_thread_month_day_hour_minute_second_millisecond_random();
    fs::path log_output_path = cfg.output_path / "logs";
    cfg.log(log_output_path / (run_identifier + ".config.log"));

    // Getting algorithm and running
    std::shared_ptr<IntervalCoverageSolver> algorithm = get_algorithm(cfg, k, log_output_path, run_identifier);
    Solution solution = algorithm->run(instance, k, tau, algorithm_countdown.remaining_ms(), run_seed);
    long runtime_algo_ms = algorithm_countdown.passed_ms();

    // Verify and log solution
    bool const correct = instance.verify_solution(
        solution.get_solution_antenna_ids(), solution.get_serviced_polygon_ids(), tau);
    if (correct) {
        std::string instance_name = cfg.input_path.stem().string();
        std::string solution_identifier = run_identifier + "_val_" + std::to_string(solution.get_number_of_serviced_polygons());
        fs::path solution_output_path = cfg.output_path / "solutions";

        SolutionLogger giscup_solution_logger(solution_output_path / (solution_identifier + ".solution.giscup"));
        giscup_solution_logger.log_solution_in_giscup_format(solution);  
        SolutionLogger id_solution_logger(solution_output_path / (solution_identifier + ".solution.ids"));
        id_solution_logger.log_solution_as_antenna_ids_and_serviced_polygons(solution);
    } else {
        std::cerr << "Warning: solution is incorrect for k=" << k << " tau=" << tau << "\n";
    }

    size_t const num_polygons = instance.get_number_polygons();
    size_t const num_antennas = instance.get_number_antennas();

    // Console summary
    std::cout << "\n========== SUMMARY ==========\n"
              << "Input path        : " << cfg.input_path        << "\n"
              << "Output path       : " << cfg.output_path       << "\n"
              << "k                 : " << k                     << "\n"
              << "tau               : " << tau                   << "\n"
              << "Polygons          : " << num_polygons          << "\n"
              << "Runtime instance  : " << runtime_instance_ms   << " ms\n"
              << "Runtime algo      : " << runtime_algo_ms       << " ms\n"
              << "Covered polygons  : " << solution.get_number_of_serviced_polygons()
              << " / "                  << num_polygons          << "\n"
              << "=============================\n";

    // CSV log
    log_run_to_csv(cfg.output_path / "log.csv", cfg, k, tau, run_seed,
                   num_polygons, num_antennas, runtime_instance_ms, runtime_algo_ms,
                   solution.get_number_of_serviced_polygons());

    // Visualization
    if (cfg.visualize) {
        std::string const suffix = std::to_string(num_polygons) + "_"
                                 + std::to_string(k) + "_"
                                 + std::to_string(static_cast<int>(std::floor(tau * 100)));
        write_solution(
            solution.get_antenna_and_arrangement_of_solution(),
            solution.get_serviced_polygons_with_coverage(),
            solution.get_unserviced_polygons_with_coverage(),
            cfg.output_path / ("solution_" + suffix));
    }

    // Solution Analysis
    if (cfg.analyse_solution) {
        std::string const suffix = std::to_string(num_polygons) + "_"
                                 + std::to_string(k) + "_"
                                 + std::to_string(static_cast<int>(std::floor(tau * 100)));

        gis_utility::compute_antenna_necessary_statistics(cfg.output_path / ("solution_analysis_" + suffix), instance, solution);
    }
}


inline void run_one(Config const &cfg,
                    DiscreteCoverageInstance const &instance,
                    unsigned k, double tau,
                    long runtime_instance_ms,
                    Countdown &run_countdown)
{
    run_one_seeded(cfg, instance, k, tau, cfg.master_seed, runtime_instance_ms, run_countdown);
}


/* -------- Cross product -------- */

// The given timelimit is equally distributed across the runs in the crossproduct
inline void process_cross_product_with_total_timelimit(
        Config const &cfg,
        std::vector<unsigned> const &ks,
        std::vector<double> const &taus,
        Countdown &absolute_timelimit)
{
    /* -------- Instance loading -------- */
    Countdown instance_countdown(absolute_timelimit.remaining_ms());
    std::optional<DiscreteCoverageInstance> pre_instance(std::in_place, get_polygons(cfg.input_path));
    initialize_and_prune_candidates(cfg, pre_instance);    
    DiscreteCoverageInstance final_instance(pre_instance.value(), cfg.visualize);
    std::cout << "antennas in final instance: "<< final_instance.get_number_antennas() << std::endl;
    pre_instance.reset();
    long const runtime_instance_ms = instance_countdown.passed_ms();

    /* ------- Timelimit distribution ------- */
    auto num_runs = taus.size() * ks.size();
    auto timelimit_per_run = absolute_timelimit.remaining_ms() / num_runs;

    /* ------- Running cross product ----------*/
    for (double tau : taus) {
        for (unsigned k : ks) {
            Countdown run_countdown(timelimit_per_run);
            run_one(cfg, final_instance, k, tau, runtime_instance_ms, run_countdown);
        }
    }
}


// Every run in the crossproduct gets the given timelimit
inline void process_cross_product_with_per_run_timelimit(
        Config const &cfg,
        std::vector<unsigned> const &ks,
        std::vector<double> const &taus,
        Countdown &absolute_timelimit)
{
    /* -------- Instance loading -------- */
    Countdown instance_countdown(absolute_timelimit.remaining_ms());
    std::optional<DiscreteCoverageInstance> pre_instance(std::in_place, get_polygons(cfg.input_path));
    initialize_and_prune_candidates(cfg, pre_instance);    
    DiscreteCoverageInstance final_instance(pre_instance.value(), cfg.visualize);
    std::cout << "antennas in final instance: "<< final_instance.get_number_antennas() << std::endl;
    pre_instance.reset();
    long const runtime_instance_ms = instance_countdown.passed_ms();
    unsigned timelimit_per_run = absolute_timelimit.remaining_ms();

    /* ------- Running cross product ----------*/
    for (double tau : taus) {
        for (unsigned k : ks) {
            // Every run gets a new Countdown with the same total timelimit
            Countdown run_countdown(absolute_timelimit.remaining_ms());
            run_one(cfg, final_instance, k, tau, runtime_instance_ms, run_countdown);
        }
    }
}


inline void process_cross_product_parallel(Config &cfg,
                                  std::vector<unsigned> const &ks,
                                  std::vector<double> const &taus,
                                  Countdown &absolute_timelimit)
{
    /* -------- Instance loading -------- */
    Countdown instance_countdown(absolute_timelimit.remaining_ms());
    std::optional<DiscreteCoverageInstance> pre_instance(std::in_place, get_polygons(cfg.input_path));
    initialize_and_prune_candidates(cfg, pre_instance);    
    DiscreteCoverageInstance final_instance(pre_instance.value(), cfg.visualize);
    std::cout << "antennas in final instance: "<< final_instance.get_number_antennas() << std::endl;
    pre_instance.reset();
    long const runtime_instance_ms = instance_countdown.passed_ms();

    /* ------- Running cross product in parallel ----------*/
    auto num_runs = taus.size() * ks.size();
    if (omp_get_num_procs() < num_runs) {
        throw std::runtime_error("Not enough cores to run in parallel");
    }

    omp_set_num_threads(std::min(omp_get_num_procs(), static_cast<int>(num_runs)));
    #pragma omp parallel for collapse(2) schedule(dynamic)
    for (size_t i = 0; i < taus.size(); ++i) {
        for (size_t j = 0; j < ks.size(); ++j) {
            double const tau = taus[i];
            unsigned const k = ks[j];

            Countdown run_countdown(absolute_timelimit.remaining_ms());

            run_one(cfg, final_instance, k, tau,
                    runtime_instance_ms, run_countdown);
        }
    }
}





#endif // GISCUPBONN_RUNUTILITIES_HPP

