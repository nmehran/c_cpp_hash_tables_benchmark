// c_cpp_hash_tables_benchmark/main.cpp
// Copyright (c) 2024 Jackson L. Allan.
// Distributed under the MIT License (see the accompanying LICENSE file).

#include <algorithm>
#include <array>
#include <chrono>
#include <concepts>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <thread>
#include <vector>
#include <string>
#include <map>
#include <set>
#include <numeric>
#include <tuple>

#include "config.h"

// Benchmark ids.
enum benchmark_ids
{
  insert_nonexisting,
  reinsert_after_erasure,
  erase_existing,
  insert_existing,
  erase_nonexisting,
  get_existing,
  get_nonexisting,
  iteration,
  benchmark_id_count // Sentinel value
};

// Benchmark names used in the heatmap and CSV.
const char *benchmark_names[] = {
  "Insert nonexisting", "Reinsert after erasure", "Erase existing",
  "Replace existing", "Erase nonexisting", "Look up existing",
  "Look up nonexisting", "Iterate"
};

// Benchmark titles used in the graphs.
const char *benchmark_graph_titles[] = {
  "Total time to insert N nonexisting keys",
  "Total time to reinsert N keys after N-1 erasures",
  "Time to erase 1,000 existing keys with N keys in the table",
  "Time to replace 1,000 existing keys with N keys in the table",
  "Time to erase 1,000 nonexisting keys with N keys in the table",
  "Time to look up 1,000 existing keys with N keys in the table",
  "Time to look up 1,000 nonexisting keys with N keys in the table",
  "Time to iterate over 5,000 keys with N keys in the table"
};

// A key to uniquely identify a specific test run (Blueprint + Shim + Benchmark ID).
using ArenaKey = std::tuple<std::string, std::string, benchmark_ids>;

// Struct to hold information about a registered shim/blueprint.
struct RegisteredShimInfo { std::string label; std::string color; bool has_tombstone_mechanism; };
struct BlueprintRuntimeInfo { std::string label; };
using RegisteredBlueprintInfo = BlueprintRuntimeInfo; // Alias for compatibility with benchmark_output.h

// Global Data Structures for the Output Generator
std::vector<RegisteredShimInfo> g_registered_shims_info;
std::vector<std::string> g_all_shim_labels;
std::map<std::string, bool> g_shim_is_tombstone;
std::vector<RegisteredBlueprintInfo> g_registered_blueprints_info;
std::vector<benchmark_ids> g_enabled_benchmark_ids_list;
std::map<ArenaKey, std::vector<std::vector<uint64_t>>> g_raw_micro_data; // For CSV
std::map<ArenaKey, std::vector<double>> g_final_median_point_averages; // For HTML graphs
std::map<ArenaKey, double> g_final_max_point_values; // For HTML graphs
std::map<ArenaKey, double> g_final_median_performance_times; // For HTML heatmap
const std::set<benchmark_ids> g_display_tombstones_of_benchmarks = { erase_existing, reinsert_after_erasure };
const std::vector<benchmark_ids> g_display_order_of_benchmarks = {
    insert_nonexisting, reinsert_after_erasure, get_existing, get_nonexisting,
    insert_existing, erase_existing, erase_nonexisting, iteration
};

// Include the output generator which depends on the above definitions
#include "benchmark_output.h"

// Check configuration.
static_assert( KEY_COUNT % KEY_COUNT_MEASUREMENT_INTERVAL == 0 );
static_assert( DISCARDED_RUNS_COUNT < RUN_COUNT );
static_assert( DISCARDED_RUNS_COUNT % 2 == 0 );

// Standard stringification macro.
#define STRINGIFY_( x ) #x
#define STRINGIFY( x ) STRINGIFY_( x )

// Variable to prevent over-optimization.
size_t do_not_optimize;

// Concept to check that a blueprint is correctly formed.
template< typename blueprint >
concept check_blueprint =
  std::is_object< typename blueprint::value_type >::value &&
  std::same_as< decltype( blueprint::label ), const char * const > &&
  std::same_as< decltype( blueprint::hash_key ), uint64_t ( const typename blueprint::key_type & ) > &&
  std::same_as<
    decltype( blueprint::cmpr_keys ),
    bool ( const typename blueprint::key_type &, const typename blueprint::key_type & )
  > &&
  std::same_as< decltype( blueprint::fill_unique_keys ), void ( std::vector< typename blueprint::key_type > & ) >
;

// Concept to check that a shim is, in isolation, correctly formed.
template< template< typename > typename shim >
concept check_shim =
  std::same_as< decltype( shim< void >::label ), const char * const > &&
  std::same_as< decltype( shim< void >::color ), const char * const > &&
  std::same_as< decltype( shim< void >::tombstone_like_mechanism ), const bool >
;

// Concept to check that a shim is correctly formed in relation to a specific blueprint.
template<
  template< typename > typename shim,
  typename blueprint,
  // Helpers to simplify the concept's definition:
  typename table_type = decltype( shim< blueprint >::create_table() ),
  typename itr_type = decltype( shim< blueprint >::begin_itr( std::declval< table_type & >() ) )
>
concept check_shim_against_blueprint =
  std::same_as< decltype( shim< blueprint >::create_table ), table_type () > &&
  std::same_as< decltype( shim< blueprint >::insert ), void ( table_type &, const typename blueprint::key_type & ) > &&
  std::same_as< decltype( shim< blueprint >::erase ), void ( table_type &, const typename blueprint::key_type & ) > &&
  std::same_as<
    decltype( shim< blueprint >::find ),
    itr_type ( table_type &, const typename blueprint::key_type & )
  > &&
  std::same_as< decltype( shim< blueprint >::begin_itr ), itr_type ( table_type & ) > &&
  std::same_as< decltype( shim< blueprint >::is_itr_valid ), bool ( table_type &, itr_type & ) > &&
  std::same_as< decltype( shim< blueprint >::increment_itr ), void ( table_type &, itr_type & ) > &&
  std::same_as<
    decltype( shim< blueprint >::get_key_from_itr ),
    const typename blueprint::key_type &( table_type &, itr_type & )
  > &&
  std::same_as<
    decltype( shim< blueprint >::get_value_from_itr ),
    const typename blueprint::value_type &( table_type &, itr_type & )
  > &&
  std::same_as< decltype( shim< blueprint >::destroy_table ), void ( table_type & ) >
;

// #include blueprints and check them for correctness.
#ifdef BLUEPRINT_1
#include STRINGIFY( blueprints/BLUEPRINT_1/blueprint.h )
static_assert( check_blueprint< BLUEPRINT_1 > );
#endif
#ifdef BLUEPRINT_2
#include STRINGIFY( blueprints/BLUEPRINT_2/blueprint.h )
static_assert( check_blueprint< BLUEPRINT_2 > );
#endif
#ifdef BLUEPRINT_3
#include STRINGIFY( blueprints/BLUEPRINT_3/blueprint.h )
static_assert( check_blueprint< BLUEPRINT_3 > );
#endif
#ifdef BLUEPRINT_4
#include STRINGIFY( blueprints/BLUEPRINT_4/blueprint.h )
static_assert( check_blueprint< BLUEPRINT_4 > );
#endif
#ifdef BLUEPRINT_5
#include STRINGIFY( blueprints/BLUEPRINT_5/blueprint.h )
static_assert( check_blueprint< BLUEPRINT_5 > );
#endif
#ifdef BLUEPRINT_6
#include STRINGIFY( blueprints/BLUEPRINT_6/blueprint.h )
static_assert( check_blueprint< BLUEPRINT_6 > );
#endif
#ifdef BLUEPRINT_7
#include STRINGIFY( blueprints/BLUEPRINT_7/blueprint.h )
static_assert( check_blueprint< BLUEPRINT_7 > );
#endif
#ifdef BLUEPRINT_8
#include STRINGIFY( blueprints/BLUEPRINT_8/blueprint.h )
static_assert( check_blueprint< BLUEPRINT_8 > );
#endif
#ifdef BLUEPRINT_9
#include STRINGIFY( blueprints/BLUEPRINT_9/blueprint.h )
static_assert( check_blueprint< BLUEPRINT_9 > );
#endif
#ifdef BLUEPRINT_10
#include STRINGIFY( blueprints/BLUEPRINT_10/blueprint.h )
static_assert( check_blueprint< BLUEPRINT_10 > );
#endif
#ifdef BLUEPRINT_11
#include STRINGIFY( blueprints/BLUEPRINT_11/blueprint.h )
static_assert( check_blueprint< BLUEPRINT_11 > );
#endif
#ifdef BLUEPRINT_12
#include STRINGIFY( blueprints/BLUEPRINT_12/blueprint.h )
static_assert( check_blueprint< BLUEPRINT_12 > );
#endif
#ifdef BLUEPRINT_13
#include STRINGIFY( blueprints/BLUEPRINT_13/blueprint.h )
static_assert( check_blueprint< BLUEPRINT_13 > );
#endif
#ifdef BLUEPRINT_14
#include STRINGIFY( blueprints/BLUEPRINT_14/blueprint.h )
static_assert( check_blueprint< BLUEPRINT_14 > );
#endif
#ifdef BLUEPRINT_15
#include STRINGIFY( blueprints/BLUEPRINT_15/blueprint.h )
static_assert( check_blueprint< BLUEPRINT_15 > );
#endif
#ifdef BLUEPRINT_16
#include STRINGIFY( blueprints/BLUEPRINT_16/blueprint.h )
static_assert( check_blueprint< BLUEPRINT_16 > );
#endif

