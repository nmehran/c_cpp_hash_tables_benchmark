# LLVM Project Integration

This directory is a placeholder for the LLVM project libraries, an **external dependency** for the `llvm::DenseMap` shim. LLVM is **not built by this project's build system**; it must be built and installed separately.

## 1. Building LLVM Separately

Build and install LLVM independently. Consult LLVM's official documentation for detailed instructions: [Getting Started with the LLVM System](https://llvm.org/docs/GettingStarted.html).

**Compatibility considerations for building LLVM:**

*   **Build Type:** For accurate performance benchmarks, it is crucial to build LLVM in its optimized `Release` mode. This can be configured with the CMake flag: `-DCMAKE_BUILD_TYPE=Release`.
*   **Minimal Components:** The full LLVM project is massive. To save significant build time and disk space, it is highly recommended to build only the necessary components. For the `llvm::DenseMap` shim, no specific projects like `clang` or `lld` are needed. A minimal build of the core LLVM libraries is sufficient.
*   **Static LLVM Libraries:** Statically linking LLVM can be complex due to its size and dependencies. If you require static libraries, refer to the official LLVM documentation for guidance.

## 2. Integrating LLVM with this Project

After LLVM is built and installed, configure this project's build system to find it:

1.  **`LLVM_DIR`:** Set this CMake variable to the path containing `LLVMConfig.cmake`. This is typically found in your LLVM installation prefix at `<install_dir>/lib/cmake/llvm`.
    Example: `cmake -DLLVM_DIR="/path/to/llvm/install/lib/cmake/llvm" ..`

2.  **Enable LLVM Support:**
    *   **CMake:** Ensure `#define HASH_BENCH_ENABLE_LLVM 1` is set in `config.h`.
    *   **Direct Compilation:** Pass `-DHASH_BENCH_ENABLE_LLVM=1` to your compiler.

## 3. Important Linking Note

*   **Dynamic Linking for this Project:** Enabling the LLVM shim may mandate dynamic linking for the final benchmark executable, especially on Linux. Attempting to use a `-static` linker flag for the main benchmark program may fail due to the nature of the LLVM libraries.