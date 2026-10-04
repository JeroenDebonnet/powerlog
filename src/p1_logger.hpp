#pragma once

#include <array>
#include <fstream>
#include <filesystem>
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
    * Maakt een logger aan die dagelijkse CSV-bestanden in de logmap schrijft.
     * @param serial_device Pad naar de RS232/seriële poort.
    * @param log_directory Map waarin dagelijkse CSV-bestanden worden opgeslagen.
    *                       Een lege map gebruikt de map van de executable.
     * @param baud_rate Baudrate van de seriële poort.
     */
    P1Logger(std::string serial_device, std::string log_directory = {}, int baud_rate = 115200);

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

    /** Opent het datumgebonden CSV-bestand als de lokale datum is gewijzigd. */
    void open_daily_file();

    /** Schrijft hoogstens één telegram per minuut naar het telegramlog. */
    void write_telegram_if_due(const std::string& telegram);

    /** Leest alle momenteel beschikbare bytes naar de interne invoerbuffer. */
    void read_available_bytes();

    /** Parseert een geldig telegram en schrijft meetwaarden en periodiek het telegram weg.
     * @param telegram Volledig P1-telegram inclusief CRC en CRLF.
     */
    void write_csv_row(const std::string& telegram);

    std::string serial_device_;
    std::filesystem::path log_directory_;
    std::ofstream csv_file_;
    std::ofstream telegram_file_;
    std::string current_log_date_;
    std::string last_telegram_minute_;
    int baud_rate_;
    int serial_fd_ = -1;
    std::string input_buffer_;
    std::array<PhasePower, 3> latest_phase_power_;
};