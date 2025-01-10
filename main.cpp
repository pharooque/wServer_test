#include <WinSock2.h>
#include <WS2tcpip.h>
#include <iostream>
#include <vector>
#include <stdexcept>
#include <string>
#include <string_view>
#include <memory>

class WSAInitializer
{
private:
    WSADATA wsaData;
public:
    WSAInitializer()
    {
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        {
            throw std::runtime_error("Failed to initialize WinSock DLL");
        }
    }

    const WSADATA& getWSAData() const { return wsaData; }
    ~WSAInitializer(){ WSACleanup(); }
};

class SocketWrapper
{
private:
    SOCKET serverSocket;
    SocketWrapper (const SocketWrapper&) = delete;
    SocketWrapper& operator=(const SocketWrapper&) = delete;
public:
    explicit SocketWrapper(SOCKET s = INVALID_SOCKET) : serverSocket(s){}
    ~SocketWrapper()
    {
        if (serverSocket != INVALID_SOCKET)
        {
            shutdown(serverSocket, SD_BOTH);
            closesocket(serverSocket);
        }
    }
    SOCKET getSocket() const { return serverSocket; }
    SOCKET release()
    {
        SOCKET  tempSock = serverSocket;
        serverSocket = INVALID_SOCKET;
        return tempSock;
    }
    void reset(SOCKET s = INVALID_SOCKET)
    {
        if (serverSocket != INVALID_SOCKET)
        {
            shutdown(serverSocket, SD_BOTH);
            closesocket(serverSocket);
        }
        serverSocket = s;
    }
};

constexpr int DEFAULT_PORT = 55555;
constexpr std::string_view DEFAULT_IP = "127.0.0.1";
constexpr size_t BUFFER_SIZE = 8192; // 8KB

class TCPServer
{
private:
    const char* serverIP;
    int serverPort;
    SocketWrapper serverSocket;

    void socketOpt()
    {
        // Enable keep-alive
        constexpr bool enable = TRUE;
        if (setsockopt(serverSocket.getSocket(), SOL_SOCKET, SO_KEEPALIVE, reinterpret_cast<const char*>(&enable), sizeof(enable)) == SOCKET_ERROR)
        {
            throw std::runtime_error("Failed to set SO_KEEPALIVE");
        }

        // Set receive buffer size
        constexpr int recvBufferSize = 64 * 1024; // 64KB
        setsockopt(serverSocket.getSocket(), SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&recvBufferSize), sizeof(recvBufferSize)) == SOCKET_ERROR;
        
        // Allow address reuse
        constexpr int addrReuse = 1;
        if (setsockopt(serverSocket.getSocket(), SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&addrReuse), sizeof(addrReuse)) == SOCKET_ERROR)  
        {
            throw std::runtime_error("Failed to set SO_REUSEADDR");
        }
    }

    void initialize()
    {
        // Create a socket
        serverSocket.reset(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
        if (serverSocket.getSocket() == INVALID_SOCKET)
        {
            throw std::runtime_error("Failed to create a socket: " + std::to_string(WSAGetLastError()));
        }
        
        // Set socket options
        socketOpt();

        // Bind the socket
        sockaddr_in service{}; // Stucture for server address
        service.sin_family = AF_INET;
        
        if (inet_pton(AF_INET, serverIP, &service.sin_addr) != 1) // Convert IP address to binary form
        {
            throw std::runtime_error("Invalid IP address format: " + std::string(serverIP));
        }
        service.sin_port = htons(static_cast<u_short>(serverPort));
        if (bind(serverSocket.getSocket(), reinterpret_cast<sockaddr*>(&service), sizeof(service)) == INVALID_SOCKET)
        {
            throw std::runtime_error("Failed to bind to " + std::string(serverIP) + ":" + std::to_string(serverPort));
        }
        
        // Listen for connections
        if (listen(serverSocket.getSocket(), 5) == SOCKET_ERROR)
        {
            throw std::runtime_error("Failed to listen on port " + std::to_string(serverPort));
        }
        
        std::cout << "Server initialized on " << serverIP << ":" << serverPort << "\n";
    }

    void acceptConnection()
    {
        std::cout << "Waiting for connections...\n";
        while (true)
        {
            SocketWrapper clientSocket(accept(serverSocket.getSocket(), nullptr, nullptr));
            if (clientSocket.getSocket() == INVALID_SOCKET)
            {
                throw std::runtime_error("Connection failed: " + std::to_string(WSAGetLastError()));
                continue;
            }
            std::cout << "Client connected\n";
            handleClient(clientSocket);
        }
    }

    void handleClient(SocketWrapper& clientSocket)
    {
        std::vector<char> buffer(BUFFER_SIZE);
        while (true)
        {
            int byteReceived = recv(clientSocket.getSocket(), buffer.data(), static_cast<int>(buffer.size()), 0);
            if (byteReceived == SOCKET_ERROR)
            {
                std::cerr << "Failed to recieve data: " << WSAGetLastError() << "\n";
                break;
            }

            if (byteReceived == 0)
            {
                std::cerr << "Client disconnected\n";
                break;
            }
            
            std::string message(buffer.data(), byteReceived);
            std::cout << "Received (" << byteReceived << " bytes): " << message << "\n";

            // Echo back to client
            if (send(clientSocket.getSocket(), buffer.data(), byteReceived, 0) == SOCKET_ERROR)
            {
                std::cerr << "Failed to send response: " << WSAGetLastError() << "\n";
                break;
            }
        }
    }

public:
    TCPServer(const char* ip = DEFAULT_IP.data(), int port = DEFAULT_PORT)
            : serverIP(ip), serverPort(port)
    {
        initialize();
    }

    void run()
    {
        acceptConnection();
    }
};

int main(int argc, char const *argv[])
{
    try
    {
        WSAInitializer wsaInit;
        std::cout << "WSAStartup success\nStatus: " << wsaInit.getWSAData().szSystemStatus << "\n";

        TCPServer server
        (
            argc > 1 ? argv[1] : DEFAULT_IP.data(),
            argc > 2 ? std::stoi(argv[2]) : DEFAULT_PORT
        );
        server.run();
    }
    catch(const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << '\n';
    }
    
    return 0;
}