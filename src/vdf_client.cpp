#include <boost/asio.hpp>
#include "vdf.h"
#include <atomic>

using boost::asio::ip::tcp;

const int max_length = 2048;
std::mutex socket_mutex;

int process_number;
// Segments are 2^16, 2^18, ..., 2^30
// Best case it'll be able to proof for up to 2^36 due to 64-wesolowski restriction.
int segments = 8;
int thread_count = 3;

void PrintInfo(std::string input) {
    std::cout << "VDF Client: " << input << "\n";
    std::cout << std::flush;
}

char disc[350];
char disc_size[5];
int disc_int_size;

void WriteProof(uint64_t iteration, Proof& result, tcp::socket& sock) {
    // Writes the number of iterations
    uint8_t int_bytes[8];
    std::vector<uint8_t> bytes;
    Int64ToBytes(int_bytes, iteration);
    VectorAppendArray(bytes, int_bytes, sizeof(int_bytes));

    // Writes the y, with prepended size
    Int64ToBytes(int_bytes, result.y.size());
    VectorAppendArray(bytes, int_bytes, sizeof(int_bytes));
    VectorAppend(bytes, result.y);

    // Writes the witness type.
    bytes.push_back(result.witness_type);

    VectorAppend(bytes, result.proof);
    std::string str_result = BytesToStr(bytes);

    Int32ToBytes(int_bytes, str_result.size());

    PrintInfo("Sending proof");
    {
        std::lock_guard<std::mutex> lock(socket_mutex);
        boost::asio::write(sock, boost::asio::buffer(int_bytes, 4));
        boost::asio::write(sock, boost::asio::buffer(str_result.c_str(), str_result.size()));
    }
    PrintInfo("Sent proof");
}

void CreateAndWriteProof(ProverManager& pm, uint64_t iteration, std::atomic<bool>& stop_signal, tcp::socket& sock) {
    Proof result = pm.Prove(iteration);
    if (stop_signal == true) {
        PrintInfo("Got stop signal before completing the proof!");
        return ;
    }
    WriteProof(iteration, result, sock);
}

void CreateAndWriteProofOneWeso(uint64_t iters, integer& D, form f, OneWesolowskiCallback* weso, std::atomic<bool>& stop_signal, tcp::socket& sock) {
    Proof result = ProveOneWesolowski(iters, D, f, weso, stop_signal);
    if (stop_signal) {
        PrintInfo("Got stop signal before completing the proof!");
        return ;
    }
    WriteProof(iters, result, sock);
}

void CreateAndWriteProofTwoWeso(integer& D, form f, uint64_t iters, TwoWesolowskiCallback* weso, std::atomic<bool>& stop_signal, tcp::socket& sock) {
    Proof result = ProveTwoWeso(D, f, iters, 0, weso, 0, stop_signal);
    if (stop_signal) {
        PrintInfo("Got stop signal before completing the proof!");
        return ;
    }
    WriteProof(iters, result, sock);
}

uint8_t initial_form_s[BQFC_FORM_SIZE];

void InitSession(tcp::socket& sock) {
    boost::system::error_code error;

    memset(disc,0x00,sizeof(disc)); // For null termination
    memset(disc_size,0x00,sizeof(disc_size)); // For null termination

    boost::asio::read(sock, boost::asio::buffer(disc_size, 3), error);
    disc_int_size = atoi(disc_size);
    boost::asio::read(sock, boost::asio::buffer(disc, disc_int_size), error);

    char form_size;
    boost::asio::read(sock, boost::asio::buffer(&form_size, 1), error);
    boost::asio::read(sock, boost::asio::buffer(initial_form_s, form_size), error);

    if (error == boost::asio::error::eof)
        return ; // Connection closed cleanly by peer.
    else if (error)
        throw boost::system::system_error(error); // Some other error.

    if (getenv( "warn_on_corruption_in_production" )!=nullptr) {
        warn_on_corruption_in_production=true;
    }
    if (is_vdf_test) {
        PrintInfo( "=== Test mode ===" );
    }
    if (warn_on_corruption_in_production) {
        PrintInfo( "=== Warn on corruption enabled ===" );
    }
    assert(is_vdf_test); //assertions should be disabled in VDF_MODE==0
    set_rounding_mode();
}

