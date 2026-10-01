#ifndef GISCUPBONN_GEOJSONLOADER_HPP
#define GISCUPBONN_GEOJSONLOADER_HPP

#include <CGAL/Exact_predicates_exact_constructions_kernel.h>
#include <CGAL/Polygon_2.h>
#include <CGAL/Polygon_with_holes_2.h>
#include <gdal_priv.h>
#include <ogrsf_frmts.h>

#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// ── Kernel & type aliases ────────────────────────────────────────────────────
// EPECK gives exact arithmetic — important for subsequent geometric operations.
// Swap for Simple_cartesian<double> if you only need fast approximate loading.
using Kernel = CGAL::Exact_predicates_exact_constructions_kernel;
using Point2 = Kernel::Point_2;
using Poly2 = CGAL::Polygon_2<Kernel>;
using PolyWH = CGAL::Polygon_with_holes_2<Kernel>;


struct PolygonWithInputId {
    Poly2 polygon;
    unsigned   input_id;
};

inline std::vector<PolygonWithInputId> load_geojson_polygons(const std::string &path) {
    std::ifstream ifs(path);
    if (!ifs)
        throw std::runtime_error("Could not open file: " + path);
    nlohmann::json root;
    ifs >> root;
    std::vector<PolygonWithInputId> polygons;
    unsigned feature_index = 0;

    for (const auto &feature : root.at("features")) {

        if (!feature.contains("geometry") || feature["geometry"].is_null())
            throw std::runtime_error("Feature at index " + std::to_string(feature_index) + " has no geometry");
        if (feature["geometry"].value("type", "") != "Polygon")
            throw std::runtime_error("Feature at index " + std::to_string(feature_index) + " is not a Polygon");

        const auto &ring = feature["geometry"]["coordinates"][0];
        PolygonWithInputId entry;
        if (!feature.contains("properties") || !feature["properties"].contains("id")) {
            throw std::runtime_error(
                "Feature at index " + std::to_string(feature_index) + " has no 'id' property");
        }
        const auto &id_val = feature["properties"]["id"];
        if (!id_val.is_number_integer()) {
            throw std::runtime_error(
                "Feature at index " + std::to_string(feature_index) +
                " has non-integer 'id': " + id_val.dump());
        }
        long long id_signed = id_val.get<long long>();
        if (id_signed < 0) {
            throw std::runtime_error(
                "Feature at index " + std::to_string(feature_index) +
                " has negative 'id': " + id_val.dump());
        }
        entry.input_id = static_cast<unsigned>(id_signed);

        // GeoJSON closes the ring by repeating the first vertex — skip it
        for (std::size_t i = 0; i + 1 < ring.size(); ++i)
            entry.polygon.push_back(Point2(ring[i][0].get<double>(), ring[i][1].get<double>()));

        polygons.push_back(std::move(entry));
        ++feature_index;
    }
    return polygons;
}

inline std::vector<PolygonWithInputId> load_geojson_polygons_without_original_ids(const std::string &path) {
    std::ifstream ifs(path);
    if (!ifs)
        throw std::runtime_error("Could not open file: " + path);
    nlohmann::json root;
    ifs >> root;
    std::vector<PolygonWithInputId> polygons;
    unsigned feature_index = 0;

    for (const auto &feature : root.at("features")) {

        if (!feature.contains("geometry") || feature["geometry"].is_null())
            throw std::runtime_error("Feature at index " + std::to_string(feature_index) + " has no geometry");
        if (feature["geometry"].value("type", "") != "Polygon")
            throw std::runtime_error("Feature at index " + std::to_string(feature_index) + " is not a Polygon");

        const auto &ring = feature["geometry"]["coordinates"][0];
        PolygonWithInputId entry;
        entry.input_id = static_cast<unsigned>(feature_index);

        // GeoJSON closes the ring by repeating the first vertex — skip it
        for (std::size_t i = 0; i + 1 < ring.size(); ++i)
            entry.polygon.push_back(Point2(ring[i][0].get<double>(), ring[i][1].get<double>()));

        polygons.push_back(std::move(entry));
        ++feature_index;
    }
    return polygons;
}

inline PolyWH make_bounding_box_with_holes(const std::vector<PolygonWithInputId> &polygons,
                                           double buffer = 50.0) {
    // ── 1. Compute bounding box over all polygons ─────────────────────────
    double xmin = std::numeric_limits<double>::max();
    double ymin = std::numeric_limits<double>::max();
    double xmax = std::numeric_limits<double>::lowest();
    double ymax = std::numeric_limits<double>::lowest();

    for (const auto &poly: polygons)
        for (auto vit = poly.polygon.vertices_begin(); vit != poly.polygon.vertices_end(); ++vit) {
            double x = CGAL::to_double(vit->x());
            double y = CGAL::to_double(vit->y());
            xmin = std::min(xmin, x);
            xmax = std::max(xmax, x);
            ymin = std::min(ymin, y);
            ymax = std::max(ymax, y);
        }

    // ── 2. Apply buffer ───────────────────────────────────────────────────
    xmin -= buffer;
    ymin -= buffer;
    xmax += buffer;
    ymax += buffer;

    // ── 3. Build outer boundary (CCW) ─────────────────────────────────────
    Poly2 outer;
    outer.push_back(Point2(xmin, ymin));
    outer.push_back(Point2(xmax, ymin));
    outer.push_back(Point2(xmax, ymax));
    outer.push_back(Point2(xmin, ymax));
    if (outer.is_clockwise_oriented())
        outer.reverse_orientation();

    // ── 4. Each input polygon becomes a hole (must be CW) ─────────────────
    std::vector<Poly2> holes;
    holes.reserve(polygons.size());
    for (auto &hole : polygons) {   // reference, not copy
        Poly2 p = hole.polygon;      // copy just the ring, only once
        if (p.is_counterclockwise_oriented())
            p.reverse_orientation();
        holes.push_back(std::move(p));
    }

    return PolyWH(outer, holes.begin(), holes.end());
}

#endif