#include <opendnp3/outstation/IOutstationApplication.h>
#include <opendnp3/outstation/OutstationStackConfig.h>
#include <opendnp3/outstation/UpdateBuilder.h>
#include <opendnp3/outstation/OutstationConfig.h>
#include <opendnp3/outstation/DatabaseConfig.h>

#include <asiodnp3/DefaultOutstationApplication.h>
#include <asiodnp3/UpdateHandler.h>
#include <asiodnp3/OutstationStack.h>
#include <asiodnp3/DNP3Manager.h>

#include <boost/asio.hpp>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>

using namespace opendnp3;
using namespace asiodnp3;
using boost::asio::ip::tcp;

void start_sensor_listener(IOutstation& outstation) {
    std::thread([](IOutstation& outstation) {
        try {
            boost::asio::io_context io_context;
            tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v4(), 20000));
            std::cout << "[INFO] Listening for sensor data on TCP port 20000...\n";

            while (true) {
                tcp::socket socket(io_context);
                acceptor.accept(socket);
                std::cout << "[INFO] Sensor connected.\n";

                char data[1024];
                size_t length = socket.read_some(boost::asio::buffer(data));
                data[length] = '\0';
                std::string message(data);

                std::cout << "[RECEIVED] " << message << "\n";

                // Parse "temp,pressure,humidity"
                float temp, pressure, humidity;
                if (sscanf(message.c_str(), "%f,%f,%f", &temp, &pressure, &humidity) == 3) {
                    UpdateBuilder builder;
                    builder.Update(Binary(true, Timestamp::Min()), 0); // Dummy binary status
                    builder.Update(Analog(temp), 1);
                    builder.Update(Analog(pressure), 2);
                    builder.Update(Analog(humidity), 3);
                    outstation.Apply(builder.Build());
                    std::cout << "[INFO] Data applied to DNP3 database.\n";
                }

                socket.close();
            }
        } catch (const std::exception& e) {
            std::cerr << "[ERROR] Sensor listener: " << e.what() << "\n";
        }
    }, std::ref(outstation)).detach();
}

int main() {
    std::cout << "[INFO] Starting DNP3 Outstation...\n";

    DNP3Manager manager(1, [](const std::string& msg) { std::cout << msg << "\n"; });

    auto channel = manager.AddTCPServer("server", levels::NORMAL, ServerAcceptMode::CloseOld,
                                        IPEndpoint("0.0.0.0", 20001), nullptr);

    OutstationStackConfig config(DatabaseConfig(10, 10));
    config.outstation.eventBufferConfig = EventBufferConfig(10);
    config.outstation.params.allowUnsolicited = true;
    config.link.LocalAddr = 10;
    config.link.RemoteAddr = 1;

    auto outstation = channel->AddOutstation("outstation",
                    [](const OutstationConfig&) { return std::make_shared<UpdateHandler>(); },
                    DefaultOutstationApplication::Create(), config);

    outstation->Enable();

    // Start listening for data from the Python script
    start_sensor_listener(*outstation);

    // Keep main thread alive
    std::this_thread::sleep_for(std::chrono::hours(24));
    return 0;
}
