#include <iostream>
#include <boost/asio.hpp>
#include <string>
#include <thread>
#include <chrono>

using boost::asio::ip::tcp;

void start_server() {
    try {
        // Setup IO context
        boost::asio::io_context io_context;

        // Listen for incoming connections on port 20000 (DNP3 Port)
        tcp::endpoint endpoint(tcp::v4(), 20000);
        tcp::acceptor acceptor(io_context, endpoint);

        std::cout << "[INFO] Listening for incoming connections on port 20000..." << std::endl;

        while (true) {
            // Accept an incoming connection
            tcp::socket socket(io_context);
            acceptor.accept(socket);
            std::cout << "[INFO] Connection established with client." << std::endl;

            // Read data sent by the client (the Python script)
            char data[1024];
            size_t length = socket.read_some(boost::asio::buffer(data));
            data[length] = '\0';  // Null-terminate the received data
            std::string received_data(data);

            std::cout << "[RECEIVED] Data from client: " << received_data << std::endl;

            // Process the data (e.g., parse the sensor values)
            // You can add further processing here, like logging or triggering events.

            // Close the connection
            socket.close();
        }
    } catch (std::exception& e) {
        std::cerr << "[ERROR] " << e.what() << std::endl;
    }
}

int main() {
    std::cout << "[INFO] Starting DNP3 Outstation server..." << std::endl;

    // Start the TCP server to receive data
    start_server();

    return 0;
}
