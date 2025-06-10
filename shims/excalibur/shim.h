/**
 * @file shims/excalibur/shim.h
 * @brief This shim adapts `Excalibur::HashTable` to the benchmark suite's
 * standardized API, enabling its inclusion in performance comparisons.
 *
 * @copyright Copyright (c) 2025-Present Gradient Dynamics LLC
 * @copyright Copyright (c) 2025-Present Nima Mehrani
 *
 * @note This shim integrates with the C/C++ hash table benchmark suite by Jackson L. Allan
 *       and its usage in that context is subject to that project's copyright and license.
 * @note The underlying `Excalibur::HashTable` library was created by Sergey Makeev
 *       (https://github.com/SergeyMakeev/ExcaliburHash) and is subject to its own copyright and license.
 *
 * @license MIT (see LICENSE file for details)
 */

#pragma once

// Standard library includes needed for KeyInfo specializations and the adapter
#include <cstring>     // For std::strcmp in the char* KeyInfo specialization
#include <string_view> // For std::hash<std::string_view> in the char* KeyInfo specialization
#include <functional>  // For std::hash
#include <type_traits> // For std::is_same_v, std::is_nothrow_invocable_v

// Include the main Excalibur hash table header. This must be done first so that the
// KeyInfo primary template is declared before we attempt to specialize it.
#include "ExcaliburHash/ExcaliburHash.h"

// Emit a compile-time advisory that this table requires a KeyInfo specialization.
#if !defined(EXCALIBUR_KEYINFO_ADVISORY_EMITTED)
    #define EXCALIBUR_KEYINFO_ADVISORY_EMITTED
    #define EXCALIBUR_ADVISORY_MSG \
        "Excalibur Shim Advisory: This table requires a `Excalibur::KeyInfo<KeyType>` specialization for each key type. The library provides them for integral types; this shim adds one for `char*`."
    #if defined(_MSC_VER)
        #pragma message(EXCALIBUR_ADVISORY_MSG)
    #elif defined(__GNUC__) || defined(__clang__)
        #pragma message EXCALIBUR_ADVISORY_MSG
    #endif
#endif // EXCALIBUR_KEYINFO_ADVISORY_EMITTED


namespace Excalibur {
namespace detail_char_sentinels {
    // These static chars provide unique memory addresses to serve as sentinel pointers for char* keys.
    // The values are chosen from ranges invalid in UTF-8 to minimize collision risk with valid data.
    static const char empty_marker_char_for_excalibur_shim = '\xFE';
    static const char tombstone_marker_char_for_excalibur_shim = '\xFF';
} // namespace detail_char_sentinels

/**
 * @brief Specialization of the `KeyInfo` trait for `char*` keys.
 * @details The Excalibur library does not provide a default `KeyInfo` for `char*`. We define
 * one here to support C-string blueprints from the benchmark suite. It provides the necessary
 * sentinel values and default hashing/comparison logic.
 */
template <>
struct KeyInfo<char*> {
    /// @brief A key is valid if it is not null and not a sentinel pointer.
    static inline bool isValid(const char* key) noexcept {
        return key != nullptr && key != getEmpty() && key != getTombstone();
    }

    /// @brief Returns the unique pointer address representing a deleted slot (tombstone).
    static inline char* getTombstone() noexcept {
        return const_cast<char*>(&detail_char_sentinels::tombstone_marker_char_for_excalibur_shim);
    }

    /// @brief Returns the unique pointer address representing an empty slot.
    static inline char* getEmpty() noexcept {
        return const_cast<char*>(&detail_char_sentinels::empty_marker_char_for_excalibur_shim);
    }

    /// @brief Hashes a valid C-string key using `std::hash<std::string_view>`.
    static inline size_t hash(const char* key) noexcept {
        if (!isValid(key)) return 0;
        return std::hash<std::string_view>{}(key);
    }

    /// @brief Compares two C-string keys. If both are valid, uses `strcmp`.
    static inline bool isEqual(const char* lhs, const char* rhs) noexcept {
        if (isValid(lhs) && isValid(rhs)) {
             return std::strcmp(lhs, rhs) == 0;
        }
        return lhs == rhs;
    }
};
} // namespace Excalibur

namespace excalibur_shim_detail {
/**
 * @brief Adapts the benchmark's `blueprint` to the `Excalibur::KeyInfo` trait.
 * @details This is the central customization point for Excalibur. It acts as a bridge:
 * - For sentinel values (empty/tombstone), it forwards to the appropriate
 *   `Excalibur::KeyInfo<KeyType>` specialization (either from the library or our shim).
 * - For hashing and comparison of valid keys, it forwards to the benchmark's
 *   `blueprint::hash_key` and `blueprint::cmpr_keys` functions.
 */
template<typename CurrentBlueprint>
struct KeyInfo_Adapter {
    using KeyType = typename CurrentBlueprint::key_type;

