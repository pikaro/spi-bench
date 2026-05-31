#pragma once

#include "Audio/Interfaces/TempoTrackerConfig.hpp"
#include "Audio/Interfaces/Types.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>

namespace Totem::Audio::detail {

class TempoTracker {
  public:
    using PeakEvents = std::array<std::optional<PeakResult>, peakGroupCount>;
    using Events = std::array<std::optional<BeatResult>, 2>;

    void reset(const TempoTrackerConfig &config) {
        _config = config;
        _locked = false;
        _lostEventPending = false;
        _lastEvidenceUs = 0;
        _nextBeatUs = 0;
        _beatIntervalUs = 0;
        _sequence = 0;
        _confidence = 0;
        _consecutiveMisses = 0;
        _clearIntervalHistory();
        _clearStabilityHistory();
    }

    [[nodiscard]] Events update(const FftResult &frame,
                                const PeakEvents &peakEvents) {
        Events events{};
        uint8_t eventCount = 0;
        if (!_config.enabled) {
            return events;
        }

        if (_lostEventPending) {
            _append(events, eventCount,
                    _makeEvent(BeatEventKind::Lost, 0, frame.timestampUs));
            _lostEventPending = false;
            return events;
        }

        const auto evidence = _evidence(peakEvents);
        if (evidence.has_value()) {
            if (_locked && _evidenceConfirmsExpected(*evidence)) {
                _observeEvidence(*evidence);
                _confidence = _increaseConfidence();
                _consecutiveMisses = 0;
                _pushStability(255);
                _append(events, eventCount,
                        _makeEvent(BeatEventKind::ExpectedHit, evidence->energy,
                                   evidence->timestampUs));
                _advanceSchedulePast(evidence->timestampUs);
                return events;
            }

            if (!_locked) {
                _observeEvidence(*evidence);
            }
            if (!_locked && _tryLock(evidence->timestampUs)) {
                _pushStability(_config.reacquireStabilityScore);
                _append(events, eventCount,
                        _makeEvent(BeatEventKind::Reacquired, evidence->energy,
                                   evidence->timestampUs));
                return events;
            }
        }

        if (_locked && _missDue(frame.timestampUs)) {
            _confidence = _decreaseConfidence();
            ++_consecutiveMisses;
            _pushStability(0);
            _append(events, eventCount,
                    _makeEvent(BeatEventKind::ExpectedMiss, 0, _nextBeatUs));
            _nextBeatUs += _beatIntervalUs;
            if (_consecutiveMisses >= _config.lostAfterMisses) {
                _locked = false;
                _lostEventPending = true;
                _confidence = 0;
                _lastEvidenceUs = 0;
                _clearIntervalHistory();
            }
        }

        return events;
    }

  private:
    [[nodiscard]] std::optional<PeakResult>
    _evidence(const PeakEvents &events) const {
        const auto index = peakGroupIndex(_config.evidenceGroup);
        if (index >= events.size()) {
            return std::nullopt;
        }
        return events[index];
    }

    void _observeEvidence(const PeakResult &event) {
        if (_lastEvidenceUs != 0 && event.timestampUs > _lastEvidenceUs) {
            const auto intervalUs =
                static_cast<uint32_t>(event.timestampUs - _lastEvidenceUs);
            if (_matchesEvidenceRate(intervalUs, event.ratePerMinute)) {
                _pushInterval(intervalUs);
            }
        }
        _lastEvidenceUs = event.timestampUs;
    }

    [[nodiscard]] bool _tryLock(uint64_t evidenceUs) {
        const auto interval = _consistentInterval();
        if (!interval.has_value()) {
            return false;
        }

        _locked = true;
        _beatIntervalUs = *interval;
        _nextBeatUs = evidenceUs + _beatIntervalUs;
        _confidence = _config.lockConfidence;
        _consecutiveMisses = 0;
        _lostEventPending = false;
        return true;
    }

    [[nodiscard]] bool _evidenceConfirmsExpected(const PeakResult &event) const {
        if (_beatIntervalUs == 0 || _nextBeatUs == 0) {
            return false;
        }
        const auto windowUs = static_cast<uint64_t>(_config.hitWindowMs) * 1000ULL;
        return event.timestampUs + windowUs >= _nextBeatUs &&
               event.timestampUs <= _nextBeatUs + windowUs;
    }

    [[nodiscard]] bool _missDue(uint64_t nowUs) const {
        if (_beatIntervalUs == 0 || _nextBeatUs == 0) {
            return false;
        }
        const auto windowUs = static_cast<uint64_t>(_config.hitWindowMs) * 1000ULL;
        return nowUs > _nextBeatUs + windowUs;
    }

    void _advanceSchedulePast(uint64_t timestampUs) {
        const auto windowUs = static_cast<uint64_t>(_config.hitWindowMs) * 1000ULL;
        while (_nextBeatUs != 0 && _nextBeatUs <= timestampUs + windowUs) {
            _nextBeatUs += _beatIntervalUs;
        }
    }

    void _pushInterval(uint32_t intervalUs) {
        const auto minIntervalUs = static_cast<uint32_t>(
            60000000ULL / std::max<uint16_t>(1, _config.maxBpm));
        const auto maxIntervalUs = static_cast<uint32_t>(
            60000000ULL / std::max<uint16_t>(1, _config.minBpm));
        if (intervalUs < minIntervalUs || intervalUs > maxIntervalUs) {
            return;
        }

        const auto capacity = _config.intervalHistorySize;
        _ibi[_ibiHead] = intervalUs;
        _ibiHead = static_cast<uint8_t>((_ibiHead + 1U) % capacity);
        if (_ibiCount < capacity) {
            ++_ibiCount;
        }
    }

