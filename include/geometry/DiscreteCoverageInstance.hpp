//
// Created by philip on 6/8/26.
//

#ifndef GISCUPBONN_COMBINATORIALINSTANCE_HPP
#define GISCUPBONN_COMBINATORIALINSTANCE_HPP
#include "geometry/GeometricInformation.hpp"
#include "geometry/VisibilityComputation.hpp"
#include <algorithm>
#include <cassert>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

using AntennaId = unsigned;

class DCI2;

struct PolygonCoverage {
    std::vector<Interval> intervals;
    unsigned polygon;
};
struct CoveredBy {
    unsigned antenna;
    unsigned entry;
    CoveredBy(unsigned a, unsigned e) : antenna(a), entry(e) {}
};

class DiscreteCoverageInstance {
public:

    void delete_the_arrangements() {
        std::cout << "\033[34m"<<"Warning: Arrangements have been deleted from DiscreteCoverageInstance."
                  << " Visualization does not work anymore."<< "\033[0m" <<std::endl;
        visibility_geometry_->antennas_vis_arrangements.clear();
        arrangements_have_been_deleted=true;
    };

    explicit DiscreteCoverageInstance(const std::vector<PolygonWithInputId> & polygons_with_input_id);

    // Builds a reduced instance from an existing (already pruned) instance:
    // keeps only the relevant antennas (reindexed densely) and rebuilds all
    // derived data structures from scratch. An instance built this way is
    // considered "finalized" — no further candidates may be added to it.
    DiscreteCoverageInstance(const DiscreteCoverageInstance &instance, bool visualization = false);

    explicit DiscreteCoverageInstance(unsigned num_polygons, const std::vector<std::vector<std::pair<unsigned, std::vector<std::pair<double, double>>>>> &coverages);


    //adds a new set of Points that lie on polygons to the candidate set and compute their visibilities
    //afterwards all data structures of DiscreteCoverageInstance are recomputed
    //NOTE: THE CANDIDATES ARE NOT TESTED IF THEY LIE ON THE POLYGON!!!! But the visibility construction should catch this
    void add_new_antenna_candidates(const std::vector<PointOnPolygon> & points_on_polygons);

    //adds exactly the vertex candidate set into the geometry and then builds the discrete coverage instance data structe
    void initialize_with_vertex_antenna_candidates();

    //Computes the best k best multi intervals with respect to being covered by anntennas
    //and adds them as new candidates to the geometry and the instance.
    //afterwards all datastructures are rebuild
    void add_candidates_via_top_r_multi_interval_boundaries(const unsigned r);

    void add_n_candidates_via_bisection(const unsigned n);


    //given a set of antennas and a vector of serviced polygons
    //returns true if this is a solution and the serviced polygons are exactly the ones that were proposed
    bool verify_solution(const std::vector<unsigned> &antenna_ids,const std::vector<unsigned> &covered_polygon_ids, double tau) const;

    //return all serviced polygons as ids for a set of anntennas and the value tau
    std::vector<unsigned> evaluate_antenna_vector(const std::vector<unsigned>& antenna_ids, double tau) const;

    //given a polygon id return all antennas that  see this polygon
    std::vector<unsigned> antennas_seeing_polygon_as_indices(unsigned polygon_id) const;

    //given a polygon id return all antennas that do not see this polygon
    std::vector<unsigned> antennas_not_seeing_polygon_index_as_indices(unsigned polygon_id) const;

    //given a polygon id return all antenna visibility arrangements that do not see this polygon
    std::vector<Arrangement> antennas_not_seeing_polygon_arrangements(unsigned polygon_id) const ;

    [[nodiscard]] const PolygonData &get_polygon_data(unsigned polygon_id) const;

    [[nodiscard]] const Arrangement &get_arrangement_for_antenna(unsigned antenna_id) const;

    [[nodiscard]] const AntennaData &get_antenna_data(unsigned antenna_id) const;

    [[nodiscard]] const unsigned &get_original_id_for_polygon(unsigned polygon_id) const {
        return visibility_geometry_->polygons[polygon_id].original_id;
    }

    [[nodiscard]] const std::vector<AntennaData> &get_antenna_data_vector() const {
        return visibility_geometry_->antennas;
    }

    [[nodiscard]] std::pair<AntennaData, Arrangement> get_antenna_and_arrangement(unsigned antenna_id) const;

