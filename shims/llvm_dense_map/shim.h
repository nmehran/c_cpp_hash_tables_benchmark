/**
 * @file shims/llvm_dense_map/shim.h
 * @brief This shim adapts `llvm::DenseMap` to the benchmark suite's
 * standardized API, enabling its inclusion in performance comparisons.
 *
 * @copyright Copyright (c) 2025-Present Gradient Dynamics LLC
 * @copyright Copyright (c) 2025-Present Nima Mehrani
 *
 * @note This shim integrates with the C/C++ hash table benchmark suite by Jackson L. Allan
 *       and its usage in that context is subject to that project's copyright and license.
 * @note The underlying `llvm::DenseMap` is part of the LLVM Project
 *       (https://github.com/llvm/llvm-project) and is subject to the
 *       Apache License v2.0 with LLVM Exceptions.
 *
 * @license Distributed under the MIT License (see the accompanying LICENSE file).
 */

#pragma once

/**
 * @note Inclusion of the LLVM library is conditional due to its substantial
 *       impact on build times and binary size from extensive dependencies.
 *       To enable this shim, `HASH_BENCH_ENABLE_LLVM` must be `1`, configured
 *       in `config.h` or via `-DHASH_BENCH_ENABLE_LLVM=1` (for direct
 *       compilation). Note that enabling LLVM may mandate dynamic linking for the
 *       benchmark executable (use of `-static` linker flag may not work).
 */
#if defined(HASH_BENCH_ENABLE_LLVM) && HASH_BENCH_ENABLE_LLVM == 1

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseMapInfo.h"

#include <cstddef>     // For std::size_t
#include <cstdint>     // For uintptr_t
#include <type_traits> // For std::is_same_v, std::is_nothrow_invocable_v, etc.
#include <utility>     // For std::declval, std::forward

// Emit a compile-time advisory that llvm::DenseMap's load factor is not
// configurable via the benchmark's MAX_LOAD_FACTOR setting.
#if !defined(LLVM_DENSEMAP_LOAD_FACTOR_ADVISORY_EMITTED)
    #define LLVM_DENSEMAP_LOAD_FACTOR_ADVISORY_EMITTED
    #define LLVM_DENSEMAP_ADVISORY_MESSAGE \
        "LLVM DenseMap Advisory: MAX_LOAD_FACTOR not directly applicable; DenseMap grows when load factor exceeds a fixed threshold of ~0.75."
    #if defined(_MSC_VER)
        #pragma message(LLVM_DENSEMAP_ADVISORY_MESSAGE)
    #elif defined(__GNUC__) || defined(__clang__)
        #pragma message LLVM_DENSEMAP_ADVISORY_MESSAGE
    #endif
#endif // LLVM_DENSEMAP_LOAD_FACTOR_ADVISORY_EMITTED

/**
 * @brief Provides the KeyInfoT trait required by `llvm::DenseMap`.
 *
 * @details This struct adapts the benchmark's generic `blueprint` to the specific
 * interface required by `llvm::DenseMapInfo`. It is responsible for providing
 * special "empty" and "tombstone" key values, as well as the hashing and
 * equality functions that `DenseMap` will use internally.
 *
 * It contains specialized logic for `char*` keys. This is critical because
 * the special empty/tombstone keys are specific pointer values that must be
 * compared by address, whereas user-provided keys are C-strings that must be
 * compared by content (e.g., via `strcmp`). This struct correctly dispatches
 * between these two comparison types. It also enables heterogeneous lookup,
 * allowing `find` with a `const char*` on a map that stores `char*` keys.
 *
 * @tparam blueprint The benchmark's `blueprint` type, providing key/value types and hash/compare functions.
 */
template <typename blueprint>
struct LlvmBlueprintDenseMapInfo {
    using ActualKeyT = typename blueprint::key_type;

    // Provide the special empty and tombstone key values required by DenseMap.
    static constexpr inline ActualKeyT getEmptyKey() noexcept(noexcept(llvm::DenseMapInfo<ActualKeyT>::getEmptyKey())) {
        return llvm::DenseMapInfo<ActualKeyT>::getEmptyKey();
    }

    static constexpr inline ActualKeyT getTombstoneKey() noexcept(noexcept(llvm::DenseMapInfo<ActualKeyT>::getTombstoneKey())) {
        return llvm::DenseMapInfo<ActualKeyT>::getTombstoneKey();
    }

