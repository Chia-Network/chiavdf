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

void doit(int thread)
{
    std::ifstream infile("vdf.txt");

    std::string challenge;
    std::string discriminant_size;
    std::string input_el;
    std::string output;
    std::string number_of_iterations;
    std::string witness_type;

    int cnt=0;

    while(true)
    {
        std::getline(infile, challenge);
        if (infile.eof())
            break;
        std::getline(infile, discriminant_size);
        std::getline(infile, input_el);
        std::getline(infile, output);
        std::getline(infile, number_of_iterations);
        std::getline(infile, witness_type);

        if(cnt%10==thread)
        {
        std::vector<uint8_t> challengebytes=HexToBytes(challenge.c_str());
        std::vector<uint8_t> inputbytes=HexToBytes(input_el.c_str());
        std::vector<uint8_t> outputbytes=HexToBytes(output.c_str());

        char *endptr;

        uint64 noi=strtoll(number_of_iterations.c_str(),&endptr,10);
        uint32 ds=strtoll(discriminant_size.c_str(),&endptr,10);
        uint8 wt=strtoll(witness_type.c_str(),&endptr,10);

        bool is_valid=CreateDiscriminantAndCheckProofOfTimeNWesolowski(challengebytes, ds, inputbytes.data(), outputbytes.data(), outputbytes.size(), noi, wt);

        printf("thread %d cnt %d is valid %d %s %s %s\n",thread,cnt,is_valid,number_of_iterations.c_str(),witness_type.c_str(),challenge.c_str());
        }
        cnt++;
    }
}

int main()
{
    std::vector<std::jthread> threads;
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back(doit, i);
    }
    doit(9);
    bunch0.join();
    bunch1.join();
    bunch2.join();
    bunch3.join();
    bunch4.join();
    bunch5.join();
    bunch6.join();
    bunch7.join();
    bunch8.join();

    return 0;
}

