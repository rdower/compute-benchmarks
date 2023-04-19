/*
 * Copyright (C) 2022-2023 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "definitions/reduction4.h"

#include "framework/test_case/register_test_case.h"
#include "framework/utility/common_gtest_args.h"
#include "framework/utility/memory_constants.h"

#include <gtest/gtest.h>

[[maybe_unused]] static const inline RegisterTestCase<Reduction4> registerTestCase{};

class Reduction4Tests : public ::testing::TestWithParam<std::tuple<Api, size_t>> {
};

TEST_P(Reduction4Tests, Test) {
    ReductionArguments4 args;
    args.api = std::get<0>(GetParam());
    args.numberOfElements = std::get<1>(GetParam());

    Reduction4 test;
    test.run(args);
}

using namespace MemoryConstants;
INSTANTIATE_TEST_SUITE_P(
    Reduction4Tests,
    Reduction4Tests,
    ::testing::Combine(
        ::CommonGtestArgs::allApis(),
        ::testing::Values(128000000, 256000000, 512000000)));