    // Returns all polygons and their coverage for a given antenna vector
    [[nodiscard]] std::vector<std::pair<Poly2, double>> get_polygons_and_coverage(
        const std::vector<unsigned> &antenna_ids) const;

    [[nodiscard]] std::vector<std::vector<PolygonCoverage>> const & antenna_covers_polygons() const;

    [[nodiscard]] std::vector<std::vector<CoveredBy>> const & polygon_covered_by_antennas() const;

    [[nodiscard]] std::vector<std::vector<LabeledMultiInterval>> const  &get_multi_labeled_coverage_intervals() const;

    //given a polygon interval value returns the geometric point and tge edge it is on
    [[nodiscard]] std::pair<Point2, unsigned> get_point_and_edge_id_from_interval_value(const double value, const unsigned polygon_id);

    //given a polygon interval returns the edge it is on and the interval in the edge reference frame
    [[nodiscard]] std::pair<unsigned,Interval> get_edge_interval_from_polygon_interval(const Interval inter, const unsigned polygon_id);

    //given a interval value in polygon reference frame returns the edge it is on and the value in the edge reference frame
    [[nodiscard]] std::pair<unsigned,double> get_position_on_edge_interval_for_interval_polygon_interval_value(const double value,const unsigned polygon_id);

    //give histogram as a vector and also prints it
    std::vector<unsigned> get_multi_labeled_coverage_interval_size_histogram(bool write=true) const;

    [[nodiscard]] unsigned get_number_antennas() const;

    [[nodiscard]] unsigned get_number_polygons() const;

    //returns the vector of relevant antennas
    //Note this is not a member and is computed in this function in O(antennasize)!!
    //Before the algorithms this should be fixed so only call this once
    [[nodiscard]] std::vector<unsigned> get_relevant_antennas() const;

    //marks antennas as irrelevant if their interval lengths in sum are samller than min_sum_coverage
    void prune_relevant_antenna_by_sum_coverage(double min_sum_coverage);

    //marks antennas as irrelevant when less then min_seen_polygons_are_seen
    void prune_relevant_antenna_by_min_seen_polygon(unsigned min_seen_polygon);

    [[nodiscard]] const DCI2 &getDCI2() const;

private:

    // Built once the instance is finalized (reduced copy or synthetic test instance);
    // rebuilds every antenna's coverage into one densely packed CSR structure so that
    // e.g. a 3x3 parallel crossproduct run can all share it read-only instead of each
    // run keeping its own copy.
    void initDCI2();

    // More densely packed variant of the important data of this instance
    // (which antenna sees which parts of which buildings). unique_ptr rather than
    // optional/by-value because DCI2 is only a forward declaration at this point --
    // its constructor needs DiscreteCoverageInstance to be complete, so it's defined
    // further down in this same header, after this class closes.
    std::unique_ptr<DCI2> dci2;

    bool arrangements_have_been_deleted=false;
    bool is_reduced_copy_ = false;

    std::vector<bool> antenna_is_relevant;
    bool antenna_is_relevant_has_been_set=false;

    std::vector<std::vector<PolygonCoverage>> antenna_covers_polygons_;
    std::vector<std::vector<CoveredBy>> polygon_covered_by_antennas_;


    //each point on the polygon is seen by a vector of antennas (possibly empty or of size 1)
    //this data structure contains for each polygon given as polygon interval
    //(i.e. each polygon is represented by a single [0,1] interval)
    //the intervals that remain consistent with respect to the antennas that see it
    //rarely it may happen that you have two intervals [i,j][j,k] that have the same antenna set
    //(i.e. they should be unified but they are not [but i think that is harmless])
    //the labeledMUltiIntervals are in order so suceeding ones should have start as end
    //and could be merged if the corresponding antenna vectors/sets are the same
    //this would remove the problem
    std::vector<std::vector<LabeledMultiInterval>> multi_labeled_coverage_intervals_;

    unsigned number_of_polygons = 0;
    unsigned number_of_antennas = 0;

    std::optional<VisibilityComputation> visibility_geometry_ = {};


    void reset_and_refresh_internal_data_structures();

    void generate_labeled_multi_intervals();

    static std::vector<LabeledMultiInterval> compute_multi_labeled_intervals_for_one_polygon(const std::vector<LabeledInterval>& labeled_intervals);

    //takes the tagged edges from the geometry and then with those computes the interval coverages
    //Notably one antenna can have multiple coverage intervals on a polygon
    //Note that edge intervals are corrected into polygon intervals
    void build_antenna_coverage_from_tagged_edges(const std::vector<AntennaData> &antennas);

