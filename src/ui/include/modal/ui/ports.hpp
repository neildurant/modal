// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace modal::ui
{

// MIDI ports are one-way: the system lists its inputs and outputs apart.
enum class PortDirection
{
    Input,
    Output,
};

// A port Modal has seen on this machine: its detected name, as the platform labels it (unique
// among those of its direction), the alias it's given, whether it's enabled, and whether it's
// there now. Only enabled ports are used: listened to, or offered to tracks.
struct KnownPort
{
    PortDirection direction = PortDirection::Input;
    std::string name;
    std::string alias;
    bool enabled = false;
    bool present = false; // not saved: whether the system lists it now

    // The alias, or else the detected name: what tracks and choosers call it.
    const std::string& shown() const { return alias.empty() ? name : alias; }

    friend bool operator==(const KnownPort&, const KnownPort&) = default;
};

// Why a track's port can't be used, if it can't.
enum class PortProblem
{
    None,
    Missing,  // no known port goes by that name, or it isn't there now
    Disabled, // the port is known but not enabled in the Ports view
};

// What a track's port name comes to: the detected name to open, or why there's none. For an
// input, an empty name is every enabled input, with no port of its own and no problem.
struct PortResolution
{
    std::optional<std::string> port;
    PortProblem problem = PortProblem::None;
    std::string named; // the name looked for: the track's, or the default output's

    friend bool operator==(const PortResolution&, const PortResolution&) = default;
};

// The MIDI ports of this machine, as the Ports view sets them (decided November 2026): kept in
// the `ports` file in the state directory, written by Modal, so they belong to the machine and
// never to the user's modalrc or a song. Songs name ports by alias, so a song plays on another
// machine whose ports have the same aliases.
class PortTable
{
public:
    // The ports the system lists now in a direction: those not seen before are added, disabled,
    // and every port of that direction is marked present or not. Returns whether any was added,
    // so the file can be written.
    bool detected(PortDirection direction, std::span<const std::string> names);

    // Every known port, inputs first, each direction in the order first seen.
    const std::vector<KnownPort>& ports() const { return ports_; }
    const KnownPort* find(PortDirection direction, std::string_view name) const;

    // Changing a port, found by its detected name; each returns whether it changed anything.
    bool setAlias(PortDirection direction, std::string_view name, std::string alias);
    bool setEnabled(PortDirection direction, std::string_view name, bool enabled);
    bool forget(PortDirection direction, std::string_view name);

    // The output for tracks without a port of their own, by detected name, or empty for none.
    const std::string& defaultOutput() const { return defaultOutput_; }
    bool setDefaultOutput(std::string name);

    // The detected names of the enabled ports there now: the inputs to listen to, or the
    // outputs that can be opened.
    std::vector<std::string> usable(PortDirection direction) const;

    // What the Tracks choosers offer: the enabled ports, by what they're shown as.
    std::vector<std::string> offered(PortDirection direction) const;

    // A track's port name: an alias, or else a detected name, exactly, then ignoring case, then
    // a part of one, as :port has always matched; empty for the default output, or every input.
    PortResolution resolve(PortDirection direction, std::string_view name) const;

    // As the file has it, and from it. A line it can't read is passed over and said in errors.
    std::string text() const;
    static PortTable read(std::string_view text, std::vector<std::string>* errors = nullptr);

    friend bool operator==(const PortTable&, const PortTable&) = default;

private:
    KnownPort* findPort(PortDirection direction, std::string_view name);

    std::vector<KnownPort> ports_;
    std::string defaultOutput_;
};

} // namespace modal::ui
