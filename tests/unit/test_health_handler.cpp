#include <gtest/gtest.h>
#include "corvus/version.h"


namespace {

TEST(HealthHandlerTest, VersionStringAvailableToHandler)
{
    EXPECT_EQ(corvus::version::string(), "0.1.0");
}

} // namespace
