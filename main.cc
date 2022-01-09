#include "dijiang/RdmaServerSocket.hpp"
#include "dijiang/RdmaClientSocket.hpp"

#include <iostream>

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        std::cout << "<usage>: ./demo {-s|-c}" << std::endl;
        return 0;
    }
    bool is_server = ((strcmp(*(argv + 1), "-s") == 0));
    const int messageBufferSize = 10 * 1024 * 1024;
    const int threadNum = 256;
    char port[] = "12345";
    char ip[] = "10.0.0.29";
    if (is_server)
    {
        SAY("Server");
        RdmaServerSocket *socket = new RdmaServerSocket(port, threadNum, messageBufferSize);
        auto handler = [](char *buffer, int size)
        {
            fprintf(stdout, "size is: %d\n", size);
        };
        socket->RegisterHandler(handler);
        socket->Loop();
    }
    else
    {
        SAY("Client");
        RdmaClientSocket *socket = new RdmaClientSocket(ip, port, threadNum, messageBufferSize, 500);
        socket->Loop();
    }
    return 0;
}
