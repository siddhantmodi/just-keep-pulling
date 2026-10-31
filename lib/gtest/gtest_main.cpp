/**
 * @file gtest_main.cpp
 * @brief GoogleTest entry point for native_sim test images.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <cstdlib>
#include <gmock/gmock.h>

int main()
{
    // gtest/gmock may rewrite argv while consuming recognized flags, so give it a
    // mutable program name with the conventional nullptr terminator.
    static char arg0[] = "gtest";
    int argc = 1;
    char *argv[] = { arg0, nullptr };

    ::testing::InitGoogleMock( &argc, argv ); // Also initializes GoogleTest
    std::exit( RUN_ALL_TESTS() );
}
