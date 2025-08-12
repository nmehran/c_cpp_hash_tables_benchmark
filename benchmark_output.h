/**
* @file benchmark_output.h
 *
 * Copyright (c) 2025-Present Gradient Dynamics LLC
 * Copyright (c) 2025-Present Nima Mehrani
 * Distributed under the MIT License (see the accompanying LICENSE file).
*/

#ifndef BENCHMARK_OUTPUT_H
#define BENCHMARK_OUTPUT_H
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <iomanip>
#include <ios>
#include <iosfwd>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>
#include <bits/ostream.tcc>

namespace BenchmarkBMIDDispatch {
    /**
     * @brief Dispatches a generic lambda to the correct benchmark implementation based on its ID.
     *
     * This function uses template recursion to iterate through `benchmark_ids` at compile-time.
     * When `runtime_bmid_to_run` matches `CompileTimeBMID`, the provided generic lambda's
     * operator() is invoked with `CompileTimeBMID` as its template argument, allowing
     * compile-time specialization of the lambda's behavior for that specific benchmark.
     *
     * @tparam CompileTimeBMID The current `benchmark_ids` enum value being checked (compile-time).
     * @tparam GenericLambda A generic lambda type (e.g., `auto&&` lambda) that expects
     * a `benchmark_ids` template parameter.
     * @tparam Args Variadic template arguments to forward to the lambda's invocation.
     * @param runtime_bmid_to_run The `benchmark_ids` enum value to match at runtime.
     * @param lambda_target The generic lambda instance to invoke.
     * @param args Arguments to pass to the lambda's templated call.
     * @return True if a dispatch was found and attempted (even if disabled), false otherwise.
     */
    template<
        benchmark_ids CompileTimeBMID = static_cast<benchmark_ids>(0),
        typename GenericLambda, // The generic lambda that will be specialized
        typename... Args
    >
    static inline bool dispatch_by_bmid_generic_lambda_target(
        benchmark_ids runtime_bmid_to_run,
        GenericLambda&& lambda_target, // The generic lambda itself
        Args&&... args                 // Arguments to pass to the lambda's instantiation
    ) {
        if constexpr (CompileTimeBMID < benchmark_id_count) {
            if (runtime_bmid_to_run == CompileTimeBMID) {
                // MODIFIED: The check for BENCHMARK_ID_ENABLED has been removed.
                // The calling context in benchmark_html_out already ensures that we are only
                // dispatching for benchmarks that were enabled and have data.
                lambda_target.template operator()<CompileTimeBMID>(std::forward<Args>(args)...);
                return true;
            } else {
                // Recursively call for the next benchmark ID.
                if constexpr (static_cast<benchmark_ids>(CompileTimeBMID + 1) < benchmark_id_count) {
                    return dispatch_by_bmid_generic_lambda_target<static_cast<benchmark_ids>(CompileTimeBMID + 1)>(
                        runtime_bmid_to_run,
                        std::forward<GenericLambda>(lambda_target),
                        std::forward<Args>(args)...
                    );
                }
            }
        }
        return false; // No matching benchmark ID found or all IDs checked.
    }
} // namespace BenchmarkBMIDDispatch

// --- Output and Graphing Functions ---
// Functions responsible for generating SVG graphs and CSV data from benchmark results.
namespace BenchmarkOutput {
// In BenchmarkOutput namespace

    // ADDED: Definition for the output directory.
    #define BENCHMARK_OUTPUT_DIRECTORY "results"

    // CONFIGURATION FOR DATA POINT PRECISION
    constexpr int DATA_POINT_Y_PRECISION = 3; // Adjust as needed (e.g., 2, 3, 4, 6)
    constexpr double JS_EPSILON_FOR_LOG = 1e-6; // Constant for JS, formerly embedded

    // HTML Output Functions
    inline void write_html_global_css(std::ofstream& file);
    inline void write_html_global_javascript(std::ofstream& file);
    inline void write_html_doc_start_and_head(std::ofstream& file, const std::string& report_timestamp);
    inline void write_html_doc_end(std::ofstream& file);
    void benchmark_html_out(const std::string& file_id_timestamp);
    void performance_heatmap_out(std::ofstream& file, const std::vector<std::string>& ordered_shim_labels_for_columns);

    // This is an overload for a helper function to escape strings for JSON
    inline std::string escape_json(const std::string& s) {
        std::ostringstream o;
        for (auto c : s) {
            switch (c) {
                case '"':  o << "\\\""; break;
                case '\\': o << "\\\\"; break;
                case '\b': o << "\\b";  break;
                case '\f': o << "\\f";  break;
                case '\n': o << "\\n";  break;
                case '\r': o << "\\r";  break;
                case '\t': o << "\\t";  break;
                default:
                    if ('\x00' <= c && c <= '\x1f') {
                        o << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
                    } else {
                        o << c;
                    }
            }
        }
        return o.str();
    }

    inline void graph_out_dispatch(
        std::ofstream &file,
        const RegisteredBlueprintInfo& bp_info,
        benchmark_ids bmid,
        const std::map<std::string, std::vector<double>>& point_averages_for_all_shims,
        const std::map<std::string, double>& max_values_for_all_shims
    ) {
        std::map<std::string, RegisteredShimInfo> shim_details_lookup;
        for(const auto& si : g_registered_shims_info) {
            shim_details_lookup[si.label] = si;
        }

        std::vector<RegisteredShimInfo> sorted_shims_for_this_graph;
        unsigned int max_num_points_for_any_shim = 0;
        for (const auto &pair_shim_points : point_averages_for_all_shims) {
             if (shim_details_lookup.contains(pair_shim_points.first)) {
                sorted_shims_for_this_graph.push_back(shim_details_lookup.at(pair_shim_points.first));
                if (pair_shim_points.second.size() > max_num_points_for_any_shim) {
                    max_num_points_for_any_shim = static_cast<unsigned int>(pair_shim_points.second.size());
                }
            }
        }
        std::sort(sorted_shims_for_this_graph.begin(), sorted_shims_for_this_graph.end(),
                  [](const RegisteredShimInfo& a, const RegisteredShimInfo& b) {
                      return a.label < b.label;
                  });

        std::string safe_bp_label_for_id = bp_info.label;
        std::ranges::replace_if(safe_bp_label_for_id, [](char c){return !std::isalnum(c);},'_');
        std::string graph_id_base = "graph_" + safe_bp_label_for_id + "_" + std::to_string(static_cast<int>(bmid));

        // --- Serialize graph data to JSON ---
        std::ostringstream json_ss;
        json_ss << std::fixed << std::setprecision(DATA_POINT_Y_PRECISION);
        json_ss << "{";
        json_ss << "\"graphId\": \"" << graph_id_base << "\",";
        json_ss << "\"bpLabel\": \"" << escape_json(bp_info.label) << "\",";
        json_ss << "\"bmTitle\": \"" << escape_json(bmid < benchmark_id_count ? benchmark_graph_titles[bmid] : "Unknown Benchmark") << "\",";
        json_ss << "\"config\": {";
        json_ss << "\"KEY_COUNT\": " << KEY_COUNT << ",";
        json_ss << "\"KEY_COUNT_MEASUREMENT_INTERVAL\": " << KEY_COUNT_MEASUREMENT_INTERVAL << ",";
        json_ss << "\"MAX_NUM_POINTS_CONST\": " << max_num_points_for_any_shim << ",";
        json_ss << "\"DATA_POINT_Y_PRECISION\": " << DATA_POINT_Y_PRECISION << ",";
        json_ss << "\"JS_EPSILON_FOR_LOG\": " << std::fixed << std::setprecision(10) << JS_EPSILON_FOR_LOG << std::resetiosflags(std::ios_base::fixed) << std::setprecision(6);
        json_ss << "},";
        json_ss << "\"shims\": [";
        bool first_shim = true;
        for (const auto& shim_info_iter : sorted_shims_for_this_graph) {
            auto it_points = point_averages_for_all_shims.find(shim_info_iter.label);
            if (it_points == point_averages_for_all_shims.end() || it_points->second.empty()) continue;

            if (!first_shim) json_ss << ",";

            const auto& points_vector = it_points->second;
            std::ostringstream oss_compressed_y;
            long long scale_factor = 1;

            if (!points_vector.empty()) {
                double y_scale_factor_prec = std::pow(10, DATA_POINT_Y_PRECISION);

                // 1. Handle the absolute start value
                auto scaled_y0 = static_cast<long long>(std::round(points_vector[0] * y_scale_factor_prec));
                oss_compressed_y << scaled_y0;

                // 2. Collect deltas
                std::vector<long long> deltas;
                if (points_vector.size() > 1) {
                    deltas.reserve(points_vector.size() - 1);
                    long long last_scaled_y_val = scaled_y0;
                    for (size_t i = 1; i < points_vector.size(); ++i) {
                        auto scaled_yi = static_cast<long long>(std::round(points_vector[i] * y_scale_factor_prec));
                        deltas.push_back(scaled_yi - last_scaled_y_val);
                        last_scaled_y_val = scaled_yi;
                    }
                }

                // 3. Find scale factor for deltas & apply RLE
                if (!deltas.empty()) {
                    oss_compressed_y << ";"; // Use a separator for the delta part

                    // Find a common factor (1000, 100, 10) for scaling
                    long long factor = 1;
                    if (!deltas.empty()) {
                         bool all_div_1000 = true; for(long long d : deltas) if (d % 1000 != 0) {all_div_1000=false; break;}
                         if(all_div_1000) factor = 1000;
                         else {
                            bool all_div_100 = true; for(long long d : deltas) if (d % 100 != 0) {all_div_100=false; break;}
                            if(all_div_100) factor = 100;
                            else {
                                bool all_div_10 = true; for(long long d : deltas) if (d % 10 != 0) {all_div_10=false; break;}
                                if(all_div_10) factor = 10;
                            }
                         }
                    }
                    scale_factor = factor;

                    // Apply Run-Length Encoding to the scaled deltas
                    long long run_val = deltas[0] / scale_factor;
                    int run_count = 1;
                    bool first_delta = true;
                    for (size_t i = 1; i < deltas.size(); ++i) {
                        long long current_val = deltas[i] / scale_factor;
                        if (current_val == run_val) {
                            run_count++;
                        } else {
                            if (!first_delta) oss_compressed_y << " ";
                            oss_compressed_y << run_val;
                            if (run_count > 1) oss_compressed_y << "*" << run_count;
                            run_val = current_val;
                            run_count = 1;
                            first_delta = false;
                        }
                    }
                    if (!first_delta) oss_compressed_y << " ";
                    oss_compressed_y << run_val;
                    if (run_count > 1) oss_compressed_y << "*" << run_count;
                }
            }

            auto it_max_shim = max_values_for_all_shims.find(shim_info_iter.label);
            double shim_max_y = (it_max_shim != max_values_for_all_shims.end()) ? it_max_shim->second : 0.0;
            if (shim_max_y <= 1e-9) shim_max_y = 1.0;

            json_ss << "{";
            json_ss << "\"label\": \"" << escape_json(shim_info_iter.label) << "\",";
            json_ss << "\"color\": \"" << shim_info_iter.color << "\",";
            json_ss << "\"compressedY\": \"" << oss_compressed_y.str() << "\",";
            json_ss << "\"scaleFactor\": " << scale_factor << ",";
            json_ss << "\"maxY\": " << std::fixed << std::setprecision(DATA_POINT_Y_PRECISION) << shim_max_y << std::resetiosflags(std::ios_base::fixed) << std::setprecision(6);
            json_ss << "}";
            first_shim = false;
        }
        json_ss << "]";
        json_ss << "}";

        // --- Output placeholder and data script ---
        file << "<div class='graph-placeholder' id='" << graph_id_base << "_placeholder'></div>\n";
        file << "<script type='application/json' id='" << graph_id_base << "_data'>" << json_ss.str() << "</script>\n";

        file << std::resetiosflags(std::ios_base::fixed) << std::setprecision(6);
    }

    /**
     * @brief Dispatches SVG graph generation for a specific benchmark ID across all relevant blueprints and shims.
     *
     * Iterates through registered blueprints and shims to collect their data for the given BMID,
     * then calls `graph_out_dispatch` to generate the SVG for each blueprint.
     *
     * @tparam BI The benchmark ID for which graphs are to be generated.
     * @param file The output filestream for the SVG.
     * @param all_point_averages A map containing point averages for all (Blueprint, Shim, BMID) combinations.
     * @param all_max_values A map containing maximum values for all (Blueprint, Shim, BMID) combinations (for scaling).
     */
    template<benchmark_ids BI>
    void graphs_out_dispatch_for_bmid(
        std::ofstream& file,
        const std::map<ArenaKey, std::vector<double>>& all_point_averages,
        const std::map<ArenaKey, double>& all_max_values
    ) {
        // 1. Collect blueprints that have data for this benchmark BI
        std::vector<RegisteredBlueprintInfo> blueprints_to_graph_for_bmid;
        for (const auto& bp_info_candidate : g_registered_blueprints_info) {
            bool data_exists_for_this_bp_for_bmid = false;
            for (const auto& shim_info_candidate : g_registered_shims_info) { // Check any shim
                ArenaKey key_check = {bp_info_candidate.label, shim_info_candidate.label, BI};
                if (all_point_averages.contains(key_check) && !all_point_averages.at(key_check).empty()) {
                    data_exists_for_this_bp_for_bmid = true;
                    break;
                }
            }
            if (data_exists_for_this_bp_for_bmid) {
                blueprints_to_graph_for_bmid.push_back(bp_info_candidate);
            }
        }

        // 2. Sort these relevant blueprints alphabetically by label
        std::sort(blueprints_to_graph_for_bmid.begin(), blueprints_to_graph_for_bmid.end(),
            [](const RegisteredBlueprintInfo& a, const RegisteredBlueprintInfo& b) {
                return a.label < b.label;
            });

        // 3. Iterate through sorted, relevant blueprints
        for (const auto& bp_info : blueprints_to_graph_for_bmid) {
            std::map<std::string, std::vector<double>> point_averages_for_this_bp_bmid;
            std::map<std::string, double> max_values_for_this_bp_bmid;

            // Gather data for *this specific blueprint (bp_info)* and all shims
            for (const auto& shim_info : g_registered_shims_info) {
                ArenaKey key = {bp_info.label, shim_info.label, BI};

                auto it_points = all_point_averages.find(key);
                if (it_points != all_point_averages.end()) {
                    point_averages_for_this_bp_bmid[shim_info.label] = it_points->second;
                }

                auto it_max = all_max_values.find(key);
                if (it_max != all_max_values.end()) {
                    max_values_for_this_bp_bmid[shim_info.label] = it_max->second;
                }
            }
            if (!point_averages_for_this_bp_bmid.empty()) {
                 graph_out_dispatch(file, bp_info, BI, point_averages_for_this_bp_bmid, max_values_for_this_bp_bmid);
            }
        }
    }