    //given an interval on an edge transform it into an interval on the polygon
    Interval corrected_interval(const Interval inter ,const unsigned polygon_id,const unsigned edge_id) const;


};


// Coverage data in CSR form: every interval of the instance lives in one array, and the
// per-building entries are spans into it, grouped by antenna. Walking one antenna's
// coverage is then a sequential read, rather than a chase through one heap allocation
// (and one vector header) per covered building.
//
// Defined here (rather than standalone) because its constructor needs
// DiscreteCoverageInstance to be a complete type.
class DCI2 {
protected:

    struct CoveredBuilding {
        unsigned interval_begin; // into all_intervals
        unsigned polygon;
    };

    std::vector<Interval> all_intervals;
    std::vector<CoveredBuilding> entries; // grouped by antenna
    std::vector<unsigned> antenna_begin;  // n_antennas + 1 indices into entries

public:

    DCI2(const DiscreteCoverageInstance &dci) {
        const AntennaId n_antennas = dci.get_number_antennas();

        // Cleaning only ever drops intervals, so these are upper bounds and the flat arrays
        // fill without reallocating.
        size_t n_entries = 0, n_intervals = 0;
        for (AntennaId i = 0; i < n_antennas; ++i) {
            const auto &poly_covs = dci.antenna_covers_polygons()[i];
            n_entries += poly_covs.size();
            for (const auto &poly_cov : poly_covs)
                n_intervals += poly_cov.intervals.size();
        }
        all_intervals.reserve(n_intervals);
        entries.reserve(n_entries + n_antennas + 1); // one sentinel per antenna, plus final sentinel
        antenna_begin.reserve(n_antennas + 1);

        std::vector<Interval> intervals; // scratch, reused across buildings

        for (AntennaId i = 0; i < n_antennas; ++i) {
            antenna_begin.emplace_back(entries.size());

            const auto &poly_covs = dci.antenna_covers_polygons()[i];
            for (const auto &poly_cov : poly_covs) {
                intervals.assign(poly_cov.intervals.begin(), poly_cov.intervals.end());
                std::erase_if(intervals, [](auto interval) {
                    return interval.length() == 0;
                });
                std::sort(intervals.begin(), intervals.end(), [](auto a, auto b) {
                    return a.start < b.start;
                });
                // Merge touching intervals: coverage is built per polygon edge, so an
                // antenna seeing several consecutive edges yields intervals that meet
                // exactly (corrected_interval() maps a full edge onto
                // [start_interval_point[e], start_interval_point[e + 1]]).
                // Chains of more than two collapse as well, since the merged interval
                // takes over the end of the one it absorbed.
                size_t out = 0;
                for (size_t in = 0; in < intervals.size(); ++in) {
                    if (out > 0 && intervals[out - 1].end == intervals[in].start)
                        intervals[out - 1].end = intervals[in].end;
                    else
                        intervals[out++] = intervals[in];
                }
                intervals.resize(out);

                const unsigned begin = all_intervals.size();
                all_intervals.insert(all_intervals.end(), intervals.begin(), intervals.end());
                entries.emplace_back(CoveredBuilding{
                    .interval_begin = begin,
                    .polygon = poly_cov.polygon
                });
            }
            entries.emplace_back(CoveredBuilding{
                .interval_begin = (unsigned)all_intervals.size(),
                .polygon = ~(unsigned)0,
            });
        }

        antenna_begin.emplace_back(entries.size());
        assert(all_intervals.size() <= n_intervals); // the reserve above was an upper bound
    }

    // The antenna's trailing sentinel is left out of the span, but stays reachable as the
    // successor of the last entry, which is where getIntervals() reads its end from.
    std::span<const CoveredBuilding> getCoveredBuildings(AntennaId antenna) const {
        const unsigned begin = antenna_begin[antenna];
        const unsigned next_begin = antenna_begin[antenna + 1];
        return {entries.data() + begin, next_begin - begin - 1};
    }

    std::span<const Interval> getIntervals(const CoveredBuilding *cb) const {
        auto size = (cb + 1)->interval_begin - cb->interval_begin;
        return {all_intervals.data() + cb->interval_begin, size};
    }

    // Base of the flat interval array. Lets callers keep offsets rather than pointers,
    // which matters when they interleave these runs with ones held in a growing scratch.
    const Interval *intervalData() const { return all_intervals.data(); }

