//
// Created by Laura on 6/23/26.
//

#ifndef GISCUPBONN_SOLUTION
#define GISCUPBONN_SOLUTION

#include "geometry/DiscreteCoverageInstance.hpp"
#include "utility/AbstractLogger.hpp"

#include <vector>

// Heavy-weight solution class to be returned by IntervalCoverageSolver classes. 
// Stores a vector of solution antennas and all polygons with their coverage. 
// Verifies solution in the constructor.
class Solution {
public:

    // Constructor with instance, antennas and polygons with coverage as input.
    Solution(const DiscreteCoverageInstance &_instance, const unsigned _k, const double _tau, 
             const std::vector<unsigned> _solution_antenna_ids, 
             const std::vector<std::pair<Poly2, double>> _polygons_and_coverage)
        : instance(_instance)
        , k(_k)
        , tau(_tau)
        , solution_antenna_ids(std::move(_solution_antenna_ids))
        , num_antennas(solution_antenna_ids.size())
        , polygons_and_coverage(std::move(_polygons_and_coverage))
        , num_polygons(polygons_and_coverage.size())
    {
        std::vector<unsigned> serviced = get_serviced_polygon_ids();
        num_serviced_polygons = serviced.size();
        if (not instance.verify_solution(solution_antenna_ids, serviced, tau)){
            throw std::runtime_error("Error! In Solution constructor: polygons_and_coverage does not match solution_antenna_ids according to instance.");
        }
    }

    // Heavy-weight constructor with instance and antennas. 
    // Polygons and their coverage are computed from antennas and instance.
    Solution(const DiscreteCoverageInstance & _instance, const unsigned _k, const double _tau, 
             const std::vector<unsigned> _solution_antenna_ids)
        : instance(_instance)
        , k(_k)
        , tau(_tau)
        , solution_antenna_ids(std::move(_solution_antenna_ids))  
        , num_antennas(solution_antenna_ids.size())
        , polygons_and_coverage(instance.get_polygons_and_coverage(solution_antenna_ids))
        , num_polygons(polygons_and_coverage.size())
    {
        std::vector<unsigned> serviced = get_serviced_polygon_ids();
        num_serviced_polygons = serviced.size();
        if (not instance.verify_solution(solution_antenna_ids, serviced, tau)){
            throw std::runtime_error("Error! In Solution constructor: polygons_and_coverage does not match solution_antenna_ids according to instance.");
        }
    }
    
    [[nodiscard]] std::vector<unsigned> get_serviced_polygon_ids() const;

    [[nodiscard]] std::vector<unsigned> get_unserviced_polygon_ids() const;

    [[nodiscard]] const std::vector<unsigned> get_solution_antenna_ids() const;

    [[nodiscard]] const std::vector<std::pair<AntennaData,Arrangement>> get_antenna_and_arrangement_of_solution() const;

    [[nodiscard]] const std::vector<AntennaData> get_solution_antennas() const;

    [[nodiscard]] const std::vector<std::pair<Poly2, double>> get_serviced_polygons_with_coverage() const;

    [[nodiscard]] const std::vector<std::pair<Poly2, double>> get_unserviced_polygons_with_coverage() const;

    [[nodiscard]] const std::vector<std::pair<Poly2, double>> get_all_polygons_with_coverage() const;

    [[nodiscard]] unsigned get_number_of_serviced_polygons() const;

    [[nodiscard]] unsigned get_k() const;

    [[nodiscard]] double get_tau() const;

    [[nodiscard]] unsigned get_original_polygon_id(unsigned polygon_id) const;

private:

    const DiscreteCoverageInstance &instance;
    const unsigned k;
    const double tau;

    const std::vector<unsigned> solution_antenna_ids;
    const unsigned num_antennas;

    const std::vector<std::pair<Poly2, double>> polygons_and_coverage;
    const unsigned num_polygons;

    unsigned num_serviced_polygons = 0; // Initialized with 0 so reserve does not do anything weird in case it is called before setting of this value
};

inline std::vector<unsigned> Solution::get_serviced_polygon_ids() const
{
    std::vector<unsigned> serviced;
    serviced.reserve(num_serviced_polygons);
    for (unsigned p = 0; p < num_polygons; ++p){
        if (polygons_and_coverage[p].second >= tau){
            serviced.push_back(p);
        }
    }
    return serviced;
}

inline std::vector<unsigned> Solution::get_unserviced_polygon_ids() const
{
    std::vector<unsigned> not_serviced;
    not_serviced.reserve(num_polygons - num_serviced_polygons);
    for (unsigned p = 0; p < num_polygons; ++p){
        if (polygons_and_coverage[p].second < tau){
            not_serviced.push_back(p);
        }
    }
    return not_serviced;
}

