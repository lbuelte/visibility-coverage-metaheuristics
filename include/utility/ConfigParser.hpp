//
// Created by Laura on 7/8/26.
//

#ifndef GISCUPBONN_CONFIGPARSER_HPP
#define GISCUPBONN_CONFIGPARSER_HPP

#include "jarro2783/cxxopts.hpp"

#include "utility/LocalSearchConfig.hpp"
#include "utility/SimulatedAnnealingConfig.hpp"

#include <omp.h>
#include <string>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;


// Lightweight config for visualization of logged solutions
struct VisualizationConfig {

    VisualizationConfig(int argc, char** argv){
        cxxopts::Options options("GIS CUP 2026 solver", "Code to compute good results for the GIS CUP 2026");
        options.add_options()
            ("i,input_path", "Path to the instance file", 
                cxxopts::value<std::string>()->default_value("data/input/small_1.geojson"))
            ("o,output_path,parent_path", "Path to parent of folder logs and folder solutions",
                cxxopts::value<std::string>()->default_value("data/output/debug"))
            ("r,run_identifier", "Unique name (identifier) of .config.log and .solution.id)")
            ("k,num_antennas", "Number of antennas in the solution", 
                cxxopts::value<unsigned>()->default_value("125"))
            ("t,threshold", "Threshold for when a building counts as covered", 
                cxxopts::value<double>()->default_value("0.5"))
            ("h,help", "print help")
            ;
        cxxopts::ParseResult result = options.parse(argc, argv);

        if (result.count("help")){
            std::cout << options.help() << "\n";
        }

        path_to_instance                         = result["input_path"].as<std::string>();
        if (!fs::exists(path_to_instance)){
            throw std::runtime_error(
                "Input file '" + path_to_instance.string() +
                "' does not exist.");
        }
        if (!fs::is_regular_file(path_to_instance)){
            throw std::runtime_error(
                "'" + path_to_instance.string() +
                "' is not a regular file.");
        }

        path_to_logs_and_solutions_parent_folder = result["output_path"].as<std::string>();
        if (fs::exists(path_to_logs_and_solutions_parent_folder)){
            if (!fs::is_directory(path_to_logs_and_solutions_parent_folder))
            {
                throw std::runtime_error(
                    "'" +
                    path_to_logs_and_solutions_parent_folder.string() +
                    "' exists but is not a directory.");
            }
        } else {
            fs::create_directories(
                path_to_logs_and_solutions_parent_folder);
        }

        if (result.count("run_identifier")){
            run_identifier = result["run_identifier"].as<std::string>();
            k              = get_k_from_run_identifier();
            tau            = get_tau_from_run_identifier();
        }
        else {
            k   = result["k"].as<unsigned>();
            tau = result["t"].as<double>();
        }

        if (k == 0) {
            throw std::runtime_error("k must be positive.");
        }

        if (tau < 0.0 || tau > 1.0) {
            throw std::runtime_error("tau must lie in [0,1].");
        }
    }

    fs::path path_to_instance;
    fs::path path_to_logs_and_solutions_parent_folder;
    std::optional<std::string> run_identifier = std::nullopt;

    unsigned k;
    double tau;    

    unsigned get_k_from_run_identifier() const {
        if (!run_identifier)
            throw std::runtime_error("run_identifier not set.");

        std::string s = fs::path(*run_identifier).stem().string();

        auto par_pos = s.rfind("_par_");
        if (par_pos == std::string::npos){
            throw std::runtime_error("Invalid run identifier: missing '_par_'.");
        }

        auto k_begin = par_pos + 5;               // strlen("_par_")
        auto k_end = s.find('_', k_begin);
        if (k_end == std::string::npos){
            throw std::runtime_error("Invalid run identifier: cannot parse k.");
        }

        if (par_pos == std::string::npos || k_end == std::string::npos)
            throw std::runtime_error("Invalid run_identifier.");

        return static_cast<unsigned>(
            std::stoul(s.substr(k_begin, k_end - k_begin)));
    }