    // Bytes actually occupied by the flat arrays' elements (size(), not capacity())
    size_t workingSetBytes() const {
        return all_intervals.size() * sizeof(Interval)
             + entries.size() * sizeof(CoveredBuilding)
             + antenna_begin.size() * sizeof(unsigned);
    }
};

inline void DiscreteCoverageInstance::initDCI2() {
    dci2 = std::make_unique<DCI2>(*this);
    double const working_set_mb = dci2->workingSetBytes() / (1024.0 * 1024.0);
    std::cout << "DCI2 working set: " << working_set_mb << " MB" << std::endl;
}

inline const DCI2 &DiscreteCoverageInstance::getDCI2() const {
    if (dci2 == nullptr)
        throw std::runtime_error("DCI2 not initialized!!!");
    return *dci2;
}


inline DiscreteCoverageInstance::DiscreteCoverageInstance(const std::vector<PolygonWithInputId> & polygons_with_input_id): visibility_geometry_(polygons_with_input_id) {
    number_of_polygons = visibility_geometry_.value().polygons.size();
    polygon_covered_by_antennas_.resize(number_of_polygons);
}

inline DiscreteCoverageInstance::DiscreteCoverageInstance(const DiscreteCoverageInstance &instance, bool visualize)
    : visibility_geometry_(instance)  // forwards to VisibilityComputation(const DiscreteCoverageInstance&)
{
    std::cout << "\033[34m"<<"Warning: Copying the DiscreteCoverageInstance is expensive "
               "and should only be called once after pruning antenna candidates! "
               "New candidates cannot be added for this reduced copy."<< "\033[0m" <<std::endl;
    is_reduced_copy_ = true;
    reset_and_refresh_internal_data_structures();
    if (not visualize){
        delete_the_arrangements();
    }
    initDCI2();
}

inline DiscreteCoverageInstance::DiscreteCoverageInstance(unsigned num_polygons, const std::vector<std::vector<std::pair<unsigned, std::vector<std::pair<double, double>>>>> &coverages) {
    number_of_polygons = num_polygons;
    number_of_antennas = coverages.size();

    polygon_covered_by_antennas_.resize(number_of_polygons);
    antenna_covers_polygons_.resize(number_of_antennas);

    for (unsigned i = 0; i < number_of_antennas; i++) {
        for (const auto [polygon, coverage] : coverages[i]) {
            std::vector<Interval> coverage_intervals;

            for (auto [start, end] : coverage) {
                coverage_intervals.emplace_back(start, end);
            }
            
            polygon_covered_by_antennas_[polygon].emplace_back(i, antenna_covers_polygons_.size());
            antenna_covers_polygons_[i].emplace_back(coverage_intervals, polygon);
        }
    }
    initDCI2();
}

inline void DiscreteCoverageInstance::add_new_antenna_candidates(const std::vector<PointOnPolygon> &points_on_polygons) {
    if (is_reduced_copy_) {
        throw std::runtime_error(
            "add_new_antenna_candidates: this instance was built as a reduced copy "
            "(via the DiscreteCoverageInstance(const DiscreteCoverageInstance&) constructor) "
            "and no longer owns the full obstacle geometry needed to process new candidates.");
    }
    visibility_geometry_.value().run_with_custom_candidates(points_on_polygons);
    reset_and_refresh_internal_data_structures();
}

inline void DiscreteCoverageInstance::initialize_with_vertex_antenna_candidates() {
    visibility_geometry_.value().run_with_vertex_candidates();
    reset_and_refresh_internal_data_structures();
}

inline void DiscreteCoverageInstance::add_candidates_via_top_r_multi_interval_boundaries(const unsigned r) {
    if (r==0) {
        return;
    }

    std::cout << "\033[34m"  // set text color to blue
          << "###################################################################################\n"
          << "WARNING: This function is not threadsafe and for now also not really tested########\n"
          << "BUT: It seems to work so feel free to use it, but it may result in weird behaviour#\n"
          << "###################################################################################"
          << "\033[0m" << std::endl;  // reset color
    std::vector<PointOnPolygon> result;
    for (unsigned p = 0; p < multi_labeled_coverage_intervals_.size(); p++) {
        // copy so we can sort without mutating the original data
        auto interval_set = multi_labeled_coverage_intervals_[p];
        std::sort(interval_set.begin(), interval_set.end(),
                  [](const LabeledMultiInterval &a, const LabeledMultiInterval &b) {
                      return a.antenna_ids.size() > b.antenna_ids.size();
                  });
        size_t count = std::min(static_cast<size_t>(r), interval_set.size());

        for (size_t i = 0; i < count; i++) {
            const auto &interval = interval_set[i];

            if (interval.start == -1) {
                std::cerr << "Warning: interval start is -1 in add_candidates_via_top_k_multi_interval_boundaries" << std::endl;
                continue;
            }

            auto p1 = get_point_and_edge_id_from_interval_value(interval.start, p);
            auto p2 = get_point_and_edge_id_from_interval_value(interval.end, p);
            result.emplace_back(p, p1.second, p1.first);
            result.emplace_back(p, p2.second, p2.first);
        }
    }
    add_new_antenna_candidates(result);
}