inline const std::vector<unsigned> Solution::get_solution_antenna_ids() const
{
    return solution_antenna_ids;
}

inline const std::vector<std::pair<AntennaData,Arrangement>> Solution::get_antenna_and_arrangement_of_solution() const
{
    std::vector<std::pair<AntennaData,Arrangement>> result;
    result.reserve(solution_antenna_ids.size());
    for (auto const id : solution_antenna_ids)
        result.push_back(instance.get_antenna_and_arrangement(id));
    return result;
}

inline const std::vector<AntennaData> Solution::get_solution_antennas() const
{
    std::vector<AntennaData> result;
    result.reserve(solution_antenna_ids.size());
    for (auto const id : solution_antenna_ids)
        result.push_back(instance.get_antenna_data(id));
    return result;
}

inline const std::vector<std::pair<Poly2, double>> Solution::get_serviced_polygons_with_coverage() const
{
    std::vector<std::pair<Poly2, double>> result;
    result.reserve(num_serviced_polygons);
    for (auto const p : get_serviced_polygon_ids())
        result.emplace_back(instance.get_polygon_data(p).poly,
                            polygons_and_coverage[p].second);
    return result;

}

inline const std::vector<std::pair<Poly2, double>> Solution::get_unserviced_polygons_with_coverage() const
{
    std::vector<std::pair<Poly2, double>> result;
    result.reserve(num_polygons - num_serviced_polygons);
    for (auto const p : get_unserviced_polygon_ids())
        result.emplace_back(instance.get_polygon_data(p).poly,
                            polygons_and_coverage[p].second);
    return result;
}

inline const std::vector<std::pair<Poly2, double>> Solution::get_all_polygons_with_coverage() const
{
    return polygons_and_coverage;
}

inline unsigned Solution::get_number_of_serviced_polygons() const
{
    return num_serviced_polygons;
}

inline unsigned Solution::get_k() const {
    return k;
}

inline double Solution::get_tau() const {
    return tau;
}

inline unsigned Solution::get_original_polygon_id(unsigned polygon_id) const {
    return instance.get_original_id_for_polygon(polygon_id);
}

class SolutionLogger : public Logger {
public:
    using Logger::Logger;

    void log_solution_as_antenna_ids(Solution const &solution, std::ostream *_stream = nullptr) const {
        std::ostream &out = _stream ? *_stream : stream();
        auto const &ids = solution.get_solution_antenna_ids();
        out << "Solution antenna IDs:\n";
        for (size_t i = 0; i < ids.size(); ++i) {
            out << ids[i];
            if (i + 1 < ids.size()) out << ",";
        }
        out << std::endl;
    }

    void log_solution_as_antenna_ids_and_serviced_polygons(Solution const &solution, std::ostream *_stream = nullptr) const
    {
        std::ostream &out = _stream ? *_stream : stream();
        auto const &antenna_ids = solution.get_solution_antenna_ids();
        out << "Solution antenna IDs:\n";
        for (size_t i = 0; i < antenna_ids.size(); ++i) {
            out << antenna_ids[i];
            if (i + 1 < antenna_ids.size()) out << ",";
        }
        auto const &polygon_ids = solution.get_serviced_polygon_ids();
        out << "\nServiced polygon IDs:\n";
        for (size_t i = 0; i < polygon_ids.size(); ++i) {
            out << polygon_ids[i];
            if (i + 1 < polygon_ids.size()) out << ",";
        }
        out << std::endl;
    }

    void log_solution_in_giscup_format(Solution const &solution, std::ostream *_stream = nullptr) const {
        std::ostream &out = _stream ? *_stream : stream();

        out << std::setprecision(std::numeric_limits<double>::max_digits10);

        out << "(" << solution.get_tau() << ", " << solution.get_k() << ")\n";

        // Antenna positions
        const auto antenna_ids = solution.get_solution_antennas();
        for (size_t i = 0; i < antenna_ids.size(); ++i) {
            auto const &pos = antenna_ids[i].position;
            out << "(" << pos.x() << "," << pos.y() << ")";
            if (i < antenna_ids.size() - 1){
                out << ",";
            }
        }
        out << "\n";

        // Serviced polygon ids
        const auto polygons = solution.get_serviced_polygon_ids();
        for (size_t i = 0; i < polygons.size(); ++i) {
            out << solution.get_original_polygon_id(polygons[i]);
            if (i < polygons.size() - 1){
                out << ",";
            }
        }
        out << std::endl;
    }
};

#endif // GISCUPBONN_SOLUTION
