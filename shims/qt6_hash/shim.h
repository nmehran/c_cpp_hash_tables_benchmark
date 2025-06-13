/**
 * @file shims/qt6_hash/shim.h
 * @brief This shim adapts `QHash` from the Qt 6 framework to the benchmark
 * suite's standardized API, enabling its inclusion in performance comparisons.
 *
 * @copyright Copyright (c) 2025-Present Gradient Dynamics LLC
 * @copyright Copyright (c) 2025-Present Nima Mehrani
 *
 * @note This shim integrates with the C/C++ hash table benchmark suite by Jackson L. Allan
 *       and its usage in that context is subject to that project's copyright and license.
 * @note The underlying `QHash` library is part of the Qt framework (https://github.com/qt/qtbase)
 *       by The Qt Company Ltd. and is subject to its own copyright and license.
 *
 * @license Distributed under the MIT License (see LICENSE file for details).
 */

#pragma once

/**
 * @note Inclusion of the Qt library is conditional due to its substantial
 *       impact on build times and binary size from extensive dependencies.
 *       To enable this shim, `HASH_BENCH_ENABLE_QT` must be `1`, configured
 *       in `config.h` or via `-HASH_BENCH_ENABLE_QT=1` (for direct
 *       compilation). Note that enabling Qt may mandate dynamic linking for the
 *       benchmark executable (use of `-static` linker flag may not work).
 */
#if defined(HASH_BENCH_ENABLE_QT) && HASH_BENCH_ENABLE_QT == 1

#include <QHash>
#include <QtGlobal>      // For qHash, quint64
#include <type_traits>   // For noexcept propagation and type introspection

// Emit a compile-time advisory that QHash uses an internal target load factor.
#if !defined(QT_QHASH_LOAD_FACTOR_ADVISORY_EMITTED)
    #define QT_QHASH_LOAD_FACTOR_ADVISORY_EMITTED
    #define QT_QHASH_ADVISORY_MESSAGE \
        "Qt QHash Advisory: MAX_LOAD_FACTOR not applicable; QHash grows when load factor reaches 0.5."
    #if defined(_MSC_VER)
        #pragma message(QT_QHASH_ADVISORY_MESSAGE)
    #elif defined(__GNUC__) || defined(__clang__)
        #pragma message QT_QHASH_ADVISORY_MESSAGE
    #endif
#endif // QT_QHASH_LOAD_FACTOR_ADVISORY_EMITTED


// By default, this shim uses the QtWrappedKey to inject the benchmark's hash/compare
// functions. This is necessary because QHash discovers these functions via Argument-Dependent
// Lookup (ADL) on global qHash() and operator==() overloads, not via template parameters.
// To use Qt's native hashing, define QT_SHIM_USE_DEFAULT_HASH_INTERNALS=1 (or uncomment the line below)
// #define QT_SHIM_USE_DEFAULT_HASH_INTERNALS 1
#if !defined(QT_SHIM_USE_DEFAULT_HASH_INTERNALS) || (QT_SHIM_USE_DEFAULT_HASH_INTERNALS == 0)

/**
 * @brief A wrapper for the blueprint's key_type that allows injection of
 * custom hash and comparison logic for QHash.
 * @tparam OwningBlueprint The benchmark blueprint providing key_type, hash_key, and cmpr_keys.
 */
template<typename OwningBlueprint>
struct QtWrappedKey
{
    using blueprint_type = OwningBlueprint;
    typename blueprint_type::key_type key_val;

    // Default constructor
    constexpr QtWrappedKey() noexcept(std::is_nothrow_default_constructible_v<typename blueprint_type::key_type>) = default;

    // Converting constructor from the underlying key type
    constexpr explicit QtWrappedKey(const typename blueprint_type::key_type& k)
        noexcept(std::is_nothrow_copy_constructible_v<typename blueprint_type::key_type>)
        : key_val(k) {}
};

/**
 * @brief Global operator== overload for QtWrappedKey.
 * @details QHash finds this overload via ADL to compare keys. It forwards the
 * call to the blueprint's `cmpr_keys` function.
 */
template<typename OwningBlueprint>
inline constexpr bool operator==(const QtWrappedKey<OwningBlueprint>& a,
                                 const QtWrappedKey<OwningBlueprint>& b)
    noexcept(noexcept(OwningBlueprint::cmpr_keys(a.key_val, b.key_val)))
{
    return OwningBlueprint::cmpr_keys(a.key_val, b.key_val);
}

/**
 * @brief Global qHash overload for QtWrappedKey.
 * @details QHash finds this overload via ADL to hash keys. It forwards the
 * call to the blueprint's `hash_key` function and then delegates seed mixing
 * to Qt's own `::qHash` overload for integers.
 */
template<typename OwningBlueprint>
inline size_t qHash(const QtWrappedKey<OwningBlueprint>& wrapper, size_t seed = 0)
    noexcept(noexcept(OwningBlueprint::hash_key(wrapper.key_val)))
{
    // Let the blueprint generate the primary hash value.
    const quint64 primary_hash = static_cast<quint64>(OwningBlueprint::hash_key(wrapper.key_val));
    // Delegate to Qt's global ::qHash to correctly mix the seed.
    return ::qHash(primary_hash, seed);
}

#endif // Logic for default behavior (using benchmark's hash/compare)


/**
 * @brief Adapter that conforms `QHash` to the benchmark API.
 * @tparam blueprint A type providing key_type, value_type, hash_key, and cmpr_keys.
 */