    double get_tau_from_run_identifier() const {
        if (!run_identifier)
            throw std::runtime_error("run_identifier not set.");

        std::string s = fs::path(*run_identifier).stem().string();

        auto par_pos = s.rfind("_par_");
        if (par_pos == std::string::npos){
            throw std::runtime_error("Invalid run identifier: missing '_par_'.");
        }

        auto k_end = s.find('_', par_pos + 5);
        if (k_end == std::string::npos){
            throw std::runtime_error("Invalid run identifier: cannot parse k.");
        }

        auto tau_begin = k_end + 1;
        auto tau_end = s.find("_id_", tau_begin);
        if (tau_end == std::string::npos){
            throw std::runtime_error("Invalid run identifier: missing '_id_'.");
        }

        if (par_pos == std::string::npos ||
            k_end == std::string::npos ||
            tau_end == std::string::npos){
            throw std::runtime_error("Invalid run_identifier.");
        }

        return std::stod(s.substr(tau_begin, tau_end - tau_begin));
    }
};


// Input parameters for final runs

struct FinalInput {
    fs::path instance_file;
    std::vector<unsigned> k;
    std::vector<double> tau;

    static FinalInput parse_final_input_toml(fs::path const &final_input_path){
        toml::table tbl;
        try {
            tbl = toml::parse_file(final_input_path.string());
        }
        catch (const toml::parse_error& e) {
            throw std::runtime_error("Could not parse final input TOML '" +
                final_input_path.string() + "': " + std::string(e.what()));
        }

        // ---------------------------------------------------------------------
        // instance_file
        // ---------------------------------------------------------------------

        auto instance_file_node = tbl["instance_file"].value<std::string>();

        if (!instance_file_node) {
            throw std::runtime_error("Final input TOML must contain a string 'instance_file'.");
        }

        fs::path instance_file(*instance_file_node);

        if (instance_file.empty()) {
            throw std::runtime_error("Final input 'instance_file' must not be empty.");
        }

        std::error_code ec;

        if (!fs::exists(instance_file, ec)) {
            if (ec) {
                throw std::runtime_error("Could not check final input instance file '" +
                    instance_file.string() + "': " + ec.message());
            }

            throw std::runtime_error("Instance file specified in final input does not exist: " + 
                instance_file.string());
        }

        if (!fs::is_regular_file(instance_file, ec)) {
            if (ec) {
                throw std::runtime_error("Could not determine type of instance file '" +
                    instance_file.string() + "': " + ec.message());
            }

            throw std::runtime_error("Instance file specified in final input is not a regular file: " + 
                instance_file.string());
        }

        instance_file = fs::weakly_canonical(instance_file, ec);

        if (ec) {
            throw std::runtime_error("Could not resolve instance file path '" +
                instance_file.string() + "': " + ec.message());
        }

        // ---------------------------------------------------------------------
        // k
        // ---------------------------------------------------------------------

        auto const* k_array = tbl["k"].as_array();

        if (!k_array) {
            throw std::runtime_error("Final input TOML must contain an array 'k'.");
        }

        std::vector<unsigned> k;
        k.reserve(k_array->size());

        for (auto const& value : *k_array) {
            auto k_value = value.value<int64_t>();

            if (!k_value) {
                throw std::runtime_error("All values in final input 'k' must be integers.");
            }

            if (*k_value <= 0) {
                throw std::runtime_error("All values in final input 'k' must be positive.");
            }

            k.push_back(static_cast<unsigned>(*k_value));
        }

        if (k.empty()) {
            throw std::runtime_error("Final input 'k' must contain at least one value.");
        }

        // ---------------------------------------------------------------------
        // tau
        // ---------------------------------------------------------------------

        auto const* tau_array = tbl["tau"].as_array();

        if (!tau_array) {
            throw std::runtime_error("Final input TOML must contain an array 'tau'.");
        }

        std::vector<double> tau;
        tau.reserve(tau_array->size());

        for (auto const& value : *tau_array) {
            auto tau_value = value.value<double>();

            if (!tau_value) {
                throw std::runtime_error("All values in final input 'tau' must be floating-point numbers.");
            }

            if (*tau_value <= 0.0) {
                throw std::runtime_error("All values in final input 'tau' must be positive.");
            }

            tau.push_back(*tau_value);
        }

        if (tau.empty()) {
            throw std::runtime_error("Final input 'tau' must contain at least one value.");
        }

        return FinalInput{
            .instance_file = std::move(instance_file),
            .k = std::move(k),
            .tau = std::move(tau)
        };

    }
};