void FinishSession(tcp::socket& sock) {
    try {
        // Tell client I've stopped everything, wait for ACK and close.
        boost::system::error_code error;

        PrintInfo("Stopped everything! Ready for the next challenge.");

        std::lock_guard<std::mutex> lock(socket_mutex);
        boost::asio::write(sock, boost::asio::buffer("STOP", 4));

        char ack[5];
        memset(ack,0x00,sizeof(ack));
        boost::asio::read(sock, boost::asio::buffer(ack, 3), error);
        assert (strncmp(ack, "ACK", 3) == 0);
    } catch (std::exception& e) {
        PrintInfo("Exception in thread: " + to_string(e.what()));
    }
}

uint64_t ReadIteration(tcp::socket& sock) {
    boost::system::error_code error;
    char data[20];
    memset(data, 0, sizeof(data));
    boost::asio::read(sock, boost::asio::buffer(data, 2), error);
    int size = (data[0] - '0') * 10 + (data[1] - '0');
    memset(data, 0, sizeof(data));
    boost::asio::read(sock, boost::asio::buffer(data, size), error);
    uint64_t iters = 0;
    for (int i = 0; i < size; i++)
        iters = iters * 10 + data[i] - '0';
    return iters;
}

void SessionFastAlgorithm(tcp::socket& sock) {
    InitSession(sock);
    try {
        integer D(disc);
        integer L=root(-D, 4);
        PrintInfo("Discriminant = " + to_string(D.impl));
        form f = DeserializeForm(D, initial_form_s, sizeof(initial_form_s));
        PrintInfo("Initial form: " + to_string(f.a.impl) + " " + to_string(f.b.impl));
        std::vector<std::thread> threads;
        const bool multi_proc_machine = (std::thread::hardware_concurrency() >= 16) ? true : false;
        WesolowskiCallback* weso = new FastAlgorithmCallback(segments, D, f, multi_proc_machine);
        FastStorage* fast_storage = NULL;
        if (multi_proc_machine) {
            fast_storage = new FastStorage((FastAlgorithmCallback*)weso);
        }
        std::atomic<bool> stopped(false);
        std::thread vdf_worker(repeated_square, f, std::ref(D), std::ref(L), weso, fast_storage, std::ref(stopped));
        ProverManager pm(D, (FastAlgorithmCallback*)weso, fast_storage, segments, thread_count);
        pm.start();

        // Tell client that I'm ready to get the challenges.
        boost::asio::write(sock, boost::asio::buffer("OK", 2));

        while (!stopped) {
            uint64_t iters = ReadIteration(sock);
            if (iters == 0) {
                PrintInfo("Got stop signal!");
                stopped = true;
                pm.stop();
                vdf_worker.join();
                for (int t = 0; t < threads.size(); t++) {
                    threads[t].join();
                }
                if (fast_storage != NULL) {
                    delete(fast_storage);
                }
                delete(weso);
            } else {
                PrintInfo("Received iteration: " + to_string(iters));
                threads.push_back(std::thread(CreateAndWriteProof, std::ref(pm), iters, std::ref(stopped), std::ref(sock)));
            }
        }
    } catch (std::exception& e) {
        PrintInfo("Exception in thread: " + to_string(e.what()));
    }
    FinishSession(sock);
}

void SessionOneWeso(tcp::socket& sock) {
    InitSession(sock);
    try {
        integer D(disc);
        integer L=root(-D, 4);
        PrintInfo("Discriminant = " + to_string(D.impl));
        form f = DeserializeForm(D, initial_form_s, sizeof(initial_form_s));
        // Tell client that I'm ready to get the challenges.
        boost::asio::write(sock, boost::asio::buffer("OK", 2));

        uint64_t iter = ReadIteration(sock);
        if (iter == 0) {
            FinishSession(sock);
            return;
        }
        std::atomic<bool> stopped(false);
        WesolowskiCallback* weso = new OneWesolowskiCallback(D, f, iter);
        FastStorage* fast_storage = NULL;
        std::thread vdf_worker(repeated_square, f, std::ref(D), std::ref(L), weso, fast_storage, std::ref(stopped));
        std::thread th_prover(CreateAndWriteProofOneWeso, iter, std::ref(D), f, (OneWesolowskiCallback*)weso, std::ref(stopped), std::ref(sock));
        iter = ReadIteration(sock);
        while (iter != 0) {
            std::cout << "Warning: did not receive stop signal\n";
            iter = ReadIteration(sock);
        }
        stopped = true;
        vdf_worker.join();
        th_prover.join();
        delete(weso);
    } catch (std::exception& e) {
        PrintInfo("Exception in thread: " + to_string(e.what()));
    }
    FinishSession(sock);
}

