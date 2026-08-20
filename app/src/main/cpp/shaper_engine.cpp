#include <jni.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <chrono>
#include <thread>
#include <vector>
#include <cstring>
#include <atomic>
#include <mutex>
#include <android/log.h>

#define LOG_TAG "AxiomShaper"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

class MicrosecondTokenBucket {
private:
    using Clock = std::chrono::steady_clock;
    using Micros = std::chrono::microseconds;
    double capacity_, tokens_, refill_rate_;
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
        LOGI("Token bucket initialized: %.2f bytes/sec", bytes_per_sec);
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
        }
        double deficit = required - tokens_;
        tokens_ = 0.0;
        return Micros(static_cast<long long>(deficit / refill_rate_));
    }
};

class TunMatrix {
private:
    int tun_fd_, epoll_fd_;
    std::atomic<bool> running_;
    MicrosecondTokenBucket bucket_;
    uint64_t packet_count_ = 0;

    void set_non_blocking(int fd) {
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags != -1) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

public:
    TunMatrix(int fd, uint64_t kbps_limit) : tun_fd_(fd), bucket_(kbps_limit), running_(true) {
        LOGI("Initializing TunMatrix with FD: %d", fd);
        set_non_blocking(tun_fd_);
        epoll_fd_ = epoll_create1(0);
        if (epoll_fd_ == -1) {
            LOGE("epoll_create1 failed: %s", strerror(errno));
        } else {
            struct epoll_event ev{};
            ev.events = EPOLLIN | EPOLLET;
            ev.data.fd = tun_fd_;
            epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, tun_fd_, &ev);
            LOGI("Epoll registered successfully.");
        }
    }

    ~TunMatrix() {
        if (epoll_fd_ >= 0) close(epoll_fd_);
        LOGI("TunMatrix destroyed.");
    }

    void run_event_loop() {
        struct epoll_event events[64];
        std::vector<uint8_t> buffer(65535);
        LOGI("Event loop started. Waiting for packets...");

        while (running_.load(std::memory_order_relaxederrno == EAGAIN || errno == EWOULDBLOCK) break;
                            LOGE("Read error: %s", strerror(errno));
                            break;
                        }
                        if (len == 0) break;

                        packet_count_++;
                        if (packet_count_ % 100 == 0) {
                            LOGI("Processed %llu packets", (unsigned long long)packet_count_);
                        }

                        // 1. Enforce Rate Limiting
                        auto delay = bucket_.consume(len);
                        if (delay.count() > 0) {
                            std::this_thread::sleep_for(delay);
                        }

                        // 2. Basic IPv4 UDP Forwarding Logic
                        if (len >= 28 && version == 4 && protocol == 17) { // UDP
                            uint32_t dest_ip = (buffer[16] << 24) | (buffer[17] << 16) | (buffer[18] << 8) | buffer[19];
                            uint16_t dest_port = (buffer[36] << 8) | buffer[37];
                            
                            // Note: For a full production app, this requires a connection map (NAT) 
                            // to route responses back to the TUN interface. 
                            // This experimental forwarder proves interception and delay are active.
                        }
                    }
                }
            }
        }
        LOGI("Event loop exited.");
    }

    void stop() { running_.store(false, std::memory_order_relaxed); }
};

extern "C" JNIEXPORT void JNICALL
Java_com_axiom_shaper_ShaperVpnService_startShaper(JNIEnv* env, jobject, jint fd, jlong kbps_limit) {
    LOGI("JNI startShaper called with FD: %d, Limit: %lld", fd, (long long)kbps_limit);
    try {
        TunMatrix matrix(fd, static_cast<uint64_t>(kbps_limit));
        matrix.run_event_loop();
    } catch (const std::exception& e) {
        LOGE("Exception in startShaper: %s", e.what());
    } catch (...) {
        LOGE("Unknown exception in startShaper");
    }
    LOGI("startShaper thread terminating.");
}
