#ifndef GISCUPBONN_IDSOLUTION_HPP
#define GISCUPBONN_IDSOLUTION_HPP

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <regex>
#include <filesystem>
#include <cmath>
#include <algorithm>

struct IdSolution {
    std::vector<unsigned> antenna_ids;
    std::vector<unsigned> serviced_polygon_ids;
    int num_serviced_polygons;
    std::string instance_name;
    unsigned k;
    double tau;
    std::filesystem::path path;
};


inline std::vector<unsigned> parse_uint_csv_line(const std::string& line) {
    std::vector<unsigned> out;
    std::stringstream ss(line);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        size_t start = tok.find_first_not_of(" \t\r\n");
        size_t end   = tok.find_last_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        tok = tok.substr(start, end - start + 1);
        if (tok.empty()) continue;
        out.push_back(static_cast<unsigned>(std::stoul(tok)));
    }
    return out;
}

// Non-throwing filename parse. Returns false (and leaves outputs untouched)
// if the filename doesn't match the expected pattern.
// Expected pattern: <instance_name>_par_<k>_<tau>_id_..._val_<val>.solution.ids
inline bool try_parse_filename(const std::string& filename, std::string& instance_name,
                                unsigned& k, double& tau, int& val_hint) {
    static const std::regex re(R"(^(.+)_par_(\d+)_([0-9]*\.?[0-9]+)_id_.*_val_(\d+)\.solution\.ids$)");
    std::smatch m;
    if (!std::regex_search(filename, m, re)) {
        return false;
    }
    instance_name = m[1].str();
    k             = static_cast<unsigned>(std::stoul(m[2].str()));
    tau           = std::stod(m[3].str());
    val_hint      = std::stoi(m[4].str());
    return true;
}

inline void parse_filename(const std::string& filename, std::string& instance_name,
                            unsigned& k, double& tau, int& val_hint) {
    if (!try_parse_filename(filename, instance_name, k, tau, val_hint)) {
        throw std::runtime_error("parse_filename: filename does not match expected pattern: " + filename);
    }
}

inline bool matches_instance_k_tau(const std::filesystem::path& p,
                                    const std::string& instance_name,
                                    unsigned k, double tau, double eps,
                                    std::string& out_instance_name,
                                    unsigned& out_k, double& out_tau) {
    if (!p.has_filename()) return false;
    std::string filename = p.filename().string();

    int val_hint = 0;
    if (!try_parse_filename(filename, out_instance_name, out_k, out_tau, val_hint)) {
        return false; // doesn't match the .solution.ids naming pattern at all
    }
    if (out_instance_name != instance_name) return false;
    if (out_k != k) return false;
    if (std::fabs(out_tau - tau) > eps) return false;
    return true;
}

// Loads a single IdSolution from a *.solution.ids file.
inline IdSolution load_id_solution(const std::filesystem::path& filepath) {
    std::ifstream in(filepath);
    if (!in.is_open()) {
        throw std::runtime_error("load_id_solution: could not open file: " + filepath.string());
    }

    IdSolution sol;
    sol.path = filepath;

    std::string filename = filepath.filename().string();
    int val_hint = 0;
    parse_filename(filename, sol.instance_name, sol.k, sol.tau, val_hint);

    std::string line;
    bool in_antennas = false;
    bool in_polygons = false;

    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();

        if (line.rfind("Solution antenna IDs:", 0) == 0) {
            in_antennas = true;
            in_polygons = false;
            continue;
        }
        if (line.rfind("Serviced polygon IDs:", 0) == 0) {
            in_antennas = false;
            in_polygons = true;
            continue;
        }

        if (line.empty()) continue;

        if (in_antennas) {
            sol.antenna_ids = parse_uint_csv_line(line);
            in_antennas = false;
        } else if (in_polygons) {
            sol.serviced_polygon_ids = parse_uint_csv_line(line);
            in_polygons = false;
        }
    }

    sol.num_serviced_polygons = static_cast<int>(sol.serviced_polygon_ids.size());

    if (sol.num_serviced_polygons != val_hint) {
        throw std::runtime_error(
            "load_id_solution: parsed serviced count (" + std::to_string(sol.num_serviced_polygons) +
            ") does not match filename val (" + std::to_string(val_hint) + ") for file: " + filename);
    }

    return sol;
}

// Loads all *.solution.ids files directly inside `folder` (non-recursive)
// whose instance name matches exactly, k matches exactly, and tau matches within `eps`.
inline std::vector<IdSolution> load_id_solutions_in_folder(
    const std::filesystem::path& folder,
    const std::string& instance_name,
    unsigned k,
    double tau,
    double eps = 1e-12) {

    if (!std::filesystem::exists(folder) || !std::filesystem::is_directory(folder)) {
        throw std::runtime_error("load_id_solutions_in_folder: not a directory: " + folder.string());
    }

    std::vector<IdSolution> results;

    for (const auto& entry : std::filesystem::directory_iterator(folder)) {
        if (!entry.is_regular_file()) continue;

        std::string file_instance;
        unsigned file_k;
        double file_tau;
        if (!matches_instance_k_tau(entry.path(), instance_name, k, tau, eps,
                                     file_instance, file_k, file_tau)) continue;

        results.push_back(load_id_solution(entry.path()));
    }

    std::sort(results.begin(), results.end(),
              [](const IdSolution& a, const IdSolution& b) { return a.path < b.path; });

    return results;
}

// Same as above, but recurses into all subfolders too.
inline std::vector<IdSolution> load_id_solutions_recursive(
    const std::filesystem::path& folder,
    const std::string& instance_name,
    unsigned k,
    double tau,
    double eps = 1e-12) {

    if (!std::filesystem::exists(folder) || !std::filesystem::is_directory(folder)) {
        throw std::runtime_error("load_id_solutions_recursive: not a directory: " + folder.string());
    }

    std::vector<IdSolution> results;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(folder)) {
        if (!entry.is_regular_file()) continue;

        std::string file_instance;
        unsigned file_k;
        double file_tau;
        if (!matches_instance_k_tau(entry.path(), instance_name, k, tau, eps,
                                     file_instance, file_k, file_tau)) continue;

        results.push_back(load_id_solution(entry.path()));
    }

    std::sort(results.begin(), results.end(),
              [](const IdSolution& a, const IdSolution& b) { return a.path < b.path; });

    return results;
}
#endif
