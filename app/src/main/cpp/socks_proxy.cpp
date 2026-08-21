#include <jni.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h> // Added for addrinfo and freeaddrinfo
#include <poll.h>
#include <thread>
#include <atomic>
#include <chrono>
#include <vector>
#include <random>
#include <android/log.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "AxiomGateway", __VA_ARGS__)
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

std::atomic<bool> g_stop{false};
int g_listen_fd = -1;
double g_bps = 1000000.0; // Default 1 Mbps
uint32_t g_delay = 0, g_jitter = 0; double g_loss = 0;

class DegradationEngine {
    double tokens, capacity, refill_rate;
    std::chrono::steady_clock::time_point last_update;
    std::mt19937 rng; uint32_t base_delay, jitter; double loss_pct;
public:
    DegradationEngine(double bps, uint32_t delay, uint32_t jit, double loss) 
        : base_delay(delay), jitter(jit), loss_pct(loss), rng(std::random_device{}()) {
        double bytes_per_sec = bps / 8.0; 
        refill_rate = bytes_per_sec / 1000000.0; // Bytes per microsecond
        capacity = bytes_per_sec * 0.1; if (capacity < 1500) capacity = 1500; tokens = capacity;
        last_update = std::chrono::steady_clock::now();
    }
    bool apply(size_t bytes) {
        if (loss_pct > 0 && std::uniform_real_distribution<>(0, 100)(rng) < loss_pct) return false;
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - last_update).count();
        tokens += elapsed * refill_rate; if (tokens > capacity) tokens = capacity; last_update = now;
        if (tokens < bytes) {
            double deficit = bytes - tokens; tokens = 0;
            std::this_thread::sleep_for(std::chrono::microseconds((long long)(deficit / refill_rate)));
        } else { tokens -= bytes; }
        if (jitter > 0) std::this_thread::sleep_for(std::chrono::milliseconds(base_delay + std::uniform_int_distribution<>(0, jitter)(rng)));
        else if (base_delay > 0) std::this_thread::sleep_for(std::chrono::milliseconds(base_delay));
        return true;
    }
};

void pipe_data(int src, int dst, DegradationEngine& eng) {
    std::vector<uint8_t> buf(1400);
    while (!g_stop.load()) {
        struct pollfd pfd{src, POLLIN, 0};
        if (poll(&pfd, 1, 200) <= 0) continue;
        ssize_t n = recv(src, buf.data(), buf.size(), 0);
        if (n <= 0) break;
        if (eng.apply(n)) send(dst, buf.data(), n, MSG_NOSIGNAL);
    }
    shutdown(src, SHUT_RDWR); shutdown(dst, SHUT_RDWR);
}

void handle_client(int c_fd) {
    int r_fd = -1; 
    uint8_t ver, nmeth;
    
    // Read version and number of methods
    if (recv(c_fd, &ver, 1, 0) != 1 || recv(c_fd, &nmeth, 1, 0) != 1) {
        close(c_fd); return;
    }
    
    std::vector<uint8_t> meth(nmeth);
    if (nmeth > 0 && recv(c_fd, meth.data(), nmeth, 0) != nmeth) {
        close(c_fd); return;
    }
    
    uint8_t auth[2] = {5, 0}; 
    send(c_fd, auth, 2, MSG_NOSIGNAL);
    
    uint8_t req[4]; 
    if (recv(c_fd, req, 4, 0) != 4) {
        close(c_fd); return;
    }
    
    std::string host;
    if (req[3] == 1) { 
        uint8_t ip[4]; 
        if (recv(c_fd, ip, 4, 0) != 4) { close(c_fd); return; }
        char b[16]; 
        inet_ntop(AF_INET, ip, b, 16); 
        host = b; 
    } else if (req[3] == 3) { 
        uint8_t len; 
        if (recv(c_fd, &len, 1, 0) != 1) { close(c_fd); return; }
        std::vector<char> d(len); 
        if (recv(c_fd, (uint8_t*)d.data(), len, 0) != len) { close(c_fd); return; }
        host.assign(d.begin(), d.end()); 
    } else {
        close(c_fd); return;
    }
    
    uint8_t p[2]; 
    if (recv(c_fd, p, 2, 0) != 2) {
        close(c_fd); return;
    }
    
    struct addrinfo hints{}, *res = nullptr; 
    hints.ai_family = AF_UNSPEC; 
    hints.ai_socktype = SOCK_STREAM;
    
    std::string port_str = std::to_string((p[0]<<8)|p[1]);
    if (getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res) != 0) {
        close(c_fd); return;
    }
    
    r_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (r_fd < 0 || connect(r_fd, res->ai_addr, res->ai_addrlen) < 0) { 
        if (r_fd >= 0) close(r_fd); 
        freeaddrinfo(res); 
        close(c_fd); 
        return; 
    }
    freeaddrinfo(res);
    
    uint8_t reply[10] = {5,0,0,1,0,0,0,0,0,0}; 
    send(c_fd, reply, 10, MSG_NOSIGNAL);
    
    DegradationEngine eng(g_bps, g_delay, g_jitter, g_loss);
    std::thread t1(pipe_data, c_fd, r_fd, std::ref(eng));
    std::thread t2(pipe_data, r_fd, c_fd, std::ref(eng));
    
    t1.join(); 
    t2.join();
    
    if (r_fd >= 0) close(r_fd); 
    close(c_fd);
}

void server_thread(int port) {
    g_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    int reuse = 1; setsockopt(g_listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    struct sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_port = htons(port); 
    addr.sin_addr.s_addr = htonl(INADDR_ANY); 
    if (bind(g_listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0 || listen(g_listen_fd, 64) < 0) {
        LOGI("Bind failed on port %d", port); close(g_listen_fd); g_listen_fd = -1; return;
    }
    LOGI("Gateway Proxy listening on 0.0.0.0:%d", port);
    while (!g_stop.load()) {
        struct pollfd pfd{g_listen_fd, POLLIN, 0};
        if (poll(&pfd, 1, 200) <= 0) continue;
        int c_fd = accept(g_listen_fd, nullptr, nullptr);
        if (c_fd >= 0) std::thread(handle_client, c_fd).detach();
    }
    close(g_listen_fd); g_listen_fd = -1;
}

extern "C" JNIEXPORT void JNICALL
Java_com_axiom_shaper_LocalProxy_nativeStart(JNIEnv*, jobject, jint port, jlong bps, jint delay, jint jitter, jdouble loss) {
    g_bps = static_cast<double>(bps); g_delay = delay; g_jitter = jitter; g_loss = loss;
    g_stop.store(false); std::thread(server_thread, port).detach();
}
extern "C" JNIEXPORT void JNICALL
Java_com_axiom_shaper_LocalProxy_nativeStop(JNIEnv*, jobject) {
    g_stop.store(true); if (g_listen_fd >= 0) close(g_listen_fd);
}
