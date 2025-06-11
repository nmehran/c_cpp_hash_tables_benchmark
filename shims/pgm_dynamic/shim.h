/**
 * @file shims/pgm_dynamic/shim.h
 * @brief This shim adapts the PGM-index (Dynamic variant) to the benchmark suite's
 * standardized API, enabling its inclusion in performance comparisons.
 *
 * @copyright Copyright (c) 2025-Present Gradient Dynamics LLC
 * @copyright Copyright (c) 2025-Present Nima Mehrani
 *
 * @note This shim integrates with the C/C++ hash table benchmark suite by Jackson L. Allan
 *       and its usage in that context is subject to that project's copyright and license.
 * @note The underlying PGM-index library was created by Giorgio Vinciguerra and Paolo Ferragina
 *       (https://github.com/gvinciguerra/PGM-index) and is subject to its own copyright and license.
 *
 * @license MIT (see LICENSE file for details)
 */

#pragma once

// Emit a compile-time advisory about the unique nature of PGM-Index.
#if !defined(PGM_DYNAMIC_SHIM_ADVISORY_EMITTED)
    #define PGM_DYNAMIC_SHIM_ADVISORY_EMITTED
    #define PGM_DYNAMIC_SHIM_ADVISORY_MSG \
        "PGM-index Shim Advisory: This is a learned index for sorted, arithmetic keys. It uses the key's native operators for its core logic, so the benchmark's `MAX_LOAD_FACTOR`, `hash`, and `cmpr` cannot be applied."
    #if defined(_MSC_VER)
        #pragma message(PGM_DYNAMIC_SHIM_ADVISORY_MSG)
    #elif defined(__GNUC__) || defined(__clang__)
        #pragma message PGM_DYNAMIC_SHIM_ADVISORY_MSG
    #endif
#endif // PGM_DYNAMIC_SHIM_ADVISORY_EMITTED

// Suppress specific warnings from the third-party PGM-index library headers.
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunknown-pragmas"  // The library uses #pragma omp (fine if not compiling with OpenMP)
#pragma GCC diagnostic ignored "-Wpedantic"  // The library uses the __int128 extension (non-standard but supported by GCC/Clang)
#endif

#include "pgm/pgm_index_dynamic.hpp"  // Main include for PGM-Dynamic

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#include <utility>      // For std::pair, std::move
#include <cstddef>      // For std::size_t
#include <optional>     // For std::optional
#include <iterator>     // For std::iterator_traits
#include <stdexcept>    // For std::runtime_error
#include <type_traits>  // For std::is_arithmetic_v, etc.

/**
 * @brief Adapter that conforms `pgm::DynamicPGMIndex` to the benchmark API.
 * @tparam blueprint A type providing key_type, value_type, hash_key, and cmpr_keys.
 */
template<typename blueprint>
struct pgm_dynamic {
public:
    using key_type_internal = typename blueprint::key_type;
    using value_type_internal = typename blueprint::value_type;

    // --- PGM-Index: Fundamental Requirements & API Mismatch ---
    // PGM-Index is a learned index for sorted data, not a general-purpose hash table.
    // Its internal algorithms have two critical requirements for the key type:
    // 1. It must be an arithmetic type (or enum) to allow for mathematical modeling.
    // 2. It must be comparable using its native `operator<` and `operator==`.
    //
    // As a result, the benchmark's `hash` and `cmpr` functions are ignored by the
    // core logic of PGM-Index. They are included in this shim solely to satisfy the
    // benchmark's generic API. The `static_assert` below enforces the key type constraint.
    static_assert((std::is_arithmetic_v<key_type_internal> || std::is_enum_v<key_type_internal>),
                  "PGM-index Shim: The provided key_type is not supported (see compiler note for the invalid type). "
                  "PGM-index requires a numeric key (e.g., int, uint64_t, double). "
                  "Non-numeric types like std::string must first be mapped to an order-preserving numeric representation to be used with this data structure.");

    // The actual PGM Dynamic Index table type from the library.
    using pgm_actual_table_type = pgm::DynamicPGMIndex<key_type_internal, value_type_internal>;
    // The actual iterator type from the PGM library.
    using pgm_actual_iterator = typename pgm_actual_table_type::iterator;

    /**
     * @brief A wrapper to make the PGM-index iterator default-constructible
     *        and to provide a safe interface for the benchmark framework.
     */
    class iterator_wrapper {
    public:
        // Standard iterator traits, derived from the actual PGM iterator.
        using difference_type   = typename std::iterator_traits<pgm_actual_iterator>::difference_type;
        using value_type        = typename std::iterator_traits<pgm_actual_iterator>::value_type;
        using pointer           = typename std::iterator_traits<pgm_actual_iterator>::pointer;
        using reference         = typename std::iterator_traits<pgm_actual_iterator>::reference;
        using iterator_category = typename std::iterator_traits<pgm_actual_iterator>::iterator_category;

    private:
        // std::optional allows us to represent a disengaged (null-like) state.
        std::optional<pgm_actual_iterator> m_it_opt;

    public:
        /// @brief Default constructor: creates a disengaged (null-like) iterator.
        iterator_wrapper() noexcept : m_it_opt() {}

        /// @brief Constructor from an actual PGM iterator.
        iterator_wrapper(pgm_actual_iterator pgm_it) : m_it_opt(std::move(pgm_it)) {}

        /// @brief Dereferences the iterator. Throws if the iterator is disengaged.
        reference operator*() const {
            return *m_it_opt.value();
        }
        /// @brief Accesses the pointed-to element's members. Throws if the iterator is disengaged.
        pointer operator->() const {
            return m_it_opt.value().operator->();
        }

