//
// Created by Laura on 7/9/26.
//

#ifndef GISCUPBONN_SIMULATEDANNEALINGCONFIG_HPP
#define GISCUPBONN_SIMULATEDANNEALINGCONFIG_HPP

#include <filesystem>
#include <stdexcept>
#include <string>
#include <cassert>
#include <iostream>

#include <toml.hpp>


/* -------- RandomTopKNeighborSelector Parameters --------- */


// Introducing wrapper structs (unsigned) to uniquely distinguish parameter types
template<typename Derived>
struct UnsignedWrapper {
    unsigned value = 1;

    Derived operator+(Derived const &rhs) const { return {value + rhs.value}; }
    Derived operator*(double s) const { return {static_cast<unsigned>(round(value * s))}; }
    friend Derived operator*(double s, Derived const &rhs) { return rhs * s; }
};

template<typename Derived>
std::ostream& operator<<(std::ostream& os, const UnsignedWrapper<Derived>& wrapper) {
    return os << wrapper.value;
}

struct MaxNumAntennasToRemove : UnsignedWrapper<MaxNumAntennasToRemove> {};
struct BufferSizeTopR         : UnsignedWrapper<BufferSizeTopR> {};
struct NumSamplesForAdding    : UnsignedWrapper<NumSamplesForAdding> {};

// Pairs of Init and Final values 
template<typename T>
struct InitFinal {
    T init;
    T last;

    // Both real_progress and cutoff should be a number between 0 and 1
    T get_interpolated_value(double real_progress, double interpolation_cutoff) const {
        assert(0 <= real_progress and real_progress <=1);
        assert(0 <= interpolation_cutoff and interpolation_cutoff <= 1);
        if (real_progress >= interpolation_cutoff){
            return last;
        }
        auto progress = real_progress / interpolation_cutoff;
        return  (1 - progress) * init + progress * last;
    }
};

template<typename T>
std::ostream& operator<<(std::ostream& os, const InitFinal<T>& schedule) {
    os << "{ init=" << schedule.init
       << ", last=" << schedule.last
       << " }";

    return os;
}

using MaxNumAntennasToRemoveSchedule = InitFinal<MaxNumAntennasToRemove>;
using BufferSizeTopRSchedule = InitFinal<BufferSizeTopR>;
using NumSamplesForAddingSchedule = InitFinal<NumSamplesForAdding>;



/* -------- ClusterExchangeNeighborSelector Parameters --------- */

enum class RemovalMode {
    ClusterOfRandomAntennaID,
    RandomCluster,
    LargestCluster,
    SmallestClusters
};

inline std::ostream &operator<<(std::ostream &os, RemovalMode mode) {
    switch (mode) {
        case RemovalMode::ClusterOfRandomAntennaID: return os << "ClusterOfRandomAntennaID";
        case RemovalMode::RandomCluster:            return os << "RandomCluster";
        case RemovalMode::LargestCluster:           return os << "LargestCluster";
        case RemovalMode::SmallestClusters:         return os << "SmallestClusters";
        default: throw std::runtime_error("Unknown RemovalMode in operator<<");
    }
}

enum class AddingMode {
    Greedy,
    RandomTopKSampling,
    BestSampling,
    Random
};

inline std::ostream& operator<<(std::ostream& os, AddingMode mode) {
    switch (mode)
    {
        case AddingMode::Greedy:             return os << "Greedy";
        case AddingMode::RandomTopKSampling: return os << "RandomTopKSampling";
        case AddingMode::BestSampling:       return os << "BestSampling";
        case AddingMode::Random:             return os << "Random";
        default: throw std::runtime_error("Unknown AddingMode in operator<<");
    }
}


/* -------- SimulatedAnnealing Parameters --------- */

struct SimAnnParamsForTopKNeighborSelector {
    MaxNumAntennasToRemoveSchedule max_num_antennas_to_remove{ 
        {20}, {2}
    }; 
    BufferSizeTopRSchedule buffer_size_top_r{
        {10}, {1}
    };
    NumSamplesForAddingSchedule num_samples_for_adding{
        {1000}, {10000}
    };
};

inline std::ostream& operator<<(std::ostream& os, const SimAnnParamsForTopKNeighborSelector& params) {
    os << "Top-K Neighbor Selector Parameters\n";
    os << "----------------------------------\n";

    os << "max_num_antennas_to_remove : "
       << params.max_num_antennas_to_remove << '\n';

    os << "buffer_size_top_r          : "
       << params.buffer_size_top_r << '\n';

    os << "num_samples_for_adding     : "
       << params.num_samples_for_adding << '\n';

    return os;
}


enum class SimAnnInitialSolution {
    Greedy,
    Random
};

inline std::ostream& operator<<(std::ostream& os, SimAnnInitialSolution sol) {
    switch (sol)
    {
        case SimAnnInitialSolution::Greedy : return os << "Greedy";
        case SimAnnInitialSolution::Random : return os << "Random";
        default: throw std::runtime_error("Unknown SimAnnInitialSolution in operator<<");
    }
}

