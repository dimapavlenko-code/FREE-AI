#pragma once
#include <string>

namespace FreeAI {
    namespace Network {

        class NetworkEnvironment {
        public:
            static bool Initialize();
            static void Shutdown();
            
            // Initialize STUN server on the given port (returns port used, or -1 on failure)
            static int InitializeSTUNServer(int port = 3478);
            
            // Shutdown the STUN server
            static void ShutdownSTUNServer();
        };

    }
}