    /**
     * @brief Generates a CSV file containing raw and adjusted benchmark data.
     *
     * The CSV includes raw run data for each measurement point and the calculated
     * adjusted averages, organized by blueprint, benchmark ID, and shim.
     *
     * @param file_id_suffix A suffix for the filename (e.g., timestamp).
     * @param raw_micro_data A map containing raw per-run, per-micro-measurement data.
     * @param adjusted_point_averages A map containing the adjusted averages for each measurement point.
     */
    inline void csv_out_dispatch(
        const std::string& file_id_suffix,
        const std::map<ArenaKey, std::vector<std::vector<uint64_t>>>& raw_micro_data,
        const std::map<ArenaKey, std::vector<double>>& adjusted_point_averages
    ) {
        std::ofstream file("result_" + file_id_suffix + ".csv");
        if (!file.is_open()) {
            std::cerr << "Error: csv_out_dispatch cannot open file 'result_" << file_id_suffix << ".csv'" << std::endl;
            return;
        }
        file << "sep=;\nN;"; // CSV separator for Excel/LibreOffice import.
        size_t num_measurement_points = 0;
        // Determine the number of measurement points from config or actual data.
        if constexpr (KEY_COUNT > 0 && KEY_COUNT_MEASUREMENT_INTERVAL > 0) {
            num_measurement_points = KEY_COUNT / KEY_COUNT_MEASUREMENT_INTERVAL;
        } else if (!adjusted_point_averages.empty() && !adjusted_point_averages.begin()->second.empty()) {
            num_measurement_points = adjusted_point_averages.begin()->second.size();
        }

        // Write header row with N values (number of keys at each measurement interval).
        for (size_t i = 0; i < num_measurement_points; ++i) {
            file << (i + 1) * KEY_COUNT_MEASUREMENT_INTERVAL << ";";
        }
        file << "\n\n";

        // Iterate through all enabled benchmarks, blueprints, and shims to write data.
        for (benchmark_ids bmid : g_enabled_benchmark_ids_list) {
            for (const auto& bp_info : g_registered_blueprints_info) {
                for (const auto& shim_info : g_registered_shims_info) {
                    file << bp_info.label << ":" << (bmid < benchmark_id_count ? benchmark_names[bmid] : "Unknown") << ":" << shim_info.label << "\n";
                    ArenaKey rk = {bp_info.label, shim_info.label, bmid};

                    // Write raw data for each run.
                    auto raw_it = raw_micro_data.find(rk);
                    if (raw_it != raw_micro_data.end()) {
                        const auto& all_runs_for_key = raw_it->second;
                        for (size_t run_i = 0; run_i < all_runs_for_key.size(); ++run_i) {
                            file << "Run " << run_i << ";";
                            const auto& single_run_points = all_runs_for_key[run_i];
                            for (unsigned long single_run_point : single_run_points) {
                                file << single_run_point << ";";
                            }
                            // Pad with N/A if a run has fewer measurement points than the determined max.
                            for (size_t res_i = single_run_points.size(); res_i < num_measurement_points; ++res_i) file << "N/A;";
                            file << "\n";
                        }
                    } else {
                        file << "Raw data not found for this key.\n";
                    }

                    // Write adjusted average data.
                    auto med_it = adjusted_point_averages.find(rk);
                    file << "Adjusted average;";
                    if (med_it != adjusted_point_averages.end()) {
                        const auto& points_vector = med_it->second;
                        for (double res_i : points_vector) {
                            file << res_i << ";";
                        }
                        // Pad with N/A if adjusted averages have fewer points than the determined max.
                        for (size_t res_i = points_vector.size(); res_i < num_measurement_points; ++res_i) file << "N/A;";
                    } else {
                        for (size_t res_i = 0; res_i < num_measurement_points; ++res_i) file << "N/A;";
                    }
                    file << "\n\n";
                }
            }
        }
        file.close();
    }

    /**
     * @brief Writes the global CSS styles to the HTML file.
     * @param file The output filestream.
     */
    inline void write_html_global_css(std::ofstream& file) {
        // Constants needed for global CSS that were in graph_out_dispatch
        constexpr double INNER_SVG_TOTAL_WIDTH_CSS = 990.0;
        constexpr double INNER_SVG_PLOT_LEFT_MARGIN_CSS = 18.0;
        constexpr double mini_pane_width_css = INNER_SVG_TOTAL_WIDTH_CSS - INNER_SVG_PLOT_LEFT_MARGIN_CSS;

        file << "<style>\n"
             // General Page Styles
             // Add padding-bottom to body to prevent the fixed global controls from hiding the last bit of content
             << "body { font-family: -apple-system, BlinkMacSystemFont, \"Segoe UI\", Roboto, Helvetica, Arial, sans-serif, \"Apple Color Emoji\", \"Segoe UI Emoji\", \"Segoe UI Symbol\"; background-color: #1e1e1e; color: #d4d4d4; margin: 0; padding: 20px 20px 120px 20px; text-align: center; line-height: 1.6; }\n"
             << ".container { display: inline-block; background-color: #252526; padding: 20px 30px; margin-top: 8px; margin-bottom: 20px; border-radius: 8px; text-align: left; max-width: 95%; box-shadow: 0 4px 8px rgba(0,0,0,0.2); overflow-x: auto;}\n"
             << "h1 { color: #4ec9b0; text-align: center; border-bottom: 1px solid #3e3e42; padding-bottom: 10px; margin-top: 0;}\n"
             << "h2 { color: #c586c0; text-align: center; border-bottom: 1px solid #3e3e42; padding-bottom: 8px; margin-top: 30px;}\n"
             // --- Pinned Global Shim Control Widget Styles ---
             << "#global-shim-controls { position: fixed; bottom: 0; left: 0; right: 0; z-index: 1000; background-color: rgba(30, 30, 30, 0.95); backdrop-filter: blur(5px); -webkit-backdrop-filter: blur(5px); border-top: 1px solid #4e4e52; box-shadow: 0 -4px 12px rgba(0,0,0,0.3); padding: 8px 20px; display: flex; flex-direction: column; justify-content: center; align-items: center; gap: 8px; }\n"
             << "#global-shim-controls-title { font-size: 10px; color: #ffffff; text-transform: uppercase; letter-spacing: 0.5px; margin: 0; padding: 0; font-weight: bold; }\n"
             << "#global-shim-toggles-container, #global-weight-toggles-container { display: flex; flex-wrap: wrap; justify-content: center; align-items: center; gap: 5px 15px; }\n"
             << "#global-shim-toggles-container .log-scale-toggle { margin-left: 10px; border-left: 1px solid #555; padding-left: 15px; }\n"
             << "#global-weight-toggles-container { border-top: 1px solid #444; padding-top: 6px; margin-top: 2px; font-size: 11px; }\n"
             << "#global-weight-toggles-container > span { display: inline-flex; align-items: center; gap: 5px; }\n"
             << ".weight-slider { margin: 0; vertical-align: middle; width: 5rem }\n"
             << "#global-weight-toggles-container .weight-value { font-family: monospace; width: 30px; text-align: left; display: inline-block; color: #9cdcfe; }\n"
             // --- Heatmap and Table Styles ---
             << "table { border-collapse: collapse; margin: 25px auto; background-color: #2d2d30; color: #cccccc; font-size: 12px; min-width: 800px; width: auto; }\n"
             << "th, td { border: 1px solid #3e3e42; padding: 0; text-align: center; }\n" // padding to 0
             << "td > div, .heatmap-bp-bm-link { padding: 4px 12px; }\n" // add padding to inner elements
             << "#heatmap-table tbody td { position: relative; }\n" // Make cells position-aware for tooltips
             << "th.col-header { background-color: #007acc; color: white; height: 100px; min-width: 90px; max-width: 160px; position: relative; padding: 4px 12px; }\n"
             << "th.col-header > div { position: absolute; left: 50%; top: 55%; transform: translate(-50%, -50%) rotate(-40deg); white-space: nowrap; display: flex; flex-direction: column; justify-content: center; text-align: center; }\n"
             << "th.row-header-th { background-color: #37373d; text-align: left; font-weight: bold; min-width: 200px; padding: 4px 12px; }\n"
             << "td.row-header { text-align: left; font-weight: bold; background-color: #37373d; min-width: 200px; }\n"
             << "td.row-header-link-cell { padding: 0 !important; }\n"
             << ".heatmap-bp-bm-link { text-decoration: none; color: inherit; display: block; width: 100%; height: 100%; box-sizing: border-box; }\n"
             << ".heatmap-bp-bm-link:hover { color: #c586c0; text-decoration: underline; background-color: #404045; }\n"
             << ".heatmap-bp-bm-link .link-emoji { font-size: 1em; vertical-align: middle; margin-left: 5px; }\n"
             << ".graph-title-link { text-decoration: none; color: #c586c0; display: inline; }\n"
             << ".graph-title-link:hover { text-decoration: underline; color: #d799d8; }\n"
             << ".graph-title-link .link-emoji { font-size: 1em; vertical-align: middle; margin-left: 5px; }\n"
             // --- Tooltip Styles ---
             << ".heatmap-tooltip { visibility: hidden; opacity: 0; position: absolute; z-index: 10; bottom: 115%; left: 50%; transform: translateX(-50%); padding: 5px 8px; border-radius: 4px; background-color: #1a1a1a; border: 1px solid #555; color: #d4d4d4; font-size: 11px; font-family: Consolas, \"Courier New\", monospace; white-space: nowrap; pointer-events: none; transition: opacity 0.2s ease-in-out; }\n"
             << ".heatmap-tooltip::after { content: ''; position: absolute; top: 100%; left: 50%; margin-left: -5px; border-width: 5px; border-style: solid; border-color: #1a1a1a transparent transparent transparent; }\n"
             << "#heatmap-table tbody td:hover .heatmap-tooltip { visibility: visible; opacity: 1; }\n"
             // --- Highlight animation for anchor links (using a pseudo-element overlay) ---
             << "@keyframes highlight-fade-opacity-animation { from { opacity: 1; } to { opacity: 0; } }\n"
             << ".heading.highlight-fade { position: relative; }\n"
             << "tr.highlight-fade > td { position: relative; }\n"
             << ".heading.highlight-fade::before, tr.highlight-fade > td::before {\n"
             << "  content: '';\n"
             << "  position: absolute; top: 0; left: 0; right: 0; bottom: 0; z-index: 1;\n"
             << "  background-color: rgba(128, 128, 128, 0.4);\n"
             << "  animation: highlight-fade-opacity-animation 3s ease-out;\n"
             << "  pointer-events: none;\n"
             << "}\n"
             << ".settings { font-family: Consolas, \"Courier New\", monospace; background-color: #1e1e1e; border: 1px solid #3e3e42; padding: 15px; border-radius: 5px; margin-bottom:25px; text-align:left; font-size:13px; color: #9cdcfe; }\n"
             << ".settings strong { color: #569cd6; }\n"
             << ".tombstone-marker { color: #8e44ad; font-weight: bold; margin-left: 3px; }\n"
             << ".graph-container { margin-top: 20px; margin-bottom: 20px; text-align: center; }\n"
             << ".link-to-graph-icon { text-decoration:none; color: #75715e; font-size:1.0em; margin-left: 8px; vertical-align: middle; }\n"
             << ".link-to-graph-icon:hover { color: #c586c0; }\n"
             // --- Global Graph CSS (formerly in graph_out_dispatch) ---
             << ".outerDiv{padding:6px;width:100%;height:100%;box-sizing:border-box;line-height:0;text-align:center; position: relative;}\n"
             << ".verticalSpacer{height:6px;}\n"
             << ".heading{height:18px;font:14px sans-serif;line-height:18px;}\n"
             << "input[type=\"checkbox\"] { position: absolute; opacity: 0; width: 0; height: 0; cursor: pointer; }\n"
             << "label { display: inline-flex; align-items: center; height: 14px; font: 12px sans-serif; line-height: 14px; margin-right: 5px; cursor: pointer; user-select: none; color: #d4d4d4; }\n"
             << "label:before { content: ''; display: inline-block; width: 12px; height: 12px; margin-right: 4px; border: 1px solid #555; background-color: transparent; box-sizing: border-box; }\n"
             << ".shim-toggle-checkbox + label:before { border-radius: 6px; border-color: var(--shim-color, #888); }\n"
             << ".shim-toggle-checkbox:checked + label:before { background-color: var(--shim-color, #888); border-color: var(--shim-color, #888); }\n"
             << ".shim-toggle-checkbox:not(:checked) + label:before { background-color: transparent; }\n"
             << ".log-scale-toggle + label:before { border-radius: 2px; border-color: #888; background-color: #fff; }\n"
             << ".log-scale-toggle:checked + label:before { background-color: #007bff; border-color: #007bff; color: white; font-size: 10px; line-height: 11px; text-align: center; font-weight: bold; content: '\\2713'; }\n"
             << ".log-scale-toggle:not(:checked) + label:before { content: ''; }\n"
             << "text{fill:#d4d4d4;}\n"
             // .innerSVG width and height are from C++ constants
             << ".innerSVG{width:"<< INNER_SVG_TOTAL_WIDTH_CSS << "px;height:572px; cursor: crosshair; /* For drag-to-zoom indication */}\n"
             << ".axis{stroke:#a0a0a0;}\n"
             << ".axisLabel{font:12px sans-serif; fill: #a0a0a0;}\n"
             << ".plotLine{fill:none;stroke-width:1;vector-effect:non-scaling-stroke; stroke: var(--shim-color, #888);}\n"
             << ".plotGroup { transform-origin: 0 0; } \n"
             << ".selectionRect { fill: rgba(0, 122, 204, 0.2); stroke: rgba(0, 122, 204, 0.6); stroke-width: 1px; }\n"
             << ".x-axis-tick line { stroke: #a0a0a0; }\n"
             << ".x-axis-tick text { font: 9px sans-serif; fill: #a0a0a0; text-anchor: middle; alignment-baseline: middle; }\n"
             // Graph controls flex container
             << ".graph-controls-flex-container { display: flex; justify-content: flex-end; align-items: center; height: 20px; padding-right: 6px; }\n"
             << ".graph-controls-flex-container .log-scale-toggle { margin-left:10px; }\n"
             << ".graph-button.reset-zoom-button { margin-left: 15px; padding: 1px 6px; font-size: 11px; height: 18px; vertical-align: middle; border-radius:3px; background-color:#555; color:#ddd; border:1px solid #666; cursor:pointer; }\n"
             // Mini Pane CSS
             << ".mini-pane-container { margin-left: " << INNER_SVG_PLOT_LEFT_MARGIN_CSS << "px; width: " << mini_pane_width_css << "px; height: 20px; background-color: #3a3a3d; position: relative; border: 1px solid #4a4a4f; user-select: none; box-sizing: border-box; }\n"
             << ".mini-pane-highlight { position: absolute; top: 0; left: 0; height: 100%; background-color: rgba(0, 122, 204, 0.4); border: 1px solid rgba(0, 122, 204, 0.7); box-sizing: border-box; cursor: grab; }\n"
             << ".mini-pane-resize-handle { position: absolute; top: 0; width: 6px; height: 100%; cursor: ew-resize; z-index:1; }\n"
             << ".mini-pane-resize-handle.left { left: -3px; }\n"
             << ".mini-pane-resize-handle.right { right: -3px; }\n"
             << "</style>\n";
    }

