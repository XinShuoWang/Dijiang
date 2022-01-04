#pragma once

#include <cstring>

class ClientSocket {
public:
    ClientSocket(const char* ip, const char * port) {
        ip_ = new char[20];
        port_ = new char[10];
        strcpy(ip_, ip);
        strcpy(port_, port);
    }

    virtual ~ClientSocket() {
        delete[] ip_;
        delete[] port_;
    }

    virtual void Write(const char* data, const int size) = 0;

private:
    char* ip_;
    char* port_;
};