// General-purpose config

struct Config {
    std::optional<fs::path> final_params_path;
    fs::path input_path  = "data/input/small_1.geojson";
    fs::path output_path = "data/output/debug/";
    std::string algo_name;
    unsigned k           = 125;
    double tau           = 0.5;
    unsigned time_limit  = 300000;
    unsigned master_seed = 42;
    unsigned num_threads = 1;

    // Instance initialization parameters
    unsigned num_bisection_enrichment_candidates  = 0;
    unsigned num_rounds_multi_interval_enrichment = 0;
    unsigned num_seen_polygons_pruning_threshold  = 0;

    int crossproduct = 0;
    bool visualize = false;
    bool analyse_solution = false;
    bool show_help = false;

    /* ----- Sub-Configs ----- */
    struct LSConfig {
        double progress_steps;
        unsigned step_iteration;
        unsigned iterations_to_local_optimum;
    };

    SimulatedAnnealingConfig simann;
    LocalSearchConfig::LocalSearchConfig ls_config;

    void log(fs::path const &filepath) const;
};


/* -------- Logging -------- */

inline std::ostream& operator<<(std::ostream& os, const Config& cfg)
{
    os << "================ Config ================\n";

    os << "input_path      : " << cfg.input_path << '\n';
    os << "output_path     : " << cfg.output_path << '\n';
    os << "algo_name       : " << cfg.algo_name << '\n';

    os << "k               : " << cfg.k << '\n';
    os << "tau             : " << cfg.tau << '\n';
    os << "time_limit      : " << cfg.time_limit << '\n';
    os << "master_seed     : " << cfg.master_seed << '\n';
    os << "num_threads     : " << cfg.num_threads << "\n";

    os << "\n--- Initialization ---\n";

    os << "num_bisection_enrichment_candidates  : "
       << cfg.num_bisection_enrichment_candidates << '\n';

    os << "num_rounds_multi_interval_enrichment : "
       << cfg.num_rounds_multi_interval_enrichment << '\n';

    os << "num_seen_polygons_pruning_threshold  : "
       << cfg.num_seen_polygons_pruning_threshold << '\n';

    os << "\n--- Misc ---\n";

    os << "final_params_path : " 
       << (cfg.final_params_path.has_value() ? cfg.final_params_path.value() : "none")
       << '\n';
    os << "crossproduct      : " << cfg.crossproduct << '\n';
    os << "visualize         : " << std::boolalpha << cfg.visualize << '\n';
    os << "show_help         : " << std::boolalpha << cfg.show_help << '\n';

    os << "\n--- Simulated Annealing ---\n";
    os << cfg.simann;

    os << "\n--- Local Search ---\n";
    os << cfg.ls_config;

    os << std::endl;

    return os;
}

inline void Config::log(std::filesystem::path const& filepath) const
{
    namespace fs = std::filesystem;

    try
    {
        // Create parent directories if necessary
        if (filepath.has_parent_path()){
            fs::create_directories(filepath.parent_path());
        }

        std::ofstream out(filepath, std::ios::out | std::ios::trunc);

        if (!out.is_open()){
            throw std::runtime_error("Could not open log file '" +
                filepath.string() + "'");
        }

        out.exceptions(std::ios::failbit | std::ios::badbit);

        out << *this;
        out.flush();

        if (!out.good()){
            throw std::runtime_error("Error while writing to log file '" +
                filepath.string() + "'");
        }
    }
    catch (std::exception const& e)
    {
        throw std::runtime_error("Config logging failed for file '" +
            filepath.string() + "': " + e.what());
    }
}


/* -------- Validation -------- */

