#include "p1_logger.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fcntl.h>
#include <iomanip>
#include <iostream>
#include <spawn.h>
#include <sstream>
#include <stdexcept>
#include <optional>
#include <set>
#include <string_view>
#include <thread>
#include <termios.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

extern char** environ;

namespace
{
bool is_daily_csv(const std::filesystem::path& path)
{
    if (path.extension() != ".csv")
        return false;

    const std::string date = path.stem().string();
    if (date.size() != 10 || date[4] != '-' || date[7] != '-')
        return false;

    return std::all_of(date.begin(), date.end(), [](unsigned char character)
    {
        return character == '-' || std::isdigit(character) != 0;
    });
}

std::optional<std::chrono::system_clock::time_point> parse_timestamp(const std::string& value)
{
    std::tm local_time{};
    std::istringstream input(value);
    input >> std::get_time(&local_time, "%Y-%m-%dT%H:%M:%S");
    if (input.fail())
        return std::nullopt;

    local_time.tm_isdst = -1;
    const std::time_t time = std::mktime(&local_time);
    if (time == static_cast<std::time_t>(-1))
        return std::nullopt;
    return std::chrono::system_clock::from_time_t(time);
}

std::optional<double> parse_csv_number(const std::string& value)
{
    try
    {
        std::size_t parsed_characters = 0;
        const double number = std::stod(value, &parsed_characters);
        if (parsed_characters != value.size() || !std::isfinite(number))
            return std::nullopt;
        return number;
    }
    catch (const std::exception&)
    {
        return std::nullopt;
    }
}

void launch_plot(const std::filesystem::path& csv_path)
{
    const auto executable_path = std::filesystem::canonical("/proc/self/exe");
    const auto script_path = executable_path.parent_path() / "plot_powerlog.py";
    if (!std::filesystem::exists(script_path))
    {
        std::cerr << "waarschuwing: plotprogramma niet gevonden: " << script_path << '\n';
        return;
    }

    std::string python = "python3";
    std::string script = script_path.string();
    std::string input = csv_path.string();
    char* arguments[] = {python.data(), script.data(), input.data(), nullptr};

    posix_spawn_file_actions_t file_actions;
    int result = posix_spawn_file_actions_init(&file_actions);
    if (result != 0)
    {
        std::cerr << "waarschuwing: plotacties niet initialiseerbaar: "
                  << std::strerror(result) << '\n';
        return;
    }

    result = posix_spawn_file_actions_addopen(
        &file_actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    if (result != 0)
    {
        posix_spawn_file_actions_destroy(&file_actions);
        std::cerr << "waarschuwing: plotuitvoer niet instelbaar: "
                  << std::strerror(result) << '\n';
        return;
    }

    pid_t process_id = -1;
    result = posix_spawnp(
        &process_id, python.c_str(), &file_actions, nullptr, arguments, environ);
    posix_spawn_file_actions_destroy(&file_actions);
    if (result != 0)
    {
        std::cerr << "waarschuwing: plotprogramma starten mislukt: "
                  << std::strerror(result) << '\n';
        return;
    }

    std::thread([process_id, csv_path]
    {
        int status = 0;
        pid_t waited;
        do
        {
            waited = waitpid(process_id, &status, 0);
        } while (waited < 0 && errno == EINTR);

        if (waited < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
            std::cerr << "waarschuwing: PNG renderen mislukt voor " << csv_path << '\n';
    }).detach();
}

void render_pending_daily_csvs(const std::filesystem::path& log_directory,
                               const std::string& current_date)
{
    std::error_code error;
    for (std::filesystem::directory_iterator entry(log_directory, error), end;
         !error && entry != end; entry.increment(error))
    {
        const auto& csv_path = entry->path();
        const std::string date = csv_path.stem().string();
        if (!is_daily_csv(csv_path) || date >= current_date)
            continue;

        auto png_path = csv_path;
        png_path.replace_extension(".png");
        if (!std::filesystem::exists(png_path, error) && !error)
            launch_plot(csv_path);
        error.clear();
    }

    if (error)
        std::cerr << "waarschuwing: logmap niet volledig gescand: " << error.message() << '\n';
}

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

/** Verwijdert het tweede CSV-veld, ook wanneer dit gequote komma's bevat.
 * @param line Een regel uit de bestaande logfile.
 * @return De regel zonder het tweede veld.
 */
std::string remove_second_csv_field(const std::string& line)
{
    const std::size_t first_separator = line.find(',');
    if (first_separator == std::string::npos)
        return line;

    const std::size_t field_start = first_separator + 1;
    std::size_t second_separator = std::string::npos;
    if (field_start < line.size() && line[field_start] == '"')
    {
        for (std::size_t index = field_start + 1; index < line.size(); ++index)
        {
            if (line[index] != '"')
                continue;
            if (index + 1 < line.size() && line[index + 1] == '"')
            {
                ++index;
                continue;
            }
            if (index + 1 < line.size() && line[index + 1] == ',')
                second_separator = index + 1;
            break;
        }
    }
    else
    {
        second_separator = line.find(',', field_start);
    }

    if (second_separator == std::string::npos)
        return line;
    return line.substr(0, first_separator + 1) + line.substr(second_separator + 1);
}

/** Migreert een bestaande logfile die nog een telegramkolom bevat.
 * @param file_path Pad naar de dagelijkse meetwaardenlog.
 */
void migrate_legacy_csv(const std::filesystem::path& file_path)
{
    std::ifstream input(file_path);
    if (!input)
        throw std::runtime_error("kan bestaande CSV niet lezen: " + file_path.string());

    std::string header;
    if (!std::getline(input, header) || header.rfind("timestamp,telegram,", 0) != 0)
        return;

    const auto temporary_path = file_path.string() + ".tmp." + std::to_string(getpid());
    std::ofstream output(temporary_path, std::ios::out | std::ios::trunc);
    if (!output)
        throw std::runtime_error("kan tijdelijke CSV niet openen: " + temporary_path);

    output << remove_second_csv_field(header) << '\n';
    std::string line;
    while (std::getline(input, line))
        output << remove_second_csv_field(line) << '\n';

    output.flush();
    if (!input.eof() || !output)
    {
        output.close();
        std::filesystem::remove(temporary_path);
        throw std::runtime_error("fout bij migreren van CSV-bestand: " + file_path.string());
    }

    input.close();
    output.close();
    std::filesystem::rename(temporary_path, file_path);
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

/** Geeft de huidige lokale datum terug als bestandsnaamcomponent.
 * @return Datum in de vorm YYYY-MM-DD.
 */
std::string date_now()
{
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm local_time{};
    localtime_r(&time, &local_time);

    std::ostringstream result;
    result << std::put_time(&local_time, "%Y-%m-%d");
    return result.str();
}

/** Geeft de huidige lokale minuut terug als unieke log-sleutel.
 * @return Tijdstip in de vorm YYYY-MM-DDTHH:MM met tijdzone.
 */
std::string minute_now()
{
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm local_time{};
    localtime_r(&time, &local_time);

    std::ostringstream result;
    result << std::put_time(&local_time, "%Y-%m-%dT%H:%M%z");
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

/** Maakt een P1Logger aan voor een seriële poort en dagelijkse CSV-bestanden.
 * @param serial_device Pad naar de RS232/seriële poort.
 * @param log_directory Map voor de dagelijkse CSV-bestanden; leeg gebruikt de executable-map.
 * @param baud_rate Baudrate van de seriële poort.
 */
P1Logger::P1Logger(std::string serial_device, std::string log_directory, int baud_rate)
    : serial_device_(std::move(serial_device)),
      log_directory_(std::move(log_directory)),
      baud_rate_(baud_rate)
{
    if (log_directory_.empty())
        log_directory_ = std::filesystem::canonical("/proc/self/exe").parent_path();

    std::filesystem::create_directories(log_directory_);
}

/** Opent het CSV-bestand voor de huidige lokale datum wanneer dat nodig is. */
void P1Logger::open_daily_file()
{
    const std::string date = date_now();
    if (date == current_log_date_)
        return;

    csv_file_.close();
    csv_file_.clear();
    telegram_file_.close();
    telegram_file_.clear();
    const auto file_path = log_directory_ / (date + ".csv");
    const auto telegram_path = log_directory_ / (date + "-TELEGRAM.log");
    if (std::filesystem::exists(file_path))
        migrate_legacy_csv(file_path);

    csv_file_.open(file_path, std::ios::out | std::ios::app);
    if (!csv_file_)
        throw std::runtime_error("kan CSV-bestand niet openen: " + file_path.string());
    telegram_file_.open(telegram_path, std::ios::out | std::ios::app);
    if (!telegram_file_)
        throw std::runtime_error("kan telegramlog niet openen: " + telegram_path.string());

    current_log_date_ = date;
    csv_file_.seekp(0, std::ios::end);
    if (csv_file_.tellp() == 0)
    {
        csv_file_ << "timestamp,"
                  << "l1_consumption_kw,l1_injection_kw,l1_current_a,l1_voltage_v,"
                  << "l2_consumption_kw,l2_injection_kw,l2_current_a,l2_voltage_v,"
                  << "l3_consumption_kw,l3_injection_kw,l3_current_a,l3_voltage_v\n";
        csv_file_.flush();
    }

    append_missing_daily_energy_summaries(date);
    load_daily_energy(file_path);

    last_telegram_minute_.clear();
    std::ifstream existing_telegram_log(telegram_path);
    std::string line;
    constexpr std::string_view minute_prefix = "# minute=";
    while (std::getline(existing_telegram_log, line))
    {
        if (line.rfind(minute_prefix, 0) == 0)
            last_telegram_minute_ = line.substr(minute_prefix.size());
    }

    render_pending_daily_csvs(log_directory_, date);
}

/** Schrijft alleen het eerste geldige telegram van iedere lokale minuut weg. */
void P1Logger::write_telegram_if_due(const std::string& telegram)
{
    const std::string minute = minute_now();
    if (minute == last_telegram_minute_)
        return;

    telegram_file_ << "# minute=" << minute << '\n' << telegram;
    telegram_file_.flush();
    if (!telegram_file_)
        throw std::runtime_error("fout bij schrijven naar telegramlog");
    last_telegram_minute_ = minute;
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

const DailyEnergy& P1Logger::daily_energy() const
{
    return daily_energy_;
}

void P1Logger::load_daily_energy(const std::filesystem::path& csv_path)
{
    daily_energy_ = {};
    previous_energy_timestamp_.reset();
    previous_consumption_kw_ = {};
    previous_injection_kw_ = {};

    std::ifstream csv_file(csv_path);
    std::string line;
    if (!std::getline(csv_file, line))
        return;

    constexpr std::array<std::size_t, 3> consumption_columns = {1, 5, 9};
    constexpr std::array<std::size_t, 3> injection_columns = {2, 6, 10};
    while (std::getline(csv_file, line))
    {
        std::array<std::string, 13> fields;
        std::istringstream row(line);
        std::size_t field_count = 0;
        while (field_count < fields.size() && std::getline(row, fields[field_count], ','))
            ++field_count;
        if (field_count != fields.size())
            continue;

        std::array<PhasePower, 3> phase_power;
        for (std::size_t phase = 0; phase < phase_power.size(); ++phase)
        {
            phase_power[phase].consumption_kw = parse_csv_number(fields[consumption_columns[phase]]);
            phase_power[phase].injection_kw = parse_csv_number(fields[injection_columns[phase]]);
        }
        accumulate_energy_sample(fields[0], phase_power);
    }
}

void P1Logger::append_missing_daily_energy_summaries(const std::string& current_date)
{
    const auto summary_path = log_directory_ / "daily_energy.csv";
    std::set<std::string> recorded_dates;
    std::ifstream existing_summary(summary_path);
    if (existing_summary)
    {
        std::string line;
        std::getline(existing_summary, line);
        while (std::getline(existing_summary, line))
        {
            const std::size_t separator = line.find(',');
            if (separator != std::string::npos)
                recorded_dates.insert(line.substr(0, separator));
        }
    }
    else
    {
        std::error_code error;
        if (std::filesystem::exists(summary_path, error) || error)
        {
            std::cerr << "waarschuwing: dagtotalenbestand niet leesbaar: "
                      << summary_path << '\n';
            return;
        }
    }

    std::vector<std::pair<std::string, std::filesystem::path>> daily_files;
    std::error_code error;
    for (std::filesystem::directory_iterator entry(log_directory_, error), end;
         !error && entry != end; entry.increment(error))
    {
        const auto& path = entry->path();
        const std::string date = path.stem().string();
        if (is_daily_csv(path) && date < current_date && !recorded_dates.contains(date))
            daily_files.emplace_back(date, path);
    }
    if (error)
    {
        std::cerr << "waarschuwing: dagbestanden niet volledig gescand: "
                  << error.message() << '\n';
        return;
    }
    std::sort(daily_files.begin(), daily_files.end(),
              [](const auto& left, const auto& right)
              {
                  return left.first < right.first;
              });
    if (daily_files.empty())
        return;

    std::vector<std::pair<std::string, DailyEnergy>> pending_summaries;
    for (const auto& [date, path] : daily_files)
    {
        load_daily_energy(path);
        pending_summaries.emplace_back(date, daily_energy_);
    }

    bool write_header = false;
    const bool summary_exists = std::filesystem::exists(summary_path, error);
    if (error)
    {
        std::cerr << "waarschuwing: dagtotalenbestand niet gecontroleerd: "
                  << error.message() << '\n';
        return;
    }
    if (!summary_exists)
        write_header = true;
    else
    {
        const auto summary_size = std::filesystem::file_size(summary_path, error);
        if (error)
        {
            std::cerr << "waarschuwing: grootte dagtotalenbestand niet gelezen: "
                      << error.message() << '\n';
            return;
        }
        write_header = summary_size == 0;
    }

    std::ofstream summary_file(summary_path, std::ios::out | std::ios::app);
    if (!summary_file)
    {
        std::cerr << "waarschuwing: dagtotalenbestand niet te openen: "
                  << summary_path << '\n';
        return;
    }
    if (write_header)
    {
        summary_file << "date,l1_consumption_kwh,l2_consumption_kwh,l3_consumption_kwh,"
                     << "total_consumption_kwh,l1_injection_kwh,l2_injection_kwh,"
                     << "l3_injection_kwh,total_injection_kwh\n";
    }

    summary_file << std::fixed << std::setprecision(6);
    for (const auto& [date, energy] : pending_summaries)
    {
        const double total_consumption = energy.consumption_kwh[0] +
                                         energy.consumption_kwh[1] +
                                         energy.consumption_kwh[2];
        const double total_injection = energy.injection_kwh[0] +
                                       energy.injection_kwh[1] +
                                       energy.injection_kwh[2];
        summary_file << date << ','
                     << energy.consumption_kwh[0] << ','
                     << energy.consumption_kwh[1] << ','
                     << energy.consumption_kwh[2] << ','
                     << total_consumption << ','
                     << energy.injection_kwh[0] << ','
                     << energy.injection_kwh[1] << ','
                     << energy.injection_kwh[2] << ','
                     << total_injection << '\n';
    }
    summary_file.flush();
    if (!summary_file)
        std::cerr << "waarschuwing: fout bij schrijven van dagtotalenbestand: "
                  << summary_path << '\n';
}

void P1Logger::accumulate_energy_sample(
    const std::string& timestamp, const std::array<PhasePower, 3>& phase_power)
{
    const auto sample_time = parse_timestamp(timestamp);
    if (!sample_time)
        return;

    if (previous_energy_timestamp_)
    {
        const double elapsed_seconds = std::chrono::duration<double>(
            *sample_time - *previous_energy_timestamp_).count();
        if (elapsed_seconds > 0.0 && elapsed_seconds <= 5.0)
        {
            for (std::size_t phase = 0; phase < phase_power.size(); ++phase)
            {
                const auto add_energy = [elapsed_seconds](
                    const std::optional<double>& previous,
                    const std::optional<double>& current,
                    double& total)
                {
                    if (previous && current && *previous >= 0.0 && *current >= 0.0)
                        total += (*previous + *current) * 0.5 * elapsed_seconds / 3600.0;
                };
                add_energy(previous_consumption_kw_[phase], phase_power[phase].consumption_kw,
                           daily_energy_.consumption_kwh[phase]);
                add_energy(previous_injection_kw_[phase], phase_power[phase].injection_kw,
                           daily_energy_.injection_kwh[phase]);
            }
        }
    }

    previous_energy_timestamp_ = sample_time;
    for (std::size_t phase = 0; phase < phase_power.size(); ++phase)
    {
        previous_consumption_kw_[phase] = phase_power[phase].consumption_kw;
        previous_injection_kw_[phase] = phase_power[phase].injection_kw;
    }
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

/** Parseert één compleet telegram en schrijft de meetwaarden naar de CSV.
 * @param telegram Compleet, CRC-gevalideerd P1-telegram.
 */
void P1Logger::write_csv_row(const std::string& telegram)
{
    open_daily_file();

    constexpr std::array phase_codes = {
        std::array{"21.7.0", "22.7.0", "31.7.0", "32.7.0"},
        std::array{"41.7.0", "42.7.0", "51.7.0", "52.7.0"},
        std::array{"61.7.0", "62.7.0", "71.7.0", "72.7.0"}};

    const std::string timestamp = timestamp_now();
    csv_file_ << timestamp;
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

            accumulate_energy_sample(timestamp, latest_phase_power_);

    csv_file_ << '\n';
    csv_file_.flush();
    if (!csv_file_)
        throw std::runtime_error("fout bij schrijven naar CSV-bestand");

    write_telegram_if_due(telegram);
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