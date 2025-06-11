#pragma once
// third_party/tommyds/tommy.cc

/**
 * This file is used to compile the necessary TommyDS C source files
 * directly into the C++ benchmark executable, avoiding the need to
 * separately compile and link TommyDS object files.
 *
 * This is a "unity build" or "jumbo build" approach for TommyDS.
 */
// Wrap in extern "C" when including into C++
// to ensure C linkage for the definitions and prevent C++ name mangling
// on the C functions.
extern "C" {
    // Push GCC diagnostic state to ignore warnings that are common
    // when compiling C code as C++ or that might be present in TommyDS.
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wpedantic"
    #pragma GCC diagnostic ignored "-Wunused-parameter"
    #pragma GCC diagnostic ignored "-Wunused-function" // Some static helpers might not be used by all configs
    #pragma GCC diagnostic ignored "-Wsign-conversion" // If any implicit conversions happen
    #pragma GCC diagnostic ignored "-Wconversion"      // For potentially lossy conversions
    #pragma GCC diagnostic ignored "-Wold-style-cast"  // If C-style casts are used internally

    // TommyDS main header file
    #include "tommy.h"

    // Core components always needed
    // NOLINTNEXTLINE(bugprone-suspicious-include)
    #include "tommyalloc.c"
    // NOLINTNEXTLINE(bugprone-suspicious-include)
    #include "tommyhash.c"  // For hashing utilities like tommy_roundup_pow2

    // Components for tommy_hash / tommy_trie
    // NOLINTNEXTLINE(bugprone-suspicious-include)
    #include "tommylist.c"    // tommy_hashtable uses tommy_list for chaining
    // NOLINTNEXTLINE(bugprone-suspicious-include)
    #include "tommyhashtbl.c"
    // NOLINTNEXTLINE(bugprone-suspicious-include)
    #include "tommyhashdyn.c" // Dynamic hashtable
    // NOLINTNEXTLINE(bugprone-suspicious-include)
    #include "tommyhashlin.c" // Linear hashtable

    // Components for tommy_trie
    // NOLINTNEXTLINE(bugprone-suspicious-include)
    #include "tommytrie.c"
    // NOLINTNEXTLINE(bugprone-suspicious-include)
    #include "tommytrieinp.c" // Inplace trie

    // --- Optional: other TommyDS components ---
    // #include "tommyarray.c"
    // #include "tommyarrayblk.c"
    // #include "tommyarrayof.c"
    // #include "tommyarrayblkof.c"
    // #include "tommytree.c"

    static_assert(((1U << (TOMMY_TRIE_BIT - TOMMY_TRIE_BUCKET_SHIFT)) == TOMMY_TRIE_BUCKET_MAX),
              "Mismatch in compile-time calculation of trie bucket parameters");
    static_assert(TOMMY_TRIE_BUCKET_MAX > 0, "TOMMY_TRIE_BUCKET_MAX is zero or negative");

    #pragma GCC diagnostic pop
} // extern "C"