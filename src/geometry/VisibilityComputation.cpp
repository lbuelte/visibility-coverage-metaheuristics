//
// Created by philip on 6/1/26.
//

#include "geometry/VisibilityComputation.hpp"
#include "geometry/DiscreteCoverageInstance.hpp"

namespace {
    void print_ms(const char *name,
                  std::chrono::high_resolution_clock::time_point s,
                  std::chrono::high_resolution_clock::time_point e) {
        std::cout << "[timing] " << name << ": "
                  << std::chrono::duration_cast<std::chrono::milliseconds>(e - s).count()
                  << " ms\n";
    }
}

VisibilityComputation::VisibilityComputation(const DiscreteCoverageInstance &instance) {
    not_correctly_initialized=true;
    // ---- 1. Copy polygon geometry. Visibility bookkeeping references old
    //         antenna ids, so clear it here; rebuilt in step 3 with new ids. ----
    const unsigned num_polygons = instance.get_number_polygons();
    polygons.reserve(num_polygons);
    for (unsigned p = 0; p < num_polygons; ++p) {
        PolygonData pd = instance.get_polygon_data(p); // copy
        for (auto &edge_list : pd.edge_is_visible_from_antenna_at_visibility_edge)
            edge_list.clear();
        polygons.push_back(std::move(pd));
    }

    // ---- 2. Copy only the relevant antennas + their arrangements,
    //         reindexed to a dense [0, relevant.size()) range. ----
    const std::vector<unsigned> relevant = instance.get_relevant_antennas();
    antennas.reserve(relevant.size());
    antennas_vis_arrangements.reserve(relevant.size());

    for (unsigned new_id = 0; new_id < relevant.size(); ++new_id) {
        const unsigned old_id = relevant[new_id];

        AntennaData ad = instance.get_antenna_data(old_id); // copy
        ad.antenna_id  = new_id;                             // reindex

        Arrangement vis_copy = instance.get_arrangement_for_antenna(old_id); // deep copy

        antennas.push_back(std::move(ad));
        antennas_vis_arrangements.push_back(std::move(vis_copy));
    }

    // ---- 3. Rebuild polygon -> antenna visibility bookkeeping with the
    //         new antenna ids (mirrors update_polygons_with_antennas_that_can_see_it). ----
    for (unsigned new_id = 0; new_id < antennas.size(); ++new_id) {
        const auto &tagged_edges = antennas[new_id].tagged_edges;
        for (unsigned j = 0; j < tagged_edges.size(); ++j) {
            const auto &te = tagged_edges[j];
            polygons[te.poly_id]
                    .edge_is_visible_from_antenna_at_visibility_edge[te.edge_id]
                    .emplace_back(new_id, j);
        }
    }

    // max_distance, bounding_box, arr, free_face, all_tagged, polygon_edge_rtree,
    // visibility_data_structure, point_location are intentionally left
    // default-initialized. See header comment for why and what that means.
}

VisibilityComputation::VisibilityComputation(const std::vector<PolygonWithInputId> &labeled_polygons) {
    auto pwh=make_bounding_box_with_holes(labeled_polygons);
    bounding_box       = pwh.outer_boundary();
    for (const auto &poly : labeled_polygons) {
        polygons.emplace_back(poly);
    }
    precompute();
}



std::vector<PointOnPolygon> VisibilityComputation::compute_filtered_candidate_set_unique_with_epsilon(const std::vector<PointOnPolygon>& candidates,double epsilon) {
    namespace bgi = boost::geometry::index;
    using Value = std::pair<BGPoint, std::size_t>;

    bgi::rtree<Value, bgi::quadratic<16>> rtree;

    const double relative_epsilon = epsilon *sqrt(max_distance);
    const double eps2 = relative_epsilon * relative_epsilon;
    std::vector<PointOnPolygon> filtered;
    filtered.reserve(candidates.size());

    for (const auto& candidate : candidates)
    {
        const double x = CGAL::to_double(candidate.position.x());
        const double y = CGAL::to_double(candidate.position.y());

        BGBox query_box{
            BGPoint{x - relative_epsilon, y - relative_epsilon},
            BGPoint{x + relative_epsilon, y + relative_epsilon}
        };

        std::vector<Value> nearby;
        rtree.query(
            bgi::intersects(query_box),
            std::back_inserter(nearby));

        bool keep = true;

        for (const auto& [pt, _] : nearby)
        {
            const double dx = pt.get<0>() - x;
            const double dy = pt.get<1>() - y;

            if (dx * dx + dy * dy < eps2)
            {
                keep = false;
                break;
            }
        }

        if (keep)
        {
            filtered.push_back(candidate);
            rtree.insert({BGPoint{x, y}, filtered.size() - 1});
        }
    }
    return filtered;
}

