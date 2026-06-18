# Agent Instructions (AGENTS.md)

This document serves as an instruction manual for AI agents (such as Antigravity) that edit or extend this repository.  
When generating code, refactoring, or fixing bugs, you must strictly adhere to the following basic constraints and detailed rules.

---

## 1. Project Overview

### 1.1 What is this repository?

`embwasm` is an **ultra-minimal WebAssembly (WASM) binary runtime engine written in C++11 for resource-constrained microcontrollers (bare-metal environments)**, such as the Cortex-M series.

* **Zero Dynamic Memory (Heap) Allocation**: Operates entirely within static memory pools without using `malloc` or `new` at all.
* **Complete Elimination of STL, Exceptions, and RTTI**: Fully complies with bare-metal compiler constraints (`-fno-exceptions`, `-fno-rtti`).
* **High-Speed Host API Dispatch**: Automatically generates a C++ lookup table at build time based on WIT (WebAssembly Interface Type) configurations, achieving near-$O(1)$ dispatch via direct `switch` statement calls.

### 1.2 Basic Information

| Item | Content |
|---|---|
| **Language** | C++11 or higher |
| **Coding Style** | Compliant with Google C++ Style Guide |
| **Supported Compilers** | GCC, Clang |
| **Supported Architectures** | Cortex-M series, ARM/ARM64, x86, x86_64 |
| **Supported OS/RTOS** | macOS, Linux, Windows, FreeRTOS, uITRON |
| **Build System** | CMake 3.11 or higher |
| **Namespace** | `embwasm` |

---

## 2. Directory Structure and File Map

embwasm/
├── include/              # Public headers for the core library
│   ├── embwasm.hpp         # Single include entry point aggregating all headers
│   ├── wasm_config.hpp     # All engine configuration constants via constexpr (memory pool size, etc.)
│   ├── wasm_types.hpp      # Basic type definitions (WasmValue, WasmResult, WasmType, etc.)
│   ├── wasm_api.hpp        # Interface declarations for Host API registration/dispatch
│   ├── wasm_memory_pool.hpp# Declaration of the static memory pool class
│   ├── wasm_engine.hpp     # Declaration of the core WASM engine class
│   └── wasm_platform.hpp   # Abstraction of platform-dependent processing (interrupt control, CLZ instructions, etc.)
│
├── src/                  # Core library implementation
│   ├── wasm_engine.cpp   # WASM binary parsing and execution engine body (Most Important File)
│   ├── wasm_memory_pool.cpp # Implementation of the static memory pool
│   └── hostmodule/       # Implementation of host API modules (for core built-ins like threads)
│       └── threads/      # Thread-related host APIs
│
├── wasm_host_modules/    # Definitions and sources for standard host API modules
│   └── <module_name>/    # wit files and implementations for each module
│
├── demo/                 # Demo applications for operation verification
│   └── hello/            # Sample calling a host API from WASM
│
├── test/                 # Base directory for unit test code
│   └── core/             # GoogleTest unit tests for the core engine and library
│
├── platform/             # Platform-specific implementations
│   ├── macos/            # Interrupt control implementation for macOS
│   ├── windows/          # Interrupt control implementation for Windows
│   ├── freertos/         # Interrupt control implementation for FreeRTOS
│   └── uitron/           # Interrupt control implementation for uITRON
│
├── tools/
│   └── codegen/
│       ├── gen_api.py    # Tool to automatically generate host API lookup tables from WIT configurations
│       └── wasm_to_cpp.py# Utility to convert WASM binaries into C++ byte arrays
│
└── docs/
├── coding_style.md   # Detailed guide on naming conventions and language feature restrictions (Must-read)
├── getting_started.md# Build procedures and quick start guide
├── api_impl_for_wasm.md # Procedures for implementing and exposing host APIs
├── tool_usage.md     # How to use code generation tools
└── wasm_vm_spec.md   # WASM interpreter implementation specification (stack, control flow, etc.)



---

## 3. Overview of Core Components

### 3.1 Main Classes and Types

| Class / Type | File | Role |
|---|---|---|
| `WasmEngine` | `include/wasm_engine.hpp` | The engine body responsible for loading and executing WASM binaries |
| `WasmMemoryPool` | `include/wasm_memory_pool.hpp` | Heapless static bump allocator |
| `WasmValue` | `include/wasm_types.hpp` | Tagged union for WASM values (i32/i64/f32/f64) |
| `WasmResult` | `include/wasm_types.hpp` | Enum for error codes (alternative to exceptions) |
| `WasmType` | `include/wasm_types.hpp` | Enum for WASM value types |
| `WasmTypeSignature` | `include/wasm_types.hpp` | WASM function signature (bounded static array) |
| `HostFunctionId` | `include/wasm_api.hpp` | Identification ID for host functions (Enum) |
| `WasmFunction` | `include/wasm_engine.hpp` | Union structure for imported and internal functions |

### 3.2 Key Configuration Constants (`include/wasm_config.hpp`)

All limits for the engine are managed using `constexpr` constants. If changes are required, modify only this file.

| Constant | Default Value | Meaning |
|---|---|---|
| `kMemoryPoolSize` | 65536 (64 KB) | Total size of the static memory pool |
| `kMaxWasmFunctions` | 32 | Upper limit on the number of supported WASM functions |
| `kMaxWasmTypes` | 16 | Upper limit on the number of supported WASM type signatures |
| `kWasmStackSize` | 64 | Maximum depth of the WASM execution stack |
| `kWasmCallStackSize` | 16 | Maximum depth of the call stack |
| `kMaxLocals` | 32 | Maximum number of local variables allowed per function |