    /**
     * @brief Writes the global JavaScript for graph interactivity to the HTML file.
     * @param file The output filestream.
     */
    inline void write_html_global_javascript(std::ofstream& file) {
file << R"delimiter(<script type='text/javascript'>
// <![CDATA[
function populateGraphFromData(graphClone, data) {
    const graphId = data.graphId;
    const svg = graphClone.querySelector('svg');
    svg.id = graphId;

    // --- Populate Title and Links ---
    const titleText = graphClone.querySelector('.graph-title-text');
    titleText.textContent = `${data.bpLabel}: ${data.bmTitle}`;
    const titleLink = graphClone.querySelector('.graph-title-link');
    const safeBpLabel = data.bpLabel.replace(/[^a-zA-Z0-9]/g, '_');
    const bmid = graphId.split('_').pop();
    titleLink.dataset.highlightTargetId = `heatmap_row_${safeBpLabel}_${bmid}`;

    // --- Populate Shim Toggles and Polylines ---
    const togglesContainer = graphClone.querySelector('.shim-toggles-container');
    const plotGroup = graphClone.querySelector('.plotGroup');
    data.shims.forEach(shim => {
        const safeLabel = shim.label.replace(/[^a-zA-Z0-9]/g, '_');
        const cssIdBase = `Plot_${safeLabel}_${safeBpLabel}_${bmid}`;

        // Create Toggles
        const checkbox = document.createElement('input');
        checkbox.id = `${cssIdBase}Checkbox`;
        checkbox.className = 'shim-toggle-checkbox';
        checkbox.type = 'checkbox';
        checkbox.checked = true;
        checkbox.dataset.color = shim.color;
        togglesContainer.appendChild(checkbox);

        const label = document.createElement('label');
        label.htmlFor = checkbox.id;
        label.textContent = shim.label;
        togglesContainer.appendChild(label);

        // Create Polylines
        const polyline = document.createElementNS('http://www.w3.org/2000/svg', 'polyline');
        polyline.id = `${cssIdBase}Line`;
        polyline.classList.add('plotLine');
        polyline.dataset.compressedYValues = shim.compressedY;
        polyline.dataset.xInterval = data.config.KEY_COUNT_MEASUREMENT_INTERVAL;
        polyline.dataset.yScaleFactorPower = data.config.DATA_POINT_Y_PRECISION;
        polyline.dataset.maxY = shim.maxY;
        polyline.dataset.checkboxId = checkbox.id;
        polyline.dataset.color = shim.color;
        polyline.dataset.scaleFactor = shim.scaleFactor; // Pass scale factor to polyline
        plotGroup.appendChild(polyline);
    });

    // --- Create Log Scale Toggle ---
    const logScalePlaceholder = graphClone.querySelector('.log-scale-control-placeholder');
    const logToggleId = `logScaleToggle_${graphId}`;
    const logCheckbox = document.createElement('input');
    logCheckbox.id = logToggleId;
    logCheckbox.className = 'log-scale-toggle';
    logCheckbox.type = 'checkbox';
    logScalePlaceholder.appendChild(logCheckbox);
    const logLabel = document.createElement('label');
    logLabel.htmlFor = logToggleId;
    logLabel.textContent = 'Log Y-Axis';
    logScalePlaceholder.appendChild(logLabel);

    // --- Set unique IDs for other elements ---
    graphClone.querySelector('.reset-zoom-button').id = `resetZoom_${graphId}`;
    graphClone.querySelector('.innerSVG').id = `innerSVG_${graphId}`; // Not strictly needed, but good practice
    graphClone.querySelector('.y-axis-label').id = `yAxisLabel_${graphId}`;
    graphClone.querySelector('.x-axis-ticks').id = `xAxisTicksGroup_${graphId}`;
    const clipPath = graphClone.querySelector('clipPath');
    const clipPathId = `clipPath_${graphId}`;
    clipPath.id = clipPathId;
    graphClone.querySelector('.clipping-wrapper').style.clipPath = `url(#${clipPathId})`;
    graphClone.querySelector('.selectionRect').id = `selectionRect_${graphId}`;
    graphClone.querySelector('.mini-pane-container').id = `miniPaneContainer_${graphId}`;
    graphClone.querySelector('.mini-pane-highlight').id = `miniPaneHighlight_${graphId}`;
}


function initInteractiveGraph(graphId, graphData) {
  const svg = document.getElementById(graphId);
  if (!svg) { console.error('Graph SVG not found:', graphId); return; }
  const doc = svg.ownerDocument || document;

  // --- CONFIGURATION AND CONSTANTS ---
  const INNER_SVG_TOTAL_WIDTH = 990.0;
  const INNER_SVG_PLOT_LEFT_MARGIN = 18.0;
  const SVG_VIEWBOX_HEIGHT = 688;
  const PLOT_AREA_WIDTH_CONST = INNER_SVG_TOTAL_WIDTH - INNER_SVG_PLOT_LEFT_MARGIN;
  const PLOT_AREA_HEIGHT_CONST = 540.0;
  const PLOT_AREA_Y_OFFSET_TOP_CONST = 0.0;

  const config = {
      DATA_POINT_PRECISION_JS: graphData.config.DATA_POINT_Y_PRECISION,
      EPSILON_FOR_LOG: graphData.config.JS_EPSILON_FOR_LOG,
      KEY_COUNT_INTERVAL_CONST: graphData.config.KEY_COUNT_MEASUREMENT_INTERVAL,
      MAX_NUM_POINTS_CONST: graphData.config.MAX_NUM_POINTS_CONST,
      INNER_SVG_WIDTH: INNER_SVG_TOTAL_WIDTH,
      PLOT_AREA_X_OFFSET: INNER_SVG_PLOT_LEFT_MARGIN,
      PLOT_AREA_HEIGHT: PLOT_AREA_HEIGHT_CONST,
      PLOT_AREA_Y_OFFSET_TOP: PLOT_AREA_Y_OFFSET_TOP_CONST,
      PLOT_AREA_WIDTH: PLOT_AREA_WIDTH_CONST
  };

  // --- Set fixed attributes on template clone ---
  svg.querySelector('.y-axis-line').setAttribute('x1', config.PLOT_AREA_X_OFFSET);
  svg.querySelector('.y-axis-line').setAttribute('x2', config.PLOT_AREA_X_OFFSET);
  svg.querySelector('.y-axis-line').setAttribute('y1', 0);
  svg.querySelector('.y-axis-line').setAttribute('y2', config.PLOT_AREA_HEIGHT);
  svg.querySelector('.x-axis-line').setAttribute('x1', config.PLOT_AREA_X_OFFSET);
  svg.querySelector('.x-axis-line').setAttribute('x2', config.INNER_SVG_WIDTH);
  svg.querySelector('.x-axis-line').setAttribute('y1', config.PLOT_AREA_HEIGHT);
  svg.querySelector('.x-axis-line').setAttribute('y2', config.PLOT_AREA_HEIGHT);
  svg.querySelector('.x-axis-label').setAttribute('x', (config.PLOT_AREA_X_OFFSET + (config.INNER_SVG_WIDTH - config.PLOT_AREA_X_OFFSET) / 2.0));
  svg.querySelector('.x-axis-label').setAttribute('y', config.PLOT_AREA_HEIGHT + 26);
  svg.querySelector('clipPath > rect').setAttribute('x', 0);
  svg.querySelector('clipPath > rect').setAttribute('y', 0);
  svg.querySelector('clipPath > rect').setAttribute('width', config.PLOT_AREA_WIDTH);
  svg.querySelector('clipPath > rect').setAttribute('height', config.PLOT_AREA_HEIGHT);
  svg.querySelector('.clipping-wrapper').setAttribute('transform', `translate(${config.PLOT_AREA_X_OFFSET}, ${config.PLOT_AREA_Y_OFFSET_TOP})`);

  // --- Element selectors using graphId ---
  const logToggle = doc.getElementById('logScaleToggle_' + graphId);
  const plotGroup = svg.querySelector('.plotGroup');
  const clippingWrapper = svg.querySelector('.clipping-wrapper');
  const yAxisLabel = svg.querySelector('.y-axis-label');
  const xAxisTicksGroup = svg.querySelector('.x-axis-ticks');
  const innerSVGElement = svg.querySelector('.innerSVG');
  const selectionRectElement = svg.querySelector('.selectionRect');
  const resetZoomButton = doc.getElementById('resetZoom_' + graphId);
  const miniPaneContainer = doc.getElementById('miniPaneContainer_' + graphId);
  const miniPaneHighlight = doc.getElementById('miniPaneHighlight_' + graphId);
  const miniPaneLeftResize = miniPaneHighlight ? miniPaneHighlight.querySelector('.mini-pane-resize-handle.left') : null;
  const miniPaneRightResize = miniPaneHighlight ? miniPaneHighlight.querySelector('.mini-pane-resize-handle.right') : null;

  // Updated decompressPoints function is below
  function decompressPoints(poly) {
    if (poly.dataset.decompressedLinearPointsCache) { return poly.dataset.decompressedLinearPointsCache; }
    const compressedYStr = poly.dataset.compressedYValues;
    if (!compressedYStr || compressedYStr.trim() === '') {
      poly.dataset.decompressedLinearPointsCache = ''; return '';
    }
    const scaleFactor = parseInt(poly.dataset.scaleFactor) || 1;
    const yScaleFactorPower = parseInt(poly.dataset.yScaleFactorPower);
    const yScaler = Math.pow(10, yScaleFactorPower);
    const xInterval = parseInt(poly.dataset.xInterval);
    if (xInterval <= 0) { poly.dataset.decompressedLinearPointsCache = ''; return ''; }

    let initialValueStr;
    let deltaStr = '';
    const separatorIndex = compressedYStr.indexOf(';');
    if (separatorIndex !== -1) {
        initialValueStr = compressedYStr.substring(0, separatorIndex);
        deltaStr = compressedYStr.substring(separatorIndex + 1);
    } else {
        initialValueStr = compressedYStr;
    }

    if (!initialValueStr) { poly.dataset.decompressedLinearPointsCache = ''; return ''; }

    const deltaParts = deltaStr ? deltaStr.split(' ') : [];
    const linearPoints = [];

    // 1. Process the first absolute point
    let currentScaledY = parseInt(initialValueStr);
    if (isNaN(currentScaledY)) { poly.dataset.decompressedLinearPointsCache = ''; return ''; }

    let x_val = xInterval;
    let y_val = currentScaledY / yScaler;
    linearPoints.push(`${x_val},${y_val.toFixed(config.DATA_POINT_PRECISION_JS)}`);

    // 2. Process the RLE-encoded deltas
    for (const part of deltaParts) {
        if (!part) continue;

        let value, count;
        const rleIndex = part.indexOf('*');
        if (rleIndex !== -1) {
            value = parseInt(part.substring(0, rleIndex));
            count = parseInt(part.substring(rleIndex + 1));
        } else {
            value = parseInt(part);
            count = 1;
        }

        if (isNaN(value) || isNaN(count)) continue;

        const scaled_delta = value * scaleFactor;
        for (let i = 0; i < count; i++) {
            currentScaledY += scaled_delta;
            x_val += xInterval;
            y_val = currentScaledY / yScaler;
            linearPoints.push(`${x_val},${y_val.toFixed(config.DATA_POINT_PRECISION_JS)}`);
        }
    }

    const decompressedStr = linearPoints.join(' ');
    poly.dataset.decompressedLinearPointsCache = decompressedStr;
    return decompressedStr;
  }

  if (!logToggle || !plotGroup || !clippingWrapper || !yAxisLabel || !xAxisTicksGroup || !innerSVGElement ||
      !selectionRectElement || !resetZoomButton ||
      !miniPaneContainer || !miniPaneHighlight ||
      !miniPaneLeftResize || !miniPaneRightResize) {
    console.error('Essential graph elements not found for graph: ', graphId, {
        logToggleFound: !!logToggle, plotGroupFound: !!plotGroup, clippingWrapperFound: !!clippingWrapper,
        yAxisLabelFound: !!yAxisLabel, xAxisTicksGroupFound: !!xAxisTicksGroup,
        innerSVGElementFound: !!innerSVGElement, selectionRectElementFound: !!selectionRectElement,
        resetZoomButtonFound: !!resetZoomButton, miniPaneContainerFound: !!miniPaneContainer,
        miniPaneHighlightFound: !!miniPaneHighlight, miniPaneLeftResizeFound: !!miniPaneLeftResize,
        miniPaneRightResizeFound: !!miniPaneRightResize
    });
    return;
  }

  const polylines = Array.from(plotGroup.querySelectorAll('polyline.plotLine'));
  const EPSILON_GENERAL = 1e-9;
  const LOG_AXIS_PADDING_FACTOR_TOP = 1.2;
  const LOG_AXIS_PADDING_FACTOR_BOTTOM = 0.8;
  const LINEAR_AXIS_PADDING_FACTOR_TOP = 1.05;
  const LINEAR_AXIS_PADDING_FACTOR_BOTTOM = 0.95;
  const initialYAxisText = yAxisLabel.textContent || 'Time (µs or ns) ⟶';
  const logYAxisText = 'Time (Log Scale, µs or ns) ⟶';
  const X_AXIS_Y_POSITION = config.PLOT_AREA_HEIGHT;
  const X_AXIS_TICK_Y1 = X_AXIS_Y_POSITION;
  const X_AXIS_TICK_Y2 = X_AXIS_Y_POSITION + 2;
  const X_AXIS_LABEL_Y = X_AXIS_Y_POSITION + 8;
  let originalXMin_data, originalXMax_data;
  let currentXMin_data, currentXMax_data;
  let zoomEnabled = false;

  if (config.MAX_NUM_POINTS_CONST > 0 && config.KEY_COUNT_INTERVAL_CONST > 0) {
    originalXMin_data = config.KEY_COUNT_INTERVAL_CONST;
    originalXMax_data = config.MAX_NUM_POINTS_CONST * config.KEY_COUNT_INTERVAL_CONST;
    if (originalXMax_data > originalXMin_data + EPSILON_GENERAL) {
       zoomEnabled = true;
    } else if (config.MAX_NUM_POINTS_CONST === 1) {
       originalXMin_data = config.KEY_COUNT_INTERVAL_CONST * 0.5;
       originalXMax_data = config.KEY_COUNT_INTERVAL_CONST * 1.5;
       zoomEnabled = true;
    } else {
       zoomEnabled = false;
    }
  } else {
    zoomEnabled = false;
  }

  if (!zoomEnabled) {
    originalXMin_data = 0; originalXMax_data = 1;
    if (resetZoomButton) resetZoomButton.disabled = true;
    if (miniPaneContainer) miniPaneContainer.style.display = 'none';
  }

  currentXMin_data = originalXMin_data;
  currentXMax_data = originalXMax_data;

  let MIN_X_RANGE_DATA_UNITS;
  if (zoomEnabled) {
      const totalDataRange = originalXMax_data - originalXMin_data;
      let primaryMinRangeRequirement = config.KEY_COUNT_INTERVAL_CONST * 50;

      if (primaryMinRangeRequirement <= EPSILON_GENERAL) {
          if (totalDataRange > EPSILON_GENERAL) {
              primaryMinRangeRequirement = Math.max(totalDataRange * 0.01, 0.1);
          } else {
              primaryMinRangeRequirement = 0.1;
          }
      }
      MIN_X_RANGE_DATA_UNITS = Math.max(EPSILON_GENERAL * 50, primaryMinRangeRequirement);
      if (totalDataRange > EPSILON_GENERAL) {
          MIN_X_RANGE_DATA_UNITS = Math.min(MIN_X_RANGE_DATA_UNITS, totalDataRange);
      }
       if (MIN_X_RANGE_DATA_UNITS <= EPSILON_GENERAL) {
          MIN_X_RANGE_DATA_UNITS = EPSILON_GENERAL * 50;
      }
  } else {
      MIN_X_RANGE_DATA_UNITS = 1;
  }

  // (Paste the rest of the original initInteractiveGraph function here, starting from updateMiniPaneView)
  // ... For brevity, I've omitted the ~350 lines that are unchanged ...
  // ... It starts with the function definition: function updateMiniPaneView() { ...
  // ... and ends with the final two lines:
  // initializeGraphStylesAndListeners();
  // updateGraph();

  function updateMiniPaneView() {
    if (!zoomEnabled || !miniPaneContainer || !miniPaneHighlight) return;
    const totalRange = originalXMax_data - originalXMin_data;
    if (totalRange <= EPSILON_GENERAL) {
        miniPaneHighlight.style.width = '100%';
        miniPaneHighlight.style.left = '0%';
        return;
    }
    const currentRange = currentXMax_data - currentXMin_data;
    const highlightWidthPercent = totalRange > EPSILON_GENERAL ? (currentRange / totalRange) * 100 : 100;
    const highlightLeftPercent = totalRange > EPSILON_GENERAL ? ((currentXMin_data - originalXMin_data) / totalRange) * 100 : 0;
    miniPaneHighlight.style.width = Math.max(0.1, Math.min(100, highlightWidthPercent)) + '%';
    miniPaneHighlight.style.left = Math.max(0, Math.min(100 - Math.max(0.1, highlightWidthPercent), highlightLeftPercent)) + '%';
  }
  function enforceXZoomLimits() {
    if (!zoomEnabled) return;
    let newMin = currentXMin_data; let newMax = currentXMax_data;
    let currentRange = newMax - newMin;
    if (currentRange < MIN_X_RANGE_DATA_UNITS - EPSILON_GENERAL) {
        const center = (newMin + newMax) / 2;
        newMin = center - MIN_X_RANGE_DATA_UNITS / 2;
        newMax = center + MIN_X_RANGE_DATA_UNITS / 2;
    }
    if (newMin < originalXMin_data) {
        newMin = originalXMin_data;
        newMax = Math.max(newMax, originalXMin_data + MIN_X_RANGE_DATA_UNITS);
    }
    if (newMax > originalXMax_data) {
        newMax = originalXMax_data;
        newMin = Math.min(newMin, originalXMax_data - MIN_X_RANGE_DATA_UNITS);
    }
    newMin = Math.max(originalXMin_data, newMin);
    newMax = Math.min(originalXMax_data, newMax);
    if (newMax <= newMin + EPSILON_GENERAL) {
        if (originalXMax_data > originalXMin_data + EPSILON_GENERAL) {
             newMax = newMin + Math.min(MIN_X_RANGE_DATA_UNITS, originalXMax_data - newMin);
             newMax = Math.min(newMax, originalXMax_data);
        } else { newMax = newMin + MIN_X_RANGE_DATA_UNITS; }
    }
    if (newMax <= newMin + EPSILON_GENERAL && originalXMax_data > originalXMin_data + EPSILON_GENERAL) {
        if (Math.abs(newMin - originalXMin_data) < EPSILON_GENERAL && Math.abs(newMax - originalXMax_data) < EPSILON_GENERAL) {}
        else { newMax = newMin + MIN_X_RANGE_DATA_UNITS; }
    }
    currentXMin_data = newMin; currentXMax_data = newMax;
  }
  function formatNumberWithSeparators(numStr) {
    const num = parseFloat(numStr);
    if (isNaN(num)) return numStr;
    const roundedNum = parseFloat(num.toFixed(Math.max(0, config.DATA_POINT_PRECISION_JS -1)));
    if (Math.abs(roundedNum - Math.round(roundedNum)) < EPSILON_GENERAL * 100) {
        return Math.round(roundedNum).toLocaleString();
    }
    let maxFractionDigits = 2;
    if (Math.abs(roundedNum) < 10) maxFractionDigits = 3;
    if (Math.abs(roundedNum) < 1) maxFractionDigits = 4;
    return roundedNum.toLocaleString(undefined, { minimumFractionDigits: 0, maximumFractionDigits: maxFractionDigits });
  }
  function calculateNiceTicks(minValue, maxValue, maxTicks = 10) {
      if (!isFinite(minValue) || !isFinite(maxValue)) return [];
      let range = maxValue - minValue;
      if (range <= EPSILON_GENERAL) return [minValue];
      const targetTickCount = Math.max(2, Math.min(maxTicks, Math.floor(config.PLOT_AREA_WIDTH / 70)));
      let tickSpacing = range / targetTickCount;
      const niceMultipliers = [1, 2, 2.5, 5, 10];
      const exponent = Math.floor(Math.log10(tickSpacing));
      const magnitude = Math.pow(10, exponent);
      const normalizedTickSpacing = tickSpacing / magnitude;
      let bestNormalizedStep = niceMultipliers[niceMultipliers.length - 1];
      for (const mult of niceMultipliers) { if (normalizedTickSpacing <= mult) { bestNormalizedStep = mult; break; } }
      tickSpacing = bestNormalizedStep * magnitude;
      if (tickSpacing <= EPSILON_GENERAL) tickSpacing = magnitude > EPSILON_GENERAL ? magnitude : EPSILON_GENERAL*10;
      const ticks = []; let currentTick = Math.floor(minValue / tickSpacing - EPSILON_GENERAL) * tickSpacing;
      const slightlyExpandedMaxValue = maxValue + tickSpacing * 0.1;
      for (let i = 0; i < maxTicks * 2 && currentTick <= slightlyExpandedMaxValue + EPSILON_GENERAL; i++) {
          if (currentTick >= minValue - EPSILON_GENERAL && currentTick <= maxValue + EPSILON_GENERAL) { ticks.push(currentTick); }
          else if (ticks.length > 0 && currentTick > maxValue + EPSILON_GENERAL) { break; }
          else if (ticks.length === 0 && currentTick > maxValue + EPSILON_GENERAL) { break; }
          currentTick += tickSpacing; if(tickSpacing <= EPSILON_GENERAL) break;
      }
      if (ticks.length < 2 && (maxValue - minValue > EPSILON_GENERAL)) {
          ticks.length = 0; ticks.push(minValue);
          if (Math.abs(maxValue - minValue) > tickSpacing * 0.1 || ticks.length === 1) { ticks.push(maxValue); }
      }
      return ticks.filter((val, idx, arr) => idx === 0 || Math.abs(val - arr[idx-1]) > EPSILON_GENERAL * tickSpacing).sort((a,b) => a-b);
  }
  function updateXAxisTicks(viewMinX_data, viewMaxX_data) {
    if (!xAxisTicksGroup) return;
    xAxisTicksGroup.innerHTML = ''; const dataRange = viewMaxX_data - viewMinX_data;
    if (dataRange <= EPSILON_GENERAL) {
        if (isFinite(viewMinX_data)) {
            const x_screen = config.PLOT_AREA_X_OFFSET;
            const line = doc.createElementNS('http://www.w3.org/2000/svg', 'line');
            line.setAttribute('x1', x_screen); line.setAttribute('x2', x_screen);
            line.setAttribute('y1', X_AXIS_TICK_Y1); line.setAttribute('y2', X_AXIS_TICK_Y2);
            xAxisTicksGroup.appendChild(line);
            const text = doc.createElementNS('http://www.w3.org/2000/svg', 'text');
            text.setAttribute('x', x_screen); text.setAttribute('y', X_AXIS_LABEL_Y);
            text.textContent = formatNumberWithSeparators(viewMinX_data.toString());
            xAxisTicksGroup.appendChild(text);
        } return;
    }
    const niceTickValues = calculateNiceTicks(viewMinX_data, viewMaxX_data);
    niceTickValues.forEach(tickVal_data => {
        const x_screen = config.PLOT_AREA_X_OFFSET + ((tickVal_data - viewMinX_data) / dataRange) * config.PLOT_AREA_WIDTH;
        if (x_screen >= config.PLOT_AREA_X_OFFSET - 5 && x_screen <= config.PLOT_AREA_X_OFFSET + config.PLOT_AREA_WIDTH + 5) {
            const line = doc.createElementNS('http://www.w3.org/2000/svg', 'line');
            line.setAttribute('x1', x_screen); line.setAttribute('x2', x_screen);
            line.setAttribute('y1', X_AXIS_TICK_Y1); line.setAttribute('y2', X_AXIS_TICK_Y2);
            xAxisTicksGroup.appendChild(line);
            const text = doc.createElementNS('http://www.w3.org/2000/svg', 'text');
            text.setAttribute('x', x_screen); text.setAttribute('y', X_AXIS_LABEL_Y);
            text.textContent = formatNumberWithSeparators(tickVal_data.toString());
            xAxisTicksGroup.appendChild(text);
        }
    });
  }
  function updateGraph() {
    if (zoomEnabled) { enforceXZoomLimits(); }
    else { currentXMin_data = originalXMin_data; currentXMax_data = originalXMax_data; }
    const isLogScale = logToggle.checked;
    let overallMinVisibleY = Infinity, overallMaxVisibleY = -Infinity;
    let anyDataInView = false, visibleShimsExist = false;
    polylines.forEach(poly => {
      const checkbox = doc.getElementById(poly.dataset.checkboxId);
      if (checkbox && checkbox.checked) {
        visibleShimsExist = true; poly.style.visibility = 'visible';
        const decompressedPointsStr = decompressPoints(poly);
        if (decompressedPointsStr) {
          decompressedPointsStr.split(' ').forEach(pStr => {
            if (!pStr) return; const parts = pStr.split(',');
            const x_data = parseFloat(parts[0]); const y_data = parseFloat(parts[1]);
            if (x_data >= currentXMin_data - EPSILON_GENERAL && x_data <= currentXMax_data + EPSILON_GENERAL) {
              overallMinVisibleY = Math.min(overallMinVisibleY, y_data);
              overallMaxVisibleY = Math.max(overallMaxVisibleY, y_data);
              anyDataInView = true;
            }
          });
        }
      } else { poly.style.visibility = 'hidden'; }
    });
    if (!visibleShimsExist || !anyDataInView) {
      plotGroup.style.transform = 'translate(0, ' + (config.PLOT_AREA_HEIGHT / 2) + ') scale(1, 0)';
      yAxisLabel.textContent = initialYAxisText;
      polylines.forEach(poly => { poly.setAttribute('points', decompressPoints(poly) || ''); });
      updateMiniPaneView(); updateXAxisTicks(currentXMin_data, currentXMax_data); return;
    }
    if (overallMinVisibleY === Infinity || overallMaxVisibleY === -Infinity) { overallMinVisibleY = 0; overallMaxVisibleY = 1; }
    else if (Math.abs(overallMinVisibleY - overallMaxVisibleY) < EPSILON_GENERAL) {
        const val = overallMaxVisibleY; const y_delta = Math.max(0.5, Math.abs(val * 0.1), EPSILON_GENERAL * 100);
        overallMinVisibleY = val - y_delta; overallMaxVisibleY = val + y_delta;
    }
    let new_Sy_transform, new_Ty_transform_for_plotGroup;
    let y_axis_min_val_for_transform, y_axis_max_val_for_transform;
    if (isLogScale) {
      yAxisLabel.textContent = logYAxisText;
      let y_log_min_data_val = Math.max(config.EPSILON_FOR_LOG, overallMinVisibleY);
      let y_log_max_data_val = Math.max(y_log_min_data_val * 1.1 , overallMaxVisibleY);
      const padded_y_log_min_data = Math.max(config.EPSILON_FOR_LOG, y_log_min_data_val * LOG_AXIS_PADDING_FACTOR_BOTTOM);
      const padded_y_log_max_data = Math.max(padded_y_log_min_data * 1.1, y_log_max_data_val * LOG_AXIS_PADDING_FACTOR_TOP);
      y_axis_min_val_for_transform = Math.log10(padded_y_log_min_data);
      y_axis_max_val_for_transform = Math.log10(padded_y_log_max_data);
      polylines.forEach(poly => {
        const checkbox = doc.getElementById(poly.dataset.checkboxId); if (!checkbox || !checkbox.checked) return;
        const decompressedPointsStr = decompressPoints(poly); if (!decompressedPointsStr) { poly.setAttribute('points', ''); return; }
        const newPoints = decompressedPointsStr.split(' ').map(pStr => {
          if (!pStr) return ''; const parts = pStr.split(','); const x_data = parts[0];
          const y_linear_data = parseFloat(parts[1]);
          const y_point_log_transformed = Math.log10(Math.max(y_linear_data, padded_y_log_min_data));
          return x_data + ',' + y_point_log_transformed.toFixed(config.DATA_POINT_PRECISION_JS + 3);
        }).join(' ');
        poly.setAttribute('points', newPoints);
      });
    } else {
      yAxisLabel.textContent = initialYAxisText;
      let y_lin_min_data_val = overallMinVisibleY; let y_lin_max_data_val = overallMaxVisibleY;
      y_axis_min_val_for_transform = y_lin_min_data_val * LINEAR_AXIS_PADDING_FACTOR_BOTTOM;
      if (y_lin_min_data_val >= 0 && y_axis_min_val_for_transform < 0 && LINEAR_AXIS_PADDING_FACTOR_BOTTOM < 1) {
           y_axis_min_val_for_transform = y_lin_min_data_val * (2 - LINEAR_AXIS_PADDING_FACTOR_TOP);
           if (y_lin_min_data_val > EPSILON_GENERAL) y_axis_min_val_for_transform = Math.max(0, y_axis_min_val_for_transform);
           else y_axis_min_val_for_transform = y_lin_min_data_val - Math.abs(y_lin_max_data_val - y_lin_min_data_val)*0.05 - EPSILON_GENERAL;
      }
      y_axis_max_val_for_transform = y_lin_max_data_val * LINEAR_AXIS_PADDING_FACTOR_TOP;
      if (y_axis_min_val_for_transform >= y_axis_max_val_for_transform - EPSILON_GENERAL) {
          const mid = (y_lin_min_data_val + y_lin_max_data_val) / 2;
          const y_delta = Math.max(0.5, Math.abs(y_lin_max_data_val - y_lin_min_data_val) * 0.1, EPSILON_GENERAL * 100);
          y_axis_min_val_for_transform = mid - y_delta; y_axis_max_val_for_transform = mid + y_delta;
      }
      polylines.forEach(poly => {
        const checkbox = doc.getElementById(poly.dataset.checkboxId); if (!checkbox || !checkbox.checked) return;
        poly.setAttribute('points', decompressPoints(poly) || '');
      });
    }
    const y_axis_range_for_transform = y_axis_max_val_for_transform - y_axis_min_val_for_transform;
    if (y_axis_range_for_transform < EPSILON_GENERAL) {
        new_Sy_transform = -1;
        new_Ty_transform_for_plotGroup = config.PLOT_AREA_HEIGHT / 2 - (y_axis_min_val_for_transform * new_Sy_transform);
    } else {
        new_Sy_transform = - (config.PLOT_AREA_HEIGHT / y_axis_range_for_transform);
        new_Ty_transform_for_plotGroup = -(y_axis_max_val_for_transform * new_Sy_transform);
    }
    const currentXRange_data_view = currentXMax_data - currentXMin_data;
    let new_Sx_transform, new_Tx_transform_for_plotGroup;
    if (currentXRange_data_view < EPSILON_GENERAL) {
        new_Sx_transform = 1;
        new_Tx_transform_for_plotGroup = config.PLOT_AREA_WIDTH / 2 - (currentXMin_data * new_Sx_transform);
    } else {
        new_Sx_transform = (config.PLOT_AREA_WIDTH / currentXRange_data_view);
        new_Tx_transform_for_plotGroup = -(currentXMin_data * new_Sx_transform);
    }
    if (isNaN(new_Sx_transform) || !isFinite(new_Sx_transform) || isNaN(new_Tx_transform_for_plotGroup) || !isFinite(new_Tx_transform_for_plotGroup) ||
        isNaN(new_Sy_transform) || !isFinite(new_Sy_transform) || isNaN(new_Ty_transform_for_plotGroup) || !isFinite(new_Ty_transform_for_plotGroup) ) {
        console.error("Invalid transform calculated for graph:", graphId, {Sx:new_Sx_transform, Tx:new_Tx_transform_for_plotGroup, Sy:new_Sy_transform, Ty:new_Ty_transform_for_plotGroup});
        plotGroup.style.transform = 'translate(0, ' + (config.PLOT_AREA_HEIGHT / 2) + ') scale(0.001, 0.001)';
    } else {
        plotGroup.style.transform = 'translate(' + new_Tx_transform_for_plotGroup + 'px, ' + new_Ty_transform_for_plotGroup + 'px) scale(' + new_Sx_transform + ', ' + new_Sy_transform + ')';
    }
    updateMiniPaneView(); updateXAxisTicks(currentXMin_data, currentXMax_data);
  }
  function initializeGraphStylesAndListeners() {
    const shimCheckboxes = Array.from(svg.querySelectorAll('.shim-toggle-checkbox'));
    shimCheckboxes.forEach(checkbox => {
      const color = checkbox.dataset.color;
      const label = doc.querySelector('label[for="' + checkbox.id + '"]');
      if (label && color) label.style.setProperty('--shim-color', color);
      const polylineId = checkbox.id.replace('Checkbox', 'Line');
      const polyline = plotGroup.querySelector('#' + polylineId);
      if (polyline && color) polyline.style.setProperty('--shim-color', color);
      checkbox.addEventListener('change', updateGraph);
      if (label && polyline) {
        label.addEventListener('mouseenter', () => {
          const cb = doc.getElementById(polyline.dataset.checkboxId);
          if (cb && cb.checked) { polyline.style.strokeWidth = '2.5'; if (plotGroup && polyline.parentNode === plotGroup) { plotGroup.appendChild(polyline);}}
        });
        label.addEventListener('mouseleave', () => { polyline.style.strokeWidth = '1'; });
      }
    });
    if (logToggle) logToggle.addEventListener('change', updateGraph);
    if (resetZoomButton) {
        resetZoomButton.addEventListener('click', () => {
            if (!zoomEnabled) return;
            currentXMin_data = originalXMin_data; currentXMax_data = originalXMax_data;
            updateGraph();
        });
        if (!zoomEnabled) resetZoomButton.disabled = true;
    }
    if (!zoomEnabled) { updateGraph(); return; }
    let isZoomDragging = false; let zoomDragStartX_screen;
    innerSVGElement.addEventListener('mousedown', (e) => {
        if (e.button !== 0 || !zoomEnabled) return;
        if (miniPaneContainer.contains(e.target) || (clippingWrapper && clippingWrapper.contains(e.target) && !e.target.classList.contains('innerSVG'))) {
            let targetElement = e.target; let isPlotBackgroundClick = false;
            while(targetElement && targetElement !== svg) {
                if (targetElement === innerSVGElement || targetElement === plotGroup || targetElement === clippingWrapper) {isPlotBackgroundClick = true; break;}
                if (targetElement.classList && targetElement.classList.contains('plotLine')) {isPlotBackgroundClick = false; break;}
                targetElement = targetElement.parentNode;
            }
            if (!isPlotBackgroundClick) return;
        } else if (!innerSVGElement.contains(e.target) && e.target !== innerSVGElement) { return; }
        isZoomDragging = true; const rect = innerSVGElement.getBoundingClientRect();
        zoomDragStartX_screen = e.clientX - rect.left;
        selectionRectElement.setAttribute('x', zoomDragStartX_screen);
        selectionRectElement.setAttribute('y', config.PLOT_AREA_Y_OFFSET_TOP);
        selectionRectElement.setAttribute('width', 0);
        selectionRectElement.setAttribute('height', config.PLOT_AREA_HEIGHT);
        selectionRectElement.style.visibility = 'visible'; innerSVGElement.style.cursor = 'ew-resize';
    });
    doc.addEventListener('mousemove', (e) => {
        if (!isZoomDragging || !zoomEnabled) return;
        const rect = innerSVGElement.getBoundingClientRect();
        let currentMouseX_screen = e.clientX - rect.left;
        const x = Math.min(zoomDragStartX_screen, currentMouseX_screen);
        const width = Math.abs(currentMouseX_screen - zoomDragStartX_screen);
        selectionRectElement.setAttribute('x', x); selectionRectElement.setAttribute('width', width);
    });
    doc.addEventListener('mouseup', (e) => {
        if (!isZoomDragging || !zoomEnabled) return;
        isZoomDragging = false; innerSVGElement.style.cursor = 'crosshair';
        selectionRectElement.style.visibility = 'hidden';
        const rect = innerSVGElement.getBoundingClientRect();
        let zoomDragEndX_screen = e.clientX - rect.left;
        const selScreenMin_abs = Math.min(zoomDragStartX_screen, zoomDragEndX_screen);
        const selScreenMax_abs = Math.max(zoomDragStartX_screen, zoomDragEndX_screen);
        if (selScreenMax_abs - selScreenMin_abs < 10) return;
        const transformStyle = window.getComputedStyle(plotGroup).transform;
        if (!transformStyle || transformStyle === 'none') { console.error("Plot group transform not found for drag-zoom for graph:", graphId); return; }
        const transformMatrix = new DOMMatrix(transformStyle);
        const plotGroup_Tx = transformMatrix.e; const plotGroup_Sx = transformMatrix.a;
        if (Math.abs(plotGroup_Sx) < EPSILON_GENERAL) { console.error("Plot group Sx is near zero for drag-zoom for graph:", graphId); return; }
        const selScreenMin_relative_to_plotArea = selScreenMin_abs - config.PLOT_AREA_X_OFFSET;
        const selScreenMax_relative_to_plotArea = selScreenMax_abs - config.PLOT_AREA_X_OFFSET;
        const newXMin_data_candidate = (selScreenMin_relative_to_plotArea - plotGroup_Tx) / plotGroup_Sx;
        const newXMax_data_candidate = (selScreenMax_relative_to_plotArea - plotGroup_Tx) / plotGroup_Sx;
        currentXMin_data = Math.max(originalXMin_data, Math.min(newXMin_data_candidate, newXMax_data_candidate));
        currentXMax_data = Math.min(originalXMax_data, Math.max(newXMin_data_candidate, newXMax_data_candidate));
        updateGraph();
    });
    innerSVGElement.addEventListener('wheel', (e) => {
        if (!zoomEnabled || miniPaneContainer.contains(e.target)) return;
        let targetElement = e.target; let isOverPlotArea = false;
        while(targetElement && targetElement !== svg) {
            if (targetElement === clippingWrapper || targetElement === plotGroup || targetElement === innerSVGElement) { isOverPlotArea = true; break; }
            targetElement = targetElement.parentNode;
        }
        if (!isOverPlotArea) return;
        e.preventDefault(); const panFactor = 0.1; const zoomFactorWheel = 0.1;
        let currentRange_data = currentXMax_data - currentXMin_data;
        if (currentRange_data <= EPSILON_GENERAL) currentRange_data = MIN_X_RANGE_DATA_UNITS;
        if (e.ctrlKey) {
            const rect = innerSVGElement.getBoundingClientRect(); const mouseX_screen_abs = e.clientX - rect.left;
            const transformStyle = window.getComputedStyle(plotGroup).transform;
            if (!transformStyle || transformStyle === 'none') return;
            const transformMatrix = new DOMMatrix(transformStyle);
            const plotGroup_Tx = transformMatrix.e; const plotGroup_Sx = transformMatrix.a;
            if (Math.abs(plotGroup_Sx) < EPSILON_GENERAL) return;
            const mouseX_screen_relative_to_plotArea = mouseX_screen_abs - config.PLOT_AREA_X_OFFSET;
            const mouseX_data = (mouseX_screen_relative_to_plotArea - plotGroup_Tx) / plotGroup_Sx;
            let newRange_data;
            if (e.deltaY < 0) { newRange_data = currentRange_data / (1 + zoomFactorWheel); }
            else { newRange_data = currentRange_data * (1 + zoomFactorWheel); }
            const mouseRatioInView = (currentRange_data > EPSILON_GENERAL) ? (mouseX_data - currentXMin_data) / currentRange_data : 0.5;
            currentXMin_data = mouseX_data - newRange_data * mouseRatioInView;
            currentXMax_data = currentXMin_data + newRange_data;
        } else {
            const panAmount_data = (e.deltaY > 0 ? 1 : -1) * currentRange_data * panFactor;
            currentXMin_data += panAmount_data; currentXMax_data += panAmount_data;
        }
        updateGraph();
    }, { passive: false });
    let miniPaneInteracting = null;
    let miniPaneInteractionStartX_screen, miniPaneInitialXMin_data, miniPaneInitialXMax_data;
    const startMiniPaneInteraction = (e, type) => {
        if (e.button !== 0 || !zoomEnabled) return;
        e.stopPropagation(); miniPaneInteracting = type;
        miniPaneInteractionStartX_screen = e.clientX;
        miniPaneInitialXMin_data = currentXMin_data; miniPaneInitialXMax_data = currentXMax_data;
        const cursorStyle = (type==='drag') ? 'grabbing' : 'ew-resize';
        if (miniPaneHighlight) miniPaneHighlight.style.cursor = cursorStyle;
        doc.body.style.cursor = cursorStyle;
    };
    if(miniPaneHighlight) miniPaneHighlight.addEventListener('mousedown', (e) => startMiniPaneInteraction(e, 'drag'));
    if(miniPaneLeftResize) miniPaneLeftResize.addEventListener('mousedown', (e) => startMiniPaneInteraction(e, 'resize-left'));
    if(miniPaneRightResize) miniPaneRightResize.addEventListener('mousedown', (e) => startMiniPaneInteraction(e, 'resize-right'));
    doc.addEventListener('mousemove', (e) => {
        if (!miniPaneInteracting || !zoomEnabled) return;
        const deltaX_screen = e.clientX - miniPaneInteractionStartX_screen;
        const miniPaneRect = miniPaneContainer.getBoundingClientRect();
        const totalOriginalDataRange = originalXMax_data - originalXMin_data;
        if (totalOriginalDataRange <= EPSILON_GENERAL || miniPaneRect.width <=0) return;
        const deltaX_data_units = (deltaX_screen / miniPaneRect.width) * totalOriginalDataRange;
        if (miniPaneInteracting === 'drag') {
            const initialRange_data = miniPaneInitialXMax_data - miniPaneInitialXMin_data;
            currentXMin_data = miniPaneInitialXMin_data + deltaX_data_units;
            currentXMax_data = currentXMin_data + initialRange_data;
        } else if (miniPaneInteracting === 'resize-left') {
            currentXMin_data = miniPaneInitialXMin_data + deltaX_data_units;
        } else if (miniPaneInteracting === 'resize-right') {
            currentXMax_data = miniPaneInitialXMax_data + deltaX_data_units;
        }
        updateGraph();
    });
    doc.addEventListener('mouseup', (e) => {
        if (!miniPaneInteracting || !zoomEnabled) return;
        miniPaneInteracting = null;
        if (miniPaneHighlight) miniPaneHighlight.style.cursor = 'grab';
        doc.body.style.cursor = 'default';
    });
    doc.addEventListener('keydown', (e) => {
        if (!zoomEnabled) return;
        if (doc.activeElement && (doc.activeElement.tagName === 'INPUT' || doc.activeElement.tagName === 'TEXTAREA' || doc.activeElement.isContentEditable)) return;
        let isGraphActive = svg.contains(doc.activeElement) || miniPaneContainer.contains(doc.activeElement) || innerSVGElement === doc.activeElement || (doc.activeElement === doc.body && (svg.matches(':hover') || miniPaneContainer.matches(':hover')));
        if (isGraphActive) {
             const panFactorKey = 0.05;
             let currentRange_data = currentXMax_data - currentXMin_data;
             if (currentRange_data <= EPSILON_GENERAL) currentRange_data = MIN_X_RANGE_DATA_UNITS;
             let dx_data = 0;
             if (e.key === 'ArrowLeft') dx_data = -currentRange_data * panFactorKey;
             else if (e.key === 'ArrowRight') dx_data = currentRange_data * panFactorKey;
             else return;
             e.preventDefault();
             currentXMin_data += dx_data; currentXMax_data += dx_data;
             updateGraph();
        }
    });
    if (!innerSVGElement.hasAttribute('tabindex') && zoomEnabled) {
        innerSVGElement.setAttribute('tabindex', '0'); innerSVGElement.style.outline = 'none';
    }
  }
  initializeGraphStylesAndListeners();
  updateGraph();
}

document.addEventListener('DOMContentLoaded', () => {
    // --- NEW: Graph Template Instantiation Logic ---
    const graphTemplate = document.getElementById('graph-template');
    if (graphTemplate) {
        const dataScripts = document.querySelectorAll("script[type='application/json']");
        dataScripts.forEach(script => {
            const graphId = script.id.replace('_data', '');
            const placeholder = document.getElementById(graphId + '_placeholder');
            try {
                const graphData = JSON.parse(script.textContent);
                if (placeholder && graphData) {
                    const graphClone = graphTemplate.content.cloneNode(true);
                    populateGraphFromData(graphClone, graphData);
                    placeholder.appendChild(graphClone);
                    initInteractiveGraph(graphData.graphId, graphData);
                }
            } catch (e) {
                console.error('Failed to parse graph JSON or create graph:', e);
            }
        });
    }

    const PENALTY_FOR_NA = 20.0;
    const heatmapColors = [
        [255,255,255],[255,255,237],[255,255,217],[255,255,195],[255,255,171],[255,255,152],
        [254,253,149],[254,252,145],[254,250,142],[254,249,139],[254,247,136],[254,246,132],
        [254,244,129],[254,242,126],[254,241,123],[254,239,120],[254,238,117],[254,236,113],
        [254,234,110],[255,233,107],[255,231,104],[255,229,102],[255,228,100],[255,226,98],
        [255,224,97],[255,223,95],[255,221,93],[255,219,92],[255,218,90],[255,216,88],
        [255,214,87],[255,213,85],[255,211,84],[255,209,82],[255,208,80],[255,206,79],
        [255,204,77],[255,203,75],[255,201,74],[255,199,72],[255,198,70],[255,196,69],
        [255,194,67],[255,193,65],[255,191,63],[255,189,62],[255,188,60],[255,186,58],
        [255,184,57],[255,182,55],[255,181,53],[255,179,52],[255,177,50],[255,176,48],
        [255,174,46],[255,172,45],[255,170,43],[255,169,41],[255,167,39],[255,165,38],
        [255,163,36],[255,162,34],[255,160,32],[255,158,30],[255,156,29],[255,154,27],
        [255,153,25],[255,151,23],[255,149,21],[255,147,19],[255,145,18],[255,143,16],
        [255,141,14],[255,139,12],[255,137,10],[255,135,8],[255,133,6],[255,131,4],
        [255,130,4],[254,128,5],[253,127,6],[252,126,5],[251,125,5],[250,124,5],
        [249,123,4],[248,122,4],[247,120,4],[246,119,3],[245,118,3],[244,117,3],
        [243,116,3],[242,115,2],[241,114,2],[240,113,2],[239,112,2],[238,111,2],
        [237,110,1],[236,109,1],[235,108,1],[234,107,1],[233,106,1],[232,104,1],
        [231,103,1],[230,102,1],[229,101,0],[228,100,0],[227,99,0],[226,98,0],
        [225,97,0],[224,96,0],[223,95,0],[222,94,0],[221,93,0],[220,92,0],
        [218,91,0],[217,90,0],[216,89,0],[215,88,0],[214,87,0],[213,86,0],
        [212,85,0],[211,84,0],[210,83,0],[209,82,0],[208,81,0],[207,80,0],
        [206,79,0],[205,78,0],[204,77,0],[203,76,0],[202,75,0],[201,74,0],
        [200,73,0],[199,72,0],[198,71,0],[197,70,0],[196,69,1],[195,68,1],
        [194,67,1],[193,66,1],[192,65,1],[191,64,1],[190,63,1],[189,62,1],
        [188,61,1],[187,60,1],[186,59,1],[185,58,1],[183,57,1],[182,56,1],
        [181,55,1],[180,54,1],[179,53,1],[178,52,1],[177,51,1],[176,50,1],
        [175,49,1],[174,48,1],[173,47,1],[172,46,1],[171,45,1],[170,44,1],
        [169,43,1],[168,42,1],[167,41,1],[166,40,1],[164,39,1],[163,38,1],
        [162,37,1],[161,35,1],[160,34,1],[159,33,1],[158,32,1],[157,31,1],
        [156,30,1],[155,29,1],[154,28,1],[153,27,1],[151,26,1],[150,26,2],
        [148,26,3],[147,26,3],[145,25,4],[144,25,4],[143,25,5],[141,24,5],
        [140,24,5],[138,24,6],[137,23,6],[136,23,6],[134,23,7],[133,22,7],
        [131,22,7],[130,22,7],[129,21,8],[127,21,8],[126,20,8],[125,20,8],
        [123,19,8],[122,19,8],[121,19,8],[119,18,8],[118,18,8],[116,18,7],
        [115,17,7],[114,17,7],[112,16,7],[111,16,6],[110,15,6],[108,15,6],
        [107,14,6],[106,14,5],[104,13,5],[103,13,5],[102,12,5],[101,12,4],
        [99,11,4],[98,10,4],[97,10,4],[95,9,3],[94,8,3],[93,8,3],
        [92,7,3],[90,6,2],[89,6,2],[88,5,2],[87,5,2],[85,4,1],
        [84,3,1],[83,3,1],[82,2,1],[80,2,1],[79,1,0],[78,1,0],
        [77,0,0],[75,0,0],[74,0,0],[72,0,0],[71,0,0],[69,0,0],
        [68,0,0],[66,0,0],[65,0,0],[63,0,0],[62,0,0],[60,0,0],
        [59,0,0],[57,0,0],[55,0,0],[54,0,0],[52,0,0],[48,4,1],
        [42,9,4],[36,13,8],[29,16,13],[24,17,16]
    ];
    function recalculateHeatmap() {
        const dataRows = Array.from(document.querySelectorAll('tr[data-benchmark-type]'));
        dataRows.forEach(row => {
            const cells = Array.from(row.querySelectorAll('td[data-original-score]'));
            const minOriginalScore = cells
                .filter(cell => cell.style.display !== 'none')
                .map(cell => {
                    const scoreStr = cell.dataset.originalScore;
                    return scoreStr === 'N/A' ? Infinity : parseFloat(scoreStr);
                })
                .reduce((min, score) => Math.min(min, score), Infinity);
            if (!isFinite(minOriginalScore)) return;
            cells.forEach(cell => {
                if (cell.style.display === 'none') return;
                const originalScoreStr = cell.dataset.originalScore;
                if (originalScoreStr === 'N/A') return;
                const originalScore = parseFloat(originalScoreStr);
                if (isNaN(originalScore)) return;
                const relativePerf = originalScore / minOriginalScore;
                const normalizedForColor = Math.max(0.0, Math.min(1.0, (relativePerf - 1.0) / 9.0));
                const colorIdx = Math.floor(normalizedForColor * 255.0);
                const color = heatmapColors[Math.min(255, colorIdx)];
                const brightness = (color[0]*0.299 + color[1]*0.587 + color[2]*0.114);
                const innerDiv = cell.querySelector('div');
                if (innerDiv) {
                    cell.style.backgroundColor = `rgb(${color[0]}, ${color[1]}, ${color[2]})`;
                    cell.style.color = (brightness > 186) ? "#252526" : "#d4d4d4";
                    const tombstone = innerDiv.querySelector('.tombstone-marker');
                    innerDiv.textContent = relativePerf.toFixed(2);
                    if (tombstone) innerDiv.appendChild(tombstone);
                }
            });
        });
    }
    function recalculateOverallPerformance() {
        const weights = {};
        document.querySelectorAll('input.weight-slider').forEach(slider => {
            weights[slider.dataset.benchmarkType] = parseFloat(slider.value);
        });
        const shimHeaders = Array.from(document.querySelectorAll('th.col-header'));
        const dataRows = Array.from(document.querySelectorAll('tr[data-benchmark-type]'));
        let shimStats = [];
        const visibleShimHeaders = shimHeaders.filter(h => h.style.display !== 'none');
        visibleShimHeaders.forEach((header, index) => {
            const shimSafeId = header.dataset.shimId;
            if (!shimSafeId) return;
            const originalIndex = shimHeaders.findIndex(h => h.dataset.shimId === shimSafeId);
            let totalWeightedRelativeScore = 0;
            let totalWeight = 0;
            dataRows.forEach(row => {
                const benchmarkType = row.dataset.benchmarkType;
                const weightSlider = document.getElementById(`weight_slider_${benchmarkType}`);
                if (!weightSlider) return;
                const weight = parseFloat(weightSlider.value);
                if (weight > 0) {
                    const cell = row.querySelector(`td:nth-child(${originalIndex + 2})`);
                    if (cell && typeof cell.dataset.score !== 'undefined') {
                        const relativeScoreStr = cell.dataset.score;
                        let score = (relativeScoreStr === 'N/A') ? PENALTY_FOR_NA : parseFloat(relativeScoreStr);

                        if (!isNaN(score)) {
                            totalWeightedRelativeScore += score * weight;
                            totalWeight += weight;
                        }
                    }
                }
            });
            shimStats.push({
                shimId: shimSafeId,
                avgPerf: (totalWeight > 0) ? totalWeightedRelativeScore / totalWeight : Infinity
            });
        });
        if (shimStats.length === 0) {
             shimHeaders.forEach(header => {
                const shimSafeId = header.dataset.shimId;
                if(shimSafeId) {
                  document.getElementById(`overall_cell_${shimSafeId}`).textContent = '-';
                  document.getElementById(`rank_span_${shimSafeId}`).textContent = '';
                  document.getElementById(`medal_span_${shimSafeId}`).innerHTML = '';
                }
             });
             return;
        }
        const minAvgPerf = shimStats.reduce((min, s) => Math.min(min, s.avgPerf), Infinity);
        const baseline = (isFinite(minAvgPerf) && minAvgPerf > 0) ? minAvgPerf : 1.0;
        shimStats.forEach(s => {
            s.normalizedAvgPerf = isFinite(s.avgPerf) ? s.avgPerf / baseline : Infinity;
        });
        shimStats.sort((a, b) => {
            if (a.normalizedAvgPerf === b.normalizedAvgPerf) return a.shimId.localeCompare(b.shimId);
            return a.normalizedAvgPerf - b.normalizedAvgPerf;
        });
        let currentRank = 0;
        let lastScore = -1.0;
        const EPSILON = 1e-9;
        shimStats.forEach((s, i) => {
            if (s.normalizedAvgPerf > lastScore + EPSILON) {
                currentRank = i + 1;
            }
            s.rank = currentRank;
            lastScore = s.normalizedAvgPerf;
        });
        const totalVisibleShims = shimStats.length;
        const allShimIds = shimHeaders.map(h => h.dataset.shimId);
        allShimIds.forEach(shimId => {
            const stat = shimStats.find(s => s.shimId === shimId);
            const overallCell = document.getElementById(`overall_cell_${shimId}`);
            const rankSpan = document.getElementById(`rank_span_${shimId}`);
            const medalSpan = document.getElementById(`medal_span_${shimId}`);
            if (stat) {
                if (isFinite(stat.normalizedAvgPerf)) {
                    overallCell.textContent = stat.normalizedAvgPerf.toFixed(2) + 'x';
                    const perf = stat.normalizedAvgPerf;
                    let color;
                    if (perf <= 1.005) {
                        color = heatmapColors[0];
                    } else {
                        const normalizedForColor = Math.max(0.0, Math.min(1.0, (perf - 1.0) / 9.0));
                        const colorIdx = Math.floor(normalizedForColor * 255.0);
                        color = heatmapColors[Math.min(255, colorIdx)];
                    }
                    const brightness = (color[0]*0.299 + color[1]*0.587 + color[2]*0.114);
                    overallCell.style.backgroundColor = `rgb(${color[0]}, ${color[1]}, ${color[2]})`;
                    overallCell.style.color = (brightness > 186) ? "#252526" : "#d4d4d4";
                } else {
                    overallCell.textContent = 'N/A';
                    overallCell.style.backgroundColor = 'rgb(60,60,60)';
                    overallCell.style.color = '#a0a0a0';
                }
                rankSpan.textContent = ` (Rank: ${stat.rank}/${totalVisibleShims})`;
                if (stat.rank === 1) medalSpan.innerHTML = "🥇";
                else if (stat.rank === 2) medalSpan.innerHTML = "🥈";
                else if (stat.rank === 3) medalSpan.innerHTML = "🥉";
                else medalSpan.innerHTML = "";
            } else {
                 overallCell.textContent = '-';
                 overallCell.style.backgroundColor = '';
                 overallCell.style.color = '';
                 rankSpan.textContent = '';
                 medalSpan.innerHTML = '';
            }
        });
    }
    const initGlobalToggles = () => {
        document.querySelectorAll('input.global-shim-toggle').forEach(toggle => {
            toggle.addEventListener('change', event => {
                const shimSafeId = event.target.dataset.shimId;
                const isChecked = event.target.checked;
                if (!shimSafeId) return;
                document.querySelectorAll(`.shim-column-${shimSafeId}`).forEach(cell => {
                    cell.style.display = isChecked ? '' : 'none';
                });
                const localCheckboxes = document.querySelectorAll(`input.shim-toggle-checkbox[id^="Plot_${shimSafeId}_"]`);
                localCheckboxes.forEach(localCheckbox => {
                    if (localCheckbox.checked !== isChecked) {
                        localCheckbox.checked = isChecked;
                        localCheckbox.dispatchEvent(new Event('change', { bubbles: true }));
                    }
                });
                recalculateHeatmap();
                recalculateOverallPerformance();
            });
        });
    };
    const initGlobalLogToggle = () => {
        const globalLogToggle = document.getElementById('global_log_scale_toggle');
        if (!globalLogToggle) return;
        globalLogToggle.addEventListener('change', event => {
            const isChecked = event.target.checked;
            document.querySelectorAll('input.log-scale-toggle').forEach(localToggle => {
                if (localToggle.checked !== isChecked) {
                    localToggle.checked = isChecked;
                    localToggle.dispatchEvent(new Event('change', { bubbles: true }));
                }
            });
        });
    };
    const initTitleHighlighting = () => {
        document.body.addEventListener('click', function(event) {
            const link = event.target.closest('.graph-title-link, .heatmap-bp-bm-link');
            if (!link) return;
            setTimeout(() => {
                let targetId;
                if (link.dataset.highlightTargetId) {
                    targetId = link.dataset.highlightTargetId;
                } else {
                    const href = link.getAttribute('href');
                    if (!href || !href.startsWith('#')) return;
                    targetId = href.substring(1);
                }
                if (!targetId) return;
                let elementToHighlight = document.getElementById(targetId);
                if (!elementToHighlight) return;
                if (elementToHighlight.tagName.toLowerCase() === 'svg' && elementToHighlight.dataset.isBenchmarkGraph) {
                    const titleElement = elementToHighlight.querySelector('.heading');
                    if (titleElement) {
                        elementToHighlight = titleElement;
                    }
                }
                elementToHighlight.classList.remove('highlight-fade');
                void elementToHighlight.offsetWidth;
                elementToHighlight.classList.add('highlight-fade');
                elementToHighlight.addEventListener('animationend', () => {
                    elementToHighlight.classList.remove('highlight-fade');
                }, { once: true });
            }, 0);
        });
    };
    document.querySelectorAll('input.weight-slider').forEach(slider => {
        slider.addEventListener('input', () => {
            document.getElementById(`weight_value_${slider.dataset.benchmarkType}`).textContent = parseFloat(slider.value).toFixed(2);
            recalculateOverallPerformance();
        });
    });
    initGlobalToggles();
    initGlobalLogToggle();
    initTitleHighlighting();
    recalculateHeatmap();
    recalculateOverallPerformance();
});
// ]]>
</script>)delimiter";
    }

    /**
     * @brief Writes the initial part of the HTML document, including head, global CSS, and start of body.
     * @param file The output filestream.
     * @param report_timestamp The timestamp for the report.
     */
    inline void write_html_doc_start_and_head(std::ofstream& file, const std::string& report_timestamp) {
        file << "<!doctype html>\n"
             << "<html>\n"
             << "<head>\n"
             << "<meta charset='utf-8'/>\n"
             << "<title>Performance Benchmark Results</title>\n"
             << "<meta name='copyright' content='Copyright (c) 2025-Present Gradient Dynamics LLC, Copyright (c) 2025-Present Nima Mehrani. All rights reserved.'/>\n"
             << "<meta name='author' content='Gradient Dynamics LLC, Nima Mehrani'/>\n";

        write_html_global_css(file); // Embed all CSS

        file << "</head>\n"
             << "<body>\n"
             << "<div class='container'>\n"
             << "<h1>Hash Table Performance Report</h1>\n"
             << "<p style='text-align:center; font-size:11px; color: #a0a0a0; margin-top:-10px; margin-bottom:20px;'>"
             << DISCARDED_RUNS_COUNT << " discards).<br/>"
             << " outer repetition(s) of the base metric."
             << "</p>\n"
             << "<div class='settings'>"
             << "<strong>KEY_COUNT:</strong> " << KEY_COUNT << "<br>\n"
             << "<strong>KEY_COUNT_MEASUREMENT_INTERVAL:</strong> " << KEY_COUNT_MEASUREMENT_INTERVAL << "<br>\n"
             << "<strong>MAX_LOAD_FACTOR:</strong> " << MAX_LOAD_FACTOR << "<br>\n"
             << "<strong>DISCARDED_RUNS_COUNT (from inner trials):</strong> " << DISCARDED_RUNS_COUNT << "<br>\n"
             << "<strong>APPROXIMATE_CACHE_SIZE (for flush):</strong> " << APPROXIMATE_CACHE_SIZE << "<br>\n"
             << "<strong>MILLISECOND_COOLDOWN_BETWEEN_BENCHMARKS (per task):</strong> " << MILLISECOND_COOLDOWN_BETWEEN_BENCHMARKS << "<br>\n";

        file << "<strong>Report Timestamp:</strong> " << report_timestamp << "<br>\n"
             << "</div>\n";
    }

    /**
     * @brief Classifies a benchmark ID into a string type (e.g., "insert", "erase").
     * @param bmid The benchmark ID to classify.
     * @param types_set An optional pointer to a set; if not null, the found type is inserted.
     * @return The string representation of the benchmark type.
     */
        inline std::string classify_benchmark_and_update_set(benchmark_ids bmid, std::set<std::string>* types_set = nullptr) {
            if (bmid >= benchmark_id_count) {
                return "unknown";
            }

            std::string bm_name_str(benchmark_names[bmid]);
            std::transform(bm_name_str.begin(), bm_name_str.end(), bm_name_str.begin(),
                           [](unsigned char c) { return std::tolower(c); });

            std::string bm_type = "unknown";
            if (bm_name_str.find("insert") != std::string::npos)           bm_type = "insert";
            else if (bm_name_str.find("erase") != std::string::npos)       bm_type = "erase";
            else if (bm_name_str.find("replace") != std::string::npos)     bm_type = "replace";
            else if (bm_name_str.find("lookup") != std::string::npos)      bm_type = "lookup";
            else if (bm_name_str.find("iterat") != std::string::npos)      bm_type = "iterate";

            if (types_set && bm_type != "unknown") {
                types_set->insert(bm_type);
            }

            return bm_type;
        }

    /**
     * @brief Writes the end of the HTML document, including the global controls widget and JavaScript.
     * @param file The output filestream.
     */
    inline void write_html_doc_end(std::ofstream& file) {
        file << "</div>\n"; // End .container

        // --- Pinned Global Shim Visibility Controls Widget ---
        std::vector<std::string> ordered_shim_labels_for_html = g_all_shim_labels;
        if(!ordered_shim_labels_for_html.empty()) {
            std::sort(ordered_shim_labels_for_html.begin(), ordered_shim_labels_for_html.end());

            std::map<std::string, RegisteredShimInfo> shim_info_map;
            for (const auto& si : g_registered_shims_info) {
                shim_info_map[si.label] = si;
            }

            file << "<div id='global-shim-controls'>\n"
                 << "  <div id='global-shim-controls-title'>Global Settings</div>\n"
                 << "  <div id='global-shim-toggles-container'>\n";

            for (const auto& shim_label : ordered_shim_labels_for_html) {
                std::string safe_label = shim_label;
                std::ranges::replace_if(safe_label, [](const char c){return !std::isalnum(c);}, '_');
                std::string shim_color = "#cccccc"; // Default color
                if (shim_info_map.contains(shim_label)) {
                    shim_color = shim_info_map.at(shim_label).color;
                }

                file << "    <span style='--shim-color:" << shim_color << ";'>"
                     << "<input id='global_toggle_" << safe_label << "' class='global-shim-toggle shim-toggle-checkbox' data-shim-id='" << safe_label << "' type='checkbox' checked/>"
                     << "<label for='global_toggle_" << safe_label << "'>" << shim_label << "</label>"
                     << "</span>\n";
            }

            file << "<span class='log-scale-toggle'>"
                 << "<input id='global_log_scale_toggle' class='log-scale-toggle' type='checkbox'/>"
                 << "<label for='global_log_scale_toggle'>Log Y-Axis</label>"
                 << "</span>\n";

            file << "  </div>\n"; // End #global-shim-toggles-container

            // --- Conditionally generate weight sliders based on executed benchmarks ---
            std::set<std::string> found_benchmark_types;
            for (benchmark_ids bmid : g_enabled_benchmark_ids_list) {
                // The new function handles classification and updates the set
                classify_benchmark_and_update_set(bmid, &found_benchmark_types);
            }

            // Only create the weights container if at least one relevant benchmark type was found.
            if (!found_benchmark_types.empty()) {
                file << "<div id='global-weight-toggles-container'>\n"
                     << "<span>Weights: </span>";

                if (found_benchmark_types.count("insert")) {
                    file << "<span><label for='weight_slider_insert'>Insert</label><input id='weight_slider_insert' class='weight-slider' data-benchmark-type='insert' type='range' min='0' max='2' step='0.25' value='1.0'><span class='weight-value' id='weight_value_insert'>1.00</span></span>\n";
                }
                if (found_benchmark_types.count("erase")) {
                    file << "<span><label for='weight_slider_erase'>Erase</label><input id='weight_slider_erase' class='weight-slider' data-benchmark-type='erase' type='range' min='0' max='2' step='0.25' value='1.0'><span class='weight-value' id='weight_value_erase'>1.00</span></span>\n";
                }
                if (found_benchmark_types.count("replace")) {
                    file << "<span><label for='weight_slider_replace'>Replace</label><input id='weight_slider_replace' class='weight-slider' data-benchmark-type='replace' type='range' min='0' max='2' step='0.25' value='1.0'><span class='weight-value' id='weight_value_replace'>1.00</span></span>\n";
                }
                if (found_benchmark_types.count("lookup")) {
                    file << "<span><label for='weight_slider_lookup'>Lookup</label><input id='weight_slider_lookup' class='weight-slider' data-benchmark-type='lookup' type='range' min='0' max='2' step='0.25' value='1.0'><span class='weight-value' id='weight_value_lookup'>1.00</span></span>\n";
                }
                if (found_benchmark_types.count("iterate")) {
                    file << "<span><label for='weight_slider_iterate'>Iterate</label><input id='weight_slider_iterate' class='weight-slider' data-benchmark-type='iterate' type='range' min='0' max='2' step='0.25' value='0.25'><span class='weight-value' id='weight_value_iterate'>0.25</span></span>\n";
                }

                file << "</div>\n"; // End #global-weight-toggles-container
            }

            file << "</div>\n"; // End #global-shim-controls
        }

        write_html_global_javascript(file); // Embed all JavaScript

        file << "\n</body>\n</html>\n";
    }


    /**
     * @brief Generates the main HTML performance report. (Orchestrator function)
     * @param file_id_timestamp A timestamp string used to create a unique filename.
     */
    inline void benchmark_html_out(const std::string& file_id_timestamp) {
        // 1. Define the output directory and ensure it exists.
        std::filesystem::path output_dir = BENCHMARK_OUTPUT_DIRECTORY;
        try {
            if (!std::filesystem::exists(output_dir)) {
                std::filesystem::create_directories(output_dir);
                std::cout << "Created output directory: " << output_dir.string() << std::endl;
            } else if (!std::filesystem::is_directory(output_dir)) {
                std::cerr << "Error: Path '" << output_dir.string() << "' exists but is not a directory. Cannot create HTML report there." << std::endl;
                return;
            }
        } catch (const std::filesystem::filesystem_error& e) {
            std::cerr << "Error creating output directory '" << output_dir.string() << "': " << e.what() << std::endl;
            return;
        }

        // 2. Construct the full file path.
        std::filesystem::path filename_path = output_dir / ("benchmark_results_" + file_id_timestamp + ".html");
        std::string filename_str = filename_path.string();

        std::ofstream file(filename_str);
        if (!file.is_open()) {
            std::cerr << "Error: Could not open file '" << filename_str << "' for writing HTML report." << std::endl;
            return;
        }
        std::cout << "Generating HTML report: " << filename_str << std::endl;

        // --- Call helper to write HTML start, head, global CSS, and body start ---
        write_html_doc_start_and_head(file, file_id_timestamp);

        // --- Heatmap Section ---
        std::vector<std::string> ordered_shim_labels_for_html = g_all_shim_labels;
        if(!ordered_shim_labels_for_html.empty()) std::sort(ordered_shim_labels_for_html.begin(), ordered_shim_labels_for_html.end());

        performance_heatmap_out(file, ordered_shim_labels_for_html);

        // --- Graphs Section ---
        if (!g_final_median_point_averages.empty() && !g_final_max_point_values.empty()) {
            file << "<div class='graph-container'>\n";
            file << "<h2>Detailed Performance Graphs</h2>\n"
                 << "<p style='text-align:center; font-size:11px; color: #a0a0a0;'>"
                 << "Graphs show time (Y-axis) vs. number of keys (X-axis). "
                 << "Each line represents a shim. Lower is better. "
                 << "Y-axis scales dynamically per graph based on the shims shown.<br>"
                 << "Click on a graph title to return to the heatmap overview."
                 << "</p>\n";

            // --- NEW: Graph Template Definition ---
            file << R"delimiter(<template id="graph-template">
<svg xmlns='http://www.w3.org/2000/svg' xmlns:xlink='http://www.w3.org/1999/xlink' width='1002px' viewBox='0 0 1002 688' style='background-color: #2d2d30; border: 1px solid #3e3e42; margin-bottom: 15px;'>
    <foreignObject x='0' y='0' width='100%' height='100%'>
        <div xmlns='http://www.w3.org/1999/xhtml' class='outerDiv'>
            <div class='heading'>
                <a class='graph-title-link' href='#heatmap_top_anchor' title='Go to Heatmap Overview'>
                    <span class='graph-title-text'></span> <span class='link-emoji'>🗓️</span>
                </a>
            </div>
            <div class='verticalSpacer'></div>
            <div class='shim-toggles-container'></div>
            <div class='verticalSpacer'></div>
            <svg xmlns='http://www.w3.org/2000/svg' class='innerSVG' preserveAspectRatio='xMinYMin meet'>
                <line class='axis y-axis-line' />
                <line class='axis x-axis-line' />
                <text class='axisLabel y-axis-label' alignment-baseline='middle' text-anchor='middle' transform='rotate(-90,6,270)'>Time (ns) ⟶</text>
                <text class='axisLabel x-axis-label' alignment-baseline='middle' text-anchor='middle'>N keys</text>
                <g class='x-axis-ticks'></g>
                <defs><clipPath><rect /></clipPath></defs>
                <g class='clipping-wrapper' style='clip-path: none;'>
                    <g class='plotGroup'></g>
                </g>
                <rect class='selectionRect' x='0' y='0' width='0' height='0' style='visibility:hidden;' />
            </svg>
            <div class='verticalSpacer'></div>
            <div class='graph-controls-flex-container'>
                <span class="log-scale-control-placeholder"></span>
                <button class='graph-button reset-zoom-button'>Reset Zoom</button>
            </div>
            <div class='verticalSpacer'></div>
            <div class='mini-pane-container'>
                <div class='mini-pane-highlight'>
                    <div class='mini-pane-resize-handle left'></div>
                    <div class='mini-pane-resize-handle right'></div>
                </div>
            </div>
        </div>
    </foreignObject>
</svg>
</template>
)delimiter";
            // --- End of Graph Template ---

            std::map<benchmark_ids, int> benchmark_order_priority_for_graphs;
            for (size_t i = 0; i < g_display_order_of_benchmarks.size(); ++i) {
                if (g_display_order_of_benchmarks[i] < benchmark_id_count) {
                    benchmark_order_priority_for_graphs[g_display_order_of_benchmarks[i]] = static_cast<int>(i);
                }
            }
            std::vector<benchmark_ids> sorted_benchmarks_to_graph = g_enabled_benchmark_ids_list;
            std::sort(sorted_benchmarks_to_graph.begin(), sorted_benchmarks_to_graph.end(),
                [&](benchmark_ids a, benchmark_ids b) {
                    bool a_in_display_order = benchmark_order_priority_for_graphs.contains(a);
                    bool b_in_display_order = benchmark_order_priority_for_graphs.contains(b);
                    if (a_in_display_order && b_in_display_order) return benchmark_order_priority_for_graphs[a] < benchmark_order_priority_for_graphs[b];
                    else if (a_in_display_order) return true;
                    else if (b_in_display_order) return false;
                    else return static_cast<int>(a) < static_cast<int>(b);
                });

            for (benchmark_ids bmid_enum_val : sorted_benchmarks_to_graph) {
                if (bmid_enum_val >= benchmark_id_count) continue;
                bool data_exists_for_bmid = false;
                for (const auto& bp_info_check : g_registered_blueprints_info) {
                     for (const auto& shim_info_check : g_registered_shims_info) {
                        ArenaKey key_check = {bp_info_check.label, shim_info_check.label, bmid_enum_val};
                        if (g_final_median_point_averages.contains(key_check) && !g_final_median_point_averages.at(key_check).empty()) {
                            data_exists_for_bmid = true;
                            break;
                        }
                    }
                    if (data_exists_for_bmid) break;
                }

                if(data_exists_for_bmid) {
                    auto graph_caller_lambda =
                        []<benchmark_ids DispatchedBMID>
                        (auto& file_arg, auto& medians_arg, auto& max_vals_arg) {
                            BenchmarkOutput::graphs_out_dispatch_for_bmid<DispatchedBMID>(file_arg, medians_arg, max_vals_arg);
                        };
                    bool dispatched = BenchmarkBMIDDispatch::dispatch_by_bmid_generic_lambda_target(
                        bmid_enum_val, graph_caller_lambda, file, g_final_median_point_averages, g_final_max_point_values
                    );
                    if (!dispatched) {
                        std::cerr << "Warning: Graphs not generated for benchmark_id "
                                  << static_cast<int>(bmid_enum_val)
                                  << " in benchmark_html_out (dispatch failed or BMID disabled)." << std::endl;
                    }
                }
            }
            file << "</div>\n"; // End .graph-container
        } else {
            file << "<p>No detailed point data available for graphs.</p>\n";
        }

        // --- Call helper to write global controls, JS, and close HTML document ---
        write_html_doc_end(file);

        file.close();
    }

    static const uint8_t heatmap_colors [ 256 ][ 3 ] =
  {
    { 255, 255, 255 }, { 255, 255, 237 }, { 255, 255, 217 }, { 255, 255, 195 }, { 255, 255, 171 }, { 255, 255, 152 },
    { 254, 253, 149 }, { 254, 252, 145 }, { 254, 250, 142 }, { 254, 249, 139 }, { 254, 247, 136 }, { 254, 246, 132 },
    { 254, 244, 129 }, { 254, 242, 126 }, { 254, 241, 123 }, { 254, 239, 120 }, { 254, 238, 117 }, { 254, 236, 113 },
    { 254, 234, 110 }, { 255, 233, 107 }, { 255, 231, 104 }, { 255, 229, 102 }, { 255, 228, 100 }, { 255, 226, 98 },
    { 255, 224, 97 }, { 255, 223, 95 }, { 255, 221, 93 }, { 255, 219, 92 }, { 255, 218, 90 }, { 255, 216, 88 },
    { 255, 214, 87 }, { 255, 213, 85 }, { 255, 211, 84 }, { 255, 209, 82 }, { 255, 208, 80 }, { 255, 206, 79 },
    { 255, 204, 77 }, { 255, 203, 75 }, { 255, 201, 74 }, { 255, 199, 72 }, { 255, 198, 70 }, { 255, 196, 69 },
    { 255, 194, 67 }, { 255, 193, 65 }, { 255, 191, 63 }, { 255, 189, 62 }, { 255, 188, 60 }, { 255, 186, 58 },
    { 255, 184, 57 }, { 255, 182, 55 }, { 255, 181, 53 }, { 255, 179, 52 }, { 255, 177, 50 }, { 255, 176, 48 },
    { 255, 174, 46 }, { 255, 172, 45 }, { 255, 170, 43 }, { 255, 169, 41 }, { 255, 167, 39 }, { 255, 165, 38 },
    { 255, 163, 36 }, { 255, 162, 34 }, { 255, 160, 32 }, { 255, 158, 30 }, { 255, 156, 29 }, { 255, 154, 27 },
    { 255, 153, 25 }, { 255, 151, 23 }, { 255, 149, 21 }, { 255, 147, 19 }, { 255, 145, 18 }, { 255, 143, 16 },
    { 255, 141, 14 }, { 255, 139, 12 }, { 255, 137, 10 }, { 255, 135, 8 }, { 255, 133, 6 }, { 255, 131, 4 },
    { 255, 130, 4 }, { 254, 128, 5 }, { 253, 127, 6 }, { 252, 126, 5 }, { 251, 125, 5 }, { 250, 124, 5 },
    { 249, 123, 4 }, { 248, 122, 4 }, { 247, 120, 4 }, { 246, 119, 3 }, { 245, 118, 3 }, { 244, 117, 3 },
    { 243, 116, 3 }, { 242, 115, 2 }, { 241, 114, 2 }, { 240, 113, 2 }, { 239, 112, 2 }, { 238, 111, 2 },
    { 237, 110, 1 }, { 236, 109, 1 }, { 235, 108, 1 }, { 234, 107, 1 }, { 233, 106, 1 }, { 232, 104, 1 },
    { 231, 103, 1 }, { 230, 102, 1 }, { 229, 101, 0 }, { 228, 100, 0 }, { 227, 99, 0 }, { 226, 98, 0 },
    { 225, 97, 0 }, { 224, 96, 0 }, { 223, 95, 0 }, { 222, 94, 0 }, { 221, 93, 0 }, { 220, 92, 0 },
    { 218, 91, 0 }, { 217, 90, 0 }, { 216, 89, 0 }, { 215, 88, 0 }, { 214, 87, 0 }, { 213, 86, 0 },
    { 212, 85, 0 }, { 211, 84, 0 }, { 210, 83, 0 }, { 209, 82, 0 }, { 208, 81, 0 }, { 207, 80, 0 },
    { 206, 79, 0 }, { 205, 78, 0 }, { 204, 77, 0 }, { 203, 76, 0 }, { 202, 75, 0 }, { 201, 74, 0 },
    { 200, 73, 0 }, { 199, 72, 0 }, { 198, 71, 0 }, { 197, 70, 0 }, { 196, 69, 1 }, { 195, 68, 1 },
    { 194, 67, 1 }, { 193, 66, 1 }, { 192, 65, 1 }, { 191, 64, 1 }, { 190, 63, 1 }, { 189, 62, 1 },
    { 188, 61, 1 }, { 187, 60, 1 }, { 186, 59, 1 }, { 185, 58, 1 }, { 183, 57, 1 }, { 182, 56, 1 },
    { 181, 55, 1 }, { 180, 54, 1 }, { 179, 53, 1 }, { 178, 52, 1 }, { 177, 51, 1 }, { 176, 50, 1 },
    { 175, 49, 1 }, { 174, 48, 1 }, { 173, 47, 1 }, { 172, 46, 1 }, { 171, 45, 1 }, { 170, 44, 1 },
    { 169, 43, 1 }, { 168, 42, 1 }, { 167, 41, 1 }, { 166, 40, 1 }, { 164, 39, 1 }, { 163, 38, 1 },
    { 162, 37, 1 }, { 161, 35, 1 }, { 160, 34, 1 }, { 159, 33, 1 }, { 158, 32, 1 }, { 157, 31, 1 },
    { 156, 30, 1 }, { 155, 29, 1 }, { 154, 28, 1 }, { 153, 27, 1 }, { 151, 26, 1 }, { 150, 26, 2 },
    { 148, 26, 3 }, { 147, 26, 3 }, { 145, 25, 4 }, { 144, 25, 4 }, { 143, 25, 5 }, { 141, 24, 5 },
    { 140, 24, 5 }, { 138, 24, 6 }, { 137, 23, 6 }, { 136, 23, 6 }, { 134, 23, 7 }, { 133, 22, 7 },
    { 131, 22, 7 }, { 130, 22, 7 }, { 129, 21, 8 }, { 127, 21, 8 }, { 126, 20, 8 }, { 125, 20, 8 },
    { 123, 19, 8 }, { 122, 19, 8 }, { 121, 19, 8 }, { 119, 18, 8 }, { 118, 18, 8 }, { 116, 18, 7 },
    { 115, 17, 7 }, { 114, 17, 7 }, { 112, 16, 7 }, { 111, 16, 6 }, { 110, 15, 6 }, { 108, 15, 6 },
    { 107, 14, 6 }, { 106, 14, 5 }, { 104, 13, 5 }, { 103, 13, 5 }, { 102, 12, 5 }, { 101, 12, 4 },
    { 99, 11, 4 }, { 98, 10, 4 }, { 97, 10, 4 }, { 95, 9, 3 }, { 94, 8, 3 }, { 93, 8, 3 },
    { 92, 7, 3 }, { 90, 6, 2 }, { 89, 6, 2 }, { 88, 5, 2 }, { 87, 5, 2 }, { 85, 4, 1 },
    { 84, 3, 1 }, { 83, 3, 1 }, { 82, 2, 1 }, { 80, 2, 1 }, { 79, 1, 0 }, { 78, 1, 0 },
    { 77, 0, 0 }, { 75, 0, 0 }, { 74, 0, 0 }, { 72, 0, 0 }, { 71, 0, 0 }, { 69, 0, 0 },
    { 68, 0, 0 }, { 66, 0, 0 }, { 65, 0, 0 }, { 63, 0, 0 }, { 62, 0, 0 }, { 60, 0, 0 },
    { 59, 0, 0 }, { 57, 0, 0 }, { 55, 0, 0 }, { 54, 0, 0 }, { 52, 0, 0 }, { 48, 4, 1 },
    { 42, 9, 4 }, { 36, 13, 8 }, { 29, 16, 13 }, { 24, 17, 16 }
  };


    /**
     * @brief Generates the performance heatmap table for the HTML report.
     * @param file The output filestream for the HTML report.
     * @param ordered_shim_labels_for_columns A sorted list of shim labels to determine column order.
     */
    inline void performance_heatmap_out(std::ofstream& file, const std::vector<std::string>& ordered_shim_labels_for_columns) {
        file << std::fixed << std::setprecision(2); // Set precision for the whole table output.

        constexpr double RELATIVE_PERF_NA_MARKER = -1.0;
        // This C++-side penalty is now just for the initial render. JS has its own constant.
        constexpr double PENALTY_FOR_NA_TEST_IN_OVERALL_AVG = 20.0;

        // Determine the display order of benchmarks for rows based on g_display_order_of_benchmarks.
        std::map<benchmark_ids, int> benchmark_order_priority;
        for (size_t i = 0; i < g_display_order_of_benchmarks.size(); ++i) {
             if (g_display_order_of_benchmarks[i] < benchmark_id_count) {
                benchmark_order_priority[g_display_order_of_benchmarks[i]] = static_cast<int>(i);
            }
        }
        std::vector<benchmark_ids> sorted_benchmarks_for_rows = g_enabled_benchmark_ids_list;
        std::sort(sorted_benchmarks_for_rows.begin(), sorted_benchmarks_for_rows.end(),
            [&](benchmark_ids a, benchmark_ids b) {
                const bool a_in_display_order = benchmark_order_priority.contains(a);
                const bool b_in_display_order = benchmark_order_priority.contains(b);
                if (a_in_display_order && b_in_display_order) return benchmark_order_priority.at(a) < benchmark_order_priority.at(b);
                if (a_in_display_order) return true;
                if (b_in_display_order) return false;
                return static_cast<int>(a) < static_cast<int>(b);
            });

        // Sort blueprints alphabetically for consistent row order.
        // MODIFIED: Used g_registered_blueprints_info instead of g_all_rnt_blueprints
        std::vector<BlueprintRuntimeInfo> sorted_blueprints_for_rows = g_registered_blueprints_info;
        std::sort(sorted_blueprints_for_rows.begin(), sorted_blueprints_for_rows.end(),
                  [](const BlueprintRuntimeInfo& a, const BlueprintRuntimeInfo& b){
            return a.label < b.label;
        });

        // Pass 1: Calculate all per-test `relative_perf` scores.
        std::map<ArenaKey, double> per_test_relative_perf_scores;
        for (benchmark_ids bmid : sorted_benchmarks_for_rows) {
            if (bmid >= benchmark_id_count) continue;
            for (const auto& bp_info : sorted_blueprints_for_rows) {
                double lowest_valid_time_for_row = -1.0;
                for (const auto& shim_label : ordered_shim_labels_for_columns) {
                    ArenaKey key = std::make_tuple(bp_info.label, shim_label, bmid);
                    auto it = g_final_median_performance_times.find(key);
                    if (it != g_final_median_performance_times.end() && it->second > 0) {
                        if (lowest_valid_time_for_row < 0 || it->second < lowest_valid_time_for_row) {
                            lowest_valid_time_for_row = it->second;
                        }
                    }
                }
                if (lowest_valid_time_for_row <= 0) lowest_valid_time_for_row = 1.0;

                for (const auto& shim_label : ordered_shim_labels_for_columns) {
                    ArenaKey key = std::make_tuple(bp_info.label, shim_label, bmid);
                    auto it = g_final_median_performance_times.find(key);
                    if (it != g_final_median_performance_times.end() && it->second >= 0) {
                        per_test_relative_perf_scores[key] = it->second / lowest_valid_time_for_row;
                    } else {
                        per_test_relative_perf_scores[key] = RELATIVE_PERF_NA_MARKER;
                    }
                }
            }
        }

        // --- HTML Output: Header and Description ---
        file << "<div id='heatmap_top_anchor'></div>\n";
        file << "<h2>Performance Heatmap</h2>\n"
             << "<p style='text-align:center; font-size:11px; color: #a0a0a0;'>"
             << "Cell values are final aggregated execution times relative to the fastest shim for that specific Blueprint/Benchmark row (lower/lighter is better, 1.00x is fastest).<br/>"
             << "Hover over a cell to see the absolute median time in nanoseconds. "
             << "Click on a 'Blueprint: Benchmark <span class='link-emoji'>📈</span>' entry to navigate to its detailed graph.<br/>"
             << "Column headers for shims include an overall rank. This rank is calculated dynamically based on the weights selected in the Global Settings panel below.<br/>"
             << "The 'Overall Relative Performance' row normalizes scores so the best shim is 1.00x. N/A results are penalized (treated as "
             << PENALTY_FOR_NA_TEST_IN_OVERALL_AVG << "x slower) when calculating this overall metric.<br/>"
             << "<span class='tombstone-marker' title='This shim is known to use a tombstone-like mechanism, which affects delete/erase operations.'>✝</span> indicates a shim known to use a tombstone-like mechanism (applies to erase benchmarks only).</p>\n";

        // --- HTML Output: Table Headers ---
        file << "<table id='heatmap-table'>\n<thead>\n<tr><th class='row-header-th'>Blueprint: Benchmark</th>";
        for (const auto& shim_label : ordered_shim_labels_for_columns) {
            std::string safe_label = shim_label;
            std::ranges::replace_if(safe_label, [](const char c){return !std::isalnum(c);}, '_');
            file << "<th class='col-header shim-column-" << safe_label << "' data-shim-id='" << safe_label << "'><div>" << shim_label
                 << "<br/><span class='rank-span' id='rank_span_" << safe_label << "'></span>"
                 << "<br/><span class='medal-span' id='medal_span_" << safe_label << "'></span>"
                 << "</div></th>";
        }
        file << "</tr>\n</thead>\n<tbody>\n";

        // --- HTML Output: Table Body (Data Cells) ---
        for (benchmark_ids bmid : sorted_benchmarks_for_rows) {
            if (bmid >= benchmark_id_count) continue;

            std::string bm_type = classify_benchmark_and_update_set(bmid);

            for (const auto& bp_info : sorted_blueprints_for_rows) {
                std::string safe_bp_label_for_id = bp_info.label;
                std::replace_if(safe_bp_label_for_id.begin(), safe_bp_label_for_id.end(), [](char c){return !std::isalnum(c);}, '_');
                std::string target_graph_anchor_id = "graph_" + safe_bp_label_for_id + "_" + std::to_string(static_cast<int>(bmid));
                std::string heatmap_row_anchor_id = "heatmap_row_" + safe_bp_label_for_id + "_" + std::to_string(static_cast<int>(bmid));

                file << "<tr id='" << heatmap_row_anchor_id << "' data-benchmark-type='" << bm_type << "'><td class='row-header row-header-link-cell'>";
                file << "<a class='heatmap-bp-bm-link' href='#" << target_graph_anchor_id
                     << "' title='View graph for " << bp_info.label << " - " << benchmark_names[bmid] << "'>";
                file << bp_info.label << ":<br>" << benchmark_names[bmid];
                file << " <span class='link-emoji'>📈</span>";
                file << "</a></td>";

                for (const auto& shim_label : ordered_shim_labels_for_columns) {
                    std::string safe_label = shim_label;
                    std::ranges::replace_if(safe_label, [](const char c){return !std::isalnum(c);}, '_');
                    ArenaKey key = std::make_tuple(bp_info.label, shim_label, bmid);

                    auto it = g_final_median_performance_times.find(key);
                    double original_score = (it != g_final_median_performance_times.end() && it->second >= 0) ? it->second : RELATIVE_PERF_NA_MARKER;
                    double cell_relative_perf = per_test_relative_perf_scores.count(key) ? per_test_relative_perf_scores.at(key) : RELATIVE_PERF_NA_MARKER;

                    std::string score_str = (cell_relative_perf == RELATIVE_PERF_NA_MARKER) ? "N/A" : std::to_string(cell_relative_perf);
                    std::string original_score_str = (original_score == RELATIVE_PERF_NA_MARKER) ? "N/A" : std::to_string(original_score);

                    if (cell_relative_perf != RELATIVE_PERF_NA_MARKER) {
                        double normalized_for_color = std::max(0.0, std::min(1.0, (cell_relative_perf - 1.0) / 9.0));
                        uint8_t color_idx = static_cast<uint8_t>(normalized_for_color * 255.0);

                        double brightness = (heatmap_colors[color_idx][0]*0.299 + heatmap_colors[color_idx][1]*0.587 + heatmap_colors[color_idx][2]*0.114);
                        std::string text_color = (brightness > 186) ? "#252526" : "#d4d4d4";

                        file << "<td class='shim-column-" << safe_label << "' data-score='" << score_str << "' data-original-score='" << original_score_str << "' style='background-color: rgb(" << static_cast<unsigned int>(heatmap_colors[color_idx][0]) << "," << static_cast<unsigned int>(heatmap_colors[color_idx][1]) << "," << static_cast<unsigned int>(heatmap_colors[color_idx][2]) << "); color: " << text_color << ";'>";

                        // This div is for padding and centering the main content
                        file << "<div>";
                        file << cell_relative_perf;
                        if (g_display_tombstones_of_benchmarks.contains(bmid) && g_shim_is_tombstone.count(shim_label) && g_shim_is_tombstone.at(shim_label)) {
                            file << "<span class='tombstone-marker' title='Uses tombstone-like mechanism'>✝</span>";
                        }
                        file << "</div>"; // End of content div

                        // CORRECTED: The tooltip is now a direct child of the <td>, not nested inside the div.
                        file << "<span class='heatmap-tooltip'>" << std::fixed << std::setprecision(2) << original_score << " ns</span>";
                        file << "</td>";
                    } else {
                        file << "<td class='shim-column-" << safe_label << "' data-score='" << score_str << "' data-original-score='" << original_score_str << "' style='background-color: rgb(60,60,60); color: #a0a0a0;'><div>N/A</div></td>";
                    }
                }
                file << "</tr>\n";
            }
        }

        // --- HTML Output: "Overall Relative Performance" Row ---
        file << "<tr><td class='row-header' style='font-weight:bold; background-color: #404045;'><div style='padding: 4px 12px;'>Overall Relative Performance</div></td>";
        for (const auto& shim_label_col : ordered_shim_labels_for_columns) {
            std::string safe_label = shim_label_col;
            std::ranges::replace_if(safe_label, [](const char c){return !std::isalnum(c);}, '_');
            file << "<td class='shim-column-" << safe_label << "' id='overall_cell_" << safe_label << "' style='font-weight:bold; padding: 4px 12px;'>-</td>";
        }
        file << "</tr>\n";

        file << "</tbody>\n</table>\n";
        file << std::resetiosflags(std::ios_base::fixed) << std::setprecision(6);
    }

} // namespace BenchmarkOutput

#endif // BENCHMARK_OUTPUT_H