std::vector<PointOnPolygon> VisibilityComputation::compute_filtered_candidate_set_epsilon_distance_to_existing_antennas(
    const std::vector<PointOnPolygon>& candidates,
    double epsilon) const
{
    namespace bgi = boost::geometry::index;
    using Value = std::pair<BGPoint, std::size_t>;
    bgi::rtree<Value, bgi::quadratic<16>> rtree;

    // Build R-tree from existing antennas
    for (std::size_t i = 0; i < antennas.size(); ++i)
    {
        const auto& p = antennas[i].position;

        rtree.insert({
            BGPoint{
                CGAL::to_double(p.x()),
                CGAL::to_double(p.y())
            },
            i
        });
    }
    const double relative_epsilon = epsilon *sqrt(max_distance);
    const double eps2 = relative_epsilon * relative_epsilon;

    std::vector<PointOnPolygon> filtered;
    filtered.reserve(candidates.size());

    for (const auto& candidate : candidates)
    {
        const double x = CGAL::to_double(candidate.position.x());
        const double y = CGAL::to_double(candidate.position.y());

        BGBox query_box{
            BGPoint{x - relative_epsilon, y - relative_epsilon},
            BGPoint{x + relative_epsilon, y + relative_epsilon}
        };
        std::vector<Value> nearby;
        rtree.query(
            bgi::intersects(query_box),
            std::back_inserter(nearby));

        bool keep = true;
        for (const auto& [pt, _] : nearby)
        {
            const double dx = pt.get<0>() - x;
            const double dy = pt.get<1>() - y;

            if (dx * dx + dy * dy < eps2)
            {
                keep = false;
                break;
            }
        }
        if (keep)
            filtered.push_back(candidate);
    }
    return filtered;
}


void VisibilityComputation::run_with_vertex_candidates() {
    if (not_correctly_initialized) {
        throw std::logic_error("VisibilityComputation::run_with_vertex_candidates() called on a corrupted instance.");
    }
    run_with_custom_candidates(collect_vertex_candidates());
}

void VisibilityComputation::run_with_exactly_n_vertex_based_candidates(int n) {
    if (not_correctly_initialized) {
        throw std::logic_error("VisibilityComputation::run_with_vertex_candidates() called on a corrupted instance.");
    }
    auto candidates = collect_vertex_candidates_and_either_clamp_or_enrich_to_n_candidates(n);
    run_with_custom_candidates(candidates);
}


void VisibilityComputation::run_with_custom_candidates(const std::vector<PointOnPolygon> &candidates) {
    if (not_correctly_initialized) {
        throw std::logic_error("VisibilityComputation::run_with_vertex_candidates() called on a corrupted instance.");
    }
    omp_set_num_threads(1);
    using clock = std::chrono::high_resolution_clock;
    auto t_start = clock::now();

    double eps=1e-6;
    std::cout << "Started with " << candidates.size() << " candidates...";
    auto unique_candidates=compute_filtered_candidate_set_unique_with_epsilon(candidates,eps);
    auto unique_and_none_duplicate_candidates=compute_filtered_candidate_set_epsilon_distance_to_existing_antennas(unique_candidates,eps);


    std::cout << "Running visibility queries for " << unique_and_none_duplicate_candidates.size()<< " candidates with " << omp_get_max_threads() << " threads...\n";
    const unsigned number_of_already_processed_antennas = static_cast<unsigned>(antennas.size());

    unsigned id_to_set = number_of_already_processed_antennas;
    std::vector<LabeledAntennaCandidate> labeled_candidates_to_process;
    for (auto & c: unique_and_none_duplicate_candidates ) {
        labeled_candidates_to_process.emplace_back(c.polygon_id,c.edge_id,id_to_set,c.position);
        id_to_set++;
    }

    std::vector<std::unique_ptr<std::pair<AntennaData, Arrangement>>> resulting_antennas_with_arrangements(candidates.size());
    #pragma omp parallel for schedule(dynamic)
    for (int i = 0; i < static_cast<int>(labeled_candidates_to_process.size()); ++i) {
        try {
            resulting_antennas_with_arrangements[i] =
                std::make_unique<std::pair<AntennaData, Arrangement>>(
                    process_labeled_antenna_candidate(labeled_candidates_to_process[i]));
        } catch (const std::exception &e) {
            #pragma omp critical
            std::cerr << "Error processing candidate " << i << ": " << e.what() << "\n";
        }
    }

    for (const auto &ptr : resulting_antennas_with_arrangements) {
        if (!ptr) continue;
        antennas.emplace_back(std::move(ptr->first));
        antennas_vis_arrangements.push_back(std::move(ptr->second));
    }

    update_polygons_with_antennas_that_can_see_it(static_cast<int>(number_of_already_processed_antennas));
    print_ms("visbility queries", t_start, clock::now());

    for (size_t i = 0; i < antennas.size(); ++i) {
        if (antennas[i].antenna_id != static_cast<int>(i)) {
            throw std::logic_error(
                "candidate at index " + std::to_string(i) +
                " has antenna_id " + std::to_string(antennas[i].antenna_id) +
                " (expected " + std::to_string(i) + ")");
        }
    }
    std::cout<<"current antenna_size: "<<antennas.size()<<std::endl;
}

