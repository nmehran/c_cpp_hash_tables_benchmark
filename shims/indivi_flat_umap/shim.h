/**
 * @file shims/indivi_flat_umap/shim.h
 * @brief This shim adapts `indivi::flat_umap` to the benchmark suite's
 * standardized API, enabling its inclusion in performance comparisons.
 *
 * @copyright Copyright (c) 2025-Present Gradient Dynamics LLC
 * @copyright Copyright (c) 2025-Present Nima Mehrani
 *
 * @note This shim integrates with the C/C++ hash table benchmark suite by Jackson L. Allan
 *       and its usage in that context is subject to that project's copyright and license.
 * @note The underlying `indivi::flat_umap` library was created by Guillaume Aujay
 *       and is subject to its own copyright and license.
 *
 * @license MIT (see LICENSE file for details)
 */

#pragma once

// Isolate this shim from any global MAX_LOAD_FACTOR definition, as it
// conflicts with the hash table's internal constant which has the same name.
#ifdef MAX_LOAD_FACTOR
  #pragma push_macro("MAX_LOAD_FACTOR")
  #undef MAX_LOAD_FACTOR
  #define INDIVI_SHIM_MAX_LOAD_FACTOR_WAS_PUSHED
#endif

// Emit a compile-time advisory that this table uses a fixed load factor.
#if !defined(INDIVI_FLAT_UMAP_LOAD_FACTOR_ADVISORY_EMITTED)
    #define INDIVI_FLAT_UMAP_LOAD_FACTOR_ADVISORY_EMITTED
    #define INDIVI_FLAT_UMAP_ADVISORY_MSG \
        "indivi_flat_umap Shim Advisory: This table uses its internal fixed load factor (0.875f). The benchmark's global MAX_LOAD_FACTOR is not applied."
    #if defined(_MSC_VER)
        #pragma message(INDIVI_FLAT_UMAP_ADVISORY_MSG)
    #elif defined(__GNUC__) || defined(__clang__)
        #pragma message INDIVI_FLAT_UMAP_ADVISORY_MSG
    #endif
#endif // INDIVI_FLAT_UMAP_LOAD_FACTOR_ADVISORY_EMITTED

#include "indivi/flat_umap.h" // The underlying hash table implementation.

// Restore the global MAX_LOAD_FACTOR if it was defined before this header.
#ifdef INDIVI_SHIM_MAX_LOAD_FACTOR_WAS_PUSHED
  #pragma pop_macro("MAX_LOAD_FACTOR")
  #undef INDIVI_SHIM_MAX_LOAD_FACTOR_WAS_PUSHED
#endif

#include <cstddef>
#include <type_traits>

/**
 * @brief Adapter that conforms `indivi::flat_umap` to the benchmark API.
 * @tparam blueprint A type providing key_type, value_type, hash_key, and cmpr_keys.
 */
template<typename blueprint>
struct indivi_flat_umap {
public:
    /// @brief Adapts the blueprint's hash function.
    struct hash {
        /// Signals to the benchmark that `blueprint::hash_key` is high-quality (unused by indivi_flat_umap).
        using is_avalanching = void;

        /// Forwards to the blueprint's hash function; noexcept if blueprint's function is noexcept.
        std::size_t operator()(const typename blueprint::key_type &key) const
            noexcept(std::is_nothrow_invocable_v<decltype(blueprint::hash_key), const typename blueprint::key_type &>) {
            return blueprint::hash_key(key);
        }
    };

    /// @brief Adapts the blueprint's key comparison function.
    struct cmpr {
        /// Forwards to the blueprint's key comparison function; noexcept if blueprint's function is noexcept.
        bool operator()(const typename blueprint::key_type &key_1, const typename blueprint::key_type &key_2) const
            noexcept(std::is_nothrow_invocable_v<decltype(blueprint::cmpr_keys), const typename blueprint::key_type &, const typename blueprint::key_type &>) {
            return blueprint::cmpr_keys(key_1, key_2);
        }
    };

    /// The specialized hash table type.
    using table_type = indivi::flat_umap<typename blueprint::key_type, typename blueprint::value_type, hash, cmpr>;
    /// The iterator for the hash table.
    using iterator_type = typename table_type::iterator;

    //===------------------------------------------------------------------===//
    //                        Benchmark API Methods
    //===------------------------------------------------------------------===//

    static table_type create_table() {
        // indivi::flat_umap uses an internal fixed max load factor (see advisory above).
        return table_type{};
    }

    static iterator_type find(table_type &table, const typename blueprint::key_type &key) {
        return table.find(key);
    }

    static void insert(table_type &table, const typename blueprint::key_type &key) {
        // Use operator[] to ensure "insert or update" semantics, as required by the benchmark.
        table[key] = typename blueprint::value_type{};
    }

    static void erase(table_type &table, const typename blueprint::key_type &key) {
        table.erase(key);
    }

    static iterator_type begin_itr(table_type &table) {
        return table.begin();
    }

    static bool is_itr_valid(table_type &table, iterator_type &itr) {
        // For indivi::flat_umap, table.end() returns a unique end iterator instance, so direct comparison is correct.
        return itr != table.end();
    }

    static void increment_itr(table_type & /*table*/, iterator_type &itr) {
        // The `table` parameter is unused but retained for API compatibility.
        ++itr;
    }

    static const typename blueprint::key_type &get_key_from_itr(table_type & /*table*/, iterator_type &itr) {
        // indivi::flat_umap iterators dereference to a pair-like structure; `itr->first` is the key.
        return itr->first;
    }

    static const typename blueprint::value_type &get_value_from_itr(table_type & /*table*/, iterator_type &itr) {
        static_assert(!std::is_same_v<typename blueprint::value_type, std::nullptr_t>,
                      "blueprint::value_type cannot be std::nullptr_t for value iteration. "
                      "Use an empty struct for set-like behavior.");
        return itr->second; // Value access via `itr->second`.
    }

    static void destroy_table(table_type & /*table*/) {
        // indivi::flat_umap utilizes RAII; its destructor handles cleanup.
    }
};

/**
 * @brief Metadata specialization for benchmark reporting.
 */
template<>
struct indivi_flat_umap<void> {
    /// The official name for display in plots and reports.
    static constexpr const char *label = "indivi_flat_umap";
    /// The color used for this table in generated plots (RGB).
    static constexpr const char *color = "rgb(128, 0, 128)"; // Purple
    /// Indicates whether the table uses a tombstone-like mechanism for deletions.
    static constexpr bool tombstone_like_mechanism = false;
};