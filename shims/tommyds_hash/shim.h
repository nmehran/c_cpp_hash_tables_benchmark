/**
 * @file shims/tommyds_hash/shim.h
 * @brief This shim adapts the C-based `tommy_hashtbl` from TommyDS to the
 *        benchmark suite's standardized C++ API.
 *
 * @copyright Copyright (c) 2025-Present Gradient Dynamics LLC
 * @copyright Copyright (c) 2025-Present Nima Mehrani
 *
 * @note This shim integrates with the C/C++ hash table benchmark suite by Jackson L. Allan
 *       and its usage in that context is subject to that project's copyright and license.
 * @note The underlying TommyDS library was created by Andrea Mazzoleni
 *       (http://www.tommyds.it/) and is subject to its own copyright and license.
 *
 * @license MIT (see LICENSE file for details)
 */

#pragma once

// Emit a compile-time advisory that this table is fixed-size.
#if !defined(TOMMYDS_HASH_FIXED_SIZE_ADVISORY_EMITTED)
    #define TOMMYDS_HASH_FIXED_SIZE_ADVISORY_EMITTED
    #define TOMMYDS_HASH_ADVISORY_MSG \
        "tommyds_tbl Shim Advisory: This table is fixed-size and is initialized using the benchmark's global KEY_COUNT. Performance will degrade if KEY_COUNT exceeds the effective capacity."
    #if defined(_MSC_VER)
        #pragma message(TOMMYDS_HASH_ADVISORY_MSG)
    #elif defined(__GNUC__) || defined(__clang__)
        #pragma message TOMMYDS_HASH_ADVISORY_MSG
    #endif
#endif // TOMMYDS_HASH_FIXED_SIZE_ADVISORY_EMITTED

#include <stdexcept>
// NOLINTNEXTLINE(bugprone-suspicious-include)
#include "tommyds/tommy.cc" // TommyDS unity build file (C linkage)

// Forward declare blueprint structs (definitions are provided by the benchmark framework)
struct uint32_uint32_murmur;
struct uint64_struct448_murmur;
struct cstring_uint64_fnv1a;

// ==========================================================================
//          tommyds_hash Shim Implementation
// ==========================================================================

template <typename Blueprint>
struct tommyds_hash {
public:
    using blueprint = Blueprint;
    /// TommyDS tables are manipulated via a pointer to their control struct.
    using table_type = tommy_hashtable*;

private:
    // --- Private Helper Structs and Functions ---

    /// @brief The structure stored in the hash table, containing the key-value pair and the TommyDS node.
    struct Entry {
        typename blueprint::key_type key_k;
        typename blueprint::value_type value_v;
        tommy_hashtable_node node; // The node required by TommyDS.

        Entry(const typename blueprint::key_type& k, const typename blueprint::value_type& v) : key_k(k), value_v(v),
            node() {
        }

        // This entry type is dynamically allocated and managed by the shim, so it should be non-copyable/movable.
        Entry(const Entry&) = delete;
        Entry& operator=(const Entry&) = delete;
        Entry(Entry&&) = delete;
        Entry& operator=(Entry&&) = delete;
    };

    /// @brief C-style callback for tommy_hashtable_foreach_arg to free Entry allocations during destruction.
    static void free_entry_cb(void* /*arg*/, void* data_ptr) {
        delete static_cast<Entry*>(data_ptr);
    }

    /// @brief C-style comparison function adapter for tommy_hashtable_remove.
    /// @return 0 for a match, non-zero otherwise.
    static int compare_keys_cb(const void* cmp_arg_key, const void* data_ptr) {
        const auto* key_to_find = static_cast<const typename blueprint::key_type*>(cmp_arg_key);
        const auto* entry = static_cast<const Entry*>(data_ptr);
        return blueprint::cmpr_keys(*key_to_find, entry->key_k) ? 0 : 1;
    }

    /// @brief A custom iterator to traverse the tommyds_hash table.
    /// @note This iterator is invalidated by any modification (insert/erase) to the table.
    struct Iterator {
        table_type table_ptr;
        tommy_size_t bucket_idx;
        tommy_hashtable_node* node_ptr;

        explicit Iterator(table_type table = nullptr) : table_ptr(table), bucket_idx(0), node_ptr(nullptr) {}

        /**
         * @brief Advances the iterator to the next valid element in the table.
         *
         * This method first attempts to move to the next node in the current bucket's chain.
         * If the chain is exhausted, it scans subsequent buckets until it finds a non-empty one,
         * at which point it positions the iterator at the head of that new bucket's chain.
         */
        void advance() {
            if (!table_ptr) {
                node_ptr = nullptr;
                return;
            }

            // If we are on a valid node, move to the next in the same bucket chain.
            if (node_ptr) {
                node_ptr = node_ptr->next;
            }

            // If we are at the end of a chain (or starting from scratch), find the next non-empty bucket.
            while (!node_ptr) {
                if (bucket_idx >= table_ptr->bucket_max) {
                    node_ptr = nullptr; // Reached the end of all buckets.
                    return;
                }
                node_ptr = table_ptr->bucket[bucket_idx++];
            }
        }

        Iterator& operator++() {
            advance();
            return *this;
        }

        bool operator!=(const Iterator& other) const {
            return node_ptr != other.node_ptr;
        }

        const Entry* get_entry() const {
            if (!node_ptr) return nullptr;
            // The `data` pointer in the node points back to our `Entry` struct.
            return static_cast<const Entry*>(node_ptr->data);
        }
    };

public:
    using itr_type = Iterator;

    //===------------------------------------------------------------------===//
    //                        Benchmark API Methods
    //===------------------------------------------------------------------===//

