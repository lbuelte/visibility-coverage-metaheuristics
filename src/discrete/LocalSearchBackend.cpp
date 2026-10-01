#include "discrete/LocalSearchBackend.hpp"
#include "utility/k_means.hpp"

#include <CGAL/number_utils.h>
#include <cassert>
#include <chrono>
#include <limits>
#include <numeric>
#include <random>
#include <algorithm>
#include <utility>
#include <vector>

void LocalSearchBackend::setup(const DiscreteCoverageInstance &instance, unsigned p_k, double p_tau, unsigned timelimit_in_ms, unsigned seed) {
    t0 = std::chrono::steady_clock::now();
    time_limit = timelimit_in_ms;

    k = p_k;
    tau = p_tau;
    antenna_id_upper_bound = instance.get_number_antennas();
    antenna_candidates = instance.get_relevant_antennas();
    num_antennas = antenna_candidates.size();
    num_polygons = instance.get_number_polygons();
    resize_vectors_etc();

    num_one_opt_steps_tested = 0;
    num_one_opt_steps_improving = 0;

    rng = std::mt19937(seed);

    set_progress_to_lexicographic();
}

void LocalSearchBackend::set_progress_to_lexicographic() {
    progress = 1.0 - 1.0 / static_cast<double>(num_polygons + 1);
}

double LocalSearchBackend::objective(Coverage coverage) const {
    return (1.0 - progress) * coverage.total + progress * static_cast<double>(coverage.serviced);
}

void LocalSearchBackend::resize_vectors_etc() {
    weighted_coverage_gain.assign(antenna_id_upper_bound, 0.0);
    num_polygons_serviced_gain.assign(antenna_id_upper_bound, 0);
    dirty_antenna.assign(antenna_id_upper_bound, true);
    tabu_until.assign(antenna_id_upper_bound, 0);
}

double LocalSearchBackend::iterate_one_opt(const DiscreteCoverageInstance &instance, DCNS2 &data_structure, unsigned max_iterations) {
    bool something_changed = true;

    double max_progress_to_break_local_optimum = -1.0;

    for (unsigned i = 0; i < max_iterations && something_changed && within_time(); i++) {
        something_changed = false;
        max_progress_to_break_local_optimum = -1.0;

        std::vector<unsigned> selected_antennas = data_structure.getSelectedAntennas();

        for (unsigned antenna : selected_antennas) {
            double best_gain = -1.0;
            unsigned best_antenna_in = none;

            num_one_opt_steps_tested++;

            const Coverage coverage_prev = data_structure.getCoverage();

            data_structure.remove(antenna);
            mark_neighbouring_antennas_dirty(instance, antenna);

            // Negative: what taking the antenna out costs.
            const Coverage loss = data_structure.getCoverage() - coverage_prev;
            const double obj_loss = objective(loss);

            for (unsigned replacement : antenna_candidates) {
                if (data_structure.has(replacement))
                    continue;

                ensure_antenna_clean(data_structure, replacement);

                double obj_gain = (1.0 - progress) * weighted_coverage_gain[replacement] + progress * static_cast<double>(num_polygons_serviced_gain[replacement]) + obj_loss;

                if (obj_gain > best_gain) {
                    best_gain = obj_gain;
                    best_antenna_in = replacement;
                }

                if (obj_gain < 0.0) {
                    double delta_weighted_coverage = weighted_coverage_gain[replacement] + loss.total;
                    double delta_serviced_polygons = static_cast<double>(num_polygons_serviced_gain[replacement] + loss.serviced);

                    if (delta_weighted_coverage > 0.0 && delta_serviced_polygons < 0.0) {
                        max_progress_to_break_local_optimum = std::max(max_progress_to_break_local_optimum, -delta_weighted_coverage / (delta_serviced_polygons - delta_weighted_coverage));
                    }
                }
            }

            if (best_gain > 0.00001) {
                data_structure.add(best_antenna_in);
                mark_neighbouring_antennas_dirty(instance, best_antenna_in);
                num_one_opt_steps_improving++;
            }
            else {
                data_structure.add(antenna);
                mark_neighbouring_antennas_dirty(instance, antenna);
            }
            something_changed = something_changed || (best_gain > 0.00001);
        }
    }

    if (something_changed) {
        return -1.0;
    }

    return max_progress_to_break_local_optimum;
}

