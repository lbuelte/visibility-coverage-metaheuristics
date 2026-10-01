//
// Created by philip on 6/8/26.
//

#ifndef GISCUPBONN_GEOMETRYHELPER_HPP
#define GISCUPBONN_GEOMETRYHELPER_HPP

#include <CGAL/Exact_predicates_exact_constructions_kernel.h>
#include <CGAL/Boolean_set_operations_2.h>
#include <CGAL/Polygon_set_2.h>

#include "geometry/GeometricInformation.hpp"


using Kernel = CGAL::Exact_predicates_exact_constructions_kernel;
using Point2 = Kernel::Point_2;
using Poly2 = CGAL::Polygon_2<Kernel>;
using PolyWH = CGAL::Polygon_with_holes_2<Kernel>;
using ArrTraits   = CGAL::Arr_segment_traits_2<Kernel>;
using Arrangement = CGAL::Arrangement_2<ArrTraits>;


inline Poly2 arrangement_to_polygon(const Arrangement &arr) {
    for (auto fit = arr.faces_begin(); fit != arr.faces_end(); ++fit) {
        if (!fit->is_unbounded()) {
            Poly2 p;
            auto circ = fit->outer_ccb();
            auto curr = circ;
            do {
                p.push_back(curr->source()->point());
                ++curr;
            } while (curr != circ);
            return p;
        }
    }
    return {};
}

inline CGAL::Polygon_set_2<Kernel> union_of_arrangements(const std::vector<Arrangement> &arrangements) {
    CGAL::Polygon_set_2<Kernel> result;
    for (const auto &arr : arrangements) {
        Poly2 poly = arrangement_to_polygon(arr);
        if (!poly.is_empty())
            result.join(poly);
    }
    return result;
}

inline std::vector<EdgeSegment> geometric_union_of_tagged_edges(const std::vector<AntennaData> &antennas) {
    // insert all segments into an arrangement — CGAL splits and merges automatically
    Arrangement merged_arr;
    for (const auto &antenna : antennas)
        for (const auto &te : antenna.tagged_edges)
            CGAL::insert(merged_arr, EdgeSegment(te.segment.source(), te.segment.target()));
    // extract all halfedges (each undirected edge once)
    std::vector<EdgeSegment> result;
    for (auto eit = merged_arr.edges_begin(); eit != merged_arr.edges_end(); ++eit)
        result.emplace_back(eit->source()->point(), eit->target()->point());

    return result;
}


static double project_to_segment_fraction(const Point2& A1,
                                          const Point2& A2,
                                          const Point2& X)
{
    const double EPS = 1e-6;

    Kernel::Vector_2 v(A1, A2);
    Kernel::Vector_2 w(A1, X);

    double v_len2 = CGAL::to_double(v.squared_length());

    // Degenerate segment
    if (v_len2 <= EPS) {
        throw std::runtime_error("Degenerate segment in projection");
    }

    // projection parameter t (A1 + t*(A2-A1))
    double t = CGAL::to_double(v * w) / v_len2;

    // clamp to segment
    t = std::max(0.0, std::min(1.0, t));

    // compute actual projected point
    Point2 P = A1 + t * v;

    // verify X is extremely close to segment
    double dist2 = CGAL::to_double(CGAL::squared_distance(P, X));

    if (dist2 > EPS) {
        throw std::runtime_error("Point X is not on the segment (too far)");
    }

    return t;
}

inline bool same_antenna_set(const std::vector<unsigned>& a, const std::vector<unsigned>& b) {
    std::vector<unsigned> sa(a), sb(b);
    std::sort(sa.begin(), sa.end());
    std::sort(sb.begin(), sb.end());

    if (sa == sb)
        return true;

    std::cout << "Vectors differ.\n";
    std::cout << "Size of a: " << a.size() << "\n";
    std::cout << "Size of b: " << b.size() << "\n";

    std::vector<unsigned> only_in_a, only_in_b;

    std::set_difference(
        sa.begin(), sa.end(),
        sb.begin(), sb.end(),
        std::back_inserter(only_in_a));

    std::set_difference(
        sb.begin(), sb.end(),
        sa.begin(), sa.end(),
        std::back_inserter(only_in_b));

    std::cout << "Only in a (" << only_in_a.size() << "): ";
    for (unsigned x : only_in_a)
        std::cout << x << " ";
    std::cout << "\n";

    std::cout << "Only in b (" << only_in_b.size() << "): ";
    for (unsigned x : only_in_b)
        std::cout << x << " ";
    std::cout << "\n";

    return false;
}

inline std::pair<double, std::vector<Interval>> interval_union_coverage(std::vector<Interval> intervals) {
    if (intervals.empty()) return {0.0, {}};

    std::sort(intervals.begin(), intervals.end());

    std::vector<Interval> merged;
    double coverage = 0.0;
    double sweep     = intervals[0].start;
    double sweep_end = intervals[0].end;

    for (const auto &iv : intervals) {
        if (iv.start > sweep_end) {
            coverage += sweep_end - sweep;
            merged.emplace_back(sweep, sweep_end);
            sweep     = iv.start;
            sweep_end = iv.end;
        } else {
            sweep_end = std::max(sweep_end, iv.end);
        }
    }
    coverage += sweep_end - sweep;
    merged.emplace_back(sweep, sweep_end);

    return {coverage, merged};
}


#endif //GISCUPBONN_GEOMETRYHELPER_HPP