### 3.3 Host API Mechanism

The mechanism for calling host-side (C++) functions from WASM is as follows:

1. **Definition**: Describe the module name, field name, argument types, and C++ mapping in the WIT configuration file (`.wit`).
2. **Auto-generation**: `tools/codegen/gen_api.py` reads the WIT file and automatically generates the `HostFunctionId` enum and lookup tables as C++ code.
3. **Lookup**: `LookupStaticHostFunctionId()` resolves the ID using a binary search ($O(\log N)$).
4. **Dispatch**: `DispatchHostFunction()` executes the function via a direct call inside a `switch` statement (complying with the rule banning function pointers).

### 3.4 Placement Rules for Host API Modules

Standard bundled host API modules must be organized according to the following rules:

* **General Rule**: Place `.wit` files and source code inside the `wasm_host_modules/<module_name>` directory. These must be structured so they can be built individually and linked selectively as needed.
* **Exception (`threads`)**: For the `threads` module only, place the source code in `src/hostmodule/threads` to build it bundled together with the core library (`src`). The `.wit` file should still be placed in `wasm_host_modules/threads`.

---

## 4. Critical Development Constraints (Strict Compliance Required)

Because this library is built for bare-metal environments, the following features are disabled at the compiler level. Code using these features will cause build errors or trigger runtime failures.

1. **Ban on STL (Standard Template Library) Usage**
   * Containers that perform implicit dynamic memory (heap) allocation (such as `std::vector`, `std::string`, `std::map`) cannot be used.
   * Design your code to be allocator-free, using fixed-size collections (`std::array`) or structures that utilize stack or static memory regions.
2. **Ban on Bare-Metal Incompatible Libraries**
   * Do not introduce libraries into the `include/` and `src/` directories that cannot be used in a bare-metal environment, except strictly for debugging purposes.
3. **Prohibition of Exception Handling**
   * `throw`, `try`, and `catch` cannot be used (`-fno-exceptions`).
   * Handle errors by returning error codes or `Result` / `Status` objects.
4. **Prohibition of RTTI (Run-Time Type Information)**
   * `dynamic_cast` and `typeid` cannot be used (`-fno-rtti`).
   * If polymorphic casting is necessary, use `static_cast` or safe downcasting via type-discriminating member variables (Tag/Enum).
5. **Prohibition of Recursion**
   * To protect the extremely small stack space of microcontrollers and prevent unexpected stack overflows, recursive calls are strictly prohibited.
   * Implement logic using loops (iterative processing) or iterative algorithms with explicit data structures like call stacks or queues.
6. **Prohibition of Function Pointers and Member Function Pointers**
   * To prevent indirect calls from making worst-case stack consumption uncalculable by static analysis tools, function pointers and member function pointers are banned in principle.
   * If dispatching or dynamic switching is required, implement static dispatch using IDs (such as Enums) and direct calls within a `switch` statement.

---

## 5. Coding Style Details

Detailed guidelines regarding naming conventions and restrictions on C++ features are defined in the following document. Be sure to reference it before modifying any code.

* **Detailed Coding Style**: [docs/coding_style.md](docs/coding_style.md)

---

## 6. Specific Action Requests for Agents

* When proposing or creating new files, always apply the naming conventions detailed in [docs/coding_style.md](docs/coding_style.md) (e.g., `CamelCase` for class names, `snake_case_` for member variables).
* To eliminate dynamic memory allocation, determine array sizes and boundaries as static constants using `constexpr`.
* To avoid undefined behavior (UB), minimize pointer arithmetic and type casting. If casting is necessary, use explicit interfaces rather than brute-force `reinterpret_cast`.
* To protect the host stack, if a function requires deep nesting or recursive logic, you must implement it iteratively using an explicit call stack (data structure).
* To ensure static analysis tools can compute the worst-case stack consumption, do not use indirect calls via function pointers or member function pointers. Implement static dispatch using IDs and `switch` branching instead.
* If you need to change the engine's capacity limits, modify only the `constexpr` constants in `include/wasm_config.hpp`. Do not hardcode magic numbers directly into the codebase.
* When adding platform-specific logic (such as interrupt control), create a corresponding directory under `platform/` and implement it according to the interface defined in `wasm_platform.hpp`.
* When adding a new host API, do not write the boilerplate by hand. Follow the automated generation workflow using `tools/codegen/gen_api.py` (Details: [docs/api_impl_for_wasm.md](docs/api_impl_for_wasm.md)).
* When adding a new host API module, place the `.wit` file and source code under `wasm_host_modules/<module_name>`, except for special exclusions like `threads`.
* Test code must be created within the `test/core/` directory (for core features) or an appropriate subdirectory. Implement tests for all functions inside `include/`, `src/`, and `wasm_host_modules/`.
* WASM specification compliance tests (verifying if the WASM module behaves according to specifications) are already included in `test/core/official_test_wasm`. Therefore, you do not need to create separate test code (such as additions to `wasm_engine_test.cpp`) purely to validate the same module operations.
* WASM binary arrays used for successful path testing within test code (like `test/core/wasm_engine_test.cpp`) must not be manually or arbitrarily assembled. Always use byte arrays fully compliant with the WASM specification generated by `clang`. When generating or updating binary arrays, utilize automated scripts such as `tools/codegen/generate_test_wasms.py`.
* When modifying or extending the WASM interpreter (VM decode loops, stack operations, control flow, etc.), you must understand and adhere to the implementation strategies outlined in [docs/wasm_vm_spec.md](docs/wasm_vm_spec.md) (management of data/call stacks, control flow branching via label stacks, yield processing, etc.), and update the specification document itself as necessary.