bool LocalSearchBackend::one_step_two_opt_fixed_removed_antennas(const DiscreteCoverageInstance &instance, DCNS2 &data_structure, unsigned antenna_a, unsigned antenna_b, const std::vector<unsigned> &replacement_candidates) {
    unsigned best_in_1 = none, best_in_2 = none;
    double best_gain = -1.0;

    // Remove antennas and calculate loss
    const Coverage coverage_prev = data_structure.getCoverage();

    data_structure.remove(antenna_a);
    data_structure.remove(antenna_b);
    mark_neighbouring_antennas_dirty(instance, antenna_a);
    mark_neighbouring_antennas_dirty(instance, antenna_b);

    const Coverage loss = data_structure.getCoverage() - coverage_prev;

    for (unsigned antenna_c : replacement_candidates) {
        if (!within_time())
            break;

        if (data_structure.has(antenna_c))
            continue;

        // Installing antenna_c makes every peek below an exact joint peek of {c, d}.
        // Adding and removing an antenna is cheap enough that this beats the old
        // independence test, which only avoided the joint peek when c and d shared
        // no polygon with each other or with the two removed antennas.
        const Coverage coverage_without_c = data_structure.getCoverage();
        data_structure.add(antenna_c);
        const Coverage delta_c = data_structure.getCoverage() - coverage_without_c;

        for (unsigned antenna_d : replacement_candidates) {
            if (antenna_c >= antenna_d)
                continue;

            if (!within_time())
                break;

            if (data_structure.has(antenna_d))
                continue;

            const double obj_gain = objective(delta_c + data_structure.peek(antenna_d) + loss);

            if (obj_gain > 0.001) {
                best_in_1 = antenna_c;
                best_in_2 = antenna_d;
                best_gain = obj_gain;
                break;
            }
        }

        // The selection is restored either way, so the gain cache stays valid.
        data_structure.remove(antenna_c);

        if (best_gain > 0.001)
            break;
    }

    if (best_gain > 0.001) {
        data_structure.add(best_in_1);
        data_structure.add(best_in_2);

        mark_neighbouring_antennas_dirty(instance, antenna_a);
        mark_neighbouring_antennas_dirty(instance, antenna_b);
        mark_neighbouring_antennas_dirty(instance, best_in_1);
        mark_neighbouring_antennas_dirty(instance, best_in_2);
    }
    else {
        data_structure.add(antenna_a);
        data_structure.add(antenna_b);
        mark_neighbouring_antennas_dirty(instance, antenna_a);
        mark_neighbouring_antennas_dirty(instance, antenna_b);
    }

    return best_gain > 0.001;
}

bool LocalSearchBackend::one_step_two_opt(const DiscreteCoverageInstance &instance, DCNS2 &data_structure, unsigned num_removal_candidates, unsigned num_insertion_candidates) {
    if(num_removal_candidates == 0 && num_insertion_candidates == 0) return false;
    std::vector<unsigned> antennas = data_structure.getSelectedAntennas();

    std::vector<std::pair<unsigned, double>> antennas_with_usefullness;
    for (unsigned antenna : antennas) {
        // What removing the antenna costs is exactly what weightedCoverageByAntenna()
        // and polygonsDependentOnAntenna() used to report, so one round trip replaces both.
        // The selection is restored, so the gain cache stays valid.
        const Coverage coverage_prev = data_structure.getCoverage();
        data_structure.remove(antenna);
        const double usefullness = -objective(data_structure.getCoverage() - coverage_prev);
        data_structure.add(antenna);

        antennas_with_usefullness.emplace_back(antenna, usefullness);
    }

    std::sort(antennas_with_usefullness.begin(), antennas_with_usefullness.end(), [](std::pair<unsigned, double> &l, std::pair<unsigned, double> &r){return l.second < r.second;});

    std::vector<unsigned> removal_candidates;
    for (unsigned i = 0; i < antennas_with_usefullness.size() && i < num_removal_candidates; i++) { // TODO make hyperparameter
        removal_candidates.emplace_back(antennas_with_usefullness[i].first);
    }

    std::vector<std::pair<unsigned, unsigned>> antennas_with_extra_service;
    for (unsigned antenna = 0; antenna < instance.get_number_antennas(); antenna++) {
        // Re-adding a selected antenna gains nothing, and peeking one is not allowed.
        if (data_structure.has(antenna)) {
            antennas_with_extra_service.emplace_back(antenna, 0);
            continue;
        }

        ensure_antenna_clean(data_structure, antenna);
        antennas_with_extra_service.emplace_back(antenna, num_polygons_serviced_gain[antenna]);
    }

    std::sort(antennas_with_extra_service.begin(), antennas_with_extra_service.end(), [](std::pair<unsigned, unsigned> &l, std::pair<unsigned, unsigned> &r){return l.second > r.second;});
    std::vector<unsigned> replacement_candidates;

    for (unsigned i = 0; i < antennas_with_extra_service.size() && i < num_insertion_candidates; i++) {
        replacement_candidates.emplace_back(antennas_with_extra_service[i].first);
    }

    for (unsigned i = 0; i < removal_candidates.size() && within_time(); i++) {
        for (unsigned j = i + 1; j < removal_candidates.size() && within_time(); j++) {
            bool found_improvement = one_step_two_opt_fixed_removed_antennas(instance, data_structure, removal_candidates[i], removal_candidates[j], replacement_candidates);
            if (found_improvement){
                return true;
            }
        }
    }

    return false;
}

