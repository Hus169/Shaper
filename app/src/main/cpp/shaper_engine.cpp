#include <jni.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet2.h>
#include <chrono>
#include <thread>
#include <vector>
#include <cstring>
#include <atomic>
#include <mutex>
#include <stdexcept>
#include <errno.h>

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
    int proxy_fd_;

    void set_non_blocking(int fd) {
        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

public:
    TunMatrix(int fd, uint64_t kbps_limit) : tun_fd_(fd), bucket_(kbps_limit), running_(true) {
        set_non_blocking(tun_fd_);
        epoll_fd_ = epoll_create1(0);
        
        struct epoll_event ev{};
        ev.events = EPOLLIN | EPOLLET;
        ev.data.fd = tun_fd_;
        epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, tun_fd_, &ev);

        // Connect to local SOCKS5 proxy (must be running on the device)
        proxy_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in proxy_addr{};
        proxy_addr.sin_family = AF_INET;
        proxy_addr.sin_port = htons(1080); // Must match your local proxy port
        inet_pton(AF_INET, "127.0.0.1", &proxy_addr.sin_addr);
        connect(proxy_fd_, (struct sockaddr*)&proxy_addr, sizeof(proxy_addr));
        set_non_blocking(proxy_fd_);
    }

    ~TunMatrix() {
        if (epoll_fd_ >= 0) close(epoll_fd_);
        if (proxy_fd_ >= 0) close(proxy_fd_);
    }

    void run_event_loop() {
        struct epoll_event events[64];
        std::vector<uint8_t> buffer(65535);

        while (running_.load(std::memory_order_relaxed)) {
            int nfds = epoll_wait(epoll_fd_, events, 64, 100);
            if (nfds <= 0) continue;

            for (int i = 0; i < nfds; ++i) {
                if (events[i].data.fd == tun_fd_ && (events[i].events & EPOLLIN)) {
                    while (true) {
                        ssize_t len = read(tun_fd_, buffer.data(), buffer.size());
                        if (len <= 0) break;
                        
                        // 1. Enforce Rate Limiting
                        auto delay = bucket_.consume(len);
                        if (delay.count() > 0) std::this_thread::sleep_for(delay);

                        // 2. Forward shaped traffic to the local proxy
                        // Note: A full implementation would parse IP/TCP headers to map 
                        // connections. For this experimental matrix, we stream raw bytes 
                        // to the proxy, which handles the actual internet routing.
                        send(proxy_fd_, buffer.data(), len, MSG_NOSIGNAL);
                    }
                }
            }
        }
    }

    void stop() { running_.store(false, std::memory_order_relaxed); }
};

extern "C" JNIEXPORT void JNICALL
Java_com_axiom_shaper_ShaperVpnService_startShaper(JNIEnv* env, jobject, jint fd, jlong kbps_limit) {
    try {
        TunMatrix matrix(fd, static_cast<uint64_t>(kbps_limit));
        matrix.run_event_loop();
    } catch (...) {}
}