// #include shims and check them for correctness.
#ifdef SHIM_1
#include STRINGIFY( shims/SHIM_1/shim.h )
static_assert( check_shim< SHIM_1 > );
#ifdef BLUEPRINT_1
static_assert( check_shim_against_blueprint< SHIM_1, BLUEPRINT_1 > );
#endif
#ifdef BLUEPRINT_2
static_assert( check_shim_against_blueprint< SHIM_1, BLUEPRINT_2 > );
#endif
#ifdef BLUEPRINT_3
static_assert( check_shim_against_blueprint< SHIM_1, BLUEPRINT_3 > );
#endif
#ifdef BLUEPRINT_4
static_assert( check_shim_against_blueprint< SHIM_1, BLUEPRINT_4 > );
#endif
#ifdef BLUEPRINT_5
static_assert( check_shim_against_blueprint< SHIM_1, BLUEPRINT_5 > );
#endif
#ifdef BLUEPRINT_6
static_assert( check_shim_against_blueprint< SHIM_1, BLUEPRINT_6 > );
#endif
#ifdef BLUEPRINT_7
static_assert( check_shim_against_blueprint< SHIM_1, BLUEPRINT_7 > );
#endif
#ifdef BLUEPRINT_8
static_assert( check_shim_against_blueprint< SHIM_1, BLUEPRINT_8 > );
#endif
#ifdef BLUEPRINT_9
static_assert( check_shim_against_blueprint< SHIM_1, BLUEPRINT_9 > );
#endif
#ifdef BLUEPRINT_10
static_assert( check_shim_against_blueprint< SHIM_1, BLUEPRINT_10 > );
#endif
#ifdef BLUEPRINT_11
static_assert( check_shim_against_blueprint< SHIM_1, BLUEPRINT_11 > );
#endif
#ifdef BLUEPRINT_12
static_assert( check_shim_against_blueprint< SHIM_1, BLUEPRINT_12 > );
#endif
#ifdef BLUEPRINT_13
static_assert( check_shim_against_blueprint< SHIM_1, BLUEPRINT_13 > );
#endif
#ifdef BLUEPRINT_14
static_assert( check_shim_against_blueprint< SHIM_1, BLUEPRINT_14 > );
#endif
#ifdef BLUEPRINT_15
static_assert( check_shim_against_blueprint< SHIM_1, BLUEPRINT_15 > );
#endif
#ifdef BLUEPRINT_16
static_assert( check_shim_against_blueprint< SHIM_1, BLUEPRINT_16 > );
#endif
#endif

#ifdef SHIM_2
#include STRINGIFY( shims/SHIM_2/shim.h )
static_assert( check_shim< SHIM_2 > );
#ifdef BLUEPRINT_1
static_assert( check_shim_against_blueprint< SHIM_2, BLUEPRINT_1 > );
#endif
#ifdef BLUEPRINT_2
static_assert( check_shim_against_blueprint< SHIM_2, BLUEPRINT_2 > );
#endif
#ifdef BLUEPRINT_3
static_assert( check_shim_against_blueprint< SHIM_2, BLUEPRINT_3 > );
#endif
#ifdef BLUEPRINT_4
static_assert( check_shim_against_blueprint< SHIM_2, BLUEPRINT_4 > );
#endif
#ifdef BLUEPRINT_5
static_assert( check_shim_against_blueprint< SHIM_2, BLUEPRINT_5 > );
#endif
#ifdef BLUEPRINT_6
static_assert( check_shim_against_blueprint< SHIM_2, BLUEPRINT_6 > );
#endif
#ifdef BLUEPRINT_7
static_assert( check_shim_against_blueprint< SHIM_2, BLUEPRINT_7 > );
#endif
#ifdef BLUEPRINT_8
static_assert( check_shim_against_blueprint< SHIM_2, BLUEPRINT_8 > );
#endif
#ifdef BLUEPRINT_9
static_assert( check_shim_against_blueprint< SHIM_2, BLUEPRINT_9 > );
#endif
#ifdef BLUEPRINT_10
static_assert( check_shim_against_blueprint< SHIM_2, BLUEPRINT_10 > );
#endif
#ifdef BLUEPRINT_11
static_assert( check_shim_against_blueprint< SHIM_2, BLUEPRINT_11 > );
#endif
#ifdef BLUEPRINT_12
static_assert( check_shim_against_blueprint< SHIM_2, BLUEPRINT_12 > );
#endif
#ifdef BLUEPRINT_13
static_assert( check_shim_against_blueprint< SHIM_2, BLUEPRINT_13 > );
#endif
#ifdef BLUEPRINT_14
static_assert( check_shim_against_blueprint< SHIM_2, BLUEPRINT_14 > );
#endif
#ifdef BLUEPRINT_15
static_assert( check_shim_against_blueprint< SHIM_2, BLUEPRINT_15 > );
#endif
#ifdef BLUEPRINT_16
static_assert( check_shim_against_blueprint< SHIM_2, BLUEPRINT_16 > );
#endif
#endif

#ifdef SHIM_3
#include STRINGIFY( shims/SHIM_3/shim.h )
static_assert( check_shim< SHIM_3 > );
#ifdef BLUEPRINT_1
static_assert( check_shim_against_blueprint< SHIM_3, BLUEPRINT_1 > );
#endif
#ifdef BLUEPRINT_2
static_assert( check_shim_against_blueprint< SHIM_3, BLUEPRINT_2 > );
#endif
#ifdef BLUEPRINT_3
static_assert( check_shim_against_blueprint< SHIM_3, BLUEPRINT_3 > );
#endif
#ifdef BLUEPRINT_4
static_assert( check_shim_against_blueprint< SHIM_3, BLUEPRINT_4 > );
#endif
#ifdef BLUEPRINT_5
static_assert( check_shim_against_blueprint< SHIM_3, BLUEPRINT_5 > );
#endif
#ifdef BLUEPRINT_6
static_assert( check_shim_against_blueprint< SHIM_3, BLUEPRINT_6 > );
#endif
#ifdef BLUEPRINT_7
static_assert( check_shim_against_blueprint< SHIM_3, BLUEPRINT_7 > );
#endif
#ifdef BLUEPRINT_8
static_assert( check_shim_against_blueprint< SHIM_3, BLUEPRINT_8 > );
#endif
#ifdef BLUEPRINT_9
static_assert( check_shim_against_blueprint< SHIM_3, BLUEPRINT_9 > );
#endif
#ifdef BLUEPRINT_10
static_assert( check_shim_against_blueprint< SHIM_3, BLUEPRINT_10 > );
#endif
#ifdef BLUEPRINT_11
static_assert( check_shim_against_blueprint< SHIM_3, BLUEPRINT_11 > );
#endif
#ifdef BLUEPRINT_12
static_assert( check_shim_against_blueprint< SHIM_3, BLUEPRINT_12 > );
#endif
#ifdef BLUEPRINT_13
static_assert( check_shim_against_blueprint< SHIM_3, BLUEPRINT_13 > );
#endif
#ifdef BLUEPRINT_14
static_assert( check_shim_against_blueprint< SHIM_3, BLUEPRINT_14 > );
#endif
#ifdef BLUEPRINT_15
static_assert( check_shim_against_blueprint< SHIM_3, BLUEPRINT_15 > );
#endif
#ifdef BLUEPRINT_16
static_assert( check_shim_against_blueprint< SHIM_3, BLUEPRINT_16 > );
#endif
#endif

#ifdef SHIM_4
#include STRINGIFY( shims/SHIM_4/shim.h )
static_assert( check_shim< SHIM_4 > );
#ifdef BLUEPRINT_1
static_assert( check_shim_against_blueprint< SHIM_4, BLUEPRINT_1 > );
#endif
#ifdef BLUEPRINT_2
static_assert( check_shim_against_blueprint< SHIM_4, BLUEPRINT_2 > );
#endif
#ifdef BLUEPRINT_3
static_assert( check_shim_against_blueprint< SHIM_4, BLUEPRINT_3 > );
#endif
#ifdef BLUEPRINT_4
static_assert( check_shim_against_blueprint< SHIM_4, BLUEPRINT_4 > );
#endif
#ifdef BLUEPRINT_5
static_assert( check_shim_against_blueprint< SHIM_4, BLUEPRINT_5 > );
#endif
#ifdef BLUEPRINT_6
static_assert( check_shim_against_blueprint< SHIM_4, BLUEPRINT_6 > );
#endif
#ifdef BLUEPRINT_7
static_assert( check_shim_against_blueprint< SHIM_4, BLUEPRINT_7 > );
#endif
#ifdef BLUEPRINT_8
static_assert( check_shim_against_blueprint< SHIM_4, BLUEPRINT_8 > );
#endif
#ifdef BLUEPRINT_9
static_assert( check_shim_against_blueprint< SHIM_4, BLUEPRINT_9 > );
#endif
#ifdef BLUEPRINT_10
static_assert( check_shim_against_blueprint< SHIM_4, BLUEPRINT_10 > );
#endif
#ifdef BLUEPRINT_11
static_assert( check_shim_against_blueprint< SHIM_4, BLUEPRINT_11 > );
#endif
#ifdef BLUEPRINT_12
static_assert( check_shim_against_blueprint< SHIM_4, BLUEPRINT_12 > );
#endif
#ifdef BLUEPRINT_13
static_assert( check_shim_against_blueprint< SHIM_4, BLUEPRINT_13 > );
#endif
#ifdef BLUEPRINT_14
static_assert( check_shim_against_blueprint< SHIM_4, BLUEPRINT_14 > );
#endif
#ifdef BLUEPRINT_15
static_assert( check_shim_against_blueprint< SHIM_4, BLUEPRINT_15 > );
#endif
#ifdef BLUEPRINT_16
static_assert( check_shim_against_blueprint< SHIM_4, BLUEPRINT_16 > );
#endif
#endif

