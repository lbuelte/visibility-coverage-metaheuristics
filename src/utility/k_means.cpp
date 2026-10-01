#include "utility/k_means.hpp"
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <utility>
#include <vector>

KMeansClustering::KMeansClustering(const std::vector<std::pair<double, double>> &points, unsigned k, unsigned seed)
                                    : points(points), k(k), seed(seed) {
                                        num_points = points.size();
                                        cluster_of.resize(num_points, 0);
                                        cluster_center.resize(k, {0.0, 0.0});
                                        points_per_cluster.resize(k, 0);

                                        if (points.empty())
                                            throw std::runtime_error("K-means called without any points");
                                        if (k == 0)
                                            throw std::runtime_error("K-Means called with k = 0");
                                    }

void KMeansClustering::run() {
    has_run = true;
    
    std::mt19937 rng(seed);
    std::uniform_int_distribution<unsigned> point_dist(0, num_points - 1);

    for (unsigned i = 0; i < k; i++) {
        cluster_center[i] = points[point_dist(rng)];
    }

    bool something_changed = true;
    std::vector<std::pair<double, double>> new_centers;

    while (something_changed) {
        something_changed = false;

        new_centers.assign(k, {0.0, 0.0});
        points_per_cluster.assign(k, 0);

        for (unsigned i = 0; i < num_points; i++) {
            double best_dist = std::numeric_limits<double>::max();
            unsigned best_center = -1;

            for (unsigned j = 0; j < k; j++) {
                double dist_to_j = (points[i].first - cluster_center[j].first) * (points[i].first - cluster_center[j].first) + (points[i].second - cluster_center[j].second) * (points[i].second - cluster_center[j].second);

                if (dist_to_j < best_dist) {
                    best_dist = dist_to_j;
                    best_center = j;
                }
            }

            something_changed |= best_center != cluster_of[i];
            cluster_of[i] = best_center;
            new_centers[best_center].first += points[i].first;
            new_centers[best_center].second += points[i].second;
            points_per_cluster[best_center]++;
        }

        for (unsigned j = 0; j < k; j++) {
            if (points_per_cluster[j] > 0) {
                cluster_center[j].first = new_centers[j].first / static_cast<double>(points_per_cluster[j]);
                cluster_center[j].second = new_centers[j].second / static_cast<double>(points_per_cluster[j]);
            }
        }
    }
}

const std::vector<unsigned> &KMeansClustering::clusters_by_ids() const {
    if (!has_run)
        throw std::runtime_error("Run k-means before evaluating result");
    return cluster_of;
}

unsigned KMeansClustering::cluster_by_ids(unsigned id) const {
    if (!has_run)
        throw std::runtime_error("Run k-means before evaluating result");
    return cluster_of[id];
}

const std::vector<std::pair<double, double>> &KMeansClustering::cluster_centers() const {
    if (!has_run)
        throw std::runtime_error("Run k-means before evaluating result");
    return cluster_center;
}
