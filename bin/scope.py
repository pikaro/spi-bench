#!/usr/bin/env python3

from __future__ import annotations

import logging
import struct
import time
from enum import Enum
from pathlib import Path
from typing import ClassVar, Literal, overload

from pydantic import BaseModel
from serial import Serial
from serial.tools import list_ports

logging.basicConfig(level=logging.DEBUG, format='%(asctime)s - %(levelname)s - %(message)s')
log = logging.getLogger(__name__)

SERIAL = 'EL131647'


class Channel(Enum):
    CH1 = 1
    CH2 = 2


class Error(Enum):
    NoError = 0
    InvalidCommand = -100
    SyntaxError = -102
    ParameterError = -220
    SettingsConflict = -221
    OutOfRange = -222
    TooMuchData = -223
    IllegalParameter = -224
    InvalidFormat = -232


class Hexprinter:
    _byte_count: int = 0

    def print(self, data: bytes) -> str:
        ret = ''.join(self._print_byte(b) for b in data)
        self._byte_count = 0
        return ret

    def _print_byte(self, data: int, padding: int = 2) -> str:
        self._byte_count += 1
        term = '\n' if self._byte_count % 32 == 0 else ' '
        return _tohex(data, padding) + term


def _tohex(data: int, padding=2) -> str:
    return f'{data:#0{padding + 2}x}'


def _tohex_bytes(data: bytes) -> str:
    return ' '.join(_tohex(b) for b in data)


def _tofloat(data: bytes, endian: Literal['little', 'big'] = 'big') -> float:
    if len(data) != 4:
        printer = Hexprinter()
        print(printer.print(data))
        raise ValueError(f'Data must be 4 bytes for float conversion, got {len(data)} bytes')
    fmt = '<f' if endian == 'little' else '>f'
    return struct.unpack(fmt, data)[0]


def _validate_ascii(v: bytes, name: str, exp: list[str] | None = None) -> str:
    asc = v.decode('ascii')
    if exp is not None and asc not in exp:
        _err = f"Invalid {name}: {asc} ({_tohex_bytes(v)}). Expected '{exp}'."
        raise ValueError(_err)
    return asc


def _feed(data: bytes, n: int) -> tuple[bytes, bytes]:
    if len(data) < n:
        _err = f'Data too short: want {n} bytes, got {len(data)} bytes'
        raise ValueError(_err)
    return data[:n], data[n:]


class SnapshotChannelData(BaseModel):
    channel: Channel
    reserved: bytes
    time_interval_ns: int
    samples: list[int]

    HEADER_SIZE: ClassVar[int] = 1 + 3 + 4
    DATA_SIZE: ClassVar[int] = 8000
    TOTAL_SIZE: ClassVar[int] = HEADER_SIZE + DATA_SIZE

    @classmethod
    def _validate_time_interval(cls, v: bytes) -> int:
        # IEEE 754 LE
        dt = _tofloat(v)
        if dt <= 0:
            _err = f'Invalid time interval: {dt}. Must be positive.'
            raise ValueError(_err)
        return round(dt * 1_000_000_000)

    @classmethod
    def _validate_channel(cls, v: bytes) -> Channel:
        if v == b'\x01':
            return Channel.CH1
        if v == b'\x02':
            return Channel.CH2
        _err = f'Invalid channel: {_tohex_bytes(v)}. Expected 0x01 or 0x02.'
        raise ValueError(_err)

    @classmethod
    def _validate_data(cls, v: bytes) -> list[int]:
        if len(v) != cls.DATA_SIZE:
            _err = f'Data must be {cls.DATA_SIZE} bytes, got {len(v)} bytes'
            raise ValueError(_err)
        ret = struct.unpack(f'>{int(cls.DATA_SIZE / 2)}h', v)
        if not all(isinstance(s, int) for s in ret) or not all(-32768 <= s <= 32767 for s in ret):
            _err = 'Invalid sample data: values must be 16-bit signed integers'
            raise ValueError(_err)
        return list(ret)

    @classmethod
    def from_bytes(cls, data: bytes) -> SnapshotChannelData:
        if len(data) < cls.TOTAL_SIZE:
            raise ValueError('Data is too short to contain a valid channel snapshot')
        time_interval_raw, data = _feed(data, 4)
        time_interval_ns = cls._validate_time_interval(time_interval_raw)
        channel_raw, data = _feed(data, 1)
        channel = cls._validate_channel(channel_raw)
        reserved, data = _feed(data, 3)
        channel_data_raw, data = _feed(data, cls.DATA_SIZE)
        samples = cls._validate_data(channel_data_raw)
        if data:
            log.warning(f'Extra data after parsing channel snapshot: {len(data)} bytes')
        return cls(
            time_interval_ns=time_interval_ns,
            channel=channel,
            reserved=reserved,
            samples=samples,
        )

    def print(self):
        log.info('Channel snapshot:')
        log.info(f'  Channel: {self.channel}')
        log.info(f'  Time interval: {self.time_interval_ns}ns')


