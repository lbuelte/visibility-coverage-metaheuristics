# Engineering Metaheuristics for a Visibility Coverage Problem

This repository contains the supplementary material and C++ code associated with our **GIS Cup 2026** competition submission and the paper:

**Engineering Metaheuristics for a Visibility Coverage Problem**

## Status

🚧 **Code release in preparation**

The code submitted to the GIS Cup 2026 competition is currently being prepared for public release. A documented version of the implementation, including instructions for compilation and execution, will be made available in this repository.

## Authors

* Laura Bülte
* Daniel Faber
* Michael Kaibel
* Benedikt Kolbe
* Philip Mayer
* Lars Müller
* Petra Mutzel
* Felix Roth

## Citation

If you use this code in your research, please cite the associated paper:

> Laura Bülte, Daniel Faber, Michael Kaibel, Benedikt Kolbe, Philip Mayer, Lars Müller, Petra Mutzel, and Felix Roth.
> **Engineering Metaheuristics for a Visibility Coverage Problem.**
> In *Proceedings of the 34th ACM International Conference on Advances in Geographic Information Systems (SIGSPATIAL ’26)*, November 3–6, 2026, Riverside, CA, USA. ACM, New York, NY, USA, 4 pages.
> https://doi.org/10.1145/3841645.3850170

## Code

The competition submission is provided as a self-contained archive. It includes the source code, bundled libraries, and required build files.

The following sections describe how to compile and run the submitted implementation. These instructions will be updated as necessary when the public code is released.

## Dependencies

### Submodules
Project uses googletest as a git submodule. Run
```bash
git submodule update --init --recursive
```
to load the submodule.

### Bundled Libraries

The following libraries are included directly in the submission:

* **GoogleTest**
* **CGAL 6.0.1**
* **Catch2**
* **cxxopts** (`jarro2783/cxxopts`)
* **nlohmann/json**

The header-only libraries `cxxopts.hpp` and `nlohmann/json.hpp` are included directly in the source tree.

CGAL 6.0.1 is also bundled and does not need to be installed separately.

### Required System Libraries

The following libraries must be installed on the system:

* **Boost 1.83**
* **GDAL** (any compatible version)
* **GMP** (compatible with CGAL 6.0.1)
* **MPFR** (compatible with CGAL 6.0.1)
* **OpenMP** (any compatible version)

Standard system packages are generally sufficient for installing GMP and MPFR.

## Compilation

To compile the code:

1. Extract the submission archive.
2. Open a terminal in the extracted directory.
3. Run the build script:

   ```bash
   ./build_binary.sh
   ```

The build script compiles the source code and places the resulting executables in the `build/` directory.

### Build Thread Requirements

The default build configuration uses at least **8 parallel compilation threads**.

If your system has fewer than 8 available threads, modify the `-j` argument in `build_binary.sh` to use a smaller number.

For example, on a system with 4 available threads, change the argument to:

```text
-j 4
```

## Running the Solver

The submission provides several executables for running the solver with different configurations and levels of parallelism. It also includes an executable for visualizing solutions.

All executables are located in the `build/` directory after compilation.

### Available Executables

| Executable                                 | Description                                                                                                                                                |
| ------------------------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `GisCupBonn_RunOne`                        | Runs the solver once with the specified configuration.                                                                                                     |
| `GisCupBonn_RunOneSeededParallel`          | Runs multiple instances of a single solver configuration in parallel, each with a different random seed.                                                   |
| `GisCupBonn_RunCrossproduct`               | Evaluates the full cross-product of the specified parameters sequentially.                                                                                 |
| `GisCupBonn_RunCrossproductParallel`       | Evaluates the full cross-product of the specified parameters in parallel, using one thread per parameter combination.                                      |
| `GisCupBonn_RunCrossproductSeededParallel` | Runs multiple instances of the full cross-product in parallel, each with a different random seed.                                                          |
| `GisCupBonn_VisualizeSolutions`            | Visualizes solutions produced by the solver.                                                                                                               |
| `GisCupBonn_FinalSeededCrossproduct`       | Runs multiple instances of the full cross-product in parallel, each with a different random seed. This executable was used for the final competition runs. |

### Running an Executable

To display the available command-line options of an executable, use its `-h` option. For example:

```bash
./build/GisCupBonn_FinalSeededCrossproduct -h
```

This displays the required arguments and available options for running the solver.

### Cross-Product Execution

The executables with `Crossproduct` in their names evaluate the full cross-product of the specified parameters. Each parameter combination constitutes one solver run and uses one thread.

The `GisCupBonn_RunCrossproduct` executable evaluates the parameter combinations sequentially, whereas `GisCupBonn_RunCrossproductParallel` evaluates them in parallel.