DCNS2 LocalSearchBackend::start_with_random_solution(const DiscreteCoverageInstance &instance, const std::vector<unsigned> &partial_solution) {
    DCNS2 data_structure(instance, tau);

    std::uniform_int_distribution<unsigned> antenna_distribution(0, antenna_candidates.size() - 1);

    for (unsigned antenna : partial_solution) {
        data_structure.add(antenna);
    }

    for (unsigned i = partial_solution.size(); i < k; i++) {
        unsigned new_antenna = antenna_candidates[antenna_distribution(rng)];
        while (data_structure.has(new_antenna)) {
            new_antenna = antenna_candidates[antenna_distribution(rng)];
        }

        data_structure.add(new_antenna);
    }

    set_progress_to_lexicographic();

    return data_structure;
}

void LocalSearchBackend::breakout_by_objective_change(const DiscreteCoverageInstance &instance, DCNS2 &data_structure, double new_weight_param, double progress_steps, unsigned iterations_per_step) {
    // The cached gains are raw coverage deltas, so re-weighting does not invalidate them.
    for (progress = new_weight_param; progress < 1.0 && within_time(); progress += progress_steps) {
        iterate_one_opt(instance, data_structure, iterations_per_step);
    }

    set_progress_to_lexicographic();
}

void LocalSearchBackend::breakout_by_random_replacement(const DiscreteCoverageInstance &instance, DCNS2 &data_structure, unsigned num_to_replace) {
    std::vector<unsigned> antennas = data_structure.getSelectedAntennas();
    unsigned num_selected = antennas.size();
    num_to_replace = std::min(num_selected, num_to_replace);

    std::uniform_int_distribution<unsigned> range(0, num_antennas - 1);

    for (unsigned i = 0; i < num_to_replace; i++) {
        std::swap(antennas[num_selected - i - 1], antennas[range(rng) % (num_selected - i)]);

        const unsigned to_remove = antennas[num_selected - i - 1];
        data_structure.remove(to_remove);
        mark_neighbouring_antennas_dirty(instance, to_remove);
    }

    for (unsigned i = 0; i < num_to_replace; i++) {
        unsigned next_antenna = antenna_candidates[range(rng)];
        while (data_structure.has(next_antenna))
            next_antenna = antenna_candidates[range(rng)];

        data_structure.add(next_antenna);
        mark_neighbouring_antennas_dirty(instance, next_antenna);
    }
}

void LocalSearchBackend::breakout_by_one_opt_tabu(const DiscreteCoverageInstance &instance, DCNS2 &data_structure, unsigned max_tabu_steps) {
    const double obj_val_prev = objective(data_structure.getCoverage());
    double obj_val_current = obj_val_prev;

    std::uniform_int_distribution<unsigned> tabu_dist(min_tabu_duration, max_tabu_steps);

    for (unsigned i = 0; i < max_tabu_steps && within_time() && obj_val_current <= obj_val_prev; i++) {
        std::vector<unsigned> antennas = data_structure.getSelectedAntennas();

        unsigned best_out = none, best_in = none;
        double best_in_delta = -std::numeric_limits<double>::max();

        for (unsigned to_remove : antennas) {
            data_structure.remove(to_remove);
            mark_neighbouring_antennas_dirty(instance, to_remove);

            double obj_loss = obj_val_prev - objective(data_structure.getCoverage());

            for (unsigned j : antenna_candidates) {
                // A selected antenna can not be swapped in, and peeking one is not allowed.
                if (tabu_until[j] == 0 && !data_structure.has(j)) {
                    ensure_antenna_clean(data_structure, j);

                    double obj_delta = (1.0 - progress) * weighted_coverage_gain[j] + progress * static_cast<double>(num_polygons_serviced_gain[j]) + obj_loss;

                    if (obj_delta > best_in_delta) {
                        best_in_delta = obj_delta;
                        best_out = to_remove;
                        best_in = j;
                    }
                }
            }

            data_structure.add(to_remove);
            mark_neighbouring_antennas_dirty(instance, to_remove);
        }

        if (best_in == none)
            return;

        data_structure.remove(best_out);
        mark_neighbouring_antennas_dirty(instance, best_out);
        data_structure.add(best_in);
        mark_neighbouring_antennas_dirty(instance, best_in);
        obj_val_current = objective(data_structure.getCoverage());

        for (unsigned j : antenna_candidates) {
            if (tabu_until[j] > 0)
                tabu_until[j]--;
        }
        tabu_until[best_out] = tabu_dist(rng);
    }
}