    // --- Hashing for stored keys (ActualKeyT) ---
    template <typename K = ActualKeyT>
    static unsigned getHashValue_impl(const K& Val, std::true_type /* is_char_ptr */)
        noexcept(std::is_nothrow_invocable_v<decltype(blueprint::hash_key), const K&>) {
        if (Val == getEmptyKey() || Val == getTombstoneKey()) {
            // For special keys, hash the pointer value to avoid dereferencing null/invalid pointers.
            return static_cast<unsigned>(reinterpret_cast<uintptr_t>(Val));
        }
        return static_cast<unsigned>(blueprint::hash_key(Val)); // Delegate to blueprint for valid strings.
    }

    template <typename K = ActualKeyT>
    static unsigned getHashValue_impl(const K& Val, std::false_type /* is_not_char_ptr */)
        noexcept(std::is_nothrow_invocable_v<decltype(blueprint::hash_key), const K&>) {
        return static_cast<unsigned>(blueprint::hash_key(Val)); // Delegate directly to blueprint.
    }

    static unsigned getHashValue(const ActualKeyT &Val)
        noexcept(noexcept(getHashValue_impl(Val, std::bool_constant<std::is_same_v<ActualKeyT, char*>>{}))) {
        return getHashValue_impl(Val, std::bool_constant<std::is_same_v<ActualKeyT, char*>>{});
    }

    // --- Equality for stored keys (ActualKeyT vs ActualKeyT) ---
    template <typename K = ActualKeyT>
    static bool isEqual_impl(const K& LHS, const K& RHS, std::true_type /* is_char_ptr */)
        noexcept(std::is_nothrow_invocable_v<decltype(blueprint::cmpr_keys), const K&, const K&>) {
        bool lhsIsSpecial = (LHS == getEmptyKey() || LHS == getTombstoneKey());
        bool rhsIsSpecial = (RHS == getEmptyKey() || RHS == getTombstoneKey());

        if (lhsIsSpecial || rhsIsSpecial) {
            return LHS == RHS; // Use pointer comparison if either is a special key.
        }
        return blueprint::cmpr_keys(LHS, RHS); // Use content comparison for two valid C-strings.
    }

    template <typename K = ActualKeyT>
    static bool isEqual_impl(const K& LHS, const K& RHS, std::false_type /* is_not_char_ptr */)
        noexcept(std::is_nothrow_invocable_v<decltype(blueprint::cmpr_keys), const K&, const K&>) {
        const K emptyKey = getEmptyKey();
        const K tombstoneKey = getTombstoneKey();
        if (LHS == emptyKey) return RHS == emptyKey;
        if (LHS == tombstoneKey) return RHS == tombstoneKey;
        if (RHS == emptyKey || RHS == tombstoneKey) return false;

        return blueprint::cmpr_keys(LHS, RHS); // Delegate to blueprint for non-special keys.
    }

    static bool isEqual(const ActualKeyT &LHS, const ActualKeyT &RHS)
        noexcept(noexcept(isEqual_impl(LHS, RHS, std::bool_constant<std::is_same_v<ActualKeyT, char*>>{}))) {
        return isEqual_impl(LHS, RHS, std::bool_constant<std::is_same_v<ActualKeyT, char*>>{});
    }

    // --- Heterogeneous lookup: `const char*` on a `char*` key map ---
    template <typename LookupKeyT,
              std::enable_if_t<std::is_same_v<ActualKeyT, char*> && std::is_same_v<LookupKeyT, const char*>, int> = 0>
    static unsigned getHashValue(LookupKeyT Val_lookup)
        noexcept(std::is_nothrow_invocable_v<decltype(blueprint::hash_key), char* const&>) {
        // NOTE: const_cast is for compatibility with blueprints that may not be const-correct.
        return static_cast<unsigned>(blueprint::hash_key(const_cast<char*>(Val_lookup)));
    }

    template <typename LookupKeyT,
              std::enable_if_t<std::is_same_v<ActualKeyT, char*> && std::is_same_v<LookupKeyT, const char*>, int> = 0>
    static bool isEqual(LookupKeyT LHS_lookup, const ActualKeyT &RHS_stored)
        noexcept(std::is_nothrow_invocable_v<decltype(blueprint::cmpr_keys), char* const&, const ActualKeyT&>) {
        if (RHS_stored == getEmptyKey() || RHS_stored == getTombstoneKey()) {
            return false;
        }
        return blueprint::cmpr_keys(const_cast<char*>(LHS_lookup), RHS_stored);
    }
};

/**
 * @brief Adapter that conforms `llvm::DenseMap` to the benchmark API.
 * @tparam blueprint A type providing key_type, value_type, hash_key, and cmpr_keys.
 */
template<typename blueprint>
struct llvm_dense_map {
public:
    /// @brief Adapts the blueprint's hash function.
    struct hash {
        /// @brief Signals that the blueprint's hash function is high-quality.
        using is_avalanching = void;

