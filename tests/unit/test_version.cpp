#include <gtest/gtest.h>
#include "corvus/version.h"

namespace
{

    TEST(VersionTest, StringIsNonEmpty)
    {
        EXPECT_FALSE(corvus::version::string().empty());
    }

    TEST(VersionTest, MajorMinorPatchAreNonNegative)
    {
        EXPECT_GE(corvus::version::major_v, 0);
        EXPECT_GE(corvus::version::minor_v, 0);
        EXPECT_GE(corvus::version::patch_v, 0);
    }

    TEST(VersionTest, StringIsCorrectVersion)
    {
        EXPECT_EQ(corvus::version::string(), "0.1.0");
    }

} // namespace
