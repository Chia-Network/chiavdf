#include "verifier.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace proof_corpus {

// Concatenated Streamable CorpusEntry records from chia-tools/export-vdf-proofs:
// u32 height, ClassgroupElement input, VDFInfo info, VDFProof proof.
// No file header/count. All integers are big-endian. Bytes start with a u32
// byte length. ClassgroupElement is exactly 100 bytes.
constexpr int DISCRIMINANT_SIZE_BITS = 1024;

struct Proof {
    std::span<const uint8_t> challenge, input, output, witness;
    uint32_t height;
    uint64_t iterations, depth;
};

constexpr size_t MAX_WITNESS_SIZE = BQFC_FORM_SIZE + 255 * (8 + 33 + BQFC_FORM_SIZE);

class MappedFile {
public:
    explicit MappedFile(const std::string& path)
    {
        const int fd = open(path.c_str(), O_RDONLY);
        if (fd < 0) throw std::runtime_error("cannot open corpus");
        struct stat info;
        const bool valid = fstat(fd, &info) == 0 && S_ISREG(info.st_mode) && info.st_size >= 0 &&
                           static_cast<uint64_t>(info.st_size) <= SIZE_MAX;
        if (!valid) {
            close(fd);
            throw std::runtime_error("cannot determine corpus size");
        }
        size_ = static_cast<size_t>(info.st_size);
        if (size_ == 0) {
            close(fd);
            throw std::runtime_error("empty corpus");
        }
        void* data = mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd, 0);
        close(fd);
        if (data == MAP_FAILED) throw std::runtime_error("cannot map corpus");
        data_ = static_cast<const uint8_t*>(data);
    }

    ~MappedFile()
    {
        munmap(const_cast<uint8_t*>(data_), size_);
    }

    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;

    const uint8_t* data() const { return data_; }
    size_t size() const { return size_; }

private:
    const uint8_t* data_;
    size_t size_ = 0;
};

uint64_t read_uint(std::span<const uint8_t>& input, unsigned size)
{
    if (size > input.size())
        throw std::runtime_error("truncated integer");
    uint64_t value = 0;
    for (uint8_t byte : input.first(size))
        value = (value << 8) | byte;
    input = input.subspan(size);
    return value;
}

std::span<const uint8_t> read_bytes(std::span<const uint8_t>& input, size_t size)
{
    if (size > input.size())
        throw std::runtime_error("truncated byte string");
    const auto bytes = input.first(size);
    input = input.subspan(size);
    return bytes;
}

std::optional<Proof> read_proof(std::span<const uint8_t>& input)
{
    if (input.empty()) return std::nullopt; // EOF at a record boundary.
    Proof proof;
    bool has_height = false;
    try {
        proof.height = static_cast<uint32_t>(read_uint(input, 4));
        has_height = true;
        proof.input = read_bytes(input, BQFC_FORM_SIZE);
        // VDFInfo: challenge, actual iteration count, output element.
        proof.challenge = read_bytes(input, 32);
        proof.iterations = read_uint(input, 8);
        proof.output = read_bytes(input, BQFC_FORM_SIZE);
        // VDFProof: witness type, length-prefixed witness, normalized flag.
        proof.depth = read_uint(input, 1);
        const auto witness_size = read_uint(input, 4);
        if (witness_size > MAX_WITNESS_SIZE)
            throw std::runtime_error("witness exceeds maximum encoded size");
        proof.witness = read_bytes(input, static_cast<size_t>(witness_size));
        if (read_uint(input, 1) > 1) throw std::runtime_error("invalid normalized flag");
        // Input and iterations are already resolved by the exporter, so the
        // normalized flag need not change the arguments passed to chiavdf.
    } catch (const std::exception& e) {
        if (has_height)
            throw std::runtime_error("height=" + std::to_string(proof.height) + ": " + e.what());
        throw;
    }
    return proof;
}

struct Result {
    uint64_t checked = 0;
    uint64_t failures = 0;
    uint64_t run_proofs = 0;
    uint64_t runs = 0;
};

struct Run {
    std::span<const uint8_t> bytes;
    std::span<const uint8_t> challenge;
    uint64_t first_number;
};

