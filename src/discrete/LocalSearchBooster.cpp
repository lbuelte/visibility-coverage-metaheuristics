// Proof of concept local search for strictly improving a candidate solution.
// Unfortunately gains over greedy are still very limited currently.

#include "LocalSearchBooster.hpp"

#include "discrete/AntennaSet.hpp"
#include "DiscreteCoverageInstance.hpp"
#include "GeometricInformation.hpp"

#include <random>
#include <iostream>
#include <algorithm>
#include <numeric>

#include <CGAL/bounding_box.h>

// TODO might want to kill this wrapper later
struct IntervalCoverProblem {
    struct AntennaInfo {
        Point2 pos;
        std::vector<CoveredBuilding> covered_buildings;
    };

    double min_coverage = 0;
    std::vector<AntennaInfo> antennas;

    IntervalCoverProblem(double min_coverage, const DiscreteCoverageInstance &dci)
        : min_coverage(min_coverage)
    {
        // TODO this is actually a map(map(...), ...)
        for (const auto &poly_covs : dci.antenna_covers_polygons()) {
            std::vector<CoveredBuilding> cbs;
            for (const auto &poly_cov : poly_covs) {
                const auto poly_data = dci.get_polygon_data(poly_cov.polygon);
                std::vector<Interval> intervals = poly_cov.intervals;
                std::erase_if(intervals, [](const auto &interval) {
                    return interval.length() == 0;
                });
                std::sort(intervals.begin(), intervals.end(), [](const auto &a, const auto &b) {
                    return a.start < b.start;
                });
                cbs.emplace_back(CoveredBuilding {
                    .id = static_cast<size_t>(poly_cov.polygon),
                    .intervals = std::move(intervals),
                });
            }
            std::sort(cbs.begin(), cbs.end(), [](const auto &a, const auto &b) {
                return a.id < b.id;
            });
            const auto antenna_id = antennas.size();
            antennas.emplace_back(AntennaInfo{dci.get_antenna_data(antenna_id).position, cbs});
        }
    }

    size_t getAntennaCount() const {
        return antennas.size();
    }

    Antenna getAntenna(size_t id) const {
        return {id, std::span(antennas[id].covered_buildings)};
    }

    std::vector<Antenna> getAntennas(std::span<const std::size_t> ids) const {
        std::vector<Antenna> res;
        for (const auto id : ids)
            res.emplace_back(getAntenna(id));
        return res;
    }

    Point2 getAntennaPos(size_t id) const {
        return antennas[id].pos;
    }
};

struct Booster {
    // somewhat arbitrarily set knobs
    constexpr static auto LEAF_ITER = 10000;
    constexpr static auto MAX_SWAPS = 5;
    constexpr static auto ROUNDS = 3;

    std::mt19937 rng;

    const IntervalCoverProblem &p;
    DynamicAntennaSet cur_best;
    std::vector<size_t> &orig_candidate;
    size_t leaf_size = 8;

    Booster(const IntervalCoverProblem &p, std::vector<std::size_t> &candidate) : p(p), orig_candidate(candidate), rng(std::random_device{}()),
        cur_best(p.min_coverage, p.getAntennas(candidate))
    {}

    void boostLeaf(std::vector<size_t> &candidate, std::span<const size_t> available);

    void boostRecursive(std::vector<size_t> &candidate, std::span<const size_t> available);

    void boost();
};

void Booster::boostLeaf(std::vector<size_t> &candidate, std::span<const size_t> available)
{
    if (candidate.empty())
        return; // let's hope another round gives us antennas to redistribute

    // TODO for local search, a substantially simpler data structure
    // with fast undo would be preferable (just remember delta)...
    std::vector<Antenna> antennas;
    for (const auto antenna_id : candidate) {
        antennas.emplace_back(p.getAntenna(antenna_id));
    }

    std::uniform_int_distribution<size_t> add_idx_dist(0, available.size() - 1);
    std::uniform_int_distribution<size_t> remove_idx_dist(0, candidate.size() - 1);
    std::uniform_int_distribution<size_t> swaps_dist(1, MAX_SWAPS);
    for (int iter = 0; iter < LEAF_ITER; ++iter) {
        std::array<std::pair<size_t, size_t>, MAX_SWAPS> undo;
        DynamicAntennaSet challenger = cur_best;
        size_t swaps = swaps_dist(rng);
        for (int i = 0; i < swaps; ++i) {
            const auto add_idx = add_idx_dist(rng);
            const auto remove_idx = remove_idx_dist(rng);
            undo[i] = {remove_idx, candidate[remove_idx]};
            challenger.removeAntenna(p.getAntenna(candidate[remove_idx]));
            challenger.addAntenna(p.getAntenna(available[add_idx]));
            candidate[remove_idx] = available[add_idx];
        }
        if (challenger.getCoverage().improves(cur_best.getCoverage())) {
            cur_best = challenger; // keep
            const auto &cov = challenger.getCoverage();
            std::cout << "new coverage: " << cov.n_covered << ", " << cov.total << std::endl;
        } else { // undo
            for (int i = swaps - 1; i > -1; --i) {
                const auto [idx, old_antenna_id] = undo[i];
                candidate[idx] = old_antenna_id;
            }
        }
    }
}

