cmake_minimum_required(VERSION 3.28)
cmake_policy(SET CMP0077 NEW)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
set(CMAKE_POLICY_VERSION_MINIMUM 3.10)
set(CMAKE_SKIP_INSTALL_RULES ON)
project(kokoro__kokoro_tts LANGUAGES C CXX)

# Options
option(BUILD_CLI "Whether to build the kokoro-cli executable." ON)
option(BUILD_SERVER "Whether to build the kokoro-server executable." ON)

# Settings
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")


# Import dependencies
include(FetchContent)
FetchContent_Declare(
    cxxopts
    GIT_REPOSITORY  https://github.com/jarro2783/cxxopts.git
    GIT_TAG         v3.3.1
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(cxxopts)
FetchContent_Declare(
    fmt
    GIT_REPOSITORY  https://github.com/fmtlib/fmt.git
    GIT_TAG         12.1.0
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(fmt)
FetchContent_Declare(
    generator
    GIT_REPOSITORY  https://github.com/todoubaba/generator.git
    GIT_TAG         main
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(generator)
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
)
set(JSON_BuildTests OFF)
FetchContent_MakeAvailable(json)
FetchContent_Declare(
    ordered_map
    GIT_REPOSITORY  https://github.com/Tessil/ordered-map.git
    GIT_TAG         v1.2.0
)
FetchContent_MakeAvailable(ordered_map)
FetchContent_Declare(
    range_v3
    GIT_REPOSITORY  https://github.com/ericniebler/range-v3.git
    GIT_TAG         0.12.0
    EXCLUDE_FROM_ALL
)
set(RANGES_CXX_STD 20)
set(RANGES_BUILD_CALENDAR_EXAMPLE OFF)
set(RANGES_NATIVE OFF)
FetchContent_MakeAvailable(range_v3)
set(BUILD_ZLIB_DEFAULT_SYSTEMS "Linux;Windows")
set(BUILD_ZLIB OFF)
if(CMAKE_SYSTEM_NAME IN_LIST BUILD_ZLIB_DEFAULT_SYSTEMS)
    set(BUILD_ZLIB ON)
endif()
FetchContent_Declare(
    cnpy
    GIT_REPOSITORY  https://github.com/rogersce/cnpy.git
    GIT_TAG         master
    GIT_SHALLOW     TRUE
    GIT_PROGRESS    TRUE
    EXCLUDE_FROM_ALL
)
FetchContent_Declare(
    zlib
    GIT_REPOSITORY  https://github.com/madler/zlib.git
    GIT_TAG         v1.3.1
    GIT_SHALLOW     TRUE
    GIT_PROGRESS    TRUE
    EXCLUDE_FROM_ALL
)
set(ENABLE_STATIC ON)
if(BUILD_ZLIB)
    if(NOT TARGET zlibstatic)
        FetchContent_MakeAvailable(zlib)
    endif()
    set(ZLIB_FOUND TRUE)
    get_target_property(ZLIB_INCLUDE_DIR zlibstatic INCLUDE_DIRECTORIES)
    set(ZLIB_LIBRARY zlibstatic)
elseif(EMSCRIPTEN)
    set(ZLIB_FOUND TRUE)
    set(ZLIB_INCLUDE_DIR "${EMSCRIPTEN_SYSROOT}/include")
    set(ZLIB_LIBRARY z)
else()
    set(ZLIB_LIBRARY z)
endif()
FetchContent_MakeAvailable(cnpy)
if(NOT BUILD_ZLIB AND EMSCRIPTEN)
    target_compile_options(cnpy-static PUBLIC -sRELOCATABLE=1 --use-port=zlib)
endif()
target_include_directories(cnpy-static INTERFACE ${cnpy_SOURCE_DIR})
target_link_libraries(cnpy-static INTERFACE ${ZLIB_LIBRARY})
set(ONNXRUNTIME_VERSION 1.20.1)
set(ONNXRUNTIME_BASE_URL "https://github.com/csukuangfj/onnxruntime-libs/releases/download/v${ONNXRUNTIME_VERSION}")

# Select the prebuilt static ONNX Runtime package matching this platform/arch.
# Each package ships a single static lib (libonnxruntime.a / onnxruntime.lib)
# under <root>/lib plus headers under <root>/include.
set(ONNXRUNTIME_SYSTEM_LIBS "")
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _ort_arch)
if(APPLE)
    set(ONNXRUNTIME_ARCHIVE "onnxruntime-osx-universal2-static_lib-${ONNXRUNTIME_VERSION}.zip")
    set(ONNXRUNTIME_LIB_NAME "libonnxruntime.a")
    set(ONNXRUNTIME_SYSTEM_LIBS "-framework CoreFoundation;-framework Foundation")
elseif(WIN32)
    if(_ort_arch MATCHES "arm64|aarch64")
        set(ONNXRUNTIME_ARCHIVE "onnxruntime-win-arm64-static_lib-${ONNXRUNTIME_VERSION}.tar.bz2")
    elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
        set(ONNXRUNTIME_ARCHIVE "onnxruntime-win-x86-static_lib-${ONNXRUNTIME_VERSION}.tar.bz2")
    else()
        set(ONNXRUNTIME_ARCHIVE "onnxruntime-win-x64-static_lib-${ONNXRUNTIME_VERSION}.tar.bz2")
    endif()
    set(ONNXRUNTIME_LIB_NAME "onnxruntime.lib")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if(_ort_arch MATCHES "aarch64|arm64")
        set(ONNXRUNTIME_ARCHIVE "onnxruntime-linux-aarch64-static_lib-${ONNXRUNTIME_VERSION}-glibc2_17.zip")
    elseif(_ort_arch MATCHES "arm")
        set(ONNXRUNTIME_ARCHIVE "onnxruntime-linux-arm-static_lib-${ONNXRUNTIME_VERSION}.zip")
    else()
        set(ONNXRUNTIME_ARCHIVE "onnxruntime-linux-x64-static_lib-${ONNXRUNTIME_VERSION}-glibc2_17.zip")
    endif()
    set(ONNXRUNTIME_LIB_NAME "libonnxruntime.a")
    # Static ORT pulls in the usual POSIX/runtime deps.
    set(ONNXRUNTIME_SYSTEM_LIBS "Threads::Threads;${CMAKE_DL_LIBS};rt;m")
else()
    message(FATAL_ERROR "No prebuilt static ONNX Runtime for system '${CMAKE_SYSTEM_NAME}' / arch '${CMAKE_SYSTEM_PROCESSOR}'.")
endif()

if(ONNXRUNTIME_SYSTEM_LIBS MATCHES "Threads::Threads")
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
endif()

FetchContent_Declare(
    onnxruntime
    URL "${ONNXRUNTIME_BASE_URL}/${ONNXRUNTIME_ARCHIVE}"
)
FetchContent_MakeAvailable(onnxruntime)
add_library(onnxruntime STATIC IMPORTED GLOBAL)
set_target_properties(
    onnxruntime PROPERTIES
    IMPORTED_LOCATION "${onnxruntime_SOURCE_DIR}/lib/${ONNXRUNTIME_LIB_NAME}"
    INTERFACE_INCLUDE_DIRECTORIES "${onnxruntime_SOURCE_DIR}/include"
    INTERFACE_LINK_LIBRARIES "${ONNXRUNTIME_SYSTEM_LIBS}"
)
FetchContent_Declare(
    simdutf
    GIT_REPOSITORY  https://github.com/simdutf/simdutf
    GIT_TAG         v7.4.0
    GIT_SHALLOW     TRUE
    GIT_PROGRESS    TRUE
    EXCLUDE_FROM_ALL
)
set(BUILD_SHARED_LIBS OFF)
set(SIMDUTF_TESTS OFF)
set(SIMDUTF_TOOLS OFF)
set(SIMDUTF_ICONV OFF)
FetchContent_MakeAvailable(simdutf)
FetchContent_Declare(
    xtl
    GIT_REPOSITORY https://github.com/xtensor-stack/xtl.git
    GIT_TAG 0.8.0
    EXCLUDE_FROM_ALL
)
FetchContent_Declare(
    xsimd
    GIT_REPOSITORY https://github.com/xtensor-stack/xsimd.git
    GIT_TAG 13.2.0
    EXCLUDE_FROM_ALL
)
FetchContent_Declare(
    xtensor
    GIT_REPOSITORY https://github.com/xtensor-stack/xtensor.git
    GIT_TAG 0.27.1
    EXCLUDE_FROM_ALL
)
set(XTENSOR_USE_XSIMD ON)
set(CPP20 ON)
FetchContent_MakeAvailable(xtl)
FetchContent_MakeAvailable(xsimd)
FetchContent_MakeAvailable(xtensor)
if(EMSCRIPTEN)
    target_compile_options(xsimd INTERFACE -msimd128 -msse4.2)
endif()
target_compile_definitions(
    xtensor INTERFACE
    _LIBCPP_ENABLE_CXX20_REMOVED_ALLOCATOR_MEMBERS
    XTENSOR_USE_XSIMD=1
)
target_link_libraries(xtensor INTERFACE xsimd)

# Define target
add_library(kokoro__kokoro_tts INTERFACE)
add_library(kokoro::kokoro_tts ALIAS kokoro__kokoro_tts)
target_compile_features(kokoro__kokoro_tts INTERFACE cxx_std_20)
target_include_directories(kokoro__kokoro_tts INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
if(MSVC)
    target_compile_options(kokoro__kokoro_tts INTERFACE /Zc:preprocessor /utf-8)
endif()
target_link_libraries(
    kokoro__kokoro_tts INTERFACE
    stdgenerator
    nlohmann_json::nlohmann_json
    fmt::fmt-header-only
    range-v3::range-v3
    tsl::ordered_map 
    cnpy-static
    onnxruntime
    simdutf
    xtensor
)

# Binaries are written into ./bin so they sit together (and drop straight into
# zallama's ./bin layout). Resource files are loaded relative to the working
# directory at runtime, not relative to the binary.
set(KOKORO_BIN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/bin)

# kokoro-cli: one-shot text -> WAV file (parakeet-cli / llama-cli style).
if(BUILD_CLI)
    add_executable(kokoro_cli src/cli.cpp)
    target_link_libraries(kokoro_cli PRIVATE
        kokoro::kokoro_tts
        cxxopts::cxxopts
    )
    set_target_properties(kokoro_cli PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${KOKORO_BIN_DIR}
        OUTPUT_NAME kokoro-cli
    )
endif()

# kokoro-server: OpenAI-compatible TTS HTTP server (parakeet-server style).
if(BUILD_SERVER)
    # cpp-httplib is header-only; no extra system deps, keeps the binary
    # standalone like the rest of the FetchContent stack.
    FetchContent_Declare(
        httplib
        GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
        GIT_TAG        v0.18.3
        GIT_SHALLOW    TRUE
        EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(httplib)

    add_executable(kokoro_server src/server.cpp)
    target_link_libraries(kokoro_server PRIVATE
        kokoro::kokoro_tts
        cxxopts::cxxopts
        httplib::httplib
    )
    set_target_properties(kokoro_server PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${KOKORO_BIN_DIR}
        OUTPUT_NAME kokoro-server
    )
endif()