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

std::string format_power(const std::optional<double>& power)
{
    if (!power)
        return "--.--";

    std::ostringstream text;
    text << std::fixed << std::setprecision(2) << *power;
    return text.str();
}
}

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

FreeNoveLcd::~FreeNoveLcd()
{
    if (i2c_fd_ >= 0)
        close(i2c_fd_);
}

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

void FreeNoveLcd::command(std::uint8_t value)
{
    output_state_ &= static_cast<std::uint8_t>(~0x01);
    write_nibble(value);
    write_nibble(static_cast<std::uint8_t>(value << 4));
    if (value == 0x01 || value == 0x02)
        usleep(2000);
}

void FreeNoveLcd::write_character(char character)
{
    output_state_ |= 0x01;
    write_nibble(static_cast<std::uint8_t>(character));
    write_nibble(static_cast<std::uint8_t>(character << 4));
}

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

void FreeNoveLcd::write_line(int row, const std::string& text)
{
    command(static_cast<std::uint8_t>(0x80 | row_addresses.at(static_cast<std::size_t>(row))));
    for (int column = 0; column < lcd_columns; ++column)
        write_character(column < static_cast<int>(text.size()) ? text[static_cast<std::size_t>(column)] : ' ');
}

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