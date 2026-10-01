//
// Created by philip on 6/11/26.
//

#include "geometry/Writer.hpp"

void write_gpkg(const std::vector<Poly2> &polygons, const std::string &out_path, const std::string &layer_name,
    int epsg) {
    GDALAllRegister();

    // ── 0. Ensure output directory exists ────────────────────────────────
    std::filesystem::path p(out_path);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }

    // ── 1. Delete stale file (GDAL won't overwrite) ───────────────────────
    if (std::filesystem::exists(out_path)) {
        std::filesystem::remove(out_path);
    }

    // ── 2. Get GPKG driver ────────────────────────────────────────────────
    GDALDriver *driver = GetGDALDriverManager()->GetDriverByName("GPKG");
    if (!driver) {
        // List available drivers so we can see what IS there
        for (int i = 0; i < GetGDALDriverManager()->GetDriverCount(); ++i)
            std::cerr << "  " << GetGDALDriverManager()->GetDriver(i)->GetDescription() << "\n";
        throw std::runtime_error("GDAL: GPKG driver not available.");
    }

    // ── 3. Create file ────────────────────────────────────────────────────
    GDALDataset *ds = driver->Create(out_path.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
    if (!ds)
        throw std::runtime_error(
            std::string("GDAL: failed to create '") + out_path + "': " +
            CPLGetLastErrorMsg());

    // ── 4. CRS ────────────────────────────────────────────────────────────
    OGRSpatialReference srs;
    if (srs.importFromEPSG(epsg) != OGRERR_NONE) {
        GDALClose(ds);
        throw std::runtime_error("GDAL: unknown EPSG:" + std::to_string(epsg));
    }
    srs.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);

    // ── 5. Create layer ───────────────────────────────────────────────────
    OGRLayer *layer = ds->CreateLayer(layer_name.c_str(), &srs, wkbPolygon, nullptr);
    if (!layer) {
        GDALClose(ds);
        throw std::runtime_error(
            std::string("GDAL: failed to create layer: ") + CPLGetLastErrorMsg());
    }

    OGRFieldDefn id_field("id", OFTInteger);
    layer->CreateField(&id_field);

    // ── 6. Write features ─────────────────────────────────────────────────
    if (layer->StartTransaction() != OGRERR_NONE) {
        GDALClose(ds);
        throw std::runtime_error("GDAL: StartTransaction failed.");
    }

    for (std::size_t i = 0; i < polygons.size(); ++i) {
        OGRLinearRing ring;
        for (auto vit = polygons[i].vertices_begin();
             vit != polygons[i].vertices_end(); ++vit)
            ring.addPoint(CGAL::to_double(vit->x()),
                          CGAL::to_double(vit->y()));
        ring.closeRings();

        OGRPolygon ogr_poly;
        ogr_poly.addRing(&ring);

        OGRFeature *feat = OGRFeature::CreateFeature(layer->GetLayerDefn());
        feat->SetField("id", static_cast<int>(i + 1));
        feat->SetGeometry(&ogr_poly);

        OGRErr err = layer->CreateFeature(feat);
        OGRFeature::DestroyFeature(feat);

        if (err != OGRERR_NONE) {
            layer->RollbackTransaction();
            GDALClose(ds);
            throw std::runtime_error(
                "GDAL: failed writing feature " + std::to_string(i) +
                ": " + CPLGetLastErrorMsg());
        }
    }

    if (layer->CommitTransaction() != OGRERR_NONE) {
        GDALClose(ds);
        throw std::runtime_error(
            std::string("GDAL: CommitTransaction failed: ") + CPLGetLastErrorMsg());
    }

    GDALClose(ds);
}

