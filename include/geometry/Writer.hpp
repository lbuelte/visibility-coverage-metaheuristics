//
// Created by philip on 6/9/26.
//

#ifndef GISCUPBONN_WRITER_HPP
#define GISCUPBONN_WRITER_HPP

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

#include "geometry/VisibilityComputation.hpp"

// ── Kernel & type aliases ────────────────────────────────────────────────────
// EPECK gives exact arithmetic — important for subsequent geometric operations.
// Swap for Simple_cartesian<double> if you only need fast approximate loading.
using Kernel = CGAL::Exact_predicates_exact_constructions_kernel;
using Point2 = Kernel::Point_2;
using Poly2 = CGAL::Polygon_2<Kernel>;
using PolyWH = CGAL::Polygon_with_holes_2<Kernel>;


//writes geopackage with polygons (layer name does not matter)
void write_gpkg(const std::vector<Poly2> &polygons,
                       const std::string &out_path,
                       const std::string &layer_name = "polygons",
                       int epsg = 32611);

//writes geopackage with one polygon with holes (layer name does not matter)
void write_gpkg_pwh(const PolyWH &pwh,
                    const std::string &out_path,
                    const std::string &layer_name = "polygon_with_holes",
                    int epsg = 32611);

//given a vector of visibility arrangements writes a geopackage with there union
//this is expensive, but otherwise the visualization side gets super slow
void write_visibility_regions_unified(
    const std::vector<Arrangement> &regions,
    const std::string &out_path,
    int epsg = 32611);

//writes individual geopackage for every arrangement
void write_visibility_regions(
    const std::vector<Arrangement> &regions,
    const std::string &out_folder,
    int epsg = 32611);

//For each antenna:
//One geopackage with the antenna position
//One geopackage with the visible polygon edges
void write_antenna_data(const std::vector<AntennaData> &antennas,
                        const std::string &out_folder,
                        int epsg = 32611);

//One Geopackage with all the edgesgements
void write_edges_gpkg(const std::vector<EdgeSegment>& edges,
                      const std::string& out_path,
                      int epsg = 32611);

//One Geopackage with exactly one edge
void write_single_edge_gpkg(const EdgeSegment& seg,
                            const std::string& out_path,
                            int epsg = 32611);

//One Geopackage with all the polygons
void write_polygons_gpkg(const std::vector<Poly2>& polygons,
                         const std::string& out_path,
                         int epsg = 32611);

//One Geopackage with exactly on polygon
void write_single_polygon_gpkg(const Poly2& poly,
                               const std::string& out_path,
                               int epsg = 32611);

//Given vector of Antenna Data writes one geopackage with all edges of all antennas
void write_tagged_edges_gpkg(const std::vector<AntennaData> &antennas,
                             const std::string &out_path,
                             int epsg = 32611);

//Given vector of Antenna Data writes one geopackage with all (unique) edges of all antennas
void write_tagged_edges_union_gpkg(const std::vector<AntennaData> &antennas,
                                   const std::string &out_path,
                                   int epsg = 32611);

//writes one geopackage with the polygons that are labeled with their coverage (needs to be enabled in qgis)
void write_polygons_with_coverage_label(
    const std::vector<std::pair<Poly2, double>> &polygons_with_coverage,
    const std::string &out_path,
    int epsg = 32611);

//writes a solution given by the antennas, the covered polygons and uncovered polygons with coverage respectively
// 5 files :antenna position, unified visibility, unified edges visible on polygons, polygons covered, polygons uncovered
void write_solution(
    const std::vector<std::pair<AntennaData, Arrangement>> &antenna_and_arrangements,
    const std::vector<std::pair<Poly2, double>> &covered,
    const std::vector<std::pair<Poly2, double>> &uncovered,
    const std::string &out_folder,
    int epsg = 32611);


// Writes antenna positions to a GeoJSON file, one point feature per antenna,
// with an "antenna_id" integer field set to its index in the antennas vector.
inline void write_antenna_position_vector(const std::vector<AntennaData> &antennas,
                                       const std::string &output_path,int epsg = 32611) {
    GDALAllRegister();

    GDALDriver *driver = GetGDALDriverManager()->GetDriverByName("GeoJSON");
    if (!driver) {
        throw std::runtime_error("write_antennas_to_geojson: GeoJSON driver not available");
    }
    // ── 7. Antenna positions ──────────────────────────────────────────────
    {
        GDALAllRegister();
        std::string out_path = output_path ;
        if (std::filesystem::exists(out_path)) std::filesystem::remove(out_path);

        GDALDriver *driver = GetGDALDriverManager()->GetDriverByName("GPKG");
        GDALDataset *ds = driver->Create(out_path.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        OGRSpatialReference srs; srs.importFromEPSG(epsg);
        srs.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
        OGRLayer *layer = ds->CreateLayer("antennas", &srs, wkbPoint, nullptr);
        OGRFieldDefn f("antenna_id", OFTInteger);
        layer->CreateField(&f);

        for (int i = 0; i < (int)antennas.size(); ++i) {
            OGRPoint pt(CGAL::to_double(antennas[i].position.x()),
                        CGAL::to_double(antennas[i].position.y()));
            OGRFeature *feat = OGRFeature::CreateFeature(layer->GetLayerDefn());
            feat->SetGeometry(&pt);
            feat->SetField("antenna_id", i);
            auto warning=layer->CreateFeature(feat);
            OGRFeature::DestroyFeature(feat);
        }
        GDALClose(ds);
    }
}

#endif //GISCUPBONN_WRITER_HPP