#ifdef SHIM_5
#include STRINGIFY( shims/SHIM_5/shim.h )
static_assert( check_shim< SHIM_5 > );
#ifdef BLUEPRINT_1
static_assert( check_shim_against_blueprint< SHIM_5, BLUEPRINT_1 > );
#endif
#ifdef BLUEPRINT_2
static_assert( check_shim_against_blueprint< SHIM_5, BLUEPRINT_2 > );
#endif
#ifdef BLUEPRINT_3
static_assert( check_shim_against_blueprint< SHIM_5, BLUEPRINT_3 > );
#endif
#ifdef BLUEPRINT_4
static_assert( check_shim_against_blueprint< SHIM_5, BLUEPRINT_4 > );
#endif
#ifdef BLUEPRINT_5
static_assert( check_shim_against_blueprint< SHIM_5, BLUEPRINT_5 > );
#endif
#ifdef BLUEPRINT_6
static_assert( check_shim_against_blueprint< SHIM_5, BLUEPRINT_6 > );
#endif
#ifdef BLUEPRINT_7
static_assert( check_shim_against_blueprint< SHIM_5, BLUEPRINT_7 > );
#endif
#ifdef BLUEPRINT_8
static_assert( check_shim_against_blueprint< SHIM_5, BLUEPRINT_8 > );
#endif
#ifdef BLUEPRINT_9
static_assert( check_shim_against_blueprint< SHIM_5, BLUEPRINT_9 > );
#endif
#ifdef BLUEPRINT_10
static_assert( check_shim_against_blueprint< SHIM_5, BLUEPRINT_10 > );
#endif
#ifdef BLUEPRINT_11
static_assert( check_shim_against_blueprint< SHIM_5, BLUEPRINT_11 > );
#endif
#ifdef BLUEPRINT_12
static_assert( check_shim_against_blueprint< SHIM_5, BLUEPRINT_12 > );
#endif
#ifdef BLUEPRINT_13
static_assert( check_shim_against_blueprint< SHIM_5, BLUEPRINT_13 > );
#endif
#ifdef BLUEPRINT_14
static_assert( check_shim_against_blueprint< SHIM_5, BLUEPRINT_14 > );
#endif
#ifdef BLUEPRINT_15
static_assert( check_shim_against_blueprint< SHIM_5, BLUEPRINT_15 > );
#endif
#ifdef BLUEPRINT_16
static_assert( check_shim_against_blueprint< SHIM_5, BLUEPRINT_16 > );
#endif
#endif

#ifdef SHIM_6
#include STRINGIFY( shims/SHIM_6/shim.h )
static_assert( check_shim< SHIM_6 > );
#ifdef BLUEPRINT_1
static_assert( check_shim_against_blueprint< SHIM_6, BLUEPRINT_1 > );
#endif
#ifdef BLUEPRINT_2
static_assert( check_shim_against_blueprint< SHIM_6, BLUEPRINT_2 > );
#endif
#ifdef BLUEPRINT_3
static_assert( check_shim_against_blueprint< SHIM_6, BLUEPRINT_3 > );
#endif
#ifdef BLUEPRINT_4
static_assert( check_shim_against_blueprint< SHIM_6, BLUEPRINT_4 > );
#endif
#ifdef BLUEPRINT_5
static_assert( check_shim_against_blueprint< SHIM_6, BLUEPRINT_5 > );
#endif
#ifdef BLUEPRINT_6
static_assert( check_shim_against_blueprint< SHIM_6, BLUEPRINT_6 > );
#endif
#ifdef BLUEPRINT_7
static_assert( check_shim_against_blueprint< SHIM_6, BLUEPRINT_7 > );
#endif
#ifdef BLUEPRINT_8
static_assert( check_shim_against_blueprint< SHIM_6, BLUEPRINT_8 > );
#endif
#ifdef BLUEPRINT_9
static_assert( check_shim_against_blueprint< SHIM_6, BLUEPRINT_9 > );
#endif
#ifdef BLUEPRINT_10
static_assert( check_shim_against_blueprint< SHIM_6, BLUEPRINT_10 > );
#endif
#ifdef BLUEPRINT_11
static_assert( check_shim_against_blueprint< SHIM_6, BLUEPRINT_11 > );
#endif
#ifdef BLUEPRINT_12
static_assert( check_shim_against_blueprint< SHIM_6, BLUEPRINT_12 > );
#endif
#ifdef BLUEPRINT_13
static_assert( check_shim_against_blueprint< SHIM_6, BLUEPRINT_13 > );
#endif
#ifdef BLUEPRINT_14
static_assert( check_shim_against_blueprint< SHIM_6, BLUEPRINT_14 > );
#endif
#ifdef BLUEPRINT_15
static_assert( check_shim_against_blueprint< SHIM_6, BLUEPRINT_15 > );
#endif
#ifdef BLUEPRINT_16
static_assert( check_shim_against_blueprint< SHIM_6, BLUEPRINT_16 > );
#endif
#endif

#ifdef SHIM_7
#include STRINGIFY( shims/SHIM_7/shim.h )
static_assert( check_shim< SHIM_7 > );
#ifdef BLUEPRINT_1
static_assert( check_shim_against_blueprint< SHIM_7, BLUEPRINT_1 > );
#endif
#ifdef BLUEPRINT_2
static_assert( check_shim_against_blueprint< SHIM_7, BLUEPRINT_2 > );
#endif
#ifdef BLUEPRINT_3
static_assert( check_shim_against_blueprint< SHIM_7, BLUEPRINT_3 > );
#endif
#ifdef BLUEPRINT_4
static_assert( check_shim_against_blueprint< SHIM_7, BLUEPRINT_4 > );
#endif
#ifdef BLUEPRINT_5
static_assert( check_shim_against_blueprint< SHIM_7, BLUEPRINT_5 > );
#endif
#ifdef BLUEPRINT_6
static_assert( check_shim_against_blueprint< SHIM_7, BLUEPRINT_6 > );
#endif
#ifdef BLUEPRINT_7
static_assert( check_shim_against_blueprint< SHIM_7, BLUEPRINT_7 > );
#endif
#ifdef BLUEPRINT_8
static_assert( check_shim_against_blueprint< SHIM_7, BLUEPRINT_8 > );
#endif
#ifdef BLUEPRINT_9
static_assert( check_shim_against_blueprint< SHIM_7, BLUEPRINT_9 > );
#endif
#ifdef BLUEPRINT_10
static_assert( check_shim_against_blueprint< SHIM_7, BLUEPRINT_10 > );
#endif
#ifdef BLUEPRINT_11
static_assert( check_shim_against_blueprint< SHIM_7, BLUEPRINT_11 > );
#endif
#ifdef BLUEPRINT_12
static_assert( check_shim_against_blueprint< SHIM_7, BLUEPRINT_12 > );
#endif
#ifdef BLUEPRINT_13
static_assert( check_shim_against_blueprint< SHIM_7, BLUEPRINT_13 > );
#endif
#ifdef BLUEPRINT_14
static_assert( check_shim_against_blueprint< SHIM_7, BLUEPRINT_14 > );
#endif
#ifdef BLUEPRINT_15
static_assert( check_shim_against_blueprint< SHIM_7, BLUEPRINT_15 > );
#endif
#ifdef BLUEPRINT_16
static_assert( check_shim_against_blueprint< SHIM_7, BLUEPRINT_16 > );
#endif
#endif

#ifdef SHIM_8
#include STRINGIFY( shims/SHIM_8/shim.h )
static_assert( check_shim< SHIM_8 > );
#ifdef BLUEPRINT_1
static_assert( check_shim_against_blueprint< SHIM_8, BLUEPRINT_1 > );
#endif
#ifdef BLUEPRINT_2
static_assert( check_shim_against_blueprint< SHIM_8, BLUEPRINT_2 > );
#endif
#ifdef BLUEPRINT_3
static_assert( check_shim_against_blueprint< SHIM_8, BLUEPRINT_3 > );
#endif
#ifdef BLUEPRINT_4
static_assert( check_shim_against_blueprint< SHIM_8, BLUEPRINT_4 > );
#endif
#ifdef BLUEPRINT_5
static_assert( check_shim_against_blueprint< SHIM_8, BLUEPRINT_5 > );
#endif
#ifdef BLUEPRINT_6
static_assert( check_shim_against_blueprint< SHIM_8, BLUEPRINT_6 > );
#endif
#ifdef BLUEPRINT_7
static_assert( check_shim_against_blueprint< SHIM_8, BLUEPRINT_7 > );
#endif
#ifdef BLUEPRINT_8
static_assert( check_shim_against_blueprint< SHIM_8, BLUEPRINT_8 > );
#endif
#ifdef BLUEPRINT_9
static_assert( check_shim_against_blueprint< SHIM_8, BLUEPRINT_9 > );
#endif
#ifdef BLUEPRINT_10
static_assert( check_shim_against_blueprint< SHIM_8, BLUEPRINT_10 > );
#endif
#ifdef BLUEPRINT_11
static_assert( check_shim_against_blueprint< SHIM_8, BLUEPRINT_11 > );
#endif
#ifdef BLUEPRINT_12
static_assert( check_shim_against_blueprint< SHIM_8, BLUEPRINT_12 > );
#endif
#ifdef BLUEPRINT_13
static_assert( check_shim_against_blueprint< SHIM_8, BLUEPRINT_13 > );
#endif
#ifdef BLUEPRINT_14
static_assert( check_shim_against_blueprint< SHIM_8, BLUEPRINT_14 > );
#endif
#ifdef BLUEPRINT_15
static_assert( check_shim_against_blueprint< SHIM_8, BLUEPRINT_15 > );
#endif
#ifdef BLUEPRINT_16
static_assert( check_shim_against_blueprint< SHIM_8, BLUEPRINT_16 > );
#endif
#endif

