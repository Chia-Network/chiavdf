#include <boost/asio.hpp>
#include "vdf.h"

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
}

char disc[350];
char disc_size[5];
int disc_int_size = atoi(disc_size);

void WriteProof(uint64_t iteration, Proof& result, tcp::socket& sock) {
    // Writes the number of iterations
    std::vector<unsigned char> bytes = ConvertIntegerToBytes(integer(iteration), 8);

    // Writes the y, with prepended size
    std::vector<unsigned char> y_size = ConvertIntegerToBytes(integer(result.y.size()), 8);
    bytes.insert(bytes.end(), y_size.begin(), y_size.end());
    bytes.insert(bytes.end(), result.y.begin(), result.y.end());
    
    // Writes the witness type.
    std::vector<unsigned char> witness_type = ConvertIntegerToBytes(integer(result.witness_type), 1);
    bytes.insert(bytes.end(), witness_type.begin(), witness_type.end());

    bytes.insert(bytes.end(), result.proof.begin(), result.proof.end());
    std::string str_result = BytesToStr(bytes);

    const uint32_t length = str_result.size();
    std::vector<unsigned char> prefix_bytes = ConvertIntegerToBytes(integer(length), 4);
    std::string prefix = BytesToStr(prefix_bytes);

    PrintInfo("Sending proof");
    {
        std::lock_guard<std::mutex> lock(socket_mutex);
        boost::asio::write(sock, boost::asio::buffer(prefix_bytes, 4));
        boost::asio::write(sock, boost::asio::buffer(str_result.c_str(), str_result.size()));
    }
    PrintInfo("Sended proof");
}

void CreateAndWriteProof(ProverManager& pm, uint64_t iteration, bool& stop_signal, tcp::socket& sock) {
    Proof result = pm.Prove(iteration);
    if (stop_signal == true) {
        PrintInfo("Got stop signal before completing the proof!");
        return ;
    }
    WriteProof(iteration, result, sock);
}

void InitSession(tcp::socket& sock) {
    boost::system::error_code error;

    memset(disc,0x00,sizeof(disc)); // For null termination
    memset(disc_size,0x00,sizeof(disc_size)); // For null termination

    boost::asio::read(sock, boost::asio::buffer(disc_size, 3), error);
    boost::asio::read(sock, boost::asio::buffer(disc, disc_int_size), error);

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
    init_gmp();
    allow_integer_constructor=true; //make sure the old gmp allocator isn't used
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

void session(tcp::socket& sock) {
    InitSession(sock);
    try {
        integer D(disc);
        integer L=root(-D, 4);
        form f=form::generator(D);
        PrintInfo("Discriminant = " + to_string(D.impl));

        std::vector<std::thread> threads;
        WesolowskiCallback weso(segments, D);
        bool stopped = false;
        std::thread vdf_worker(repeated_square, f, D, L, std::ref(weso), std::ref(stopped));
        ProverManager pm(D, &weso, segments, thread_count);        
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
                free(forms);
                free(weso.checkpoints);
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
        form f=form::generator(D);
        WesolowskiCallback weso(0, D);
        PrintInfo("Discriminant = " + to_string(D.impl));

        uint64_t iter = ReadIteration(sock);
        weso.wanted_iter = iter;
        uint64_t k, l;
        ApproximateParameters(iter, k, l);
        weso.kl = k * l;

        bool stopped = false;
        uint64_t space_needed = iter / (k * l) + 100;
        forms = (form*) calloc(space_needed, sizeof(form));
        forms[0] = f;

        std::thread vdf_worker(repeated_square, f, D, L, std::ref(weso), std::ref(stopped));

        Proof proof = ProveOneWesolowski(iter, D, &weso);
        WriteProof(iter, proof, sock);

        iter = ReadIteration(sock);
        while (iter != 0) {
            std::cout << "Warning: did not receive stop signal\n";
            iter = ReadIteration(sock);
        }
        stopped = true;
        vdf_worker.join();
        free(forms);
    } catch (std::exception& e) {
        PrintInfo("Exception in thread: " + to_string(e.what()));
    }
    FinishSession(sock);
}

int gcd_base_bits=50;
int gcd_128_max_iter=3;

int main(int argc, char* argv[])
{
  try
  {
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
    one_weso = false;
    boost::system::error_code error;
    char one_weso_buf[10];
    boost::asio::read(s, boost::asio::buffer(one_weso_buf, 6), error);
    // Check for "SIMPLE" (1weso) or "NORMAL" (nweso)
    if (one_weso_buf[0] == 'S') {
        one_weso = true;
        SessionOneWeso(s);
    } else {
        session(s);
    }
  } catch (std::exception& e) {
    std::cerr << "Exception: " << e.what() << "\n";
  } 
  return 0;
}
