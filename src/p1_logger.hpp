#pragma once

#include <chrono>
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

struct DailyEnergy
{
    std::array<double, 3> consumption_kwh{};
    std::array<double, 3> injection_kwh{};
    std::array<std::optional<double>, 2> tariff_consumption_kwh{};
    std::array<std::optional<double>, 2> tariff_injection_kwh{};
    std::optional<double> gas_meter_m3;
};

class P1Logger
{
public:
    /** Maakt een logger aan die dagelijkse CSV-bestanden in de logmap schrijft.
     * @param serial_device Pad naar de RS232/seriële poort.
     * @param log_directory Map waarin dagelijkse CSV-bestanden worden opgeslagen.
     *                      Een lege map gebruikt de map van de executable.
     * @param baud_rate Baudrate van de seriële poort.
     */
    P1Logger(std::string serial_device, std::string log_directory = {}, int baud_rate = 115200);

    /** Sluit de seriële poort wanneer de logger wordt vernietigd. */
    ~P1Logger();

    /** Verbiedt kopiëren om het eigendom van de seriële verbinding uniek te houden.
     * @param other Logger waarvan kopiëren wordt voorkomen.
     */
    P1Logger(const P1Logger&) = delete;

    /** Verbiedt toewijzing om het eigendom van de seriële verbinding uniek te houden.
     * @param other Andere logger waarvan toewijzing wordt voorkomen.
     */
    P1Logger& operator=(const P1Logger&) = delete;

    /**
     * Leest beschikbare bytes, verwerkt complete telegrammen en keert direct terug.
     * Deze functie blokkeert niet wanneer er geen seriële data beschikbaar is.
     */
    void run();

    /** Geeft de laatst succesvol CRC-gevalideerde vermogenswaarden terug.
     * @return Vermogens-, stroom- en spanningswaarden per fase.
     */
    const std::array<PhasePower, 3>& latest_phase_power() const;

    /** Geeft de geïntegreerde afname en injectie van de huidige lokale dag terug.
     * @return Dagtotalen en de laatst bekende T1/T2-meterstanden.
     */
    const DailyEnergy& daily_energy() const;

private:
    /** Opent en configureert de seriële poort als non-blocking 8N1-verbinding.
        * Bij een fout wordt een uitzondering gegooid.
     */
    void open_serial();

    /** Opent het datumgebonden CSV-bestand als de lokale datum is gewijzigd.
        * Bij een fout wordt een uitzondering gegooid.
     */
    void open_daily_file();

    /** Herstelt dagtotalen en meterstanden uit het dagbestand en telegramarchief.
     * @param csv_path Pad naar het datumgebonden meet-CSV-bestand.
     */
    void load_daily_energy(const std::filesystem::path& csv_path);

    /** Schrijft ontbrekende samenvattingsregels voor afgesloten dagbestanden.
     * @param current_date Huidige datum als YYYY-MM-DD; deze dag blijft open.
     */
    void append_missing_daily_energy_summaries(const std::string& current_date);

    /** Integreert één meting met de vorige meting voor de dagtotalen.
     * @param timestamp Tijdstempel van de meting in de vorm YYYY-MM-DDTHH:MM:SS.
     * @param phase_power Vermogenswaarden van de drie fasen voor deze meting.
     */
    void accumulate_energy_sample(const std::string& timestamp,
                                  const std::array<PhasePower, 3>& phase_power);

    /** Schrijft hoogstens één telegram per lokale minuut naar het telegramlog.
     * @param telegram Volledig, CRC-gevalideerd P1-telegram.
     */
    void write_telegram_if_due(const std::string& telegram);

    /** Leest alle momenteel beschikbare bytes naar de interne invoerbuffer.
        * Bij een leesfout wordt een uitzondering gegooid.
     */
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
    DailyEnergy daily_energy_;
    std::optional<std::chrono::system_clock::time_point> previous_energy_timestamp_;
    std::array<std::optional<double>, 3> previous_consumption_kw_;
    std::array<std::optional<double>, 3> previous_injection_kw_;
};