    static table_type create_table() {
        // Since tommy_hashtbl is fixed-size, we initialize it with the benchmark's
        // global KEY_COUNT. tommy_hashtable_init will round this up to the next
        // power of two for the actual bucket count. See advisory above.
        const auto table = new tommy_hashtable();
        tommy_hashtable_init(table, KEY_COUNT);
        return table;
    }

    /**
     * @brief Implements "insert-or-update" semantics for the benchmark.
     *
     * `tommy_hashtable_insert` is a "multi-map" style insert; it unconditionally
     * adds a new element. To adhere to the benchmark's unique-key map semantics,
     * we must first manually search for the key. If found, we update the value
     * in-place. If not found, we proceed with inserting a new element.
     */
    static void insert(table_type& table, const typename blueprint::key_type& key) {
        const tommy_hash_t hash = static_cast<tommy_hash_t>(blueprint::hash_key(key));
        tommy_hashtable_node* node = tommy_hashtable_bucket(table, hash);
        while (node) {
            // First check the full hash to minimize expensive key comparisons.
            if (node->index == hash) {
                if (auto* entry = static_cast<Entry*>(node->data); blueprint::cmpr_keys(key, entry->key_k)) {
                    // Key found. Update the value in place.
                    entry->value_v = typename blueprint::value_type{};
                    return;
                }
            }
            node = node->next;
        }
        // Key not found, so create and insert a new entry.
        Entry* new_entry = new Entry(key, typename blueprint::value_type{});
        tommy_hashtable_insert(table, &new_entry->node, new_entry, hash);
    }

    static void erase(table_type& table, const typename blueprint::key_type& key) {
        const tommy_hash_t hash = static_cast<tommy_hash_t>(blueprint::hash_key(key));
        // tommy_hashtable_remove searches, unlinks, and returns the data pointer.
        if (void* removed_data = tommy_hashtable_remove(table, compare_keys_cb, &key, hash)) {
            // We are responsible for freeing the memory of the Entry struct.
            delete static_cast<Entry*>(removed_data);
        }
    }

    /**
     * @brief Finds an element and returns an iterator to it.
     *
     * TommyDS search functions return a `void*` to the user's data, not the
     * internal node required for an iterator. Therefore, we must manually
     * traverse the bucket chain to locate the node and construct a valid iterator.
     */
    static itr_type find(table_type& table, const typename blueprint::key_type& key) {
        const tommy_hash_t hash = static_cast<tommy_hash_t>(blueprint::hash_key(key));
        const tommy_size_t bucket_idx = hash & table->bucket_mask;
        tommy_hashtable_node* node = table->bucket[bucket_idx];

        while (node) {
            if (node->index == hash) {
                if (const auto* entry = static_cast<const Entry*>(node->data); blueprint::cmpr_keys(key, entry->key_k)) {
                    // Found: construct an iterator pointing to this specific node.
                    itr_type found_itr(table);
                    found_itr.node_ptr = node;
                    // Set the bucket index to the *next* bucket to probe. This ensures that
                    // if ++itr is called and this is the last node in the chain, the
                    // iterator correctly resumes its scan from the next bucket.
                    found_itr.bucket_idx = bucket_idx + 1;
                    return found_itr;
                }
            }
            node = node->next;
        }
        // Not found: return an end iterator (represented by a default-constructed iterator).
        return itr_type();
    }

    static itr_type begin_itr(table_type& table) {
        itr_type itr(table);
        // Position the iterator at the first valid element in the table.
        itr.advance();
        return itr;
    }

    static bool is_itr_valid(table_type& /*table*/, itr_type& itr) {
        // An iterator is valid if its internal node pointer is not null.
        return itr.node_ptr != nullptr;
    }

    static void increment_itr(table_type& /*table*/, itr_type& itr) {
        // The iterator's advance() method handles all logic for finding the next element.
        ++itr;
    }

    static const typename blueprint::key_type& get_key_from_itr(table_type& /*table*/, itr_type& itr) {
        const Entry* entry = itr.get_entry();
        if (!entry) throw std::logic_error("Dereferencing invalid tommyds_hash iterator");
        return entry->key_k;
    }

    static const typename blueprint::value_type& get_value_from_itr(table_type& /*table*/, itr_type& itr) {
        const Entry* entry = itr.get_entry();
        if (!entry) throw std::logic_error("Dereferencing invalid tommyds_hash iterator");
        return entry->value_v;
    }

    static void destroy_table(table_type& table) {
        // tommy_hashtable_done only frees the bucket array, not the elements.
        // We must iterate through all elements and free our custom Entry structs first.
        tommy_hashtable_foreach_arg(table, free_entry_cb, nullptr);
        tommy_hashtable_done(table);
        delete table;
        table = nullptr; // Set the caller's pointer to null after deletion.
    }
};

/**
 * @brief Metadata specialization for benchmark reporting.
 */
template <>
struct tommyds_hash<void> {
    /// The official name for display in plots and reports.
    static constexpr const char* label = "tommyds_hash";
    /// The color used for this table in generated plots (RGB).
    static constexpr const char* color = "rgb( 244, 164, 96 )"; // SandyBrown
    /// Indicates that tommyds_hash does not use tombstones. Deletion involves direct node removal.
    static constexpr bool tombstone_like_mechanism = false;
};

// --- Instantiate tommyds_hash for Each Enabled Blueprint ---
#ifdef UINT32_UINT32_MURMUR_ENABLED
template class tommyds_hash<uint32_uint32_murmur>;
#endif
#ifdef UINT64_STRUCT448_MURMUR_ENABLED
template class tommyds_hash<uint64_struct448_murmur>;
#endif
#ifdef CSTRING_UINT64_FNV1A_ENABLED
template class tommyds_hash<cstring_uint64_fnv1a>;
#endif