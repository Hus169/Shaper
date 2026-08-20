// ==============================================================================
// File: app/src/main/cpp/shaper_engine.cpp
// ==============================================================================

#include <jni.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <chrono>
#include <thread>
#include <vector>
#include <iostream>
#include <cstring>
#include <atomic>
#include <mutex>
#include <stdexcept>
#include <errno.h>

// ==============================================================================
// 1. High-Precision Chronological Token Bucket
// ==============================================================================
class MicrosecondTokenBucket {
private:
    using Clock = std::chrono::steady_clock;
    using Micros = std::chrono::microseconds;

    double capacity_;
    double tokens_;
    double refill_rate_; // bytes per microsecond
    Clock::time_point last_update_;
    std::mutex mtx_;

public:
    MicrosecondTokenBucket(uint64_t kbps_limit) {
        double bytes_per_sec = (kbps_limit * 1000.0) / 8.0;
        refill_rate_ = bytes_per_sec / 1000000.0;
        capacity_ = bytes_per_sec * 0.1;
        if (capacity_ < 1500.0) capacity_ = 1500.0;
        tokens_ = capacity_;
        last_update_ = Clock::now();
    }

    Micros consume(size_t bytes) {
        std::lock_guard<std::mutex> lock(mtx_);
        auto now = Clock::now();
        auto elapsed_us = std::chrono::duration_cast<Micros>(now - last_update_).count();
        
        tokens_ += elapsed_us * refill_rate_;
        if (tokens_ > capacity_) tokens_ = capacity_;
        last_update_ = now;

        double required = static_cast<double>(bytes);
        if (tokens_ >= required) {
            tokens_ -= required;
            return Micros(0);
        } else {
            double deficit = required - tokens_;
            tokens_ = 0.0;
            double wait_us = deficit / refill_rate_;
            return Micros(static_cast<long long>(wait_us));
        }
    }
};

// ==============================================================================
// 2. POSIX Epoll TUN Matrix
// ==============================================================================
class TunMatrix {
private:
    int tun_fd_;
    int epoll_fd_;
    std::atomic<bool> running_;
    MicrosecondTokenBucket bucket_;

    void set_non_blocking(int fd) {
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags == -1) throw std::runtime_error("fcntl F_GETFL failed");
        if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
            throw std::runtime_error("fcntl F_SETFL failed");
        }
    }

public:
    TunMatrix(int fd, uint64_t kbps_limit) 
        : tun_fd_(fd), bucket_(kbps_limit), running_(true) {
        set_non_blocking(tun_fd_);
        epoll_fd_ = epoll_create1(0);
        if (epoll_fd_ == -1) throw std::runtime_error("epoll_create1 failed");

        struct epoll_event ev;
        ev.events = EPOLLIN | EPOLLET;
        ev.data.fd = tun_fd_;
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, tun_fd_, &ev) == -1) {
            throw std::runtime_error("epoll_ctl failed");
        }
    }

    ~TunMatrix() {
        if (epoll_fd_ >= 0) close(epoll_fd_);
    }

    void run_event_loop() {
        struct epoll_event events[64];
        std::vector<uint8_t> buffer(65535);

        while (running_.load(std::memory_order_relaxed)) {
            int nfds = epoll_wait(epoll_fd_, events, 64, 100);
            if (nfds == -1) {
                if (errno == EINTR) continue;
                break;
            }

            for (int i = 0; i < nfds; ++i) {
                if (events[i].data.fd == tun_fd_ && (events[i].events & EPOLLIN)) {
                    while (true) {
                        ssize_t len = read(tun_fd_, buffer.data(), buffer.size());
                        if (len < 0) {
                            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                            break;
                        }
                        if (len == 0) break;

                        process_and_shape_packet(buffer.data(), len);
                    }
                }
            }
        }
    }

    void process_and_shape_packet(const uint8_t* pkt, ssize_t len) {
        if (len < 20) return;

        // Architecture-agnostic IPv4 header parsing
        uint8_t version = (pkt[0] >> 4) & 0x0F;
        if (version != 4) return;

        // Enforce Chronological Rate Limiting
        auto delay = bucket_.consume(len);
        if (delay.count() > 0) {
            std::this_thread::sleep_for(delay);
        }

        // Note: In a production environment, the shaped packet would be 
        // forwarded to a user-space proxy socket here. For this matrix, 
        // it is consumed and dropped to prove the rate-limiting logic.
    }

    void stop() {
        running_.store(false, std::memory_order_relaxed);
    }
};

// ==============================================================================
// 3. JNI Bridge (Android NDK Entry Point)
// ==============================================================================
extern "C" JNIEXPORT void JNICALL
Java_com_axiom_shaper_ShaperVpnService_startShaper(
    JNIEnv* env, 
    jobject /* this */, 
    jint fd, 
    jlong kbps_limit) {
    
    try {
        TunMatrix matrix(fd, static_cast<uint64_t>(kbps_limit));
        matrix.run_event_loop();
    } catch (const std::exception& e) {
        // In production, use __android_log_print
    }
}