class Snapshot(BaseModel):
    size_digit: int
    size: int
    channel: SnapshotChannelData

    HEADER_SIZE: ClassVar[int] = 1 + 1 + 4
    DATA_SIZE: ClassVar[int] = SnapshotChannelData.TOTAL_SIZE
    TOTAL_SIZE: ClassVar[int] = HEADER_SIZE + DATA_SIZE

    @classmethod
    def _validate_preamble(cls, v: bytes) -> None:
        _validate_ascii(v, 'preamble', ['#'])

    @classmethod
    def _validate_size_digit(cls, v: bytes) -> int:
        return int(_validate_ascii(v, 'size digit', ['4']))

    @classmethod
    def _validate_size(cls, v: bytes) -> int:
        return int(_validate_ascii(v, 'size', [str(SnapshotChannelData.TOTAL_SIZE)]))

    @classmethod
    def from_bytes(cls, data: bytes) -> Snapshot:
        if len(data) < cls.TOTAL_SIZE:
            _err = f'Data too short: want {cls.TOTAL_SIZE} bytes, got {len(data)} bytes'
            raise ValueError(_err)
        if len(data) > cls.TOTAL_SIZE:
            log.warning(f'Data too long: want {cls.TOTAL_SIZE} bytes, got {len(data)} bytes')
        preamble, data = _feed(data, 1)
        cls._validate_preamble(preamble)
        size_digit_raw, data = _feed(data, 1)
        size_digit = cls._validate_size_digit(size_digit_raw)
        size_raw, data = _feed(data, 4)
        size = cls._validate_size(size_raw)
        channel_raw, data = _feed(data, SnapshotChannelData.TOTAL_SIZE)
        channel = SnapshotChannelData.from_bytes(channel_raw)
        if data:
            log.warning(f'Extra data after parsing snapshot: {len(data)} bytes')
        return cls(
            size_digit=size_digit,
            size=size,
            channel=channel,
        )

    def print(self):
        log.info('Snapshot:')
        log.info(f'  Size digit: {self.size_digit}')
        log.info(f'  Size: {self.size}')
        self.channel.print()


class Device:
    serial: str
    port: Path
    device: Serial
    _printer: Hexprinter = Hexprinter()

    def __init__(self, serial: str):
        self.serial = serial
        self.port = self._find_port()
        self.device = Serial(self.port.as_posix(), 921600, timeout=1)
        self.reset()
        self.identification()
        self.start()

    def __del__(self):
        if self.device.is_open:
            self.device.close()

    def _find_port(self) -> Path:
        for port in list_ports.comports():
            if port.serial_number == SERIAL:
                path = Path(port.device)
                if not path.exists():
                    raise RuntimeError('Port not found')
                log.info(f'Found scope on {path}')
                return path
        raise RuntimeError('Scope not found')

    @overload
    def run(self, cmd: str, size: int | None = None, *, ascii: Literal[False] = False) -> bytes: ...

    @overload
    def run(self, cmd: str, size: int | None = None, *, ascii: Literal[True]) -> str: ...

    @overload
    def run(self, cmd: str, size: int | None = None, *, ascii: bool) -> str | bytes: ...

    def run(self, cmd: str, size: int | None = None, *, ascii: bool = False) -> str | bytes:
        start = time.perf_counter()
        self.device.write(cmd.encode('ascii') + b'\n')
        log.info(f'Sent command: {cmd}')

        data = self.device.read_until(b'\n') if size is None else self.device.read(size)

        end = time.perf_counter()
        log.info(f'Received {len(data)} bytes in {end - start:.3f} seconds')

        if ascii:
            if data.endswith(b'\n'):
                data = data[:-1]
            txt = map(self._try_ascii, data)
            return ''.join(txt)

        return data

    def run_print(self, cmd: str) -> str:
        ret = self.run(cmd, ascii=True)
        log.info(f'Response: {ret}')
        return ret.strip()

    def reset(self) -> None:
        self.run('*rst')

    def start(self) -> None:
        self.run(':run')

    def stop(self) -> None:
        self.run(':stop')

    def autoset(self) -> None:
        self.run(':autoset')
        time.sleep(2)

    def identification(self) -> str:
        return self.run_print('*idn?')

    def error(self) -> Error:
        data = self.run(':syst:err?', ascii=True)
        code, msg = data.split(',', 1)
        try:
            ret = Error(int(code))
        except ValueError:
            log.error(f'Unknown error code: {code}')
            raise RuntimeError(f'Unknown error code: {code}')
        if ret != Error.NoError:
            log.error(f'Scope error: {ret} ({msg})')
        return ret

    def ready(self, channel: Channel) -> bool:
        data = self.run(f':acquire{channel.value}:state?', ascii=True)
        return data.strip() == '1'

    def scale(self, channel: Channel) -> float:
        data = self.run(f':channel{channel.value}:scale?')
        return _tofloat(data.strip())

    def offset(self, channel: Channel) -> float:
        data = self.run(f':channel{channel.value}:offset?')
        return _tofloat(data.strip())

    def snap(self, channel: Channel) -> Snapshot:
        return Snapshot.from_bytes(
            self.run(
                f':acquire{channel.value}:memory?',
                size=Snapshot.TOTAL_SIZE,
            ),
        )

    def _try_ascii(self, data: int) -> str:
        if 32 <= data <= 126:
            return chr(data)
        return _tohex(data)


if __name__ == '__main__':
    scope = Device(SERIAL)
    # scope.ready(Channel.CH1)
    # log.info(f"CH1 scale: {scope.scale(Channel.CH1)} V/div")
    # log.info(f"CH1 offset: {scope.offset(Channel.CH1)} V")
    time.sleep(1)
    snap1 = scope.snap(Channel.CH1)
    snap1.print()
    snap2 = scope.snap(Channel.CH2)
    snap2.print()
    print(list(set(snap1.channel.samples)))
    print(list(set(snap2.channel.samples)))
