#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <csignal>
#include <memory>
#include <random>
#include <iomanip>
#include <algorithm>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

// ==============================================================================
// 1. Global State & Telemetry
// ==============================================================================
static std::atomic<bool> g_stop{false};
static std::atomic<uint64_t> g_total_bytes{0};
static std::atomic<uint64_t> g_dropped_packets{0};

static void handle_signal(int) { g_stop.store(true, std::memory_order_relaxed); }

// ==============================================================================
// 2. The "Terrible WiFi" Simulator (Bandwidth + Latency + Loss)
// ==============================================================================
class NetworkDegradationEngine {
private:
    using Clock = std::chrono::steady_clock;
    using Micros = std::chrono::microseconds;

    // Bandwidth (Token Bucket)
    double capacity_, tokens_, refill_rate_;
    Clock::time_point last_update_;
    std::mutex mtx_;

    // Latency & Jitter
    uint32_t base_delay_ms_;
    uint32_t jitter_ms_;
    std::mt19937 rng_;
    std::uniform_int_distribution<> jitter_dist_;

    // Packet Loss
    double loss_percent_;
    std::uniform_real_distribution<> loss_dist_;

public:
    NetworkDegradationEngine(uint64_t kbps_limit, uint32_t delay_ms, uint32_t jitter, double loss_pct)
        : base_delay_ms_(delay_ms), jitter_ms_(jitter), loss_percent_(loss_pct),
          rng_(std::random_device{}()), jitter_dist_(0, jitter), loss_dist_(0.0, 100.0) {
        
        double bytes_per_sec = (static_cast<double>(kbps_limit) * 1000.0) / 8.0;
        refill_rate_ = bytes_per_sec / 1000000.0;
        capacity_ = bytes_per_sec * 0.1;
        if (capacity_ < 1500.0) capacity_ = 1500.0;
        tokens_ = capacity_;
        last_update_ = Clock::now();
    }

    // Returns true if the packet should be sent, false if it should be dropped
    bool apply_degradation(size_t bytes) {
        // 1. Stochastic Packet Loss
        if (loss_percent_ > 0.0 && loss_dist_(rng_) < loss_percent_) {
            g_dropped_packets.fetch_add(1, std::memory_order_relaxed);
            return false; // Drop the packet
        }

        // 2. Bandwidth Throttling (Token Bucket)
        {
            std::lock_guard<std::mutex> lock(mtx_);
            auto now = Clock::now();
            auto elapsed_us = std::chrono::duration_cast<Micros>(now - last_update_).count();
            tokens_ += static_cast<double>(elapsed_us) * refill_rate_;
            if (tokens_ > capacity_) tokens_ = capacity_;
            last_update_ = now;

            double required = static_cast<double>(bytes);
            if (tokens_ < required) {
                double deficit = required - tokens_;
                tokens_ = 0.0;
                double wait_us = deficit / refill_rate_;
                std::this_thread::sleep_for(Micros(static_cast<long long>(wait_us)));
            } else {
                tokens_ -= required;
            }
        }

        // 3. Latency & Jitter Injection
        uint32_t actual_delay = base_delay_ms_;
        if (jitter_ms_ > 0) {
            actual_delay += jitter_dist_(rng_);
        }
        if (actual_delay > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(actual_delay));
        }
        
        return true; // Send the packet
    }
};

// ==============================================================================
// 3. Socket Utilities & SOCKS5 Handshake
// ==============================================================================
static bool wait_fd(int fd, short events, int timeout_ms) {
    struct pollfd pfd{fd, events, 0};
    return poll(&pfd, 1, timeout_ms) > 0;
}

static void recv_exact(int fd, uint8_t* out, size_t len) {
    size_t got = 0;
    while (got < len) {
        if (!wait_fd(fd, POLLIN, 5000)) throw std::runtime_error("recv timeout");
        ssize_t n = recv(fd, out + got, len - got, 0);
        if (n <= 0) throw std::runtime_error("connection closed");
        got += n;
    }
}

static void send_all(int fd, const uint8_t* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        if (!wait_fd(fd, POLLOUT, 5000)) throw std::runtime_error("send timeout");
        ssize_t n = send(fd, data + sent, len - sent, MSG_NOSIGNAL);
        if (n < 0) throw std::runtime_error(strerror(errno));
        sent += n;
    }
}

static int connect_to_host(const std::string& host, uint16_t port) {
    struct addrinfo hints{}, *res;
    hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0) 
        throw std::runtime_error("DNS resolution failed");
    
    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (connect(fd, res->ai_addr, res->ai_addrlen) < 0) {
        close(fd); freeaddrinfo(res);
        throw std::runtime_error("connect failed");
    }
    freeaddrinfo(res);
    return fd;
}