void VisibilityComputation::precompute() {
    std::cout << "Precomputing visibility information...   ";
    std::vector<Segment> segments;

    collect_segments(segments, bounding_box);
    max_distance = -1; // reset: bounding box edges must not influence the threshold
    for (const auto &pd : polygons)
        collect_segments(segments, pd.poly);

    CGAL::insert(arr, segments.begin(), segments.end());
    free_face = find_free_face(arr);

    // Populate the R-tree with every obstacle edge, tagged with its
    // polygon and edge index for spatial lookup during tagging.
    std::size_t poly_id = 0;
    for (const auto &pd : polygons) {
        const int n = (int)pd.vertices.size();
        for (int edge_id = 0; edge_id < n; ++edge_id) {
            const Point2 &src = pd.vertices[edge_id];
            const Point2 &tgt = pd.vertices[(edge_id + 1) % n];
            all_tagged.emplace_back(EdgeSegment(src, tgt), static_cast<int>(poly_id), edge_id, Interval(0, 0.1));
            polygon_edge_rtree.insert({to_box(src, tgt), all_tagged.size() - 1});
        }
        ++poly_id;
    }
    visibility_data_structure.emplace(arr);
    point_location.emplace(arr);
    std::cout << "DONE.\n";
}

void VisibilityComputation::update_polygons_with_antennas_that_can_see_it(const int number_of_already_processed_antennas) {
    for (int i=number_of_already_processed_antennas;i<antennas.size();i++) {
        auto antenna_id=antennas[i].antenna_id;
        auto ad=antennas[i].tagged_edges;
        for (int j = 0; j < static_cast<int>(ad.size()); ++j) {
            const auto &te = ad[j];
            polygons[te.poly_id]
                    .edge_is_visible_from_antenna_at_visibility_edge[te.edge_id]
                    .emplace_back(antenna_id, j);
        }
    }
}

std::pair<AntennaData, Arrangement> VisibilityComputation::process_labeled_antenna_candidate(
    const LabeledAntennaCandidate &candidate) const {
    Arrangement vis;
    auto p = candidate.position;
    auto antenna_id = candidate.antenna_id;

    
    const auto fh = compute_visibility_region_for_query_point(p, vis);
    if (!fh) {
        AntennaData dummy_antenna(p, {}, antenna_id, -1, -1);
        std::cerr << "Warning: missing free-space element for query point — creates a trivial dummy antenna (non-fatal).\n";
        return {dummy_antenna, {}};
    }
    auto ad = tag_visibility_region(vis, *fh);
    return {AntennaData(candidate, ad), vis};
}

std::vector<TaggedEdge> VisibilityComputation::tag_visibility_region(const Arrangement &vis,
    Arrangement::Face_const_handle fh) const {
    std::vector<TaggedEdge> ad;
    auto curr = fh->outer_ccb();
    do {
        if (curr->twin()->face() == vis.unbounded_face()) {
            const Point2 &a = curr->source()->point();
            const Point2 &b = curr->target()->point();
            const TaggedEdge *te = match_halfedge(a, b);
            if (te) {
                int poly_id  = te->poly_id;
                int edge_id  = te->edge_id;
                const Point2 &vert_1 = polygons[poly_id].vertices[edge_id];
                const Point2 &vert_2 = polygons[poly_id].vertices[
                    (edge_id + 1) % (int)polygons[poly_id].vertices.size()];

                double t1 = project_to_segment_fraction(vert_1, vert_2, a);
                double t2 = project_to_segment_fraction(vert_1, vert_2, b);
                const double start= std::max(0.0, std::min(t1, t2));
                const double end=std::min(1.0, std::max(t1, t2));

                //this  just ensures that the interval is not extremely tiny which can lead to
                //intervals of size 0 which are sometimes problematic
                //this is safe, our reported solutions could only be better with them
                if (std::abs(start-end)>1e-12) {
                    Interval interval(start,end);
                    ad.emplace_back(EdgeSegment(a, b), poly_id, edge_id, interval);
                }
            }
        }
        ++curr;
    } while (curr != fh->outer_ccb());
    return ad;
}