inline void DiscreteCoverageInstance::add_n_candidates_via_bisection(const unsigned n) {
    if (n==0) {
        return;
    }
    auto  candidates= visibility_geometry_.value().generate_n_candidates_via_bisection(n);
    add_new_antenna_candidates(candidates);
}


inline void DiscreteCoverageInstance::build_antenna_coverage_from_tagged_edges(
    const std::vector<AntennaData> &antennas) {
    for (unsigned i = 0; i < antennas.size(); i++) {
        const auto &antenna = antennas[i];
        auto sorted_edges = antenna.tagged_edges;
        if (sorted_edges.empty()) continue;
        std::sort(sorted_edges.begin(), sorted_edges.end(),
                  [](const auto &a, const auto &b) {
                      return a.poly_id < b.poly_id;
                  });
        std::vector<PolygonCoverage> covered_by_antenna_tmp;
        unsigned current_polygon = sorted_edges[0].poly_id;
        PolygonCoverage pc;
        pc.polygon = current_polygon;
        for (unsigned j = 0; j < sorted_edges.size(); j++) {
            const auto &edge = sorted_edges[j];
            if (edge.poly_id != current_polygon) {
                // flush completed polygon group
                polygon_covered_by_antennas_[current_polygon].emplace_back(i, covered_by_antenna_tmp.size());
                covered_by_antenna_tmp.push_back(pc);
                // start new group
                current_polygon = edge.poly_id;
                pc = PolygonCoverage{};
                pc.polygon = current_polygon;
            }
            pc.intervals.emplace_back(corrected_interval(edge.interval,edge.poly_id,edge.edge_id));
        }
        // flush last group
        polygon_covered_by_antennas_[current_polygon].emplace_back(i, covered_by_antenna_tmp.size());
        covered_by_antenna_tmp.push_back(pc);
        antenna_covers_polygons_[i] = std::move(covered_by_antenna_tmp);
    }
}

inline std::vector<unsigned> DiscreteCoverageInstance::antennas_seeing_polygon_as_indices(unsigned polygon_id) const {
    std::vector<unsigned> antennas;
    for (const auto &p : polygon_covered_by_antennas_[polygon_id]) {
        antennas.push_back(p.antenna);
    }
    return antennas;
}

inline std::vector<unsigned> DiscreteCoverageInstance::antennas_not_seeing_polygon_index_as_indices(unsigned polygon_id) const {
    std::vector<unsigned> antennas_seeing =
            antennas_seeing_polygon_as_indices(polygon_id);

    std::unordered_set<unsigned> seeing_set(
        antennas_seeing.begin(),
        antennas_seeing.end()
    );

    std::vector<unsigned> antennas_not_seeing;
    antennas_not_seeing.reserve(number_of_antennas - antennas_seeing.size());

    for (unsigned i = 0; i < number_of_antennas; ++i) {
        if (seeing_set.find(i) == seeing_set.end()) {
            antennas_not_seeing.push_back(i);
        }
    }
    return antennas_not_seeing;
}

inline std::vector<Arrangement> DiscreteCoverageInstance::antennas_not_seeing_polygon_arrangements(unsigned polygon_id) const {
    if (arrangements_have_been_deleted) {
        throw std::runtime_error("Cannot call antennas_not_seeing_polygon_arrangements after arrangements have been deleted");
    }
    std::vector<Arrangement> antennas_not_seeing;
    for (unsigned i : antennas_not_seeing_polygon_index_as_indices(polygon_id)) {
        antennas_not_seeing.push_back(visibility_geometry_.value().antennas_vis_arrangements[i]);
    }
    return antennas_not_seeing;
}

