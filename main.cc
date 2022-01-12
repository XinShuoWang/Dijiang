#include "dijiang/RdmaServerSocket.hpp"
#include "dijiang/RdmaClientSocket.hpp"

#include <iostream>
#include <memory>

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        std::cout << "<usage>: ./demo {-s|-c}" << std::endl;
        return 0;
    }
    bool is_server = ((strcmp(*(argv + 1), "-s") == 0));
    const int messageBufferSize = 10 * 1024 * 1024;
    char port[] = "12345";
    char ip[] = "10.0.0.28";
    if (is_server)
    {
        SAY("Server");
        const int threadNum = 256;
        auto socket = std::make_shared<RdmaServerSocket>(port, threadNum, messageBufferSize);
        auto handler = [](char *buffer, int size)
        {
            //std::string str(buffer, size);
            //fprintf(stdout, "dijinag -> size is: %d, content is: %s, \n", size, str.c_str());
        };
        socket->RegisterHandler(handler);
        socket->Loop();
    }
    else
    {
        SAY("Client");
        std::vector<std::thread> v;
        int timeout = 500;
        const int threadNum = 16;
        auto socket = std::make_shared<RdmaClientSocket>(ip, port, threadNum, messageBufferSize, timeout);
        const int data_size = 128 * 1024;
        char *data = new char[data_size];
        memset(data, 'a', data_size);
        auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < 1024; ++i)
          socket->Write(data, data_size);
        auto end = std::chrono::steady_clock::now();
        std::chrono::duration<double> elapsed_seconds = end - start;
        std::cout << "elapsed time: " << elapsed_seconds.count() << "s\n";
        std::cout << "Bandwidth is: " << data_size / 1024.0 / elapsed_seconds.count() << "MB/s\n";
    }
    return 0;
}