#ifdef SHIM_9
#include STRINGIFY( shims/SHIM_9/shim.h )
static_assert( check_shim< SHIM_9 > );
#ifdef BLUEPRINT_1
static_assert( check_shim_against_blueprint< SHIM_9, BLUEPRINT_1 > );
#endif
#ifdef BLUEPRINT_2
static_assert( check_shim_against_blueprint< SHIM_9, BLUEPRINT_2 > );
#endif
#ifdef BLUEPRINT_3
static_assert( check_shim_against_blueprint< SHIM_9, BLUEPRINT_3 > );
#endif
#ifdef BLUEPRINT_4
static_assert( check_shim_against_blueprint< SHIM_9, BLUEPRINT_4 > );
#endif
#ifdef BLUEPRINT_5
static_assert( check_shim_against_blueprint< SHIM_9, BLUEPRINT_5 > );
#endif
#ifdef BLUEPRINT_6
static_assert( check_shim_against_blueprint< SHIM_9, BLUEPRINT_6 > );
#endif
#ifdef BLUEPRINT_7
static_assert( check_shim_against_blueprint< SHIM_9, BLUEPRINT_7 > );
#endif
#ifdef BLUEPRINT_8
static_assert( check_shim_against_blueprint< SHIM_9, BLUEPRINT_8 > );
#endif
#ifdef BLUEPRINT_9
static_assert( check_shim_against_blueprint< SHIM_9, BLUEPRINT_9 > );
#endif
#ifdef BLUEPRINT_10
static_assert( check_shim_against_blueprint< SHIM_9, BLUEPRINT_10 > );
#endif
#ifdef BLUEPRINT_11
static_assert( check_shim_against_blueprint< SHIM_9, BLUEPRINT_11 > );
#endif
#ifdef BLUEPRINT_12
static_assert( check_shim_against_blueprint< SHIM_9, BLUEPRINT_12 > );
#endif
#ifdef BLUEPRINT_13
static_assert( check_shim_against_blueprint< SHIM_9, BLUEPRINT_13 > );
#endif
#ifdef BLUEPRINT_14
static_assert( check_shim_against_blueprint< SHIM_9, BLUEPRINT_14 > );
#endif
#ifdef BLUEPRINT_15
static_assert( check_shim_against_blueprint< SHIM_9, BLUEPRINT_15 > );
#endif
#ifdef BLUEPRINT_16
static_assert( check_shim_against_blueprint< SHIM_9, BLUEPRINT_16 > );
#endif
#endif

#ifdef SHIM_10
#include STRINGIFY( shims/SHIM_10/shim.h )
static_assert( check_shim< SHIM_10 > );
#ifdef BLUEPRINT_1
static_assert( check_shim_against_blueprint< SHIM_10, BLUEPRINT_1 > );
#endif
#ifdef BLUEPRINT_2
static_assert( check_shim_against_blueprint< SHIM_10, BLUEPRINT_2 > );
#endif
#ifdef BLUEPRINT_3
static_assert( check_shim_against_blueprint< SHIM_10, BLUEPRINT_3 > );
#endif
#ifdef BLUEPRINT_4
static_assert( check_shim_against_blueprint< SHIM_10, BLUEPRINT_4 > );
#endif
#ifdef BLUEPRINT_5
static_assert( check_shim_against_blueprint< SHIM_10, BLUEPRINT_5 > );
#endif
#ifdef BLUEPRINT_6
static_assert( check_shim_against_blueprint< SHIM_10, BLUEPRINT_6 > );
#endif
#ifdef BLUEPRINT_7
static_assert( check_shim_against_blueprint< SHIM_10, BLUEPRINT_7 > );
#endif
#ifdef BLUEPRINT_8
static_assert( check_shim_against_blueprint< SHIM_10, BLUEPRINT_8 > );
#endif
#ifdef BLUEPRINT_9
static_assert( check_shim_against_blueprint< SHIM_10, BLUEPRINT_9 > );
#endif
#ifdef BLUEPRINT_10
static_assert( check_shim_against_blueprint< SHIM_10, BLUEPRINT_10 > );
#endif
#ifdef BLUEPRINT_11
static_assert( check_shim_against_blueprint< SHIM_10, BLUEPRINT_11 > );
#endif
#ifdef BLUEPRINT_12
static_assert( check_shim_against_blueprint< SHIM_10, BLUEPRINT_12 > );
#endif
#ifdef BLUEPRINT_13
static_assert( check_shim_against_blueprint< SHIM_10, BLUEPRINT_13 > );
#endif
#ifdef BLUEPRINT_14
static_assert( check_shim_against_blueprint< SHIM_10, BLUEPRINT_14 > );
#endif
#ifdef BLUEPRINT_15
static_assert( check_shim_against_blueprint< SHIM_10, BLUEPRINT_15 > );
#endif
#ifdef BLUEPRINT_16
static_assert( check_shim_against_blueprint< SHIM_10, BLUEPRINT_16 > );
#endif
#endif

#ifdef SHIM_11
#include STRINGIFY( shims/SHIM_11/shim.h )
static_assert( check_shim< SHIM_11 > );
#ifdef BLUEPRINT_1
static_assert( check_shim_against_blueprint< SHIM_11, BLUEPRINT_1 > );
#endif
#ifdef BLUEPRINT_2
static_assert( check_shim_against_blueprint< SHIM_11, BLUEPRINT_2 > );
#endif
#ifdef BLUEPRINT_3
static_assert( check_shim_against_blueprint< SHIM_11, BLUEPRINT_3 > );
#endif
#ifdef BLUEPRINT_4
static_assert( check_shim_against_blueprint< SHIM_11, BLUEPRINT_4 > );
#endif
#ifdef BLUEPRINT_5
static_assert( check_shim_against_blueprint< SHIM_11, BLUEPRINT_5 > );
#endif
#ifdef BLUEPRINT_6
static_assert( check_shim_against_blueprint< SHIM_11, BLUEPRINT_6 > );
#endif
#ifdef BLUEPRINT_7
static_assert( check_shim_against_blueprint< SHIM_11, BLUEPRINT_7 > );
#endif
#ifdef BLUEPRINT_8
static_assert( check_shim_against_blueprint< SHIM_11, BLUEPRINT_8 > );
#endif
#ifdef BLUEPRINT_9
static_assert( check_shim_against_blueprint< SHIM_11, BLUEPRINT_9 > );
#endif
#ifdef BLUEPRINT_10
static_assert( check_shim_against_blueprint< SHIM_11, BLUEPRINT_10 > );
#endif
#ifdef BLUEPRINT_11
static_assert( check_shim_against_blueprint< SHIM_11, BLUEPRINT_11 > );
#endif
#ifdef BLUEPRINT_12
static_assert( check_shim_against_blueprint< SHIM_11, BLUEPRINT_12 > );
#endif
#ifdef BLUEPRINT_13
static_assert( check_shim_against_blueprint< SHIM_11, BLUEPRINT_13 > );
#endif
#ifdef BLUEPRINT_14
static_assert( check_shim_against_blueprint< SHIM_11, BLUEPRINT_14 > );
#endif
#ifdef BLUEPRINT_15
static_assert( check_shim_against_blueprint< SHIM_11, BLUEPRINT_15 > );
#endif
#ifdef BLUEPRINT_16
static_assert( check_shim_against_blueprint< SHIM_11, BLUEPRINT_16 > );
#endif
#endif

#ifdef SHIM_12
#include STRINGIFY( shims/SHIM_12/shim.h )
static_assert( check_shim< SHIM_12 > );
#ifdef BLUEPRINT_1
static_assert( check_shim_against_blueprint< SHIM_12, BLUEPRINT_1 > );
#endif
#ifdef BLUEPRINT_2
static_assert( check_shim_against_blueprint< SHIM_12, BLUEPRINT_2 > );
#endif
#ifdef BLUEPRINT_3
static_assert( check_shim_against_blueprint< SHIM_12, BLUEPRINT_3 > );
#endif
#ifdef BLUEPRINT_4
static_assert( check_shim_against_blueprint< SHIM_12, BLUEPRINT_4 > );
#endif
#ifdef BLUEPRINT_5
static_assert( check_shim_against_blueprint< SHIM_12, BLUEPRINT_5 > );
#endif
#ifdef BLUEPRINT_6
static_assert( check_shim_against_blueprint< SHIM_12, BLUEPRINT_6 > );
#endif
#ifdef BLUEPRINT_7
static_assert( check_shim_against_blueprint< SHIM_12, BLUEPRINT_7 > );
#endif
#ifdef BLUEPRINT_8
static_assert( check_shim_against_blueprint< SHIM_12, BLUEPRINT_8 > );
#endif
#ifdef BLUEPRINT_9
static_assert( check_shim_against_blueprint< SHIM_12, BLUEPRINT_9 > );
#endif
#ifdef BLUEPRINT_10
static_assert( check_shim_against_blueprint< SHIM_12, BLUEPRINT_10 > );
#endif
#ifdef BLUEPRINT_11
static_assert( check_shim_against_blueprint< SHIM_12, BLUEPRINT_11 > );
#endif
#ifdef BLUEPRINT_12
static_assert( check_shim_against_blueprint< SHIM_12, BLUEPRINT_12 > );
#endif
#ifdef BLUEPRINT_13
static_assert( check_shim_against_blueprint< SHIM_12, BLUEPRINT_13 > );
#endif
#ifdef BLUEPRINT_14
static_assert( check_shim_against_blueprint< SHIM_12, BLUEPRINT_14 > );
#endif
#ifdef BLUEPRINT_15
static_assert( check_shim_against_blueprint< SHIM_12, BLUEPRINT_15 > );
#endif
#ifdef BLUEPRINT_16
static_assert( check_shim_against_blueprint< SHIM_12, BLUEPRINT_16 > );
#endif
#endif

