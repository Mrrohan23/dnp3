#include <iostream>
#include <string>
#include <thread>
#include <boost/asio.hpp>

#include <asiodnp3/DNP3Manager.h>
#include <asiodnp3/PrintingChannelListener.h>
#include <asiodnp3/UpdateBuilder.h>
#include <opendnp3/outstation/SimpleCommandHandler.h>
#include <opendnp3/outstation/IOutstationApplication.h>
#include <openpal/executor/UTCTimestamp.h>

using namespace std;
using namespace boost::asio;
using namespace asiodnp3;
using namespace opendnp3;

// Minimal custom application class
class BasicOutstationApp : public IOutstationApplication {
public:
    bool SupportsWriteTime() const override { return false; }
    bool WriteAbsoluteTime(const openpal::UTCTimestamp&) override { return false; }
    bool SupportsAssignClass() const override { return false; }
    RestartMode ColdRestartSupport() const override { return RestartMode::UNSUPPORTED; }
    RestartMode WarmRestartSupport() const override { return RestartMode::UNSUPPORTED; }
    uint16_t ColdRestart() override { return 0; }
    uint16_t WarmRestart() override { return 0; }
};

// Process the sensor data and update DNP3 outstation
void handle_sensor_data(const std::string& data, std::shared_ptr<IOutstation> outstation) {
    float temp, pressure, humidity;
    if (sscanf(data.c_str(), "%f,%f,%f", &temp, &pressure, &humidity) == 3) {
        UpdateBuilder builder;
        builder.Update(Analog(temp), 0);
        builder.Update(Analog(pressure), 1);
        builder.Update(Analog(humidity), 2);
        outstation->Apply(builder.Build());
        std::cout << "[UPDATED] T=" << temp << ", P=" << pressure << ", H=" << humidity << std::endl;
    } else {
        std::cerr << "[ERROR] Invalid sensor format: " << data << std::endl;
    }
}

// TCP listener to receive data from Python
void start_python_server(std::shared_ptr<IOutstation> outstation) {
    try {
        io_context io;
        ip::tcp::acceptor acceptor(io, ip::tcp::endpoint(ip::tcp::v4(), 15000));
        std::cout << "[INFO] Waiting for Python data on port 15000..." << std::endl;

        while (true) {
            ip::tcp::socket socket(io);
            acceptor.accept(socket);

            char data[1024] = {0};
            size_t len = socket.read_some(buffer(data));
            data[len] = '\0';

            std::string received(data);
            std::cout << "[RECEIVED] " << received << std::endl;
            handle_sensor_data(received, outstation);

            socket.close();
        }
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] TCP Server: " << e.what() << std::endl;
    }
}

int main() {
    std::cout << "[START] DNP3 Outstation" << std::endl;

    DNP3Manager manager(1);

    auto channel = manager.AddTCPServer(
        "server",
        0,  // No log filters
        ChannelRetry::Default(),
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
        std::make_shared<BasicOutstationApp>(),
        config
    );

    outstation->Enable();

    std::thread listener(start_python_server, outstation);
    listener.join();

    return 0;
}
