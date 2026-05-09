#include <gtest/gtest.h>
#include "corvus/version.h"

namespace
{

    TEST(VersionTest, StringIsNonEmpty)
    {
        EXPECT_FALSE(corvus::version::string().empty());
    }

    TEST(VersionTest, StringMatchesMajorMinorPatch)
    {
        const auto v = std::string{corvus::version::string()};

        const auto expected = std::to_string(corvus::version::major_v) + "." +
                              std::to_string(corvus::version::minor_v) + "." +
                              std::to_string(corvus::version::patch_v);

        EXPECT_EQ(v, expected);
    }

    TEST(VersionTest, StringContainsTwoDots)
    {
        const auto v = std::string{corvus::version::string()};
        const auto dot_count = std::count(v.begin(), v.end(), '.');
        EXPECT_EQ(dot_count, 2);
    }

    TEST(VersionTest, StringHasNoLeadingOrTrailingWhitespace)
    {
        const auto v = std::string{corvus::version::string()};
        EXPECT_NE(v.front(), ' ');
        EXPECT_NE(v.back(), ' ');
    }

    TEST(VersionTest, MajorIsNonNegative)
    {
        EXPECT_GE(corvus::version::major_v, 0);
    }

    TEST(VersionTest, MinorIsNonNegative)
    {
        EXPECT_GE(corvus::version::minor_v, 0);
    }

    TEST(VersionTest, PatchIsNonNegative)
    {
        EXPECT_GE(corvus::version::patch_v, 0);
    }

    TEST(VersionTest, CurrentVersionIs010)
    {
        EXPECT_EQ(corvus::version::major_v, 0);
        EXPECT_EQ(corvus::version::minor_v, 1);
        EXPECT_EQ(corvus::version::patch_v, 0);
        EXPECT_EQ(corvus::version::string(), "0.1.0");
    }

    TEST(VersionTest, StringIsUsableAtCompileTime)
    {
        constexpr auto v = corvus::version::string();
        EXPECT_FALSE(v.empty());
    }

    TEST(VersionTest, ComponentsAreUsableAtCompileTime)
    {
        constexpr int major = corvus::version::major_v;
        constexpr int minor = corvus::version::minor_v;
        constexpr int patch = corvus::version::patch_v;
        EXPECT_GE(major + minor + patch, 0);
    }

} // namespace
