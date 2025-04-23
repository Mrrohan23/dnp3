#include <asiodnp3/DNP3Manager.h>
#include <asiodnp3/PrintingSOEHandler.h>
#include <asiodnp3/PrintingChannelListener.h>
#include <asiodnp3/ConsoleLogger.h>
#include <asiodnp3/UpdateBuilder.h>
#include <asiopal/UTCTimeSource.h>
#include <opendnp3/outstation/SimpleCommandHandler.h>
#include <opendnp3/outstation/IUpdateHandler.h>
#include <opendnp3/LogLevels.h>
#include <boost/asio.hpp>
#include <iostream>
#include <sstream>
#include <thread>
#include <string>

using namespace std;
using namespace opendnp3;
using namespace openpal;
using namespace asiopal;
using namespace asiodnp3;
using boost::asio::ip::tcp;

struct State {
    double value = 0;
};

void ConfigureDatabase(DatabaseConfig& config)
{
    // Only 1 analog input: temperature
    config.analog[0].clazz = PointClass::Class2;
    config.analog[0].svariation = StaticAnalogVariation::Group30Var5;
    config.analog[0].evariation = EventAnalogVariation::Group32Var7;
}

void AddUpdates(UpdateBuilder& builder, State& state, const std::string& arguments)
{
    for (const char& c : arguments)
    {
        if (c == 'a') {
            builder.Update(Analog(state.value), 0);
            state.value += 1;
        }
    }
}

void ReceiveSensorData(std::shared_ptr<IOutstation> outstation)
{
    try {
        boost::asio::io_context io_context;
        tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v4(), 15000));
        std::cout << "[INFO] Listening for sensor data on port 15000..." << std::endl;

        while (true)
        {
            tcp::socket socket(io_context);
            acceptor.accept(socket);
            std::cout << "[INFO] Sensor connected." << std::endl;

            char buffer[1024];
            size_t length = socket.read_some(boost::asio::buffer(buffer));
            buffer[length] = '\0';
            std::string data(buffer);
            std::cout << "[DATA RECEIVED] " << data << std::endl;

            try {
                float temperature = std::stof(data);
                UpdateBuilder builder;
                builder.Update(Analog(temperature), 0);
                outstation->Apply(builder.Build());
                std::cout << "[INFO] Sent to outstation: T=" << temperature << std::endl;
            }
            catch (const std::exception& e) {
                std::cerr << "[ERROR] Invalid temperature data: " << data << " - " << e.what() << std::endl;
            }

            socket.close();
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "[ERROR] Exception in ReceiveSensorData: " << e.what() << std::endl;
    }
}

void HandleUserInput(std::shared_ptr<IOutstation> outstation)
{
    string input;
    State state;
    while (true)
    {
        std::cout << "Enter 'a' to send analog update, 'quit' to exit." << std::endl;
        std::cin >> input;
        if (input == "quit") exit(0);
        UpdateBuilder builder;
        AddUpdates(builder, state, input);
        outstation->Apply(builder.Build());
    }
}

int main(int argc, char* argv[])
{
    const uint32_t FILTERS = levels::NORMAL | levels::ALL_COMMS;
    DNP3Manager manager(1, ConsoleLogger::Create());

    auto channel = manager.AddTCPServer(
        "server",
        FILTERS,
        ChannelRetry::Default(),
        "0.0.0.0",
        20000,
        PrintingChannelListener::Create()
    );

    DatabaseSizes dbSizes;
    dbSizes.numAnalog = 1;
    dbSizes.numBinary = 0;
    OutstationStackConfig config(DatabaseConfig(dbSizes));

    config.outstation.eventBufferConfig = EventBufferConfig::AllTypes(10);
    config.outstation.params.allowUnsolicited = true;
    config.link.LocalAddr = 10;
    config.link.RemoteAddr = 1;
    config.link.KeepAliveTimeout = openpal::TimeDuration::Max();

    ConfigureDatabase(config.dbConfig);

    auto outstation = channel->AddOutstation(
        "outstation",
        SuccessCommandHandler::Create(),
        DefaultOutstationApplication::Create(),
        config
    );

    outstation->Enable();

    std::thread sensorThread(ReceiveSensorData, outstation);
    std::thread inputThread(HandleUserInput, outstation);

    sensorThread.join();
    inputThread.join();

    return 0;
}
