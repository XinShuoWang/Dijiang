#pragma once

#include "ServerSocket.hpp"

class RdmaServerSocket : public ServerSocket
{
public:
    RdmaServerSocket(const char *port) : ServerSocket(port)
    {
        
    }

    ~RdmaServerSocket() override
    {
        
    }

    void RegisterOnConnection(Task &func) override
    {
        
    }

    void RegisterOnDisconnect(Task &func) override
    {

    }

    void RegisterOnMessage(Task &func) override
    {

    }

    void Loop() override
    {

    }

private:
};