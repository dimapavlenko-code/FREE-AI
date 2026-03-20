#pragma once

#include <string>
#include <vector>

namespace FreeAI {
    namespace Network {

        class ISocket {
        public:
            virtual ~ISocket() = default;
            virtual bool connect(const std::string& host, int port) = 0;
            virtual bool bind(int port) = 0;
            virtual int send(const void* data, size_t length) = 0;
            virtual int receive(void* buffer, size_t length) = 0;
            virtual void close() = 0;
            virtual bool is_valid() const = 0;
        };

    }
}
