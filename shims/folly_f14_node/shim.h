/**
 * @file definitions/shims/folly_f14_node/shim.h
 * @brief This shim adapts `folly::F14NodeMap` to the benchmark suite's
 * standardized API, enabling its inclusion in performance comparisons.
 *
 * Copyright (c) 2025-Present Gradient Dynamics LLC
 * Copyright (c) 2025-Present Nima Mehrani
 *
 * Distributed under the MIT License (see LICENSE file for details).
 *
 * @note This shim integrates with the C/C++ hash table benchmark suite by Jackson L. Allan
 *       and its usage in that context is subject to that project's copyright and license.
 * @note The underlying `folly::F14Map` library was created by Meta Platforms, Inc. and affiliates
 *       (https://github.com/facebook/folly) and is subject to its own copyright and license.
 *
 * @license MIT (see LICENSE file for details)
 */

#pragma once

/**
 * @note Inclusion of the Folly library is conditional due to its substantial
 *       impact on build times and binary size from extensive dependencies.
 *       To enable this shim, `HASH_BENCH_ENABLE_FOLLY` must be `1`, configured
 *       in `config.h` or via `-DHASH_BENCH_ENABLE_FOLLY=1` (for direct
 *       compilation). Note that enabling Folly may mandate dynamic linking for the
 *       benchmark executable (use of `-static` linker flag may not work).
 */
#if defined(HASH_BENCH_ENABLE_FOLLY) && HASH_BENCH_ENABLE_FOLLY == 1
  #include <folly/container/F14Map.h>
  #include <type_traits> // Required for std::is_nothrow_invocable_v, std::true_type

/**
 * @brief Adapter that conforms `folly::F14NodeMap` to the benchmark API.
 * @tparam blueprint A type providing key_type, value_type, hash_key, and cmpr_keys.
 */
template< typename blueprint > struct folly_f14_node
{
  /// @brief Adapts the blueprint's hash function.
  struct hash
  {
    /// Signals to the benchmark that `blueprint::hash_key` is high-quality.
    /// Folly F14 handles mixing internally; `is_avalanching = void` aligns with this.
    using is_avalanching = void;
    /// Folly-specific trait: `std::true_type` indicates the hash is avalanching,
    /// allowing Folly's internal mixing to be skipped.
    using folly_is_avalanching = std::true_type;

    /// Forwards to the blueprint's hash function; noexcept if blueprint's function is noexcept.
    std::size_t operator()( const typename blueprint::key_type &key ) const
        noexcept(std::is_nothrow_invocable_v<decltype(blueprint::hash_key), const typename blueprint::key_type &>)
    {
      return blueprint::hash_key( key );
    }
  };

  /// @brief Adapts the blueprint's key comparison function.
  struct cmpr
  {
    /// Forwards to the blueprint's key comparison function; noexcept if blueprint's function is noexcept.
    bool operator()( const typename blueprint::key_type &key_1, const typename blueprint::key_type &key_2 ) const
        noexcept(std::is_nothrow_invocable_v<decltype(blueprint::cmpr_keys), const typename blueprint::key_type &, const typename blueprint::key_type &>)
    {
      return blueprint::cmpr_keys( key_1, key_2 );
    }
  };

  /// The specialized hash table type.
  using table_type = folly::F14NodeMap<
    typename blueprint::key_type,
    typename blueprint::value_type,
    hash,
    cmpr
    // Uses folly::f14::DefaultAlloc by default
  >;
  /// The iterator for the hash table.
  using iterator_type = typename table_type::iterator;

  //===------------------------------------------------------------------===//
  //                        Benchmark API Methods
  //===------------------------------------------------------------------===//

  static table_type create_table()
  {
    table_type table;
    // F14NodeMap's Policy::kMaxLoadFactor is 1.0.
    // The actual max_load_factor will be min(MAX_LOAD_FACTOR, Policy::kMaxLoadFactor).
    // This means F14NodeMap will fully respect the benchmark's global MAX_LOAD_FACTOR.
    table.max_load_factor( MAX_LOAD_FACTOR );
    return table;
  }

  static iterator_type find( table_type &table, const typename blueprint::key_type &key )
  {
    return table.find( key );
  }

  static void insert( table_type &table, const typename blueprint::key_type &key )
  {
    // Using insert_or_assign to avoid redundant default-construction+assignment
    // when inserting new elements, which operator[] would incur.
    table.insert_or_assign(key, typename blueprint::value_type{});
  }

  static void erase( table_type &table, const typename blueprint::key_type &key )
  {
    table.erase( key );
  }

  static iterator_type begin_itr( table_type &table )
  {
    return table.begin();
  }

  static bool is_itr_valid( table_type &table, iterator_type &itr )
  {
    return itr != table.end();
  }

  static void increment_itr( table_type &table /*unused*/, iterator_type &itr )
  {
    (void)table; // Unused
    ++itr;
  }

  static const typename blueprint::key_type &get_key_from_itr( table_type &table /*unused*/, iterator_type &itr )
  {
    (void)table; // Unused
    return itr->first;
  }

  static const typename blueprint::value_type &get_value_from_itr( table_type &table /*unused*/, iterator_type &itr )
  {
    static_assert(!std::is_same_v<typename blueprint::value_type, std::nullptr_t>,
                      "blueprint::value_type cannot be std::nullptr_t for value iteration. "
                      "Use an empty struct for set-like behavior.");
    (void)table; // Unused
    return itr->second;
  }

  static void destroy_table( table_type &table /*unused*/ )
  {
    (void)table; // Unused
    // F14 utilizes RAII; its destructor handles cleanup.
  }
};

/**
 * @brief Metadata specialization for benchmark reporting.
 */
template<> struct folly_f14_node< void > // Corrected template specialization syntax
{
  /// The official name for display in plots and reports.
  static constexpr const char *label = "folly_f14_node";
  /// The color used for this table in generated plots (RGB).
  static constexpr const char *color = "rgb( 0, 191, 255 )"; // DeepSkyBlue
  /// Indicates whether the table uses a tombstone-like mechanism for deletions.
  static constexpr bool tombstone_like_mechanism = true;
};

#endif