/* -------- SimulatedAnnealing Config --------- */

struct SimulatedAnnealingConfig {
    std::filesystem::path simann_output_path = "data/output/simann";
    SimAnnInitialSolution initial_sol = SimAnnInitialSolution::Random;

    // Reheating parameters
    double min_reheat_start_progress = 0.7;
    double max_reheat_start_progress = 0.9;
    unsigned stagnation_threshold_seconds = 10;
    double reheat_amount = 0.0;

    // TopK Neighbor Selector params
    SimAnnParamsForTopKNeighborSelector simann_topk_params{};
    double interpolation_cutoff_progress = 0.9;

    // Cluster Exchange Neighbor Selector params
    double probability_for_cluster_exchange = 0.005;
    unsigned num_clusters = 10;
    RemovalMode removal_mode =  RemovalMode::SmallestClusters;
    AddingMode adding_mode   =  AddingMode::RandomTopKSampling;
    double probability_for_progress_weighted_greedy_adding = 0.25;

    void set_output_filepath(unsigned k, double tau, std::filesystem::path const &input_file){
        std::string input = input_file.string() == "data/input/GIS-cup-sample-dataset.geojson" ? "normal" : "large";
        simann_output_path = simann_output_path / 
                (std::to_string(k) + "_" + std::to_string(tau) + "_" + input + "_" + std::to_string(probability_for_cluster_exchange));
    }
    void set_output_filepath(std::string const &run_identifier, std::optional<std::filesystem::path> const &log_path = std::nullopt){
        if (log_path.has_value()){
            simann_output_path = log_path.value() / run_identifier;
        }
        else {
            simann_output_path = simann_output_path / run_identifier;
        }
    }
    
    void set_num_clusters(unsigned num_antennas){
        num_clusters = std::min(std::max(10u, static_cast<unsigned>(std::floor(num_antennas / 20))), 50u);
    }
};

inline std::ostream& operator<<(std::ostream& os, const SimulatedAnnealingConfig& cfg)
{
    os << "========================================\n";
    os << "Simulated Annealing Configuration\n";
    os << "========================================\n";

    os << "simann_output_path                : "
       << cfg.simann_output_path << '\n';

    os << "initial_solution                  : "
       << cfg.initial_sol << '\n';

    os << "\n---- Reheating parameters ----\n";

    os << "reheat_amount                     : "
       << cfg.reheat_amount << '\n';

    os << "min_reheat_start_progress         : "
       << cfg.min_reheat_start_progress << '\n';

    os << "max_reheat_start_progress         : "        
       << cfg.max_reheat_start_progress << '\n';

    os << "stagnation_threshold              : "
       << cfg.stagnation_threshold_seconds << '\n';

    os << "\n---- Cluster Exchange parameters ----\n";

    os << "probability_for_cluster_exchange  : "
       << cfg.probability_for_cluster_exchange << '\n';

    os << "num_clusters                      : "
       << cfg.num_clusters << '\n';

    os << "removal_mode                      : "
       << cfg.removal_mode << '\n';

    os << "adding_mode                       : "
       << cfg.adding_mode << '\n';

    os << "probability for greedy adding\n" 
       << "(weighted by progress)            : "
       << cfg.probability_for_progress_weighted_greedy_adding << '\n';

    os << '\n';

    os << "interpolation_cutoff_progress     : "
       << cfg.interpolation_cutoff_progress << '\n';

    os << cfg.simann_topk_params
       << '\n';

    return os;
}



///////////////////////////////////////////////////////

/* -------- SimulatedAnnealingConfig Parser -------- */

namespace {

template<typename T>
T get_required(const toml::table& tbl, const std::string& key)
{
    auto value = tbl[key].value<T>();

    if (!value) {
        throw std::runtime_error(
            "Missing or invalid value for key '" + key + "'");
    }

    return *value;
}

template<typename Wrapper>
Wrapper get_positive_unsigned(
    const toml::table& tbl,
    const std::string& key)
{
    auto value = get_required<int64_t>(tbl, key);

    if (value <= 0) {
        throw std::runtime_error(
            "'" + key + "' must be > 0");
    }
    return Wrapper{
        static_cast<unsigned>(value)
    };
}

template<typename Wrapper>
InitFinal<Wrapper> parse_schedule(const toml::table& tbl)
{
    auto init = get_positive_unsigned<Wrapper>(tbl, "init");
    auto last = get_positive_unsigned<Wrapper>(tbl, "last");

    return {init, last};
}

double get_probability(
    const toml::table& tbl,
    const std::string& key)
{
    auto value = get_required<double>(tbl, key);

    if (value < 0.0 || value > 1.0) {
        throw std::runtime_error(
            "'" + key + "' must be in [0,1]");
    }

    return value;
}

SimAnnInitialSolution parse_initial_solution(const std::string & str)
{
    if (str == "Greedy")
        return SimAnnInitialSolution::Greedy;
    if (str == "Random")
        return SimAnnInitialSolution::Random;

    throw std::runtime_error(
        "Unknown SimAnnInitialSolution '" + str + "'");
}

RemovalMode parse_removal_mode(const std::string& str)
{
    if (str == "ClusterOfRandomAntennaID")
        return RemovalMode::ClusterOfRandomAntennaID;
    if (str == "RandomCluster")
        return RemovalMode::RandomCluster;
    if (str == "LargestCluster")
        return RemovalMode::LargestCluster;
    if (str == "SmallestClusters")
        return RemovalMode::SmallestClusters;

    throw std::runtime_error(
        "Unknown RemovalMode '" + str + "'");
}

AddingMode parse_adding_mode(const std::string& str)
{
    if (str == "Greedy")
        return AddingMode::Greedy;
    if (str == "RandomTopKSampling")
        return AddingMode::RandomTopKSampling;
    if (str == "BestSampling")
        return AddingMode::BestSampling;
    if (str == "Random")
        return AddingMode::Random;

    throw std::runtime_error(
        "Unknown AddingMode '" + str + "'");
}

} // end of anonymous namespace


