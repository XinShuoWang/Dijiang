#include "dijiang/ServerSocket.hpp"
#include "dijiang/RdmaServerSocket.hpp"

#include <iostream>

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        std::cout << "<usage>: ./demo {-s|-c}" << std::endl;
        return 0;
    }
    bool is_server = (*(argv + 1) == "-s");
    std::cout << *(argv + 1) << std::endl;
    if (is_server)
    {
        std::cout << "server" << std::endl;
        char port[] = "3344";
        ServerSocket* socker = new RdmaServerSocket(port);
        socker->Loop();
    }
    else
    {
        std::cout << "client" << std::endl;
    }
    return 0;
}