inline const PolygonData &DiscreteCoverageInstance::get_polygon_data(const unsigned polygon_id) const {
    return visibility_geometry_.value().polygons[polygon_id];
}

inline const Arrangement &DiscreteCoverageInstance::get_arrangement_for_antenna(unsigned antenna_id) const {
    if (arrangements_have_been_deleted) {
        throw std::runtime_error("Cannot call get_arrangement_for_antenna after arrangements have been deleted");
    }
    return visibility_geometry_.value().antennas_vis_arrangements[antenna_id];
}

inline const AntennaData &DiscreteCoverageInstance::get_antenna_data(const unsigned antenna_id) const {
    return visibility_geometry_.value().antennas[antenna_id];
}

inline std::pair<AntennaData, Arrangement> DiscreteCoverageInstance::get_antenna_and_arrangement(
    unsigned antenna_id) const {
    if (arrangements_have_been_deleted) {
        throw std::runtime_error("Cannot call get_antenna_and_arrangement after arrangements have been deleted");
    }
    return {visibility_geometry_.value().antennas[antenna_id], visibility_geometry_.value().antennas_vis_arrangements[antenna_id]};
}

inline bool DiscreteCoverageInstance::verify_solution(const std::vector<unsigned> &antenna_ids,
    const std::vector<unsigned> &covered_polygon_ids, double tau) const {
    const std::vector<unsigned> computed = evaluate_antenna_vector(antenna_ids, tau);
    return same_antenna_set(computed, covered_polygon_ids);
}

inline std::vector<unsigned> DiscreteCoverageInstance::evaluate_antenna_vector(const std::vector<unsigned> &antenna_ids,
    double tau) const {

    std::vector<std::vector<Interval>> polygon_intervals(number_of_polygons);

    for (unsigned antenna_id : antenna_ids) {
        for (const PolygonCoverage& pc : antenna_covers_polygons_[antenna_id]) {
            for (const Interval& iv : pc.intervals) {
                polygon_intervals[pc.polygon].push_back(iv);
            }
        }
    }

    std::vector<unsigned> covered_polygons;
    for (unsigned polygon_id = 0; polygon_id < number_of_polygons; ++polygon_id) {
        auto [coverage, _] = interval_union_coverage(std::move(polygon_intervals[polygon_id]));
        if (coverage >= tau) covered_polygons.push_back(polygon_id);
    }

    return covered_polygons;
}

inline std::vector<std::pair<Poly2, double>> DiscreteCoverageInstance::get_polygons_and_coverage(
    const std::vector<unsigned> &antenna_ids) const {
    std::vector<std::vector<Interval>> polygon_intervals(number_of_polygons);
    for (unsigned antenna_id : antenna_ids) {
        for (const PolygonCoverage& pc : antenna_covers_polygons_[antenna_id]) {
            for (const Interval& iv : pc.intervals) {
                polygon_intervals[pc.polygon].push_back(iv);
            }
        }
    }

    std::vector<std::pair<Poly2, double>> polygons_and_coverage;
    for (unsigned polygon_id = 0; polygon_id < number_of_polygons; ++polygon_id) {
        auto [coverage, _] = interval_union_coverage(std::move(polygon_intervals[polygon_id]));
        polygons_and_coverage.emplace_back(get_polygon_data(polygon_id).poly, coverage);
    }
    return polygons_and_coverage;
}

inline std::vector<std::vector<PolygonCoverage>> const & DiscreteCoverageInstance::antenna_covers_polygons() const {
    return antenna_covers_polygons_;
}

inline std::vector<std::vector<CoveredBy>> const & DiscreteCoverageInstance::polygon_covered_by_antennas() const {
    return polygon_covered_by_antennas_;
}

