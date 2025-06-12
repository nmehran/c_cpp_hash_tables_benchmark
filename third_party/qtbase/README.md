# Qt 6 Library Integration

This directory is a placeholder for the Qt 6 framework, an **external dependency** for the `qt_hash_6` shim. Qt is **not built by this project's build system**; it must be built and installed separately.

## 1. Building Qt 6 Separately

Build and install the required Qt 6 modules independently. The general process involves downloading the source code, running a `configure` script to define the build options, and then compiling.

For complete, platform-specific instructions, consult the official documentation:
*   **Official Guide:** [Building Qt Sources](https://doc.qt.io/qt-6/build-sources.html)
*   **Wiki for bleeding-edge:** [Building Qt 6 from Git](https://wiki.qt.io/Building_Qt_6_from_Git)

**Compatibility considerations for building Qt:**

*   **Compiler Flags (`-march`):** It is not necessary to manually specify CPU architecture flags. Qt's `configure` script and build system are designed to handle platform-specific optimizations automatically.
*   **Static Qt Libraries:** Building Qt statically is a complex process and not recommended unless absolutely necessary. It can lead to a very large final binary and may have subtle issues.
*   **Dependencies:** Building from source requires several dependencies, including Python, Perl, and a C++17 compatible compiler. Refer to the official documentation for a full list.

## 2. Integrating Qt 6 with this Project

After Qt 6 is built and installed, configure this project's build system to find it:

1.  **`CMAKE_PREFIX_PATH`:** Set this CMake variable to point to your Qt installation prefix (the directory you specified as the installation target when configuring Qt).
    *   Example: `cmake -DCMAKE_PREFIX_PATH="/path/to/qt-install" ..`

2.  **Enable Qt Support:**
    *   **CMake:** Ensure `#define HASH_BENCH_ENABLE_QT 1` is present in your `config.h`.
    *   **Direct Compilation:** Pass the `-DHASH_BENCH_ENABLE_QT=1` flag to your compiler.

## 3. Important Linking Note

*   **Dynamic Linking for this Project:** Using the standard, dynamically-built Qt libraries will require the benchmark executable to be dynamically linked. Attempting to link the final benchmark executable with `-static` will likely fail if it depends on a dynamic build of Qt.