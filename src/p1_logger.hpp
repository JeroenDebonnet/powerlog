#pragma once

#include <array>
#include <fstream>
#include <optional>
#include <string>

struct PhasePower
{
    std::optional<double> consumption_kw;
    std::optional<double> injection_kw;
    std::optional<double> voltage_v;
    std::optional<double> current_a;
};

class P1Logger
{
public:
    /**
     * Maakt een logger aan en opent het CSV-bestand.
     * @param serial_device Pad naar de RS232/seriële poort.
     * @param csv_file Pad naar het CSV-bestand waarin telegrammen worden gelogd.
     * @param baud_rate Baudrate van de seriële poort.
     */
    P1Logger(std::string serial_device, std::string csv_file, int baud_rate = 115200);

    /** Sluit de seriële poort wanneer de logger wordt vernietigd. */
    ~P1Logger();

    P1Logger(const P1Logger&) = delete;
    P1Logger& operator=(const P1Logger&) = delete;

    /**
     * Leest beschikbare bytes, verwerkt complete telegrammen en keert direct terug.
     * Deze functie blokkeert niet wanneer er geen seriële data beschikbaar is.
     */
    void run();

    /** Geeft de laatst succesvol CRC-gevalideerde vermogenswaarden terug. */
    const std::array<PhasePower, 3>& latest_phase_power() const;

private:
    /** Opent en configureert de seriële poort als non-blocking 8N1-verbinding. */
    void open_serial();

    /** Leest alle momenteel beschikbare bytes naar de interne invoerbuffer. */
    void read_available_bytes();

    /** Parseert een geldig telegram en schrijft de ruwe en gemeten waarden naar CSV.
     * @param telegram Volledig P1-telegram inclusief CRC en CRLF.
     */
    void write_csv_row(const std::string& telegram);

    std::string serial_device_;
    std::ofstream csv_file_;
    int baud_rate_;
    int serial_fd_ = -1;
    std::string input_buffer_;
    std::array<PhasePower, 3> latest_phase_power_;
};