Result verify(const MappedFile& file, const std::string& path, unsigned workers, uint32_t start_height)
{
    if (workers == 0) throw std::runtime_error("need at least one worker");
    std::span<const uint8_t> input(file.data(), file.size());
    std::mutex mutex;
    bool stop = false; // Protected by mutex.
    uint64_t record_number = 0;
    Result result;
    auto worker = [&] {
        std::array<uint8_t, BQFC_FORM_SIZE + MAX_WITNESS_SIZE> proof_blob;
        // Claim a whole consecutive challenge run under the lock. Compute its
        // discriminant once, then verify every proof outside the lock.
        while (true) {
            Run run;
            uint64_t selected = 0;
            {
                std::lock_guard<std::mutex> guard(mutex);
                if (stop || input.empty()) return;
                const auto start = input;
                auto next = input;
                std::span<const uint8_t> challenge;
                uint64_t count = 0;
                try {
                    const auto first = read_proof(next);
                    challenge = first->challenge;
                    ++count;
                    selected += first->height >= start_height;
                    while (!next.empty()) {
                        auto after = next;
                        const auto candidate = read_proof(after);
                        if (!std::equal(first->challenge.begin(), first->challenge.end(),
                                        candidate->challenge.begin()))
                            break;
                        next = after;
                        ++count;
                        selected += candidate->height >= start_height;
                    }
                } catch (const std::exception& e) {
                    // A malformed record may destroy framing. Keep the valid
                    // prefix of this run and let in-flight verifications finish.
                    stop = true;
                    ++result.failures;
                    std::cerr << path << ": record " << record_number + count + 1
                              << ": " << e.what() << std::endl;
                }
                run = {start.first(start.size() - next.size()), challenge, record_number + 1};
                record_number += count;
                if (selected != 0) {
                    result.run_proofs += selected;
                    ++result.runs;
                }
                input = next;
            }
            if (run.bytes.empty()) return;
            if (selected == 0) continue;
            std::optional<integer> discriminant;
            std::string discriminant_error;
            try {
                std::vector<uint8_t> challenge(run.challenge.begin(), run.challenge.end());
                discriminant = CreateDiscriminant(challenge, DISCRIMINANT_SIZE_BITS);
            } catch (const std::exception& e) {
                discriminant_error = e.what();
            }
            auto records = run.bytes;
            uint64_t number = run.first_number;
            while (!records.empty()) {
                const auto proof = read_proof(records);
                if (proof->height < start_height) {
                    ++number;
                    continue;
                }
                std::string error = discriminant_error;
                if (discriminant) {
                    try {
                        std::copy(proof->output.begin(), proof->output.end(), proof_blob.begin());
                        if (!proof->witness.empty())
                            std::copy(proof->witness.begin(), proof->witness.end(),
                                      proof_blob.begin() + proof->output.size());
                        if (!CheckProofOfTimeNWesolowski(
                                *discriminant, proof->input.data(), proof_blob.data(),
                                proof->output.size() + proof->witness.size(), proof->iterations,
                                DISCRIMINANT_SIZE_BITS, proof->depth))
                            error = "INVALID";
                    } catch (const std::exception& e) {
                        error = e.what();
                    }
                }
                std::lock_guard<std::mutex> guard(mutex);
                ++result.checked;
                if (!error.empty()) {
                    ++result.failures;
                    std::cerr << path << ": record " << number << ": height=" << proof->height
                              << ": " << error << std::endl;
                }
                if (result.checked % 10000 == 0)
                    std::cout << path << ": checked " << result.checked << " proofs, "
                              << result.failures << " failures, average challenge run length "
                              << std::fixed << std::setprecision(2)
                              << static_cast<double>(result.run_proofs) / result.runs
                              << " (height=" << proof->height << ")" << std::endl;
                ++number;
            }
        }
    };
    {
        std::vector<std::thread> threads;
        try {
            threads.reserve(workers);
            for (unsigned i = 0; i < workers; ++i)
                threads.emplace_back(worker);
        } catch (...) {
            {
                std::lock_guard<std::mutex> guard(mutex);
                stop = true;
            }
            for (auto& thread : threads)
                thread.join();
            throw;
        }
        for (auto& thread : threads)
            thread.join();
    }
    return result;
}

} // namespace proof_corpus


int main(int argc, char* argv[])
{
    const auto usage = [&] {
        std::cout << "Usage: " << argv[0]
                  << " [--start-height HEIGHT] CORPUS.bin [CORPUS.bin ...]\n"
                  << "Verify binary Streamable corpora from chia-tools/export-vdf-proofs.\n";
    };
    uint32_t start_height = 0;
    bool has_start_height = false;
    bool files_only = false;
    std::vector<std::string> paths;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (!files_only && arg == "--help") {
            usage();
            return 0;
        }
        if (!files_only && arg == "--") {
            files_only = true;
            continue;
        }
        if (!files_only && arg == "--start-height") {
            if (has_start_height || i + 1 == argc) {
                std::cerr << "--start-height requires one HEIGHT value\n";
                return 2;
            }
            const std::string value = argv[++i];
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), start_height);
            if (error != std::errc{} || end != value.data() + value.size()) {
                std::cerr << "invalid start height: " << value << std::endl;
                return 2;
            }
            has_start_height = true;
            continue;
        }
        if (!files_only && !arg.empty() && arg[0] == '-') {
            std::cerr << "unknown option: " << arg << std::endl;
            return 2;
        }
        paths.push_back(arg);
    }
    if (paths.empty()) { usage(); return 2; }
    const unsigned workers = std::max(1u, std::thread::hardware_concurrency());
    std::cout << "Using " << workers << " verifier threads" << std::endl;
    uint64_t total_proofs = 0;
    uint64_t total_failures = 0;
    unsigned failed_files = 0;
    for (const std::string& path : paths) {
        try {
            std::cout << path << ": start height " << start_height << std::endl;
            proof_corpus::MappedFile file(path);
            auto result = proof_corpus::verify(file, path, workers, start_height);
            total_proofs += result.checked;
            total_failures += result.failures;
            if (result.failures != 0) ++failed_files;
            std::cout << path << ": checked " << result.checked << " proofs, "
                      << result.failures << " failures" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << path << ": " << e.what() << std::endl;
            ++failed_files;
            ++total_failures;
        }
    }
    std::cout << "Total: " << total_proofs << " proofs, " << total_failures
              << " failures, " << failed_files << " failed files out of " << paths.size() << std::endl;
    return total_failures == 0 ? 0 : 1;
}
