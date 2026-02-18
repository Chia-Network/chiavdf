#pragma once

#include <boost/asio.hpp>
#include "bqfc.h"

#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>

inline char disc[350];
inline uint8_t initial_form_s[BQFC_FORM_SIZE];

inline void InitSession(boost::asio::ip::tcp::socket& sock) {
    boost::system::error_code error;
    char disc_size[5];
    int disc_int_size;
    auto check_read_error = [&](const char* field_name) {
        if (error == boost::asio::error::eof) {
            throw std::runtime_error(std::string("Connection closed while reading ") + field_name);
        } else if (error) {
            throw boost::system::system_error(error);
        }
    };

    memset(disc, 0x00, sizeof(disc)); // For null termination
    memset(disc_size, 0x00, sizeof(disc_size)); // For null termination

    boost::asio::read(sock, boost::asio::buffer(disc_size, 3), error);
    check_read_error("discriminant size");
    disc_int_size = atoi(disc_size);
    if (disc_int_size <= 0 || disc_int_size >= (int)sizeof(disc)) {
        throw std::runtime_error("Invalid discriminant size");
    }
    boost::asio::read(sock, boost::asio::buffer(disc, disc_int_size), error);
    check_read_error("discriminant");

    // Signed char is intentional: values 128-255 wrap negative, caught by the <= 0 check below
    char form_size;
    boost::asio::read(sock, boost::asio::buffer(&form_size, 1), error);
    check_read_error("form size");
    if (form_size <= 0 || form_size > (int)sizeof(initial_form_s)) {
        throw std::runtime_error("Invalid form size");
    }
    boost::asio::read(sock, boost::asio::buffer(initial_form_s, form_size), error);
    check_read_error("initial form");
}
