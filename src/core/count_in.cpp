#include <modal/core/count_in.hpp>

#include <algorithm>

namespace modal::core
{

namespace
{

using CountInMode = CountInSettings::Mode;

Tick preRollStart(const MeterMap& meter, Tick recordPoint, int bars)
{
    const auto bar = meter.toBarBeatTick(recordPoint).bar;
    if (bar - bars < 1)
    {
        return 0;
    }
    return recordPoint - (meter.barStart(bar) - meter.barStart(bar - bars));
}

std::optional<PlannedCountIn> planCountIn(const Song& song, Tick recordPoint,
                                          DestinationId destination)
{
    const auto& settings = song.countIn;
    const auto& metronome = song.metronome;
    const auto& meter = song.referenceMeter();
    const auto signature = meter.signatureAt(recordPoint);
    const std::int64_t beats =
        settings.beats > 0 ? settings.beats : std::int64_t{settings.bars} * signature.numerator;
    if (beats == 0 || !metronome.isValid())
    {
        return std::nullopt;
    }

    // At the tempo at the record point throughout.
    const TempoMap tempo{song.ppqn(), song.tempo().tempoAt(recordPoint)};
    const Tick beat = signature.beatLength().toTicks(song.ppqn());
    const Tick clickTicks = std::clamp(metronome.length.toTicks(song.ppqn()), Tick{1}, beat);

    PlannedCountIn countIn{destination,
                           static_cast<std::uint8_t>(metronome.channel - 1),
                           metronome.downbeatNote,
                           metronome.downbeatVelocity,
                           metronome.beatNote,
                           metronome.beatVelocity,
                           tempo.toTime(clickTicks),
                           tempo.toTime(beats * beat),
                           {}};

    // The record point's beat in its bar, counting from 0, so the count-in's accents fall
    // where bars would start if its meter ran back from there.
    const std::int64_t beatInBar = meter.toBarBeatTick(recordPoint).beat - 1;
    for (std::int64_t back = beats; back > 0; --back)
    {
        const auto inBar =
            ((beatInBar - back) % signature.numerator + signature.numerator) % signature.numerator;
        countIn.clicks.push_back({tempo.toTime(back * beat), inBar == 0});
    }
    return countIn;
}

} // namespace

RecordStart planRecordStart(const Song& song, Tick recordPoint,
                            std::optional<DestinationId> metronomeDestination)
{
    const auto& settings = song.countIn;
    if (!settings.isValid())
    {
        return {recordPoint, std::nullopt};
    }
    switch (settings.mode)
    {
    case CountInMode::None:
        break;
    case CountInMode::CountIn:
        if (metronomeDestination)
        {
            return {recordPoint, planCountIn(song, recordPoint, *metronomeDestination)};
        }
        break;
    case CountInMode::PreRoll:
        return {preRollStart(song.referenceMeter(), recordPoint, settings.preRollBars),
                std::nullopt};
    }
    return {recordPoint, std::nullopt};
}

} // namespace modal::core
