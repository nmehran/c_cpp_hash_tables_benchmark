/**
 * @file shims/tommyds_lin/shim.h
 * @brief This shim adapts the C-based `tommy_hashlin` from TommyDS to the
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

// Emit a compile-time advisory that this table uses a fixed internal load factor.
#if !defined(TOMMYDS_LIN_LOAD_FACTOR_ADVISORY_EMITTED)
    #define TOMMYDS_LIN_LOAD_FACTOR_ADVISORY_EMITTED
    #define TOMMYDS_LIN_ADVISORY_MSG \
        "tommyds_lin Shim Advisory: This table uses a fixed internal load factor policy (grows >0.5, shrinks <0.125). The benchmark's global MAX_LOAD_FACTOR is not applied."
    #if defined(_MSC_VER)
        #pragma message(TOMMYDS_LIN_ADVISORY_MSG)
    #elif defined(__GNUC__) || defined(__clang__)
        #pragma message TOMMYDS_LIN_ADVISORY_MSG
    #endif
#endif // TOMMYDS_LIN_LOAD_FACTOR_ADVISORY_EMITTED

#include <stdexcept>
// NOLINTNEXTLINE(bugprone-suspicious-include)
#include "third_party/tommyds/tommy.cc" // TommyDS unity build file (C linkage)

// Forward declare blueprint structs (definitions are provided by the benchmark framework)
struct uint32_uint32_murmur;
struct uint64_struct448_murmur;
struct cstring_uint64_fnv1a;

// ==========================================================================
//          tommyds_lin Shim Implementation
// ==========================================================================

template <typename Blueprint>
struct tommyds_lin {
public:
    using blueprint = Blueprint;
    /// TommyDS tables are manipulated via a pointer to their control struct.
    using table_type = tommy_hashlin*;

private:
    // --- Private Helper Structs and Functions ---

    /// @brief The structure stored in the hash table, containing the key-value pair and the TommyDS node.
    struct Entry {
        typename blueprint::key_type key_k;
        typename blueprint::value_type value_v;
        tommy_hashlin_node node; // The node required by TommyDS.

        Entry(const typename blueprint::key_type& k, const typename blueprint::value_type& v) : key_k(k), value_v(v),
            node() {
        }

        // This entry type is dynamically allocated and managed by the shim, so it should be non-copyable/movable.
        Entry(const Entry&) = delete;
        Entry& operator=(const Entry&) = delete;
        Entry(Entry&&) = delete;
        Entry& operator=(Entry&&) = delete;
    };

    /// @brief C-style callback for tommy_hashlin_foreach_arg to free Entry allocations during destruction.
    static void free_entry_cb(void* /*arg*/, void* data_ptr) {
        delete static_cast<Entry*>(data_ptr);
    }

    /// @brief C-style comparison function adapter for tommy_hashlin_remove.
    /// @return 0 for a match, non-zero otherwise.
    static int compare_keys_cb(const void* cmp_arg_key, const void* data_ptr) {
        const auto* key_to_find = static_cast<const typename blueprint::key_type*>(cmp_arg_key);
        const auto* entry = static_cast<const Entry*>(data_ptr);
        return blueprint::cmpr_keys(*key_to_find, entry->key_k) ? 0 : 1;
    }

    /// @brief A custom iterator to traverse the tommyds_lin table.
    /// @note This iterator is invalidated by any modification (insert/erase) to the table.
    struct Iterator {
        table_type table_ptr;
        tommy_size_t bucket_idx;
        tommy_hashlin_node* node_ptr;

        explicit Iterator(table_type table = nullptr) : table_ptr(table), bucket_idx(0), node_ptr(nullptr) {}

        /**
         * @brief Advances the iterator to the next valid element in the table.
         *
         * This method first attempts to move to the next node in the current bucket's chain.
         * If the chain is exhausted, it scans subsequent active buckets until it finds a
         * non-empty one, at which point it positions the iterator at its head.
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
                // The number of active buckets is the sum of the lower half and the split part of the upper half.
                tommy_size_t active_bucket_count = table_ptr->low_max + table_ptr->split;
                if (bucket_idx >= active_bucket_count) {
                    node_ptr = nullptr; // Reached the end of all active buckets.
                    return;
                }
                // *tommy_hashlin_pos() returns the head of the bucket chain.
                node_ptr = *tommy_hashlin_pos(table_ptr, bucket_idx++);
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
        // tommy_hashlin is dynamically sized and manages its own memory.
        const auto table = new tommy_hashlin();
        tommy_hashlin_init(table);
        return table;
    }

    /**
     * @brief Implements "insert-or-update" semantics for the benchmark.
     *
     * `tommy_hashlin_insert` is a "multi-map" style insert. To adhere to the
     * benchmark's unique-key semantics, we must first manually search for the
     * key. If found, we update the value in-place. If not, we insert a new element.
     */
    static void insert(table_type& table, const typename blueprint::key_type& key) {
        const tommy_hash_t hash = static_cast<tommy_hash_t>(blueprint::hash_key(key));
        tommy_hashlin_node* node = tommy_hashlin_bucket(table, hash);
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
        tommy_hashlin_insert(table, &new_entry->node, new_entry, hash);
    }

    static void erase(table_type& table, const typename blueprint::key_type& key) {
        const tommy_hash_t hash = static_cast<tommy_hash_t>(blueprint::hash_key(key));
        // tommy_hashlin_remove searches, unlinks, and returns the data pointer.
        if (void* removed_data = tommy_hashlin_remove(table, compare_keys_cb, &key, hash)) {
            // We are responsible for freeing the memory of the Entry struct.
            delete static_cast<Entry*>(removed_data);
        }
    }

    /**
     * @brief Finds an element and returns an iterator to it.
     *
     * TommyDS search functions return a `void*` to the user's data, not the
     * internal node required for an iterator. We manually reimplement the bucket
     * lookup and chain traversal to locate the node and construct a valid iterator.
     */
    static itr_type find(table_type& table, const typename blueprint::key_type& key) {
        const tommy_hash_t hash = static_cast<tommy_hash_t>(blueprint::hash_key(key));

        // Replicate logic from tommy_hashlin_bucket_ref to find the correct bucket index.
        tommy_size_t bucket_idx;
        {
            tommy_size_t pos = hash & table->low_mask;
            if (pos < table->split) {
                pos = hash & table->bucket_mask;
            }
            bucket_idx = pos;
        }

        tommy_hashlin_node* node = *tommy_hashlin_pos(table, bucket_idx);

        while (node) {
            if (node->index == hash) {
                if (const auto* entry = static_cast<const Entry*>(node->data); blueprint::cmpr_keys(key, entry->key_k)) {
                    // Found: construct iterator pointing to this node.
                    itr_type found_itr(table);
                    found_itr.node_ptr = node;
                    // Set bucket_idx to the *next* bucket for subsequent increments.
                    found_itr.bucket_idx = bucket_idx + 1;
                    return found_itr;
                }
            }
            node = node->next;
        }
        // Not found: return an end iterator.
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
        if (!entry) throw std::logic_error("Dereferencing invalid tommyds_lin iterator");
        return entry->key_k;
    }

    static const typename blueprint::value_type& get_value_from_itr(table_type& /*table*/, itr_type& itr) {
        const Entry* entry = itr.get_entry();
        if (!entry) throw std::logic_error("Dereferencing invalid tommyds_lin iterator");
        return entry->value_v;
    }

    static void destroy_table(table_type& table) {
        // tommy_hashlin_done only frees the bucket array, not the elements.
        // We must iterate through all elements and free our custom Entry structs first.
        tommy_hashlin_foreach_arg(table, free_entry_cb, nullptr);
        tommy_hashlin_done(table);
        delete table;
        table = nullptr; // Set the caller's pointer to null after deletion.
    }
};

/**
 * @brief Metadata specialization for benchmark reporting.
 */
template <>
struct tommyds_lin<void> {
    /// The official name for display in plots and reports.
    static constexpr const char* label = "tommyds_lin";
    /// The color used for this table in generated plots (RGB).
    static constexpr const char* color = "rgb( 218, 165, 32 )"; // Goldenrod
    /// Indicates that tommyds_lin does not use tombstones. Deletion involves direct node removal.
    static constexpr bool tombstone_like_mechanism = false;
};

// --- Instantiate tommyds_lin for Each Enabled Blueprint ---
#ifdef UINT32_UINT32_MURMUR_ENABLED
template class tommyds_lin<uint32_uint32_murmur>;
#endif
#ifdef UINT64_STRUCT448_MURMUR_ENABLED
template class tommyds_lin<uint64_struct448_murmur>;
#endif
#ifdef CSTRING_UINT64_FNV1A_ENABLED
template class tommyds_lin<cstring_uint64_fnv1a>;
#endif