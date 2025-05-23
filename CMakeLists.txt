cmake_minimum_required(VERSION 3.10)
project(MinimalChromiumThread)

# Set C++ standard to C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Configure runtime library consistently (important for ClangCL)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /MT")  
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} /MTd")  
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MTd")

# Define preprocessor macros globally
add_definitions(-DBASE_COMPONENT_BUILD=0)

# Common sources for the services
set(COMMON_SOURCES
    ${CMAKE_SOURCE_DIR}/src/service/sqlite/sqlite3.c
    ${CMAKE_SOURCE_DIR}/src/service/database_initializer.cpp
)

# MainServer sources
set(MAIN_SERVER_SOURCES
    ${CMAKE_SOURCE_DIR}/src/service/main_service.cpp
    ${COMMON_SOURCES}
)

# Collect source files for chromium_threading
file(GLOB_RECURSE BASE_SOURCES
    "${CMAKE_SOURCE_DIR}/src/base/*.cc"
    "${CMAKE_SOURCE_DIR}/src/base/files/*.cc"
    "${CMAKE_SOURCE_DIR}/src/base/task/*.cc" # Added for thread pool
)
file(GLOB_RECURSE ABSEIL_SOURCES
    "${CMAKE_SOURCE_DIR}/src/third_party/abseil-cpp/*.cc"
)
file(GLOB_RECURSE CITYHASH_SOURCES
    "${CMAKE_SOURCE_DIR}/src/base/third_party/cityhash/*.cc"
)
file(GLOB_RECURSE BORINGSSL_SOURCES
    "${CMAKE_SOURCE_DIR}/src/third_party/boringssl/src/**/*.cc"
    "${CMAKE_SOURCE_DIR}/src/third_party/boringssl/src/**/*.c"
)
file(GLOB_RECURSE BORINGSSL_ASSEMBLY_SOURCES
    "${CMAKE_SOURCE_DIR}/src/third_party/boringssl/src/**/*.S"
)
file(GLOB_RECURSE BORINGSSL_SOURCES_CRYPTO
    "${CMAKE_SOURCE_DIR}/src/third_party/boringssl/src/crypto/*.cc"
)
file(GLOB_RECURSE MODP_SOURCES
    "${CMAKE_SOURCE_DIR}/src/third_party/modp_b64/*.cc"
)

# Ensure assembly files are processed
if(WIN32 AND MSVC)
    enable_language(ASM_MASM)
    set(CMAKE_ASM_MASM_COMPILE_OBJECT "<CMAKE_ASM_MASM_COMPILER> /Fo <OBJECT> /c <SOURCE>")
    foreach(asm_file ${BORINGSSL_ASSEMBLY_SOURCES})
        if(asm_file MATCHES ".*-x86_64.S$")
            # Convert .S to .asm for MSVC (lld-link compatibility)
            get_filename_component(asm_name ${asm_file} NAME_WE)
            set(asm_output "${CMAKE_BINARY_DIR}/${asm_name}.asm")
            configure_file(${asm_file} ${asm_output} COPYONLY)
            list(APPEND BORINGSSL_ASSEMBLY_CONVERTED ${asm_output})
        endif()
    endforeach()
endif()

