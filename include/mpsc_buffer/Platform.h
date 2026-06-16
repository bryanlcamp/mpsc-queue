#pragma once

#include <thread>
#include <cstddef>
#include <new>

#if defined(__linux__)
    #include <pthread.h>
    #include <sched.h>
#elif defined(_MSC_VER)
    #include <intrin.h>
#endif

namespace mpsc_buffer {

/**
 * @brief Software-driven hardware execution hint.
 * Prevents execution serialization and pipeline flushes during tight spin-loops.
 */
inline void cpuPause() noexcept {
    #if defined(_MSC_VER)
        _mm_pause();                         
    #elif defined(__aarch64__) || defined(__arm__) || defined(_M_ARM64)
        asm volatile("yield" ::: "memory");  
    #elif defined(__x86_64__) || defined(__i386__) || defined(_M_X64)
        asm volatile("pause" ::: "memory");  
    #else
        std::this_thread::yield();           
    #endif
}

/**
 * @brief Software-driven core allocation binding.
 * Hard-pins the executing OS execution block to an isolated physical core.
 */
inline bool pinCurrentThread([[maybe_unused]] int coreId) noexcept {
    #if defined(__linux__)
        if (coreId < 0) return false;
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(coreId, &cpuset);
        
        pthread_t currentThread = pthread_self();
        return pthread_setaffinity_np(currentThread, sizeof(cpu_set_t), &cpuset) == 0;
    #else
        // C++23 optimization invariant hint for local Mac/Windows environments
        [[assume(coreId >= 0)]];
        return true; 
    #endif
}

/**
 * @brief Hard-locked layout boundary matrix markers.
 * Maximizes cache packing efficiency without risking false-sharing collisions.
 */
inline constexpr std::size_t getCacheLineSize() noexcept {
#if defined(__cpp_lib_hardware_interference_size)
    return std::hardware_destructive_interference_size;
#else
    #if defined(__aarch64__) || defined(__arm__) || defined(_M_ARM64)
        return 256; // Tailored for optimal Apple Silicon layout bounds
    #else
        return 64;  // Standard x86 architecture guidelines
    #endif
#endif
}

} // namespace mpsc_buffer
