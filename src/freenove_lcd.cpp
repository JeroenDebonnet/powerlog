/* FreeNove LCD driver class
 * Copyright (c) 2026, Jeroen Debonnet
 * This file is part of the P1-logger project.
 * FreeNove LCD is a 20x4 character LCD with an I2C backpack based on the PCF8574 chip.
*/
#include "freenove_lcd.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iomanip>
#include <linux/i2c-dev.h>
#include <sstream>
#include <stdexcept>
#include <sys/ioctl.h>
#include <unistd.h>

namespace
{
constexpr int lcd_columns = 20;
constexpr std::array<std::uint8_t, 4> row_addresses = {0x00, 0x40, 0x14, 0x54};

/** Formatteert een optioneel vermogen met twee decimalen.
 * @param power Vermogen in kW, of std::nullopt als het niet beschikbaar is.
 * @return De geformatteerde waarde, of "--.--" als het vermogen ontbreekt.
 */
std::string format_power(const std::optional<double>& power)
{
    if (!power)
        return "--.--";

    std::ostringstream text;
    text << std::fixed << std::setprecision(2) << *power;
    return text.str();
}
}

/** Opent en initialiseert het FreeNove-LCD via de opgegeven I2C-bus.
 * @param i2c_device Pad naar het I2C-device.
 * @param preferred_address Eerst te proberen PCF8574-adres.
 * Gooit std::runtime_error als het I2C-device of LCD niet beschikbaar is.
 */
FreeNoveLcd::FreeNoveLcd(std::string i2c_device, std::uint8_t preferred_address)
{
    i2c_fd_ = open(i2c_device.c_str(), O_RDWR);
    if (i2c_fd_ < 0)
        throw std::runtime_error("kan I2C-device niet openen: " + std::string(std::strerror(errno)));

    const std::array<std::uint8_t, 2> addresses = {
        preferred_address,
        static_cast<std::uint8_t>(preferred_address == 0x27 ? 0x3F : 0x27)};
    for (const std::uint8_t address : addresses)
    {
        if (ioctl(i2c_fd_, I2C_SLAVE, address) == 0 && write(i2c_fd_, &output_state_, 1) == 1)
        {
            address_ = address;
            initialise();
            return;
        }
    }

    close(i2c_fd_);
    i2c_fd_ = -1;
    throw std::runtime_error("geen FreeNove LCD gevonden op 0x27 of 0x3F");
}

/** Sluit de geopende I2C-device descriptor.
 */
FreeNoveLcd::~FreeNoveLcd()
{
    if (i2c_fd_ >= 0)
        close(i2c_fd_);
}

/** Stuurt de hoge vier bits van een byte en pulseert de LCD-enable-lijn.
 * @param value Byte waarvan de hoge nibble naar het LCD gaat.
 */
void FreeNoveLcd::write_nibble(std::uint8_t value)
{
    output_state_ = static_cast<std::uint8_t>((value & 0xF0) | (output_state_ & 0x0F));
    if (write(i2c_fd_, &output_state_, 1) != 1)
        throw std::runtime_error("fout bij schrijven naar LCD");

    output_state_ |= 0x04;
    if (write(i2c_fd_, &output_state_, 1) != 1)
        throw std::runtime_error("fout bij pulsen van LCD-enable");

    output_state_ &= static_cast<std::uint8_t>(~0x04);
    if (write(i2c_fd_, &output_state_, 1) != 1)
        throw std::runtime_error("fout bij vrijgeven van LCD-enable");
    usleep(50);
}

/** Stuurt een volledig HD44780-commando in twee nibbles.
 * @param value HD44780-commando.
 */
void FreeNoveLcd::command(std::uint8_t value)
{
    output_state_ &= static_cast<std::uint8_t>(~0x01);
    write_nibble(value);
    write_nibble(static_cast<std::uint8_t>(value << 4));
    if (value == 0x01 || value == 0x02)
        usleep(2000);
}

/** Stuurt een teken als LCD-data in twee nibbles.
 * @param character ASCII-teken dat op het LCD wordt geschreven.
 */
void FreeNoveLcd::write_character(char character)
{
    output_state_ |= 0x01;
    write_nibble(static_cast<std::uint8_t>(character));
    write_nibble(static_cast<std::uint8_t>(character << 4));
}

/** Initialiseert het LCD in 4-bitmodus en schakelt het display in.
 */
void FreeNoveLcd::initialise()
{
    output_state_ = 0x08;
    usleep(50000);
    write_nibble(0x30);
    usleep(4500);
    write_nibble(0x30);
    usleep(4500);
    write_nibble(0x30);
    usleep(150);
    write_nibble(0x20);
    command(0x28); // 4-bit, 2-line mode; works for the 4-row HD44780 LCD.
    command(0x08); // display off
    command(0x01); // clear
    command(0x06); // increment cursor
    command(0x0C); // display on, cursor off
}

/** Schrijft tekst naar een rij en vult of kapt af tot twintig tekens.
 * @param row LCD-rij, van 0 tot en met 3.
 * @param text Tekst die op de rij wordt weergegeven.
 */
void FreeNoveLcd::write_line(int row, const std::string& text)
{
    command(static_cast<std::uint8_t>(0x80 | row_addresses.at(static_cast<std::size_t>(row))));
    for (int column = 0; column < lcd_columns; ++column)
        write_character(column < static_cast<int>(text.size()) ? text[static_cast<std::size_t>(column)] : ' ');
}

/** Toont het actuele afname- en injectievermogen per fase.
 * @param phase_power Vermogenswaarden voor L1, L2 en L3.
 */
void FreeNoveLcd::update(const std::array<PhasePower, 3>& phase_power)
{
    for (std::size_t phase = 0; phase < phase_power.size(); ++phase)
    {
        std::ostringstream line;
        line << "L" << phase + 1 << " C:" << format_power(phase_power[phase].consumption_kw)
             << " I:" << format_power(phase_power[phase].injection_kw) << "kW";
        write_line(static_cast<int>(phase), line.str());
    }
    write_line(3, "Powerlog  P1 meter");
}