//
// Created by philip on 6/26/26.
//

#ifndef GISCUPBONN_COOLINGSCHEDULES_HPP
#define GISCUPBONN_COOLINGSCHEDULES_HPP

#include <numeric>
#include <optional>
#include <functional>
#include <vector>
#include <iostream>
#include "DiscreteCoverageInstance.hpp"
#include "DCNS2.hpp"
#include "RandomTopKNeighborSelector.hpp"
#include "utility/SimulatedAnnealingConfig.hpp"

struct DeltaHistograms {
    std::vector<unsigned> increasing_move;
    std::vector<unsigned> decreasing_move;
    double avg_increasing_move = 0.0;
    double avg_decreasing_move = 0.0;
};

struct CoolingParamsForTopKNeighborSelector {
    MaxNumAntennasToRemove max_num_antennas_to_remove;
    BufferSizeTopR top_r_buffer_size;
    NumSamplesForAdding num_samples_for_adding;
};

class AcceptanceThresholdFunctionGenerator {
public:
    explicit AcceptanceThresholdFunctionGenerator(const DiscreteCoverageInstance & instance_) :instance(instance_){};

    //gives an acceptance_threshold function that is derived via sampling during a greedy random search for a the fixed set of hyperparameters
    std::function<double(double, double)> run_for_top_r_selector(   unsigned k,
                                                                    double tau,
                                                                    CoolingParamsForTopKNeighborSelector const &neighbor_selector_params,
                                                                    const unsigned timelimit_ms = 60000);

    //sets an acceptance function only do this for quick visulization or logging
    void set_acceptance_threshold_function(const std::function<double(double, double)>& function);

    //writes a table to give an idea how the currently set threshold function operates
    void log_acceptance_threshold_function(std::ostream &os = std::cout) const;

    //writes the delta currently set delta histogram
    void log_delta_histogram(std::ostream &os = std::cout) const;

private:
    const DiscreteCoverageInstance & instance;
    std::optional<std::function<double(double, double)>> acceptance_threshold_function=std::nullopt;
    std::optional<DeltaHistograms> histogram=std::nullopt;

    //derives an acceptance_threshold_function function that ensures that the average worsening move is
    //accepted with probability p_start in the beginning
    //and the smallest worsening move (-1) is accepted with p_end when we finish
    static std::function<double(double, double)> derive_acceptance_threshold_function_from_decreasing_move_average(const double decreasing_move_average,
                                                                                                         const double p_start = 0.8,
                                                                                                         const double p_end = 0.005);

    //compute an initial random solution and brings a DCNS2
    //in the correct state so that the selector can start from this initial solution
    static DCNS2 compute_data_structure_state_for_random_initialization(const DiscreteCoverageInstance &instance,
                                                                                                         const unsigned k,
                                                                                                         const double tau,
                                                                                                         const unsigned seed = 43,
                                                                                                         const std::vector<unsigned> &antenna_candidates = {});

    //Expects dcn to be initialized with a valid initial solution (antennas added and structure cleaned).
    //computes as many samples as possible in the given timelimit for the selector with fixed hyperparameters
    //and builds a histogram of how the samples each would change the objective value
    //worsening moves are rejected and improving moves are always accepted
    //this simulates a random greed search
    //important: this should start from a random solution
    static DeltaHistograms compute_objective_delta_value_histogram(DCNS2 &dcn,
                                                        NeighborSelector &selector,
                                                        const unsigned timelimit_ms = 60000);
};

inline std::function<double(double, double)> AcceptanceThresholdFunctionGenerator::run_for_top_r_selector(unsigned k, double tau,
    CoolingParamsForTopKNeighborSelector const &neighbor_selector_params,
    const unsigned timelimit_ms) {

    unsigned seed=1337;
    std::vector<unsigned> candidates(instance.antenna_covers_polygons().size());
    std::iota(candidates.begin(), candidates.end(), 0u);
    auto dcn_structure=compute_data_structure_state_for_random_initialization(instance, k, tau, seed, candidates);
    // The calibration structure is built with the default objective (serviced polygons),
    // and the histogram below reads only num_serviced_polygons -- so this reproduces the
    // previous behaviour exactly.
    RandomTopKNeighborSelector selector(
            dcn_structure,
            candidates,
            [](double, unsigned num_serviced) { return static_cast<double>(num_serviced); },
            seed + 1,
            neighbor_selector_params.max_num_antennas_to_remove,
            neighbor_selector_params.top_r_buffer_size,
            neighbor_selector_params.num_samples_for_adding);
    histogram=compute_objective_delta_value_histogram(dcn_structure, selector, timelimit_ms);
    set_acceptance_threshold_function(derive_acceptance_threshold_function_from_decreasing_move_average(histogram.value().avg_decreasing_move));
    return acceptance_threshold_function.value();
}

inline void AcceptanceThresholdFunctionGenerator::set_acceptance_threshold_function(const std::function<double(double, double)> &function) {
    acceptance_threshold_function=function;
}

