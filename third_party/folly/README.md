# Folly Library Integration

This directory is a placeholder for the Folly library, an **external dependency** for Folly-based hash table shims. Folly is **not built by this project's build system**; it must be built and installed separately.

## 1. Building Folly Separately

Build and install Folly independently. Consult Folly's official documentation for instructions: [https://github.com/facebook/folly](https://github.com/facebook/folly)

**Compatibility considerations for building Folly:**

*   **Compiler Flags (`-march`):** Folly uses extensive SSE intrinsics. Build Folly with CPU architecture flags compatible with this project (e.g., `-march=haswell` or `-march=native` on your target machine).
*   **Static Folly Libraries:** Building Folly statically (`libfolly.a`) is complex. It requires statically building *all* of Folly's numerous transitive dependencies. Additionally, `-DFOLLY_NO_EXCEPTION_TRACER=ON` may be needed to avoid symbol conflicts.

## 2. Integrating Folly with this Project

After Folly is built and installed, configure this project's build system to find it:

1.  **`CMAKE_PREFIX_PATH`:** Set this CMake variable to Folly's installation prefix (the directory containing `include/` and `lib/`).
    Example: `cmake -DCMAKE_PREFIX_PATH="/path/to/folly/install" ..`

2.  **Enable Folly Support:**
    *   **CMake:** Ensure `#define HASH_BENCH_ENABLE_FOLLY 1` is in `config.h`.
    *   **Direct Compilation:** Pass `-DHASH_BENCH_ENABLE_FOLLY=1` to your compiler.

## 3. Important Linking Note

*   **Dynamic Linking for this Project:** Enabling Folly may mandate dynamic linking for the benchmark executable (use of `-static` linker flag may not work).