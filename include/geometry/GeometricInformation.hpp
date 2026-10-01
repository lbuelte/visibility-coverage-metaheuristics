//
// Created by philip on 6/3/26.
//

#ifndef GISCUPBONN_VISIBILITYINFORMATION_HPP
#define GISCUPBONN_VISIBILITYINFORMATION_HPP

#include <CGAL/Exact_predicates_exact_constructions_kernel.h>
#include <CGAL/Polygon_2.h>
#include <CGAL/Polygon_with_holes_2.h>

#include "GeoJsonLoader.hpp"
#include "Interval.hpp"

using Kernel      = CGAL::Exact_predicates_exact_constructions_kernel;
using Point2      = Kernel::Point_2;
using Poly2       = CGAL::Polygon_2<Kernel>;
using PolyWH      = CGAL::Polygon_with_holes_2<Kernel>;
using EdgeSegment = CGAL::Segment_2<Kernel>;


struct LabeledInterval {
    double start;
    double end;
    unsigned antenna_id;
    LabeledInterval(const double s, const double e, const unsigned antenna_id) : start(s), end(e) ,antenna_id(antenna_id) {}
    friend bool operator<(const LabeledInterval &l, const LabeledInterval &r) {
        return l.start < r.start;
    }
};

struct LabeledMultiInterval {
    double start{-1};
    double end{-1};
    std::vector<unsigned> antenna_ids;
    LabeledMultiInterval()=default;
    LabeledMultiInterval(const double s, const double e, const unsigned antenna_id) : start(s), end(e)  {
        antenna_ids.emplace_back(antenna_id);
    }
    LabeledMultiInterval(const double s, const double e, const std::vector<unsigned> & antenna_ids) : start(s), end(e), antenna_ids(antenna_ids) {
    }

    friend bool operator<(const LabeledMultiInterval &l, const LabeledMultiInterval &r) {
        return l.start < r.start;
    }
};


// A sub-edge of the visibility boundary, tagged with the obstacle edge it belongs to.
// The segment is often a strict sub-segment of the original polygon edge (poly_id, edge_id).
struct TaggedEdge {
    EdgeSegment segment;  // geometric sub-segment on the visibility boundary
    unsigned poly_id;          // index of the obstacle polygon containing this edge
    unsigned edge_id;          // index of the edge within that polygon
    Interval interval;    // interval on the edge (not on the polygon yet)

    TaggedEdge(const EdgeSegment &segment, unsigned poly_id, unsigned edge_id, Interval interval)
        : segment(segment), poly_id(poly_id), edge_id(edge_id),interval(interval) {}
};


// Stores a polygon's geometry and, for each edge, the list of antennas that can see it.
// Each entry in edge_is_visible_from_antennas is a (antenna_id, tagged_edge_index) pair.
// Note the list of antennas that can see it are only generated later
//start_interval_point is for vertex i the start value in the [0,1] representation of the polygon
//interval_portion how much of the interval it takes up
struct PolygonData {
    unsigned original_id = 0;

    Poly2 poly;
    double total_boundary_length = 0.0;
    std::vector<Point2> vertices;
    std::vector<std::pair<unsigned, unsigned>> edges;
    std::vector<std::vector<std::pair<unsigned, unsigned>>> edge_is_visible_from_antenna_at_visibility_edge;
    std::vector<double> start_interval_point;
    std::vector<double> interval_portion;

    explicit PolygonData(const PolygonWithInputId &p) : poly(p.polygon) {
        if (p.input_id < 0) {
            throw std::invalid_argument("PolygonData: negative input id (" + std::to_string(p.input_id) + ")");
        }
        original_id = p.input_id;
        vertices.assign(poly.vertices_begin(), poly.vertices_end());
        const unsigned n = (vertices.size());
        edges.reserve(n);
        for (unsigned i = 0; i < n; ++i)
            edges.emplace_back(i, (i + 1) % n);
        edge_is_visible_from_antenna_at_visibility_edge.resize(n);
        compute_interval_representation();
    }
    // explicit PolygonData(const Poly2 &p) : poly(p) {
    //     vertices.assign(p.vertices_begin(), p.vertices_end());
    //     const unsigned n = (vertices.size());
    //     edges.reserve(n);
    //     for (unsigned i = 0; i < n; ++i)
    //         edges.emplace_back(i, (i + 1) % n);
    //     edge_is_visible_from_antenna_at_visibility_edge.resize(n);
    //     compute_interval_representation();
    // }

