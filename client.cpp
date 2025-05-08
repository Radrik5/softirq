#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <random>
#include <chrono>
#include <string>
#include <ctime>
#include <netinet/tcp.h>
#include "config.h"

// Function to set socket to non-blocking mode
bool setNonBlocking(int socket) {
    int flags = fcntl(socket, F_GETFL, 0);
    if (flags == -1) {
        std::cerr << "Failed to get socket flags" << std::endl;
        return false;
    }
    
    flags |= O_NONBLOCK;
    if (fcntl(socket, F_SETFL, flags) == -1) {
        std::cerr << "Failed to set socket to non-blocking mode" << std::endl;
        return false;
    }
    
    return true;
}

bool setTcpNoDelay(int socket) {
    int flag = 1;
    if (setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) < 0) {
        std::cerr << "Error setting TCP_NODELAY: " << strerror(errno) << std::endl;
        return false;
    }
    return true;
}

int main(int argc, char* argv[]) {
    // Parse command line argument for duration (default: 5 seconds)
    int duration = 5;
    if (argc > 1) {
        try {
            duration = std::stoi(argv[1]);
            if (duration <= 0) {
                std::cerr << "Duration must be a positive number" << std::endl;
                return 1;
            }
        } catch (const std::exception& e) {
            std::cerr << "Invalid duration: " << e.what() << std::endl;
            return 1;
        }
    }
    
    // Create socket
    int clientSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (clientSocket == -1) {
        std::cerr << "Failed to create socket" << std::endl;
        return 1;
    }
    
    // Prepare server address
    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(SERVER_PORT);
    
    if (inet_pton(AF_INET, SERVER_IP, &serverAddr.sin_addr) <= 0) {
        std::cerr << "Invalid address / Address not supported" << std::endl;
        close(clientSocket);
        return 1;
    }
    
    // Connect to server
    std::cout << "Connecting to server at " << SERVER_IP << ":" << SERVER_PORT << "..." << std::endl;
    if (connect(clientSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        std::cerr << "Connection failed: " << strerror(errno) << std::endl;
        close(clientSocket);
        return 1;
    }
    std::cout << "Connected to server" << std::endl;
    
    // Set socket to non-blocking mode
    if (!setNonBlocking(clientSocket) || !setTcpNoDelay(clientSocket)) {
        close(clientSocket);
        return 1;
    }
    
    // Setup faster non-cryptographically secure PRNG
    unsigned int seed = static_cast<unsigned int>(time(nullptr));
    std::minstd_rand fast_gen(seed);
    std::uniform_int_distribution<> distrib(0, 255);
    
    // Buffer for sending/receiving data
    unsigned char buffer[BUFFER_SIZE];
    
    // Track start time
    auto startTime = std::chrono::steady_clock::now();
    auto endTime = startTime + std::chrono::seconds(duration);
    
    std::cout << "Starting high CPU usage simulation for " << duration << " seconds..." << std::endl;
    
    // Main loop
    int sendCount = 0;
    int recvCount = 0;
    
    while (std::chrono::steady_clock::now() < endTime) {
        // Fill buffer with random bytes using faster PRNG
        for (size_t i = 0; i < BUFFER_SIZE; ++i) {
            buffer[i] = static_cast<unsigned char>(distrib(fast_gen));
        }
        
        // Send data with busy polling
        size_t totalSent = 0;
        while (totalSent < BUFFER_SIZE) {
            int sent = send(clientSocket, buffer + totalSent, BUFFER_SIZE - totalSent, 0);
            
            if (sent > 0) {
                totalSent += sent;
            } else if (sent == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // Socket is not ready for writing, continue busy polling
                    continue;
                } else {
                    std::cerr << "Send error: " << strerror(errno) << std::endl;
                    close(clientSocket);
                    return 1;
                }
            }
        }
        sendCount++;
        
        // Receive data with busy polling
        size_t totalReceived = 0;
        while (totalReceived < BUFFER_SIZE) {
            int received = recv(clientSocket, buffer + totalReceived, BUFFER_SIZE - totalReceived, 0);
            
            if (received > 0) {
                totalReceived += received;
            } else if (received == 0) {
                std::cerr << "Connection closed by server" << std::endl;
                close(clientSocket);
                return 1;
            } else if (received == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // Socket is not ready for reading, continue busy polling
                    continue;
                } else {
                    std::cerr << "Receive error: " << strerror(errno) << std::endl;
                    close(clientSocket);
                    return 1;
                }
            }
        }
        recvCount++;
        
        // Optionally add a very small delay to prevent completely maxing out the CPU
        // std::this_thread::sleep_for(std::chrono::microseconds(1));
    }
    
    // Calculate and display statistics
    auto actualDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - startTime).count() / 1000.0;
    
    std::cout << "Simulation completed in " << actualDuration << " seconds" << std::endl;
    std::cout << "Sent " << sendCount << " packets (" << (sendCount * BUFFER_SIZE / 1024) << " KB)" << std::endl;
    std::cout << "Received " << recvCount << " packets (" << (recvCount * BUFFER_SIZE / 1024) << " KB)" << std::endl;
    
    // Close socket
    close(clientSocket);
    
    return 0;
}
