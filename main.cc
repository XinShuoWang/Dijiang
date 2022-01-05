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
    if (is_server)
    {
        SAY("Server");
        char port[] = "12345";
        const int threadNum = 256;
        RdmaServerSocket *socket = new RdmaServerSocket(port, threadNum);
        auto handler = [](char *buffer, int size)
        {
            fprintf(stdout, "size is : %d \n", size);
            std::string content(buffer, size);
            fprintf(stdout, "content is: %s \n", content.c_str());
        };
        socket->RegisterMessageCallback(handler, messageBufferSize);
        socket->Loop();
    }
    else
    {
        SAY("Client");
        char port[] = "12345";
        char ip[] = "10.0.0.28";
        RdmaClientSocket *socket = new RdmaClientSocket(ip, port, 16, 10 * 1024 * 1024, 500);
        char message[] = "hello, world!";
        socket->Write(message, strlen(message));
        socket->Loop();
    }
    return 0;
}