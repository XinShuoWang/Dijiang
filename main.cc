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
            std::string str(buffer, size);
            fprintf(stdout, "dijinag -> size is: %d, content is: %s, \n", size, str.c_str());
        };
        socket->RegisterHandler(handler);
        socket->Loop();
    }
    else
    {
        SAY("Client");
        int timeout = 500;
        RdmaClientSocket *socket = new RdmaClientSocket(ip, port, threadNum, messageBufferSize, timeout);
        socket->Loop();
        char data[] = "hello,world";
        int size = strlen(data);
        socket->Write(data, size);
    }
    return 0;
}
