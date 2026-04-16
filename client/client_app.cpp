#include "client_app.h"

#include <chrono>
#include <deque>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

namespace client {
namespace {

struct InputBuffer {
    std::mutex mutex;
    std::deque<std::string> lines;
};

void inputThreadMain(InputBuffer& inputBuffer) {
    for (std::string line; std::getline(std::cin, line);) {
        std::lock_guard<std::mutex> lock(inputBuffer.mutex);
        inputBuffer.lines.push_back(line);
    }
}

bool tryPopInput(InputBuffer& inputBuffer, std::string& line) {
    std::lock_guard<std::mutex> lock(inputBuffer.mutex);
    if (inputBuffer.lines.empty()) {
        return false;
    }
    line = inputBuffer.lines.front();
    inputBuffer.lines.pop_front();
    return true;
}

std::string normalizeChoice(std::string value) {
    for (char& ch : value) {
        if (ch >= 'a' && ch <= 'z') {
            ch = static_cast<char>(ch - ('a' - 'A'));
        }
    }
    return value;
}

} // namespace

ClientConsoleApp::ClientConsoleApp(const ClientOptions& options) : client(options) {}

int ClientConsoleApp::run() {
    {
        std::cout << "====================================\n";
        std::cout << "  AIRCRAFT CLIENT - " << client.aircraftId() << '\n';
        std::cout << "====================================\n";
        if (!client.aircraftIdExplicit()) {
            std::cout << "  Auto-assigned aircraft ID\n";
        }
    }

    InputBuffer inputBuffer;
    std::thread(inputThreadMain, std::ref(inputBuffer)).detach();

    bool printedDisconnectedMenu = false;
    bool printedConnectedMenu = false;

    while (true) {
        const bool connected = client.isRunning();

        if (!connected) {
            client.closeSocketIfOpen();

            printedConnectedMenu = false;
            if (!printedDisconnectedMenu) {
                client.printStatus();
                client.printLine("[1] Connect to Ground Control  [Q] Quit");
                printedDisconnectedMenu = true;
            }

            std::string choice;
            if (!tryPopInput(inputBuffer, choice)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            choice = normalizeChoice(choice);
            if (choice == "Q") {
                break;
            }
            if (choice == "1") {
                printedDisconnectedMenu = false;
                if (!client.connectSession()) {
                    continue;
                }

                std::thread telemetry(&AircraftClient::telemetryLoop, &client);
                std::thread receiver(&AircraftClient::receiverLoop, &client);

                printedConnectedMenu = false;
                while (client.isRunning()) {
                    if (!printedConnectedMenu) {
                        client.printStatus();
                        client.printLine("[W] Request Weather Map  [D] Disconnect");
                        printedConnectedMenu = true;
                    }

                    std::string command;
                    if (!tryPopInput(inputBuffer, command)) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        continue;
                    }

                    command = normalizeChoice(command);
                    if (command == "W") {
                        if (!client.requestWeatherMap()) {
                            break;
                        }
                        printedConnectedMenu = false;
                    } else if (command == "D") {
                        client.disconnectSession();
                    }
                }

                client.disconnectSession();
                if (telemetry.joinable()) {
                    telemetry.join();
                }
                if (receiver.joinable()) {
                    receiver.join();
                }

                if (client.state() == ClientState::FAULT) {
                    client.printStatus();
                }

                client.resetToDisconnected();
                printedDisconnectedMenu = false;
            }
        }
    }

    return 0;
}

} // namespace client
