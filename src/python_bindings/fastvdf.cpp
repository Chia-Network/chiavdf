#include <pybind11/pybind11.h>
#include "../verifier.h"
#include "../prover_slow.h"
#include "../alloc.hpp"

namespace py = pybind11;

PYBIND11_MODULE(chiavdf, m) {
    m.doc() = "Chia proof of time";

    // Creates discriminant.
    m.def("create_discriminant", [] (const py::bytes& challenge_hash, int discriminant_size_bits) {
        std::string challenge_hash_str(challenge_hash);
        py::gil_scoped_release release;
        auto challenge_hash_bits = std::vector<uint8_t>(challenge_hash_str.begin(), challenge_hash_str.end());
        integer D = CreateDiscriminant(
            challenge_hash_bits,
            discriminant_size_bits
        );
        return D.to_string();
    });

    // Checks a simple wesolowski proof.
    m.def("verify_wesolowski", [] (const string& discriminant,
                                   const string& x_s, const string& y_s,
                                   const string& proof_s,
                                   uint64_t num_iterations) {
        py::gil_scoped_release release;
        integer D(discriminant);
        form x = DeserializeForm(D, (const uint8_t *)x_s.data(), x_s.size());
        form y = DeserializeForm(D, (const uint8_t *)y_s.data(), y_s.size());
        form proof = DeserializeForm(D, (const uint8_t *)proof_s.data(), proof_s.size());

        bool is_valid = false;
        VerifyWesolowskiProof(D, x, y, proof, num_iterations, is_valid);
        return is_valid;
    });

    // Checks an N wesolowski proof.
    m.def("verify_n_wesolowski", [] (const string& discriminant,
                                   const string& x_s,
                                   const string& proof_blob,
                                   const uint64_t num_iterations, const uint64_t disc_size_bits, const uint64_t recursion) {
        py::gil_scoped_release release;
        std::string proof_blob_str(proof_blob);
        uint8_t *proof_blob_ptr = reinterpret_cast<uint8_t *>(proof_blob_str.data());
        int proof_blob_size = proof_blob.size();

        return CheckProofOfTimeNWesolowski(integer(discriminant), (const uint8_t *)x_s.data(), proof_blob_ptr, proof_blob_size, num_iterations, disc_size_bits, recursion);
    });

    m.def("prove", [] (const py::bytes& challenge_hash, const string& x_s, int discriminant_size_bits, uint64_t num_iterations) {
        std::string challenge_hash_str(challenge_hash);
        std::vector<uint8_t> result;
        {
            py::gil_scoped_release release;
            std::vector<uint8_t> challenge_hash_bytes(challenge_hash_str.begin(), challenge_hash_str.end());
            integer D = CreateDiscriminant(
                    challenge_hash_bytes,
                    discriminant_size_bits
            );
            form x = DeserializeForm(D, (const uint8_t *) x_s.data(), x_s.size());
            result = ProveSlow(D, x, num_iterations);
        }
        py::bytes ret = py::bytes(reinterpret_cast<char*>(result.data()), result.size());
        return ret;
    });

    // Checks an N wesolowski proof, given y is given by 'GetB()' instead of a form.
    m.def("verify_n_wesolowski_with_b", [] (const string& discriminant,
                                   const string& B,
                                   const string& x_s,
                                   const string& proof_blob,
                                   const uint64_t num_iterations, const uint64_t recursion) {
        std::pair<bool, std::vector<uint8_t>> result;
        {
            py::gil_scoped_release release;
            std::string proof_blob_str(proof_blob);
            uint8_t *proof_blob_ptr = reinterpret_cast<uint8_t *>(proof_blob_str.data());
            int proof_blob_size = proof_blob.size();
            result = CheckProofOfTimeNWesolowskiWithB(integer(discriminant), integer(B), (const uint8_t *)x_s.data(), proof_blob_ptr, proof_blob_size, num_iterations, recursion);
        }
        py::bytes res_bytes = py::bytes(reinterpret_cast<char*>(result.second.data()), result.second.size());
        py::tuple res_tuple = py::make_tuple(result.first, res_bytes);
        return res_tuple;
    });

    m.def("get_b_from_n_wesolowski", [] (const string& discriminant,
                                   const string& x_s,
                                   const string& proof_blob,
                                   const uint64_t num_iterations, const uint64_t recursion) {
        py::gil_scoped_release release;
        std::string proof_blob_str(proof_blob);
        uint8_t *proof_blob_ptr = reinterpret_cast<uint8_t *>(proof_blob_str.data());
        int proof_blob_size = proof_blob.size();
        integer B = GetBFromProof(integer(discriminant), (const uint8_t *)x_s.data(), proof_blob_ptr, proof_blob_size, num_iterations, recursion);
        return B.to_string();
    });
}
