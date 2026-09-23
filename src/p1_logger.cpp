#include "p1_logger.hpp"

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <iomanip>
#include <array>
#include <sstream>
#include <stdexcept>
#include <optional>
#include <termios.h>
#include <unistd.h>

namespace
{
/** Zet een numerieke baudrate om naar de bijbehorende termios-constante.
 * @param baud_rate Baudrate, momenteel 9600 of 115200.
 * @return De termios-waarde voor de baudrate.
 */
speed_t to_termios_baud(int baud_rate)
{
    switch (baud_rate)
    {
    case 9600: return B9600;
    case 115200: return B115200;
    default: throw std::invalid_argument("baudrate moet 9600 of 115200 zijn");
    }
}

/** Maakt tekst geschikt voor gebruik als één CSV-veld.
 * @param value Tekst die in het CSV-bestand moet komen.
 * @return De gequote en ge-escape-te tekst.
 */
std::string csv_escape(const std::string& value)
{
    std::string escaped = "\"";
    for (const char character : value)
    {
        if (character == '"')
            escaped += "\"\"";
        else if (character != '\r' && character != '\n')
            escaped += character;
    }
    escaped += '"';
    return escaped;
}

/** Geeft de huidige lokale tijd terug in ISO-achtige notatie.
 * @return Tijdstip in de vorm YYYY-MM-DDTHH:MM:SS.
 */
std::string timestamp_now()
{
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm local_time{};
    localtime_r(&time, &local_time);

    std::ostringstream result;
    result << std::put_time(&local_time, "%Y-%m-%dT%H:%M:%S");
    return result.str();
}

/** Berekent de CRC16 die door DSMR/P1-telegrammen wordt gebruikt.
 * @param data Bytes waarover de CRC moet worden berekend.
 * @return De berekende CRC16-waarde.
 */
std::uint16_t calculate_crc16(const std::string& data)
{
    std::uint16_t crc = 0;
    for (const unsigned char byte : data)
    {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 1) != 0 ? (crc >> 1) ^ 0xA001 : crc >> 1;
    }
    return crc;
}

/** Zet één hexadecimaal teken om naar zijn numerieke waarde.
 * @param character Hexadecimaal teken, bijvoorbeeld 'A' of '7'.
 * @return Waarde 0-15, of -1 als het teken geen hex-teken is.
 */
int hex_value(char character)
{
    if (character >= '0' && character <= '9')
        return character - '0';
    if (character >= 'A' && character <= 'F')
        return character - 'A' + 10;
    if (character >= 'a' && character <= 'f')
        return character - 'a' + 10;
    return -1;
}

/** Controleert het officiële !CCCC\r\n-einde en de CRC van een telegram.
 * @param telegram Volledig telegram inclusief startteken, CRC en CRLF.
 * @param exclamation_mark Positie van het '!' teken in het telegram.
 * @return true als suffix en CRC geldig zijn.
 */
bool has_valid_crc(const std::string& telegram, std::size_t exclamation_mark)
{
    if (exclamation_mark + 7 != telegram.size() || telegram[exclamation_mark + 5] != '\r' ||
        telegram[exclamation_mark + 6] != '\n')
        return false;

    std::uint16_t received_crc = 0;
    for (std::size_t index = exclamation_mark + 1; index <= exclamation_mark + 4; ++index)
    {
        const int value = hex_value(telegram[index]);
        if (value < 0)
            return false;
        received_crc = static_cast<std::uint16_t>((received_crc << 4) | value);
    }

    return received_crc == calculate_crc16(telegram.substr(0, exclamation_mark + 1));
}

struct Measurement
{
    std::optional<double> value;
    std::string unit;
};

/** Zoekt een OBIS-meetwaarde en de bijbehorende eenheid in een telegram.
 * @param telegram Telegram waarin gezocht wordt.
 * @param obis_code OBIS-code zonder het voorvoegsel '1-0:'.
 * @return De gevonden waarde en eenheid, of een lege waarde als die ontbreekt.
 */
Measurement find_measurement(const std::string& telegram, const std::string& obis_code)
{
    std::istringstream lines(telegram);
    std::string line;

    while (std::getline(lines, line))
    {
        const std::size_t code_position = line.find(obis_code);
        if (code_position == std::string::npos)
            continue;

        const std::size_t opening_parenthesis = line.find('(', code_position + obis_code.size());
        const std::size_t unit_separator = line.find('*', opening_parenthesis);
        const std::size_t closing_parenthesis = line.find(')', unit_separator);
        if (opening_parenthesis == std::string::npos ||
            unit_separator == std::string::npos ||
            closing_parenthesis == std::string::npos)
            continue;

        try
        {
            return {
                std::stod(line.substr(opening_parenthesis + 1,
                                      unit_separator - opening_parenthesis - 1)),
                line.substr(unit_separator + 1,
                            closing_parenthesis - unit_separator - 1)};
        }
        catch (const std::exception&)
        {
            return {};
        }
    }

    return {};
}

/** Schrijft een optionele meetwaarde naar een uitvoerstroom.
 * @param output Uitvoerstroom waarin de waarde wordt geschreven.
 * @param measurement Meetwaarde die eventueel aanwezig is.
 */
void write_measurement(std::ostream& output, const Measurement& measurement)
{
    if (measurement.value)
        output << *measurement.value;
}
}

/** Maakt een P1Logger aan voor een seriële poort en CSV-bestand.
 * @param serial_device Pad naar de RS232/seriële poort.
 * @param csv_file Pad naar het CSV-bestand.
 * @param baud_rate Baudrate van de seriële poort.
 */