void write_gpkg_pwh(const PolyWH &pwh, const std::string &out_path, const std::string &layer_name, int epsg) {
    GDALAllRegister();

    std::filesystem::path p(out_path);
    if (p.has_parent_path())
        std::filesystem::create_directories(p.parent_path());
    if (std::filesystem::exists(out_path))
        std::filesystem::remove(out_path);

    GDALDriver *driver = GetGDALDriverManager()->GetDriverByName("GPKG");
    if (!driver)
        throw std::runtime_error("GDAL: GPKG driver not available.");

    GDALDataset *ds = driver->Create(out_path.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
    if (!ds)
        throw std::runtime_error(std::string("GDAL: failed to create '") + out_path + "': " + CPLGetLastErrorMsg());

    OGRSpatialReference srs;
    srs.importFromEPSG(epsg);
    srs.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);

    OGRLayer *layer = ds->CreateLayer(layer_name.c_str(), &srs, wkbPolygon, nullptr);
    if (!layer) {
        GDALClose(ds);
        throw std::runtime_error(std::string("GDAL: failed to create layer: ") + CPLGetLastErrorMsg());
    }

    // ── Build OGR polygon ─────────────────────────────────────────────────
    OGRPolygon ogr_poly;

    // Outer boundary
    OGRLinearRing outer;
    for (auto vit = pwh.outer_boundary().vertices_begin();
         vit != pwh.outer_boundary().vertices_end(); ++vit)
        outer.addPoint(CGAL::to_double(vit->x()), CGAL::to_double(vit->y()));
    outer.closeRings();
    ogr_poly.addRing(&outer);

    // Holes
    for (auto hit = pwh.holes_begin();
         hit != pwh.holes_end(); ++hit) {
        OGRLinearRing hole;
        for (auto vit = hit->vertices_begin();
             vit != hit->vertices_end(); ++vit)
            hole.addPoint(CGAL::to_double(vit->x()), CGAL::to_double(vit->y()));
        hole.closeRings();
        ogr_poly.addRing(&hole);
    }

    // ── Write single feature ──────────────────────────────────────────────
    OGRFeature *feat = OGRFeature::CreateFeature(layer->GetLayerDefn());
    feat->SetGeometry(&ogr_poly);

    OGRErr err = layer->CreateFeature(feat);
    OGRFeature::DestroyFeature(feat);

    if (err != OGRERR_NONE) {
        GDALClose(ds);
        throw std::runtime_error(std::string("GDAL: failed to write feature: ") + CPLGetLastErrorMsg());
    }
    GDALClose(ds);
}

void write_visibility_regions_unified(const std::vector<Arrangement> &regions, const std::string &out_path, int epsg) {
    // ── 1. Union all visibility polygons ─────────────────────────────────
    CGAL::Polygon_set_2<Kernel> poly_set;
    for (const auto &arr : regions) {
        for (auto fit = arr.faces_begin(); fit != arr.faces_end(); ++fit) {
            if (!fit->is_unbounded()) {
                Poly2 p;
                auto curr = fit->outer_ccb();
                do {
                    p.push_back(curr->source()->point());
                    ++curr;
                } while (curr != fit->outer_ccb());
                if (!p.is_empty())
                    poly_set.join(p);
                break;
            }
        }
    }

    std::vector<PolyWH> united;
    poly_set.polygons_with_holes(std::back_inserter(united));

    // ── 2. Write to GeoPackage ────────────────────────────────────────────
    GDALAllRegister();

    std::filesystem::path fp(out_path);
    if (fp.has_parent_path())
        std::filesystem::create_directories(fp.parent_path());
    if (std::filesystem::exists(out_path))
        std::filesystem::remove(out_path);

    GDALDriver *driver = GetGDALDriverManager()->GetDriverByName("GPKG");
    if (!driver)
        throw std::runtime_error("GDAL: GPKG driver not available.");

    GDALDataset *ds = driver->Create(out_path.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
    if (!ds)
        throw std::runtime_error(std::string("GDAL: failed to create '") + out_path + "'");

    OGRSpatialReference srs;
    srs.importFromEPSG(epsg);
    srs.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);

    OGRLayer *layer = ds->CreateLayer("unified_visibility", &srs, wkbPolygon, nullptr);
    if (!layer) {
        GDALClose(ds);
        throw std::runtime_error("GDAL: failed to create layer.");
    }

    for (const auto &pwh : united) {
        OGRPolygon ogr_poly;

        OGRLinearRing outer;
        for (auto vit = pwh.outer_boundary().vertices_begin();
             vit != pwh.outer_boundary().vertices_end(); ++vit)
            outer.addPoint(CGAL::to_double(vit->x()), CGAL::to_double(vit->y()));
        outer.closeRings();
        ogr_poly.addRing(&outer);

        for (auto hit = pwh.holes_begin(); hit != pwh.holes_end(); ++hit) {
            OGRLinearRing hole;
            for (auto vit = hit->vertices_begin(); vit != hit->vertices_end(); ++vit)
                hole.addPoint(CGAL::to_double(vit->x()), CGAL::to_double(vit->y()));
            hole.closeRings();
            ogr_poly.addRing(&hole);
        }

        OGRFeature *feat = OGRFeature::CreateFeature(layer->GetLayerDefn());
        feat->SetGeometry(&ogr_poly);
        OGRErr err = layer->CreateFeature(feat);
        OGRFeature::DestroyFeature(feat);

        if (err != OGRERR_NONE) {
            GDALClose(ds);
            throw std::runtime_error("GDAL: failed to write unified feature.");
        }
    }

    GDALClose(ds);
}