list(FILTER BASE_SOURCES EXCLUDE REGEX ".*checked_iterators.h")
list(FILTER BASE_SOURCES EXCLUDE REGEX ".*checked_iterators.cc")
list(FILTER BASE_SOURCES EXCLUDE REGEX ".*/test/.*")
list(FILTER BASE_SOURCES EXCLUDE REGEX ".*trace_event.*")
list(FILTER BASE_SOURCES EXCLUDE REGEX ".*/tracing/.*")
list(FILTER BASE_SOURCES EXCLUDE REGEX ".*/icu/.*")
list(FILTER BASE_SOURCES EXCLUDE REGEX ".*/testing/.*")

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/src/third_party/boringssl/src/include/
    ${CMAKE_SOURCE_DIR}/src/third_party/boringssl/src/
    ${CMAKE_SOURCE_DIR}/src/out/threading_base/gen/
    ${CMAKE_SOURCE_DIR}/src/base/
    ${CMAKE_SOURCE_DIR}/src/
    ${CMAKE_SOURCE_DIR}/src/base/allocator/partition_allocator/src/
    ${CMAKE_SOURCE_DIR}/src/out/threading_base/gen/base/allocator/partition_allocator/src/
    ${CMAKE_SOURCE_DIR}/src/out/threading_base/gen/base/allocator/partition_allocator/src/partition_alloc/
    ${CMAKE_SOURCE_DIR}/src/third_party/abseil-cpp/
    ${CMAKE_SOURCE_DIR}/src/third_party/double-conversion/double-conversion/
    ${CMAKE_SOURCE_DIR}/src/third_party/perfetto/include/
    ${CMAKE_SOURCE_DIR}/src/build/
    ${CMAKE_SOURCE_DIR}/src/service/sqlite
    ${CMAKE_SOURCE_DIR}/src/service/json/single_include
)

