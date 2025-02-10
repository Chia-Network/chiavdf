#include "verifier.h"
#include <sstream>
#include <string>
#include <fstream>
#include <thread>

std::vector<uint8_t> HexToBytes(const char *hex_proof) {
    int len = strlen(hex_proof);
    assert(len % 2 == 0);
    std::vector<uint8_t> result;
    for (int i = 0; i < len; i += 2)
    {
        int hex1 = hex_proof[i] >= 'a' ? (hex_proof[i] - 'a' + 10) : (hex_proof[i] - '0');
        int hex2 = hex_proof[i + 1] >= 'a' ? (hex_proof[i + 1] - 'a' + 10) : (hex_proof[i + 1] - '0');
        result.push_back(hex1 * 16 + hex2);
    }
    return result;
}

struct job
{
    std::vector<uint8_t> challengebytes;
    std::vector<uint8_t> inputbytes;
    std::vector<uint8_t> outputbytes;
    uint64 number_of_iterations;
    uint32 discriminant_size;
    uint8 witness_type;
};

void doit(int thread, std::vector<job> const& jobs)
{
    int cnt = 0;
    for (job const& j : jobs)
    {
        bool const is_valid = CreateDiscriminantAndCheckProofOfTimeNWesolowski(
            j.challengebytes,
            j.discriminant_size,
            j.inputbytes.data(),
            j.outputbytes.data(),
            j.outputbytes.size(),
            j.number_of_iterations,
            j.witness_type);
        if (!is_valid) {
            printf("thread %d cnt %d is valid %d %llu %d\n",
                thread,
                cnt,
                is_valid,
                j.number_of_iterations,
                j.witness_type);
            std::terminate();
        }
        cnt++;
    }
}

int main()
{
    std::ifstream infile("vdf.txt");

    std::string challenge;
    std::string discriminant_size;
    std::string input_el;
    std::string output;
    std::string number_of_iterations;
    std::string witness_type;

    std::vector<job> jobs;

    while (true) {
        std::getline(infile, challenge);
        if (infile.eof())
            break;
        std::getline(infile, discriminant_size);
        std::getline(infile, input_el);
        std::getline(infile, output);
        std::getline(infile, number_of_iterations);
        std::getline(infile, witness_type);

        std::vector<uint8_t> challengebytes=HexToBytes(challenge.c_str());
        std::vector<uint8_t> inputbytes=HexToBytes(input_el.c_str());
        std::vector<uint8_t> outputbytes=HexToBytes(output.c_str());

        char *endptr;

        uint64 noi=strtoll(number_of_iterations.c_str(),&endptr,10);
        if (errno == ERANGE) std::terminate();
        uint32 ds=strtoll(discriminant_size.c_str(),&endptr,10);
        if (errno == ERANGE) std::terminate();
        uint8 wt=strtoll(witness_type.c_str(),&endptr,10);
        if (errno == ERANGE) std::terminate();

        jobs.push_back({challengebytes, inputbytes, outputbytes, noi, ds, wt});
    }

    std::vector<std::thread> threads;
    for (int i = 0; i < 20; ++i)
        threads.emplace_back(doit, i, std::ref(jobs));

    for (auto& t : threads)
        t.join();

    return 0;
}