void SessionTwoWeso(tcp::socket& sock) {
    const int kMaxProcessesAllowed = 100;
    InitSession(sock);
    try {
        integer D(disc);
        integer L=root(-D, 4);
        PrintInfo("Discriminant = " + to_string(D.impl));
        form f = DeserializeForm(D, initial_form_s, sizeof(initial_form_s));

        // Tell client that I'm ready to get the challenges.
        boost::asio::write(sock, boost::asio::buffer("OK", 2));

        std::atomic<bool> stopped(false);
        std::atomic<bool> stop_vector[100];
        std::vector<std::thread> threads;
        // (iteration, thread_id)
        std::set<std::pair<uint64_t, uint64_t> > seen_iterations;
        WesolowskiCallback* weso = new TwoWesolowskiCallback(D, f);
        FastStorage* fast_storage = NULL;
        std::thread vdf_worker(repeated_square, f, std::ref(D), std::ref(L), weso, fast_storage, std::ref(stopped));

        while (!stopped) {
            uint64_t iters = ReadIteration(sock);
            if (iters == 0) {
                PrintInfo("Got stop signal!");
                stopped = true;
                for (int i = 0; i < threads.size(); i++)
                    stop_vector[i] = true;
                for (int t = 0; t < threads.size(); t++) {
                    threads[t].join();
                }
                vdf_worker.join();
                delete(weso);
            } else {
                uint64_t max_iter = 0;
                uint64_t max_iter_thread_id = -1;
                uint64_t min_iter = 1ULL << 62;
                bool unique = true;
                for (auto active_iter: seen_iterations) {
                    if (active_iter.first > max_iter) {
                        max_iter = active_iter.first;
                        max_iter_thread_id = active_iter.second;
                    }
                    if (active_iter.first < min_iter) {
                        min_iter = active_iter.first;
                    }
                    if (active_iter.first == iters) {
                        unique = false;
                        break;
                    }
                }
                if (!unique) {
                    PrintInfo("Duplicate iteration " + to_string(iters) + "... Ignoring.");
                    continue;
                }
                if (iters >= kMaxProcessesAllowed - 500000) {
                    PrintInfo("Too big iter... ignoring");
                    continue;
                }
                if (threads.size() < kMaxProcessesAllowed || iters < min_iter) {
                    seen_iterations.insert({iters, threads.size()});
                    PrintInfo("Running proving for iter: " + to_string(iters));
                    stop_vector[threads.size()] = false;
                    threads.push_back(std::thread(CreateAndWriteProofTwoWeso, std::ref(D), f, iters,
                                      (TwoWesolowskiCallback*)weso, std::ref(stop_vector[threads.size()]),
                                      std::ref(sock)));
                    if (threads.size() > kMaxProcessesAllowed) {
                        PrintInfo("Stopping proving for iter: " + to_string(max_iter));
                        stop_vector[max_iter_thread_id] = true;
                        seen_iterations.erase({max_iter, max_iter_thread_id});
                    }
                }
            }
        }
    } catch (std::exception& e) {
        PrintInfo("Exception in thread: " + to_string(e.what()));
    }
    FinishSession(sock);
}

int gcd_base_bits=50;
int gcd_128_max_iter=3;

int main(int argc, char* argv[]) try
{
    init_gmp();
    if (argc != 4)
    {
      std::cerr << "Usage: ./vdf_client <host> <port>\n";
      return 1;
    }

    if(hasAVX2())
    {
      gcd_base_bits=63;
      gcd_128_max_iter=2;
    }

    boost::asio::io_service io_service;

    tcp::resolver resolver(io_service);
    tcp::resolver::query query(tcp::v6(), argv[1], argv[2], boost::asio::ip::resolver_query_base::v4_mapped);
    tcp::resolver::iterator iterator = resolver.resolve(query);

    tcp::socket s(io_service);
    boost::asio::connect(s, iterator);
    fast_algorithm = false;
    two_weso = false;
    boost::system::error_code error;
    char prover_type_buf[5];
    boost::asio::read(s, boost::asio::buffer(prover_type_buf, 1), error);
    // Check for "S" (simple weso), "N" (n-weso), or "T" (2-weso)
    if (prover_type_buf[0] == 'S') {
        SessionOneWeso(s);
    }
    if (prover_type_buf[0] == 'N') {
        fast_algorithm = true;
        SessionFastAlgorithm(s);
    }
    if (prover_type_buf[0] == 'T') {
        two_weso = true;
        SessionTwoWeso(s);
    }
    return 0;
}
catch (std::exception& e) {
    std::cerr << "Exception: " << e.what() << "\n";
    return 1;
}
