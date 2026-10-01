//
// Created by Laura on 7/20/26.
//

#include "RunUtilities.hpp"
#include "utility/IdSolution.hpp"


int main(int argc, char **argv) {

    /* ---- Load Config ---- */
    VisualizationConfig cfg(argc, argv);

    /* ---- Load instance ---- */
    DiscreteCoverageInstance instance(get_polygons(cfg.path_to_instance));  
    instance.initialize_with_vertex_antenna_candidates();

    std::cout << "Loaded instance with " << instance.get_number_antennas() 
              << " antennas and " << instance.get_number_polygons()
              << " polygons." << std::endl;

    /* ---- Load IdSolution from solutions folder ---- */
    std::vector<IdSolution> id_solutions = load_id_solutions_in_folder(
            cfg.path_to_logs_and_solutions_parent_folder / "solutions",
            cfg.path_to_instance.stem().string(),
            cfg.k,
            cfg.tau);

    std::cout << "Number of loaded IdSolutions for Visualization: " << id_solutions.size() << std::endl;

    /* ---- Visualization ---- */
    for (auto const &id_solution : id_solutions){
        Solution solution(instance, cfg.k, cfg.tau, id_solution.antenna_ids);
        visualize_solution(solution, cfg.path_to_logs_and_solutions_parent_folder / "visualization", id_solution.path.stem());
    }

    return 0;
}