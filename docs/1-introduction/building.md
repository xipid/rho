# Building & Integration

## Prerequisites

- C++17 compiler (GCC 7+, Clang 5+, or equivalent)
- CMake 3.14+
- The [Xi framework](https://github.com/xipid/xic) (placed next to the `rho` directory, or specify with `-DXI_DIR`)

## Building

```bash
# Clone both repos side by side
git clone https://github.com/xipid/xic.git
git clone https://github.com/xipid/rho.git

# Build
cd rho
mkdir build && cd build
cmake ..
make -j$(nproc)
```

This produces:
- `rhod` — The Rho daemon binary
- `tests/*` — Test executables

## Using Rho in Your Project

Rho is **header-only**. Add it as a subdirectory in your CMake project:

```cmake
add_subdirectory(path/to/rho)
target_link_libraries(your_target PRIVATE Rho)
```

Or simply add `include/` to your include path and link against Xi:

```cmake
target_include_directories(your_target PRIVATE path/to/rho/include)
target_link_libraries(your_target PRIVATE Xi)
```

## ESP32 (PlatformIO)

Rho compiles on ESP32 with lwIP sockets. The `Bind` class automatically detects the platform:

```cpp
// On ESP32, Bind uses lwip/sockets.h instead of sys/socket.h
// No code changes needed — just include and build
#include <Lines/Bind.hpp>

Bind bind("192.168.1.100:8080");
```

Add both `xic` and `rho` as library dependencies in your `platformio.ini`.

## Compiler Flags

For maximum performance, build with:

```bash
cmake .. -DCMAKE_BUILD_TYPE=Release
```

Rho benefits significantly from inlining and link-time optimization:

```cmake
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -flto -DNDEBUG")
```
