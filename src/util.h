#ifndef UTIL_H
#define UTIL_H

#include "vdf_new.h"

struct Segment {
    uint64_t start;
    uint64_t length;
    form x;
    form y;
    form proof;
    bool is_empty = true;

    Segment() = default;
    Segment(Segment&&) = default;
    Segment(Segment const&) = default;
    Segment& operator=(Segment const&) = default;
    Segment& operator=(Segment&&) = default;

    Segment(uint64_t start, uint64_t length, form& x, form& y) {        
        this->start = start;
        this->length = length;
        this->x = x;
        this->y = y;
        is_empty = false;
    }

    bool IsWorseThan(Segment& other) {
        if (is_empty) {
            if (!other.is_empty)
                return true;
            return false;
        }
        if (length > other.length)
            return true;
        if (length < other.length)  
            return false;
        return start > other.start;
    }

    int GetSegmentBucket() {
        uint64_t c_length = length;
        length >>= 16;
        int index = 0;
        while (length > 1) {
            index++;
            if (length == 2 || length == 3) {
                std::cout << "Warning: Invalid segment length.\n";
            }
            length >>= 2;
        }
        length = c_length;
        return index;
    }
};

std::string BytesToStr(const std::vector<unsigned char> &in)
{
    std::vector<unsigned char>::const_iterator from = in.cbegin();
    std::vector<unsigned char>::const_iterator to = in.cend();
    std::ostringstream oss;
    for (; from != to; ++from)
       oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(*from);
    return oss.str();
}

void ApproximateParameters(uint64_t T, uint32_t& L, uint32_t& K) {
    double log_memory = 23.25349666;
    double log_T = log2(T);
    L = 1;
    if (log_T - log_memory > 0.000001) {
        L = ceil(pow(2, log_memory - 20));
    }
    double intermediate = T * (double)0.6931471 / (2.0 * L);
    K = std::max(std::round(log(intermediate) - log(log(intermediate)) + 0.25), 1.0);
}

struct Proof {
    Proof() {

    }

    Proof(std::vector<unsigned char> y, std::vector<unsigned char> proof) {
        this->y = y;
        this->proof = proof;
    }

    string hex() {
        std::vector<unsigned char> bytes(y);
        bytes.insert(bytes.end(), proof.begin(), proof.end());
        return BytesToStr(bytes);
    }

    std::vector<unsigned char> y;
    std::vector<unsigned char> proof;
    uint8_t witness_type;
};

#endif // UTIL_H
