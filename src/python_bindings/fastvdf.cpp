#include <pybind11/pybind11.h>
#include "../verifier.h"
#include "../prover_slow.h"

namespace py = pybind11;

PYBIND11_MODULE(chiavdf, m) {
    m.doc() = "Chia proof of time";

    // Creates discriminant.
    m.def("create_discriminant", [] (const py::bytes& challenge_hash, int discriminant_size_bits) {
        std::string challenge_hash_str(challenge_hash);
        integer D;
        {
            py::gil_scoped_release release;
            auto challenge_hash_bits = std::vector<uint8_t>(challenge_hash_str.begin(), challenge_hash_str.end());
            D = CreateDiscriminant(
                challenge_hash_bits,
                discriminant_size_bits
            );
        }
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
        bool is_valid = false;
        {
            py::gil_scoped_release release;
            form x = DeserializeForm(D, (const uint8_t *)x_s_copy.data(), x_s_copy.size());
            form y = DeserializeForm(D, (const uint8_t *)y_s_copy.data(), y_s_copy.size());
            form proof = DeserializeForm(D, (const uint8_t *)proof_s_copy.data(), proof_s_copy.size());
            VerifyWesolowskiProof(D, x, y, proof, num_iterations, is_valid);
        }
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
        uint8_t *proof_blob_ptr = reinterpret_cast<uint8_t *>(proof_blob_copy.data());
        int proof_blob_size = proof_blob_copy.size();
        bool is_valid = false;
        {
            py::gil_scoped_release release;
            is_valid=CheckProofOfTimeNWesolowski(integer(discriminant_copy), (const uint8_t *)x_s_copy.data(), proof_blob_ptr, proof_blob_size, num_iterations, disc_size_bits, recursion);
        }
        return is_valid;
    });

    // Checks an N wesolowski proof.
    m.def("create_discriminant_and_verify_n_wesolowski", [] (const py::bytes& challenge_hash,
                                   const int discriminant_size_bits,
                                   const string& x_s,
                                   const string& proof_blob,
                                   const uint64_t num_iterations,
                                   const uint64_t recursion) {
        std::string challenge_hash_str(challenge_hash);
        std::vector<uint8_t> challenge_hash_bits = std::vector<uint8_t>(challenge_hash_str.begin(), challenge_hash_str.end());
        std::string x_s_copy(x_s);
        std::string proof_blob_copy(proof_blob);
        bool is_valid = false;
        {
            py::gil_scoped_release release;
            is_valid=CreateDiscriminantAndCheckProofOfTimeNWesolowski(challenge_hash_bits, discriminant_size_bits,(const uint8_t *)x_s_copy.data(), (const uint8_t *)proof_blob_copy.data(), proof_blob_copy.size(), num_iterations, recursion);
        }
        return is_valid;
    });

    m.def("prove", [] (const py::bytes& challenge_hash, const string& x_s, int discriminant_size_bits, uint64_t num_iterations, const string& shutdown_file_path) {
        std::string challenge_hash_str(challenge_hash);
        std::string x_s_copy(x_s);
        std::vector<uint8_t> result;
        std::string shutdown_file_path_copy(shutdown_file_path);
        {
            py::gil_scoped_release release;
            std::vector<uint8_t> challenge_hash_bytes(challenge_hash_str.begin(), challenge_hash_str.end());
            integer D = CreateDiscriminant(
                    challenge_hash_bytes,
                    discriminant_size_bits
            );
            form x = DeserializeForm(D, (const uint8_t *) x_s_copy.data(), x_s_copy.size());
            result = ProveSlow(D, x, num_iterations, shutdown_file_path_copy);
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
        std::string discriminant_copy(discriminant);
        std::string B_copy(B);
        std::string x_s_copy(x_s);
        std::string proof_blob_copy(proof_blob);
        std::pair<bool, std::vector<uint8_t>> result;
        {
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
        integer B;
        {
            py::gil_scoped_release release;
            uint8_t *proof_blob_ptr = reinterpret_cast<uint8_t *>(proof_blob_copy.data());
            int proof_blob_size = proof_blob_copy.size();
            B = GetBFromProof(integer(discriminant_copy), (const uint8_t *)x_s_copy.data(), proof_blob_ptr, proof_blob_size, num_iterations, recursion);
        }
        return B.to_string();
    });
}