#ifdef SHIM_13
#include STRINGIFY( shims/SHIM_13/shim.h )
static_assert( check_shim< SHIM_13 > );
#ifdef BLUEPRINT_1
static_assert( check_shim_against_blueprint< SHIM_13, BLUEPRINT_1 > );
#endif
#ifdef BLUEPRINT_2
static_assert( check_shim_against_blueprint< SHIM_13, BLUEPRINT_2 > );
#endif
#ifdef BLUEPRINT_3
static_assert( check_shim_against_blueprint< SHIM_13, BLUEPRINT_3 > );
#endif
#ifdef BLUEPRINT_4
static_assert( check_shim_against_blueprint< SHIM_13, BLUEPRINT_4 > );
#endif
#ifdef BLUEPRINT_5
static_assert( check_shim_against_blueprint< SHIM_13, BLUEPRINT_5 > );
#endif
#ifdef BLUEPRINT_6
static_assert( check_shim_against_blueprint< SHIM_13, BLUEPRINT_6 > );
#endif
#ifdef BLUEPRINT_7
static_assert( check_shim_against_blueprint< SHIM_13, BLUEPRINT_7 > );
#endif
#ifdef BLUEPRINT_8
static_assert( check_shim_against_blueprint< SHIM_13, BLUEPRINT_8 > );
#endif
#ifdef BLUEPRINT_9
static_assert( check_shim_against_blueprint< SHIM_13, BLUEPRINT_9 > );
#endif
#ifdef BLUEPRINT_10
static_assert( check_shim_against_blueprint< SHIM_13, BLUEPRINT_10 > );
#endif
#ifdef BLUEPRINT_11
static_assert( check_shim_against_blueprint< SHIM_13, BLUEPRINT_11 > );
#endif
#ifdef BLUEPRINT_12
static_assert( check_shim_against_blueprint< SHIM_13, BLUEPRINT_12 > );
#endif
#ifdef BLUEPRINT_13
static_assert( check_shim_against_blueprint< SHIM_13, BLUEPRINT_13 > );
#endif
#ifdef BLUEPRINT_14
static_assert( check_shim_against_blueprint< SHIM_13, BLUEPRINT_14 > );
#endif
#ifdef BLUEPRINT_15
static_assert( check_shim_against_blueprint< SHIM_13, BLUEPRINT_15 > );
#endif
#ifdef BLUEPRINT_16
static_assert( check_shim_against_blueprint< SHIM_13, BLUEPRINT_16 > );
#endif
#endif

#ifdef SHIM_14
#include STRINGIFY( shims/SHIM_14/shim.h )
static_assert( check_shim< SHIM_14 > );
#ifdef BLUEPRINT_1
static_assert( check_shim_against_blueprint< SHIM_14, BLUEPRINT_1 > );
#endif
#ifdef BLUEPRINT_2
static_assert( check_shim_against_blueprint< SHIM_14, BLUEPRINT_2 > );
#endif
#ifdef BLUEPRINT_3
static_assert( check_shim_against_blueprint< SHIM_14, BLUEPRINT_3 > );
#endif
#ifdef BLUEPRINT_4
static_assert( check_shim_against_blueprint< SHIM_14, BLUEPRINT_4 > );
#endif
#ifdef BLUEPRINT_5
static_assert( check_shim_against_blueprint< SHIM_14, BLUEPRINT_5 > );
#endif
#ifdef BLUEPRINT_6
static_assert( check_shim_against_blueprint< SHIM_14, BLUEPRINT_6 > );
#endif
#ifdef BLUEPRINT_7
static_assert( check_shim_against_blueprint< SHIM_14, BLUEPRINT_7 > );
#endif
#ifdef BLUEPRINT_8
static_assert( check_shim_against_blueprint< SHIM_14, BLUEPRINT_8 > );
#endif
#ifdef BLUEPRINT_9
static_assert( check_shim_against_blueprint< SHIM_14, BLUEPRINT_9 > );
#endif
#ifdef BLUEPRINT_10
static_assert( check_shim_against_blueprint< SHIM_14, BLUEPRINT_10 > );
#endif
#ifdef BLUEPRINT_11
static_assert( check_shim_against_blueprint< SHIM_14, BLUEPRINT_11 > );
#endif
#ifdef BLUEPRINT_12
static_assert( check_shim_against_blueprint< SHIM_14, BLUEPRINT_12 > );
#endif
#ifdef BLUEPRINT_13
static_assert( check_shim_against_blueprint< SHIM_14, BLUEPRINT_13 > );
#endif
#ifdef BLUEPRINT_14
static_assert( check_shim_against_blueprint< SHIM_14, BLUEPRINT_14 > );
#endif
#ifdef BLUEPRINT_15
static_assert( check_shim_against_blueprint< SHIM_14, BLUEPRINT_15 > );
#endif
#ifdef BLUEPRINT_16
static_assert( check_shim_against_blueprint< SHIM_14, BLUEPRINT_16 > );
#endif
#endif

#ifdef SHIM_15
#include STRINGIFY( shims/SHIM_15/shim.h )
static_assert( check_shim< SHIM_15 > );
#ifdef BLUEPRINT_1
static_assert( check_shim_against_blueprint< SHIM_15, BLUEPRINT_1 > );
#endif
#ifdef BLUEPRINT_2
static_assert( check_shim_against_blueprint< SHIM_15, BLUEPRINT_2 > );
#endif
#ifdef BLUEPRINT_3
static_assert( check_shim_against_blueprint< SHIM_15, BLUEPRINT_3 > );
#endif
#ifdef BLUEPRINT_4
static_assert( check_shim_against_blueprint< SHIM_15, BLUEPRINT_4 > );
#endif
#ifdef BLUEPRINT_5
static_assert( check_shim_against_blueprint< SHIM_15, BLUEPRINT_5 > );
#endif
#ifdef BLUEPRINT_6
static_assert( check_shim_against_blueprint< SHIM_15, BLUEPRINT_6 > );
#endif
#ifdef BLUEPRINT_7
static_assert( check_shim_against_blueprint< SHIM_15, BLUEPRINT_7 > );
#endif
#ifdef BLUEPRINT_8
static_assert( check_shim_against_blueprint< SHIM_15, BLUEPRINT_8 > );
#endif
#ifdef BLUEPRINT_9
static_assert( check_shim_against_blueprint< SHIM_15, BLUEPRINT_9 > );
#endif
#ifdef BLUEPRINT_10
static_assert( check_shim_against_blueprint< SHIM_15, BLUEPRINT_10 > );
#endif
#ifdef BLUEPRINT_11
static_assert( check_shim_against_blueprint< SHIM_15, BLUEPRINT_11 > );
#endif
#ifdef BLUEPRINT_12
static_assert( check_shim_against_blueprint< SHIM_15, BLUEPRINT_12 > );
#endif
#ifdef BLUEPRINT_13
static_assert( check_shim_against_blueprint< SHIM_15, BLUEPRINT_13 > );
#endif
#ifdef BLUEPRINT_14
static_assert( check_shim_against_blueprint< SHIM_15, BLUEPRINT_14 > );
#endif
#ifdef BLUEPRINT_15
static_assert( check_shim_against_blueprint< SHIM_15, BLUEPRINT_15 > );
#endif
#ifdef BLUEPRINT_16
static_assert( check_shim_against_blueprint< SHIM_15, BLUEPRINT_16 > );
#endif
#endif

#ifdef SHIM_16
#include STRINGIFY( shims/SHIM_16/shim.h )
static_assert( check_shim< SHIM_16 > );
#ifdef BLUEPRINT_1
static_assert( check_shim_against_blueprint< SHIM_16, BLUEPRINT_1 > );
#endif
#ifdef BLUEPRINT_2
static_assert( check_shim_against_blueprint< SHIM_16, BLUEPRINT_2 > );
#endif
#ifdef BLUEPRINT_3
static_assert( check_shim_against_blueprint< SHIM_16, BLUEPRINT_3 > );
#endif
#ifdef BLUEPRINT_4
static_assert( check_shim_against_blueprint< SHIM_16, BLUEPRINT_4 > );
#endif
#ifdef BLUEPRINT_5
static_assert( check_shim_against_blueprint< SHIM_16, BLUEPRINT_5 > );
#endif
#ifdef BLUEPRINT_6
static_assert( check_shim_against_blueprint< SHIM_16, BLUEPRINT_6 > );
#endif
#ifdef BLUEPRINT_7
static_assert( check_shim_against_blueprint< SHIM_16, BLUEPRINT_7 > );
#endif
#ifdef BLUEPRINT_8
static_assert( check_shim_against_blueprint< SHIM_16, BLUEPRINT_8 > );
#endif
#ifdef BLUEPRINT_9
static_assert( check_shim_against_blueprint< SHIM_16, BLUEPRINT_9 > );
#endif
#ifdef BLUEPRINT_10
static_assert( check_shim_against_blueprint< SHIM_16, BLUEPRINT_10 > );
#endif
#ifdef BLUEPRINT_11
static_assert( check_shim_against_blueprint< SHIM_16, BLUEPRINT_11 > );
#endif
#ifdef BLUEPRINT_12
static_assert( check_shim_against_blueprint< SHIM_16, BLUEPRINT_12 > );
#endif
#ifdef BLUEPRINT_13
static_assert( check_shim_against_blueprint< SHIM_16, BLUEPRINT_13 > );
#endif
#ifdef BLUEPRINT_14
static_assert( check_shim_against_blueprint< SHIM_16, BLUEPRINT_14 > );
#endif
#ifdef BLUEPRINT_15
static_assert( check_shim_against_blueprint< SHIM_16, BLUEPRINT_15 > );
#endif
#ifdef BLUEPRINT_16
static_assert( check_shim_against_blueprint< SHIM_16, BLUEPRINT_16 > );
#endif
#endif