inline void validate_config(Config const &cfg) 
{
    // Input file
    if (not fs::exists(cfg.input_path)) {
        throw std::runtime_error("Input file does not exist: " + cfg.input_path.string());
    }
    if (not fs::is_regular_file(cfg.input_path)) {
        throw std::runtime_error("Input path is not a file: " + cfg.input_path.string());
    }

    // Parameters
    if (cfg.k == 0) {
        throw std::runtime_error("k must be greater than 0.");
    }
    if (cfg.tau <= 0.0 or cfg.tau > 1.0) {
        throw std::runtime_error("tau must be in (0, 1], got: " + std::to_string(cfg.tau));
    }
    if (cfg.time_limit == 0) {
        throw std::runtime_error("time_limit must be greater than 0.");
    }
}

inline fs::path validate_and_get_final_input_path(
    std::string const& final_input_path)
{
    if (final_input_path.empty()) {
        throw std::runtime_error("Final input path must not be empty.");
    }

    const fs::path path(final_input_path);

    std::error_code ec;

    if (!fs::exists(path, ec)) {
        if (ec) {
            throw std::runtime_error("Could not check final input file '" +
                    path.string() + "': " + ec.message());
        }

        throw std::runtime_error("Final input file does not exist: " + path.string());
    }

    if (!fs::is_regular_file(path, ec)) {
        if (ec) {
            throw std::runtime_error("Could not determine type of final input '" +
                    path.string() + "': " + ec.message());
        }

        throw std::runtime_error("Final input path is not a regular file: " + path.string());
    }

    ec.clear();
    const fs::path canonical_path = fs::weakly_canonical(path, ec);

    if (ec) {
        throw std::runtime_error("Could not resolve final input path '" +
                path.string() + "': " + ec.message());
    }

    return canonical_path;
}

/* -------- Parsing -------- */


inline void prepare_output_path(fs::path const &output_path) {
    std::error_code ec;
    fs::create_directories(output_path, ec);
    if (ec) {
        throw std::runtime_error("Could not create output directory '"
            + output_path.string() + "': " + ec.message());
    }
    if (not fs::is_directory(output_path)) {
        throw std::runtime_error("Output path exists but is not a directory: "
            + output_path.string());
    }
}


std::chrono::system_clock::time_point parse_deadline_local(const std::string& s)
{
    std::tm tm{};
    // Expects "YYYY-MM-DD HH:MM:SS" in local (CEST) time
    if (strptime(s.c_str(), "%Y-%m-%d %H:%M:%S", &tm) == nullptr) {
        throw std::runtime_error("Failed to parse deadline: " + s);
    }
    tm.tm_isdst = -1;              // let the system figure out DST
    const std::time_t epoch_seconds = mktime(&tm);  // interprets tm as LOCAL time
    if (epoch_seconds == -1) {
        throw std::runtime_error("mktime failed to convert deadline: " + s);
    }
    return std::chrono::system_clock::from_time_t(epoch_seconds);
}


// cfg.deadline is a wall-clock time_point, read from config/CLI
inline double ms_until_deadline(const std::chrono::system_clock::time_point& deadline)
{
    const auto now = std::chrono::system_clock::now();
    const auto remaining = deadline - now;
    const double ms = std::chrono::duration<double, std::milli>(remaining).count();
    return std::max(0.0, ms);
}


