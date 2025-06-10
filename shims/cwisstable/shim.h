/**
 * @file shims/cwisstable/shim.h
 * @brief This shim adapts the `cwisstable` C hash table library to the
 *        benchmark suite's standardized C++ API.
 *
 * @copyright Copyright (c) 2025-Present Gradient Dynamics LLC
 * @copyright Copyright (c) 2025-Present Nima Mehrani
 *
 * @note This shim integrates with the C/C++ hash table benchmark suite by Jackson L. Allan
 *       and its usage in that context is subject to that project's copyright and license.
 * @note The underlying `cwisstable` library is a C port of Google's Abseil Swiss Table
 *       (https://github.com/google/cwisstable) and is subject to its own copyright and license.
 *
 * @license MIT (see LICENSE file for details)
 */

#pragma once

// Emit a compile-time advisory that this table uses a fixed load factor.
#if !defined(CWISSTABLE_LOAD_FACTOR_ADVISORY_EMITTED)
    #define CWISSTABLE_LOAD_FACTOR_ADVISORY_EMITTED
    #define CWISSTABLE_ADVISORY_MSG \
        "cwisstable Shim Advisory: This table uses its internal fixed max load factor (0.875). The benchmark's global MAX_LOAD_FACTOR is not applied."
    #if defined(_MSC_VER)
        #pragma message(CWISSTABLE_ADVISORY_MSG)
    #elif defined(__GNUC__) || defined(__clang__)
        #pragma message CWISSTABLE_ADVISORY_MSG
    #endif
#endif // CWISSTABLE_LOAD_FACTOR_ADVISORY_EMITTED

#include <cstddef>
#include <cstdint>
#include <type_traits>

// Forward declare C++ blueprint structs
struct uint32_uint32_murmur;
struct uint64_struct448_murmur;
struct cstring_uint64_fnv1a;

// Include cwisstable.h within extern "C" to handle C syntax & linkage and suppress C-style warnings.
extern "C" {
    #if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wpedantic"
    #pragma clang diagnostic ignored "-Wc99-extensions"
    #pragma clang diagnostic ignored "-Wgnu-empty-struct"
    #pragma clang diagnostic ignored "-Wzero-length-array"
    #pragma clang diagnostic ignored "-Wnarrowing"
    #pragma clang diagnostic ignored "-Woverflow"
    #pragma clang diagnostic ignored "-Wsign-compare"
    #elif defined(__GNUC__) || defined(__GNUG__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wpedantic"
    #pragma GCC diagnostic ignored "-Wnarrowing"
    #pragma GCC diagnostic ignored "-Woverflow"
    #pragma GCC diagnostic ignored "-Wsign-compare"
    #endif

    #include "cwisstable.h" // THE C HEADER

    #if defined(__clang__)
    #pragma clang diagnostic pop
    #elif defined(__GNUC__) || defined(__GNUG__)
    #pragma GCC diagnostic pop
    #endif
} // extern "C"

// Primary template declaration
template <typename Blueprint>
struct cwisstable;

