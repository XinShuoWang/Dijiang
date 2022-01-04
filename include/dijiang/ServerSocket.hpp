#pragma once

#include "ThreadPool.hpp"

#include <cstring>

class ServerSocket {
public:
    ServerSocket(const char* port) {
        port_ = new char[10];
        strcpy(port_, port);
    }

    virtual ~ServerSocket() {
        delete[] port_;
    }

    virtual void RegisterOnConnection(Task& func) = 0;
    virtual void RegisterOnDisconnect(Task& func) = 0;
    virtual void RegisterOnMessage(Task& func) = 0;
    virtual void Loop() = 0;
private:
    char* port_;
};