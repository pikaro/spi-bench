#include "BatteryMonitor/Interfaces/Wire.hpp"
#include "BatteryMonitor/detail/Calibration.hpp"
#include "BatteryMonitor/detail/Estimator.hpp"
#include "BatteryMonitor/detail/JournalFormat.hpp"
#include "BatteryMonitor/detail/JournalScanner.hpp"
#include "BatteryMonitor/detail/JournalStorage.hpp"
#include "BatteryMonitor/detail/StartRequest.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iostream>
#include <span>
#include <vector>

namespace {

using namespace Totem::BatteryMonitor;
using namespace Totem::BatteryMonitor::detail;

int failures = 0;

void expect(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

BatteryMeasurement sample(uint32_t atMs, uint32_t millivolts, int32_t microamps,
                          int32_t milliwatts) {
    return {
        .capturedAtMs = atMs,
        .voltageMillivolts = millivolts,
        .currentMicroamps = microamps,
        .powerMilliwatts = milliwatts,
    };
}

void testBatteryStatusEventAvailability() {
    BatteryStatusEvent event{
        .stateOfChargePartsPerThousand = 500,
        .sourceState = BatterySourceState::Normal,
        .measurementFreshness = BatteryMeasurementFreshness::Fresh,
        .confidence = BatteryEstimateConfidence::VoltageOnly,
    };
    expect(hasUsableStateOfCharge(event),
           "fresh normal battery estimate must be usable by consumers");

    event.measurementFreshness = BatteryMeasurementFreshness::Stale;
    expect(!hasUsableStateOfCharge(event),
           "stale battery estimate must not drive a gauge");
    event.measurementFreshness = BatteryMeasurementFreshness::Fresh;
    event.sourceState = BatterySourceState::Absent;
    expect(!hasUsableStateOfCharge(event),
           "absent battery must not drive a gauge");
    event.sourceState = BatterySourceState::Normal;
    event.stateOfChargePartsPerThousand = 1'001;
    expect(!hasUsableStateOfCharge(event),
           "out-of-range battery estimate must not drive a gauge");
}

void testEstimatorAvailability() {
    BatteryConfig config{};
    config.averagePowerWindowMs = 1'000;
    config.minimumTimeToEmptyPowerMilliwatts = 1;
    Estimator estimator{};
    estimator.begin(config);

    auto first = estimator.observe(sample(0, 25'200, 100'000, 1'000));
    expect(!first.integrated, "first sample must not integrate");
    expect(estimator.status().measurementFreshness ==
               BatteryMeasurementFreshness::Fresh,
           "first sample must be fresh");
    expect(estimator.status().timeToEmptyMinutes.has_value(),
           "positive load must produce TTE");

    (void)estimator.observe(sample(1, 25'200, -100'000, -1'000));
    expect(!estimator.status().timeToEmptyMinutes.has_value(),
           "charging must invalidate TTE immediately");

    expect(estimator.checkFreshness(config.sampleGapToleranceMs + 2U),
           "freshness timeout must transition once");
    expect(estimator.status().measurementFreshness ==
               BatteryMeasurementFreshness::Stale,
           "expired measurement must be stale");
    expect(!estimator.status().timeToEmptyMinutes.has_value(),
           "stale measurement must not retain TTE");
    expect(!estimator.checkFreshness(config.sampleGapToleranceMs + 3U),
           "stale transition must be edge-triggered");
}

void testAveragePowerRemainder() {
    BatteryConfig config{};
    config.averagePowerWindowMs = 1'000;
    config.sampleGapToleranceMs = 2'000;
    config.minimumTimeToEmptyPowerMilliwatts = 1;
    Estimator estimator{};
    estimator.begin(config);
    (void)estimator.observe(sample(0, 25'200, 100'000, 1'000));

    uint32_t previous = estimator.status().averagePowerMilliwatts;
    for (uint32_t atMs = 1; atMs <= 1'000; ++atMs) {
        (void)estimator.observe(sample(atMs, 25'200, 0, 0));
        const auto current = estimator.status().averagePowerMilliwatts;
        expect(current <= previous, "EMA decay must be monotonic");
        previous = current;
    }
    expect(previous < 500,
           "fractional EMA adjustments must accumulate instead of freezing");

    (void)estimator.observe(sample(1'001, 25'200, 100'000, 2'000));
    expect(estimator.status().averagePowerMilliwatts >= previous,
           "EMA step-up must be monotonic");
}

void testGapAndRollover() {
    BatteryConfig config{};
    config.sampleGapToleranceMs = 2'000;
    Estimator estimator{};
    estimator.begin(config);
    (void)estimator.observe(sample(0, 25'200, 1'000'000, 25'200));
    const auto gap =
        estimator.observe(sample(2'001, 25'200, 1'000'000, 25'200));
    expect(gap.sampleGapExceeded, "oversized sample gap must be reported");
    expect(!gap.integrated, "oversized sample gap must not be integrated");

    estimator.reset();
    (void)estimator.observe(sample(0xFFFFFFF0U, 25'200, 1'000'000, 25'200));
    const auto wrapped =
        estimator.observe(sample(0x10U, 25'200, 1'000'000, 25'200));
    expect(wrapped.elapsedMs == 32U, "timestamp rollover must be preserved");
    expect(wrapped.integrated, "short rollover interval must integrate");
}

void testValidZeroTte() {
    BatteryConfig config{};
    config.minimumTimeToEmptyPowerMilliwatts = 1;
    Estimator estimator{};
    estimator.begin(config);
    (void)estimator.observe(
        sample(0, config.practicalMinPackMillivolts(), 100'000, 1'000));
    expect(estimator.status().timeToEmptyMinutes.has_value(),
           "depleted battery under load must have a valid TTE");
    expect(estimator.status().timeToEmptyMinutes.value_or(1) == 0,
           "valid zero-minute TTE must differ from unavailable");
}

void testCalibrationFailureConsumption() {
    BatteryConfig config{};
    Estimator estimator{};
    estimator.begin(config);
    const auto full = sample(100, 29'000, 100'000, 2'900);
    (void)estimator.observe(full);

    Calibration calibration{};
    calibration.begin(config);
    expect(calibration.arm(estimator.status(), 1).ok(),
           "qualified calibration must arm");
    const auto observation =
        estimator.observe(sample(200, 28'990, 100'000, 2'899));
    (void)calibration.observe(sample(200, 28'990, 100'000, 2'899), observation,
                              estimator.status());
    expect(calibration.headerPending(), "started calibration needs a header");
    calibration.storageFailed();
    expect(!calibration.headerPending(),
           "failed header must be consumed rather than retried");
    expect(!calibration.intervalPending(),
           "failed persistence must clear pending interval");
    expect(!calibration.footerPending(),
           "failed persistence must clear pending footer");
}

void testCalibrationRejectsStaleMeasurement() {
    BatteryConfig config{};
    Estimator estimator{};
    estimator.begin(config);
    (void)estimator.observe(sample(100, 29'000, 100'000, 2'900));
    expect(estimator.checkFreshness(100 + config.sampleGapToleranceMs + 1U),
           "calibration test measurement must become stale");

    Calibration calibration{};
    calibration.begin(config);
    expect(!calibration.arm(estimator.status(), 1).ok(),
           "stale full measurement must not arm calibration");
    expect(calibration.reason() ==
               BatteryCalibrationInvalidReason::SensorTimeout,
           "stale calibration rejection must identify sensor timeout");
}

void testCalibrationStartRejections() {
    BatteryConfig config{};
    BatteryStatus status{
        .latestMeasurement = sample(100, 29'000, 100'000, 2'900),
        .measurementFreshness = BatteryMeasurementFreshness::Fresh,
        .storageHealth = BatteryStorageHealth::Healthy,
    };

    expect(assessCalibrationStart(status, config).queued(),
           "qualified calibration start must be queueable");

    status.latestMeasurement->voltageMillivolts = 27'999;
    expect(assessCalibrationStart(status, config).reason ==
               BatteryCalibrationInvalidReason::NotFull,
           "low start voltage must report NotFull");

    status.latestMeasurement->voltageMillivolts = 31'000;
    expect(assessCalibrationStart(status, config).reason ==
               BatteryCalibrationInvalidReason::AbsoluteLimit,
           "excessive start voltage must report AbsoluteLimit");

    status.latestMeasurement->voltageMillivolts = 29'000;
    status.latestMeasurement->currentMicroamps = 0;
    expect(assessCalibrationStart(status, config).reason ==
               BatteryCalibrationInvalidReason::NoDischargeLoad,
           "low start current must report NoDischargeLoad");

    status.latestMeasurement->currentMicroamps = -10'000;
    expect(assessCalibrationStart(status, config).reason ==
               BatteryCalibrationInvalidReason::ChargingDetected,
           "negative start current must report ChargingDetected");

    status.latestMeasurement->currentMicroamps = 100'000;
    status.measurementFreshness = BatteryMeasurementFreshness::Stale;
    expect(assessCalibrationStart(status, config).reason ==
               BatteryCalibrationInvalidReason::SensorTimeout,
           "stale start measurement must report SensorTimeout");

    status.measurementFreshness = BatteryMeasurementFreshness::Fresh;
    status.latestMeasurement.reset();
    expect(assessCalibrationStart(status, config).reason ==
               BatteryCalibrationInvalidReason::SensorTimeout,
           "missing start measurement must report SensorTimeout");

    status.storageHealth = BatteryStorageHealth::Full;
    expect(assessCalibrationStart(status, config).reason ==
               BatteryCalibrationInvalidReason::StorageUnavailable,
           "full calibration storage must report StorageUnavailable");
}

void testJournalFormat() {
    BatteryConfig config{};
    const auto zeroPayloadRecord = makeJournalRecord(
        JournalRecordType::SessionHeader, 0, 7, JournalPayload{});
    expect(getU32(zeroPayloadRecord, journalCrcOffset) == 0xCA2F39D4U,
           "journal CRC must match the standard host zlib CRC-32 used by the "
           "commissioning validator");

    const auto payload = makeHeaderPayload(config, 123);
    auto record =
        makeJournalRecord(JournalRecordType::SessionHeader, 0, 7, payload);
    DecodedJournalRecord decoded{};
    expect(decodeJournalRecord(record, decoded),
           "fresh journal record must pass CRC");
    expect(decoded.sessionId == 7 && decoded.sequence == 0,
           "journal identity must round-trip");
    record[journalPayloadOffset] ^= std::byte{1};
    expect(!decodeJournalRecord(record, decoded),
           "corrupt journal payload must fail CRC");

    const ProfilePointRecordData point{
        .stateOfChargePercent = 42,
        .loadedVoltageMillivolts = 25'000,
        .representativeCurrentMicroamps = 500'000,
    };
    const auto decodedPoint =
        decodeProfilePointPayload(makeProfilePointPayload(point));
    expect(decodedPoint.stateOfChargePercent == 42 &&
               decodedPoint.loadedVoltageMillivolts == 25'000 &&
               decodedPoint.representativeCurrentMicroamps == 500'000,
           "semantic profile point must round-trip");
}

struct FaultSink {
    enum class Failure : uint8_t {
        None,
        Open,
        Write,
        ShortWrite,
        Flush,
        Close,
    };

    ReturnCode open() {
        ++openCalls;
        return result(Failure::Open);
    }

    std::expected<std::size_t, ReturnCode>
    write(std::span<const std::byte> bytes) {
        ++writeCalls;
        if (failure == Failure::Write) {
            return std::unexpected(
                ReturnCode::from(CoreError::OperationFailed));
        }
        if (failure == Failure::ShortWrite) {
            return bytes.size() - 1U;
        }
        return bytes.size();
    }

    ReturnCode flush() {
        ++flushCalls;
        return result(Failure::Flush);
    }

    ReturnCode close() {
        ++closeCalls;
        return result(Failure::Close);
    }

    ReturnCode result(Failure stage) const {
        return ReturnCode::from(failure == stage ? CoreError::OperationFailed
                                                 : CoreError::Ok);
    }

    Failure failure = Failure::None;
    uint32_t openCalls = 0;
    uint32_t writeCalls = 0;
    uint32_t flushCalls = 0;
    uint32_t closeCalls = 0;
};

void testJournalWriteFailures() {
    const auto record = makeJournalRecord(JournalRecordType::SessionHeader, 0,
                                          1, JournalPayload{});
    for (const auto failure :
         {FaultSink::Failure::Open, FaultSink::Failure::Write,
          FaultSink::Failure::ShortWrite, FaultSink::Failure::Flush,
          FaultSink::Failure::Close}) {
        FaultSink sink{.failure = failure};
        const auto ret = appendJournalRecord(sink, record);
        expect(!ret.ok(), "injected journal stage failure must propagate");
        expect(sink.openCalls == 1,
               "journal append must attempt open exactly once");
        expect(sink.writeCalls <= 1 && sink.flushCalls <= 1,
               "journal append must never retry write or flush");
        expect(sink.closeCalls <= 1, "journal append must close at most once");
    }

    FaultSink success{};
    expect(appendJournalRecord(success, record).ok(),
           "successful journal append must complete");
    expect(success.openCalls == 1 && success.writeCalls == 1 &&
               success.flushCalls == 1 && success.closeCalls == 1,
           "successful journal append must run each stage once");
}

void testJournalBounds() {
    expect(journalRecordsFit(10, 20, 30, 20 * journalRecordSize),
           "exact journal and free-space bound must fit");
    expect(!journalRecordsFit(10, 21, 30, 21 * journalRecordSize),
           "journal record limit must reject a full session");
    expect(!journalRecordsFit(31, 0, 30, 0),
           "oversized existing journal must be rejected");
    expect(!journalRecordsFit(10, 20, 30, 20 * journalRecordSize - 1),
           "filesystem free-space limit must reject a short capacity");
}

struct MemoryJournalReader {
    explicit MemoryJournalReader(const std::vector<std::byte> &storage)
        : bytes{storage} {}

    std::expected<bool, ReturnCode> readNext() {
        if (offset == bytes.size()) {
            chunkSize = 0;
            return false;
        }
        chunkSize = std::min(buffer.size(), bytes.size() - offset);
        std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                    chunkSize, buffer.begin());
        offset += chunkSize;
        return true;
    }

    [[nodiscard]] std::size_t size() const { return chunkSize; }
    [[nodiscard]] std::span<const std::byte> span() const {
        return {buffer.data(), chunkSize};
    }

    const std::vector<std::byte> &bytes;
    std::array<std::byte, journalRecordSize> buffer{};
    std::size_t offset = 0;
    std::size_t chunkSize = 0;
};

void appendRecord(std::vector<std::byte> &storage,
                  const JournalRecord &record) {
    storage.insert(storage.end(), record.begin(), record.end());
}

std::expected<JournalScanResult, ReturnCode>
scanMemoryJournal(const std::vector<std::byte> &storage,
                  const BatteryConfig &config, uint32_t maximumRecords = 512) {
    auto initial = classifyJournalSize(storage.size(), maximumRecords);
    if (initial.issue == JournalScanIssue::Oversized) {
        return initial;
    }
    MemoryJournalReader reader{storage};
    return scanJournalRecords(reader, config, maximumRecords, initial);
}

void testJournalRebootRecovery() {
    BatteryConfig config{};
    std::vector<std::byte> storage{};
    const auto header = makeJournalRecord(JournalRecordType::SessionHeader, 0,
                                          7, makeHeaderPayload(config, 100));
    const auto interval =
        makeJournalRecord(JournalRecordType::Interval, 1, 7,
                          makeIntervalPayload({.sampleCount = 1}));
    appendRecord(storage, header);
    appendRecord(storage, interval);

    auto interrupted = scanMemoryJournal(storage, config);
    expect(interrupted.has_value(), "interrupted journal must remain readable");
    expect(interrupted && interrupted->incompleteSessionCount == 1 &&
               interrupted->danglingSessionId == 7 &&
               interrupted->danglingNextSequence == 2,
           "reboot scan must identify the exact dangling session");

    const uint32_t recordsChecksum =
        getU32(header, journalCrcOffset) ^ getU32(interval, journalCrcOffset);
    const auto rebootFooter = makeJournalRecord(
        JournalRecordType::SessionFooter, 2, 7,
        makeFooterPayload({
            .state = BatteryCalibrationState::Invalid,
            .reason = BatteryCalibrationInvalidReason::Rebooted,
            .recordsChecksum = recordsChecksum,
        }));
    appendRecord(storage, rebootFooter);

    auto recovered = scanMemoryJournal(storage, config);
    expect(recovered.has_value(), "reboot footer journal must remain readable");
    expect(recovered && recovered->incompleteSessionCount == 1 &&
               recovered->danglingSessionId == 0,
           "reboot footer must close without activating the session");

    storage.push_back(std::byte{0x42});
    auto partial = scanMemoryJournal(storage, config);
    expect(partial && partial->issue == JournalScanIssue::Corrupt,
           "partial trailing write must be detected as corrupt");
}

void testJournalCompleteProfileActivation() {
    BatteryConfig config{};
    std::vector<std::byte> storage{};
    uint32_t sequence = 0;
    uint32_t recordsChecksum = 0;
    const auto header =
        makeJournalRecord(JournalRecordType::SessionHeader, sequence++, 11,
                          makeHeaderPayload(config, 100));
    appendRecord(storage, header);
    recordsChecksum = getU32(header, journalCrcOffset);

    const auto interval =
        makeJournalRecord(JournalRecordType::Interval, sequence++, 11,
                          makeIntervalPayload({.cumulativeMilliampHours = 1,
                                               .cumulativeMilliwattHours = 25,
                                               .sampleCount = 1}));
    appendRecord(storage, interval);
    recordsChecksum ^= getU32(interval, journalCrcOffset);

    for (uint16_t soc = 0; soc <= 100; ++soc) {
        const auto point = makeJournalRecord(
            JournalRecordType::ProfilePoint, sequence++, 11,
            makeProfilePointPayload({
                .stateOfChargePercent = soc,
                .loadedVoltageMillivolts = 21'000U + soc * 84U,
                .representativeCurrentMicroamps = 500'000,
            }));
        appendRecord(storage, point);
        recordsChecksum ^= getU32(point, journalCrcOffset);
    }
    const auto footer =
        makeJournalRecord(JournalRecordType::SessionFooter, sequence, 11,
                          makeFooterPayload({
                              .state = BatteryCalibrationState::Complete,
                              .reason = BatteryCalibrationInvalidReason::None,
                              .pointCount = profilePointCount,
                              .usableMilliampHours = 9'000,
                              .usableMilliwattHours = 230'000,
                              .recordsChecksum = recordsChecksum,
                          }));
    appendRecord(storage, footer);

    auto complete = scanMemoryJournal(storage, config);
    expect(complete && complete->issue == JournalScanIssue::None,
           "complete journal must scan cleanly");
    expect(complete && complete->completeSessionCount == 1 &&
               complete->latestProfile.status.active &&
               complete->latestProfile.status.sessionId == 11,
           "only a complete checksum-valid profile may activate");

    storage[journalPayloadOffset] ^= std::byte{1};
    auto corrupt = scanMemoryJournal(storage, config);
    expect(corrupt && corrupt->issue == JournalScanIssue::Corrupt &&
               !corrupt->latestProfile.status.active,
           "corruption must prevent profile activation");

    const auto oversized = classifyJournalSize(
        (static_cast<uint64_t>(config.maximumJournalRecords) + 1U) *
            journalRecordSize,
        config.maximumJournalRecords);
    expect(oversized.issue == JournalScanIssue::Oversized,
           "journal size classification must stop beyond its record bound");
}

} // namespace

int main() {
    testBatteryStatusEventAvailability();
    testEstimatorAvailability();
    testAveragePowerRemainder();
    testGapAndRollover();
    testValidZeroTte();
    testCalibrationFailureConsumption();
    testCalibrationRejectsStaleMeasurement();
    testCalibrationStartRejections();
    testJournalFormat();
    testJournalWriteFailures();
    testJournalBounds();
    testJournalRebootRecovery();
    testJournalCompleteProfileActivation();
    if (failures != 0) {
        std::cerr << failures << " battery monitor test(s) failed\n";
        return 1;
    }
    std::cout << "BatteryMonitor host logic tests passed\n";
    return 0;
}