    [[nodiscard]] bool _matchesEvidenceRate(uint32_t intervalUs,
                                            float evidenceRate) const {
        if (!std::isfinite(evidenceRate) || evidenceRate <= 0.0F ||
            intervalUs == 0) {
            return true;
        }

        const auto candidateRate =
            60.0F * 1000000.0F / static_cast<float>(intervalUs);
        if (!_rateWithinTempoBounds(candidateRate)) {
            return false;
        }

        return _ratesMatch(candidateRate, evidenceRate) ||
               _ratesMatch(candidateRate, evidenceRate * 2.0F) ||
               _ratesMatch(candidateRate, evidenceRate * 0.5F);
    }

    [[nodiscard]] bool _ratesMatch(float candidateRate,
                                   float referenceRate) const {
        if (!_rateWithinTempoBounds(referenceRate)) {
            return false;
        }
        const auto tolerance =
            (referenceRate * static_cast<float>(_config.evidenceRateTolerancePct)) /
            100.0F;
        return std::fabs(candidateRate - referenceRate) <= tolerance;
    }

    [[nodiscard]] bool _rateWithinTempoBounds(float rate) const {
        return std::isfinite(rate) &&
               rate >= static_cast<float>(_config.minBpm) &&
               rate <= static_cast<float>(_config.maxBpm);
    }

    void _pushStability(uint8_t score) {
        const auto capacity = _config.stabilityWindowSize;
        _stabilityScores[_stabilityHead] = score;
        _stabilityHead = static_cast<uint8_t>((_stabilityHead + 1U) % capacity);
        if (_stabilityCount < capacity) {
            ++_stabilityCount;
        }
    }

    [[nodiscard]] uint8_t _stabilityConfidenceCap() const {
        if (_stabilityCount == 0) {
            return 0;
        }
        uint16_t total = 0;
        for (uint8_t i = 0; i < _stabilityCount; ++i) {
            total += _stabilityScores[i];
        }
        return static_cast<uint8_t>(total / _stabilityCount);
    }

    void _clearIntervalHistory() {
        _ibi.fill(0);
        _ibiCount = 0;
        _ibiHead = 0;
    }

    void _clearStabilityHistory() {
        _stabilityScores.fill(0);
        _stabilityCount = 0;
        _stabilityHead = 0;
    }

    [[nodiscard]] std::optional<uint32_t> _consistentInterval() const {
        if (_ibiCount < _config.minConsistentIntervals) {
            return std::nullopt;
        }

        std::array<uint32_t, 16> sorted{};
        for (uint8_t i = 0; i < _ibiCount; ++i) {
            sorted[i] = _ibi[i];
        }
        std::sort(sorted.begin(), sorted.begin() + _ibiCount);
        const bool even = (_ibiCount % 2U) == 0U;
        const auto median = even
                                ? ((sorted[(_ibiCount / 2U) - 1U] / 2U) +
                                   (sorted[_ibiCount / 2U] / 2U))
                                : sorted[_ibiCount / 2U];
        if (median == 0) {
            return std::nullopt;
        }

        const auto tolerance =
            (static_cast<uint64_t>(median) * _config.intervalTolerancePct) / 100ULL;
        uint8_t consistent = 0;
        for (uint8_t i = 0; i < _ibiCount; ++i) {
            const auto value = _ibi[i];
            const auto delta = value > median ? value - median : median - value;
            if (delta <= tolerance) {
                ++consistent;
            }
        }

        if (consistent < _config.minConsistentIntervals) {
            return std::nullopt;
        }
        return median;
    }

    [[nodiscard]] uint8_t _increaseConfidence() const {
        const auto value =
            static_cast<uint16_t>(_confidence) + _config.confidenceGain;
        return static_cast<uint8_t>(std::min<uint16_t>(255, value));
    }

    [[nodiscard]] uint8_t _decreaseConfidence() const {
        if (_confidence <= _config.missPenalty) {
            return 0;
        }
        return static_cast<uint8_t>(_confidence - _config.missPenalty);
    }

    [[nodiscard]] uint8_t _bpm() const {
        if (_beatIntervalUs == 0) {
            return 0;
        }
        const auto bpm = static_cast<uint32_t>(
            std::lround(60.0F * 1000000.0F /
                        static_cast<float>(_beatIntervalUs)));
        return static_cast<uint8_t>(std::clamp<uint32_t>(bpm, 0, 255));
    }

    [[nodiscard]] BeatResult _makeEvent(BeatEventKind kind, uint8_t energy,
                                        uint64_t timestampUs) {
        return BeatResult{
            .kind = kind,
            .bpm = _bpm(),
            .confidence = static_cast<uint8_t>(
                _locked ? std::min(_confidence, _stabilityConfidenceCap())
                        : 0),
            .energy = energy,
            .sequence = ++_sequence,
            .timestampUs = timestampUs,
        };
    }

    static void _append(Events &events, uint8_t &count, BeatResult event) {
        if (count >= events.size()) {
            return;
        }
        events[count++] = event;
    }

    TempoTrackerConfig _config{};
    bool _locked = false;
    bool _lostEventPending = false;
    uint64_t _lastEvidenceUs = 0;
    uint64_t _nextBeatUs = 0;
    uint32_t _beatIntervalUs = 0;
    uint32_t _sequence = 0;
    uint8_t _confidence = 0;
    uint8_t _consecutiveMisses = 0;
    std::array<uint32_t, 16> _ibi{};
    uint8_t _ibiCount = 0;
    uint8_t _ibiHead = 0;
    std::array<uint8_t, 16> _stabilityScores{};
    uint8_t _stabilityCount = 0;
    uint8_t _stabilityHead = 0;
};

} // namespace Totem::Audio::detail
