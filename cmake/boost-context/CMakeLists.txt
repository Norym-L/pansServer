set(PANS_BOOST_CONTEXT_SOURCE_DIR "${PROJECT_SOURCE_DIR}/third_party/boost_1_92_0" CACHE PATH "Boost.Context source")
set(_pans_boost_context_required_files
        "boost/context/fiber.hpp"
        "boost/context/detail/exception.hpp"
        "boost/version.hpp"
        "libs/context/src/fcontext.cpp"
)

foreach (_pans_boost_context_file IN LISTS _pans_boost_context_required_files)
    if (NOT EXISTS "${PANS_BOOST_CONTEXT_SOURCE_DIR}/${_pans_boost_context_file}")
        message(FATAL_ERROR
                "Incomplete Boost.Context source tree: missing "
                "${PANS_BOOST_CONTEXT_SOURCE_DIR}/${_pans_boost_context_file}"
        )
    endif ()
endforeach ()

file(STRINGS
        "${PANS_BOOST_CONTEXT_SOURCE_DIR}/boost/version.hpp"
        _pans_boost_version_definition
        REGEX "^#define BOOST_VERSION [0-9]+$"
        LIMIT_COUNT 1
)

if (NOT _pans_boost_version_definition)
    message(FATAL_ERROR
            "Cannot determine Boost version from "
            "${PANS_BOOST_CONTEXT_SOURCE_DIR}/boost/version.hpp"
    )
endif ()

string(REGEX REPLACE
        "^#define BOOST_VERSION ([0-9]+)$"
        "\\1"
        _pans_boost_version_number
        "${_pans_boost_version_definition}"
)
math(EXPR _pans_boost_version_major "${_pans_boost_version_number} / 100000")
math(EXPR _pans_boost_version_minor "(${_pans_boost_version_number} / 100) % 1000")
math(EXPR _pans_boost_version_patch "${_pans_boost_version_number} % 100")
set(PANS_BOOST_CONTEXT_VERSION "${_pans_boost_version_major}.${_pans_boost_version_minor}.${_pans_boost_version_patch}")

if (WIN32 OR CYGWIN)
    set(_pans_boost_context_default_binary_format pe)
elseif (APPLE)
    set(_pans_boost_context_default_binary_format mach-o)
else ()
    set(_pans_boost_context_default_binary_format elf)
endif ()
set(BOOST_CONTEXT_BINARY_FORMAT "${_pans_boost_context_default_binary_format}" CACHE STRING "Boost.Context binary format")

math(EXPR _pans_boost_context_bits "${CMAKE_SIZEOF_VOID_P} * 8")

if (CMAKE_SYSTEM_PROCESSOR MATCHES "^[Aa][Rr][Mm]"
        OR CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
    set(_pans_boost_context_default_abi aapcs)
elseif (WIN32 OR CYGWIN)
    set(_pans_boost_context_default_abi ms)
elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "^mips")
    if (_pans_boost_context_bits EQUAL 32)
        set(_pans_boost_context_default_abi o32)
    else ()
        set(_pans_boost_context_default_abi n64)
    endif ()
else ()
    set(_pans_boost_context_default_abi sysv)
endif ()
set(BOOST_CONTEXT_ABI "${_pans_boost_context_default_abi}" CACHE STRING "Boost.Context ABI")
set(_pans_boost_context_architectures arm arm64 e2k loongarch64 mips32 mips64 ppc32 ppc64 riscv64 s390x i386 x86_64 combined)

if (CMAKE_SYSTEM_PROCESSOR IN_LIST _pans_boost_context_architectures)
    set(_pans_boost_context_default_architecture "${CMAKE_SYSTEM_PROCESSOR}")
elseif (_pans_boost_context_bits EQUAL 32) # 32位CPU
    if (CMAKE_SYSTEM_PROCESSOR MATCHES "^[Aa][Rr][Mm]")
        set(_pans_boost_context_default_architecture arm) # ARM系列，arm
    elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "^mips")
        set(_pans_boost_context_default_architecture mips32) # MIPS系列，mips32
    elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "^(powerpc|ppc32)")
        set(_pans_boost_context_default_architecture ppc32) # PowerPC系列
    else ()
        set(_pans_boost_context_default_architecture i386) # 无法识别的默认i386
    endif ()
else () # 64位CPU
    if (CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64" OR CMAKE_SYSTEM_PROCESSOR MATCHES "^[Aa][Rr][Mm]")
        set(_pans_boost_context_default_architecture arm64) # aarch64或ARM系列，arm64
    elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "^[Ee]2[Kk]")
        set(_pans_boost_context_default_architecture e2k) # e2k
    elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "^mips")
        set(_pans_boost_context_default_architecture mips64) # MIPS系列，mips64
    elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "^(powerpc|ppc64)")
        set(_pans_boost_context_default_architecture ppc64) # PowerPC系列，ppc64
    else ()
        set(_pans_boost_context_default_architecture x86_64) # 无法识别的默认是x86_64
    endif ()
