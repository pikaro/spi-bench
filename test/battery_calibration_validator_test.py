# ruff: noqa: COM812, INP001, S101, S603

from __future__ import annotations

import struct
import subprocess
import tempfile
import unittest
import zlib
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
VALIDATOR = REPO_ROOT / 'bin' / 'validate-battery-calibration'
RECORD_SIZE = 64
PAYLOAD_SIZE = 44
CRC_OFFSET = 60
SESSION_ID = 7


def make_record(kind: int, sequence: int, payload: bytes) -> bytes:
    raw = bytearray(RECORD_SIZE)
    struct.pack_into(
        '<IBBHII',
        raw,
        0,
        0x4E4F4D42,
        2,
        kind,
        PAYLOAD_SIZE,
        sequence,
        SESSION_ID,
    )
    raw[16:60] = payload
    struct.pack_into('<I', raw, CRC_OFFSET, zlib.crc32(raw[:CRC_OFFSET]))
    return bytes(raw)


def make_fixture() -> tuple[bytes, str]:
    records: list[bytes] = []
    checksum = 0
    sequence = 0

    header = bytearray(PAYLOAD_SIZE)
    struct.pack_into(
        '<IIIIIIII',
        header,
        0,
        0x12345678,
        100,
        1,
        10_000,
        259_000,
        50_000,
        21_000,
        29_400,
    )
    header[32:35] = bytes((7, 4, 0))
    record = make_record(1, sequence, header)
    records.append(record)
    checksum = struct.unpack_from('<I', record, CRC_OFFSET)[0]
    sequence += 1

    definitions = [
        (60, 29_000, 600),
        (120, 28_800, 600),
        (180, 28_600, 600),
        (185, 28_500, 10),
    ]
    intervals: list[tuple[int, ...]] = []
    previous_elapsed = 0
    charge = 0.0
    energy = 0.0
    log_lines: list[str] = []
    for number, (elapsed, voltage, samples) in enumerate(definitions, start=1):
        delta = elapsed - previous_elapsed
        current = voltage * 20
        power = voltage * current // 1_000_000
        charge += current * delta / 3_600_000
        energy += power * delta / 3_600
        interval = (
            elapsed,
            voltage,
            voltage - 10,
            voltage + 10,
            current,
            power,
            int(charge),
            int(energy),
            samples,
            100,
            voltage,
        )
        intervals.append(interval)
        payload = struct.pack('<IIIIiiIIIII', *interval)
        record = make_record(2, sequence, payload)
        records.append(record)
        checksum ^= struct.unpack_from('<I', record, CRC_OFFSET)[0]
        sequence += 1
        log_lines.extend(
            (
                f'[12:{number:02d}:00.000] Battery cal interval: sid={SESSION_ID} '
                f'n={number} elapsed={elapsed}s samples={samples} maxGap=100ms',
                f'[12:{number:02d}:00.001] Battery cal values: sid={SESSION_ID} '
                f'n={number} V={voltage}/{voltage - 10}/{voltage + 10}mV '
                f'I={current}uA P={power}mW used={int(charge)}mAh/'
                f'{int(energy)}mWh',
            )
        )
        previous_elapsed = elapsed

    for soc in range(101):
        interval = intervals[soc % len(intervals)]
        payload = bytearray(PAYLOAD_SIZE)
        struct.pack_into('<H', payload, 0, soc)
        struct.pack_into('<I', payload, 4, interval[1])
        struct.pack_into('<i', payload, 8, interval[4])
        record = make_record(3, sequence, payload)
        records.append(record)
        checksum ^= struct.unpack_from('<I', record, CRC_OFFSET)[0]
        sequence += 1

    footer = bytearray(PAYLOAD_SIZE)
    struct.pack_into('<BBH', footer, 0, 4, 0, 101)
    struct.pack_into(
        '<IIIIIIII',
        footer,
        4,
        int(charge),
        int(energy),
        190,
        0,
        definitions[-1][1],
        len(intervals),
        100,
        checksum,
    )
    records.append(make_record(4, sequence, footer))
    return b''.join(records), '\n'.join(log_lines) + '\n'


class BatteryCalibrationValidatorTest(unittest.TestCase):
    def run_validator(
        self, journal: bytes, log: str | None = None
    ) -> subprocess.CompletedProcess[str]:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            journal_path = root / 'battery.bin'
            journal_path.write_bytes(journal)
            command = [str(VALIDATOR), str(journal_path)]
            if log is not None:
                log_path = root / 'capture.log'
                log_path.write_text(log)
                command.extend(('--log', str(log_path)))
            return subprocess.run(command, capture_output=True, text=True, check=False)

    def test_accepts_consistent_complete_session_and_log(self) -> None:
        journal, log = make_fixture()
        result = self.run_validator(journal, log)
        assert result.returncode == 0, result.stderr
        assert 'PASS session 7' in result.stdout
        assert 'all 4 timestamped interval reports match' in result.stdout

    def test_rejects_corrupt_record_crc(self) -> None:
        journal, _ = make_fixture()
        corrupted = bytearray(journal)
        corrupted[20] ^= 1
        result = self.run_validator(bytes(corrupted))
        assert result.returncode == 1
        assert 'CRC mismatch' in result.stderr

    def test_rejects_log_that_does_not_match_journal(self) -> None:
        journal, log = make_fixture()
        result = self.run_validator(journal, log.replace('samples=600', 'samples=599', 1))
        assert result.returncode == 1
        assert 'log interval 1 is missing or differs' in result.stderr


if __name__ == '__main__':
    unittest.main()
