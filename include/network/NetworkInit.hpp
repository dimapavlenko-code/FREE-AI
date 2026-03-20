#pragma once

namespace FreeAI {
    namespace Network {

        class NetworkEnvironment {
        public:
            static bool Initialize();
            static void Shutdown();
        };

    }
}