void write_visibility_regions(const std::vector<Arrangement> &regions, const std::string &out_folder, int epsg) {
    GDALAllRegister();
    std::filesystem::create_directories(out_folder);

    for (std::size_t i = 0; i < regions.size(); ++i) {
        const Arrangement &arr = regions[i];

        Arrangement::Face_const_handle bounded_face;
        bool found = false;

        for (auto fit = arr.faces_begin(); fit != arr.faces_end(); ++fit) {
            if (!fit->is_unbounded()) {
                bounded_face = fit;
                found = true;
                break;
            }
        }

        if (!found) {
            std::cerr << "Warning: region " << i
                    << " has no bounded face, skipping.\n";
            continue;
        }

        // Count boundary edges
        int edge_count = 0;
        {
            auto circ = bounded_face->outer_ccb();
            auto curr = circ;

            do {
                ++edge_count;
                ++curr;
            } while (curr != circ);
        }

        // Build polygon
        OGRLinearRing ring;
        auto circ = bounded_face->outer_ccb();
        auto curr = circ;

        do {
            const Point2 &p = curr->source()->point();
            ring.addPoint(
                CGAL::to_double(p.x()),
                CGAL::to_double(p.y()));
            ++curr;
        } while (curr != circ);

        ring.closeRings();

        OGRPolygon ogr_poly;
        ogr_poly.addRing(&ring);

        std::string out_path =
                out_folder + "/region_" + std::to_string(i) +
                "_e" + std::to_string(edge_count) + ".gpkg";

        if (std::filesystem::exists(out_path))
            std::filesystem::remove(out_path);

        GDALDriver *driver =
                GetGDALDriverManager()->GetDriverByName("GPKG");

        if (!driver)
            throw std::runtime_error("GDAL: GPKG driver not available.");

        GDALDataset *ds =
                driver->Create(out_path.c_str(), 0, 0, 0,
                               GDT_Unknown, nullptr);

        if (!ds)
            throw std::runtime_error(
                std::string("GDAL: failed to create '") +
                out_path + "': " + CPLGetLastErrorMsg());

        OGRSpatialReference srs;
        srs.importFromEPSG(epsg);
        srs.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);

        OGRLayer *layer =
                ds->CreateLayer("visibility", &srs, wkbPolygon, nullptr);

        if (!layer) {
            GDALClose(ds);
            throw std::runtime_error(
                std::string("GDAL: failed to create layer: ") +
                CPLGetLastErrorMsg());
        }

        OGRFeature *feat =
                OGRFeature::CreateFeature(layer->GetLayerDefn());

        feat->SetGeometry(&ogr_poly);

        OGRErr err = layer->CreateFeature(feat);

        OGRFeature::DestroyFeature(feat);
        GDALClose(ds);

        if (err != OGRERR_NONE)
            throw std::runtime_error(
                "GDAL: failed to write feature for region " +
                std::to_string(i));
    }
}

