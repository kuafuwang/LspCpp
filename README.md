# LspCpp

A C++ library for building [Language Server Protocol (LSP)](https://microsoft.github.io/language-server-protocol/) servers. It provides JSON-RPC transport, LSP message types, and helpers for stdio, TCP, and WebSocket communication.

## Dependencies

### Core (library only)

These are required to build the `lspcpp` static library:

| Dependency | Source |
|------------|--------|
| [Asio](https://think-async.com/Asio/) (standalone) | Bundled in `third_party/asio`, or via vcpkg / system package |
| [RapidJSON](https://github.com/Tencent/rapidjson) | Bundled in `third_party/rapidjson`, or system package |
| [utfcpp](https://github.com/nemtrif/utfcpp) | Bundled in `third_party/utfcpp` |
| [network-uri](https://github.com/cpp-netlib/uri) | Bundled in `third_party/uri` |
| [IXWebSocket](https://github.com/machinezone/IXWebSocket) | Bundled in `third_party/ixwebsocket`, or via vcpkg |

Boost is **not** required for the library when `LSPCPP_STANDALONE_ASIO` is enabled (the default).

### Optional

| Dependency | When needed |
|------------|-------------|
| Boost (`filesystem`, `program_options`, `system`) | Building examples or tests |
| [Boehm GC](https://www.hboehm.info/gc/) | Optional GC support (`LSPCPP_SUPPORT_BOEHM_GC=ON`) |

## Build

Requires CMake 3.16+ and a C++17-capable compiler (`LSPCPP_USE_CPP17=ON` by default).

### 1. Initialize submodules

```shell
git submodule update --init --recursive
```

### 2. Build the library (Linux / macOS)

```shell
mkdir _build && cd _build
cmake -DUri_BUILD_TESTS=OFF ..
cmake --build . -j
```

This produces `liblspcpp.a` (or `lspcpp.lib` on Windows) without installing Boost.

### 3. Build with examples and tests

Examples and CTest smoke tests require Boost. Enable them with:

```shell
cmake -DUri_BUILD_TESTS=OFF -DLSPCPP_BUILD_TESTS=ON ..
cmake --build . -j
ctest --output-on-failure
```

`LSPCPP_BUILD_TESTS=ON` automatically enables `LSPCPP_BUILD_EXAMPLES`.

On Linux:

```shell
sudo apt-get install libboost-filesystem-dev libboost-program-options-dev libboost-system-dev
```

On macOS:

```shell
brew install boost
```

### 4. Build with vcpkg (CI configuration)

A `vcpkg.json` manifest is provided for convenience. It is not required for a local build, but matches the CI setup.

Set `VCPKG_ROOT` to your vcpkg installation, then:

```shell
export LSPCPP_CI_VCPKG_FEATURES=tests   # optional: pull in Boost for tests/examples
cmake --preset ci/default
cmake --build --preset ci/default -j
ctest --preset ci/default
```

### Windows

Generate a Visual Studio solution and build:

```shell
mkdir _build
cd _build
cmake -DUri_BUILD_TESTS=OFF -DUri_USE_STATIC_CRT=OFF ..
```

Open the generated solution in Visual Studio, or build from the command line with `cmake --build .`.

To build tests on Windows, use vcpkg with the `tests` feature (see section 4 above).

### Useful CMake options

| Option | Default | Description |
|--------|---------|-------------|
| `LSPCPP_STANDALONE_ASIO` | `ON` | Use standalone Asio instead of Boost.Asio |
| `LSPCPP_BUILD_WEBSOCKETS` | `ON` | Build WebSocket server support |
| `LSPCPP_BUILD_EXAMPLES` | `OFF` | Build example applications |
| `LSPCPP_BUILD_TESTS` | `OFF` | Build and register CTest smoke tests |
| `LSPCPP_USE_CPP17` | `ON` | Compile as C++17 |
| `USE_SYSTEM_RAPIDJSON` | `OFF` | Use system RapidJSON instead of the submodule |
| `USE_EXTERNAL_ASIO` | `OFF` | Use vcpkg/system Asio instead of the submodule |
| `USE_EXTERNAL_IXWEBSOCKET` | `OFF` | Use vcpkg/system IXWebSocket instead of the submodule |

## Examples

Example applications live in the [examples](https://github.com/kuafuwang/LspCpp/tree/master/examples) directory:

- `StdIOClientExample` / `StdIOServerExample` — stdio JSON-RPC
- `TcpServerExample` — TCP server
- `WebsocketExample` — WebSocket server

## Versioning

LspCpp uses semantic versioning for project and package metadata. The current
project version is `1.0.1`.

When preparing a release:

- Update `LSPCPP_VERSION` in `CMakeLists.txt`.
- Update `version-semver` in `vcpkg.json` to the same value.
- Tag the release as `vX.Y.Z`, for example `v1.0.1`.
- Use the same version in the GitHub release title.

## Projects using LspCpp

* [JCIDE](https://www.javacardos.com/javacardforum/viewtopic.php?f=5&t=3569&sid=e01238adf55cd08696fbf495dfa6c8e5)
* [LPG-language-server](https://github.com/kuafuwang/LPG-language-server)
* [Asymptote](https://github.com/vectorgraphics)
* [chemical](https://github.com/chemicallang/chemical)

## Reference

Some code from [cquery][1].

## License

MIT

## Development guide

For any merges into the `master` branch, ensure the C++ code complies with the clang-format standard. As of currently, the latest clang-format version offered in Ubuntu 24.04 (18) is used, but this may change in the future as newer versions of clang-format become available for Ubuntu.

To check the current version of clang-format used, see the `check-format-cpp` workflow. It prints out the version used. Ensure the C++ code is compliant with that version of clang-format.

[1]: https://github.com/cquery-project/cquery "cquery"
