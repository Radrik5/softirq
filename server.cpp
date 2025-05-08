#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <csignal>
#include <vector>
#include <map>
#include <cstdlib>
#include <ctime>
#include <netinet/tcp.h>
#include "config.h"

// Constants for epoll
constexpr int MAX_EVENTS = 64;
constexpr int EPOLL_TIMEOUT_MS = 0; // Zero timeout for busy polling

// Global flag for termination
volatile sig_atomic_t g_running = 1;

// Signal handler for SIGINT
void signalHandler(int signal) {
    if (signal == SIGINT) {
        std::cout << "\nReceived SIGINT. Shutting down server..." << std::endl;
        g_running = 0;
    }
}

// Function to set socket to non-blocking mode
bool setNonBlocking(int socket) {
    int flags = fcntl(socket, F_GETFL, 0);
    if (flags == -1) {
        std::cerr << "Failed to get socket flags: " << strerror(errno) << std::endl;
        return false;
    }
    
    flags |= O_NONBLOCK;
    if (fcntl(socket, F_SETFL, flags) == -1) {
        std::cerr << "Failed to set socket to non-blocking mode: " << strerror(errno) << std::endl;
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

// Structure to maintain client data
struct ClientData {
    std::vector<unsigned char> buffer;
    size_t bytesReceived;
    size_t bytesSent;
    bool receivingData;
    
    ClientData() : buffer(BUFFER_SIZE), bytesReceived(0), bytesSent(0), receivingData(true) {}
};

int main() {
    // Initialize random number generator
    std::srand(static_cast<unsigned int>(std::time(nullptr)));
    
    // Set up signal handling for SIGINT
    struct sigaction sa;
    sa.sa_handler = signalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    
    if (sigaction(SIGINT, &sa, nullptr) == -1) {
        std::cerr << "Failed to set up signal handler: " << strerror(errno) << std::endl;
        return 1;
    }
    
    // Create listening socket
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == -1) {
        std::cerr << "Failed to create socket: " << strerror(errno) << std::endl;
        return 1;
    }
    
    // Set socket option to reuse address
    int opt = 1;
    if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        std::cerr << "Failed to set SO_REUSEADDR: " << strerror(errno) << std::endl;
        close(serverSocket);
        return 1;
    }
    
    // Set listening socket to non-blocking mode
    if (!setNonBlocking(serverSocket)) {
        close(serverSocket);
        return 1;
    }
    
    // Prepare server address
    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(SERVER_PORT);
    
    if (inet_pton(AF_INET, SERVER_IP, &serverAddr.sin_addr) <= 0) {
        std::cerr << "Invalid address / Address not supported: " << strerror(errno) << std::endl;
        close(serverSocket);
        return 1;
    }
    
    // Bind socket to address
    if (bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == -1) {
        std::cerr << "Failed to bind socket: " << strerror(errno) << std::endl;
        close(serverSocket);
        return 1;
    }
    
    // Listen for incoming connections
    if (listen(serverSocket, SOMAXCONN) == -1) {
        std::cerr << "Failed to listen on socket: " << strerror(errno) << std::endl;
        close(serverSocket);
        return 1;
    }
    
    std::cout << "Server listening on " << SERVER_IP << ":" << SERVER_PORT << std::endl;
    
    // Create epoll instance
    int epollFd = epoll_create1(0);
    if (epollFd == -1) {
        std::cerr << "Failed to create epoll instance: " << strerror(errno) << std::endl;
        close(serverSocket);
        return 1;
    }
    
    // Add listening socket to epoll
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = serverSocket;
    
    if (epoll_ctl(epollFd, EPOLL_CTL_ADD, serverSocket, &ev) == -1) {
        std::cerr << "Failed to add listening socket to epoll: " << strerror(errno) << std::endl;
        close(serverSocket);
        close(epollFd);
        return 1;
    }
    
    // Buffer for epoll events
    struct epoll_event events[MAX_EVENTS];
    
    // Map to track client data
    std::map<int, ClientData> clients;
    
    // Statistics
    int totalConnections = 0;
    int activeConnections = 0;
    long totalBytesProcessed = 0;
    
    // Main server loop
    std::cout << "Server started. Press Ctrl+C to stop." << std::endl;
    
    while (g_running) {
        // Wait for events with busy polling (zero timeout)
        int numEvents = epoll_wait(epollFd, events, MAX_EVENTS, EPOLL_TIMEOUT_MS);
        
        if (numEvents == -1) {
            if (errno == EINTR) {
                // Interrupted by signal, check if we're still running
                continue;
            }
            std::cerr << "epoll_wait failed: " << strerror(errno) << std::endl;
            break;
        }
        
        // Process events
        for (int i = 0; i < numEvents; ++i) {
            // If event on the listening socket, accept new connection
            if (events[i].data.fd == serverSocket) {
                struct sockaddr_in clientAddr;
                socklen_t clientLen = sizeof(clientAddr);
                
                int clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddr, &clientLen);
                if (clientSocket == -1) {
                    if (errno != EAGAIN && errno != EWOULDBLOCK) {
                        std::cerr << "Failed to accept connection: " << strerror(errno) << std::endl;
                    }
                    continue;
                }
                
                // Set new client socket to non-blocking mode
                if (!setNonBlocking(clientSocket) || !setTcpNoDelay(clientSocket)) {
                    close(clientSocket);
                    continue;
                }
                
                // Add client socket to epoll
                struct epoll_event clientEv;
                clientEv.events = EPOLLIN | EPOLLET; // Edge-triggered mode
                clientEv.data.fd = clientSocket;
                
                if (epoll_ctl(epollFd, EPOLL_CTL_ADD, clientSocket, &clientEv) == -1) {
                    std::cerr << "Failed to add client socket to epoll: " << strerror(errno) << std::endl;
                    close(clientSocket);
                    continue;
                }
                
                // Initialize client data
                clients[clientSocket] = ClientData();
                
                // Display client connection info
                char clientIP[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, sizeof(clientIP));
                
                totalConnections++;
                activeConnections++;
                std::cout << "New connection from " << clientIP << ":" << ntohs(clientAddr.sin_port) 
                          << " (fd: " << clientSocket << ", total: " << activeConnections << ")" << std::endl;
            }
            // If event on client socket, process data
            else {
                int clientSocket = events[i].data.fd;
                
                // Check if the socket exists in our map
                if (clients.find(clientSocket) == clients.end()) {
                    // Unexpected socket, remove from epoll and close
                    epoll_ctl(epollFd, EPOLL_CTL_DEL, clientSocket, nullptr);
                    close(clientSocket);
                    continue;
                }
                
                ClientData& client = clients[clientSocket];
                
                // If we're in receiving mode
                if (client.receivingData) {
                    while (client.bytesReceived < BUFFER_SIZE) {
                        ssize_t bytesRead = recv(clientSocket, 
                                                client.buffer.data() + client.bytesReceived, 
                                                BUFFER_SIZE - client.bytesReceived, 
                                                0);
                        
                        if (bytesRead > 0) {
                            client.bytesReceived += bytesRead;
                        } else if (bytesRead == 0) {
                            // Client disconnected
                            epoll_ctl(epollFd, EPOLL_CTL_DEL, clientSocket, nullptr);
                            close(clientSocket);
                            totalBytesProcessed += client.bytesReceived;
                            clients.erase(clientSocket);
                            activeConnections--;
                            std::cout << "Client disconnected (fd: " << clientSocket 
                                      << ", remaining: " << activeConnections << ")" << std::endl;
                            break;
                        } else if (bytesRead == -1) {
                            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                                // No more data available right now, continue busy polling
                                continue;
                            } else {
                                // Error occurred
                                std::cerr << "Error reading from client (fd: " << clientSocket 
                                          << "): " << strerror(errno) << std::endl;
                                epoll_ctl(epollFd, EPOLL_CTL_DEL, clientSocket, nullptr);
                                close(clientSocket);
                                clients.erase(clientSocket);
                                activeConnections--;
                                break;
                            }
                        }
                    }
                    
                                            // If we received all data, process it and switch to sending mode
                    if (client.bytesReceived == BUFFER_SIZE) {
                        // XOR each byte in the buffer with a new random byte
                        for (size_t j = 0; j < BUFFER_SIZE; ++j) {
                            unsigned char randomByte = static_cast<unsigned char>(std::rand() % 256);
                            client.buffer[j] ^= randomByte;
                        }
                        
                        // Switch to sending mode
                        client.receivingData = false;
                        client.bytesSent = 0;
                        
                        // Modify the event to monitor for write readiness
                        struct epoll_event clientEv;
                        clientEv.events = EPOLLOUT | EPOLLET;
                        clientEv.data.fd = clientSocket;
                        
                        if (epoll_ctl(epollFd, EPOLL_CTL_MOD, clientSocket, &clientEv) == -1) {
                            std::cerr << "Failed to modify client socket event: " << strerror(errno) << std::endl;
                            close(clientSocket);
                            clients.erase(clientSocket);
                            activeConnections--;
                        }
                    }
                }
                // If we're in sending mode
                else {
                    while (client.bytesSent < BUFFER_SIZE) {
                        ssize_t bytesSent = send(clientSocket, 
                                               client.buffer.data() + client.bytesSent, 
                                               BUFFER_SIZE - client.bytesSent, 
                                               0);
                        
                        if (bytesSent > 0) {
                            client.bytesSent += bytesSent;
                        } else if (bytesSent == -1) {
                            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                                // Socket is not ready for writing, continue busy polling
                                continue;
                            } else {
                                // Error occurred
                                std::cerr << "Error sending to client (fd: " << clientSocket 
                                          << "): " << strerror(errno) << std::endl;
                                epoll_ctl(epollFd, EPOLL_CTL_DEL, clientSocket, nullptr);
                                close(clientSocket);
                                clients.erase(clientSocket);
                                activeConnections--;
                                break;
                            }
                        }
                    }
                    
                    // If we sent all data, switch back to receiving mode
                    if (client.bytesSent == BUFFER_SIZE) {
                        // Update statistics
                        totalBytesProcessed += BUFFER_SIZE;
                        
                        // Reset for next reception
                        client.receivingData = true;
                        client.bytesReceived = 0;
                        
                        // Modify the event to monitor for read readiness
                        struct epoll_event clientEv;
                        clientEv.events = EPOLLIN | EPOLLET;
                        clientEv.data.fd = clientSocket;
                        
                        if (epoll_ctl(epollFd, EPOLL_CTL_MOD, clientSocket, &clientEv) == -1) {
                            std::cerr << "Failed to modify client socket event: " << strerror(errno) << std::endl;
                            close(clientSocket);
                            clients.erase(clientSocket);
                            activeConnections--;
                        }
                    }
                }
            }
        }
        
        // Optional small delay to prevent 100% CPU usage
        // usleep(1); // Uncomment this line if you want to control CPU usage
    }
    
    // Clean up
    std::cout << "Shutting down server..." << std::endl;
    std::cout << "Total connections: " << totalConnections << std::endl;
    std::cout << "Total bytes processed: " << totalBytesProcessed << " (" 
              << (totalBytesProcessed / 1024) << " KB)" << std::endl;
    
    // Close all client connections
    for (const auto& client : clients) {
        close(client.first);
    }
    
    // Close server socket and epoll
    close(serverSocket);
    close(epollFd);
    
    return 0;
}