P1Logger::P1Logger(std::string serial_device, std::string csv_file, int baud_rate)
    : serial_device_(std::move(serial_device)), csv_file_(std::move(csv_file)), baud_rate_(baud_rate)
{
    if (!csv_file_)
        throw std::runtime_error("kan CSV-bestand niet openen");

    csv_file_.seekp(0, std::ios::end);
    if (csv_file_.tellp() == 0)
    {
        csv_file_ << "timestamp,telegram,"
                  << "l1_consumption_kw,l1_injection_kw,l1_current_a,l1_voltage_v,"
                  << "l2_consumption_kw,l2_injection_kw,l2_current_a,l2_voltage_v,"
                  << "l3_consumption_kw,l3_injection_kw,l3_current_a,l3_voltage_v\n";
    }
}

/** Sluit de seriële filedescriptor wanneer die geopend is. */
P1Logger::~P1Logger()
{
    if (serial_fd_ >= 0)
        close(serial_fd_);
}

const std::array<PhasePower, 3>& P1Logger::latest_phase_power() const
{
    return latest_phase_power_;
}

/** Opent de seriële poort en stelt non-blocking 8N1 in.
 * Deze functie heeft geen parameters; de configuratie komt uit de constructor.
 */
void P1Logger::open_serial()
{
    serial_fd_ = open(serial_device_.c_str(), O_RDONLY | O_NOCTTY | O_NONBLOCK);
    if (serial_fd_ < 0)
        throw std::runtime_error("kan seriele poort niet openen: " + std::string(std::strerror(errno)));

    termios settings{};
    if (tcgetattr(serial_fd_, &settings) < 0)
        throw std::runtime_error("kan seriële instellingen niet lezen");

    cfmakeraw(&settings);
    const speed_t baud = to_termios_baud(baud_rate_);
    cfsetispeed(&settings, baud);
    cfsetospeed(&settings, baud);
    settings.c_cflag |= CLOCAL | CREAD;
    settings.c_cflag &= ~CSTOPB;
    settings.c_cflag &= ~CRTSCTS;
    settings.c_cflag &= ~PARENB;
    settings.c_cflag &= ~CSIZE;
    settings.c_cflag |= CS8;
    settings.c_cc[VMIN] = 0;
    settings.c_cc[VTIME] = 0;

    if (tcsetattr(serial_fd_, TCSANOW, &settings) < 0)
        throw std::runtime_error("kan seriële instellingen niet toepassen");
}

/** Leest alle bytes die zonder wachten uit de seriële poort kunnen worden gelezen.
 * De bytes worden toegevoegd aan input_buffer_.
 */
void P1Logger::read_available_bytes()
{
    char buffer[256];

    while (true)
    {
        const ssize_t bytes_read = read(serial_fd_, buffer, sizeof(buffer));
        if (bytes_read < 0)
        {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return;
            throw std::runtime_error("fout bij lezen van seriële poort");
        }
        if (bytes_read == 0)
            return;

        input_buffer_.append(buffer, static_cast<std::size_t>(bytes_read));
    }
}

/** Parseert één compleet telegram en schrijft één CSV-regel.
 * @param telegram Compleet, CRC-gevalideerd P1-telegram.
 */
void P1Logger::write_csv_row(const std::string& telegram)
{
    constexpr std::array phase_codes = {
        std::array{"21.7.0", "22.7.0", "31.7.0", "32.7.0"},
        std::array{"41.7.0", "42.7.0", "51.7.0", "52.7.0"},
        std::array{"61.7.0", "62.7.0", "71.7.0", "72.7.0"}};

    csv_file_ << timestamp_now() << ',' << csv_escape(telegram);
    for (const auto& phase : phase_codes)
    {
        for (const auto& code : phase)
        {
            csv_file_ << ',';
            write_measurement(csv_file_, find_measurement(telegram, code));
        }
    }

    for (std::size_t phase = 0; phase < latest_phase_power_.size(); ++phase)
    {
        latest_phase_power_[phase].consumption_kw =
            find_measurement(telegram, phase_codes[phase][0]).value;
        latest_phase_power_[phase].injection_kw =
            find_measurement(telegram, phase_codes[phase][1]).value;
        latest_phase_power_[phase].current_a =
            find_measurement(telegram, phase_codes[phase][2]).value;
        latest_phase_power_[phase].voltage_v =
            find_measurement(telegram, phase_codes[phase][3]).value;
    }

    csv_file_ << '\n';
    csv_file_.flush();
    if (!csv_file_)
        throw std::runtime_error("fout bij schrijven naar CSV-bestand");
}

/** Verwerkt beschikbare seriële data zonder op nieuwe bytes te wachten.
 * Complete telegrammen worden gevalideerd en gelogd; restdata blijft gebufferd.
 */
void P1Logger::run()
{
    if (serial_fd_ < 0)
        open_serial();

    read_available_bytes();

    while (true)
    {
        const std::size_t start = input_buffer_.find('/');
        if (start == std::string::npos)
        {
            input_buffer_.clear();
            return;
        }

        if (start > 0)
            input_buffer_.erase(0, start);

        const std::size_t exclamation_mark = input_buffer_.find('!', 1);
        if (exclamation_mark == std::string::npos)
            return;

        constexpr std::size_t telegram_suffix_size = 7; // ! + vier CRC-tekens + CRLF
        if (input_buffer_.size() < exclamation_mark + telegram_suffix_size)
            return;

        const std::string telegram = input_buffer_.substr(0, exclamation_mark + telegram_suffix_size);
        input_buffer_.erase(0, exclamation_mark + telegram_suffix_size);

        if (has_valid_crc(telegram, exclamation_mark))
            write_csv_row(telegram);
    }
}