The seeded parallel executables, `GisCupBonn_RunCrossproductSeededParallel` and `GisCupBonn_FinalSeededCrossproduct`, execute multiple instances of the entire cross-product concurrently. Each instance uses a different random seed.

The number of cross-products that can run concurrently depends on the number of available solver threads and the size of the cross-product. Each cross-product requires as many threads as there are parameter combinations.

For example, if the cross-product contains 9 parameter combinations and 20 solver threads are available, 2 cross-products can run concurrently, using 18 threads in total. Each cross-product is executed with a different random seed. The remaining 2 threads are insufficient to start another complete cross-product.

In general, the maximum number of concurrent cross-products is determined by:

```text
floor(number of solver threads / cross-product size)
```

The same principle applies to `GisCupBonn_RunOneSeededParallel`, except that each individual solver run requires only one thread. Consequently, the number of concurrent runs is determined directly by the number of available solver threads.

### Final Competition Executable

The `GisCupBonn_FinalSeededCrossproduct` executable was used for the final competition runs. It evaluates the full cross-product and requires at least **9 solver threads** to execute a complete cross-product.

The number of solver threads is specified using the `-p` option. If `-p` is omitted or set to a value smaller than 9, the program exits without invoking the solver.

For example, to run a complete cross-product with 9 solver threads:

```bash
./build/GisCupBonn_FinalSeededCrossproduct -p 9 <other-options>
```

