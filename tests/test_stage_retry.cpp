#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include "StageRetry.hpp"

TEST_CASE("retry runs only for the scheduled generation and stage") {
    const StageRetryTicket t{3, 2};
    CHECK(should_run_stage_retry(t, {3, 2}, true, false, false));
    CHECK_FALSE(should_run_stage_retry(t, {4, 2}, true, false, false));
    CHECK_FALSE(should_run_stage_retry(t, {3, 3}, true, false, false));
}

TEST_CASE("retry never runs once cancelled, closing or destroyed") {
    const StageRetryTicket t{1, 1};
    CHECK_FALSE(should_run_stage_retry(t, t, true, true, false));
    CHECK_FALSE(should_run_stage_retry(t, t, true, false, true));
    CHECK_FALSE(should_run_stage_retry(t, t, false, false, false));
}