inline SimulatedAnnealingConfig parse_simulated_annealing_config(const std::filesystem::path& path)
{
    SimulatedAnnealingConfig config;
    if (not std::filesystem::exists(path)) {
        std::cout << "Simulated Annealing Config file not found, using defaults.\n";
        return config;
    }
    
    toml::table tbl = toml::parse_file(path.string());

    //----------------------------------------
    // Top-level parameters
    //----------------------------------------

    config.simann_output_path =
        get_required<std::string>(tbl, "output_path");

    config.initial_sol = parse_initial_solution(get_required<std::string>(tbl, "initial_sol"));  

    config.probability_for_cluster_exchange =
        get_probability(tbl, "probability_for_cluster_exchange");

    // --------------------------------------
    // Reheating
    // -----------------------------------
    config.min_reheat_start_progress =
        get_required<double>(tbl, "min_reheat_start_progress");
    config.max_reheat_start_progress =
        get_required<double>(tbl, "max_reheat_start_progress");        
    config.reheat_amount =
        get_required<double>(tbl, "reheat_amount");
    config.stagnation_threshold_seconds = 
        get_required<unsigned>(tbl, "stagnation_threshold");

    //----------------------------------------
    // simann_topk
    //----------------------------------------

    config.interpolation_cutoff_progress =
        get_probability(tbl, "interpolation_cutoff_progress");

    auto const* simann_topk = tbl["simann_topk"].as_table();
    if (!simann_topk) {
        throw std::runtime_error("Missing [simann_topk] section");
    }

    auto const* max_remove = (*simann_topk)["max_num_antennas_to_remove"].as_table();
    auto const* buffer_size = (*simann_topk)["buffer_size_top_r"].as_table();
    auto const* num_samples = (*simann_topk)["num_samples_for_adding"].as_table();

    if (!max_remove){
        throw std::runtime_error("Missing [simann_topk.max_num_antennas_to_remove]");
    }
    if (!buffer_size){
        throw std::runtime_error("Missing [simann_topk.buffer_size_top_r]");
    }
    if (!num_samples){
        throw std::runtime_error("Missing [simann_topk.num_samples_for_adding]");
    }

    config.simann_topk_params.max_num_antennas_to_remove =
        parse_schedule<MaxNumAntennasToRemove>(*max_remove);
    config.simann_topk_params.buffer_size_top_r =
        parse_schedule<BufferSizeTopR>(*buffer_size);
    config.simann_topk_params.num_samples_for_adding =
        parse_schedule<NumSamplesForAdding>(*num_samples);

    //----------------------------------------
    // cluster_exchange
    //----------------------------------------

    auto const* cluster = tbl["cluster_exchange"].as_table();
    if (!cluster) {
        throw std::runtime_error("Missing [cluster_exchange] section");
    }

    config.removal_mode = parse_removal_mode(get_required<std::string>(*cluster, "removal_mode"));
    config.adding_mode = parse_adding_mode(get_required<std::string>(*cluster, "adding_mode"));

    config.probability_for_progress_weighted_greedy_adding = 
        get_probability(*cluster, "probability_for_greedy_adding");

    //----------------------------------------
    // Optional schedule sanity checks
    //----------------------------------------

    if (config.simann_topk_params.max_num_antennas_to_remove.init.value
        < config.simann_topk_params.max_num_antennas_to_remove.last.value) {
        throw std::runtime_error("max_num_antennas_to_remove.init should usually be >= last");
    }

    if (config.simann_topk_params.buffer_size_top_r.init.value
        < config.simann_topk_params.buffer_size_top_r.last.value) {
        throw std::runtime_error("buffer_size_top_r.init should usually be >= last");
    }

    return config;
}




#endif // GISCUPBONN_SIMULATEDANNEALINGCONFIG_HPP