const TaggedEdge * VisibilityComputation::match_halfedge(const Point2 &a, const Point2 &b) const {
    if (CGAL::squared_distance(a, b) > max_distance)
        return nullptr;

    const Point2     mid = CGAL::midpoint(a, b);
    const double     mx  = CGAL::to_double(mid.x());
    const double     my  = CGAL::to_double(mid.y());
    constexpr double eps = 1e-4;

    std::vector<RTreeEntry> candidates;
    polygon_edge_rtree.query(
        bgi::intersects(BGBox{{mx - eps, my - eps}, {mx + eps, my + eps}}),
        std::back_inserter(candidates));

    for (const auto &[box, idx] : candidates) {
        const TaggedEdge &te = all_tagged[idx];
        if (CGAL::collinear(te.segment.source(), te.segment.target(), a) &&
            CGAL::collinear(te.segment.source(), te.segment.target(), b) &&
            te.segment.collinear_has_on(a) &&
            te.segment.collinear_has_on(b))
            return &te;
    }
    return nullptr;
}

std::vector<PointOnPolygon> VisibilityComputation::collect_vertex_candidates() const {
    std::vector<PointOnPolygon> candidates;
    int antenna_id_counter=0;
    for (int i=0;i<polygons.size();i++) {
        for (int j=0;j<polygons[i].vertices.size();j++) {
            candidates.emplace_back(i,j,polygons[i].vertices[j]);
            antenna_id_counter++;
        }
    }
    return candidates;
}


std::vector<PointOnPolygon> VisibilityComputation::generate_n_candidates_via_bisection(int nr_new_candidates) const {
    if (not_correctly_initialized) {
        throw std::logic_error("VisibilityComputation::run_with_vertex_candidates() called on a corrupted instance.");
    }

    struct InformedSegment {
        int polygon_id;
        int edge_id;
        EdgeSegment edge;
        double length;
        InformedSegment(int polygon_id, int edge_id, const EdgeSegment &edge) :
            polygon_id(polygon_id), edge_id(edge_id), edge(edge),
            length(sqrt(CGAL::to_double(edge.squared_length()))) {}
        InformedSegment(int polygon_id, int edge_id, const EdgeSegment &edge, double length) :
            polygon_id(polygon_id), edge_id(edge_id), edge(edge), length(length) {}
    };

    std::vector<PointOnPolygon> new_candidates;

    std::vector<InformedSegment> current_segments;
    for (int i = 0; i < static_cast<int>(polygons.size()); i++) {
        for (int j = 0; j < static_cast<int>(polygons[i].vertices.size()); j++) {
            auto v1 = polygons[i].vertices[j];
            auto v2 = polygons[i].vertices[(j + 1) % polygons[i].vertices.size()];
            current_segments.emplace_back(i, j, EdgeSegment(v1, v2));
        }
    }

    auto by_length_asc = [](const InformedSegment &a, const InformedSegment &b) {
        return a.length < b.length;
    };
    std::sort(current_segments.begin(), current_segments.end(), by_length_asc);
    const int batch_size = 1000;
    while (static_cast<int>(new_candidates.size()) < nr_new_candidates && !current_segments.empty()) {
        int clamped_batch_size = std::min(batch_size, static_cast<int>(current_segments.size()));

        // take the last batch_size many segments, remove them and add their bisection, additionally add the midpoint as a candidate
        std::vector<InformedSegment> batch(current_segments.end() - clamped_batch_size, current_segments.end());
        current_segments.erase(current_segments.end() - clamped_batch_size, current_segments.end());
        for (auto &seg : batch) {
            if (static_cast<int>(new_candidates.size()) >= nr_new_candidates) break;
            Point2 midpoint = CGAL::midpoint(seg.edge.source(), seg.edge.target());
            new_candidates.emplace_back(seg.polygon_id, seg.edge_id, midpoint);
            double half_length = seg.length / 2.0;
            current_segments.emplace_back(seg.polygon_id, seg.edge_id,
                                          EdgeSegment(seg.edge.source(), midpoint), half_length);
            current_segments.emplace_back(seg.polygon_id, seg.edge_id,
                                          EdgeSegment(midpoint, seg.edge.target()), half_length);
        }
        std::sort(current_segments.begin(), current_segments.end(), by_length_asc);
    }

    return new_candidates;
}