        /// @brief Pre-increments the iterator.
        iterator_wrapper& operator++() {
            if (!m_it_opt) {
                throw std::runtime_error("Attempted to increment a disengaged (default-constructed) pgm_dynamic iterator.");
            }
            ++(m_it_opt.value());
            return *this;
        }

        /// @brief Post-increments the iterator.
        iterator_wrapper operator++(int) {
            iterator_wrapper temp = *this;
            ++(*this);
            return temp;
        }

        /// @brief Compares two iterators for equality.
        friend bool operator==(const iterator_wrapper& a, const iterator_wrapper& b) {
            if (a.m_it_opt.has_value() != b.m_it_opt.has_value()) {
                return false;
            }
            return !a.m_it_opt.has_value() || (a.m_it_opt.value() == b.m_it_opt.value());
        }

        /// @brief Compares two iterators for inequality.
        friend bool operator!=(const iterator_wrapper& a, const iterator_wrapper& b) {
            return !(a == b);
        }

        /// @brief Helper to check if the iterator is engaged.
        bool is_engaged() const {
            return m_it_opt.has_value();
        }

        /// @brief Helper to get the underlying iterator. Throws if not engaged.
        const pgm_actual_iterator& get_underlying_iterator() const {
            return m_it_opt.value();
        }
    };

    /// The specialized table type being benchmarked.
    using table_type = pgm_actual_table_type;
    /// The iterator for the table, using our wrapper.
    using iterator = iterator_wrapper;

    // --- API Compatibility Structs ---
    // These structs adapt the blueprint's functions for API compatibility. As noted
    // above, they are not used by the core PGM-Index logic.

    /// @brief Adapts the blueprint's hash function for API compatibility.
    struct hash {
        using is_avalanching = void;
        std::size_t operator()(const key_type_internal& key) const
            noexcept(std::is_nothrow_invocable_v<decltype(blueprint::hash_key), const key_type_internal&>) {
            return blueprint::hash_key(key);
        }
    };

    /// @brief Adapts the blueprint's key comparison function for API compatibility.
    struct cmpr {
        bool operator()(const key_type_internal& key_1, const key_type_internal& key_2) const
            noexcept(std::is_nothrow_invocable_v<decltype(blueprint::cmpr_keys), const key_type_internal&, const key_type_internal&>) {
            return blueprint::cmpr_keys(key_1, key_2);
        }
    };

    //===------------------------------------------------------------------===//
    //                        Benchmark API Methods
    //===------------------------------------------------------------------===//

    static table_type create_table() {
        // Use the default constructor for pgm::DynamicPGMIndex.
        // The benchmark's global MAX_LOAD_FACTOR is not applicable.
        return table_type{};
    }

    static iterator find(table_type& table, const key_type_internal& key) {
        // pgm::DynamicPGMIndex::find returns a library-native iterator, which we wrap.
        return iterator(table.find(key));
    }

    static void insert(table_type& table, const key_type_internal& key) {
        // pgm::DynamicPGMIndex::insert_or_assign implements "insert or update" semantics.
        // The benchmark API implies inserting with a default-constructed value.
        // NOTE: The library uses a 'tombstone' value for deletions (e.g., numeric_limits<V>::max()).
        // If value_type_internal() (e.g., 0 for int) equals this tombstone, it will throw.
        // This is extremely unlikely for standard types and their default values.
        table.insert_or_assign(key, value_type_internal{});
    }

    static void erase(table_type& table, const key_type_internal& key) {
        // pgm::DynamicPGMIndex::erase marks elements as deleted (tombstone).
        table.erase(key);
    }

    static iterator begin_itr(table_type& table) {
        // Wrap the iterator returned by pgm::DynamicPGMIndex::begin.
        return iterator(table.begin());
    }

    static bool is_itr_valid(table_type& table, iterator& itr) {
        // A disengaged (default-constructed) iterator is never valid.
        if (!itr.is_engaged()) {
            return false;
        }
        // An engaged iterator is valid if it's not equal to the table's end iterator.
        // We must compare the underlying PGM iterator to the one returned by table.end().
        return itr.get_underlying_iterator() != table.end();
    }

    static void increment_itr(table_type& /*table*/, iterator& itr) {
        // The wrapper's ++ operator handles the increment logic.
        // It will throw if called on a disengaged iterator.
        ++itr;
    }

    static const key_type_internal& get_key_from_itr(table_type& /*table*/, iterator& itr) {
        // The wrapper's operator->() returns a const pointer to the internal Item struct,
        // which has a public `first` member for the key.
        return itr->first;
    }

    static const value_type_internal& get_value_from_itr(table_type& /*table*/, iterator& itr) {
        static_assert(!std::is_same_v<value_type_internal, std::nullptr_t>,
                      "blueprint::value_type cannot be std::nullptr_t for value iteration. "
                      "Use an empty struct for set-like behavior.");
        // The internal Item struct has a public `second` member for the value.
        return itr->second;
    }

    static void destroy_table(table_type& /*table*/) {
        // pgm::DynamicPGMIndex utilizes RAII; its destructor handles all cleanup.
    }
};

/**
 * @brief Metadata specialization for benchmark reporting.
 */
template<>
struct pgm_dynamic<void> {
    /// The official name for display in plots and reports.
    static constexpr const char* label = "pgm_dynamic";
    /// The color used for this table in generated plots (RGB).
    static constexpr const char* color = "rgb( 189, 183, 107 )"; // DarkKhaki
    /// Indicates whether the table uses a tombstone-like mechanism for deletions.
    /// pgm::DynamicPGMIndex uses an internal flag or a reserved value to mark
    /// items as deleted, which is functionally equivalent to a tombstone.
    static constexpr bool tombstone_like_mechanism = true;
};