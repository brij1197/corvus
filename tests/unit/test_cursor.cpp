#include <gtest/gtest.h>
#include "corvus/resources/cursor.h"
#include "corvus/resources/resource_repository.h"

using namespace corvus::resources;

namespace
{
    const CursorPosition kPos{"2026-11-08T07:15:42.123456Z",
                              "3f2b8c1e-9a4d-4e6f-8b2a-1c3d5e7f9a0b"};
}

TEST(CursorTest, RoundTrips)
{
    EXPECT_EQ(decode_cursor(encode_cursor(kPos)), kPos);
}

TEST(CursorTest, EncodingIsUrlSafeAndUnpadded)
{
    std::string all;
    for (int i = 0; i < 256; ++i)
        all.push_back(static_cast<char>(i));
    for (std::size_t len = 0; len <= all.size(); ++len)
    {
        const auto enc = base64url_encode(all.substr(0, len));
        EXPECT_EQ(enc.find_first_of("+/= "), std::string::npos) << enc;
        EXPECT_EQ(base64url_decode(enc), all.substr(0, len));
    }
}

TEST(CursorTest, MatchesRfc4648Vectors)
{
    EXPECT_EQ(base64url_encode(""), "");
    EXPECT_EQ(base64url_encode("f"), "Zg");
    EXPECT_EQ(base64url_encode("fo"), "Zm8");
    EXPECT_EQ(base64url_encode("foo"), "Zm9v");
    EXPECT_EQ(base64url_encode("foob"), "Zm9vYg");
    EXPECT_EQ(base64url_encode("\xfb\xff"), "-_8");
}

TEST(CursorTest, AcceptsPaddingAndOldStandardAlphabet)
{
    EXPECT_EQ(base64url_decode("Zm8="), "fo");
    EXPECT_EQ(base64url_decode("+/8"), "\xfb\xff");
}

TEST(CursorTest, RejectsSpacesInsteadOfSkippingThem)
{
    auto enc = encode_cursor(kPos);
    enc[4] = ' ';
    EXPECT_THROW(decode_cursor(enc), InvalidCursorError);
}

TEST(CursorTest, RejectsJunk)
{
    for (const std::string bad : {"", "not-valid-base64!!!", "A", "@@@@"})
        EXPECT_THROW(decode_cursor(bad), InvalidCursorError) << bad;
}

TEST(CursorTest, RejectsWellEncodedGarbage)
{
    for (const std::string payload :
         {"no-separator", "|", "x|y", "2026-11-08T07:15:42.123456Z|not-a-uuid",
          "yesterday|3f2b8c1e-9a4d-4e6f-8b2a-1c3d5e7f9a0b",
          "2026-11-08T07:15:42.123456Z|3f2b8c1e-9a4d-4e6f-8b2a-1c3d5e7f9a0b' OR '1'='1"})
        EXPECT_THROW(decode_cursor(base64url_encode(payload)), InvalidCursorError) << payload;
}

TEST(CursorTest, RejectsOversizedCursor)
{
    EXPECT_THROW(decode_cursor(std::string(200, 'A')), InvalidCursorError);
}