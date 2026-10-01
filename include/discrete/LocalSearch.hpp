#ifndef GISCUPBONN_NAIVELOCALSEARCH
#define GISCUPBONN_NAIVELOCALSEARCH

#include "LocalSearchBackend.hpp"
#include "utility/AbstractLogger.hpp"
#include "utility/Solution.hpp"
#include "utility/LocalSearchConfig.hpp"
#include "geometry/DiscreteCoverageInstance.hpp"
#include "discrete/AbstractIntervalCoverageAlgorithm.hpp"
#include <filesystem>
#include <vector>

namespace LocalSearchLogging {
    struct LocalSearchValues {
        unsigned num_full_resets = 0;

        unsigned improvements_from_2_opt = 0;
        
        unsigned num_randomization_improved = 0;
        unsigned num_objective_change_improved = 0;
        unsigned num_tabu_improved = 0;
        unsigned num_cluster_big_improved = 0;
        unsigned num_cluster_small_improved = 0;
        unsigned num_total_reset_improved = 0;
    };

    enum LastBreakoutTechnique {
        FULL_RESET,
        PARTIAL_RANDOMIZATION,
        OBJECTIVE_CHANGE,
        TABU,
        CLUSTER_BIG,
        CLUSTER_SMALL,
    };

    class LocalSearchLogger : public Logger {
    public:
        void log_local_search_stats() {
            log("--------------------------------------------------------------------------------");
            log("Number of full resets                 : ", stats.num_full_resets);
            log("Improvements by 2-Opt                 : ", stats.improvements_from_2_opt);
            log("--------------------------------------------------------------------------------");
            log("Improvements by Full Reset            :", stats.num_total_reset_improved);
            log("Improvements by Partial Randomization :", stats.num_randomization_improved);
            log("Improvements by Objective Change      :", stats.num_objective_change_improved);
            log("Improvements by Tabu Search           :", stats.num_tabu_improved);
            log("Improvements by Big Cluster Removal   :", stats.num_cluster_big_improved);
            log("Improvements by Small Cluster Removal :", stats.num_cluster_small_improved);

            stream() << std::flush;
        }

        void log_solution(const std::vector<unsigned> &antennas, unsigned objective_value) {
            stream() << "\n----------------------------------------\n";
            stream() << "Solution Value : " << objective_value << "\n";
            stream() << "Antenna IDs    : \n";
            for (unsigned antenna_id : antennas) {
                stream() << antenna_id << " ";
            }
            stream() << "----------------------------------------";
            stream() << std::endl;
        }

        void increment_num_full_resets() {
            stats.num_full_resets++;
        }

        void increment_2_opt_improvements() {
            stats.improvements_from_2_opt++;
        }

        void set_last_breakout_strategy(LastBreakoutTechnique technique) {
            last_breakout_technique = technique;
        }

        void inform_new_best_solution() {
            switch (last_breakout_technique) {
                case LocalSearchLogging::LastBreakoutTechnique::FULL_RESET:
                    stats.num_total_reset_improved++;
                    break;
                case LocalSearchLogging::LastBreakoutTechnique::PARTIAL_RANDOMIZATION:
                    stats.num_randomization_improved++;
                    break;
                case LocalSearchLogging::LastBreakoutTechnique::OBJECTIVE_CHANGE:
                    stats.num_objective_change_improved++;
                    break;
                case LocalSearchLogging::LastBreakoutTechnique::TABU:
                    stats.num_tabu_improved++;
                    break;
                case LocalSearchLogging::LastBreakoutTechnique::CLUSTER_BIG:
                    stats.num_cluster_big_improved++;
                    break;
                case LocalSearchLogging::LastBreakoutTechnique::CLUSTER_SMALL:
                    stats.num_cluster_small_improved++;
                    break;
                default:
                    throw std::runtime_error("Forgot a breakout technique");
            }
        }

        

    private:
        LastBreakoutTechnique last_breakout_technique;
        LocalSearchValues stats;
    };
}

class LocalSearch : public IntervalCoverageSolver {
public:
    LocalSearch(const LocalSearchConfig::LocalSearchConfig &config) : config(config) {}

    Solution run(const DiscreteCoverageInstance &instance, unsigned k, double tau, unsigned timelimit_in_ms, unsigned seed, std::vector<unsigned> const &partial_solution) const override {
        LocalSearchImpl algo(config);

        return algo.run(instance, k, tau, timelimit_in_ms, seed, partial_solution);
    }

private:
    const LocalSearchConfig::LocalSearchConfig config;

    class LocalSearchImpl : private LocalSearchBackend {
    public:
        LocalSearchImpl(const LocalSearchConfig::LocalSearchConfig &config) : config(config), logger() {}

        Solution run(const DiscreteCoverageInstance &instance, unsigned k, double tau, unsigned timelimit_in_ms, unsigned seed, std::vector<unsigned> const &partial_solution);
        
    private:
        const LocalSearchConfig::LocalSearchConfig &config;
        LocalSearchLogging::LocalSearchLogger logger;

        mutable std::optional<std::filesystem::path> last_written_solution_path;

        void write_best_solution(std::ostream &stream, const std::vector<unsigned> &antenna_ids, const DiscreteCoverageInstance &instance) const;
        void write_best_solution_atomic(const std::vector<unsigned> &antenna_ids, const DiscreteCoverageInstance &instance, const std::filesystem::path &path) const;
    };
};

class ImproveWithOneOpt : public IntervalCoverageSolver {
public:
    ImproveWithOneOpt(unsigned num_steps) : num_steps(num_steps) {}

    Solution run(const DiscreteCoverageInstance &instance, unsigned k, double tau, unsigned timelimit_in_ms, unsigned seed, std::vector<unsigned> const &partial_solution) const override {
        ImproveWithOneOptImpl algo(num_steps);

        return algo.run(instance, k, tau, timelimit_in_ms, seed, partial_solution);
    }

private:
    unsigned num_steps;

    class ImproveWithOneOptImpl : private LocalSearchBackend {
    public:
        ImproveWithOneOptImpl(unsigned num_steps) : num_steps(num_steps) {}

        Solution run(const DiscreteCoverageInstance &instance, unsigned k, double tau, unsigned timelimit_in_ms, unsigned seed, std::vector<unsigned> const &partial_solution);

    private:
        unsigned num_steps;
    };
};

#endif

