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
        integer D(discriminant);
        std::string x_s_copy(x_s);
        std::string y_s_copy(y_s);
        std::string proof_s_copy(proof_s);
        py::gil_scoped_release release;
        form x = DeserializeForm(D, (const uint8_t *)x_s_copy.data(), x_s_copy.size());
        form y = DeserializeForm(D, (const uint8_t *)y_s_copy.data(), y_s_copy.size());
        form proof = DeserializeForm(D, (const uint8_t *)proof_s_copy.data(), proof_s_copy.size());

        bool is_valid = false;
        VerifyWesolowskiProof(D, x, y, proof, num_iterations, is_valid);
        return is_valid;
    });

    // Checks an N wesolowski proof.
    m.def("verify_n_wesolowski", [] (const string& discriminant,
                                   const string& x_s,
                                   const string& proof_blob,
                                   const uint64_t num_iterations, const uint64_t disc_size_bits, const uint64_t recursion) {
        std::string discriminant_copy(discriminant);
        std::string x_s_copy(x_s);
        std::string proof_blob_copy(proof_blob);
        py::gil_scoped_release release;
        uint8_t *proof_blob_ptr = reinterpret_cast<uint8_t *>(proof_blob_copy.data());
        int proof_blob_size = proof_blob_copy.size();

        return CheckProofOfTimeNWesolowski(integer(discriminant_copy), (const uint8_t *)x_s_copy.data(), proof_blob_ptr, proof_blob_size, num_iterations, disc_size_bits, recursion);
    });

    m.def("prove", [] (const py::bytes& challenge_hash, const string& x_s, int discriminant_size_bits, uint64_t num_iterations, const string& shutdown_file_path) {
        std::string challenge_hash_str(challenge_hash);
        std::string x_s_copy(x_s);
        std::vector<uint8_t> result;
        {
            py::gil_scoped_release release;
            std::vector<uint8_t> challenge_hash_bytes(challenge_hash_str.begin(), challenge_hash_str.end());
            integer D = CreateDiscriminant(
                    challenge_hash_bytes,
                    discriminant_size_bits
            );
            form x = DeserializeForm(D, (const uint8_t *) x_s_copy.data(), x_s_copy.size());
            result = ProveSlow(D, x, num_iterations, shutdown_file_path);
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
            std::string discriminant_copy(discriminant);
            std::string B_copy(B);
            std::string x_s_copy(x_s);
            std::string proof_blob_copy(proof_blob);
            py::gil_scoped_release release;
            uint8_t *proof_blob_ptr = reinterpret_cast<uint8_t *>(proof_blob_copy.data());
            int proof_blob_size = proof_blob_copy.size();
            result = CheckProofOfTimeNWesolowskiWithB(integer(discriminant_copy), integer(B_copy), (const uint8_t *)x_s_copy.data(), proof_blob_ptr, proof_blob_size, num_iterations, recursion);
        }
        py::bytes res_bytes = py::bytes(reinterpret_cast<char*>(result.second.data()), result.second.size());
        py::tuple res_tuple = py::make_tuple(result.first, res_bytes);
        return res_tuple;
    });

    m.def("get_b_from_n_wesolowski", [] (const string& discriminant,
                                   const string& x_s,
                                   const string& proof_blob,
                                   const uint64_t num_iterations, const uint64_t recursion) {
        std::string discriminant_copy(discriminant);
        std::string x_s_copy(x_s);
        std::string proof_blob_copy(proof_blob);
        py::gil_scoped_release release;
        uint8_t *proof_blob_ptr = reinterpret_cast<uint8_t *>(proof_blob_copy.data());
        int proof_blob_size = proof_blob_copy.size();
        integer B = GetBFromProof(integer(discriminant_copy), (const uint8_t *)x_s_copy.data(), proof_blob_ptr, proof_blob_size, num_iterations, recursion);
        return B.to_string();
    });
}
