#pragma once

#ifdef _WIN32
    #include <windows.h>
#else
    #include <pthread.h>
    #include <sched.h>
#endif

namespace FreeAI {
    namespace Utils {

        inline void SetThreadPriorityLow() {
#ifdef _WIN32
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#else
            pthread_t thread = pthread_self();
            struct sched_param params;
            params.sched_priority = sched_get_priority_min(SCHED_OTHER);
            pthread_setschedparam(thread, SCHED_OTHER, &params);
#endif
        }

        inline void SetThreadPriorityNormal() {
#ifdef _WIN32
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
#else
            pthread_t thread = pthread_self();
            struct sched_param params;
            params.sched_priority = sched_get_priority_max(SCHED_OTHER);
            pthread_setschedparam(thread, SCHED_OTHER, &params);
#endif
        }

    }
}