// Random number generator.
std::default_random_engine random_number_generator( std::chrono::steady_clock::now().time_since_epoch().count() );

// Function for providing unique keys for a given blueprint in random order.
// Besides the KEY_COUNT keys to be inserted, it also provides an extra KEY_COUNT / KEY_COUNT_MEASUREMENT_INTERVAL *
// 1000 keys for testing failed look-ups.
template< typename blueprint > const typename blueprint::key_type &shuffled_unique_key( size_t index )
{
  static auto keys = []()
  {
    std::vector<typename blueprint::key_type> keys( KEY_COUNT + KEY_COUNT / KEY_COUNT_MEASUREMENT_INTERVAL * 1000 );
    blueprint::fill_unique_keys( keys );
    std::shuffle( keys.begin(), keys.end(), random_number_generator );
    return keys;
  }();

  return keys[ index ];
}

// Function for recording and accessing a single result, i.e. one measurement for a particular table, blueprint, and
// benchmark during a particular run.
template< template< typename > typename shim, typename blueprint, benchmark_ids benchmark_id >
uint64_t &results( size_t run_index, size_t result_index )
{
  static auto results = std::vector< uint64_t >( RUN_COUNT * ( KEY_COUNT / KEY_COUNT_MEASUREMENT_INTERVAL ) );
  return results[ run_index * ( KEY_COUNT / KEY_COUNT_MEASUREMENT_INTERVAL ) + result_index ];
}

// Function that attempts to reset the cache to the same state before each table is benchmarked.
// The strategy is to iterate over an array that is at least as large as the L1, L2, and L3 caches combined.
void flush_cache()
{
  static auto buffer = std::vector< uint64_t >( APPROXIMATE_CACHE_SIZE / sizeof( uint64_t ) + 1 );

  for( auto itr = buffer.begin(); itr != buffer.end(); ++itr )
    do_not_optimize += *itr;
}

// Actual benchmarking function.
template< template< typename > typename shim, typename blueprint >void benchmark( unsigned int run )
{
  std::cout << shim<void>::label << ": " << blueprint::label << "\n";

  // Ugly workaround to ensure that the static unique keys and results arrays inside their respective template functions
  // are initialized outside of the timed code below.
  // This is necessary because C++ has no built-in mechanism to specify that local static variables should be fully
  // initialized at program start-up.
  do_not_optimize += *(unsigned char *)&shuffled_unique_key< blueprint >( 0 );
  do_not_optimize += *(unsigned char *)&results< shim, blueprint, insert_nonexisting >( 0, 0 );
  do_not_optimize += *(unsigned char *)&results< shim, blueprint, reinsert_after_erasure >( 0, 0 );
  do_not_optimize += *(unsigned char *)&results< shim, blueprint, erase_existing >( 0, 0 );
  do_not_optimize += *(unsigned char *)&results< shim, blueprint, insert_existing >( 0, 0 );
  do_not_optimize += *(unsigned char *)&results< shim, blueprint, erase_nonexisting >( 0, 0 );
  do_not_optimize += *(unsigned char *)&results< shim, blueprint, get_existing >( 0, 0 );
  do_not_optimize += *(unsigned char *)&results< shim, blueprint, get_nonexisting >( 0, 0 );
  do_not_optimize += *(unsigned char *)&results< shim, blueprint, iteration >( 0, 0 );

  // Reset the cache state.
  // This ensures that each table starts each benchmarking run on equal footing, but it does not prevent the cache
  // effects of one benchmark from potentially influencing latter benchmarks.
  flush_cache();

  #ifdef BENCHMARK_INSERT_NONEXISTING
  {
    auto table = shim< blueprint >::create_table();

    std::this_thread::sleep_for( std::chrono::milliseconds( MILLISECOND_COOLDOWN_BETWEEN_BENCHMARKS ) );

    size_t i = 0;
    size_t j = 0;
    auto start = std::chrono::high_resolution_clock::now();
    while( i < KEY_COUNT )
    {
      shim< blueprint >::insert( table, shuffled_unique_key< blueprint >( i ) );

      ++i;
      if( ++j == KEY_COUNT_MEASUREMENT_INTERVAL )
      {
        results< shim, blueprint, insert_nonexisting >( run, i / KEY_COUNT_MEASUREMENT_INTERVAL - 1 ) =
          std::chrono::duration_cast< std::chrono::nanoseconds >(
            std::chrono::high_resolution_clock::now() - start
          ).count();

        j = 0;
      }
    }

    shim< blueprint >::destroy_table( table );
  }
  #endif

  #ifdef BENCHMARK_REINSERT_AFTER_ERASURE
  {
      auto table = shim<blueprint>::create_table();

      std::this_thread::sleep_for(std::chrono::milliseconds(MILLISECOND_COOLDOWN_BETWEEN_BENCHMARKS));

      for (size_t k = 0; k < KEY_COUNT; ++k) {
          shim<blueprint>::insert(table, shuffled_unique_key<blueprint>(k));
      }

      // Erasure Phase (not timed for this benchmark's primary result)
      // Erase the first KEY_COUNT - 1 keys that were inserted.
      // This leaves the last key (shuffled_unique_key<blueprint>(KEY_COUNT - 1)) in the table.
      for (size_t k = 0; k < KEY_COUNT - 1; ++k) {
          shim<blueprint>::erase(table, shuffled_unique_key<blueprint>(k));
      }
      // At this point, the table should effectively contain 1 element.

      // Re-insertion Phase (Measured)
      size_t i = 0; // Key counter for re-insertion
      size_t j = 0; // Counter for measurement interval
      const auto start = std::chrono::high_resolution_clock::now();
      while (i < KEY_COUNT) {
          // Re-insert all original KEY_COUNT keys using the same sequence.
          // This will re-insert the KEY_COUNT-1 erased keys,
          // and attempt to re-insert the 1 key that remained (testing update/replace behavior).
          shim<blueprint>::insert(table, shuffled_unique_key<blueprint>(i));

          ++i;
          if (++j == KEY_COUNT_MEASUREMENT_INTERVAL) {
              results<shim, blueprint, reinsert_after_erasure>(run, (i / KEY_COUNT_MEASUREMENT_INTERVAL) - 1) =
                  std::chrono::duration_cast<std::chrono::nanoseconds>(
                      std::chrono::high_resolution_clock::now() - start).count();
              j = 0;
          }
      }
      shim<blueprint>::destroy_table(table);
  }
  #endif // BENCHMARK_REINSERT_AFTER_ERASURE

  #ifdef BENCHMARK_ERASE_EXISTING
  {
    auto table = shim< blueprint >::create_table();

    std::this_thread::sleep_for( std::chrono::milliseconds( MILLISECOND_COOLDOWN_BETWEEN_BENCHMARKS ) );

    size_t i = 0;
    size_t j = 0;
    while( i < KEY_COUNT )
    {
      shim< blueprint >::insert( table, shuffled_unique_key< blueprint >( i ) );

      ++i;
      if( ++j == KEY_COUNT_MEASUREMENT_INTERVAL )
      {
        size_t result_index = i / KEY_COUNT_MEASUREMENT_INTERVAL - 1;

        // To determine which keys to erase, we randomly chose a position in the sequence of keys already inserted and
        // then erase the subsequent 1000 keys, wrapping around to the start of the sequence if necessary.
        // This strategy has the potential drawback that keys are erased in the same order in which they were inserted.
        size_t erase_keys_begin = std::uniform_int_distribution<size_t>( 0, i - 1 )( random_number_generator );

        size_t k = 0;
        size_t l = erase_keys_begin;

        auto start = std::chrono::high_resolution_clock::now();
        while( true )
        {
          shim< blueprint >::erase( table, shuffled_unique_key< blueprint >( l ) );

          if( ++k == 1000 )
            break;
          if( ++l == i )
            l = 0;
        }

        results< shim, blueprint, erase_existing >( run, result_index ) =
          std::chrono::duration_cast< std::chrono::nanoseconds >(
            std::chrono::high_resolution_clock::now() - start
          ).count();

        // Re-insert the erased keys.
        // This has the drawback that if tombstones are being used, those tombstones created by the above erasures will
        // all be replaced by the re-inserted keys.
        // Hence, this benchmark cannot show the lingering effect of tombstones.
        k = 0;
        l = erase_keys_begin;
        while( true )
        {
          shim< blueprint >::insert( table, shuffled_unique_key< blueprint >( l ) );

          if( ++k == 1000 )
            break;
          if( ++l == i )
            l = 0;
        }

        j = 0;
      }
    }

    shim< blueprint >::destroy_table( table );
  }
  #endif

  #if                                         \
    defined( BENCHMARK_INSERT_EXISTING )   || \
    defined( BENCHMARK_ERASE_NONEXISTING ) || \
    defined( BENCHMARK_GET_EXISTING )      || \
    defined( BENCHMARK_GET_NONEXISTING )   || \
    defined( BENCHMARK_ITERATION )
  {
    auto table = shim< blueprint >::create_table();

    std::this_thread::sleep_for( std::chrono::milliseconds( MILLISECOND_COOLDOWN_BETWEEN_BENCHMARKS ) );

    size_t i = 0;
    size_t j = 0;
    while( i < KEY_COUNT )
    {
      shim< blueprint >::insert( table, shuffled_unique_key< blueprint >( i ) );

      ++i;
      if( ++j == KEY_COUNT_MEASUREMENT_INTERVAL )
      {
        size_t result_index = i / KEY_COUNT_MEASUREMENT_INTERVAL - 1;

        #ifdef BENCHMARK_INSERT_EXISTING
        {
          size_t k = 0;

          // To determine which existing keys to replace, we apply the same strategy that we applied when erasing
          // existing keys.
          size_t l = std::uniform_int_distribution<size_t>( 0, i - 1 )( random_number_generator );

          auto start = std::chrono::high_resolution_clock::now();
          while( true )
          {
            shim< blueprint >::insert( table, shuffled_unique_key< blueprint >( l ) );

            if( ++k == 1000 )
              break;
            if( ++l == i )
              l = 0;
          }

          results< shim, blueprint, insert_existing >( run, result_index ) =
            std::chrono::duration_cast< std::chrono::nanoseconds >(
              std::chrono::high_resolution_clock::now() - start
            ).count();
        }
        #endif

        #ifdef BENCHMARK_ERASE_NONEXISTING
        {
          size_t k = 0;

          // To determine which nonexisting keys to attempt to erase, we randomly chose a position in the sequence of
          // nonexisting keys (which starts at KEY_COUNT) and then call erase for the subsequent 1000 keys, wrapping
          // around to the start of the sequence if necessary.
          size_t l = std::uniform_int_distribution<size_t>(
            KEY_COUNT,
            KEY_COUNT + KEY_COUNT / KEY_COUNT_MEASUREMENT_INTERVAL * 1000 - 1
          )( random_number_generator );

          auto start = std::chrono::high_resolution_clock::now();
          while( true )
          {
            shim< blueprint >::erase( table, shuffled_unique_key< blueprint >( l ) );

            if( ++k == 1000 )
              break;
            if( ++l == KEY_COUNT + KEY_COUNT / KEY_COUNT_MEASUREMENT_INTERVAL * 1000 )
              l = KEY_COUNT;
          }

          results< shim, blueprint, erase_nonexisting >( run, result_index ) =
            std::chrono::duration_cast< std::chrono::nanoseconds >(
              std::chrono::high_resolution_clock::now() - start
            ).count();
        }
        #endif

        #ifdef BENCHMARK_GET_EXISTING
        {
          size_t k = 0;

          // To determine which existing keys to look-up, we apply the same strategy that we applied when erasing
          // and inserting existing keys.
          size_t l = std::uniform_int_distribution<size_t>( 0, i - 1 )( random_number_generator );

          auto start = std::chrono::high_resolution_clock::now();
          while( true )
          {
            auto itr = shim< blueprint >::find( table, shuffled_unique_key< blueprint >( l ) );

            // Accessing the first byte of the value prevents the above call from being optimized out and ensures that
            // tables that can preform look-ups without accessing the values (because the keys are stored separately)
            // actually incur the additional cache miss that they would incur during normal use.
            do_not_optimize += *(unsigned char *)&shim< blueprint >::get_value_from_itr( table, itr );

            if( shim< blueprint >::get_key_from_itr( table, itr ) != shuffled_unique_key< blueprint >( l ) )
              exit( 0 );

            if( ++k == 1000 )
              break;
            if( ++l == i )
              l = 0;
          }

          results< shim, blueprint, get_existing >( run, result_index ) =
            std::chrono::duration_cast< std::chrono::nanoseconds >(
              std::chrono::high_resolution_clock::now() - start
            ).count();
        }
        #endif

        #ifdef BENCHMARK_GET_NONEXISTING
        {
          size_t k = 0;

          // To determine which nonexisting keys to attempt to look-up, we apply the same strategy that we applied when
          // erasing nonexisting keys.
          size_t l = std::uniform_int_distribution<size_t>(
            KEY_COUNT,
            KEY_COUNT + KEY_COUNT / KEY_COUNT_MEASUREMENT_INTERVAL * 1000 - 1
          )( random_number_generator );

          auto start = std::chrono::high_resolution_clock::now();
          while( true )
          {
            auto itr = shim< blueprint >::find( table, shuffled_unique_key< blueprint >( l ) );
            do_not_optimize += shim< blueprint >::is_itr_valid( table, itr ); // Should always be false.

            if( ++k == 1000 )
              break;
            if( ++l == KEY_COUNT + KEY_COUNT / KEY_COUNT_MEASUREMENT_INTERVAL * 1000 )
              l = KEY_COUNT;
          }

          results< shim, blueprint, get_nonexisting >( run, result_index ) =
            std::chrono::duration_cast< std::chrono::nanoseconds >(
              std::chrono::high_resolution_clock::now() - start
            ).count();
        }
        #endif

        #ifdef BENCHMARK_ITERATION
        {
          // To determine where inside the table to begin iteration, we randomly choose an existing key.
          // This ensures that we are not just hitting the same, cached memory every time we measure.
          auto itr = shim< blueprint >::find(
            table,
            shuffled_unique_key< blueprint >(
              std::uniform_int_distribution<size_t>( 0, i - 1 )( random_number_generator )
            )
          );

          size_t k = 0;
          auto start = std::chrono::high_resolution_clock::now();
          while( true )
          {
            // Accessing the first bytes of the key and value ensures that tables that iterate without directly accessing
            // the keys and/or values actually incur the additional cache misses that they would incur during normal
            // use.
            do_not_optimize += *(unsigned char *)&shim< blueprint >::get_key_from_itr( table, itr );
            do_not_optimize += *(unsigned char *)&shim< blueprint >::get_value_from_itr( table, itr );

            if( ++k == 1000 )
              break;

            shim< blueprint >::increment_itr( table, itr );
            if( !shim< blueprint >::is_itr_valid( table, itr ) )
              itr = shim< blueprint >::begin_itr( table );
          }

          results< shim, blueprint, iteration >( run, result_index ) =
            std::chrono::duration_cast< std::chrono::nanoseconds >(
              std::chrono::high_resolution_clock::now() - start
          ).count();
        }
        #endif

        j = 0;
      }
    }

    shim< blueprint >::destroy_table( table );
  }
  #endif
}