void LocalSearchBackend::breakout_by_removing_clusters(const DiscreteCoverageInstance &instance, DCNS2 &data_structure, unsigned num_clusters, unsigned num_clusters_removed, const IntervalCoverageSolver &replacement_algo, unsigned algo_time) {
    std::vector<std::pair<double, double>> current_antenna_positions;
    std::map<unsigned, unsigned> reduced_id;

    unsigned next_id = 0;

    const std::vector<unsigned> antenna_ids = data_structure.getSelectedAntennas();

    for (unsigned antenna_id : antenna_ids) {
        current_antenna_positions.emplace_back(CGAL::to_double(instance.get_antenna_data(antenna_id).position.x()), CGAL::to_double(instance.get_antenna_data(antenna_id).position.y()));
        reduced_id[antenna_id] = next_id;
        next_id++;
    }

    std::uniform_int_distribution<unsigned> antenna_range(0, instance.get_number_antennas() - 1);

    KMeansClustering cluster_algo(current_antenna_positions, num_clusters, antenna_range(rng));

    cluster_algo.run();

    std::vector<unsigned> cluster_ids(num_clusters);
    std::iota(cluster_ids.begin(), cluster_ids.end(), 0);
    std::shuffle(cluster_ids.begin(), cluster_ids.end(), rng);

    std::vector<bool> cluster_removed(num_clusters, false);
    for (unsigned i = 0; i < num_clusters_removed; i++) {
        cluster_removed[cluster_ids[i]] = true;
    }

    unsigned num_to_replenish = 0;

    for (unsigned antenna : antenna_ids) {
        if (cluster_removed[cluster_algo.cluster_by_ids(reduced_id.at(antenna))]) {
            num_to_replenish++;

            data_structure.remove(antenna);
            mark_neighbouring_antennas_dirty(instance, antenna);
        }
    }

    if (num_to_replenish > 0) { // Cover case with k close to number centers where empty clusters appear
        Solution antennas_to_add = replacement_algo.run(instance, num_to_replenish, tau, algo_time, antenna_range(rng), {antenna_range(rng)});

        for (unsigned new_antenna : antennas_to_add.get_solution_antenna_ids()) {
            if (num_to_replenish == 0)
                break;

            if (!data_structure.has(new_antenna)) {
                data_structure.add(new_antenna);
                mark_neighbouring_antennas_dirty(instance, new_antenna);
                num_to_replenish--;
            }
        }

        for (unsigned i = 0; i < num_to_replenish; i++) {
            unsigned new_antenna = antenna_range(rng);

            while (data_structure.has(new_antenna)) {
                new_antenna = antenna_range(rng);
            }

            data_structure.add(new_antenna);
            mark_neighbouring_antennas_dirty(instance, new_antenna);
        }
    }
}

void LocalSearchBackend::mark_neighbouring_antennas_dirty(const DiscreteCoverageInstance &instance, unsigned antenna) {
    for (const PolygonCoverage &coverage: instance.antenna_covers_polygons()[antenna]) {
        for (const CoveredBy &seen_by_antenna : instance.polygon_covered_by_antennas()[coverage.polygon]) {
            dirty_antenna[seen_by_antenna.antenna] = true;
        }
    }
}

void LocalSearchBackend::ensure_antenna_clean(DCNS2 &data_structure, unsigned antenna) {
    if (dirty_antenna[antenna])
    {
        const Coverage replacement_gain = data_structure.peek(antenna);
        weighted_coverage_gain[antenna] = replacement_gain.total;
        num_polygons_serviced_gain[antenna] = replacement_gain.serviced;
        dirty_antenna[antenna] = false;
    }
}

bool LocalSearchBackend::within_time() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count() < time_limit;
}