For further details on the configuration and execution of the final competition runs, see [Reproducing the Competition Runs](#reproducing-the-competition-runs).

## Output Files

For each solver run, several output files are generated. The output path specified with `-o` is used as the root directory. Two subdirectories, `solutions` and `logs`, are created within it to organize the output.

Each run is assigned a unique identifier based on the instance name, the parameter values, the machine thread, the current date and time, and a random component. The identifier has the following structure:

```text
<instance_name>_par_<k>_<tau>_id_<timestamp>_<random>
```

The solution identifier is derived from the run identifier and extended with the number of serviced polygons:

```text
<run_identifier>_val_<number_of_serviced_polygons>
```

All files generated for a run use one of these identifiers.

### Solutions

The `solutions` directory contains the final solution files for each run:

* `<solution_identifier>.solution.giscup`
* `<solution_identifier>.solution.id`

The `.solution.giscup` file contains the solution in the GIS Cup format:

1. The parameter pair \((\tau, k)\).
2. A comma-separated list of the \(k\) antenna coordinates.
3. A comma-separated list of the IDs of the buildings claimed to be serviced.

The `.solution.id` file contains the antenna IDs and serviced polygon IDs in a human-readable format:

```text
Solution antenna IDs:
...

Serviced polygon IDs:
...
```

### Logs

The `logs` directory contains the configuration, log output, and backup solutions for each run:

* `<run_identifier>.config` — the overall configuration used for the run.
* `<run_identifier>.ls.log` — the log output of the local search procedure.
* `<run_identifier>.simann.log` — the log output of the simulated annealing procedure.
* `<run_identifier>.ls.sol` — the best solution found by local search, in GIS Cup format.
* `<run_identifier>.simann.sol` — the best solution found by simulated annealing, in GIS Cup format.

The `.ls.sol` and `.simann.sol` files serve as backup solutions, preserving the best solutions found by the respective procedures.



## Configuration Parameters

The solver supports various command-line parameters to specify the input instance, algorithm, solution parameters, runtime limits, and output options. The configuration interface differs between the regular executables and the final competition executable.

### Regular Executables

The regular executables (`GisCupBonn_RunOne`, `GisCupBonn_RunOneSeededParallel`, `GisCupBonn_RunCrossproduct`, `GisCupBonn_RunCrossproductParallel`, and `GisCupBonn_RunCrossproductSeededParallel`) share the same command-line interface.

The parameters can be divided into general settings, single-run configuration, cross-product configuration, and algorithm-specific settings.

#### General Parameters

| Option                             | Description                                                  | Default                                     |
| ---------------------------------- | ------------------------------------------------------------ | ------------------------------------------- |
| `-i`, `--input_path`               | Path to the input instance file.                             | `data/input/GIS-cup-sample-dataset.geojson` |
| `-o`, `--output_path`              | Path to the output directory.                                | `data/output/debug/`                        |
| `-a`, `--algo`                     | Algorithm to use.                                            | Required                                    |
| `-z`, `--time-limit`               | Time limit for each solver run, in minutes.                  | `5`                                         |
| `-s`, `--seed`, `--master_seed`    | Global seed used to initialize the random number generation. | `42`                                        |
| `-v`, `--visualize`                | Whether to write solution files for visualization.           | `false`                                     |
| `--analyse-solution`               | Whether to perform additional analysis of the solution.      | `false`                                  |
| `-p`, `--threads`, `--num_threads` | Number of threads for parallel seeded runs.                  | `1`                                         |
| `-h`, `--help`                     | Display the available command-line options.                  | —                                           |

The available algorithms are:

* `greedy`
* `greedy-boosted`
* `local-search`
* `simulated-annealing`
* `simann-ls-finish`

The `--threads` parameter controls the number of solver threads used for parallel seeded runs. For cross-product executions, the number of threads required per run depends on the size of the cross-product.

#### Single-Run Configuration (`RunOne`)

The `GisCupBonn_RunOne` executable runs the solver for a single parameter configuration. The number of antennas and the coverage threshold are specified using the following parameters:

| Option                 | Description                                            | Default |
| ---------------------- | ------------------------------------------------------ | ------- |
| `-k`, `--num_antennas` | Number of antennas in the solution.                    | `100`   |
| `-t`, `--threshold`    | Coverage threshold for a building to count as covered. | `0.5`   |

For example, to run the solver with 100 antennas and a coverage threshold of 0.5:

```bash
./build/GisCupBonn_RunOne \
    -i data/input/GIS-cup-sample-dataset.geojson \
    -a local-search \
    -k 100 \
    -t 0.5
```

The `GisCupBonn_RunOneSeededParallel` executable uses the same configuration parameters but allows multiple runs of the same configuration to be executed in parallel, each with a different random seed.

For an example, see and run the script example/script_run_one_seeded.sh.


#### Cross-Product Configuration (`RunCrossproduct`)

The cross-product executables evaluate multiple combinations of the number of antennas (`k`) and the coverage threshold (`tau`).

The `-c` or `--crossproduct` parameter selects one of the predefined parameter sets:

| Value | Number of antennas (`k`)       | Coverage threshold (`tau`) |
| ----- | ------------------------------ | -------------------------- |
| `0`   | No cross-product               | No cross-product           |
| `1`   | `{15, 125, 250}`               | `{0.25, 0.5, 0.75}`        |
| `2`   | `{50, 500, 1000}`              | `{0.25, 0.5, 0.75}`        |
| `3`   | `{50, 500, 1000, 5000, 10000}` | `{0.25, 0.5, 0.75}`        |

Each cross-product consists of all combinations of the specified values of `k` and `tau`. For example, cross-product `1` contains 9 parameter combinations (3 values of `k` × 3 values of `tau`).

The `-c` parameter defaults to `0`. For cross-product executables, a predefined cross-product must be selected to evaluate multiple parameter combinations.

For example, to run cross-product `1` using local search:

```bash
./build/GisCupBonn_RunCrossproduct \
    -i data/input/GIS-cup-sample-dataset.geojson \
    -a local-search \
    -c 1
```

The parallel cross-product executables support parallel evaluation of the parameter combinations. The seeded parallel version additionally allows multiple instances of the entire cross-product to run concurrently, each with a different random seed.

#### Algorithm-Specific Parameters

Some algorithms support additional parameters.

**Local Search**

| Option                             | Description                                                                                                | Default   |
| ---------------------------------- | ---------------------------------------------------------------------------------------------------------- | --------- |
| `--ls-progress-steps`              | Progress step size for naive local search. Must be in `(0, 1]`; ideally, the value should divide 1 evenly. | `0.1`     |
| `--ls-step-iterations`             | Number of local search iterations per step in naive local search.                                          | `3`       |
| `--ls-iterations-to-local-optimum` | Number of iterations before giving up on finding a local optimum in naive local search.                    | `1000000` |

The default iteration limit is sufficiently high that a local optimum should generally be found unless the run reaches its time limit.

**Enrichment and Pruning**

| Option                        | Description                                                 | Default |
| ----------------------------- | ----------------------------------------------------------- | ------- |
| `--bisection-candidates`      | Number of candidates used for bisection enrichment.         | `0`     |
| `--multi-interval-rounds`     | Number of rounds of multi-interval enrichment.              | `0`     |
| `--polygon-pruning-threshold` | Threshold for pruning based on the number of seen polygons. | `0`     |

**Configuration Files**

The simulated annealing and local search algorithms support additional configuration files.

| Option                                           | Description                                         | Default                            |
| ------------------------------------------------ | --------------------------------------------------- | ---------------------------------- |
| `--sa-f`, `--sa-config`, `--simann-config`       | Path to the simulated annealing configuration file. | `configs/simann_base_config.toml`  |
| `--ls-f`, `--ls-config`, `--local-search-config` | Path to the local search configuration file.        | `configs/local_search_config.toml` |

These files contain algorithm-specific parameters and can be used to experiment with different configurations.


### Final Competition Executable

The `GisCupBonn_FinalSeededCrossproduct` executable uses a separate configuration interface. Unlike the regular cross-product executables, it reads the input instance and the parameter cross-product from a TOML file.

#### Parameter File

The parameter file specifies the input instance and the values of `k` and `tau` to be evaluated.

For example, `competition_params.toml` can contain:

```toml
instance_file = "data/input/GIS-cup-competition-dataset.geojson"

k = [9, 49, 484]
tau = [0.32, 0.49, 0.68]
```

The solver evaluates the full Cartesian product of the specified values. In this example, the cross-product consists of 9 parameter combinations (3 values of `k` × 3 values of `tau`).

The `instance_file` parameter specifies the path to the input instance. The `k` and `tau` parameters specify the values of the number of antennas and the coverage threshold, respectively.

#### Command-Line Parameters

The final executable accepts the following command-line parameters:

| Option                                                   | Description                                                                                                | Default                            |
| -------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------- | ---------------------------------- |
| `-f`, `--final`, `--final_params`, `--final_params_file` | Path to the TOML parameter file.                                                                           | Required                           |
| `-o`, `--output_path`                                    | Path to the output directory.                                                                              | `data/output/final/`               |
| `-a`, `--algo`                                           | Algorithm to use.                                                                                          | Required                           |
| `-d`, `--deadline`, `--time-limit-clock`                 | Absolute deadline in the format `YYYY-MM-DD HH:MM:SS`.                                                     | Required                               |
| `-s`, `--seed`, `--master_seed`                          | Global seed used to initialize the random number generation.                                               | `42`                               |
| `-p`, `--threads`, `--num_threads`                       | Number of solver threads available for parallel seeded runs.                                               | `1`                                |
| `-v`, `--visualize`                                      | Whether to write solution files for visualization.                                                         | `false`                            |
| `--analyse-solution`                                     | Whether to perform additional analysis of the solution.                                                    | `false`                         |
| `--ls-progress-steps`                                    | Progress step size for naive local search. Must be in `(0, 1]`; ideally, the value should divide 1 evenly. | `0.1`                              |
| `--ls-step-iterations`                                   | Number of local search iterations per step in naive local search.                                          | `3`                                |
| `--ls-iterations-to-local-optimum`                       | Number of iterations before giving up on finding a local optimum in naive local search.                    | `1000000`                          |
| `--bisection-candidates`                                 | Number of candidates used for bisection enrichment.                                                        | `0`                                |
| `--multi-interval-rounds`                                | Number of rounds of multi-interval enrichment.                                                             | `0`                                |
| `--polygon-pruning-threshold`                            | Threshold for pruning based on the number of seen polygons.                                                | `0`                                |
| `--sa-f`, `--sa-config`, `--simann-config`               | Path to the simulated annealing configuration file.                                                        | `configs/simann_base_config.toml`  |
| `--ls-f`, `--ls-config`, `--local-search-config`         | Path to the local search configuration file.                                                               | `configs/local_search_config.toml` |
| `-h`, `--help`                                           | Display the available command-line options.                                                                | —                                  |

The supported algorithms are:

* `greedy`
* `greedy-boosted`
* `local-search`
* `simulated-annealing`
* `simann-ls-finish`

The algorithm is selected using the `-a` option. The final competition runs used `simann-ls-finish` and `local-search`.

#### Parallel Execution

The final executable evaluates the full cross-product specified in the TOML file. Each complete cross-product is executed with a different random seed.

Each parameter combination requires one solver thread. Consequently, the number of threads required to execute one complete cross-product is equal to the number of parameter combinations.

The executable uses the available solver threads to determine how many complete cross-products can be executed concurrently.

For example, if the parameter file specifies 9 combinations and 20 solver threads are available, 2 complete cross-products can run concurrently, each with a different seed.

At least 9 solver threads are required to execute a complete cross-product. If fewer than 9 threads are specified using `-p`, the program exits without invoking the solver.

#### Example Invocation

For an example, see and run the script in example/script_competition_run.sh.

The following command runs the solver using the example parameter file, the `simann-ls-finish` algorithm, and 18 solver threads:

```bash
./build/GisCupBonn_FinalSeededCrossproduct \
    -f competition_params.toml \
    -a simann-ls-finish \
    -p 18
```

The output directory can be changed using `-o`. The master seed can be specified using `-s`.

An absolute deadline can be provided using `-d`, for example:

```bash
-d "YYYY-MM-DD HH:MM:SS"
```

The solver uses the deadline to limit the execution time of the final runs. The timezone should be the same as the server you are running the code on.


## License

The license information will be added with the public code release.
