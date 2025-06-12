# Folly Library Integration

This directory is a placeholder for the Folly library, an **external dependency** for Folly-based hash table shims. Folly is **not built by this project's build system**; it must be built and installed separately.

## 1. Building Folly Separately

Build and install Folly independently. Consult Folly's official documentation for the most up-to-date instructions: [https://github.com/facebook/folly](https://github.com/facebook/folly)

**Compatibility considerations for building Folly:**

*   **Compiler Flags (`-march`):** **This is critical.** Folly's high-performance components, like the F14 hash tables, use compile-time dispatch to select code paths based on available CPU instructions (e.g., SSE4.2, `POPCNT`, AVX2). To prevent linker errors, the CPU features available when building Folly **must be consistent** with the features available when building this benchmark.
    *   **Recommendation:** Build both Folly and this benchmark with a common, explicit architecture flag. Using `-march=native` (if building and running on the same machine) or a specific target like `-march=haswell` (which enables SSE4.2, AVX, and AVX2) is strongly recommended to ensure consistency.

*   **Static Folly Libraries:** Building Folly statically (`libfolly.a`) is a complex undertaking.
    *   It requires statically building **all** of Folly's numerous transitive dependencies (GFlags, GLog, Boost, etc.).
    *   All components (Folly, its dependencies, and this benchmark) must be compiled with the **same C++ standard library** (e.g., all with `libstdc++` or all with `libc++`) to prevent linker errors.
    *   The CMake flag `-DFOLLY_NO_EXCEPTION_TRACER=ON` is often necessary when building Folly to avoid symbol conflicts during static linking.

## 2. Integrating Folly with this Project

After Folly is built and installed, configure this project's build system to find it:

1.  **`CMAKE_PREFIX_PATH`:** Set this CMake variable to Folly's installation prefix (the directory containing `include/` and `lib/`).
    *   Example: `cmake -DCMAKE_PREFIX_PATH="/path/to/folly/install" ..`

2.  **Enable Folly Support:**
    *   **CMake:** Ensure `#define HASH_BENCH_ENABLE_FOLLY 1` is in `config.h`.
    *   **Direct Compilation:** Pass `-DHASH_BENCH_ENABLE_FOLLY=1` to your compiler.

## 3. Important Linking Note

*   **Dynamic Linking for this Project:** Due to the complexity of static linking, the recommended approach is to link against a dynamic build of Folly. This may mandate dynamic linking for the final benchmark executable (use of the `-static` linker flag will likely not work).