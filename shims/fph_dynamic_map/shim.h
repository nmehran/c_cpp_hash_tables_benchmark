/**
 * @file shims/fph_dynamic_map/shim.h
 * @brief This shim adapts `fph::DynamicFphMap` to the benchmark suite's
 * standardized API, enabling its inclusion in performance comparisons.
 *
 * @copyright Copyright (c) 2025-Present Gradient Dynamics LLC
 * @copyright Copyright (c) 2025-Present Nima Mehrani
 *
 * @note This shim integrates with the C/C++ hash table benchmark suite by Jackson L. Allan
 *       and its usage in that context is subject to that project's copyright and license.
 * @note The underlying `fph::DynamicFphMap` library was created by Ren Zibei
 *       (https://github.com/renzibei/fph-table/) and is subject to its own copyright and license.
 *
 * @license MIT (see LICENSE file for details)
 */

#pragma once

// Isolate this shim from any global MAX_LOAD_FACTOR definition. The underlying
// algorithm is not designed for one-by-one insertions at high load factors.
#ifdef MAX_LOAD_FACTOR
  #pragma push_macro("MAX_LOAD_FACTOR")
  #undef MAX_LOAD_FACTOR
  #define FPH_SHIM_MAX_LOAD_FACTOR_WAS_PUSHED
#endif

// Emit a compile-time advisory about this table's load factor and intended use.
#if !defined(FPH_DYNAMIC_MAP_ADVISORY_EMITTED)
    #define FPH_DYNAMIC_MAP_ADVISORY_EMITTED
    #define FPH_DYNAMIC_MAP_ADVISORY_MSG "fph_dynamic_map Shim Advisory: This table is not designed for one-by-one insertions at high density. To prevent stalls and ensure fair benchmarking of its intended use case (fast lookups), the benchmark's global MAX_LOAD_FACTOR is ignored and the library's internal default (~0.6f) is used."
    #if defined(_MSC_VER)
        #pragma message(FPH_DYNAMIC_MAP_ADVISORY_MSG)
    #elif defined(__GNUC__) || defined(__clang__)
        #pragma message FPH_DYNAMIC_MAP_ADVISORY_MSG
    #endif
#endif // FPH_DYNAMIC_MAP_ADVISORY_EMITTED

#include "dynamic_fph_table.h" // Main include for FPH
#include <utility>             // For std::pair
#include <string>              // For std::string
#include <cstddef>             // For std::size_t
#include <type_traits>         // For static_assert, is_nothrow_invocable_v
#include <list>                // For the custom C-string generator
#include <random>              // For the custom C-string generator

/**
 * @brief Adapter that conforms `fph::DynamicFphMap` to the benchmark API.
 * @tparam blueprint A type providing key_type, value_type, hash_key, and cmpr_keys.
 */
template <typename blueprint>
struct fph_dynamic_map {
public:
    /// @brief Adapts the blueprint's hash function to FPH's seeded hash requirement.
    /// This is critical for C-strings to ensure content is hashed, not pointer values.
    struct fph_seed_hash {
        std::size_t operator()(const typename blueprint::key_type &key, std::size_t seed) const
            noexcept(std::is_nothrow_invocable_v<decltype(blueprint::hash_key), const typename blueprint::key_type &>) {

            std::size_t h = blueprint::hash_key(key);

            // Mix the blueprint's hash value with the seed provided by FPH.
            // This mixing strategy is inspired by FPH's internal examples to ensure
            // the seed properly influences the final hash value.
            if constexpr (sizeof(std::size_t) == 8) { // 64-bit
                 h ^= h >> 33;
                 h *= (seed + 0xff51afd7ed558ccdULL); // Constant from FPH's AnoSeedHash64
                 h ^= h >> 33;
            } else { // 32-bit
                 h ^= h >> 16;
                 h *= (seed + 0x85ebca6bU); // MurmurHash3 constant
                 h ^= h >> 16;
            }
            return h;
        }
    };

    /// @brief Adapts the blueprint's key comparison function.
    struct fph_key_equal {
        bool operator()(const typename blueprint::key_type &key1, const typename blueprint::key_type &key2) const
            noexcept(std::is_nothrow_invocable_v<decltype(blueprint::cmpr_keys), const typename blueprint::key_type &, const typename blueprint::key_type &>) {
            return blueprint::cmpr_keys(key1, key2);
        }
    };

    /// @brief A safe, deterministic, and optimized random key generator for C-strings (`char*`).
    ///
    /// @purpose This generator solves a critical safety issue in the FPH library. The default
    /// generator for pointer types creates dangling pointers, leading to segmentation faults.
    /// This implementation provides memory safety by managing the lifetime of all
    /// generated strings within a single, persistent memory arena.
    ///
    /// @usage FPH calls this generator only during table construction or rehash to create a few
    /// internal "fill" keys for marking empty slots. It is NOT on the critical path for
    /// standard `insert`, `find`, or `erase` operations, but has been optimized
    /// as a matter of good practice.
    struct fph_cstring_random_generator {
        // --- Static Members for Singleton-like State Management ---
        // All members are static to create a single, shared resource manager that persists
        // across the temporary generator objects created by the FPH library.

        /// @brief A single, contiguous memory pool (arena) to hold all generated strings.
        static inline std::vector<char> memory_arena;

        /// @brief The offset that tracks the next available position in the memory_arena.
        static inline size_t next_string_offset = 0;

        /// @brief The shared state for our deterministic pseudo-random number generator (RNG).
        static inline uint64_t rng_state = 0x123456789ABCDEF1ULL;

