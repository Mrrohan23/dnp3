#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <boost/asio.hpp>

#include <asiodnp3/DNP3Manager.h>
#include <asiodnp3/PrintingChannelListener.h>
#include <asiodnp3/PrintingSOEHandler.h>
#include <asiodnp3/DefaultOutstationApplication.h>
#include <opendnp3/outstation/SimpleCommandHandler.h>
#include <opendnp3/LogLevels.h>
#include <asiodnp3/UpdateBuilder.h>

#include <asiodnp3/TLSConfig.h>

using namespace std;
using namespace boost::asio;
using namespace asiodnp3;
using namespace opendnp3;

void handle_sensor_data(const std::string& data, std::shared_ptr<IOutstation> outstation)
{
    float temperature, pressure, humidity;
    sscanf(data.c_str(), "%f,%f,%f", &temperature, &pressure, &humidity);

    UpdateBuilder builder;
    builder.Update(Analog(temperature), 0);
    builder.Update(Analog(pressure), 1);
    builder.Update(Analog(humidity), 2);

    outstation->Apply(builder.Build());
}

void start_python_data_server(std::shared_ptr<IOutstation> outstation)
{
    try {
        io_context io;
        ip::tcp::acceptor acceptor(io, ip::tcp::endpoint(ip::tcp::v4(), 15000));
        std::cout << "[INFO] Listening for Python sensor data on port 15000..." << std::endl;

        while (true) {
            ip::tcp::socket socket(io);
            acceptor.accept(socket);
            std::cout << "[INFO] Python client connected." << std::endl;

            char data[1024];
            size_t length = socket.read_some(buffer(data));
            data[length] = '\0';
            std::string received_data(data);

            std::cout << "[DATA RECEIVED] " << received_data << std::endl;
            handle_sensor_data(received_data, outstation);

            socket.close();
        }

    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Python TCP server failed: " << e.what() << std::endl;
    }
}

int main()
{
    std::cout << "[INFO] Starting DNP3 Outstation with TLS..." << std::endl;

    const uint32_t FILTERS = levels::NORMAL | levels::ALL_COMMS;
    DNP3Manager manager(1, ConsoleLogger::Create());

    // TLS configuration
    TLSConfig tlsConfig(
        "server-cert.pem",   // Server cert
        "server-key.pem",    // Server private key
        "ca-cert.pem",       // Trusted CA certs
        CertificateMode::VerifyIfPresent
    );

    auto channel = manager.AddTLSServer(
        "tls-server",
        FILTERS,
        ChannelRetry::Default(),
        tlsConfig,
        "0.0.0.0",
        20000,
        PrintingChannelListener::Create()
    );

    OutstationStackConfig stackConfig(DatabaseSizes::AllTypes(10));
    stackConfig.link.LocalAddr = 10;
    stackConfig.link.RemoteAddr = 1;
    stackConfig.outstation.eventBufferConfig = EventBufferConfig::AllTypes(10);
    stackConfig.outstation.params.allowUnsolicited = true;

    auto outstation = channel->AddOutstation(
        "outstation",
        SuccessCommandHandler::Create(),
        DefaultOutstationApplication::Create(),
        stackConfig
    );

    outstation->Enable();

    // Run Python listener on a separate thread
    std::thread pythonListener(start_python_data_server, outstation);
    pythonListener.join();

    return 0;
}

