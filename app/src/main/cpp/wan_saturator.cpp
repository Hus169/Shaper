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

#define LOG_TAG "AxiomSaturator"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

std::atomic<bool> g_saturator_running{false};

void saturate_thread(std::string target_ip, int target_port, uint64_t target_bps) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        LOGI("Socket creation failed");
        return;
    }

    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(target_port);
    inet_pton(AF_INET, target_ip.c_str(), &dest_addr.sin_addr);

    const size_t PAYLOAD_SIZE = 1400; // Max safe UDP payload
    std::vector<char> payload(PAYLOAD_SIZE, 'A');

    // Calculate timing: Bits per packet / Target bits per second = Seconds per packet
    double bits_per_packet = PAYLOAD_SIZE * 8.0;
    double packets_per_second = static_cast<double>(target_bps) / bits_per_packet;
    double delay_us = 1000000.0 / packets_per_second; // Microseconds
    
    LOGI("Target: %s:%d | Rate: %llu bps | Delay: %.2f us", 
         target_ip.c_str(), target_port, (unsigned long long)target_bps, delay_us);

    auto next_send = std::chrono::steady_clock::now();

    while (g_saturator_running.load(std::memory_order_relaxed)) {
        sendto(sock, payload.data(), payload.size(), 0, 
               (struct sockaddr*)&dest_addr, sizeof(dest_addr));

        next_send += std::chrono::microseconds(static_cast<long long>(delay_us));
        auto now = std::chrono::steady_clock::now();
        
        if (next_send > now) {
            std::this_thread::sleep_until(next_send);
        } else {
            next_send = now; // Prevent burst catching if we fall behind
        }
    }
    close(sock);
    LOGI("Saturator thread terminated.");
}

extern "C" JNIEXPORT void JNICALL
Java_com_axiom_shaper_WanSaturator_nativeStart(JNIEnv* env, jobject, jstring j_ip, jint j_port, jlong j_bps) {
    if (g_saturator_running.load()) return;
    
    const char* ip_str = env->GetStringUTFChars(j_ip, 0);
    std::string target_ip(ip_str);
    env->ReleaseStringUTFChars(j_ip, ip_str);

    g_saturator_running.store(true, std::memory_order_relaxed);
    std::thread(saturate_thread, target_ip, j_port, static_cast<uint64_t>(j_bps)).detach();
}

extern "C" JNIEXPORT void JNICALL
Java_com_axiom_shaper_WanSaturator_nativeStop(JNIEnv*, jobject) {
    g_saturator_running.store(false, std::memory_order_relaxed);
}
