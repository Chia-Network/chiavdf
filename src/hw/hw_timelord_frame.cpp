#include "hw_proof.hpp"

#include <cstring>

static void append_hex(std::vector<uint8_t> &out, const uint8_t *data, size_t size)
{
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < size; i++) {
        out.push_back(hex[data[i] >> 4]);
        out.push_back(hex[data[i] & 0xf]);
    }
}

std::vector<uint8_t> hw_proof_timelord_frame(const vdf_proof_delivery &delivery)
{
    const size_t count = delivery.checkpoints.size();
    uint8_t header[8 + 8 + 1 + BQFC_FORM_SIZE * 2];
    const vdf_proof *proof = &delivery.final;

    Int64ToBytes(&header[0], proof->iters);
    Int64ToBytes(&header[8], BQFC_FORM_SIZE);
    memcpy(&header[16], proof->y, BQFC_FORM_SIZE);
    header[16 + BQFC_FORM_SIZE] = static_cast<uint8_t>(count);
    memcpy(&header[17 + BQFC_FORM_SIZE], proof->proof, BQFC_FORM_SIZE);

    const size_t payload = sizeof(header) + count * sizeof(hw_timelord_segment);
    std::vector<uint8_t> out(4);
    Int32ToBytes(out.data(), static_cast<uint32_t>(payload * 2));
    append_hex(out, header, sizeof(header));
    for (const vdf_proof &checkpoint : delivery.checkpoints) {
        hw_timelord_segment segment{};
        Int64ToBytes(segment.iters, checkpoint.seg_iters);
        memcpy(segment.B, checkpoint.B, sizeof(segment.B));
        memcpy(segment.proof, checkpoint.proof, sizeof(segment.proof));
        append_hex(out, reinterpret_cast<uint8_t *>(&segment), sizeof(segment));
    }
    return out;
}
