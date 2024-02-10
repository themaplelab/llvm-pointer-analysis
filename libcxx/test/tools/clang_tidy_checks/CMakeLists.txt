
# The find_package changes these variables. This leaves the build in an odd
# state. Calling cmake a second time tries to write site config information in
# the system's libc++. Restoring these setting after testing fixes this issue.
set(LLVM_DIR_SAVE ${LLVM_DIR})
set(Clang_DIR_SAVE ${Clang_DIR})

# libc++ only supports the latest two released versions of clang-tidy. During
# the release period there the new unreleased version is also supported. To keep
# this code simple, always accept the last 3 versions.
set(HEAD ${LLVM_VERSION_MAJOR})
math(EXPR MINUS_1 "${HEAD} - 1")
math(EXPR MINUS_2 "${MINUS_1} - 1")
math(EXPR MINUS_3 "${MINUS_2} - 1")

if("${LIBCXX_CLANG_TIDY_EXECUTABLE}" STREQUAL "")
  set(NAMES "clang-tidy-${HEAD}"
            "clang-tidy-${MINUS_1}"
            "clang-tidy-${MINUS_2}"
            "clang-tidy-${MINUS_3}"
            "clang-tidy")
  if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    string(REGEX MATCH
           "[0-9]+"
           LIBCXX_CLANG_MAJOR
           "${CMAKE_CXX_COMPILER_VERSION}")
    set(NAMES "clang-tidy-${LIBCXX_CLANG_MAJOR}"
              "clang-tidy")
  endif()
  find_program(LIBCXX_CLANG_TIDY NAMES ${NAMES})
else()
  set(LIBCXX_CLANG_TIDY "${LIBCXX_CLANG_TIDY_EXECUTABLE}" CACHE INTERNAL "")
endif()

if("${LIBCXX_CLANG_TIDY}" STREQUAL "")
  set(LIBCXX_CLANG_TIDY "" CACHE INTERNAL "")
  message(STATUS "Could not find a clang-tidy executable;
                  custom libc++ clang-tidy checks will not be available.")
  return()
endif()

execute_process(COMMAND
            "${LIBCXX_CLANG_TIDY}" "--version"
            OUTPUT_VARIABLE CLANG_TIDY_VERSION_OUTPUT
            COMMAND_ERROR_IS_FATAL ANY)
string(REGEX MATCH
       "[0-9]+\.[0-9]+\.[0-9]+"
       LIBCXX_CLANG_TIDY_VERSION
       "${CLANG_TIDY_VERSION_OUTPUT}")

if("${LIBCXX_CLANG_TIDY_VERSION}" VERSION_LESS "${MINUS_3}")
  set(LIBCXX_CLANG_TIDY "" CACHE INTERNAL "")
  message(STATUS "The clang-tidy version found ${LIBCXX_CLANG_TIDY_VERSION} is
                  too old, version ${MINUS_3}.0.0 is required;
                  custom libc++ clang-tidy checks will not be available.")
  return()
endif()

# For modules clang-tidy 17 or newer is required. This is due to the state of
# implementation in clang/clang-tidy. This additional restriction should be
# temporary.
# TODO(LLVM-19) Remove this check
if("${LIBCXX_CLANG_TIDY_VERSION}" VERSION_LESS "17.0.0")
  message(STATUS "The clang-tidy version found ${LIBCXX_CLANG_TIDY_VERSION} is
                  too old, version 17.0.0 is required;
                  custom libc++ clang-tidy checks will not be available.")
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND
   NOT CMAKE_CXX_COMPILER_VERSION VERSION_EQUAL "${LIBCXX_CLANG_TIDY_VERSION}")

  set(LIBCXX_CLANG_TIDY "" CACHE INTERNAL "")
  message(STATUS "The version of clang ${CMAKE_CXX_COMPILER_VERSION} differs
                  from the version of clang-tidy ${LIBCXX_CLANG_TIDY_VERSION};
                  custom libc++ clang-tidy checks will not be available.")
  return()
endif()

# Since the Clang C++ ABI is not stable the Clang libraries and clang-tidy
# version must match. Otherwise there likely will be ODR-violations. This had
# led to crashes and incorrect output of the clang-tidy based checks.
find_package(Clang "${LIBCXX_CLANG_TIDY_VERSION}")

set(SOURCES
    abi_tag_on_virtual.cpp
    header_exportable_declarations.cpp
    hide_from_abi.cpp
    proper_version_checks.cpp
    qualify_declval.cpp
    robust_against_adl.cpp
    uglify_attributes.cpp

    libcpp_module.cpp
   )

if(NOT Clang_FOUND)
  set(LIBCXX_CLANG_TIDY "" CACHE INTERNAL "")
  message(STATUS "Could not find a suitable version of the Clang development package;
                  custom libc++ clang-tidy checks will not be available.")
  return()
endif()

set(LLVM_DIR "${LLVM_DIR_SAVE}" CACHE PATH "The directory containing a CMake configuration file for LLVM." FORCE)
set(Clang_DIR "${Clang_DIR_SAVE}" CACHE PATH "The directory containing a CMake configuration file for Clang." FORCE)

message(STATUS "Found system-installed LLVM ${LLVM_PACKAGE_VERSION} with headers in ${LLVM_INCLUDE_DIRS}")

set(CMAKE_CXX_STANDARD 20)

# Link only against clangTidy itself, not anything that clangTidy uses; otherwise we run setup code multiple times
# which results in clang-tidy crashing
set_target_properties(clangTidy PROPERTIES INTERFACE_LINK_LIBRARIES "")
# ClangTargets.cmake doesn't set the include paths, so we have to do it
target_include_directories(clangTidy INTERFACE
                           ${CLANG_INCLUDE_DIRS}
                           ${LLVM_INCLUDE_DIRS}
                          )
target_compile_options(clangTidy INTERFACE
                       -fno-rtti
                       -fno-sanitize=address,hwaddress,undefined,thread,leak # ignore any sanitizers
                      )

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
  target_compile_options(clangTidy INTERFACE
                         -fno-sanitize=memory,dataflow
                        )
endif()

add_library(cxx-tidy MODULE ${SOURCES})
target_link_libraries(cxx-tidy clangTidy)

set_target_properties(cxx-tidy PROPERTIES
                      CXX_STANDARD 20
                      CXX_STANDARD_REQUIRED YES
                      CXX_EXTENSIONS NO)

set_target_properties(cxx-tidy PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_SHARED_MODULE_SUFFIX_CXX .plugin) # Use a portable suffix to simplify how we can find it from Lit