void Booster::boostRecursive(std::vector<size_t> &candidate, std::span<const size_t> available) {
    if (candidate.size() < leaf_size || available.size() < leaf_size) {
        boostLeaf(candidate, available);
        return;
    }
    // could probably be done zero copy with a view but eh
    std::vector<Point2> antenna_points;
    for (const auto id : available)
        antenna_points.emplace_back(p.getAntennaPos(id));
    const auto bbox = CGAL::bounding_box(antenna_points.begin(), antenna_points.end());
    int split_axis = 0;
    if (bbox.ymax() - bbox.ymin() > bbox.xmax() - bbox.xmin())
        split_axis = 1;

    std::uniform_int_distribution<size_t> pivot_idx_dist(0, available.size()-1);
    const auto pivot_idx = pivot_idx_dist(rng);
    std::nth_element(antenna_points.begin(), antenna_points.begin() + pivot_idx, antenna_points.end());
    const auto pivot_coord = antenna_points[pivot_idx][split_axis];

    std::array<std::vector<size_t>, 2> candidate_split;
    std::array<std::vector<size_t>, 2> available_split;

    for (const auto id : candidate)
        candidate_split[p.getAntennaPos(id)[split_axis] <= pivot_coord].emplace_back(id);

    for (const auto id : available)
        available_split[p.getAntennaPos(id)[split_axis] <= pivot_coord].emplace_back(id);

    boostRecursive(candidate_split[0], available_split[0]);
    boostRecursive(candidate_split[1], available_split[1]);
    candidate.clear();
    // recombine boosted parts
    candidate.insert(candidate.end(),
        candidate_split[0].begin(),
        candidate_split[0].end());
    candidate.insert(candidate.end(),
        candidate_split[1].begin(),
        candidate_split[1].end());
}

void Booster::boost() {
    for (int i = 0; i < ROUNDS; ++i, leaf_size *= 2) {
        std::cout << "Round " << i << ", leaf size " << leaf_size << std::endl;
        std::vector<size_t> all_antennas(p.getAntennaCount());
        std::iota(all_antennas.begin(), all_antennas.end(), 0);

        // Exclude the current solution from the swap-in pool so we never "add" an
        // antenna that's already present. set_difference needs both ranges sorted.
        std::sort(orig_candidate.begin(), orig_candidate.end());

        std::vector<size_t> available;
        std::set_difference(
            all_antennas.begin(), all_antennas.end(),
            orig_candidate.begin(), orig_candidate.end(),
            std::back_inserter(available)
        );
    
        boostRecursive(orig_candidate, available);
    }
}

Solution LocalSearchBooster::run(
    DiscreteCoverageInstance const &instance,
    const unsigned k,
    const double tau, 
    const unsigned timelimit_in_ms,
    const unsigned seed,
    std::vector<unsigned> const &partial_solution_antenna_ids) const
{
    Solution sol = initial_solver->run(instance, k, tau, timelimit_in_ms, seed, partial_solution_antenna_ids);

    std::vector<size_t> candidate;
    for (const auto id : sol.get_solution_antenna_ids())
        candidate.emplace_back(id);

    IntervalCoverProblem p(tau, instance);
    Booster booster(p, candidate);
    booster.boost();

    std::vector<unsigned> candidate_uint;
    for (const auto id : booster.cur_best.getAntennaIds())
        candidate_uint.emplace_back(id);

    return Solution(instance, k, tau, std::move(candidate_uint));
}
