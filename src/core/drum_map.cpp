// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/drum_map.hpp>

#include <cstddef>

namespace modal::core
{

// The General MIDI percussion names, which most drum sounds follow, from 35 to 81.
std::string_view generalMidiDrumName(int note)
{
    static constexpr std::array<std::string_view, 47> kNames{"Acoustic Bass Drum",
                                                             "Bass Drum 1",
                                                             "Side Stick",
                                                             "Acoustic Snare",
                                                             "Hand Clap",
                                                             "Electric Snare",
                                                             "Low Floor Tom",
                                                             "Closed Hi-Hat",
                                                             "High Floor Tom",
                                                             "Pedal Hi-Hat",
                                                             "Low Tom",
                                                             "Open Hi-Hat",
                                                             "Low-Mid Tom",
                                                             "Hi-Mid Tom",
                                                             "Crash Cymbal 1",
                                                             "High Tom",
                                                             "Ride Cymbal 1",
                                                             "Chinese Cymbal",
                                                             "Ride Bell",
                                                             "Tambourine",
                                                             "Splash Cymbal",
                                                             "Cowbell",
                                                             "Crash Cymbal 2",
                                                             "Vibraslap",
                                                             "Ride Cymbal 2",
                                                             "Hi Bongo",
                                                             "Low Bongo",
                                                             "Mute Hi Conga",
                                                             "Open Hi Conga",
                                                             "Low Conga",
                                                             "High Timbale",
                                                             "Low Timbale",
                                                             "High Agogo",
                                                             "Low Agogo",
                                                             "Cabasa",
                                                             "Maracas",
                                                             "Short Whistle",
                                                             "Long Whistle",
                                                             "Short Guiro",
                                                             "Long Guiro",
                                                             "Claves",
                                                             "Hi Wood Block",
                                                             "Low Wood Block",
                                                             "Mute Cuica",
                                                             "Open Cuica",
                                                             "Mute Triangle",
                                                             "Open Triangle"};
    return note >= 35 && note <= 81 ? kNames[note - 35] : std::string_view{};
}

DrumMap generalMidiDrumMap()
{
    DrumMap map;
    map.name = "General MIDI";
    for (std::size_t note = 0; note < map.notes.size(); ++note)
    {
        map.notes[note] = generalMidiDrumName(static_cast<int>(note));
    }
    return map;
}

} // namespace modal::core
