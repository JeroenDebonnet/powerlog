#pragma once

#include "p1_logger.hpp"

#include <array>
#include <cstdint>
#include <string>

class FreeNoveLcd
{
public:
    /**
     * Initialiseert een FreeNove LCD2004 via een PCF8574 I2C-backpack.
     * @param i2c_device I2C-device, normaal gesproken /dev/i2c-1.
     * @param preferred_address Voorkeursadres, meestal 0x27 of 0x3F.
     */
    explicit FreeNoveLcd(std::string i2c_device = "/dev/i2c-1",
                         std::uint8_t preferred_address = 0x27);

    /** Sluit de I2C-device descriptor. */
    ~FreeNoveLcd();

    FreeNoveLcd(const FreeNoveLcd&) = delete;
    FreeNoveLcd& operator=(const FreeNoveLcd&) = delete;

    /** Toont het actuele afname- en injectievermogen van de drie fasen.
     * @param phase_power Laatst gemeten waarden voor L1, L2 en L3.
     */
    void update(const std::array<PhasePower, 3>& phase_power);

private:
    /** Opent de I2C-bus en initialiseert het LCD in 4-bitmodus. */
    void initialise();

    /** Stuurt één LCD-commando over de PCF8574.
     * @param command HD44780-commando.
     */
    void command(std::uint8_t command);

    /** Stuurt één ASCII-teken naar het LCD.
     * @param character Te schrijven ASCII-teken.
     */
    void write_character(char character);

    /** Schrijft een tekstregel op een vaste LCD-rij.
     * @param row LCD-rij van 0 tot en met 3.
     * @param text Tekst, afgekapt of opgevuld tot 20 tekens.
     */
    void write_line(int row, const std::string& text);

    /** Schrijft een byte via de PCF8574 en pulseert de enable-lijn.
     * @param value PCF8574-uitgangsbits.
     */
    void write_nibble(std::uint8_t value);

    int i2c_fd_ = -1;
    std::uint8_t address_ = 0;
    std::uint8_t output_state_ = 0x08;
};