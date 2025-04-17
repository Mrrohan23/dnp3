#include <iostream>
#include <string>
#include <thread>
#include <boost/asio.hpp>

#include <asiodnp3/DNP3Manager.h>
#include <asiodnp3/PrintingChannelListener.h>
#include <asiodnp3/ConsoleLogger.h>
#include <asiodnp3/UpdateBuilder.h>
#include <asiodnp3/DefaultOutstationApplication.h>
#include <asiodnp3/TLSConfig.h>

#include <opendnp3/outstation/SimpleCommandHandler.h>
#include <opendnp3/LogLevels.h>
#include <opendnp3/outstation/DatabaseConfig.h>
#include <opendnp3/outstation/EventBufferConfig.h>

using namespace std;
using namespace boost::asio;
using namespace opendnp3;
using namespace asiodnp3;

void handle_sensor_data(const std::string& data, std::shared_ptr<IOutstation> outstation)
{
    float temperature = 0, pressure = 0, humidity = 0;
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
        boost::asio::io_context io;
        ip::tcp::acceptor acceptor(io, ip::tcp::endpoint(ip::tcp::v4(), 15000));
        std::cout << "[INFO] Listening for sensor data on port 15000..." << std::endl;

        while (true) {
            ip::tcp::socket socket(io);
            acceptor.accept(socket);
            std::cout << "[INFO] Sensor data client connected." << std::endl;

            char data[1024];
            size_t length = socket.read_some(boost::asio::buffer(data));
            data[length] = '\0';
            std::string received_data(data);

            std::cout << "[DATA RECEIVED] " << received_data << std::endl;
            handle_sensor_data(received_data, outstation);

            socket.close();
        }

    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Sensor server failed: " << e.what() << std::endl;
    }
}

int main()
{
    std::cout << "[INFO] Starting TLS-enabled DNP3 Outstation..." << std::endl;

    const uint32_t FILTERS = levels::NORMAL | levels::ALL_COMMS;
    DNP3Manager manager(1, ConsoleLogger::Create());

    TLSConfig tlsConfig(
        "server-cert.pem",  // Server certificate
        "server-key.pem",   // Server private key
        "ca-cert.pem",      // Trusted CA cert
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

    OutstationStackConfig config(DatabaseSizes::AllTypes(10));
    config.link.LocalAddr = 10;
    config.link.RemoteAddr = 1;
    config.outstation.eventBufferConfig = EventBufferConfig::AllTypes(10);
    config.outstation.params.allowUnsolicited = true;

    auto outstation = channel->AddOutstation(
        "outstation",
        SuccessCommandHandler::Create(),
        DefaultOutstationApplication::Create(),
        config
    );

    outstation->Enable();

    std::thread sensor_thread(start_python_data_server, outstation);
    sensor_thread.join();

    return 0;
}
