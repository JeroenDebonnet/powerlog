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

        /** Verbiedt kopiëren omdat dit object eigenaar is van de ncurses-sessie.
         * @param other Terminalobject waarvan kopiëren wordt voorkomen.
         */
    TerminalDisplay(const TerminalDisplay&) = delete;

        /** Verbiedt toewijzing omdat dit object eigenaar is van de ncurses-sessie.
         * @param other Terminalobject waarvan toewijzing wordt voorkomen.
         */
    TerminalDisplay& operator=(const TerminalDisplay&) = delete;

    /** Toont de gemeten waarden van alle drie fasen.
     * @param phase_measurements Meetwaarden voor L1, L2 en L3.
         * @param daily_energy Dagtotalen en meterstanden voor de huidige dag.
     * @return false wanneer de gebruiker 'q' indrukt of de terminal sluit.
     */
    bool update(const std::array<PhasePower, 3>& phase_measurements,
                const DailyEnergy& daily_energy);

private:
    /** Zet één optionele waarde om naar compacte terminaltekst.
     * @param value Meetwaarde die weergegeven moet worden.
     * @param decimals Aantal decimalen.
     * @return Geformatteerde waarde, of "--" als de waarde ontbreekt.
     */
    static std::string format_value(const std::optional<double>& value, int decimals);
};