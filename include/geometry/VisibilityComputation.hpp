#ifndef GISCUPBONN_VISIBILITYCOMPUTATION_HPP
#define GISCUPBONN_VISIBILITYCOMPUTATION_HPP

#include <chrono>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <vector>
#include <CGAL/Exact_predicates_exact_constructions_kernel.h>
#include <CGAL/Polygon_2.h>
#include <CGAL/Polygon_with_holes_2.h>
#include <CGAL/Triangular_expansion_visibility_2.h>
#include <CGAL/Arrangement_2.h>
#include <CGAL/Arr_segment_traits_2.h>
#include <CGAL/Arr_landmarks_point_location.h>

#include <boost/geometry.hpp>
#include <boost/geometry/index/rtree.hpp>

#include <omp.h>
#include <random>

#include "GeoJsonLoader.hpp"
#include "geometry/GeometryHelper.hpp"
#include "geometry/GeometricInformation.hpp"

namespace bg  = boost::geometry;
namespace bgi = boost::geometry::index;

using Kernel      = CGAL::Exact_predicates_exact_constructions_kernel;
using Point2      = Kernel::Point_2;
using Poly2       = CGAL::Polygon_2<Kernel>;
using PolyWH      = CGAL::Polygon_with_holes_2<Kernel>;
using ArrTraits   = CGAL::Arr_segment_traits_2<Kernel>;
using Arrangement = CGAL::Arrangement_2<ArrTraits>;
using Segment     = ArrTraits::X_monotone_curve_2;
using TEV         = CGAL::Triangular_expansion_visibility_2<Arrangement>;
using LocResult   = CGAL::Arr_point_location_result<Arrangement>::Type;
using BGPoint     = bg::model::point<double, 2, bg::cs::cartesian>;
using BGBox       = bg::model::box<BGPoint>;
using RTreeEntry  = std::pair<BGBox, std::size_t>;
using RTree       = bgi::rtree<RTreeEntry, bgi::rstar<16>>;

class DiscreteCoverageInstance;

class VisibilityComputation {
public:
    VisibilityComputation(const DiscreteCoverageInstance &instance);


    explicit VisibilityComputation(const std::vector<PolygonWithInputId> &labeled_polygons);

    ~VisibilityComputation() {
        point_location.reset();
        visibility_data_structure.reset();
        antennas_vis_arrangements.clear();
        antennas.clear();
        polygons.clear();
    }


    //computes the visibility for the candidate set given by the vertices of the polygons
    void run_with_vertex_candidates();

    //Computes the visibility for the given candidate set
    //The labels must be set correctly: the label of candidate[i] is antenna.size()+i
    void run_with_custom_candidates(
        const std::vector<PointOnPolygon> &candidates);

    //take all vertex candidate if > n randomly removes candidates until size is n
    // if < n enrich via bisection of the longest edges
    void run_with_exactly_n_vertex_based_candidates(int n);

    std::vector<PointOnPolygon> generate_n_candidates_via_bisection(int nr_new_candidates) const;

    std::vector<PolygonData> polygons;
    std::vector<AntennaData> antennas;
    std::vector<Arrangement> antennas_vis_arrangements;

private:
    bool not_correctly_initialized = false;

    double      max_distance = -1;
    Poly2 bounding_box;
    Arrangement arr;
    Arrangement::Face_const_handle free_face;
    std::vector<TaggedEdge> all_tagged;
    RTree polygon_edge_rtree;
    std::optional<TEV> visibility_data_structure;
    std::optional<CGAL::Arr_landmarks_point_location<Arrangement>> point_location;

    // Builds the CGAL arrangement from all input segments, populates the
    // R-tree with tagged polygon edges, and initializes the visibility engine
    // and point-location structure. The bounding box is inserted first
    // (without updating max_distance) so that max_distance reflects only
    // obstacle edge lengths.
    void precompute();

    //for newly added antennas tell each polygon which antenna can see it with which tagged edges
    void update_polygons_with_antennas_that_can_see_it(const int number_of_already_processed_antennas);

    // Processes a single antenna candidate: computes its visibility region,
    // tags the boundary edges with polygon/edge indices and intervals, and
    // immediately updates the backward polygon->antenna mapping.
    std::pair<AntennaData, Arrangement> process_labeled_antenna_candidate(const LabeledAntennaCandidate &candidate) const;

    // Walks the outer boundary of the visibility face and tags each edge that
    // borders the unbounded face of vis (i.e. a real obstacle boundary edge,
    // not an artificial cut introduced by TEV). Artificial edges and bounding
    // box edges that slip through are naturally rejected by match_halfedge.
    std::vector<TaggedEdge> tag_visibility_region(
        const Arrangement &vis, Arrangement::Face_const_handle fh) const;

    // Identifies which original obstacle edge contains the sub-edge (a, b).
    // Uses the midpoint to query the R-tree for nearby candidates, then
    // verifies via exact collinearity that both endpoints lie on the segment.
    // Returns nullptr for bounding box edges (not in the R-tree) and for
    // edges longer than any obstacle edge (max_distance early-out).
    const TaggedEdge *match_halfedge(const Point2 &a, const Point2 &b) const;

    // All polygon vertices as antenna candidates.
    std::vector<PointOnPolygon> collect_vertex_candidates() const;


    //enriches the candidates vector in-place via binary bisection of long edges
    void enrich_to_n_candidates_via_bisection(std::vector<PointOnPolygon> & candidates, int n) const;

    //clamps a to large candidate vector to exactly n candidates
    static void clamp_to_n_candidates(std::vector<PointOnPolygon> & candidates,int n );

    //collects vertex candidates and then either clamps or enriches them depending on size and n
    std::vector<PointOnPolygon> collect_vertex_candidates_and_either_clamp_or_enrich_to_n_candidates(int n) const;

    // Locates the query point in the arrangement and dispatches to the
    // appropriate TEV overload. Ensures the adjacent face is free space before
    // calling compute_visibility, since querying from inside an obstacle
    // would produce a meaningless result.
    std::optional<Arrangement::Face_const_handle> compute_visibility_region_for_query_point(
        const Point2 &p, Arrangement &vis) const;

    // Appends polygon edges to segments and tracks the longest squared edge
    // length (scaled by 1.05 as a safety margin for the max_distance filter).
    void collect_segments(std::vector<Segment> &segments, const Poly2 &poly);

    //given a candidate set with unique candidates
    //outputs a reduced set of candidates where
    //all candidates with distance smaller than eps*max_overall_edge_length
    //to an existing antenna are pruned
    std::vector<PointOnPolygon> compute_filtered_candidate_set_epsilon_distance_to_existing_antennas(
    const std::vector<PointOnPolygon>& candidates,
    double epsilon) const;

    // The free face is the region between the bounding box and the obstacles.
    // It is identified as the bounded face adjacent to the unbounded face,
    // i.e. the face "inside" the bounding box but "outside" all obstacles.
    static Arrangement::Face_const_handle find_free_face(const Arrangement &arr);

    //given a candidate set
    //return a pruned candidate set where for candidates
    //with distance smaller than eps*max_overall_edge_length only on is retained
    std::vector<PointOnPolygon> compute_filtered_candidate_set_unique_with_epsilon(const std::vector<PointOnPolygon>& candidates, double epsilon);

    static BGPoint to_bg(const Point2 &p);

    static BGBox to_box(const Point2 &a, const Point2 &b);
};

#endif //GISCUPBONN_VISIBILITYCOMPUTATION_HPP