void VisibilityComputation::enrich_to_n_candidates_via_bisection(std::vector<PointOnPolygon> &candidates,
    int n) const {
    int nr_new_candidates = n - static_cast<int>(candidates.size());
    if (nr_new_candidates <= 0) return;

    std::vector<PointOnPolygon> new_candidates = generate_n_candidates_via_bisection(nr_new_candidates);
    candidates.insert(candidates.end(), new_candidates.begin(), new_candidates.end());
}

void VisibilityComputation::clamp_to_n_candidates(std::vector<PointOnPolygon> &candidates, int n) {
    if (static_cast<int>(candidates.size()) > n) {
        std::mt19937 rng(42);
        std::shuffle(candidates.begin(), candidates.end(), rng);
        candidates.resize(n);
    }
}

std::vector<PointOnPolygon> VisibilityComputation::
collect_vertex_candidates_and_either_clamp_or_enrich_to_n_candidates(int n) const {
    auto candidates = collect_vertex_candidates();
    std::cout << "\033[34m" << "n=" << n
            << ", candidates=" << candidates.size()
            << "\033[0m" << std::endl;

    if (n == static_cast<int>(candidates.size())) {
        std::cout << "\033[34m" << "Note: candidates where as expected"
                << " (n=" << n << ", candidates" << candidates.size() << ")"
                << "\033[0m" << std::endl;
    }
    else if (n > 0 && n > static_cast<int>(candidates.size())) {
        std::cout << "\033[34m" << "Note: n is larger than the number of candidates, enriching..."
                << " (n=" << n << ", candidates=" << candidates.size() << ")"
                << "\033[0m\n";
        enrich_to_n_candidates_via_bisection(candidates, n);
    }
    else if (n > 0 && n < static_cast<int>(candidates.size())) {
        std::cout << "\033[34m" << "Note: n is smaller than the number of candidates, clamping..."
                << " (n=" << n << ", candidates.size()=" << candidates.size() << ")"
                << "\033[0m\n";
        clamp_to_n_candidates(candidates, n);
    }

    return candidates;

}

std::optional<Arrangement::Face_const_handle> VisibilityComputation::compute_visibility_region_for_query_point(
    const Point2 &p, Arrangement &vis) const {
    LocResult loc = point_location->locate(p);
    if (auto *vh = std::get_if<Arrangement::Vertex_const_handle>(&loc)) {
        auto circ = (*vh)->incident_halfedges();
        auto curr = circ;
        do {
            if (curr->face() == free_face)
                return visibility_data_structure->compute_visibility(p, curr, vis);
            ++curr;
        } while (curr != circ);
    } else if (auto *he = std::get_if<Arrangement::Halfedge_const_handle>(&loc)) {
        if ((*he)->face() == free_face)
            return visibility_data_structure->compute_visibility(p, *he, vis);
        if ((*he)->twin()->face() == free_face)
            return visibility_data_structure->compute_visibility(p, (*he)->twin(), vis);
    } else if (auto *fh = std::get_if<Arrangement::Face_const_handle>(&loc)) {
        std::cerr << "Candidate was not on an edge or vertex but we just ignore it\n";
            return std::nullopt;

    }
    return std::nullopt;
}

void VisibilityComputation::collect_segments(std::vector<Segment> &segments, const Poly2 &poly) {
    auto curr = poly.vertices_begin();
    auto next = std::next(curr);
    for (; next != poly.vertices_end(); ++curr, ++next) {
        double d = 1.05 * CGAL::to_double(CGAL::squared_distance(*curr, *next));
        if (d > max_distance) max_distance = d;
        segments.emplace_back(*curr, *next);
    }
    segments.emplace_back(*curr, *poly.vertices_begin());
}

Arrangement::Face_const_handle VisibilityComputation::find_free_face(const Arrangement &arr) {
    for (auto hit = arr.unbounded_face()->holes_begin();
         hit != arr.unbounded_face()->holes_end(); ++hit) {
        auto face = (*hit)->twin()->face();
        if (!face->is_unbounded()) return face;
    }
    throw std::runtime_error("find_free_face: no bounded face adjacent to unbounded face.");
}

BGPoint VisibilityComputation::to_bg(const Point2 &p) {
    return {CGAL::to_double(p.x()), CGAL::to_double(p.y())};
}

BGBox VisibilityComputation::to_box(const Point2 &a, const Point2 &b) {
    auto pa = to_bg(a), pb = to_bg(b);
    return {
        BGPoint{std::min(pa.get<0>(), pb.get<0>()), std::min(pa.get<1>(), pb.get<1>())},
        BGPoint{std::max(pa.get<0>(), pb.get<0>()), std::max(pa.get<1>(), pb.get<1>())}
    };
}
