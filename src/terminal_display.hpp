#pragma once

#include "p1_logger.hpp"

#include <array>
#include <string>

class TerminalDisplay
{
public:
    /** Initialiseert de ncurses-terminalweergave.
     * De terminal wordt automatisch hersteld in de destructor.
     */
    TerminalDisplay();

    /** Herstelt de terminalinstellingen en sluit ncurses. */
    ~TerminalDisplay();

    TerminalDisplay(const TerminalDisplay&) = delete;
    TerminalDisplay& operator=(const TerminalDisplay&) = delete;

    /** Toont de gemeten waarden van alle drie fasen.
     * @param phase_measurements Meetwaarden voor L1, L2 en L3.
     * @return false wanneer de gebruiker 'q' indrukt of de terminal sluit.
     */
    bool update(const std::array<PhasePower, 3>& phase_measurements);

private:
    /** Zet één optionele waarde om naar compacte terminaltekst.
     * @param value Meetwaarde die weergegeven moet worden.
     * @param decimals Aantal decimalen.
     * @return Geformatteerde waarde, of "--" als de waarde ontbreekt.
     */
    static std::string format_value(const std::optional<double>& value, int decimals);
};