# Add source files for the static library
set(SOURCES
    ${CMAKE_SOURCE_DIR}/src/third_party/boringssl/src/crypto/x509/asn1_gen.cc
    ${CMAKE_SOURCE_DIR}/src/third_party/boringssl/src/crypto/bytestring/cbs.cc
    ${CMAKE_SOURCE_DIR}/src/third_party/boringssl/src/crypto/bytestring/cbb.cc
    ${CMAKE_SOURCE_DIR}/src/third_party/boringssl/src/crypto/bytestring/asn1_compat.cc
    ${CMAKE_SOURCE_DIR}/src/third_party/boringssl/src/ssl/ssl_asn1.cc
    ${BASE_SOURCES}
    ${ABSEIL_SOURCES}
    ${CITYHASH_SOURCES}
    ${BORINGSSL_SOURCES}
    ${BORINGSSL_SOURCES_CRYPTO}
    ${BORINGSSL_ASSEMBLY_SOURCES}
    ${BORINGSSL_ASSEMBLY_CONVERTED}
    ${MODP_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/base/third_party/superfasthash/superfasthash.c
    ${CMAKE_SOURCE_DIR}/src/base/trace_event/memory_allocator_dump_guid.cc
    ${CMAKE_SOURCE_DIR}/src/base/trace_event/heap_profiler_allocation_context_tracker.cc
    ${CMAKE_SOURCE_DIR}/src/base/trace_event/trace_event_stub.cc
    ${CMAKE_SOURCE_DIR}/src/base/trace_event/histogram_scope.cc
    ${CMAKE_SOURCE_DIR}/src/service/windows_service.cc
    ${CMAKE_SOURCE_DIR}/src/service/main.cc
)

# Apply exclusion filters
list(FILTER SOURCES EXCLUDE REGEX ".*gtest_util.h")
list(FILTER SOURCES EXCLUDE REGEX ".*checked_iterators.h")
list(FILTER SOURCES EXCLUDE REGEX ".*checked_iterators.cc")
list(FILTER SOURCES EXCLUDE REGEX ".*/test/.*")
list(FILTER SOURCES EXCLUDE REGEX ".*/arm64/.*")
list(FILTER SOURCES EXCLUDE REGEX ".*_unittest.cc")
list(FILTER SOURCES EXCLUDE REGEX "/unittest_.*.cc")
list(FILTER SOURCES EXCLUDE REGEX ".*unittest_utils.cc")
list(FILTER SOURCES EXCLUDE REGEX ".*_test/\.cc")
list(FILTER SOURCES EXCLUDE REGEX ".*charconv_parse_test\\.cc")
list(FILTER SOURCES EXCLUDE REGEX "/test_.*.cc")
list(FILTER SOURCES EXCLUDE REGEX ".*_perftest.cc")
list(FILTER SOURCES EXCLUDE REGEX ".*_posix.cc")
list(FILTER SOURCES EXCLUDE REGEX ".*_linux.cc")
list(FILTER SOURCES EXCLUDE REGEX ".*_epoll.cc")
list(FILTER SOURCES EXCLUDE REGEX ".*_ios_libdispatch.cc")
list(FILTER SOURCES EXCLUDE REGEX ".*_kqueue.cc")
list(FILTER SOURCES EXCLUDE REGEX ".*_glib.cc")
list(FILTER SOURCES EXCLUDE REGEX ".*_mac.cc")
list(FILTER SOURCES EXCLUDE REGEX ".*_aix.cc")
list(FILTER SOURCES EXCLUDE REGEX ".*_ios.cc")
list(FILTER SOURCES EXCLUDE REGEX ".*_nacl.cc")
list(FILTER SOURCES EXCLUDE REGEX ".*_cros.cc")
list(FILTER SOURCES EXCLUDE REGEX ".*_linux_base.cc")
list(FILTER SOURCES EXCLUDE REGEX ".*android.*")
list(FILTER SOURCES EXCLUDE REGEX ".*apple.*")
list(FILTER SOURCES EXCLUDE REGEX ".*posix.*")
list(FILTER SOURCES EXCLUDE REGEX ".*mac.*")
list(FILTER SOURCES EXCLUDE REGEX ".*gmock.*")
list(FILTER SOURCES EXCLUDE REGEX ".*modulewrapper.*")
list(FILTER SOURCES EXCLUDE REGEX ".*symbolize.*")
list(FILTER SOURCES EXCLUDE REGEX ".*nix.*")
list(FILTER SOURCES EXCLUDE REGEX ".*shim.*")
list(FILTER SOURCES EXCLUDE REGEX ".*json.*")
list(FILTER SOURCES EXCLUDE REGEX ".*i18n.*")
list(FILTER SOURCES EXCLUDE REGEX ".*/native_library/.*")
list(FILTER SOURCES EXCLUDE REGEX ".*/gmock/.*")
list(FILTER SOURCES EXCLUDE REGEX ".*mac.*")
list(FILTER SOURCES EXCLUDE REGEX ".*gmock.*")
list(FILTER SOURCES EXCLUDE REGEX ".*test.*")
list(FILTER SOURCES EXCLUDE REGEX ".*fuchsia.*")
list(FILTER SOURCES EXCLUDE REGEX ".*unicode.*")
list(FILTER SOURCES EXCLUDE REGEX ".*_android.*\\.cc")
list(FILTER SOURCES EXCLUDE REGEX ".*_fuchsia.*\\.cc")
list(FILTER SOURCES EXCLUDE REGEX ".*_benchmark.*\\.cc")
list(FILTER SOURCES EXCLUDE REGEX ".*_apple.*\\.cc")
list(FILTER SOURCES EXCLUDE REGEX ".*_icu.*\\.cc")
list(FILTER SOURCES EXCLUDE REGEX ".*_test.*\\.cc")
list(FILTER SOURCES EXCLUDE REGEX ".*_freebsd.*\\.cc")
list(FILTER SOURCES EXCLUDE REGEX ".*_chromeos.*\\.cc")
list(FILTER SOURCES EXCLUDE REGEX ".*dummy_*\\.cc")
list(FILTER SOURCES EXCLUDE REGEX ".*rust_*\\.cc")
list(FILTER SOURCES EXCLUDE REGEX ".*linux_*\\.cc")
list(FILTER SOURCES EXCLUDE REGEX ".*_fuzzer.*\\.cc")
list(FILTER SOURCES EXCLUDE REGEX ".*_unittest_win.*\\.cc")
list(FILTER SOURCES EXCLUDE REGEX ".*_openbsd.*\\.cc")
list(FILTER SOURCES EXCLUDE REGEX ".*_fsevents.*\\.cc")
list(FILTER SOURCES EXCLUDE REGEX ".*allocation_trace.*\\.cc")
list(FILTER SOURCES EXCLUDE REGEX ".*rust_log_integration.*\\.cc")
list(FILTER SOURCES EXCLUDE REGEX ".*scoped_mock_log.*\\.cc")
list(FILTER SOURCES EXCLUDE REGEX ".*benchmarks.*\\.cc")
list(FILTER SOURCES EXCLUDE REGEX ".*elf_reader.*\\.cc")
list(FILTER SOURCES EXCLUDE REGEX ".*perfetto_.*\\.cc")
list(FILTER SOURCES EXCLUDE REGEX ".*file_path_watcher_stub.*\\.cc")
list(FILTER SOURCES EXCLUDE REGEX ".*iopm_power_source_sampling_event_source.*\\.cc")
list(FILTER SOURCES EXCLUDE REGEX ".*linux_util.*\\.cc")
list(FILTER SOURCES EXCLUDE REGEX ".*break_iterator.*\\.cc")
list(FILTER SOURCES EXCLUDE REGEX ".*dangling_raw_ptr_checks.cc.*\\.cc")
list(FILTER SOURCES EXCLUDE REGEX ".*_inotify.*\\.cc")
list(FILTER SOURCES EXCLUDE REGEX ".*cancelable_event_default.*\\.cc")
list(FILTER SOURCES EXCLUDE REGEX ".*demangle.*\\.cc")
list(FILTER SOURCES EXCLUDE REGEX ".*status_matchers.*\\.cc")
list(FILTER SOURCES EXCLUDE REGEX ".*process_tvos.*\\.cc")
list(FILTER SOURCES EXCLUDE REGEX ".*memory_stubs.*\\.cc")
list(FILTER SOURCES EXCLUDE REGEX ".*push_registers_asm.*\\.cc")
list(FILTER SOURCES EXCLUDE REGEX ".*process_stubs.*\\.cc")
list(FILTER SOURCES EXCLUDE REGEX ".*ubrk.*\\.cc")
list(FILTER SOURCES EXCLUDE REGEX ".*raw_ptr_asan_unowned_impl.*\\.cc")
list(FILTER SOURCES EXCLUDE REGEX ".*raw_ptr_backup_ref_impl.*\\.cc")
list(FILTER SOURCES EXCLUDE REGEX ".*raw_ptr_hookable_impl.*\\.cc")
list(FILTER SOURCES EXCLUDE REGEX ".*file_descriptor_store.*\\.cc")
list(FILTER SOURCES EXCLUDE REGEX ".*builtin_categories.*\\.cc")
list(FILTER SOURCES EXCLUDE REGEX ".*interned_args_helper.*\\.cc")
list(FILTER SOURCES EXCLUDE REGEX ".*interceptor.*\\.cc")
list(FILTER SOURCES EXCLUDE REGEX ".*stack_copier_signal.*\\.cc")

# Create static library
add_library(chromium_threading STATIC ${SOURCES})

# Common compile definitions for all targets
set(COMMON_COMPILE_DEFINITIONS
    NO_BASE_TRACING
    OPENSSL_X86_64
    _WIN32_WINNT=0x0A00
    OPENSSL_64_BIT
    OPENSSL_NO_ASM=0
    BASE_EXPORT=
    BASE_NO_EXPORT=
    PA_IMPLEMENTATION
    PA_COMPONENT_BUILD=0
    "PA_COMPONENT_EXPORT(x)="
    PA_BUILDFLAG_USE_PARTITION_ALLOC_AS_MALLOC=1
    DISABLE_FILE_SUPPORT
    NO_TCMALLOC
    NO_LOGGING
    NO_BASE_METRICS
    DCHECK_ALWAYS_ON=0
    LOGGING_DISABLED=1
    ENABLE_BASE_METRICS=0
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
    TRACING_DISABLED=1
    enable_base_tracing=0
    use_blink=0
    OS_WIN
    PA_BUILDFLAG_INTERNAL_ENABLE_BACKUP_REF_PTR_SUPPORT=0
    PA_BUILDFLAG_INTERNAL_ENABLE_GWP_ASAN_SUPPORT=0
    PA_BUILDFLAG_INTERNAL_ENABLE_POINTER_COMPRESSION=0
    PA_BUILDFLAG_INTERNAL_DCHECKS_ARE_ON=0
    PA_BUILDFLAG_INTERNAL_HAS_64_BIT_POINTERS=1
    PA_BUILDFLAG_INTERNAL_DCHECK_IS_CONFIGURABLE=0
    PA_BUILDFLAG_INTERNAL_ENABLE_DANGLING_RAW_PTR_CHECKS=0
    PA_BUILDFLAG_INTERNAL_ENABLE_DANGLING_RAW_PTR_FEATURE_FLAG=0
    PA_BUILDFLAG_INTERNAL_RAW_PTR_ZERO_ON_CONSTRUCT=0
    PA_BUILDFLAG_INTERNAL_RAW_PTR_ZERO_ON_DESTRUCT=0
    PA_BUILDFLAG_INTERNAL_RAW_PTR_ZERO_ON_MOVE=0
    PA_BUILDFLAG_INTERNAL_USE_FREELIST_DISPATCHER=1
    PA_BUILDFLAG_INTERNAL_RECORD_ALLOC_INFO=0
)

# Common Windows libraries for all targets
set(COMMON_WIN_LIBRARIES
    ${CMAKE_SOURCE_DIR}/src/out/threading_base/obj/third_party/boringssl/boringssl.lib
    ws2_32.lib
    kernel32.lib
    user32.lib
    gdi32.lib
    winmm.lib
    imm32.lib
    ole32.lib
    oleaut32.lib
    advapi32.lib
    shell32.lib
    version.lib
    propsys.lib
    ntdll.lib
    setupapi.lib
    cfgmgr32.lib
    shcore.lib
    runtimeobject.lib
    wintrust.lib
    crypt32.lib
    dbghelp.lib
    shlwapi.lib
    wbemuuid.lib
    delayimp.lib
    powrprof.lib
    version.lib
)

# Common compiler options for MSVC/ClangCL
set(COMMON_COMPILE_OPTIONS
    /W3
    /EHsc
    /Zc:__cplusplus
    /wd4267 # Suppress conversion warnings
    /wd4244 # Suppress conversion warnings
    /arch:AVX2
    /bigobj # Enable big object files
)

# Apply settings to the static library
target_compile_definitions(chromium_threading PRIVATE ${COMMON_COMPILE_DEFINITIONS})
target_link_options(chromium_threading PRIVATE /errorlimit:0)
target_compile_options(chromium_threading PRIVATE ${COMMON_COMPILE_OPTIONS})
if(WIN32)
    target_link_libraries(chromium_threading PRIVATE ${COMMON_WIN_LIBRARIES})
endif()

# Add executable for sezMainServer
add_executable(sezMainServer ${MAIN_SERVER_SOURCES})

# Apply settings to sezMainServer
target_compile_definitions(sezMainServer PRIVATE ${COMMON_COMPILE_DEFINITIONS})
target_link_options(sezMainServer PRIVATE /errorlimit:0)
target_compile_options(sezMainServer PRIVATE ${COMMON_COMPILE_OPTIONS})
target_link_libraries(sezMainServer PRIVATE chromium_threading)
if(WIN32)
    target_link_libraries(sezMainServer PRIVATE ${COMMON_WIN_LIBRARIES})
endif()

# Ensure Release mode by default
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Set output directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>)

# Installation
install(TARGETS sezMainServer DESTINATION bin)