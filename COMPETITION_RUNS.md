# Competition Runs

This document describes the solver configurations used for the GIS Cup 2026 competition. We evaluated several configurations of simulated annealing (SA) and local search (LS). For each configuration, we executed a seeded cross-product using the `GisCupBonn_FinalSeededCrossproduct` executable.

Each configuration was run independently on a compute node. Different random seeds were used for the runs to distinguish the experiments.

## Common Configuration

All competition runs used the following common settings:

| Parameter                | Value                                        | Description                                                              |
| ------------------------ | -------------------------------------------- | ------------------------------------------------------------------------ |
| Executable               | `./build/GisCupBonn_FinalSeededCrossproduct` | Executable used to run the solver.                                       |
| Parameter file           | `competition_params.toml`                    | TOML file specifying the input instance and the parameter cross-product. |
| Output directory         | `data/output/final`                          | Directory in which the solver writes its output.                         |
| Number of threads        | `90`                                         | Number of solver threads available for each run.                         |
| Deadline                 | `2026-08-16 16:00:00`                        | Absolute execution deadline.                                             |
| Default SA configuration | `configs/simann_base_config.toml`            | Configuration file used for simulated annealing unless overridden.       |
| Default LS configuration | `configs/local_search_config.toml`           | Configuration file used for local search unless overridden.       |
| Default pruning          | Disabled (`0`)                               | Polygon pruning was disabled unless explicitly enabled for a run.        |

Each run evaluated the full cross-product specified in `competition_params.toml`. The executable used the available solver threads to execute multiple parameter combinations and, where possible, multiple seeded cross-products concurrently.

The configurations below differ in the selected algorithm, random seed, simulated annealing or local search configuration file, and polygon pruning settings.

## Simulated Annealing Configurations

The following runs used the `simann-ls-finish` algorithm. The configuration files and pruning settings shown below override the common defaults where specified.

| Run     | Seed | SA configuration file                         | Polygon pruning threshold |
| ------- | ---: | --------------------------------------------- | ------------------------: |
| `run_2` |    3 | `configs/simann_base_config.toml`             |                  Disabled |
| `run_4` |    5 | `configs/simann_greedy_config.toml`           |                  Disabled |
| `run_5` |    6 | `configs/simann_base_config.toml`             |                        10 |
| `run_6` |    7 | `configs/simann_base_config.toml`             |                        15 |
| `run_7` |    8 | `configs/simann_no_reheat_config.toml`        |                  Disabled |
| `run_8` |    9 | `configs/simann_special_reheat_config.toml`   |                  Disabled |
| `run_9` |   17 | `configs/simann_cluster_exchange_config.toml` |                  Disabled |

All these runs used the same parameter file, output directory, thread count, and deadline described in the common configuration.

The SA configuration files are included in the repository under `configs/`. They specify the algorithm-specific settings used in each experiment.

## Local Search Configurations

The following runs used the `local-search` algorithm.

| Run      | Seed | Local search configuration file             | Polygon pruning threshold |
| -------- | ---: | ------------------------------------------- | ------------------------: |
| `run_3`  |    4 | `configs/local_search_config.toml`          |                  Disabled |
| `run_10` |   18 | `configs/local_search_2opt_10_config.toml`  |                  Disabled |
| `run_11` |   19 | `configs/local_search_2opt_100_config.toml` |                  Disabled |

The default local search configuration was used for `run_3`. The other runs used the alternative configuration files shown in the table.

All local search runs used the same parameter file, output directory, thread count, and deadline described in the common configuration.

The local search configuration files are included in the repository under `configs/`.

## Configuration Files

All configuration files referenced in this document are provided in the repository:

* `configs/simann_base_config.toml`
* `configs/simann_greedy_config.toml`
* `configs/simann_no_reheat_config.toml`
* `configs/simann_special_reheat_config.toml`
* `configs/simann_cluster_exchange_config.toml`
* `configs/local_search_config.toml`
* `configs/local_search_2opt_10_config.toml`
* `configs/local_search_2opt_100_config.toml`

These files contain the algorithm-specific parameters used for the corresponding experiments. Their contents are provided alongside the source code to support reproducibility.