inline void DiscreteCoverageInstance::generate_labeled_multi_intervals() {
    //labels every interval with its antenna for the polygons
    std::vector<std::vector<LabeledInterval>> coverage_intervals_labeled(number_of_polygons);
    for (unsigned i=0;i<antenna_covers_polygons_.size();i++) {
        for (auto &[intervals, polygon]:antenna_covers_polygons_[i]) {
            for (auto & interval:intervals) {
                coverage_intervals_labeled[polygon].emplace_back(interval.start,interval.end,i);
            }
        }
    }

    std::vector<std::vector<LabeledMultiInterval>> coverage_collector;
    for (unsigned i=0;i<number_of_polygons;i++) {
        coverage_collector.push_back(compute_multi_labeled_intervals_for_one_polygon(coverage_intervals_labeled[i]));
    }

    multi_labeled_coverage_intervals_=coverage_collector;
}
inline std::vector<LabeledMultiInterval> DiscreteCoverageInstance::compute_multi_labeled_intervals_for_one_polygon(const std::vector<LabeledInterval>& labeled_intervals)
{
    if (labeled_intervals.empty()) return {};

    struct Event {
        double pos;
        unsigned antenna_id;
        bool is_start;
    };

    std::vector<Event> events;
    events.reserve(labeled_intervals.size() * 2);

    for (const auto& iv : labeled_intervals) {
        events.push_back({iv.start, static_cast<unsigned>(iv.antenna_id), true});
        events.push_back({iv.end, static_cast<unsigned>(iv.antenna_id), false});
    }

    // Sort by position; ends before starts on ties so [a,b] and [b,c] don't overlap
    std::sort(events.begin(), events.end(), [](const Event& a, const Event& b) {
        if (a.pos != b.pos) return a.pos < b.pos;
        return !a.is_start && b.is_start;
    });

    std::vector<LabeledMultiInterval> result;
    std::set<unsigned> active;
    double sweep_pos = 0.0;

    for (const auto& ev : events) {
        if (!active.empty() && ev.pos > sweep_pos) {
            result.emplace_back(
                sweep_pos,
                ev.pos,
                std::vector<unsigned>(active.begin(), active.end())
            );
        }

        sweep_pos = ev.pos;

        if (ev.is_start)
            active.insert(ev.antenna_id);
        else
            active.erase(ev.antenna_id);
    }

    return result;
}

inline std::pair<Point2, unsigned> DiscreteCoverageInstance::get_point_and_edge_id_from_interval_value(
    const double value, const unsigned polygon_id) {
    auto [edge_id, local_t] = get_position_on_edge_interval_for_interval_polygon_interval_value(value, polygon_id);
    return {visibility_geometry_.value().polygons[polygon_id].get_vertex_from_edge_id_and_edge_interval_value(edge_id, local_t),edge_id};
}

inline void DiscreteCoverageInstance::reset_and_refresh_internal_data_structures() {
    if (antenna_is_relevant_has_been_set) {
        throw std::runtime_error("new candidates should not be added after we decided on the relevant candidates, i.e., did candidate pruning");
    }
    std::cout<<"started resetting internal discrete coverage data structure"<<std::endl;
    multi_labeled_coverage_intervals_.clear();
    antenna_covers_polygons_.clear();
    polygon_covered_by_antennas_.clear();
    number_of_polygons=0;
    number_of_antennas=0;

    number_of_polygons = visibility_geometry_.value().polygons.size();
    number_of_antennas = visibility_geometry_.value().antennas.size();
    polygon_covered_by_antennas_.resize(number_of_polygons);
    antenna_covers_polygons_.resize(number_of_antennas);

    antenna_is_relevant.assign(number_of_antennas, true);
    build_antenna_coverage_from_tagged_edges(visibility_geometry_.value().antennas);
    generate_labeled_multi_intervals();
    std::cout<<"finished resetting internal discrete coverage data structure"<<std::endl;
}

inline std::vector<std::vector<LabeledMultiInterval>> const & DiscreteCoverageInstance::
get_multi_labeled_coverage_intervals() const {
    return multi_labeled_coverage_intervals_;
}

inline std::vector<unsigned> DiscreteCoverageInstance::get_multi_labeled_coverage_interval_size_histogram(bool write) const {
    unsigned max=0;
    for (auto& intervals:multi_labeled_coverage_intervals_) {
        for (auto& interval:intervals) {
            if (interval.antenna_ids.size()>max) max=interval.antenna_ids.size();
        }
    }
    std::vector<unsigned> histogram(max+1,0);
    for (auto& intervals:multi_labeled_coverage_intervals_) {
        for (auto& interval:intervals) {
            histogram[interval.antenna_ids.size()]++;
        }
    }
    if (write) {
        std::cout<<"multi labeled coverage interval size histogram"<<std::endl;
        unsigned count=0;
        for (unsigned i=0;i<histogram.size();i++) {
            std::cout<<i<<" "<<histogram[i]<<std::endl;
            count+=histogram[i];
        }
        std::cout<<"The instance has "<<count << " multi labeled coverage intervals"<<std::endl;
        std::cout<<"--------------------"<<std::endl;
    }

    return histogram;
}

