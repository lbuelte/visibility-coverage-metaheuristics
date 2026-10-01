#include "gtest/gtest.h"
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <utility>
#include <vector>

#include "DiscreteCoverageInstance.hpp"
#include "DiscreteCoverageNeighbourhoodStructure.hpp"
#include "GeoJsonLoader.hpp"

TEST(LocalSearchHelperTest, smallTest) {
    unsigned num_polygons = 3;
    double tau = 0.5;

    std::function<double(double)> coverage_valuation_function = [tau](double coverage){return std::min(coverage, tau) / 2.0;};
    std::function<double(double, unsigned)> objective_function = [](double weighted_coverage, unsigned num_polygons_covered){
        return 0.5 * weighted_coverage / 0.5 + 0.5 * static_cast<double>(num_polygons_covered);
    };
    
    std::vector<std::vector<std::pair<unsigned, std::vector<std::pair<double, double>>>>> coverages =
    {
        { // Antenna 0
            {0, {{0.0, 0.2}, {0.8, 1.0}}},
            {1, {{0.2, 0.6}}},
            {2, {{0.0, 0.5}}},
        },
        { // Antenna 1
            {0, {{0.1, 0.4}}},
            {1, {{0.8, 1.0}}},
            {2, {{0.5, 0.8}}}
        },
        { // Antenna 2
            {0, {{0.3, 0.9}}},
            {1, {{0.0, 0.1}, {0.2, 0.3}, {0.5, 0.9}}},
            {2, {{0.8, 1.0}}},
        },
        { // Antenna 3
            {0, {{0.1, 0.2}, {0.6, 0.8}}},
            {1, {{0.3, 0.6}}},
            {2, {{0.4, 0.9}}},
        }
    };

    DiscreteCoverageInstance instance(num_polygons, coverages);
    DiscreteCoverageNeighbourhoodStructure data_structure(instance, 3, 0.5, {0, 1, 2, 3}, coverage_valuation_function, objective_function);

    data_structure.add_antenna_lazy(0);
    data_structure.add_antenna_lazy(1);

    EXPECT_ANY_THROW(data_structure.weightedCoverageByAntenna(1));
    EXPECT_ANY_THROW(data_structure.uniqueCoverageByAntenna(0));
    EXPECT_ANY_THROW(data_structure.polygonsDependentOnAntenna(1));
    
    data_structure.clean_structure();

    EXPECT_DOUBLE_EQ(data_structure.objective_value(), 2.25);

    EXPECT_DOUBLE_EQ(data_structure.uniqueCoverageByAntenna(0), 1.2);
    EXPECT_DOUBLE_EQ(data_structure.weightedCoverageByAntenna(0), 0.35);
    EXPECT_DOUBLE_EQ(data_structure.polygonsDependentOnAntenna(0), 3);

    EXPECT_DOUBLE_EQ(data_structure.uniqueCoverageByAntenna(1), 0.7);
    EXPECT_DOUBLE_EQ(data_structure.weightedCoverageByAntenna(1), 0.1);
    EXPECT_DOUBLE_EQ(data_structure.polygonsDependentOnAntenna(1), 2);

    EXPECT_DOUBLE_EQ(data_structure.peek_antennas({2, 3}).new_objective, 2.25);

    data_structure.remove_antenna_lazy(1);

    EXPECT_ANY_THROW(data_structure.peek_antennas({3}));
    EXPECT_ANY_THROW(data_structure.objective_value());

    data_structure.clean_structure_for_peek();

    EXPECT_DOUBLE_EQ(data_structure.objective_value(), 1.15);

    EXPECT_DOUBLE_EQ(data_structure.peek_antennas({3}).new_objective, 1.7);
    EXPECT_DOUBLE_EQ(data_structure.peek_antennas({2}).new_objective, 2.25);

    data_structure.add_antenna_lazy(2);
    data_structure.clean_structure();

    EXPECT_DOUBLE_EQ(data_structure.objective_value(), 2.25);

    EXPECT_DOUBLE_EQ(data_structure.uniqueCoverageByAntenna(0), 1.0);
    EXPECT_DOUBLE_EQ(data_structure.weightedCoverageByAntenna(0), 0.15);
    EXPECT_DOUBLE_EQ(data_structure.polygonsDependentOnAntenna(0), 1);

    EXPECT_DOUBLE_EQ(data_structure.uniqueCoverageByAntenna(2), 1.1);
    EXPECT_LT(std::abs(data_structure.weightedCoverageByAntenna(2) - 0.1), 0.000001); // Because gtest double equality is too strict
    EXPECT_DOUBLE_EQ(data_structure.polygonsDependentOnAntenna(2), 2);
}