inline void AcceptanceThresholdFunctionGenerator::log_acceptance_threshold_function(std::ostream &os) const {
    if (!acceptance_threshold_function.has_value()) {
        os << "No acceptance_threshold_function set. Cannot log acceptance function." << std::endl;
        return;
    }

    const std::vector<int> deltas = {
        -1, -2, -3, -5, -8, -9, -10,
        -13, -16, -20
    };
    const std::vector<int> progress_pcts = {
        0, 5, 10, 15, 20, 25, 30, 35, 40, 45,
        50, 55, 60, 65, 70, 75, 80, 85, 90, 95, 100
    };

    constexpr int col_w = 8;
    constexpr int row_w = 6;

    // Header
    os << std::setw(row_w) << "p\\delta";
    for (int d : deltas)
        os << std::setw(col_w) << d;
    os << "\n";

    // Separator
    os << std::string(row_w + col_w * deltas.size(), '-') << "\n";

    // Rows
    os << std::fixed << std::setprecision(4);
    for (int p : progress_pcts) {
        os << std::setw(row_w) << (std::to_string(p) + "%");
        for (int d : deltas) {
            double prob = acceptance_threshold_function.value()(d, p / 100.0);
            os << std::setw(col_w) << prob;
        }
        os << "\n";
    }
}

inline void AcceptanceThresholdFunctionGenerator::log_delta_histogram(std::ostream &os) const {
    if (!histogram.has_value()) {
        os << "No histogram available. Run a selector first." << std::endl;
        return;
    }

    auto print_histogram = [&os](const std::vector<unsigned> &hist, const std::string &label) {
        os << "=== " << label << " ===\n";
        for (unsigned i = 0; i < hist.size(); ++i) {
            os << "  delta=" << i << " : " << hist[i] << "\n";
        }
    };

    print_histogram(histogram.value().increasing_move, "Deltas that decrease the objective value:");
    os << "  avg: " << histogram.value().avg_increasing_move << "\n\n";

    print_histogram(histogram.value().decreasing_move, "Deltas that increase the objective value:");
    os << "  avg: " << histogram.value().avg_decreasing_move << "\n";
}

inline std::function<double(double, double)> AcceptanceThresholdFunctionGenerator::
derive_acceptance_threshold_function_from_decreasing_move_average(const double decreasing_move_average, const double p_start,
    const double p_end) {
    const double T_start = -decreasing_move_average / std::log(p_start);
    const double T_end   = -1.0        / std::log(p_end);
    return [T_start, T_end](const double delta, const double progress) -> double {
        if (delta >= 0)
            return 1.0;
        const double temperature = T_start * std::pow(T_end / T_start, progress);
        return std::exp(delta / temperature);
    };
}

inline DCNS2 AcceptanceThresholdFunctionGenerator::
compute_data_structure_state_for_random_initialization(const DiscreteCoverageInstance &instance, const unsigned k,
    const double tau, const unsigned seed, const std::vector<unsigned> &antenna_candidates) {
    DCNS2 dcn(instance, tau);
    std::mt19937 rng(seed);
    std::vector<unsigned> initial_antennas;
    initial_antennas.reserve(k);
    std::sample(antenna_candidates.begin(), antenna_candidates.end(),
                std::back_inserter(initial_antennas), k, rng);
    for (unsigned const antenna : initial_antennas)
        dcn.add(antenna);
    return dcn;
}

inline DeltaHistograms AcceptanceThresholdFunctionGenerator::compute_objective_delta_value_histogram(
    DCNS2 &dcn, NeighborSelector &selector,
    const unsigned timelimit_ms) {

    std::vector<int> increasing_deltas;
    std::vector<int> decreasing_deltas;

    const auto start = std::chrono::steady_clock::now();
    unsigned old_serviced = dcn.getCoverage().serviced;
    while (std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - start).count() < timelimit_ms) {

        Neighbor neighbor = selector.propose_neighbor();
        const int delta = neighbor.result.num_serviced_polygons - old_serviced;

        if (delta >= 0) {
            increasing_deltas.push_back(delta);
            selector.accept_neighbor();
            old_serviced += delta;
        } else {
            decreasing_deltas.push_back(-delta);
            selector.reject_neighbor();
        }
    }

    const auto make_hist = [](const std::vector<int> &deltas) {
        if (deltas.empty())
            return std::vector<unsigned>{};
        const int max_val = *std::max_element(deltas.begin(), deltas.end());
        std::vector<unsigned> hist(max_val + 1, 0);
        for (const int d : deltas)
            hist[d]++;
        return hist;
    };

    const auto avg = [](const std::vector<int> &deltas) {
        if (deltas.empty()) return 0.0;
        return static_cast<double>(std::accumulate(deltas.begin(), deltas.end(), 0LL)) / deltas.size();
    };

    return {make_hist(increasing_deltas), make_hist(decreasing_deltas),
        avg(increasing_deltas), avg(decreasing_deltas)};
}

#endif //GISCUPBONN_COOLINGSCHEDULES_HPP