endif ()
set(BOOST_CONTEXT_ARCHITECTURE "${_pans_boost_context_default_architecture}" CACHE STRING "Boost.Context architecture")

if (MSVC)
    if (BOOST_CONTEXT_ARCHITECTURE STREQUAL "arm64"
            OR BOOST_CONTEXT_ARCHITECTURE STREQUAL "arm")
        set(_pans_boost_context_default_assembler armasm)
    else ()
        set(_pans_boost_context_default_assembler masm)
    endif ()
elseif (BOOST_CONTEXT_ARCHITECTURE STREQUAL "arm64" AND MINGW)
    set(_pans_boost_context_default_assembler armclang)
else ()
    set(_pans_boost_context_default_assembler gas)
endif ()
set(BOOST_CONTEXT_ASSEMBLER "${_pans_boost_context_default_assembler}" CACHE STRING "Boost.Context assembler")

if (BOOST_CONTEXT_ASSEMBLER STREQUAL "armclang")
    set(_pans_boost_context_default_asm_suffix .S)
elseif (BOOST_CONTEXT_BINARY_FORMAT STREQUAL "pe")
    set(_pans_boost_context_default_asm_suffix .asm)
elseif (BOOST_CONTEXT_ASSEMBLER STREQUAL "gas")
    set(_pans_boost_context_default_asm_suffix .S)
else ()
    set(_pans_boost_context_default_asm_suffix .asm)
endif ()
set(BOOST_CONTEXT_ASM_SUFFIX "${_pans_boost_context_default_asm_suffix}" CACHE STRING "Boost.Context assembler source suffix")

if (BOOST_CONTEXT_ARCHITECTURE STREQUAL "e2k")
    set(_pans_boost_context_default_implementation ucontext)
else ()
    set(_pans_boost_context_default_implementation fcontext)
endif ()
set(BOOST_CONTEXT_IMPLEMENTATION "${_pans_boost_context_default_implementation}" CACHE STRING "Boost.Context implementation")

message(STATUS
        "Boost.Context ${PANS_BOOST_CONTEXT_VERSION}: architecture ${BOOST_CONTEXT_ARCHITECTURE}, "
        "binary format ${BOOST_CONTEXT_BINARY_FORMAT}, ABI ${BOOST_CONTEXT_ABI}, "
        "assembler ${BOOST_CONTEXT_ASSEMBLER}, implementation ${BOOST_CONTEXT_IMPLEMENTATION}"
)

set(_pans_boost_context_source_root "${PANS_BOOST_CONTEXT_SOURCE_DIR}/libs/context/src")
set(_pans_boost_context_sources)
set(_pans_boost_context_compile_definitions)