TEST(LocalSearchHelperTest, smallTestWithMap) {
    unsigned num_polygons = 3;
    double tau = 0.5;

    std::function<double(double)> coverage_valuation_function = [tau](double coverage){return std::min(coverage, tau) / 2.0;};
    std::function<double(double, unsigned)> objective_function = [](double weighted_coverage, unsigned num_polygons_covered){
        return 0.5 * weighted_coverage / 0.5 + 0.5 * static_cast<double>(num_polygons_covered);
    };
    
    std::vector<std::vector<std::pair<unsigned, std::vector<std::pair<double, double>>>>> coverages =
    {
        { // Antenna 0
            {0, {{0.0, 0.2}, {0.8, 1.0}}},
            {1, {{0.2, 0.6}}},
            {2, {{0.0, 0.5}}},
        },
        {}, {}, {}, {},
        { // Antenna 5
            {0, {{0.1, 0.4}}},
            {1, {{0.8, 1.0}}},
            {2, {{0.5, 0.8}}}
        },
        {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {},
        { // Antenna 22
            {0, {{0.3, 0.9}}},
            {1, {{0.0, 0.1}, {0.2, 0.3}, {0.5, 0.9}}},
            {2, {{0.8, 1.0}}},
        },
        {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {},
        { // Antenna 47
            {0, {{0.1, 0.2}, {0.6, 0.8}}},
            {1, {{0.3, 0.6}}},
            {2, {{0.4, 0.9}}},
        }
    };

    for (unsigned i = 0; i < 100; i++)
        coverages.emplace_back();

    DiscreteCoverageInstance instance(num_polygons, coverages);
    DiscreteCoverageNeighbourhoodStructure data_structure(instance, 3, 0.5, {0, 5, 22, 47}, coverage_valuation_function, objective_function);

    data_structure.add_antenna_lazy(0);
    data_structure.add_antenna_lazy(5);

    EXPECT_ANY_THROW(data_structure.weightedCoverageByAntenna(5));
    EXPECT_ANY_THROW(data_structure.uniqueCoverageByAntenna(0));
    EXPECT_ANY_THROW(data_structure.polygonsDependentOnAntenna(5));
    
    data_structure.clean_structure();

    EXPECT_DOUBLE_EQ(data_structure.objective_value(), 2.25);

    EXPECT_DOUBLE_EQ(data_structure.uniqueCoverageByAntenna(0), 1.2);
    EXPECT_DOUBLE_EQ(data_structure.weightedCoverageByAntenna(0), 0.35);
    EXPECT_DOUBLE_EQ(data_structure.polygonsDependentOnAntenna(0), 3);

    EXPECT_DOUBLE_EQ(data_structure.uniqueCoverageByAntenna(5), 0.7);
    EXPECT_DOUBLE_EQ(data_structure.weightedCoverageByAntenna(5), 0.1);
    EXPECT_DOUBLE_EQ(data_structure.polygonsDependentOnAntenna(5), 2);

    EXPECT_DOUBLE_EQ(data_structure.peek_antennas({22, 47}).new_objective, 2.25);

    data_structure.remove_antenna_lazy(5);

    EXPECT_ANY_THROW(data_structure.peek_antennas({47}));
    EXPECT_ANY_THROW(data_structure.objective_value());

    data_structure.clean_structure_for_peek();

    EXPECT_DOUBLE_EQ(data_structure.objective_value(), 1.15);

    EXPECT_DOUBLE_EQ(data_structure.peek_antennas({47}).new_objective, 1.7);
    EXPECT_DOUBLE_EQ(data_structure.peek_antennas({22}).new_objective, 2.25);

    data_structure.add_antenna_lazy(22);
    data_structure.clean_structure();

    EXPECT_DOUBLE_EQ(data_structure.objective_value(), 2.25);

    EXPECT_DOUBLE_EQ(data_structure.uniqueCoverageByAntenna(0), 1.0);
    EXPECT_DOUBLE_EQ(data_structure.weightedCoverageByAntenna(0), 0.15);
    EXPECT_DOUBLE_EQ(data_structure.polygonsDependentOnAntenna(0), 1);

    EXPECT_DOUBLE_EQ(data_structure.uniqueCoverageByAntenna(22), 1.1);
    EXPECT_LT(std::abs(data_structure.weightedCoverageByAntenna(22) - 0.1), 0.000001); // Because gtest double equality is too strict
    EXPECT_DOUBLE_EQ(data_structure.polygonsDependentOnAntenna(22), 2);
}