    void compute_interval_representation() {
        const unsigned n = (vertices.size());

        if (n < 3)
            throw std::invalid_argument(
                "PolygonData: degenerate polygon (id " + std::to_string(original_id) +
                ") has only " + std::to_string(n) + " vertices");

        interval_portion.resize(n);
        start_interval_point.resize(n);
        std::vector<double> lengths(n);
        total_boundary_length = 0.0;

        for (unsigned i = 0; i < n; ++i) {
            const Point2 &a = vertices[i];
            const Point2 &b = vertices[(i + 1) % n];

            const double len = std::sqrt(CGAL::to_double(CGAL::squared_distance(a, b)));
            lengths[i] = len;
            total_boundary_length += len;
        }

        if (total_boundary_length == 0.0)
            throw std::invalid_argument(
                "PolygonData: zero boundary length (id " + std::to_string(original_id) + ")");

        double cumulative = 0.0;
        for (unsigned i = 0; i < n; ++i) {
            interval_portion[i] = lengths[i] / total_boundary_length;
            start_interval_point[i] = cumulative;
            cumulative += interval_portion[i];
        }
    }


    Point2 get_vertex_from_edge_id_and_edge_interval_value(unsigned edge_id, double percentage_on_edge_as_interval) {
        const Point2 &a = vertices[edge_id];
        const Point2 &b = vertices[(edge_id + 1) % vertices.size()];

        double t = std::clamp(percentage_on_edge_as_interval, 0.0, 1.0);

        return Point2(
            a.x() + t * (b.x() - a.x()),
            a.y() + t * (b.y() - a.y())
        );
    }
};

struct PointOnPolygon {
    Point2 position;
    unsigned polygon_id{std::numeric_limits<unsigned>::max()};
    unsigned edge_id{std::numeric_limits<unsigned>::max()};
    PointOnPolygon(const unsigned poly_id, const unsigned edge_id,const Point2 &p) : position(p), polygon_id(poly_id), edge_id(edge_id) {}
    PointOnPolygon()=default;
};

struct LabeledAntennaCandidate {
    unsigned polygon_id{std::numeric_limits<unsigned>::max()};
    unsigned edge_id{std::numeric_limits<unsigned>::max()};
    unsigned antenna_id{std::numeric_limits<unsigned>::max()};
    Point2 position;
    LabeledAntennaCandidate() = default;
    LabeledAntennaCandidate(const unsigned poly_id,const unsigned e_id,const  unsigned antenna_id, const Point2 &pos) : polygon_id(poly_id), edge_id(e_id), antenna_id(antenna_id), position(pos) {}
};

// Groups an antenna's position with the visibility edges visible from it together with indices on which input geometry they lie.
struct AntennaData {
    unsigned antenna_id;
    unsigned polygon_id;
    unsigned edge_id;
    Point2 position;
    std::vector<TaggedEdge> tagged_edges;
    AntennaData(const Point2 &p,  const std::vector<TaggedEdge> &tagged_edges,const unsigned antenna_id,const unsigned polygon_id, const unsigned edge_id)
        : antenna_id(antenna_id), polygon_id(polygon_id),edge_id(edge_id),position(p),tagged_edges(tagged_edges) {}
    AntennaData(const LabeledAntennaCandidate&  candidate,  const std::vector<TaggedEdge> &tagged_edges)
        : antenna_id(candidate.antenna_id),polygon_id(candidate.polygon_id),edge_id(candidate.edge_id),position(candidate.position), tagged_edges(tagged_edges) {}
};


#endif //GISCUPBONN_VISIBILITYINFORMATION_HPP
