#ifndef GISCUPBONN_LOCALSEARCHCONFIG
#define GISCUPBONN_LOCALSEARCHCONFIG

#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

#include "toml.hpp"

namespace LocalSearchConfig {
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

    template<typename T>
    std::optional<T> get_optional(const toml::table& tbl, const std::string& key)
    {
        auto value = tbl[key].value<T>();

        if (!value) {
            return {};
        }

        return *value;
    }

    inline double get_probability(
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

    struct TabuConfig {
        //! Maximum number of 1-Opt steps done in tabu search before stopping
        unsigned max_number_tabu_steps = 200;
        //! An antenna removed during tabu search can't be selected by tabu search again for at least #ANTENNAS * this value
        double min_tabu_percentage = 0.01;
        //! An antenna removed during tabu search can't be selected by tabu search again for at most #ANTENNAS * this value
        double max_tabu_percentage = 0.5;    
    };

    inline std::ostream& operator<<(std::ostream& os, const TabuConfig& cfg) {
        os << "Tabu Search Parameters\n";
        os << "----------------------\n";

        os << "max_number_tabu_steps : "
        << cfg.max_number_tabu_steps << '\n';

        os << "min_tabu_percentage   : "
        << cfg.min_tabu_percentage << '\n';

        os << "max_tabu_percentage   : "
        << cfg.max_tabu_percentage << '\n';

        return os;
    }

    struct ClusterRemovalConfig {
        //! Cluster removal flips a coin between a few large clusters and a lot of small ones
        //! With a few large clusters one is deleted
        //! With many small ones a percentage of clusters is removed
        //! number clusters for the large clusters case
        unsigned num_clusters = 5;
        //! fraction of num_antennas that accounts for cluster number (e.g. 0.1 means num_antennas/10 clusters)
        double frac_of_antennas_clusters = 0.1;
        //! Fraction of the clusters in the many clusters case that get removed (also in expectation the fraction of antennas removed)
        double frac_of_clusters_removed = 0.2;
    };

    inline std::ostream& operator<<(std::ostream& os, const ClusterRemovalConfig& cfg) {
        os << "Cluster Removal Parameters\n";
        os << "--------------------------\n";

        os << "num_clusters              : "
        << cfg.num_clusters << '\n';

        os << "frac_of_antennas_clusters : "
        << cfg.frac_of_antennas_clusters << '\n';

        os << "frac_of_clusters_removed  : "
        << cfg.frac_of_clusters_removed << '\n';

        return os;
    }

    struct ObjectiveChangeConfig {
        //! New objective interpolation is selected at random from [x - interpolation_width, x] where x is the max value so that the current optimum stops being optimal
        double interpolation_width = 0.5;
        //! The objective interpolation is moved in these steps to go back to lexicographic
        double interpolation_step_size = 0.1;
        //! The number of local search iterations performed for a specific objective interpolation
        unsigned num_iterations_per_step = 3;
    };

    inline std::ostream& operator<<(std::ostream& os, const ObjectiveChangeConfig& cfg) {
        os << "Objective Change Parameters\n";
        os << "---------------------------\n";

        os << "interpolation_width        : "
        << cfg.interpolation_width << '\n';

        os << "interpolation_step_size    : "
        << cfg.interpolation_step_size << '\n';

        os << "num_iterations_per_step    : "
        << cfg.num_iterations_per_step << '\n';

        return os;
    }

    struct RandomizationConfig {
        //! Mina nd max percentage of antennas randomly removed and reinserted
        double min_removed_percentage = 0.1;
        double max_removed_percentage = 0.5;
    };

    inline std::ostream& operator<<(std::ostream& os, const RandomizationConfig& cfg) {
        os << "Randomization Parameters\n";
        os << "------------------------\n";

        os << "min_removed_percentage   : "
        << cfg.min_removed_percentage << '\n';

        os << "max_removed_percentage   : "
        << cfg.max_removed_percentage << '\n';

        return os;
    }

    struct LocalSearchConfig {
        std::filesystem::path ls_log_path = "data/output/local_search";
        //! Iterations of trying breakout strategies before restarting with a completely random solution
        unsigned iterations_without_improvement = 20;
        unsigned num_2_opt_removal_candidates = 10;
        unsigned num_2_opt_insertion_candidates = 2000;

        //! These calues should be from [0,1] and add up to at most 1. They describe the probability of each breakout strategy. Randomization has probability 1 - sum of the others
        double prob_objective_change = 0.3;
        double prob_tabu_search = 0.3;
        double prob_cluster_removal = 0.3;

        TabuConfig tabu_config;
        ClusterRemovalConfig cluster_removal_config;
        ObjectiveChangeConfig objective_change_config;
        RandomizationConfig randomization_config;

        void set_output_path(const std::string &run_identifier, std::optional<std::filesystem::path> const &log_path = std::nullopt) {
            if (log_path.has_value()){
                ls_log_path = log_path.value() / run_identifier;
            }
            else {
                ls_log_path = ls_log_path / run_identifier;
            }
        }
    };

    inline std::ostream& operator<<(std::ostream& os, const LocalSearchConfig& cfg)
    {
        os << "========================================\n";
        os << "Local Search Configuration\n";
        os << "========================================\n";

        os << "log_path                         : "
        << cfg.ls_log_path << "\n";

        os << "iterations_without_improvement   : "
        << cfg.iterations_without_improvement << '\n';

        os << "num_2_opt_removal_candidates     : "
        << cfg.num_2_opt_removal_candidates << '\n';

        os << "num_2_opt_insertion_candidates   : "
        << cfg.num_2_opt_insertion_candidates << '\n';

        os << "prob_objective_change            : "
        << cfg.prob_objective_change << '\n';

        os << "prob_tabu_search                 : "
        << cfg.prob_tabu_search << '\n';

        os << "prob_cluster_removal             : "
        << cfg.prob_cluster_removal << '\n';

        os << '\n';
        os << cfg.tabu_config << '\n';
        os << cfg.cluster_removal_config << '\n';
        os << cfg.objective_change_config << '\n';
        os << cfg.randomization_config;

        return os;
    }


    inline ObjectiveChangeConfig parse_objective_change_config(const toml::table &toml_config) {
        if (! toml_config["objective_change_config"].as_table()) {
            throw std::runtime_error("Local Search missing object_change_config");
        }
        toml::table objective_change_config = *toml_config["objective_change_config"].as_table();
        
        ObjectiveChangeConfig config;

        config.interpolation_step_size = get_probability(objective_change_config, "interpolation_step_size");
        config.interpolation_width = get_probability(objective_change_config, "interpolation_step_size");
        config.num_iterations_per_step = get_required<unsigned>(objective_change_config, "num_iterations_per_step");

        return config;
    }

    inline TabuConfig parse_tabu_config(const toml::table &toml_config) {
        if (! toml_config["tabu_config"].as_table()) {
            throw std::runtime_error("Local Search missing object_change_config");
        }
        toml::table tabu_config = *toml_config["tabu_config"].as_table();
        
        TabuConfig config;

        config.max_number_tabu_steps = get_required<unsigned>(tabu_config, "max_number_tabu_steps");
        config.min_tabu_percentage = get_probability(tabu_config, "min_tabu_percentage");
        config.max_tabu_percentage = get_probability(tabu_config, "max_tabu_percentage");

        return config;
    }

    inline ClusterRemovalConfig parse_cluster_removal_config(const toml::table &toml_config) {
        if (! toml_config["cluster_removal_config"].as_table()) {
            throw std::runtime_error("Local Search missing object_change_config");
        }
        toml::table tabu_config = *toml_config["cluster_removal_config"].as_table();
        
        ClusterRemovalConfig config;

        config.num_clusters = get_required<unsigned>(tabu_config, "num_clusters");
        config.frac_of_antennas_clusters = get_probability(tabu_config, "frac_of_antennas_clusters");
        config.frac_of_clusters_removed = get_probability(tabu_config, "frac_of_clusters_removed");

        return config;
    }

    inline RandomizationConfig parse_randomization_config(const toml::table &toml_config) {
        if (! toml_config["randomization_config"].as_table()) {
            throw std::runtime_error("Local Search missing object_change_config");
        }
        toml::table tabu_config = *toml_config["randomization_config"].as_table();
        
        RandomizationConfig config;

        config.min_removed_percentage = get_probability(tabu_config, "min_removed_percentage");
        config.max_removed_percentage = get_probability(tabu_config, "max_removed_percentage");

        return config;
    }

    inline LocalSearchConfig parse_local_search_config(const std::filesystem::path &path) {
        LocalSearchConfig config;

        if (! std::filesystem::exists(path)) {
            std::cout << "Local Search Config not found, using defaults\n";
            std::cout << "Tried path " << path.string() << "\n";
            return config;
        }

        toml::table toml_config = toml::parse_file(path.string());

        config.iterations_without_improvement = get_required<unsigned>(toml_config, "iterations_without_improvement");
        config.num_2_opt_insertion_candidates = get_required<unsigned>(toml_config, "num_2_opt_insertion_candidates");
        config.num_2_opt_removal_candidates = get_required<unsigned>(toml_config, "num_2_opt_removal_candidates");
        config.prob_cluster_removal = get_probability(toml_config, "prob_cluster_removal");
        config.prob_objective_change = get_probability(toml_config, "prob_objective_change");
        config.prob_tabu_search = get_probability(toml_config, "prob_tabu_search");

        if (config.prob_tabu_search + config.prob_cluster_removal + config.prob_objective_change > 1.0)
            throw std::runtime_error("Probabilities of breakout strategies have to add up to at most 1.0");

        config.objective_change_config = parse_objective_change_config(toml_config);
        config.tabu_config = parse_tabu_config(toml_config);
        config.cluster_removal_config = parse_cluster_removal_config(toml_config);
        config.randomization_config = parse_randomization_config(toml_config);

        return config;
    }
}

#endif // GISCUPBONN_LOCALSEARCHCONFIG