// ==============================================================================
// 4. Bidirectional Degradation Pipe
// ==============================================================================
static void pipe_data(int src_fd, int dst_fd, NetworkDegradationEngine& engine) {
    std::vector<uint8_t> buffer(1400);
    try {
        while (!g_stop.load(std::memory_order_relaxed)) {
            if (!wait_fd(src_fd, POLLIN, 200)) continue;
            ssize_t n = recv(src_fd, buffer.data(), buffer.size(), 0);
            if (n <= 0) break;

            // Apply the "Terrible WiFi" matrix
            // If it returns false, the packet is dropped and not forwarded
            if (engine.apply_degradation(n)) {
                send_all(dst_fd, buffer.data(), n);
                g_total_bytes.fetch_add(n, std::memory_order_relaxed);
            }
        }
    } catch (...) {}
    shutdown(src_fd, SHUT_RDWR);
    shutdown(dst_fd, SHUT_RDWR);
}

// ==============================================================================
// 5. Execution Entry Point
// ==============================================================================
int main(int argc, char* argv[]) {
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);
    std::signal(SIGPIPE, SIG_IGN);

    uint16_t port = 1080;
    uint64_t kbps = 500;       // 500 kbps (Terrible)
    uint32_t delay = 150;      // 150ms latency
    uint32_t jitter = 50;      // +/- 50ms jitter
    double loss = 3.0;         // 3% packet loss

    if (argc >= 2) kbps = std::stoull(argv[1]);
    if (argc >= 3) delay = std::stoul(argv[2]);
    if (argc >= 4) jitter = std::stoul(argv[3]);
    if (argc >= 5) loss = std::stod(argv[4]);

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    int reuse = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0 || listen(listen_fd, 64) < 0) {
        std::cerr << "[-] Bind failed. Is port " << port << " in use?\n";
        return 1;
    }

    std::cout << "[*] Axiom Terrible WiFi Simulator Active.\n";
    std::cout << "[*] Proxy: socks5h://127.0.0.1:" << port << "\n";
    std::cout << "[*] Matrix: " << kbps << " kbps | " << delay << "ms delay | " 
              << jitter << "ms jitter | " << loss << "% loss\n";

    std::thread stats([&]() {
        while (!g_stop.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            std::cout << "[+] Shaped: " << g_total_bytes.load() << " bytes | "
                      << "Dropped: " << g_dropped_packets.load() << " packets\n";
        }
    });

    while (!g_stop.load(std::memory_order_relaxed)) {
        if (!wait_fd(listen_fd, POLLIN, 200)) continue;
        int client_fd = accept(listen_fd, nullptr, nullptr);
        if (client_fd < 0) continue;

        std::thread([client_fd, kbps, delay, jitter, loss]() {
            int remote_fd = -1;
            try {
                uint8_t ver, nmethods;
                recv_exact(client_fd, &ver, 1); recv_exact(client_fd, &nmethods, 1);
                std::vector<uint8_t> methods(nmethods);
                if (nmethods > 0) recv_exact(client_fd, methods.data(), nmethods);
                uint8_t auth_resp[2] = {0x05, 0x00};
                send_all(client_fd, auth_resp, 2);

                uint8_t req[4]; recv_exact(client_fd, req, 4);
                std::string host;
                if (req[3] == 0x01) { uint8_t ip[4]; recv_exact(client_fd, ip, 4); char buf[16]; inet_ntop(AF_INET, ip, buf, 16); host = buf; }
                else if (req[3] == 0x03) { uint8_t len; recv_exact(client_fd, &len, 1); std::vector<char> d(len); recv_exact(client_fd, (uint8_t*)d.data(), len); host.assign(d.begin(), d.end()); }
                uint8_t p[2]; recv_exact(client_fd, p, 2);
                uint16_t rport = (p[0] << 8) | p[1];

                remote_fd = connect_to_host(host, rport);
                uint8_t reply[10] = {0x05, 0x00, 0x00, 0x01, 0,0,0,0, 0,0};
                send_all(client_fd, reply, 10);

                NetworkDegradationEngine engine(kbps, delay, jitter, loss);
                std::thread t1(pipe_data, client_fd, remote_fd, std::ref(engine));
                std::thread t2(pipe_data, remote_fd, client_fd, std::ref(engine));
                t1.join(); t2.join();
            } catch (...) {}
            if (remote_fd >= 0) close(remote_fd);
            close(client_fd);
        }).detach();
    }
    close(listen_fd);
    return 0;
}