// Macro to generate a full C++ shim specialization for a given blueprint.
#define CWISSTABLE_SHIM_SPECIALIZATION(blueprint_name)                                                                \
    /* Define a layout-compatible entry struct. This is a key technique for interfacing with the C macro API. */      \
    /* It breaks a circular dependency where the C-API's type generation needs our adapter functions, which in */     \
    /* turn need the generated types. This manually-defined struct resolves the deadlock. */                          \
    struct cwisstable_shim_entry_##blueprint_name {                                                                   \
        typename blueprint_name::key_type key;                                                                        \
        typename blueprint_name::value_type val;                                                                      \
    };                                                                                                                \
                                                                                                                      \
    /* A namespace to isolate the C-to-C++ adapter functions, preventing name collisions. */                          \
    namespace cwisstable_adapters_##blueprint_name {                                                                  \
        extern "C" {                                                                                                  \
            /* Bridges the C API's hash function pointer to the C++ blueprint's hash_key method. */                   \
            static inline size_t generated_hash_adapter_for_##blueprint_name(const void* key_ptr) {                   \
                return blueprint_name::hash_key(                                                                      \
                    *static_cast<const typename blueprint_name::key_type*>(key_ptr)                                   \
                );                                                                                                    \
            }                                                                                                         \
            /* Bridges the C API's equality function pointer to the C++ blueprint's cmpr_keys method. */              \
            static inline bool generated_eq_adapter_for_##blueprint_name(                                             \
                const void* needle_key_ptr,                                                                           \
                const void* candidate_entry_ptr                                                                       \
            ) {                                                                                                       \
                /* The C API passes a pointer to the full entry for the candidate slot. We must cast */               \
                /* to our layout-compatible struct and then access its .key member for correct comparison. */         \
                return blueprint_name::cmpr_keys(                                                                     \
                    *static_cast<const typename blueprint_name::key_type*>(needle_key_ptr),                           \
                    static_cast<const ::cwisstable_shim_entry_##blueprint_name*>(candidate_entry_ptr)->key            \
                );                                                                                                    \
            }                                                                                                         \
        }                                                                                                             \
    }                                                                                                                 \
                                                                                                                      \
    /* With the adapters defined, we can now invoke the C macro to create a policy object. This policy */             \
    /* object embeds the function pointers to our C++ adapters, completing the bridge. */                             \
    CWISS_DECLARE_FLAT_MAP_POLICY(                                                                                    \
        CwisstablePolicy_##blueprint_name,                                                                            \
        typename blueprint_name::key_type,                                                                            \
        typename blueprint_name::value_type,                                                                          \
        (key_hash, ::cwisstable_adapters_##blueprint_name::generated_hash_adapter_for_##blueprint_name),              \
        (key_eq,   ::cwisstable_adapters_##blueprint_name::generated_eq_adapter_for_##blueprint_name)                 \
    );                                                                                                                \
                                                                                                                      \
    /* Finally, invoke the main C macro to generate the hash map's types (e.g., CwisstableMap_...) and */             \
    /* its associated C-style API, all configured to use our custom policy. */                                        \
    CWISS_DECLARE_HASHMAP_WITH(                                                                                       \
        CwisstableMap_##blueprint_name,                                                                               \
        typename blueprint_name::key_type,                                                                            \
        typename blueprint_name::value_type,                                                                          \
        CwisstablePolicy_##blueprint_name                                                                             \
    );                                                                                                                \
                                                                                                                      \
    /**                                                                                                               \
     * @brief Adapter that conforms cwisstable to the benchmark API for a specific blueprint.                         \
     */                                                                                                               \
    template <> struct cwisstable<blueprint_name> {                                                                   \
    public:                                                                                                           \
        using BP = blueprint_name;                                                                                    \
        using table_type    = CwisstableMap_##blueprint_name;                                                         \
        using iterator_type = CwisstableMap_##blueprint_name##_Iter;                                                  \
        using entry_type    = CwisstableMap_##blueprint_name##_Entry;                                                 \
                                                                                                                      \
        /*===------------------------------------------------------------------===*/                                  \
        /*                        Benchmark API Methods                           */                                  \
        /*===------------------------------------------------------------------===*/                                  \
                                                                                                                      \
        static table_type create_table() {                                                                            \
            /* Create the table with a small initial capacity. */                                                     \
            /* The C API will normalize this to the nearest valid capacity (e.g., 64 -> 127). */                      \
            return CwisstableMap_##blueprint_name##_new(64);                                                          \
        }                                                                                                             \
        static void insert(table_type &table, const typename BP::key_type &key) {                                     \
            auto result = CwisstableMap_##blueprint_name##_deferred_insert(&table, &key);                             \
            if (result.inserted) {                                                                                    \
                entry_type* entry_ptr = CwisstableMap_##blueprint_name##_Iter_get(&result.iter);                      \
                entry_ptr->key = key;                                                                                 \
                entry_ptr->val = typename BP::value_type{};                                                           \
            }                                                                                                         \
        }                                                                                                             \
        static iterator_type find(table_type &table, const typename BP::key_type &key) {                              \
            return CwisstableMap_##blueprint_name##_find(&table, &key);                                               \
        }                                                                                                             \
        static void erase(table_type &table, const typename BP::key_type &key) {                                      \
            (void)CwisstableMap_##blueprint_name##_erase(&table, &key);                                               \
        }                                                                                                             \
        static iterator_type begin_itr(table_type &table) {                                                           \
            return CwisstableMap_##blueprint_name##_iter(&table);                                                     \
        }                                                                                                             \
        static bool is_itr_valid(table_type & /*table*/, iterator_type &itr) {                                        \
            return CwisstableMap_##blueprint_name##_Iter_get(&itr) != nullptr;                                        \
        }                                                                                                             \
        static void increment_itr(table_type & /*table*/, iterator_type &itr) {                                       \
            (void)CwisstableMap_##blueprint_name##_Iter_next(&itr);                                                   \
        }                                                                                                             \
        static const typename BP::key_type &get_key_from_itr(table_type & /*table*/, iterator_type &itr) {            \
            return CwisstableMap_##blueprint_name##_Iter_get(&itr)->key;                                              \
        }                                                                                                             \
        static const typename BP::value_type &get_value_from_itr(table_type & /*table*/, iterator_type &itr) {        \
            static_assert(!std::is_same_v<typename BP::value_type, std::nullptr_t>,                                   \
                      "blueprint::value_type cannot be std::nullptr_t for value iteration. "                          \
                      "Use an empty struct for set-like behavior.");                                                  \
            return CwisstableMap_##blueprint_name##_Iter_get(&itr)->val;                                              \
        }                                                                                                             \
        static void destroy_table(table_type &table) {                                                                \
            CwisstableMap_##blueprint_name##_destroy(&table);                                                         \
        }                                                                                                             \
    };

// The C macros use C99 compound literals, which are a non-standard extension in C++.
// We suppress the resulting -Wpedantic warnings just for the macro expansion block.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpedantic"
#elif defined(__GNUC__) || defined(__GNUG__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif

// Instantiate the specializations for the enabled blueprints
#ifdef UINT32_UINT32_MURMUR_ENABLED
CWISSTABLE_SHIM_SPECIALIZATION(uint32_uint32_murmur)
#endif

#ifdef UINT64_STRUCT448_MURMUR_ENABLED
CWISSTABLE_SHIM_SPECIALIZATION(uint64_struct448_murmur)
#endif

#ifdef CSTRING_UINT64_FNV1A_ENABLED
CWISSTABLE_SHIM_SPECIALIZATION(cstring_uint64_fnv1a)
#endif

// Restore the diagnostic settings.
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__) || defined(__GNUG__)
#pragma GCC diagnostic pop
#endif

#undef CWISSTABLE_SHIM_SPECIALIZATION

/**
 * @brief Metadata specialization for the cwisstable shim.
 */
template <>
struct cwisstable<void> {
    /// The official name for display in plots and reports.
    static constexpr const char *label = "cwisstable";
    /// The color used for this table in generated plots (RGB).
    static constexpr const char *color = "rgb( 220, 20, 60 )"; // Crimson
    /// Indicates that the table uses tombstones (CWISS_kDeleted) for deletions.
    static constexpr bool tombstone_like_mechanism = true;
};