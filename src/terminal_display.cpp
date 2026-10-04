#include "terminal_display.hpp"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <ncurses.h>
#include <sstream>
#include <stdexcept>

namespace
{
std::string current_datetime()
{
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm local_time{};
    localtime_r(&time, &local_time);

    std::ostringstream text;
    text << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S");
    return text.str();
}

std::string cpu_temperature()
{
    std::ifstream temperature_file("/sys/class/thermal/thermal_zone0/temp");
    long temperature_millidegrees = 0;
    if (!(temperature_file >> temperature_millidegrees))
        return "--";

    std::ostringstream text;
    text << std::fixed << std::setprecision(1)
         << static_cast<double>(temperature_millidegrees) / 1000.0;
    return text.str();
}
}

TerminalDisplay::TerminalDisplay()
{
    if (initscr() == nullptr)
        throw std::runtime_error("kan ncurses-terminal niet initialiseren");

    cbreak();
    noecho();
    nodelay(stdscr, TRUE);
    curs_set(0);
    keypad(stdscr, TRUE);
}

TerminalDisplay::~TerminalDisplay()
{
    endwin();
}

bool TerminalDisplay::update(const std::array<PhasePower, 3>& phase_measurements)
{
    erase();
    mvprintw(0, 0, "Powerlog - druk q om te stoppen");
    const std::string datetime = current_datetime();
    const std::string temperature = cpu_temperature();
    mvprintw(1, 0, "%s  CPU:%s C", datetime.c_str(), temperature.c_str());
    for (std::size_t phase = 0; phase < phase_measurements.size(); ++phase)
    {
        const auto& measurement = phase_measurements[phase];
        const int row = 3 + static_cast<int>(phase) * 2;
        const std::string consumption = format_value(measurement.consumption_kw, 3);
        const std::string injection = format_value(measurement.injection_kw, 3);
        const std::string voltage = format_value(measurement.voltage_v, 1);
        const std::string current = format_value(measurement.current_a, 2);
        mvprintw(row, 0, "L%zu C:%skW I:%skW", phase + 1,
             consumption.c_str(), injection.c_str());
        mvprintw(row + 1, 0, "   U:%sV A:%sA", voltage.c_str(), current.c_str());
    }
    mvprintw(12, 0, "Total consumption: %skW", format_value(
        phase_measurements[0].consumption_kw.value_or(0.0) +
        phase_measurements[1].consumption_kw.value_or(0.0) +
        phase_measurements[2].consumption_kw.value_or(0.0), 3).c_str());
    mvprintw(13, 0, "Total injection  : %skW", format_value(
        phase_measurements[0].injection_kw.value_or(0.0) +
        phase_measurements[1].injection_kw.value_or(0.0) +
        phase_measurements[2].injection_kw.value_or(0.0), 3).c_str());
    refresh();

    const int key = getch();
    return key != 'q' && key != 'Q' && key != KEY_EXIT;
}

/** Formatteert een optionele meetwaarde met een vast aantal decimalen.
 * @param value Meetwaarde die moet worden weergegeven.
 * @param decimals Aantal decimalen in de tekst.
 * @return Geformatteerde meetwaarde, of "--" als de waarde ontbreekt.
 */
std::string TerminalDisplay::format_value(const std::optional<double>& value, int decimals)
{
    if (!value)
        return "--";

    std::ostringstream text;
    text << std::fixed << std::setprecision(decimals) << *value;
    return text.str();
}