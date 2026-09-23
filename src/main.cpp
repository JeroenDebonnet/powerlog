#include "p1_logger.hpp"
#include "freenove_lcd.hpp"
#include "terminal_display.hpp"

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <thread>

namespace
{
volatile std::sig_atomic_t keep_running = 1;

/** Markeert dat de hoofdloop na de huidige iteratie moet stoppen.
 * @param signal_number Ontvangen POSIX-signaal; de waarde wordt niet gebruikt.
 */
void handle_signal(int signal_number)
{
    static_cast<void>(signal_number);
    keep_running = 0;
}
}

/** Start de P1-logger met seriële poort, CSV-bestand en optionele baudrate.
 * @param argc Aantal command-lineargumenten.
 * @param argv Command-lineargumenten: poort, CSV-bestand en optioneel baudrate.
 * @return EXIT_SUCCESS bij normaal einde, anders EXIT_FAILURE.
 */
int main(int argc, char* argv[])
{
    if (argc < 3 || argc > 6)
    {
        std::cerr << "gebruik: " << argv[0]
                  << " <seriele-poort> <csv-bestand> [baudrate] [i2c-device] [i2c-adres]\n"
                  << "voorbeeld: " << argv[0]
                  << " /dev/ttyUSB0 meter.csv 115200 /dev/i2c-1 0x27\n";
        return EXIT_FAILURE;
    }

    try
    {
        const int baud_rate = argc == 4 ? std::stoi(argv[3]) : 115200;
        P1Logger logger(argv[1], argv[2], baud_rate);
        const std::string i2c_device = argc >= 5 ? argv[4] : "/dev/i2c-1";
        const auto i2c_address = argc == 6
                                     ? static_cast<std::uint8_t>(std::stoul(argv[5], nullptr, 0))
                                     : static_cast<std::uint8_t>(0x27);
        std::unique_ptr<FreeNoveLcd> lcd;
        try
        {
            lcd = std::make_unique<FreeNoveLcd>(i2c_device, i2c_address);
        }
        catch (const std::exception& error)
        {
            std::cerr << "waarschuwing: LCD uitgeschakeld: " << error.what() << '\n';
        }

        TerminalDisplay terminal;
        std::signal(SIGINT, handle_signal);
        std::signal(SIGTERM, handle_signal);
        auto next_display_update = std::chrono::steady_clock::now();
        while (keep_running != 0)
        {
            logger.run();
            const auto now = std::chrono::steady_clock::now();
            if (now >= next_display_update)
            {
                if (lcd)
                    lcd->update(logger.latest_phase_power());
                if (!terminal.update(logger.latest_phase_power()))
                    keep_running = 0;
                next_display_update = now + std::chrono::seconds(1);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    catch (const std::exception& error)
    {
        std::cerr << "fout: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
