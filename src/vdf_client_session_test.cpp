#include "vdf_client_session.h"

#include <boost/asio.hpp>
#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <exception>
#include <string>
#include <thread>
#include <vector>

namespace {

std::string run_init_session_with_payload(const std::vector<uint8_t>& payload) {
    boost::asio::io_context io;
    using boost::asio::ip::tcp;

    tcp::acceptor acceptor(io, tcp::endpoint(tcp::v4(), 0));
    const uint16_t port = acceptor.local_endpoint().port();

    std::thread server([&]() {
        boost::system::error_code server_error;
        tcp::socket peer(io);
        acceptor.accept(peer, server_error);
        if (server_error) {
            return;
        }
        if (!payload.empty()) {
            boost::asio::write(peer, boost::asio::buffer(payload), server_error);
        }
        peer.shutdown(tcp::socket::shutdown_send, server_error);
        peer.close(server_error);
    });

    std::string error_message;
    try {
        tcp::socket client(io);
        client.connect(tcp::endpoint(boost::asio::ip::address_v4::loopback(), port));
        InitSession(client);
    } catch (const std::exception& e) {
        error_message = e.what();
    }

    server.join();
    return error_message;
}

}  // namespace

TEST(VdfClientSessionRegressionTest, TruncatedDiscriminantSizeFailsBeforeParse) {
    const std::vector<uint8_t> payload = {'0', '3'};
    const std::string error = run_init_session_with_payload(payload);
    EXPECT_NE(error.find("Connection closed while reading discriminant size"), std::string::npos);
}

TEST(VdfClientSessionRegressionTest, ZeroDiscriminantSizeIsRejected) {
    const std::vector<uint8_t> payload = {'0', '0', '0'};
    const std::string error = run_init_session_with_payload(payload);
    EXPECT_NE(error.find("Invalid discriminant size"), std::string::npos);
}

TEST(VdfClientSessionRegressionTest, OversizedDiscriminantSizeIsRejected) {
    const std::vector<uint8_t> payload = {'3', '5', '0'};
    const std::string error = run_init_session_with_payload(payload);
    EXPECT_NE(error.find("Invalid discriminant size"), std::string::npos);
}

TEST(VdfClientSessionRegressionTest, TruncatedFormSizeFailsBeforeValidation) {
    const std::vector<uint8_t> payload = {'0', '0', '3', 'a', 'b', 'c'};
    const std::string error = run_init_session_with_payload(payload);
    EXPECT_NE(error.find("Connection closed while reading form size"), std::string::npos);
}

TEST(VdfClientSessionRegressionTest, ZeroFormSizeIsRejected) {
    const std::vector<uint8_t> payload = {'0', '0', '3', 'a', 'b', 'c', 0x00};
    const std::string error = run_init_session_with_payload(payload);
    EXPECT_NE(error.find("Invalid form size"), std::string::npos);
}

TEST(VdfClientSessionRegressionTest, WrappedNegativeFormSizeIsRejected) {
    // 0xFF arrives as -1 in signed char and must be rejected by the <= 0 check.
    const std::vector<uint8_t> payload = {'0', '0', '3', 'a', 'b', 'c', 0xFF};
    const std::string error = run_init_session_with_payload(payload);
    EXPECT_NE(error.find("Invalid form size"), std::string::npos);
}

TEST(VdfClientSessionRegressionTest, ValidPayloadParsesWithoutProtocolOverread) {
    const std::vector<uint8_t> payload = {'0', '0', '3', 'a', 'b', 'c', 0x05, 0x10, 0x20, 0x30, 0x40, 0x50};
    const std::string error = run_init_session_with_payload(payload);
    EXPECT_TRUE(error.empty());

    EXPECT_EQ(std::strncmp(disc, "abc", 3), 0);
    EXPECT_EQ(initial_form_s[0], 0x10);
    EXPECT_EQ(initial_form_s[1], 0x20);
    EXPECT_EQ(initial_form_s[2], 0x30);
    EXPECT_EQ(initial_form_s[3], 0x40);
    EXPECT_EQ(initial_form_s[4], 0x50);
}