// Function for benchmarking a shim against all blueprints.
template< template< typename > typename shim >
void benchmarks( unsigned int run )
{
  #ifdef BLUEPRINT_1
  benchmark< shim, BLUEPRINT_1 >( run );
  #endif
  #ifdef BLUEPRINT_2
  benchmark< shim, BLUEPRINT_2 >( run );
  #endif
  #ifdef BLUEPRINT_3
  benchmark< shim, BLUEPRINT_3 >( run );
  #endif
  #ifdef BLUEPRINT_4
  benchmark< shim, BLUEPRINT_4 >( run );
  #endif
  #ifdef BLUEPRINT_5
  benchmark< shim, BLUEPRINT_5 >( run );
  #endif
  #ifdef BLUEPRINT_6
  benchmark< shim, BLUEPRINT_6 >( run );
  #endif
  #ifdef BLUEPRINT_7
  benchmark< shim, BLUEPRINT_7 >( run );
  #endif
  #ifdef BLUEPRINT_8
  benchmark< shim, BLUEPRINT_8 >( run );
  #endif
  #ifdef BLUEPRINT_9
  benchmark< shim, BLUEPRINT_9 >( run );
  #endif
  #ifdef BLUEPRINT_10
  benchmark< shim, BLUEPRINT_10 >( run );
  #endif
  #ifdef BLUEPRINT_11
  benchmark< shim, BLUEPRINT_11 >( run );
  #endif
  #ifdef BLUEPRINT_12
  benchmark< shim, BLUEPRINT_12 >( run );
  #endif
  #ifdef BLUEPRINT_13
  benchmark< shim, BLUEPRINT_13 >( run );
  #endif
  #ifdef BLUEPRINT_14
  benchmark< shim, BLUEPRINT_14 >( run );
  #endif
  #ifdef BLUEPRINT_15
  benchmark< shim, BLUEPRINT_15 >( run );
  #endif
  #ifdef BLUEPRINT_16
  benchmark< shim, BLUEPRINT_16 >( run );
  #endif
}

// --- NEW: Data Processing and Population Logic ---

