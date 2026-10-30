# SPDX-License-Identifier: Apache-2.0
#
# Builds GoogleTest + GoogleMock from the west project 'googletest' (see west.yml)
# into the image. Test-only: enabled by CONFIG_GTEST from a test's prj.conf.

set(GTEST_ROOT "" CACHE PATH "GoogleTest source checkout (default: the 'googletest' west project)")

if(NOT GTEST_ROOT AND WEST)
    execute_process(
        COMMAND ${WEST} list -f "{abspath}" googletest
        WORKING_DIRECTORY ${ZEPHYR_BASE}
        OUTPUT_VARIABLE GTEST_ROOT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
endif()

if(NOT EXISTS "${GTEST_ROOT}/googletest/src/gtest-all.cc")
    message(FATAL_ERROR
        "CONFIG_GTEST=y but GoogleTest sources were not found (GTEST_ROOT='${GTEST_ROOT}'). "
        "Run 'west update googletest' or pass -DGTEST_ROOT=<path to googletest checkout>.")
endif()

zephyr_library()

# Third-party sources: silence warnings so Zephyr's warning flags (and -Werror in
# apps) do not trip on them.
zephyr_library_compile_options(-w)

zephyr_library_sources(
    ${GTEST_ROOT}/googletest/src/gtest-all.cc
    ${GTEST_ROOT}/googlemock/src/gmock-all.cc
)
zephyr_library_sources_ifdef(CONFIG_GTEST_MAIN gtest_main.cpp)

# Public headers for tests: <gtest/gtest.h>, <gmock/gmock.h>.
zephyr_include_directories(
    ${GTEST_ROOT}/googletest/include
    ${GTEST_ROOT}/googlemock/include
)

# The gtest-all.cc / gmock-all.cc amalgamations include their sources relative to these.
zephyr_library_include_directories(
    ${GTEST_ROOT}/googletest
    ${GTEST_ROOT}/googlemock
)