inline unsigned DiscreteCoverageInstance::get_number_antennas() const {
    return number_of_antennas;
}

inline unsigned DiscreteCoverageInstance::get_number_polygons() const {
    return number_of_polygons;
}

inline std::vector<unsigned>  DiscreteCoverageInstance::get_relevant_antennas() const {
    std::vector<unsigned> relevant_antennas;
    if (antenna_covers_polygons().size()!=antenna_is_relevant.size()) {
        throw std::runtime_error("antenna_is_relevant is not the same size as antenna_covers_polygons");
    }
    for (unsigned i=0;i<antenna_is_relevant.size();i++) {
        if (antenna_is_relevant[i]) {
            relevant_antennas.push_back(i);
        }
    }
    return relevant_antennas;

}

inline void DiscreteCoverageInstance::prune_relevant_antenna_by_sum_coverage(double min_sum_coverage) {
    antenna_is_relevant_has_been_set=true;
    for (int i=0;i<antenna_covers_polygons_.size();i++) {
        auto antenna=antenna_covers_polygons_[i];
        double sum_coverage=0;
        for (auto pc:antenna) {
            for (auto iv:pc.intervals) {
                sum_coverage+=iv.length();
            }
        }
        if (sum_coverage<min_sum_coverage) {
            antenna_is_relevant[i]=false;
        }
    }
}

inline void DiscreteCoverageInstance::prune_relevant_antenna_by_min_seen_polygon(unsigned min_seen_polygon) {
    if (min_seen_polygon==0) {
        return;
    }
    int counter=0;
    antenna_is_relevant_has_been_set=true;
    for (int i=0;i<antenna_covers_polygons_.size();i++) {
        auto antenna=antenna_covers_polygons_[i];
        if (antenna.size()<min_seen_polygon) {
            antenna_is_relevant[i]=false;
            counter++;
        }
    }
    std::cout<<"pruned "<<counter<<" antennas"<<std::endl;
}

inline Interval DiscreteCoverageInstance::corrected_interval(const Interval inter, const unsigned polygon_id,
                                                             const unsigned edge_id) const {
    const double start=visibility_geometry_.value().polygons[polygon_id].start_interval_point[edge_id];
    const double portion=visibility_geometry_.value().polygons[polygon_id].interval_portion[edge_id];
    const double new_start=start+inter.start*portion;
    const double new_end=start+inter.end*portion;
    return {new_start,new_end};
}


inline std::pair<unsigned, double> DiscreteCoverageInstance::get_position_on_edge_interval_for_interval_polygon_interval_value(const double value,
    const unsigned polygon_id) {
    assert(value >= 0.0);
    assert(value <= 1.01);
    auto& polygon = visibility_geometry_.value().polygons[polygon_id];
    std::optional<unsigned> edge_id = std::nullopt;
    double best_start = -std::numeric_limits<double>::infinity();
    for (unsigned i = 0; i < polygon.start_interval_point.size(); ++i) {
        const double s = polygon.start_interval_point[i];
        if (s <= value && s > best_start) {
            best_start = s;
            edge_id = i;
        }
    }


    if (not edge_id) {
        std::cerr << "Warning: interval value smaller than 0 but it was handled correctly"<< std::endl;
        edge_id = 0;
    }

    const double start   = polygon.start_interval_point[edge_id.value()];
    const double portion = polygon.interval_portion[edge_id.value()];

    if (portion == 0.0) return {edge_id.value(), 0.0};

    double local_t = (value - start) / portion;

    if (local_t > 1.001 || local_t < -0.001) {
        std::cerr << std::setprecision(17)
                  << "Warning: edge interval value: "
                  << local_t
                  << " .. was out of interval bounds but it was clamped"
                  << std::endl;
    }
    local_t = std::max(0.0, std::min(1.0, local_t));

    return {edge_id.value(), local_t};
}

inline std::pair<unsigned, Interval> DiscreteCoverageInstance::get_edge_interval_from_polygon_interval(const Interval inter, const unsigned polygon_id) {
    auto [start_edge, local_start] = get_position_on_edge_interval_for_interval_polygon_interval_value(inter.start, polygon_id);
    auto [end_edge,   local_end]   = get_position_on_edge_interval_for_interval_polygon_interval_value(inter.end,   polygon_id);

    return {start_edge, {local_start, local_end}};
}

#endif //GISCUPBONN_COMBINATORIALINSTANCE_HPP