inline Config parse_final_config(int argc, char** argv) 
{
    cxxopts::Options options("GIS CUP 2026 solver", "Code to compute good results for the GIS CUP 2026");
    options.add_options()
        ("f,final,final_params,final_params_file", "Path to toml file with final paramters",
            cxxopts::value<std::string>())
        ("o,output_path", "Path to output file", 
            cxxopts::value<std::string>()->default_value("data/output/final/"))
        ("a,algo", "An algorithm chosen from greedy, greedy-boosted, local-search, simulated-annealing, simann-ls-finish", 
            cxxopts::value<std::string>())
        ("d,deadline,time-limit-clock", "deadline in format '2026-08-16 16:00:00'",
            cxxopts::value<std::string>())
        ("s,seed,master_seed", "global seed", 
            cxxopts::value<uint32_t>()->default_value("42"))
        ("p,threads,num_threads", "Number of threads for parallel seeded runs of one k-tau-configuration", 
            cxxopts::value<unsigned>()->default_value("1"))
        ("v,visualize", "Write solution files", 
            cxxopts::value<bool>()->default_value("0"))
        ("analyse-solution", "should the solution be analysed further", 
            cxxopts::value<bool>()->default_value("0"))        
        ("h,help", "print help")
        ("ls-progress-steps", "progress step size for naive local search, should be in (0, 1] and ideally cleanly divide 1", 
            cxxopts::value<double>()->default_value("0.1"))
        ("ls-step-iterations", "local search iterations per step in naive local search", 
            cxxopts::value<unsigned>()->default_value("3"))
        ("ls-iterations-to-local-optimum", "iterations before giving up on finding a local optimum in naive local search. Default is set so high that a local optimum should always be found (unless timeout)", 
            cxxopts::value<unsigned>()->default_value("1000000"))
        ("bisection-candidates", "number of bisection enrichment candidates",
           cxxopts::value<unsigned>()->default_value("0"))
        ("multi-interval-rounds", "number of rounds of multi-interval enrichment",
           cxxopts::value<unsigned>()->default_value("0"))
        ("polygon-pruning-threshold", "threshold for number of seen polygons pruning",
           cxxopts::value<unsigned>()->default_value("0"))
        ("sa-f,sa-config,simann-config", "file path to simualted annealing config.toml file",
            cxxopts::value<std::string>()->default_value("configs/simann_base_config.toml"))
        ("ls-f,ls-config,local-search-config", "file path to simualted annealing config.toml file",
            cxxopts::value<std::string>()->default_value("configs/local_search_config.toml"))
        ;
    cxxopts::ParseResult result = options.parse(argc, argv);

    Config cfg;
    cfg.show_help = result.count("help");
    if (cfg.show_help) {
        std::cout << options.help() << "\n";
        return cfg;
    }

    if (result.count("final")){
        cfg.final_params_path = validate_and_get_final_input_path(result["final"].as<std::string>());
    }

    cfg.output_path  = result["output_path"].as<std::string>();
    cfg.algo_name    = result["algo"].as<std::string>();
    cfg.time_limit   = ms_until_deadline(parse_deadline_local(result["deadline"].as<std::string>()));
    cfg.master_seed  = result["master_seed"].as<uint32_t>();
    cfg.num_threads  = result["threads"].as<unsigned>();
    cfg.visualize    = result["visualize"].as<bool>();
    cfg.analyse_solution = result["analyse-solution"].as<bool>();
    cfg.num_bisection_enrichment_candidates  = result["bisection-candidates"].as<unsigned>();
    cfg.num_rounds_multi_interval_enrichment = result["multi-interval-rounds"].as<unsigned>();
    cfg.num_seen_polygons_pruning_threshold  = result["polygon-pruning-threshold"].as<unsigned>();

    cfg.simann = parse_simulated_annealing_config(result["sa-config"].as<std::string>());
    cfg.simann.set_num_clusters(cfg.k);
    cfg.ls_config = LocalSearchConfig::parse_local_search_config(result["ls-config"].as<std::string>());

    prepare_output_path(cfg.output_path);

    // TODO This is a very bad spot to set this, but currently the only one where I can easily set it for all mains
    // TODO Should be moved somewhere else
    omp_set_max_active_levels(2);

    return cfg;
}



