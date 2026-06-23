#include "core/transport/framed_stdio.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace {

TEST(FramedStdioTest, RoundTripsFrame) {
    const std::string payload = "hello";
    std::string error;

    std::ostringstream out(std::ios::binary);
    ASSERT_TRUE(anolis_provider_ezo::transport::write_frame(out, reinterpret_cast<const uint8_t *>(payload.data()),
                                                            payload.size(), error))
        << error;

    std::istringstream in(out.str(), std::ios::binary);
    std::vector<uint8_t> frame;
    ASSERT_TRUE(anolis_provider_ezo::transport::read_frame(in, frame, error)) << error;

    ASSERT_EQ(frame.size(), payload.size());
    EXPECT_EQ(std::string(reinterpret_cast<const char *>(frame.data()), frame.size()), payload);
}

TEST(FramedStdioTest, RejectsZeroLengthFrameWrite) {
    std::ostringstream out(std::ios::binary);
    std::string error;

    EXPECT_FALSE(anolis_provider_ezo::transport::write_frame(out, nullptr, 0, error));
    EXPECT_NE(error.find("invalid frame length"), std::string::npos);
}

}  // namespace