        /// @brief The constructor's only job is to perform a one-time setup of the memory arena.
        fph_cstring_random_generator() {
            if (memory_arena.capacity() == 0) {
                // Pre-allocate space for 16 strings to avoid reallocations on the hot path.
                memory_arena.reserve(blueprint::string_length * 16);
            }
        }

        /// @brief Fast, high-quality 64-bit non-cryptographic RNG (SplitMix64).
        static uint64_t next_rng() {
            uint64_t z = (rng_state += 0x9e3779b97f4a7c15);
            z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9;
            z = (z ^ (z >> 27)) * 0x94d049bb133111eb;
            return z ^ (z >> 31);
        }

        typename blueprint::key_type operator()() {
            // Blueprint must provide the necessary string_length attribute for C-strings.
            constexpr size_t len = blueprint::string_length;

            // Grow arena if needed (rarely happens due to pre-allocation).
            if (next_string_offset + len > memory_arena.size()) {
                memory_arena.resize(memory_arena.size() * 2 + len * 16);
            }

            char* dest = memory_arena.data() + next_string_offset;

            // Generate random characters using a fast lookup table and bitwise operations.
            static constexpr char alphabet[] = "abcdefghijklmnopqrstuvwxyz012345"; // 32 chars
            static constexpr uint64_t mask = 0x1F; // Bitmask for 5 bits (0-31)

            size_t i = 0;
            while (i < len - 1) {
                uint64_t random_bits = next_rng();
                // Process up to 12 characters from a single 64-bit random number.
                for (int j = 0; j < 12 && i < len - 1; ++j) {
                    dest[i++] = alphabet[random_bits & mask];
                    random_bits >>= 5;
                }
            }
            dest[len - 1] = '\0';

            // Advance the offset for the next call.
            next_string_offset += len;
            return dest;
        }
    };

    // Conditionally select the correct random key generator at compile time.
    using fph_random_key_generator = std::conditional_t<
        std::is_same_v<typename blueprint::key_type, char*>,
        fph_cstring_random_generator,
        fph::dynamic::RandomGenerator<typename blueprint::key_type>
    >;

    /// @brief The specialized hash table type. Template arguments are:
    /// 1. Key:              From the benchmark blueprint.
    /// 2. T:                From the benchmark blueprint.
    /// 3. SeedHash:         Our adapter for the blueprint's hash_key function.
    /// 4. KeyEqual:         Our adapter for the blueprint's cmpr_keys function.
    /// 5. Allocator:        Standard allocator.
    /// 6. BucketParamType:    An integer type that stores packed hashing parameters
    ///    (an offset and a seed-selection bit) for internal key groups. `uint32_t`
    ///    is the library's default, offering a good balance between memory usage
    ///    and table capacity (up to 2^31 elements).
    /// 7. RandomKeyGenerator: Required by FPH for internal fill keys. Our shim
    ///    provides a memory-safe version for C-strings.
    using table_type = fph::DynamicFphMap<
        typename blueprint::key_type,
        typename blueprint::value_type,
        fph_seed_hash,
        fph_key_equal,
        std::allocator<std::pair<const typename blueprint::key_type, typename blueprint::value_type>>,
        uint32_t, // BucketParamType: uint32_t is FPH's default and generally suitable.
        fph_random_key_generator
    >;

    /// The iterator for the hash table.
    using iterator_type = typename table_type::iterator;

    //===------------------------------------------------------------------===//
    //                        Benchmark API Methods
    //===------------------------------------------------------------------===//

    static table_type create_table() {
        // This table uses its internal default max load factor (~0.6f).
        // See advisory at the top of this file for the rationale.
        return table_type{};
    }

    static iterator_type find(table_type &table, const typename blueprint::key_type &key) {
        return table.find(key);
    }

    static void insert(table_type &table, const typename blueprint::key_type &key) {
        // Use try_emplace to insert a key with a default-constructed value.
        // This is more efficient than `table[key] = value_type{}` as it avoids
        // a separate assignment operation after the initial emplacement.
        table.try_emplace(key);
    }

    static void erase(table_type &table, const typename blueprint::key_type &key) {
        table.erase(key);
    }

    static iterator_type begin_itr(table_type &table) {
        return table.begin();
    }

    static bool is_itr_valid(table_type &table, iterator_type &itr) {
        return itr != table.end();
    }

    static void increment_itr(table_type & /*table*/, iterator_type &itr) {
        ++itr;
    }

    static const typename blueprint::key_type &get_key_from_itr(table_type & /*table*/, iterator_type &itr) {
        return itr->first;
    }

    static const typename blueprint::value_type &get_value_from_itr(table_type & /*table*/, iterator_type &itr) {
        static_assert(!std::is_same_v<typename blueprint::value_type, std::nullptr_t>,
                      "blueprint::value_type cannot be std::nullptr_t for value iteration. "
                      "Use an empty struct for set-like behavior.");
        return itr->second;
    }

    static void destroy_table(table_type & /*table*/) {
        // fph::DynamicFphMap utilizes RAII; its destructor handles all cleanup.
    }
};

/**
 * @brief Metadata specialization for benchmark reporting.
 */
template <>
struct fph_dynamic_map<void> {
    static constexpr const char *label = "fph_dynamic_map";
    static constexpr const char *color = "rgb( 255, 160, 122 )";  // LightSalmon
    static constexpr bool tombstone_like_mechanism = false;
};

// Restore the global MAX_LOAD_FACTOR if it was defined before this header.
#ifdef FPH_SHIM_MAX_LOAD_FACTOR_WAS_PUSHED
  #pragma pop_macro("MAX_LOAD_FACTOR")
  #undef FPH_SHIM_MAX_LOAD_FACTOR_WAS_PUSHED
#endif