void write_antenna_data(const std::vector<AntennaData> &antennas, const std::string &out_folder, int epsg) {
    GDALAllRegister();
    std::filesystem::create_directories(out_folder);

    for (std::size_t i = 0; i < antennas.size(); ++i) {

        const AntennaData &ad = antennas[i];

        if (ad.tagged_edges.empty()) {
            std::cerr << "Warning: antenna " << i
                    << " has no tagged edges, skipping.\n";
            continue;
        }

        std::string out_path =
                out_folder + "/antenna_" + std::to_string(i) + ".gpkg";

        if (std::filesystem::exists(out_path))
            std::filesystem::remove(out_path);

        GDALDriver *driver =
                GetGDALDriverManager()->GetDriverByName("GPKG");

        if (!driver)
            throw std::runtime_error(
                "GDAL: GPKG driver not available.");

        GDALDataset *ds =
                driver->Create(out_path.c_str(),
                               0, 0, 0,
                               GDT_Unknown,
                               nullptr);

        if (!ds) {
            throw std::runtime_error(
                std::string("GDAL: failed to create '") +
                out_path + "': " +
                CPLGetLastErrorMsg());
        }

        OGRSpatialReference srs;
        srs.importFromEPSG(epsg);
        srs.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);

        // ============================================================
        // Edge layer
        // ============================================================

        OGRLayer *edge_layer =
                ds->CreateLayer("antenna_edges",
                                &srs,
                                wkbLineString,
                                nullptr);

        if (!edge_layer) {
            GDALClose(ds);
            throw std::runtime_error(
                std::string("GDAL: failed to create edge layer: ") +
                CPLGetLastErrorMsg());
        }

        OGRFieldDefn f_poly("poly_id", OFTInteger);
        OGRFieldDefn f_edge("edge_id", OFTInteger);

        edge_layer->CreateField(&f_poly);
        edge_layer->CreateField(&f_edge);

        for (const TaggedEdge &te: ad.tagged_edges) {
            OGRLineString line;

            line.addPoint(
                CGAL::to_double(te.segment.source().x()),
                CGAL::to_double(te.segment.source().y()));

            line.addPoint(
                CGAL::to_double(te.segment.target().x()),
                CGAL::to_double(te.segment.target().y()));

            OGRFeature *feat =
                    OGRFeature::CreateFeature(
                        edge_layer->GetLayerDefn());

            feat->SetGeometry(&line);
            feat->SetField("poly_id", static_cast<int>(te.poly_id));
            feat->SetField("edge_id", static_cast<int>(te.edge_id));

            OGRErr err = edge_layer->CreateFeature(feat);

            OGRFeature::DestroyFeature(feat);

            if (err != OGRERR_NONE) {
                GDALClose(ds);
                throw std::runtime_error(
                    "GDAL: failed to write edge feature for antenna " +
                    std::to_string(i));
            }
        }

        // ============================================================
        // Antenna position layer
        // ============================================================

        OGRLayer *antenna_layer =
                ds->CreateLayer("antenna_position",
                                &srs,
                                wkbPoint,
                                nullptr);

        if (!antenna_layer) {
            GDALClose(ds);
            throw std::runtime_error(
                std::string(
                    "GDAL: failed to create antenna_position layer: ") +
                CPLGetLastErrorMsg());
        }

        OGRFieldDefn f_ant_id("antenna_id", OFTInteger);
        antenna_layer->CreateField(&f_ant_id);

        OGRPoint antenna_point(
            CGAL::to_double(ad.position.x()),
            CGAL::to_double(ad.position.y()));

        OGRFeature *antenna_feat =
                OGRFeature::CreateFeature(
                    antenna_layer->GetLayerDefn());

        antenna_feat->SetGeometry(&antenna_point);
        antenna_feat->SetField(
            "antenna_id",
            static_cast<int>(i));

        OGRErr antenna_err =
                antenna_layer->CreateFeature(antenna_feat);

        OGRFeature::DestroyFeature(antenna_feat);

        if (antenna_err != OGRERR_NONE) {
            GDALClose(ds);
            throw std::runtime_error(
                "GDAL: failed to write antenna position for antenna " +
                std::to_string(i));
        }

        GDALClose(ds);
    }

    std::cout << "Written antenna datasets to "
            << out_folder << '\n';
}