if (BOOST_CONTEXT_IMPLEMENTATION STREQUAL "fcontext")
    if (BOOST_CONTEXT_ASSEMBLER STREQUAL "gas" OR BOOST_CONTEXT_ASSEMBLER STREQUAL "armclang")
        if (CMAKE_CXX_PLATFORM_ID MATCHES "Cygwin")
            set(_pans_boost_context_asm_language ASM-ATT)
        else ()
            set(_pans_boost_context_asm_language ASM)
        endif ()
    elseif (BOOST_CONTEXT_ASSEMBLER STREQUAL "armasm")
        if (MSVC)
            set(_pans_boost_context_asm_language ASM_MARMASM)
        else ()
            set(_pans_boost_context_asm_language ASM_ARMASM)
        endif ()
    else ()
        set(_pans_boost_context_asm_language ASM_MASM)
    endif ()

    set(_pans_boost_context_source_binary_format "${BOOST_CONTEXT_BINARY_FORMAT}")
    if (_pans_boost_context_source_binary_format STREQUAL "mach-o")
        set(_pans_boost_context_source_binary_format macho)
    endif ()
    string(CONCAT _pans_boost_context_asm_suffix
            "${BOOST_CONTEXT_ARCHITECTURE}_${BOOST_CONTEXT_ABI}_"
            "${_pans_boost_context_source_binary_format}_${BOOST_CONTEXT_ASSEMBLER}"
            "${BOOST_CONTEXT_ASM_SUFFIX}"
    )
    set(_pans_boost_context_asm_sources
            "${_pans_boost_context_source_root}/asm/make_${_pans_boost_context_asm_suffix}"
            "${_pans_boost_context_source_root}/asm/jump_${_pans_boost_context_asm_suffix}"
            "${_pans_boost_context_source_root}/asm/ontop_${_pans_boost_context_asm_suffix}"
    )

    foreach (_pans_boost_context_asm_source IN LISTS _pans_boost_context_asm_sources)
        if (NOT EXISTS "${_pans_boost_context_asm_source}")
            message(FATAL_ERROR
                    "Boost.Context does not contain an implementation for this platform: "
                    "${_pans_boost_context_asm_source}"
            )
        endif ()
    endforeach ()

    enable_language("${_pans_boost_context_asm_language}")
    set_source_files_properties(${_pans_boost_context_asm_sources} PROPERTIES LANGUAGE "${_pans_boost_context_asm_language}")
    if (BOOST_CONTEXT_ASSEMBLER STREQUAL "masm")
        set_property(SOURCE ${_pans_boost_context_asm_sources} APPEND PROPERTY COMPILE_OPTIONS "/nologo") # 隐藏启动信息
        if (MSVC AND NOT MSVC_VERSION LESS 1936 AND NOT CMAKE_CXX_SIMULATE_VERSION)
            set_property(SOURCE ${_pans_boost_context_asm_sources} APPEND PROPERTY COMPILE_OPTIONS "/quiet") # 减少MASM输出
        endif ()
        if (BOOST_CONTEXT_ARCHITECTURE STREQUAL "i386")
            set_property(SOURCE ${_pans_boost_context_asm_sources} APPEND PROPERTY COMPILE_OPTIONS "/safeseh") # 32位添加/safeseh用于生成安全异常处理信息
        endif ()
    elseif (NOT BOOST_CONTEXT_ASSEMBLER STREQUAL "armasm")
        if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            set_property(SOURCE ${_pans_boost_context_asm_sources} APPEND PROPERTY COMPILE_OPTIONS "-x" "assembler-with-cpp")
        elseif (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
            set_property(SOURCE ${_pans_boost_context_asm_sources} APPEND PROPERTY COMPILE_OPTIONS "-Wno-unused-command-line-argument")
        endif ()
    endif ()

    list(APPEND _pans_boost_context_sources
            ${_pans_boost_context_asm_sources}
            "${_pans_boost_context_source_root}/fcontext.cpp"
    )
elseif (BOOST_CONTEXT_IMPLEMENTATION STREQUAL "ucontext")
    list(APPEND _pans_boost_context_sources
            "${_pans_boost_context_source_root}/continuation.cpp"
            "${_pans_boost_context_source_root}/fiber.cpp"
    )
    list(APPEND _pans_boost_context_compile_definitions BOOST_USE_UCONTEXT=)
elseif (BOOST_CONTEXT_IMPLEMENTATION STREQUAL "winfib")
    list(APPEND _pans_boost_context_sources
            "${_pans_boost_context_source_root}/continuation.cpp"
            "${_pans_boost_context_source_root}/fiber.cpp"
    )
    list(APPEND _pans_boost_context_compile_definitions BOOST_USE_WINFIB=)
else ()
    message(FATAL_ERROR
            "Unsupported BOOST_CONTEXT_IMPLEMENTATION=${BOOST_CONTEXT_IMPLEMENTATION}"
    )
endif ()

if (WIN32 AND NOT CMAKE_CXX_PLATFORM_ID MATCHES "Cygwin")
    list(APPEND _pans_boost_context_sources
            "${_pans_boost_context_source_root}/windows/stack_traits.cpp"
    )
else ()
    list(APPEND _pans_boost_context_sources
            "${_pans_boost_context_source_root}/posix/stack_traits.cpp"
    )
endif ()

add_library(boost_context STATIC ${_pans_boost_context_sources})
add_library(Boost::context ALIAS boost_context)

target_compile_features(boost_context PUBLIC cxx_std_11)
target_include_directories(boost_context
        PUBLIC "$<BUILD_INTERFACE:${PANS_BOOST_CONTEXT_SOURCE_DIR}>"
)

target_compile_definitions(boost_context
        PUBLIC
        BOOST_CONTEXT_NO_LIB=
        BOOST_CONTEXT_STATIC_LINK=
        BOOST_CONTEXT_EXPORT=
        ${_pans_boost_context_compile_definitions}
        PRIVATE BOOST_CONTEXT_SOURCE=
)

set_target_properties(boost_context PROPERTIES OUTPUT_NAME boost_context)
install(TARGETS boost_context
        EXPORT pansTargets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

unset(_pans_boost_context_required_files)
unset(_pans_boost_context_file)
unset(_pans_boost_version_definition)
unset(_pans_boost_version_number)
unset(_pans_boost_version_major)
unset(_pans_boost_version_minor)
unset(_pans_boost_version_patch)
unset(_pans_boost_context_default_binary_format)
unset(_pans_boost_context_bits)
unset(_pans_boost_context_default_abi)
unset(_pans_boost_context_architectures)
unset(_pans_boost_context_default_architecture)
unset(_pans_boost_context_default_assembler)
unset(_pans_boost_context_default_asm_suffix)
unset(_pans_boost_context_default_implementation)
unset(_pans_boost_context_source_root)
unset(_pans_boost_context_sources)
unset(_pans_boost_context_compile_definitions)
unset(_pans_boost_context_asm_language)
unset(_pans_boost_context_source_binary_format)
unset(_pans_boost_context_asm_suffix)
unset(_pans_boost_context_asm_sources)
unset(_pans_boost_context_asm_source)