inline Config parse_args(int argc, char** argv) 
{
    cxxopts::Options options("GIS CUP 2026 solver", "Code to compute good results for the GIS CUP 2026");
    options.add_options()
        ("i,input_path", "Path to the instance file", 
            cxxopts::value<std::string>()->default_value("data/input/GIS-cup-sample-dataset.geojson"))
        ("o,output_path", "Path to output file", 
            cxxopts::value<std::string>()->default_value("data/output/debug/"))
        ("a,algo", "An algorithm chosen from greedy, greedy-boosted, local-search, simulated-annealing, simann-ls-finish", 
            cxxopts::value<std::string>())
        ("k,num_antennas", "Number of antennas in the solution", 
            cxxopts::value<unsigned>()->default_value("100"))
        ("t,threshold", "Threshold for when a building counts as covered", 
            cxxopts::value<double>()->default_value("0.5"))
        ("z,time-limit", "time limit in minutes", 
            cxxopts::value<unsigned>()->default_value("5"))
        ("s,seed,master_seed", "global seed", 
            cxxopts::value<uint32_t>()->default_value("42"))
        ("v,visualize", "Write solution files", 
            cxxopts::value<bool>()->default_value("0"))
        ("analyse-solution", "should the solution be analysed further", 
            cxxopts::value<bool>()->default_value("0"))
        ("c,crossproduct", "Run predefined parameter set\n 0 = do not do a crossproduct\n 1 = run {15, 125, 250} x {0.25, 0.5, 0.75}\n 2 = run {50, 500, 1000} x {0.25, 0.5, 0.75}\n 3 = run {50, 500, 1000, 5000, 10000} x {0.25, 0.5, 0.75}", 
            cxxopts::value<int>()->default_value("1"))
        ("p,threads,num_threads", "Number of threads for parallel seeded runs of one k-tau-configuration", 
            cxxopts::value<unsigned>()->default_value("1"))
        ("h,help", "print help")
        ("ls-progress-steps", "progress step size for naive local search, should be in (0, 1] and ideally cleanly divide 1", 
            cxxopts::value<double>()->default_value("0.1"))
        ("ls-step-iterations", "local search iterations per step in naive local search", 
            cxxopts::value<unsigned>()->default_value("3"))
        ("ls-iterations-to-local-optimum", "iterations before giving up on finding a local optimum in naive local search. Default is set so high that a local optimum should always be found (unless timeout)", 
            cxxopts::value<unsigned>()->default_value("1000000"))
        ("bisection-candidates", "number of bisection enrichment candidates",
           cxxopts::value<unsigned>()->default_value("0"))
        ("multi-interval-rounds", "number of rounds of multi-interval enrichment",
           cxxopts::value<unsigned>()->default_value("0"))
        ("polygon-pruning-threshold", "threshold for number of seen polygons pruning",
           cxxopts::value<unsigned>()->default_value("0"))
        ("sa-f,sa-config,simann-config", "file path to simualted annealing config.toml file",
            cxxopts::value<std::string>()->default_value("configs/simann_base_config.toml"))
        ("ls-f,ls-config,local-search-config", "file path to simualted annealing config.toml file",
            cxxopts::value<std::string>()->default_value("configs/local_search_config.toml"))
        ;
    cxxopts::ParseResult result = options.parse(argc, argv);

    Config cfg;
    cfg.show_help = result.count("help");
    if (cfg.show_help) {
        std::cout << options.help() << "\n";
        return cfg;
    }

    cfg.input_path   = result["input_path"].as<std::string>();
    cfg.output_path  = result["output_path"].as<std::string>();
    cfg.algo_name    = result["algo"].as<std::string>();
    cfg.k            = result["k"].as<unsigned>();
    cfg.tau          = result["threshold"].as<double>();
    cfg.time_limit   = result["time-limit"].as<unsigned>() * 60000;
    cfg.master_seed  = result["master_seed"].as<uint32_t>();
    cfg.num_threads  = result["threads"].as<unsigned>();
    cfg.visualize    = result["visualize"].as<bool>();
    cfg.analyse_solution = result["analyse-solution"].as<bool>();
    cfg.crossproduct = result["crossproduct"].as<int>();
    cfg.num_bisection_enrichment_candidates  = result["bisection-candidates"].as<unsigned>();
    cfg.num_rounds_multi_interval_enrichment = result["multi-interval-rounds"].as<unsigned>();
    cfg.num_seen_polygons_pruning_threshold  = result["polygon-pruning-threshold"].as<unsigned>();

    // Set sub-configs. TODOCould also be done from a toml or something.
    cfg.simann = parse_simulated_annealing_config(result["sa-config"].as<std::string>());
    cfg.simann.set_num_clusters(cfg.k);
    cfg.ls_config = LocalSearchConfig::parse_local_search_config(result["ls-config"].as<std::string>());

    validate_config(cfg);
    prepare_output_path(cfg.output_path);

    // TODO This is a very bad spot to set this, but currently the only one where I can easily set it for all mains
    // TODO Should be moved somewhere else
    omp_set_max_active_levels(2);

    return cfg;
}


#endif // GISCUPBONN_CONFIGPARSER_HPP