void write_edges_gpkg(const std::vector<EdgeSegment> &edges, const std::string &out_path, int epsg) {
    GDALAllRegister();

    if (std::filesystem::exists(out_path))
        std::filesystem::remove(out_path);

    GDALDriver* driver =
            GetGDALDriverManager()->GetDriverByName("GPKG");

    if (!driver)
        throw std::runtime_error("GPKG driver not available");

    GDALDataset* ds =
            driver->Create(out_path.c_str(), 0, 0, 0,
                           GDT_Unknown, nullptr);

    if (!ds)
        throw std::runtime_error("Failed to create GeoPackage");

    OGRSpatialReference srs;
    srs.importFromEPSG(epsg);
    srs.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);

    OGRLayer* layer =
            ds->CreateLayer("edges", &srs, wkbLineString, nullptr);

    if (!layer) {
        GDALClose(ds);
        throw std::runtime_error("Failed to create layer");
    }

    for (const auto& seg : edges) {

        OGRLineString line;

        line.addPoint(
            CGAL::to_double(seg.source().x()),
            CGAL::to_double(seg.source().y()));

        line.addPoint(
            CGAL::to_double(seg.target().x()),
            CGAL::to_double(seg.target().y()));

        OGRFeature* feat =
                OGRFeature::CreateFeature(layer->GetLayerDefn());

        feat->SetGeometry(&line);

        if (layer->CreateFeature(feat) != OGRERR_NONE) {
            OGRFeature::DestroyFeature(feat);
            GDALClose(ds);
            throw std::runtime_error("Failed to write feature");
        }

        OGRFeature::DestroyFeature(feat);
    }

    GDALClose(ds);
}

void write_single_edge_gpkg(const EdgeSegment &seg, const std::string &out_path, int epsg) {
    write_edges_gpkg({seg}, out_path, epsg);
}

void write_polygons_gpkg(const std::vector<Poly2> &polygons, const std::string &out_path, int epsg) {
    GDALAllRegister();

    if (std::filesystem::exists(out_path))
        std::filesystem::remove(out_path);

    GDALDriver* driver =
            GetGDALDriverManager()->GetDriverByName("GPKG");

    if (!driver)
        throw std::runtime_error("GPKG driver not available");

    GDALDataset* ds =
            driver->Create(out_path.c_str(), 0, 0, 0,
                           GDT_Unknown, nullptr);

    if (!ds)
        throw std::runtime_error("Failed to create GeoPackage");

    OGRSpatialReference srs;
    srs.importFromEPSG(epsg);
    srs.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);

    OGRLayer* layer =
            ds->CreateLayer("polygons", &srs, wkbPolygon, nullptr);

    if (!layer) {
        GDALClose(ds);
        throw std::runtime_error("Failed to create layer");
    }

    for (const auto& poly : polygons) {

        OGRLinearRing ring;

        for (auto vit = poly.vertices_begin();
             vit != poly.vertices_end();
             ++vit)
        {
            ring.addPoint(CGAL::to_double(vit->x()),
                          CGAL::to_double(vit->y()));
        }

        ring.closeRings();

        OGRPolygon ogr_poly;
        ogr_poly.addRing(&ring);

        OGRFeature* feat =
                OGRFeature::CreateFeature(layer->GetLayerDefn());

        feat->SetGeometry(&ogr_poly);

        if (layer->CreateFeature(feat) != OGRERR_NONE) {
            OGRFeature::DestroyFeature(feat);
            GDALClose(ds);
            throw std::runtime_error("Failed to write feature");
        }

        OGRFeature::DestroyFeature(feat);
    }

    GDALClose(ds);
}

void write_single_polygon_gpkg(const Poly2 &poly, const std::string &out_path, int epsg) {
    write_polygons_gpkg({poly}, out_path, epsg);
}