    static inline KeyType getTombstone() noexcept { return Excalibur::KeyInfo<KeyType>::getTombstone(); }
    static inline KeyType getEmpty() noexcept { return Excalibur::KeyInfo<KeyType>::getEmpty(); }
    static inline bool isValid(const KeyType& key) noexcept { return Excalibur::KeyInfo<KeyType>::isValid(key); }

    static inline size_t hash(const KeyType& key) noexcept(
        std::is_nothrow_invocable_v<decltype(CurrentBlueprint::hash_key), const KeyType&>
    ) {
        return CurrentBlueprint::hash_key(key);
    }

    static inline bool isEqual(const KeyType& lhs, const KeyType& rhs) noexcept(
        std::is_nothrow_invocable_v<decltype(CurrentBlueprint::cmpr_keys), const KeyType&, const KeyType&>
    ) {
        // Fast-path for identity; avoids expensive `cmpr_keys` call on success.
        if (lhs == rhs) {
            return true;
        }

        // If pointers/values differ, check if both keys are valid application keys.
        // If so, use the blueprint's full, potentially expensive, comparison logic.
        if (Excalibur::KeyInfo<KeyType>::isValid(lhs) && Excalibur::KeyInfo<KeyType>::isValid(rhs)) {
            return CurrentBlueprint::cmpr_keys(lhs, rhs);
        }

        // If we reach here, at least one key is a sentinel or null, and their
        // pointers/values are not equal, so the keys cannot be equal.
        return false;
    }
};
} // namespace excalibur_shim_detail

/**
 * @brief Adapter that conforms `Excalibur::HashTable` to the benchmark API.
 * @tparam blueprint A type providing key_type, value_type, hash_key, and cmpr_keys.
 */
template<typename blueprint>
struct excalibur {
public:
    /// @brief The specialized hash table type.
    /// @note Unlike many other tables, Excalibur is not customized with `Hash` and `KeyEqual`
    ///       template parameters. Instead, all key-related operations are bundled into the
    ///       `TKeyInfo` trait, which we provide via our `excalibur_shim_detail::KeyInfo_Adapter`.
    using table_type = Excalibur::HashTable<
        typename blueprint::key_type,
        typename blueprint::value_type,
        1, // kNumInlineItems: A small default, as the table grows dynamically.
        excalibur_shim_detail::KeyInfo_Adapter<blueprint>
    >;
    /// @brief The iterator for the hash table.
    using iterator_type = typename table_type::IteratorKV;

    //===------------------------------------------------------------------===//
    //                        Benchmark API Methods
    //===------------------------------------------------------------------===//

    static table_type create_table() {
        return table_type{};
    }

    static iterator_type find(table_type &table, const typename blueprint::key_type &key) {
        return table.find(key);
    }

    static void insert(table_type &table, const typename blueprint::key_type &key) {
        // Use operator[] to ensure "insert or update" semantics, as required by the benchmark.
        // Excalibur's operator[] finds or creates the element, and the assignment
        // updates the value to a default-constructed one.
        table[key] = typename blueprint::value_type{};
    }

    static void erase(table_type &table, const typename blueprint::key_type &key) {
        table.erase(key);
    }

    static iterator_type begin_itr(table_type &table) {
        // `ibegin` returns an iterator over key-value pairs.
        return table.ibegin();
    }

    static bool is_itr_valid(table_type &table, iterator_type &itr) {
        return itr != table.iend();
    }

    static void increment_itr(table_type & /*table*/, iterator_type &itr) {
        // The table parameter is unused but retained for API compatibility.
        // Excalibur's `operator++` is implemented to scan past empty/tombstone slots.
        ++itr;
    }

    static const typename blueprint::key_type &get_key_from_itr(table_type & /*table*/, iterator_type &itr) {
        return itr->first;
    }

    static const typename blueprint::value_type &get_value_from_itr(table_type & /*table*/, iterator_type &itr) {
        static_assert(!std::is_same_v<typename blueprint::value_type, std::nullptr_t>,
                      "excalibur shim: blueprint::value_type cannot be std::nullptr_t "
                      "for item iteration. Use an empty struct for set-like behavior.");
        return itr->second;
    }

    static void destroy_table(table_type & /*table*/) {
        // Excalibur::HashTable handles its own memory via RAII; its destructor handles cleanup.
    }
};

/**
 * @brief Metadata specialization for benchmark reporting.
 */
template<>
struct excalibur<void> {
    /// @brief The official name for display in plots and reports.
    static constexpr const char *label = "excalibur";
    /// @brief The color used for this table in generated plots (RGB).
    static constexpr const char *color = "rgb(255, 127, 80)"; // Coral
    /// @brief Indicates whether the table uses a tombstone-like mechanism for deletions.
    static constexpr bool tombstone_like_mechanism = true;
};