// Processes the raw data from the static `results` arrays and populates the global data structures.
void populate_and_process_results()
{
    // Lambda to process a single benchmark configuration
    auto process_benchmark =
        []<template<typename> typename shim, typename blueprint, benchmark_ids bmid>()
    {
        // Populate info about the shim and blueprint if not already done
        if (std::find_if(g_registered_shims_info.begin(), g_registered_shims_info.end(),
            [](const auto& si){ return si.label == shim<void>::label; }) == g_registered_shims_info.end()) {
            g_registered_shims_info.push_back({shim<void>::label, shim<void>::color, shim<void>::tombstone_like_mechanism});
            g_all_shim_labels.push_back(shim<void>::label);
            g_shim_is_tombstone[shim<void>::label] = shim<void>::tombstone_like_mechanism;
        }
        if (std::find_if(g_registered_blueprints_info.begin(), g_registered_blueprints_info.end(),
            [](const auto& bp){ return bp.label == blueprint::label; }) == g_registered_blueprints_info.end()) {
            g_registered_blueprints_info.push_back({blueprint::label});
        }
        if (std::find(g_enabled_benchmark_ids_list.begin(), g_enabled_benchmark_ids_list.end(), bmid) == g_enabled_benchmark_ids_list.end()){
             g_enabled_benchmark_ids_list.push_back(bmid);
        }

        const size_t num_points = KEY_COUNT / KEY_COUNT_MEASUREMENT_INTERVAL;
        ArenaKey key = {blueprint::label, shim<void>::label, bmid};

        // 1. Populate raw data for CSV
        auto& raw_data_vec = g_raw_micro_data[key];
        raw_data_vec.resize(RUN_COUNT);
        for (size_t run = 0; run < RUN_COUNT; ++run) {
            raw_data_vec[run].resize(num_points);
            for (size_t i = 0; i < num_points; ++i) {
                raw_data_vec[run][i] = results<shim, blueprint, bmid>(run, i);
            }
        }

        // 2. Calculate adjusted averages for HTML graphs
        std::vector<double> adjusted_points(num_points);
        double max_val = 0.0;
        std::array<uint64_t, RUN_COUNT> results_temp;

        for (size_t i = 0; i < num_points; ++i) {
            for (size_t run = 0; run < RUN_COUNT; ++run) {
                results_temp[run] = raw_data_vec[run][i];
            }
            std::sort(results_temp.begin(), results_temp.end());
            adjusted_points[i] = (double)std::accumulate(
                results_temp.begin() + DISCARDED_RUNS_COUNT / 2,
                results_temp.end() - DISCARDED_RUNS_COUNT / 2,
                (uint64_t)0
            ) / (RUN_COUNT - DISCARDED_RUNS_COUNT);

            if (adjusted_points[i] > max_val) {
                max_val = adjusted_points[i];
            }
        }

        // 3. Calculate total time for heatmap
        double total_time = (bmid == insert_nonexisting || bmid == reinsert_after_erasure)
            ? (adjusted_points.empty() ? 0.0 : adjusted_points.back())
            : std::accumulate(adjusted_points.begin(), adjusted_points.end(), 0.0);

        // 4. Store final processed data
        g_final_median_point_averages[key] = adjusted_points;
        g_final_max_point_values[key] = max_val;
        g_final_median_performance_times[key] = total_time;
    };

    // Lambda to iterate over all blueprints for a given shim and benchmark
    auto process_shim_benchmarks =
        [process_benchmark]<template<typename> typename shim, benchmark_ids bmid>() {
        #ifdef BLUEPRINT_1
        process_benchmark.operator()<shim, BLUEPRINT_1, bmid>();
        #endif
        #ifdef BLUEPRINT_2
        process_benchmark.operator()<shim, BLUEPRINT_2, bmid>();
        #endif
        #ifdef BLUEPRINT_3
        process_benchmark.operator()<shim, BLUEPRINT_3, bmid>();
        #endif
        #ifdef BLUEPRINT_4
        process_benchmark.operator()<shim, BLUEPRINT_4, bmid>();
        #endif
        #ifdef BLUEPRINT_5
        process_benchmark.operator()<shim, BLUEPRINT_5, bmid>();
        #endif
        #ifdef BLUEPRINT_6
        process_benchmark.operator()<shim, BLUEPRINT_6, bmid>();
        #endif
        #ifdef BLUEPRINT_7
        process_benchmark.operator()<shim, BLUEPRINT_7, bmid>();
        #endif
        #ifdef BLUEPRINT_8
        process_benchmark.operator()<shim, BLUEPRINT_8, bmid>();
        #endif
        #ifdef BLUEPRINT_9
        process_benchmark.operator()<shim, BLUEPRINT_9, bmid>();
        #endif
        #ifdef BLUEPRINT_10
        process_benchmark.operator()<shim, BLUEPRINT_10, bmid>();
        #endif
        #ifdef BLUEPRINT_11
        process_benchmark.operator()<shim, BLUEPRINT_11, bmid>();
        #endif
        #ifdef BLUEPRINT_12
        process_benchmark.operator()<shim, BLUEPRINT_12, bmid>();
        #endif
        #ifdef BLUEPRINT_13
        process_benchmark.operator()<shim, BLUEPRINT_13, bmid>();
        #endif
        #ifdef BLUEPRINT_14
        process_benchmark.operator()<shim, BLUEPRINT_14, bmid>();
        #endif
        #ifdef BLUEPRINT_15
        process_benchmark.operator()<shim, BLUEPRINT_15, bmid>();
        #endif
        #ifdef BLUEPRINT_16
        process_benchmark.operator()<shim, BLUEPRINT_16, bmid>();
        #endif
    };

    // Lambda to iterate over all benchmarks for a given shim
    auto process_all_for_shim =
        [process_shim_benchmarks]<template<typename> typename shim>() {
        #ifdef BENCHMARK_INSERT_NONEXISTING
        process_shim_benchmarks.operator()<shim, insert_nonexisting>();
        #endif
        #ifdef BENCHMARK_REINSERT_AFTER_ERASURE
        process_shim_benchmarks.operator()<shim, reinsert_after_erasure>();
        #endif
        #ifdef BENCHMARK_ERASE_EXISTING
        process_shim_benchmarks.operator()<shim, erase_existing>();
        #endif
        #ifdef BENCHMARK_INSERT_EXISTING
        process_shim_benchmarks.operator()<shim, insert_existing>();
        #endif
        #ifdef BENCHMARK_ERASE_NONEXISTING
        process_shim_benchmarks.operator()<shim, erase_nonexisting>();
        #endif
        #ifdef BENCHMARK_GET_EXISTING
        process_shim_benchmarks.operator()<shim, get_existing>();
        #endif
        #ifdef BENCHMARK_GET_NONEXISTING
        process_shim_benchmarks.operator()<shim, get_nonexisting>();
        #endif
        #ifdef BENCHMARK_ITERATION
        process_shim_benchmarks.operator()<shim, iteration>();
        #endif
    };

    // --- Main processing loop ---
    #ifdef SHIM_1
    process_all_for_shim.operator()<SHIM_1>();
    #endif
    #ifdef SHIM_2
    process_all_for_shim.operator()<SHIM_2>();
    #endif
    #ifdef SHIM_3
    process_all_for_shim.operator()<SHIM_3>();
    #endif
    #ifdef SHIM_4
    process_all_for_shim.operator()<SHIM_4>();
    #endif
    #ifdef SHIM_5
    process_all_for_shim.operator()<SHIM_5>();
    #endif
    #ifdef SHIM_6
    process_all_for_shim.operator()<SHIM_6>();
    #endif
    #ifdef SHIM_7
    process_all_for_shim.operator()<SHIM_7>();
    #endif
    #ifdef SHIM_8
    process_all_for_shim.operator()<SHIM_8>();
    #endif
    #ifdef SHIM_9
    process_all_for_shim.operator()<SHIM_9>();
    #endif
    #ifdef SHIM_10
    process_all_for_shim.operator()<SHIM_10>();
    #endif
    #ifdef SHIM_11
    process_all_for_shim.operator()<SHIM_11>();
    #endif
    #ifdef SHIM_12
    process_all_for_shim.operator()<SHIM_12>();
    #endif
    #ifdef SHIM_13
    process_all_for_shim.operator()<SHIM_13>();
    #endif
    #ifdef SHIM_14
    process_all_for_shim.operator()<SHIM_14>();
    #endif
    #ifdef SHIM_15
    process_all_for_shim.operator()<SHIM_15>();
    #endif
    #ifdef SHIM_16
    process_all_for_shim.operator()<SHIM_16>();
    #endif
}


// Program entry.
int main()
{
  for( unsigned int run = 0; run < RUN_COUNT; ++run )
  {
    std::cout << "Run " << run << '\n';

    #ifdef SHIM_1
    benchmarks< SHIM_1 >( run );
    #endif
    #ifdef SHIM_2
    benchmarks< SHIM_2 >( run );
    #endif
    #ifdef SHIM_3
    benchmarks< SHIM_3 >( run );
    #endif
    #ifdef SHIM_4
    benchmarks< SHIM_4 >( run );
    #endif
    #ifdef SHIM_5
    benchmarks< SHIM_5 >( run );
    #endif
    #ifdef SHIM_6
    benchmarks< SHIM_6 >( run );
    #endif
    #ifdef SHIM_7
    benchmarks< SHIM_7 >( run );
    #endif
    #ifdef SHIM_8
    benchmarks< SHIM_8 >( run );
    #endif
    #ifdef SHIM_9
    benchmarks< SHIM_9 >( run );
    #endif
    #ifdef SHIM_10
    benchmarks< SHIM_10 >( run );
    #endif
    #ifdef SHIM_11
    benchmarks< SHIM_11 >( run );
    #endif
    #ifdef SHIM_12
    benchmarks< SHIM_12 >( run );
    #endif
    #ifdef SHIM_13
    benchmarks< SHIM_13 >( run );
    #endif
    #ifdef SHIM_14
    benchmarks< SHIM_14 >( run );
    #endif
    #ifdef SHIM_15
    benchmarks< SHIM_15 >( run );
    #endif
    #ifdef SHIM_16
    benchmarks< SHIM_16 >( run );
    #endif
  }

  std::cout << "Processing and outputting results...\n";

  populate_and_process_results();

  // Get UTC time string with colons replaced by underscores.
  auto itt = std::chrono::system_clock::to_time_t( std::chrono::system_clock::now() );
  std::ostringstream ss;
  ss << std::put_time( gmtime( &itt), "%Y-%m-%dT%H_%M_%S" );
  auto time_str = ss.str();

  // Call the output generators
  BenchmarkOutput::benchmark_html_out(time_str);
  BenchmarkOutput::csv_out_dispatch(time_str, g_raw_micro_data, g_final_median_point_averages);

  std::cout << "Optimization preventer: " << do_not_optimize << "\n";
  std::cout << "Done\n";
}