void write_tagged_edges_gpkg(const std::vector<AntennaData> &antennas, const std::string &out_path, int epsg) {
    std::vector<EdgeSegment> edges;
    for (const auto &antenna : antennas)
        for (const auto &te : antenna.tagged_edges)
            edges.push_back(te.segment);
    write_edges_gpkg(edges, out_path, epsg);
}

void write_tagged_edges_union_gpkg(const std::vector<AntennaData> &antennas, const std::string &out_path, int epsg) {
    write_edges_gpkg(geometric_union_of_tagged_edges(antennas), out_path, epsg);
}

void write_polygons_with_coverage_label(const std::vector<std::pair<Poly2, double>> &polygons_with_coverage,
    const std::string &out_path, int epsg) {
    GDALAllRegister();
    if (std::filesystem::exists(out_path)) std::filesystem::remove(out_path);

    GDALDriver *driver = GetGDALDriverManager()->GetDriverByName("GPKG");
    GDALDataset *ds = driver->Create(out_path.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
    if (!ds) throw std::runtime_error("GDAL: failed to create " + out_path);

    OGRSpatialReference srs;
    srs.importFromEPSG(epsg);
    srs.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);

    OGRLayer *poly_layer = ds->CreateLayer("polygons", &srs, wkbPolygon, nullptr);
    OGRFieldDefn f_cov("coverage", OFTReal);
    OGRFieldDefn f_label("label", OFTString);
    poly_layer->CreateField(&f_cov);
    poly_layer->CreateField(&f_label);

    for (const auto &[poly, cov] : polygons_with_coverage) {
        OGRLinearRing ring;
        for (auto vit = poly.vertices_begin(); vit != poly.vertices_end(); ++vit)
            ring.addPoint(CGAL::to_double(vit->x()), CGAL::to_double(vit->y()));
        ring.closeRings();
        OGRPolygon ogr_poly;
        ogr_poly.addRing(&ring);

        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << cov;

        OGRFeature *feat = OGRFeature::CreateFeature(poly_layer->GetLayerDefn());
        feat->SetGeometry(&ogr_poly);
        feat->SetField("coverage", cov);
        feat->SetField("label", oss.str().c_str());
        auto warning=poly_layer->CreateFeature(feat);
        OGRFeature::DestroyFeature(feat);
    }

    GDALClose(ds);
}

void write_solution(const std::vector<std::pair<AntennaData, Arrangement>> &antenna_and_arrangements,
    const std::vector<std::pair<Poly2, double>> &covered, const std::vector<std::pair<Poly2, double>> &uncovered,
    const std::string &out_folder, int epsg) {
    std::cout<<"[writer] started writing solution... this may take some time due to geometric unions being done"<<std::endl;
    std::filesystem::create_directories(out_folder);

    // ── 1. Union of arrangements ──────────────────────────────────────────
    std::vector<Arrangement> arrangements;
    arrangements.reserve(antenna_and_arrangements.size());
    for (const auto &[ad, arr] : antenna_and_arrangements)
        arrangements.push_back(arr);
    write_visibility_regions_unified(arrangements, out_folder + "/union_visibility.gpkg", epsg);

    // ── 2. Union of tagged edge segments ─────────────────────────────────
    std::vector<AntennaData> antennas;
    antennas.reserve(antenna_and_arrangements.size());
    for (const auto &[ad, arr] : antenna_and_arrangements)
        antennas.push_back(ad);
    write_tagged_edges_union_gpkg(antennas, out_folder + "/union_edges.gpkg", epsg);


    // ── 5. Covered polygons with coverage label ───────────────────────────
    write_polygons_with_coverage_label(
        covered, out_folder + "/covered_polygons_with_coverage.gpkg", epsg);

    // ── 6. Uncovered polygons with coverage label ─────────────────────────
    write_polygons_with_coverage_label(
        uncovered, out_folder + "/uncovered_polygons_with_coverage.gpkg", epsg);

    // ── 7. Antenna positions ──────────────────────────────────────────────
    {
        GDALAllRegister();
        std::string out_path = out_folder + "/antenna_positions.gpkg";
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

    std::cout << "[writer] Solution written to " << out_folder << "\n";
}