template< typename blueprint >
struct qt_hash_6
{
public:
#if defined(QT_SHIM_USE_DEFAULT_HASH_INTERNALS) && (QT_SHIM_USE_DEFAULT_HASH_INTERNALS != 0)
    /// Optional mode: Use blueprint's key_type directly, relying on user-provided global overloads.
    using internal_key_type = typename blueprint::key_type;
#else
    /// Default mode: Use the wrapper to force blueprint's hash/compare logic via ADL.
    using internal_key_type = QtWrappedKey<blueprint>;
#endif

    /// @brief Adapts the blueprint's hash function (for benchmark framework consistency).
    /// @note QHash does not use this struct directly. See QtWrappedKey's qHash overload.
    struct hash
    {
        /// Signals to the benchmark that `blueprint::hash_key` is high-quality.
        using is_avalanching = void;
        /// Forwards to the blueprint's hash function; noexcept if blueprint's function is noexcept.
        std::size_t operator()( const typename blueprint::key_type &key ) const
            noexcept(noexcept(blueprint::hash_key( key )))
        {
            return blueprint::hash_key( key );
        }
    };

    /// @brief Adapts the blueprint's key comparison function (for benchmark framework consistency).
    /// @note QHash does not use this struct directly. See QtWrappedKey's operator== overload.
    struct cmpr
    {
        /// Forwards to the blueprint's key comparison function; noexcept if blueprint's function is noexcept.
        bool operator()( const typename blueprint::key_type &key_1, const typename blueprint::key_type &key_2 ) const
            noexcept(noexcept(blueprint::cmpr_keys( key_1, key_2 )))
        {
            return blueprint::cmpr_keys( key_1, key_2 );
        }
    };

    /// The specialized hash table type.
    using table_type = QHash<
        internal_key_type,
        typename blueprint::value_type
    >;
    /// The iterator for the hash table.
    using iterator_type = typename table_type::iterator;

    //===------------------------------------------------------------------===//
    //                        Benchmark API Methods
    //===------------------------------------------------------------------===//

    static table_type create_table()
    {
        // QHash is default-constructible. The load factor is managed internally.
        return table_type{};
    }

    static iterator_type find( table_type &table, const typename blueprint::key_type &key )
    {
        // Construct the appropriate internal_key_type (either the wrapper or the key
        // itself) and pass it to QHash's find method.
        return table.find(internal_key_type(key));
    }

    static void insert( table_type &table, const typename blueprint::key_type &key )
    {
        // Use emplace(key) for an efficient "insert or update" operation.
        // If the key exists, its value is default-constructed (a no-op for the benchmark).
        // If it doesn't exist, a new element is created with a default-constructed value.
        table.emplace(internal_key_type(key));
    }

    static void erase( table_type &table, const typename blueprint::key_type &key )
    {
        // QHash::remove() is the equivalent of erase-by-key.
        table.remove(internal_key_type(key));
    }

    static iterator_type begin_itr( table_type &table )
    {
        return table.begin();
    }

    static bool is_itr_valid( table_type &table, iterator_type &itr )
    {
        // Standard C++ iterator validity check against the end sentinel.
        return itr != table.end();
    }

    static void increment_itr( table_type & /*table*/, iterator_type &itr )
    {
        // The `table` parameter is unused but retained for API compatibility.
        // QHash's iterator supports standard pre-increment. Its implementation
        // correctly finds the next occupied bucket in the table.
        ++itr;
    }

    static const typename blueprint::key_type &get_key_from_itr( table_type & /*table*/, iterator_type &itr )
    {
        // Access the key via the iterator's key() method.
        // If using the wrapper, we must unwrap it to return the blueprint's key_type.
#if defined(QT_SHIM_USE_DEFAULT_HASH_INTERNALS) && (QT_SHIM_USE_DEFAULT_HASH_INTERNALS != 0)
        return itr.key();
#else
        return itr.key().key_val;
#endif
    }

    static const typename blueprint::value_type &get_value_from_itr( table_type & /*table*/, iterator_type &itr )
    {
        static_assert(!std::is_same_v<typename blueprint::value_type, std::nullptr_t>,
                      "blueprint::value_type cannot be std::nullptr_t for value iteration. "
                      "Use an empty struct for set-like behavior.");
        // Access the value via the iterator's value() method.
        return itr.value();
    }

    static void destroy_table( table_type & /*table*/ )
    {
        // QHash utilizes RAII; its destructor handles all cleanup.
    }
};

/**
 * @brief Metadata specialization for benchmark reporting.
 */
template<> struct qt_hash_6< void >
{
    /// The official name for display in plots and reports.
    static constexpr const char *label =
        #if defined(QT_SHIM_USE_DEFAULT_HASH_INTERNALS) && (QT_SHIM_USE_DEFAULT_HASH_INTERNALS != 0)
            "qt6_hash_default";
        #else
            "qt6_hash";
        #endif
    /// The color used for this table in generated plots (RGB).
    static constexpr const char *color = "rgb( 50, 205, 50 )";  // LimeGreen
    /// Indicates whether the table uses a tombstone-like mechanism for deletions.
    /// QHash uses robin-hood-like backward-shifting on erasure, not tombstones.
    static constexpr bool tombstone_like_mechanism = false;
};

#endif // defined(HASH_BENCH_ENABLE_QT) && HASH_BENCH_ENABLE_QT == 1