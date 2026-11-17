#include <gtest/gtest.h>
#include "core/networking/message_frame.hpp"
#include "core/common/uuid.hpp"

using namespace dicp;

TEST(MessageFrameTest, EncodeAndDecodeValidEnvelope) {
    proto::Envelope env;
    env.set_protocol_version(1);
    env.set_sender_node_id("node-alice");
    env.set_message_id(GenerateUUID());
    env.set_sender_sequence(42);
    env.set_type(proto::MSG_PING);

    auto* ping = env.mutable_ping();
    ping->set_probe_id("probe-123");
    ping->set_target_node_id("node-bob");
    ping->set_sender_incarnation(5);

    auto encoded = MessageFrame::Encode(env, 1200);
    ASSERT_FALSE(encoded.empty());
    EXPECT_GT(encoded.size(), MessageFrame::HEADER_SIZE);

    auto decoded_opt = MessageFrame::Decode(encoded, 1200);
    ASSERT_TRUE(decoded_opt.has_value());

    const auto& decoded = *decoded_opt;
    EXPECT_EQ(decoded.sender_node_id(), "node-alice");
    EXPECT_EQ(decoded.sender_sequence(), 42ULL);
    EXPECT_EQ(decoded.type(), proto::MSG_PING);
    EXPECT_EQ(decoded.ping().probe_id(), "probe-123");
    EXPECT_EQ(decoded.ping().sender_incarnation(), 5ULL);
}

TEST(MessageFrameTest, RejectsInvalidMagic) {
    proto::Envelope env;
    env.set_protocol_version(1);
    env.set_type(proto::MSG_PING);

    auto encoded = MessageFrame::Encode(env, 1200);
    ASSERT_FALSE(encoded.empty());

    // Corrupt magic bytes
    encoded[0] = 0xFF;
    auto decoded_opt = MessageFrame::Decode(encoded, 1200);
    EXPECT_FALSE(decoded_opt.has_value());
}

TEST(MessageFrameTest, RejectsTruncatedBuffer) {
    proto::Envelope env;
    env.set_protocol_version(1);
    env.set_type(proto::MSG_PING);
    env.mutable_ping()->set_probe_id("test");

    auto encoded = MessageFrame::Encode(env, 1200);
    ASSERT_FALSE(encoded.empty());

    // Pass truncated size
    auto decoded_opt = MessageFrame::Decode(encoded.data(), encoded.size() - 2, 1200);
    EXPECT_FALSE(decoded_opt.has_value());
}

TEST(MessageFrameTest, RejectsOversizedMessage) {
    proto::Envelope env;
    env.set_protocol_version(1);
    env.set_type(proto::MSG_PING);
    std::string large_string(2000, 'X');
    env.mutable_ping()->set_probe_id(large_string);

    // Max limit is 1200
    auto encoded = MessageFrame::Encode(env, 1200);
    EXPECT_TRUE(encoded.empty());
}