        /// @brief Forwards to the blueprint's hash function.
        std::size_t operator()(const typename blueprint::key_type &key) const
            noexcept(std::is_nothrow_invocable_v<decltype(blueprint::hash_key), const typename blueprint::key_type &>) {
            return blueprint::hash_key(key);
        }
    };

    /// @brief Adapts the blueprint's key comparison function.
    struct cmpr {
        /// @brief Forwards to the blueprint's key comparison function.
        bool operator()(const typename blueprint::key_type &key_1, const typename blueprint::key_type &key_2) const
            noexcept(std::is_nothrow_invocable_v<decltype(blueprint::cmpr_keys), const typename blueprint::key_type &, const typename blueprint::key_type &>) {
            return blueprint::cmpr_keys(key_1, key_2);
        }
    };

    /// @brief KeyInfo provider for llvm::DenseMap, using our adapter.
    using key_info_provider = LlvmBlueprintDenseMapInfo<blueprint>;

    /// @brief The specialized llvm::DenseMap hash table type.
    using table_type = llvm::DenseMap<
        typename blueprint::key_type,
        typename blueprint::value_type,
        key_info_provider
    >;

    /// @brief The iterator for the hash table.
    using iterator_type = typename table_type::iterator;

    //===------------------------------------------------------------------===//
    //                        Benchmark API Methods
    //===------------------------------------------------------------------===//

    /// @brief Creates an empty table.
    static table_type create_table() noexcept(std::is_nothrow_default_constructible_v<table_type>) {
        // llvm::DenseMap does not support a user-specified max load factor. It grows
        // when the load factor exceeds a fixed threshold (~0.75). The advisory
        // message at the top of this file informs the user of this.
        return table_type{};
    }

    /// @brief Finds a key in the table.
    static iterator_type find(table_type &table, const typename blueprint::key_type &key) {
        return table.find(key);
    }

    /// @brief Inserts a key with a default-constructed value.
    static void insert(table_type &table, const typename blueprint::key_type &key) {
        // Use insert_or_assign for efficient "insert or update" semantics.
        // This is superior to `table[key] = value_type{}` as it avoids a
        // default-construction followed by an assignment for newly inserted elements.
        table.insert_or_assign(key, typename blueprint::value_type{});
    }

    /// @brief Erases a key from the table.
    static void erase(table_type &table, const typename blueprint::key_type &key) {
        table.erase(key);
    }

    /// @brief Returns an iterator to the beginning of the table.
    static iterator_type begin_itr(table_type &table) {
        return table.begin();
    }

    /// @brief Checks if an iterator is still valid (i.e., not at the end).
    static bool is_itr_valid(table_type &table, iterator_type &itr) {
        return itr != table.end();
    }

    /// @brief Advances the iterator to the next element.
    static void increment_itr(table_type & /*table*/, iterator_type &itr) {
        // The iterator's operator++ correctly advances to the next valid element,
        // internally skipping over any empty or tombstone buckets.
        ++itr;
    }

    /// @brief Retrieves the key from the element pointed to by the iterator.
    static const typename blueprint::key_type &get_key_from_itr(table_type & /*table*/, iterator_type &itr) {
        // DenseMap iterators dereference to a pair-like bucket (DenseMapPair),
        // which provides getFirst() to access the key.
        return itr->getFirst();
    }

    /// @brief Retrieves the value from the element pointed to by the iterator.
    static const typename blueprint::value_type &get_value_from_itr(table_type & /*table*/, iterator_type &itr) {
        static_assert(!std::is_same_v<typename blueprint::value_type, std::nullptr_t>,
                      "blueprint::value_type cannot be std::nullptr_t for value iteration. "
                      "Use an empty struct for set-like behavior.");
        // The bucket's getSecond() method provides access to the value.
        return itr->getSecond();
    }

    /// @brief Destroys the table (a no-op due to RAII).
    static void destroy_table(table_type & /*table*/) {
        // llvm::DenseMap utilizes RAII; its destructor handles all cleanup.
    }
};

/**
 * @brief Metadata specialization for benchmark reporting.
 */
template<>
struct llvm_dense_map<void> {
    /// @brief The official name for display in plots and reports.
    static constexpr const char *label = "llvm_dense";
    /// @brief The color used for this table in generated plots (RGB).
    static constexpr const char *color = "rgb( 112, 128, 144 )"; // SlateGray
    /// @brief Indicates whether the table uses a tombstone-like mechanism for deletions.
    static constexpr bool tombstone_like_mechanism = true;
};

#endif // defined(HASH_BENCH_ENABLE_LLVM) && HASH_BENCH_ENABLE_LLVM == 1