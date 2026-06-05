"""Generated wire models. Do not edit."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any


@dataclass(frozen=True)
class EnumValue:
    name: str
    value: int


@dataclass(frozen=True)
class EnumDesc:
    qualified_name: str
    underlying_type: str
    values: tuple[EnumValue, ...]

    def value_by_name(self, name: str) -> int:
        for value in self.values:
            if value.name == name:
                return value.value
        raise KeyError(name)

    def name_by_value(self, value: int) -> str:
        for item in self.values:
            if item.value == value:
                return item.name
        raise KeyError(value)


@dataclass(frozen=True)
class FieldDesc:
    name: str
    type_name: str
    kind: str
    default: Any | None
    width: int | None = None
    signed: bool | None = None
    enum_name: str | None = None
    model_name: str | None = None
    array_len: int | None = None
    element_type: str | None = None
    element_kind: str | None = None
    element_width: int | None = None
    element_signed: bool | None = None
    element_enum_name: str | None = None
    element_model_name: str | None = None


@dataclass(frozen=True)
class ModelDesc:
    qualified_name: str
    include_path: str
    fields: tuple[FieldDesc, ...]


class UnsupportedFieldError(ValueError):
    pass


def _coerce_bool(value: Any) -> bool:
    if isinstance(value, bool):
        return value
    if isinstance(value, int):
        return value != 0
    if isinstance(value, str):
        lowered = value.strip().lower()
        if lowered in {"1", "true", "yes", "on"}:
            return True
        if lowered in {"0", "false", "no", "off"}:
            return False
    raise ValueError(f"cannot parse bool value {value!r}")


def _coerce_int(value: Any) -> int:
    if isinstance(value, bool):
        return int(value)
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        return int(value.strip(), 0)
    raise ValueError(f"cannot parse integer value {value!r}")


def _encode_int(value: Any, width: int, signed: bool) -> bytes:
    number = _coerce_int(value)
    bits = width * 8
    if signed:
        low = -(1 << (bits - 1))
        high = (1 << (bits - 1)) - 1
    else:
        low = 0
        high = (1 << bits) - 1
    if number < low or number > high:
        raise OverflowError(f"{number} does not fit in {bits}-bit {'signed' if signed else 'unsigned'} storage")
    return number.to_bytes(width, "little", signed=signed)


def _enum_value(enum_name: str, value: Any) -> int:
    if isinstance(value, str):
        item = value.strip()
        if "::" in item:
            item = item.rsplit("::", 1)[1]
        try:
            return ENUMS[enum_name].value_by_name(item)
        except KeyError:
            pass
        return int(item, 0)
    return _coerce_int(value)


def default_value(field: FieldDesc) -> Any:
    if field.default is not None:
        return field.default
    if field.kind == "bool":
        return False
    if field.kind == "scalar":
        return 0
    if field.kind == "enum":
        enum = ENUMS[field.enum_name or ""]
        return enum.values[0].name if enum.values else None
    if field.kind == "array":
        return [default_array_element(field) for _ in range(field.array_len or 0)]
    if field.kind == "model":
        return default_model(field.model_name or "")
    return None


def default_array_element(field: FieldDesc) -> Any:
    if field.element_kind == "bool":
        return False
    if field.element_kind == "scalar":
        return 0
    if field.element_kind == "enum":
        enum = ENUMS[field.element_enum_name or ""]
        return enum.values[0].name if enum.values else None
    if field.element_kind == "model":
        return default_model(field.element_model_name or "")
    return None


def default_model(model_name: str) -> dict[str, Any]:
    model = MODELS[model_name]
    return {field.name: default_value(field) for field in model.fields}


def encode_model(model_name: str, values: dict[str, Any] | None = None) -> bytes:
    model = MODELS[model_name]
    merged = default_model(model_name)
    if values:
        merged.update(values)
    return b"".join(encode_field(field, merged.get(field.name)) for field in model.fields)


def encode_field(field: FieldDesc, value: Any) -> bytes:
    if field.kind == "bool":
        return bytes([1 if _coerce_bool(value) else 0])
    if field.kind == "scalar":
        return _encode_int(value, field.width or 0, bool(field.signed))
    if field.kind == "enum":
        return _encode_int(
            _enum_value(field.enum_name or "", value),
            field.width or 0,
            bool(field.signed),
        )
    if field.kind == "array":
        return _encode_array(field, value)
    if field.kind == "model":
        if not isinstance(value, dict):
            raise ValueError(f"model field {field.name} needs a dict value")
        return encode_model(field.model_name or "", value)
    raise UnsupportedFieldError(
        f"field {field.name} has unsupported wire type {field.type_name}"
    )


def _encode_array(field: FieldDesc, value: Any) -> bytes:
    if isinstance(value, str) and field.element_kind == "scalar":
        cleaned = "".join(value.split())
        values = list(bytes.fromhex(cleaned))
    elif isinstance(value, (bytes, bytearray)) and field.element_kind == "scalar":
        values = list(value)
    else:
        values = list(value or [])
    expected = field.array_len or 0
    if len(values) != expected:
        raise ValueError(f"array field {field.name} needs {expected} values, got {len(values)}")
    parts = []
    for item in values:
        if field.element_kind == "bool":
            parts.append(bytes([1 if _coerce_bool(item) else 0]))
        elif field.element_kind == "scalar":
            parts.append(_encode_int(item, field.element_width or 0, bool(field.element_signed)))
        elif field.element_kind == "enum":
            parts.append(
                _encode_int(
                    _enum_value(field.element_enum_name or "", item),
                    field.element_width or 0,
                    bool(field.element_signed),
                )
            )
        elif field.element_kind == "model":
            if not isinstance(item, dict):
                raise ValueError(f"array field {field.name} needs dict elements")
            parts.append(encode_model(field.element_model_name or "", item))
        else:
            raise UnsupportedFieldError(
                f"array field {field.name} has unsupported element type {field.element_type}"
            )
    return b"".join(parts)



ENUMS = {
    '::Totem::Audio::BeatEventKind': EnumDesc('::Totem::Audio::BeatEventKind', 'uint8_t', (EnumValue('ExpectedHit', 0), EnumValue('ExpectedMiss', 1), EnumValue('Reacquired', 2), EnumValue('Lost', 3),)),
    '::Totem::Audio::PeakGroup': EnumDesc('::Totem::Audio::PeakGroup', 'uint8_t', (EnumValue('Bass', 0), EnumValue('Mid', 1), EnumValue('High', 2),)),
    '::Totem::Buttons::ButtonEventType': EnumDesc('::Totem::Buttons::ButtonEventType', 'uint8_t', (EnumValue('Pressed', 0), EnumValue('Released', 1),)),
    '::Totem::Data::PubSub::NodeId': EnumDesc('::Totem::Data::PubSub::NodeId', 'uint8_t', (EnumValue('None', 0), EnumValue('Master', 1), EnumValue('Media', 2), EnumValue('InputOutput', 4), EnumValue('GPUNode0', 8), EnumValue('GPUNode1', 16), EnumValue('GPUNode2', 32), EnumValue('GPUNode3', 64), EnumValue('Host', 128),)),
    '::Totem::Data::PubSub::Topic': EnumDesc('::Totem::Data::PubSub::Topic', 'uint32_t', (EnumValue('None', 0), EnumValue('Heartbeat', 1), EnumValue('PubSub', 2), EnumValue('Wheel', 4), EnumValue('Beat', 8), EnumValue('FftFrame', 16), EnumValue('Power', 32), EnumValue('Logs', 64), EnumValue('Metrics', 128), EnumValue('Button', 256), EnumValue('AnimationPlay', 512), EnumValue('AnimationUpdate', 1024), EnumValue('AnimationStop', 2048), EnumValue('AnimationSetHueOffset', 4096), EnumValue('AnimationSetRotationOffset', 8192), EnumValue('AnimationSetBrightness', 16384), EnumValue('AnimationSetLayerActive', 32768), EnumValue('AnimationSetLayerOpacity', 65536), EnumValue('AnimationFadeLayerSwap', 131072), EnumValue('LedPwm', 262144), EnumValue('Peak', 524288),)),
    '::Totem::LedDisplay::AnimationKind': EnumDesc('::Totem::LedDisplay::AnimationKind', 'uint8_t', (EnumValue('None', 0), EnumValue('DiagnosticFill', 1), EnumValue('CenterWave', 2), EnumValue('SpectralWeave', 3), EnumValue('SpectralIris', 4), EnumValue('OrbitSparks', 5), EnumValue('StainedCells', 6), EnumValue('WheelIndicator', 7), EnumValue('SpokeSweep', 8), EnumValue('Sinelon', 9), EnumValue('SineWave', 10), EnumValue('Starburst', 11), EnumValue('Vortex', 12), EnumValue('Shutter', 13), EnumValue('OrbitRing', 14), EnumValue('Lighthouse', 15), EnumValue('Cymatic', 16), EnumValue('BreathingRings', 17), EnumValue('RadialCurtain', 18), EnumValue('PolarLattice', 19), EnumValue('Bolt', 20),)),
    '::Totem::LedDisplay::Layer': EnumDesc('::Totem::LedDisplay::Layer', 'uint8_t', (EnumValue('Background', 0), EnumValue('Fft', 1), EnumValue('FftAlt', 2), EnumValue('Effect', 3), EnumValue('TransientEffect', 4), EnumValue('Wheel', 5), EnumValue('Debug', 6),)),
    '::Totem::LedPwm::CommandEventType': EnumDesc('::Totem::LedPwm::CommandEventType', 'uint8_t', (EnumValue('None', 0), EnumValue('SetBrightness', 1), EnumValue('StartPulse', 2), EnumValue('StartGlitter', 3), EnumValue('ClearAnimations', 4),)),
    '::Totem::LedPwm::Curve': EnumDesc('::Totem::LedPwm::Curve', 'uint8_t', (EnumValue('Linear', 0), EnumValue('SmoothStep', 1),)),
    '::Totem::PubSubBackend::TrafficClass': EnumDesc('::Totem::PubSubBackend::TrafficClass', 'uint8_t', (EnumValue('Noncritical', 0), EnumValue('Critical', 1),)),
    '::Totem::PubSubBackend::detail::SubscribeEventType': EnumDesc('::Totem::PubSubBackend::detail::SubscribeEventType', 'uint8_t', (EnumValue('Register', 0), EnumValue('Unregister', 1),)),
    'PeripheralButton': EnumDesc('PeripheralButton', 'uint8_t', (EnumValue('Bell', 0), EnumValue('Calibration', 1),)),
    'PeripheralLed': EnumDesc('PeripheralLed', 'uint8_t', (EnumValue('Bulb1', 0), EnumValue('Bulb2', 1), EnumValue('Onboard', 2), EnumValue('PeakIndicator', 3),)),
}
MODELS = {
    '::PubSubTest::Message': ModelDesc('::PubSubTest::Message', 'Setups/PubSubTestMessage.hpp', (FieldDesc(name='flag', type_name='bool', kind='bool', default=None, width=1, signed=False), FieldDesc(name='intVal', type_name='int', kind='scalar', default=None, width=4, signed=True), FieldDesc(name='uint32Val', type_name='unsigned int', kind='scalar', default=None, width=4, signed=False), FieldDesc(name='uint16Val', type_name='unsigned short', kind='scalar', default=None, width=2, signed=False), FieldDesc(name='uint8Val', type_name='unsigned char', kind='scalar', default=None, width=1, signed=False), FieldDesc(name='strVal', type_name='std::array<char, 32>', kind='array', default=None, array_len=32, element_type='char', element_kind='scalar', element_width=1, element_signed=True), FieldDesc(name='byteArrayVal', type_name='std::array<std::byte, 16>', kind='array', default=None, array_len=16, element_type='std::byte', element_kind='scalar', element_width=1, element_signed=False),)),
    '::Totem::Audio::BeatEvent': ModelDesc('::Totem::Audio::BeatEvent', 'Audio/Interfaces/Wire.hpp', (FieldDesc(name='kind', type_name='::Totem::Audio::BeatEventKind', kind='enum', default=None, width=1, signed=False, enum_name='::Totem::Audio::BeatEventKind'), FieldDesc(name='bpm', type_name='unsigned char', kind='scalar', default=None, width=1, signed=False), FieldDesc(name='confidence', type_name='unsigned char', kind='scalar', default=None, width=1, signed=False), FieldDesc(name='energy', type_name='unsigned char', kind='scalar', default=None, width=1, signed=False), FieldDesc(name='sequence', type_name='unsigned int', kind='scalar', default=None, width=4, signed=False),)),
    '::Totem::Audio::FftFrame': ModelDesc('::Totem::Audio::FftFrame', 'Audio/Interfaces/Wire.hpp', (FieldDesc(name='subBass', type_name='unsigned short', kind='scalar', default=None, width=2, signed=False), FieldDesc(name='bass', type_name='unsigned short', kind='scalar', default=None, width=2, signed=False), FieldDesc(name='lowMid', type_name='unsigned short', kind='scalar', default=None, width=2, signed=False), FieldDesc(name='mid', type_name='unsigned short', kind='scalar', default=None, width=2, signed=False), FieldDesc(name='highMid', type_name='unsigned short', kind='scalar', default=None, width=2, signed=False), FieldDesc(name='presence', type_name='unsigned short', kind='scalar', default=None, width=2, signed=False), FieldDesc(name='brilliance', type_name='unsigned short', kind='scalar', default=None, width=2, signed=False), FieldDesc(name='air', type_name='unsigned short', kind='scalar', default=None, width=2, signed=False),)),
    '::Totem::Audio::PeakEvent': ModelDesc('::Totem::Audio::PeakEvent', 'Audio/Interfaces/Wire.hpp', (FieldDesc(name='group', type_name='::Totem::Audio::PeakGroup', kind='enum', default=None, width=1, signed=False, enum_name='::Totem::Audio::PeakGroup'), FieldDesc(name='energy', type_name='unsigned char', kind='scalar', default=None, width=1, signed=False), FieldDesc(name='lowerBand', type_name='unsigned char', kind='scalar', default=None, width=1, signed=False), FieldDesc(name='upperBand', type_name='unsigned char', kind='scalar', default=None, width=1, signed=False), FieldDesc(name='frameSequence', type_name='unsigned int', kind='scalar', default=None, width=4, signed=False),)),
    '::Totem::Buttons::ButtonEvent': ModelDesc('::Totem::Buttons::ButtonEvent', 'Buttons/Interfaces/Wire.hpp', (FieldDesc(name='type', type_name='::Totem::Buttons::ButtonEventType', kind='enum', default=None, width=1, signed=False, enum_name='::Totem::Buttons::ButtonEventType'), FieldDesc(name='button', type_name='PeripheralButton', kind='enum', default=None, width=1, signed=False, enum_name='PeripheralButton'),)),
    '::Totem::LedDisplay::AnimationFadeLayerSwapCommand': ModelDesc('::Totem::LedDisplay::AnimationFadeLayerSwapCommand', 'LedDisplay/Interfaces/AnimationCommand.hpp', (FieldDesc(name='first', type_name='::Totem::LedDisplay::Layer', kind='enum', default='Fft', width=1, signed=False, enum_name='::Totem::LedDisplay::Layer'), FieldDesc(name='second', type_name='::Totem::LedDisplay::Layer', kind='enum', default='FftAlt', width=1, signed=False, enum_name='::Totem::LedDisplay::Layer'), FieldDesc(name='durationMs', type_name='unsigned short', kind='scalar', default=10000, width=2, signed=False),)),
    '::Totem::LedDisplay::AnimationPlayCommand': ModelDesc('::Totem::LedDisplay::AnimationPlayCommand', 'LedDisplay/Interfaces/AnimationCommand.hpp', (FieldDesc(name='kind', type_name='::Totem::LedDisplay::AnimationKind', kind='enum', default='None', width=1, signed=False, enum_name='::Totem::LedDisplay::AnimationKind'), FieldDesc(name='requestId', type_name='unsigned short', kind='scalar', default=0, width=2, signed=False), FieldDesc(name='layer', type_name='::Totem::LedDisplay::Layer', kind='enum', default='Effect', width=1, signed=False, enum_name='::Totem::LedDisplay::Layer'), FieldDesc(name='lifetimeMs', type_name='unsigned short', kind='scalar', default=1200, width=2, signed=False), FieldDesc(name='payloadSize', type_name='unsigned char', kind='scalar', default=0, width=1, signed=False), FieldDesc(name='payload', type_name='std::array<std::byte, 32>', kind='array', default=None, array_len=32, element_type='std::byte', element_kind='scalar', element_width=1, element_signed=False),)),
    '::Totem::LedDisplay::AnimationSetBrightnessCommand': ModelDesc('::Totem::LedDisplay::AnimationSetBrightnessCommand', 'LedDisplay/Interfaces/AnimationCommand.hpp', (FieldDesc(name='value', type_name='unsigned char', kind='scalar', default=0, width=1, signed=False),)),
    '::Totem::LedDisplay::AnimationSetHueOffsetCommand': ModelDesc('::Totem::LedDisplay::AnimationSetHueOffsetCommand', 'LedDisplay/Interfaces/AnimationCommand.hpp', (FieldDesc(name='offset', type_name='unsigned char', kind='scalar', default=0, width=1, signed=False),)),
    '::Totem::LedDisplay::AnimationSetLayerActiveCommand': ModelDesc('::Totem::LedDisplay::AnimationSetLayerActiveCommand', 'LedDisplay/Interfaces/AnimationCommand.hpp', (FieldDesc(name='layer', type_name='::Totem::LedDisplay::Layer', kind='enum', default='Effect', width=1, signed=False, enum_name='::Totem::LedDisplay::Layer'), FieldDesc(name='active', type_name='bool', kind='bool', default=True, width=1, signed=False),)),
    '::Totem::LedDisplay::AnimationSetLayerOpacityCommand': ModelDesc('::Totem::LedDisplay::AnimationSetLayerOpacityCommand', 'LedDisplay/Interfaces/AnimationCommand.hpp', (FieldDesc(name='layer', type_name='::Totem::LedDisplay::Layer', kind='enum', default='Effect', width=1, signed=False, enum_name='::Totem::LedDisplay::Layer'), FieldDesc(name='opacity', type_name='unsigned char', kind='scalar', default=255, width=1, signed=False),)),
    '::Totem::LedDisplay::AnimationSetRotationOffsetCommand': ModelDesc('::Totem::LedDisplay::AnimationSetRotationOffsetCommand', 'LedDisplay/Interfaces/AnimationCommand.hpp', (FieldDesc(name='offset', type_name='unsigned char', kind='scalar', default=0, width=1, signed=False),)),
    '::Totem::LedDisplay::AnimationStopCommand': ModelDesc('::Totem::LedDisplay::AnimationStopCommand', 'LedDisplay/Interfaces/AnimationCommand.hpp', (FieldDesc(name='requestId', type_name='unsigned short', kind='scalar', default=0, width=2, signed=False),)),
    '::Totem::LedDisplay::AnimationUpdateCommand': ModelDesc('::Totem::LedDisplay::AnimationUpdateCommand', 'LedDisplay/Interfaces/AnimationCommand.hpp', (FieldDesc(name='kind', type_name='::Totem::LedDisplay::AnimationKind', kind='enum', default='None', width=1, signed=False, enum_name='::Totem::LedDisplay::AnimationKind'), FieldDesc(name='requestId', type_name='unsigned short', kind='scalar', default=0, width=2, signed=False), FieldDesc(name='payloadSize', type_name='unsigned char', kind='scalar', default=0, width=1, signed=False), FieldDesc(name='payload', type_name='std::array<std::byte, 32>', kind='array', default=None, array_len=32, element_type='std::byte', element_kind='scalar', element_width=1, element_signed=False),)),
    '::Totem::LedDisplay::Animations::BoltConfig': ModelDesc('::Totem::LedDisplay::Animations::BoltConfig', 'LedDisplay/Animations/Bolt/Config.hpp', (FieldDesc(name='hue', type_name='unsigned char', kind='scalar', default=24, width=1, signed=False), FieldDesc(name='saturation', type_name='unsigned char', kind='scalar', default=255, width=1, signed=False), FieldDesc(name='value', type_name='unsigned char', kind='scalar', default=255, width=1, signed=False), FieldDesc(name='width', type_name='unsigned char', kind='scalar', default=1, width=1, signed=False), FieldDesc(name='jitter', type_name='unsigned char', kind='scalar', default=1, width=1, signed=False), FieldDesc(name='forks', type_name='unsigned char', kind='scalar', default=1, width=1, signed=False), FieldDesc(name='seed', type_name='unsigned char', kind='scalar', default=0, width=1, signed=False), FieldDesc(name='outerOrigin', type_name='bool', kind='bool', default=True, width=1, signed=False),)),
    '::Totem::LedDisplay::Animations::BreathingRingsConfig': ModelDesc('::Totem::LedDisplay::Animations::BreathingRingsConfig', 'LedDisplay/Animations/BreathingRings/Config.hpp', (FieldDesc(name='hue', type_name='unsigned char', kind='scalar', default=112, width=1, signed=False), FieldDesc(name='saturation', type_name='unsigned char', kind='scalar', default=255, width=1, signed=False), FieldDesc(name='value', type_name='unsigned char', kind='scalar', default=170, width=1, signed=False), FieldDesc(name='spacing', type_name='unsigned char', kind='scalar', default=8, width=1, signed=False), FieldDesc(name='width', type_name='unsigned char', kind='scalar', default=3, width=1, signed=False), FieldDesc(name='cycles', type_name='unsigned char', kind='scalar', default=1, width=1, signed=False), FieldDesc(name='direction', type_name='unsigned char', kind='scalar', default=0, width=1, signed=False), FieldDesc(name='hueStep', type_name='unsigned char', kind='scalar', default=8, width=1, signed=False),)),
    '::Totem::LedDisplay::Animations::CenterWaveConfig': ModelDesc('::Totem::LedDisplay::Animations::CenterWaveConfig', 'LedDisplay/Animations/CenterWave/Config.hpp', (FieldDesc(name='hue', type_name='unsigned char', kind='scalar', default=144, width=1, signed=False), FieldDesc(name='saturation', type_name='unsigned char', kind='scalar', default=255, width=1, signed=False), FieldDesc(name='value', type_name='unsigned char', kind='scalar', default=180, width=1, signed=False), FieldDesc(name='rise', type_name='unsigned char', kind='scalar', default=2, width=1, signed=False), FieldDesc(name='peak', type_name='unsigned char', kind='scalar', default=2, width=1, signed=False), FieldDesc(name='wake', type_name='unsigned char', kind='scalar', default=5, width=1, signed=False), FieldDesc(name='peakDelta', type_name='unsigned char', kind='scalar', default=0, width=1, signed=False), FieldDesc(name='speedDelta', type_name='unsigned char', kind='scalar', default=0, width=1, signed=False), FieldDesc(name='spokeModulo', type_name='unsigned char', kind='scalar', default=1, width=1, signed=False),)),
    '::Totem::LedDisplay::Animations::CymaticConfig': ModelDesc('::Totem::LedDisplay::Animations::CymaticConfig', 'LedDisplay/Animations/Cymatic/Config.hpp', (FieldDesc(name='hue', type_name='unsigned char', kind='scalar', default=176, width=1, signed=False), FieldDesc(name='saturation', type_name='unsigned char', kind='scalar', default=240, width=1, signed=False), FieldDesc(name='value', type_name='unsigned char', kind='scalar', default=180, width=1, signed=False), FieldDesc(name='sourceMode', type_name='unsigned char', kind='scalar', default=0, width=1, signed=False), FieldDesc(name='wavelength', type_name='unsigned char', kind='scalar', default=36, width=1, signed=False), FieldDesc(name='speed', type_name='unsigned char', kind='scalar', default=96, width=1, signed=False), FieldDesc(name='contrast', type_name='unsigned char', kind='scalar', default=180, width=1, signed=False), FieldDesc(name='hueStep', type_name='unsigned char', kind='scalar', default=16, width=1, signed=False),)),
    '::Totem::LedDisplay::Animations::DiagnosticFillConfig': ModelDesc('::Totem::LedDisplay::Animations::DiagnosticFillConfig', 'LedDisplay/Animations/DiagnosticFill/Config.hpp', (FieldDesc(name='hue', type_name='unsigned char', kind='scalar', default=0, width=1, signed=False), FieldDesc(name='saturation', type_name='unsigned char', kind='scalar', default=0, width=1, signed=False), FieldDesc(name='value', type_name='unsigned char', kind='scalar', default=48, width=1, signed=False),)),
    '::Totem::LedDisplay::Animations::LighthouseConfig': ModelDesc('::Totem::LedDisplay::Animations::LighthouseConfig', 'LedDisplay/Animations/Lighthouse/Config.hpp', (FieldDesc(name='hue', type_name='unsigned char', kind='scalar', default=144, width=1, signed=False), FieldDesc(name='saturation', type_name='unsigned char', kind='scalar', default=255, width=1, signed=False), FieldDesc(name='value', type_name='unsigned char', kind='scalar', default=220, width=1, signed=False), FieldDesc(name='beamWidth', type_name='unsigned char', kind='scalar', default=3, width=1, signed=False), FieldDesc(name='trailSpokes', type_name='unsigned char', kind='scalar', default=4, width=1, signed=False), FieldDesc(name='cycles', type_name='unsigned char', kind='scalar', default=1, width=1, signed=False), FieldDesc(name='innerRing', type_name='unsigned char', kind='scalar', default=0, width=1, signed=False), FieldDesc(name='outerRing', type_name='unsigned char', kind='scalar', default=0, width=1, signed=False),)),
    '::Totem::LedDisplay::Animations::OrbitRingConfig': ModelDesc('::Totem::LedDisplay::Animations::OrbitRingConfig', 'LedDisplay/Animations/OrbitRing/Config.hpp', (FieldDesc(name='hue', type_name='unsigned char', kind='scalar', default=96, width=1, signed=False), FieldDesc(name='saturation', type_name='unsigned char', kind='scalar', default=255, width=1, signed=False), FieldDesc(name='value', type_name='unsigned char', kind='scalar', default=220, width=1, signed=False), FieldDesc(name='radius', type_name='unsigned char', kind='scalar', default=128, width=1, signed=False), FieldDesc(name='radialWidth', type_name='unsigned char', kind='scalar', default=36, width=1, signed=False), FieldDesc(name='angularWidth', type_name='unsigned char', kind='scalar', default=28, width=1, signed=False), FieldDesc(name='comets', type_name='unsigned char', kind='scalar', default=2, width=1, signed=False), FieldDesc(name='laps', type_name='unsigned char', kind='scalar', default=1, width=1, signed=False), FieldDesc(name='trail', type_name='unsigned char', kind='scalar', default=56, width=1, signed=False), FieldDesc(name='sparkle', type_name='unsigned char', kind='scalar', default=220, width=1, signed=False), FieldDesc(name='hueJitter', type_name='unsigned char', kind='scalar', default=24, width=1, signed=False), FieldDesc(name='radialDrift', type_name='unsigned char', kind='scalar', default=48, width=1, signed=False), FieldDesc(name='radialDirection', type_name='unsigned char', kind='scalar', default=1, width=1, signed=False),)),
    '::Totem::LedDisplay::Animations::OrbitSparksConfig': ModelDesc('::Totem::LedDisplay::Animations::OrbitSparksConfig', 'LedDisplay/Animations/OrbitSparks/Config.hpp', (FieldDesc(name='baseHue', type_name='unsigned char', kind='scalar', default=32, width=1, signed=False), FieldDesc(name='saturation', type_name='unsigned char', kind='scalar', default=255, width=1, signed=False), FieldDesc(name='value', type_name='unsigned char', kind='scalar', default=230, width=1, signed=False), FieldDesc(name='sparkCount', type_name='unsigned char', kind='scalar', default=32, width=1, signed=False), FieldDesc(name='sparkSize', type_name='unsigned char', kind='scalar', default=1, width=1, signed=False), FieldDesc(name='orbitSpeed', type_name='unsigned char', kind='scalar', default=32, width=1, signed=False), FieldDesc(name='radialDrift', type_name='unsigned char', kind='scalar', default=96, width=1, signed=False), FieldDesc(name='highSparkle', type_name='unsigned char', kind='scalar', default=160, width=1, signed=False), FieldDesc(name='peakSensitivity', type_name='unsigned char', kind='scalar', default=128, width=1, signed=False), FieldDesc(name='seed', type_name='unsigned char', kind='scalar', default=165, width=1, signed=False), FieldDesc(name='hueModulation', type_name='unsigned char', kind='scalar', default=160, width=1, signed=False),)),
    '::Totem::LedDisplay::Animations::PolarLatticeConfig': ModelDesc('::Totem::LedDisplay::Animations::PolarLatticeConfig', 'LedDisplay/Animations/PolarLattice/Config.hpp', (FieldDesc(name='hue', type_name='unsigned char', kind='scalar', default=64, width=1, signed=False), FieldDesc(name='saturation', type_name='unsigned char', kind='scalar', default=255, width=1, signed=False), FieldDesc(name='value', type_name='unsigned char', kind='scalar', default=170, width=1, signed=False), FieldDesc(name='radialMode', type_name='unsigned char', kind='scalar', default=4, width=1, signed=False), FieldDesc(name='angularMode', type_name='unsigned char', kind='scalar', default=3, width=1, signed=False), FieldDesc(name='speed', type_name='unsigned char', kind='scalar', default=96, width=1, signed=False), FieldDesc(name='mix', type_name='unsigned char', kind='scalar', default=128, width=1, signed=False), FieldDesc(name='contrast', type_name='unsigned char', kind='scalar', default=160, width=1, signed=False),)),
    '::Totem::LedDisplay::Animations::RadialCurtainConfig': ModelDesc('::Totem::LedDisplay::Animations::RadialCurtainConfig', 'LedDisplay/Animations/RadialCurtain/Config.hpp', (FieldDesc(name='hue', type_name='unsigned char', kind='scalar', default=200, width=1, signed=False), FieldDesc(name='saturation', type_name='unsigned char', kind='scalar', default=220, width=1, signed=False), FieldDesc(name='value', type_name='unsigned char', kind='scalar', default=190, width=1, signed=False), FieldDesc(name='width', type_name='unsigned char', kind='scalar', default=4, width=1, signed=False), FieldDesc(name='tilt', type_name='unsigned char', kind='scalar', default=32, width=1, signed=False), FieldDesc(name='speed', type_name='unsigned char', kind='scalar', default=128, width=1, signed=False), FieldDesc(name='outerOrigin', type_name='bool', kind='bool', default=False, width=1, signed=False), FieldDesc(name='spokePhase', type_name='unsigned char', kind='scalar', default=16, width=1, signed=False),)),
    '::Totem::LedDisplay::Animations::ShutterConfig': ModelDesc('::Totem::LedDisplay::Animations::ShutterConfig', 'LedDisplay/Animations/Shutter/Config.hpp', (FieldDesc(name='hue', type_name='unsigned char', kind='scalar', default=48, width=1, signed=False), FieldDesc(name='saturation', type_name='unsigned char', kind='scalar', default=255, width=1, signed=False), FieldDesc(name='value', type_name='unsigned char', kind='scalar', default=210, width=1, signed=False), FieldDesc(name='segments', type_name='unsigned char', kind='scalar', default=8, width=1, signed=False), FieldDesc(name='openPct', type_name='unsigned char', kind='scalar', default=128, width=1, signed=False), FieldDesc(name='edgeWidth', type_name='unsigned char', kind='scalar', default=48, width=1, signed=False), FieldDesc(name='rotationCycles', type_name='unsigned char', kind='scalar', default=1, width=1, signed=False), FieldDesc(name='mode', type_name='unsigned char', kind='scalar', default=1, width=1, signed=False),)),
    '::Totem::LedDisplay::Animations::SineWaveConfig': ModelDesc('::Totem::LedDisplay::Animations::SineWaveConfig', 'LedDisplay/Animations/SineWave/Config.hpp', (FieldDesc(name='hue', type_name='unsigned char', kind='scalar', default=96, width=1, signed=False), FieldDesc(name='saturation', type_name='unsigned char', kind='scalar', default=192, width=1, signed=False), FieldDesc(name='value', type_name='unsigned char', kind='scalar', default=120, width=1, signed=False), FieldDesc(name='baseValue', type_name='unsigned char', kind='scalar', default=0, width=1, signed=False), FieldDesc(name='width', type_name='unsigned char', kind='scalar', default=1, width=1, signed=False), FieldDesc(name='durationMs', type_name='unsigned short', kind='scalar', default=3200, width=2, signed=False), FieldDesc(name='wavelength', type_name='unsigned char', kind='scalar', default=8, width=1, signed=False), FieldDesc(name='outerOrigin', type_name='bool', kind='bool', default=False, width=1, signed=False), FieldDesc(name='travelRings', type_name='unsigned char', kind='scalar', default=0, width=1, signed=False), FieldDesc(name='spokeGainPct', type_name='unsigned short', kind='scalar', default=100, width=2, signed=False), FieldDesc(name='tailDecay', type_name='unsigned char', kind='scalar', default=8, width=1, signed=False), FieldDesc(name='peakHold', type_name='unsigned char', kind='scalar', default=160, width=1, signed=False),)),
    '::Totem::LedDisplay::Animations::SinelonConfig': ModelDesc('::Totem::LedDisplay::Animations::SinelonConfig', 'LedDisplay/Animations/Sinelon/Config.hpp', (FieldDesc(name='hue', type_name='unsigned char', kind='scalar', default=96, width=1, signed=False), FieldDesc(name='saturation', type_name='unsigned char', kind='scalar', default=255, width=1, signed=False), FieldDesc(name='value', type_name='unsigned char', kind='scalar', default=220, width=1, signed=False), FieldDesc(name='width', type_name='unsigned char', kind='scalar', default=3, width=1, signed=False), FieldDesc(name='periodMs', type_name='unsigned short', kind='scalar', default=2400, width=2, signed=False), FieldDesc(name='outerOrigin', type_name='bool', kind='bool', default=False, width=1, signed=False), FieldDesc(name='travelRings', type_name='unsigned char', kind='scalar', default=0, width=1, signed=False), FieldDesc(name='bounceAttenuation', type_name='unsigned char', kind='scalar', default=255, width=1, signed=False), FieldDesc(name='spokeGainPct', type_name='unsigned short', kind='scalar', default=100, width=2, signed=False), FieldDesc(name='spokeGainPhaseStep', type_name='unsigned char', kind='scalar', default=64, width=1, signed=False),)),
    '::Totem::LedDisplay::Animations::SpectralIrisConfig': ModelDesc('::Totem::LedDisplay::Animations::SpectralIrisConfig', 'LedDisplay/Animations/SpectralIris/Config.hpp', (FieldDesc(name='baseHue', type_name='unsigned char', kind='scalar', default=96, width=1, signed=False), FieldDesc(name='saturation', type_name='unsigned char', kind='scalar', default=255, width=1, signed=False), FieldDesc(name='value', type_name='unsigned char', kind='scalar', default=220, width=1, signed=False), FieldDesc(name='baseValue', type_name='unsigned char', kind='scalar', default=8, width=1, signed=False), FieldDesc(name='petals', type_name='unsigned char', kind='scalar', default=8, width=1, signed=False), FieldDesc(name='aperture', type_name='unsigned char', kind='scalar', default=128, width=1, signed=False), FieldDesc(name='rimWidth', type_name='unsigned char', kind='scalar', default=28, width=1, signed=False), FieldDesc(name='contrast', type_name='unsigned char', kind='scalar', default=180, width=1, signed=False), FieldDesc(name='peakSensitivity', type_name='unsigned char', kind='scalar', default=96, width=1, signed=False), FieldDesc(name='flowSpeed', type_name='unsigned char', kind='scalar', default=24, width=1, signed=False), FieldDesc(name='hueModulation', type_name='unsigned char', kind='scalar', default=128, width=1, signed=False),)),
    '::Totem::LedDisplay::Animations::SpectralWeaveConfig': ModelDesc('::Totem::LedDisplay::Animations::SpectralWeaveConfig', 'LedDisplay/Animations/SpectralWeave/Config.hpp', (FieldDesc(name='baseHue', type_name='unsigned char', kind='scalar', default=144, width=1, signed=False), FieldDesc(name='saturation', type_name='unsigned char', kind='scalar', default=255, width=1, signed=False), FieldDesc(name='value', type_name='unsigned char', kind='scalar', default=220, width=1, signed=False), FieldDesc(name='baseValue', type_name='unsigned char', kind='scalar', default=20, width=1, signed=False), FieldDesc(name='radialMode', type_name='unsigned char', kind='scalar', default=2, width=1, signed=False), FieldDesc(name='angularMode', type_name='unsigned char', kind='scalar', default=3, width=1, signed=False), FieldDesc(name='symmetry', type_name='unsigned char', kind='scalar', default=1, width=1, signed=False), FieldDesc(name='contrast', type_name='unsigned char', kind='scalar', default=180, width=1, signed=False), FieldDesc(name='peakSensitivity', type_name='unsigned char', kind='scalar', default=96, width=1, signed=False), FieldDesc(name='flowSpeed', type_name='unsigned char', kind='scalar', default=40, width=1, signed=False), FieldDesc(name='hueModulation', type_name='unsigned char', kind='scalar', default=192, width=1, signed=False),)),
    '::Totem::LedDisplay::Animations::SpokeSweepConfig': ModelDesc('::Totem::LedDisplay::Animations::SpokeSweepConfig', 'LedDisplay/Animations/SpokeSweep/Config.hpp', (FieldDesc(name='baseHue', type_name='unsigned char', kind='scalar', default=0, width=1, signed=False), FieldDesc(name='hueStride', type_name='unsigned char', kind='scalar', default=16, width=1, signed=False), FieldDesc(name='value', type_name='unsigned char', kind='scalar', default=220, width=1, signed=False), FieldDesc(name='trailSpokes', type_name='unsigned char', kind='scalar', default=2, width=1, signed=False), FieldDesc(name='cycles', type_name='unsigned char', kind='scalar', default=1, width=1, signed=False), FieldDesc(name='markerValue', type_name='unsigned char', kind='scalar', default=255, width=1, signed=False), FieldDesc(name='useMarkers', type_name='bool', kind='bool', default=True, width=1, signed=False),)),
    '::Totem::LedDisplay::Animations::StainedCellsConfig': ModelDesc('::Totem::LedDisplay::Animations::StainedCellsConfig', 'LedDisplay/Animations/StainedCells/Config.hpp', (FieldDesc(name='baseHue', type_name='unsigned char', kind='scalar', default=160, width=1, signed=False), FieldDesc(name='saturation', type_name='unsigned char', kind='scalar', default=245, width=1, signed=False), FieldDesc(name='value', type_name='unsigned char', kind='scalar', default=210, width=1, signed=False), FieldDesc(name='baseValue', type_name='unsigned char', kind='scalar', default=10, width=1, signed=False), FieldDesc(name='seedCount', type_name='unsigned char', kind='scalar', default=6, width=1, signed=False), FieldDesc(name='borderWidth', type_name='unsigned char', kind='scalar', default=28, width=1, signed=False), FieldDesc(name='interiorValue', type_name='unsigned char', kind='scalar', default=64, width=1, signed=False), FieldDesc(name='driftSpeed', type_name='unsigned char', kind='scalar', default=16, width=1, signed=False), FieldDesc(name='contrast', type_name='unsigned char', kind='scalar', default=180, width=1, signed=False), FieldDesc(name='peakSensitivity', type_name='unsigned char', kind='scalar', default=96, width=1, signed=False), FieldDesc(name='seed', type_name='unsigned char', kind='scalar', default=61, width=1, signed=False), FieldDesc(name='hueModulation', type_name='unsigned char', kind='scalar', default=144, width=1, signed=False),)),
    '::Totem::LedDisplay::Animations::StarburstConfig': ModelDesc('::Totem::LedDisplay::Animations::StarburstConfig', 'LedDisplay/Animations/Starburst/Config.hpp', (FieldDesc(name='hue', type_name='unsigned char', kind='scalar', default=32, width=1, signed=False), FieldDesc(name='saturation', type_name='unsigned char', kind='scalar', default=255, width=1, signed=False), FieldDesc(name='value', type_name='unsigned char', kind='scalar', default=220, width=1, signed=False), FieldDesc(name='rise', type_name='unsigned char', kind='scalar', default=1, width=1, signed=False), FieldDesc(name='peak', type_name='unsigned char', kind='scalar', default=2, width=1, signed=False), FieldDesc(name='wake', type_name='unsigned char', kind='scalar', default=6, width=1, signed=False), FieldDesc(name='points', type_name='unsigned char', kind='scalar', default=4, width=1, signed=False), FieldDesc(name='pointGain', type_name='unsigned char', kind='scalar', default=2, width=1, signed=False), FieldDesc(name='twist', type_name='unsigned char', kind='scalar', default=0, width=1, signed=False), FieldDesc(name='cycles', type_name='unsigned char', kind='scalar', default=1, width=1, signed=False),)),
    '::Totem::LedDisplay::Animations::VortexConfig': ModelDesc('::Totem::LedDisplay::Animations::VortexConfig', 'LedDisplay/Animations/Vortex/Config.hpp', (FieldDesc(name='hue', type_name='unsigned char', kind='scalar', default=160, width=1, signed=False), FieldDesc(name='saturation', type_name='unsigned char', kind='scalar', default=255, width=1, signed=False), FieldDesc(name='value', type_name='unsigned char', kind='scalar', default=220, width=1, signed=False), FieldDesc(name='arms', type_name='unsigned char', kind='scalar', default=3, width=1, signed=False), FieldDesc(name='twist', type_name='unsigned char', kind='scalar', default=5, width=1, signed=False), FieldDesc(name='width', type_name='unsigned char', kind='scalar', default=128, width=1, signed=False), FieldDesc(name='cycles', type_name='unsigned char', kind='scalar', default=1, width=1, signed=False), FieldDesc(name='hueStep', type_name='unsigned char', kind='scalar', default=24, width=1, signed=False),)),
    '::Totem::LedDisplay::Animations::WheelIndicatorConfig': ModelDesc('::Totem::LedDisplay::Animations::WheelIndicatorConfig', 'LedDisplay/Animations/WheelIndicator/Config.hpp', (FieldDesc(name='hue', type_name='unsigned char', kind='scalar', default=160, width=1, signed=False), FieldDesc(name='saturation', type_name='unsigned char', kind='scalar', default=255, width=1, signed=False), FieldDesc(name='value', type_name='unsigned char', kind='scalar', default=96, width=1, signed=False), FieldDesc(name='spokes', type_name='unsigned char', kind='scalar', default=3, width=1, signed=False), FieldDesc(name='falloff', type_name='unsigned char', kind='scalar', default=1, width=1, signed=False),)),
    '::Totem::LedPwm::CommandEvent': ModelDesc('::Totem::LedPwm::CommandEvent', 'LedPwm/Interfaces/CommandEvent.hpp', (FieldDesc(name='led', type_name='PeripheralLed', kind='enum', default='Bulb1', width=1, signed=False, enum_name='PeripheralLed'), FieldDesc(name='type', type_name='::Totem::LedPwm::CommandEventType', kind='enum', default='None', width=1, signed=False, enum_name='::Totem::LedPwm::CommandEventType'), FieldDesc(name='brightness', type_name='::Totem::LedPwm::Brightness', kind='scalar', default=None, width=2, signed=False), FieldDesc(name='pulse', type_name='::Totem::LedPwm::Pulse', kind='model', default=None, model_name='::Totem::LedPwm::Pulse'), FieldDesc(name='glitter', type_name='::Totem::LedPwm::Glitter', kind='model', default=None, model_name='::Totem::LedPwm::Glitter'),)),
    '::Totem::LedPwm::Glitter': ModelDesc('::Totem::LedPwm::Glitter', 'LedPwm/Interfaces/Types.hpp', (FieldDesc(name='base', type_name='::Totem::LedPwm::Brightness', kind='scalar', default=None, width=2, signed=False), FieldDesc(name='glimmerPeak', type_name='::Totem::LedPwm::Brightness', kind='scalar', default=None, width=2, signed=False), FieldDesc(name='sparklePeak', type_name='::Totem::LedPwm::Brightness', kind='scalar', default=None, width=2, signed=False), FieldDesc(name='stepMs', type_name='unsigned short', kind='scalar', default=120, width=2, signed=False), FieldDesc(name='sparkleMs', type_name='unsigned short', kind='scalar', default=28, width=2, signed=False), FieldDesc(name='sparkleChance', type_name='unsigned char', kind='scalar', default=36, width=1, signed=False), FieldDesc(name='seed', type_name='unsigned short', kind='scalar', default=1, width=2, signed=False),)),
    '::Totem::LedPwm::Pulse': ModelDesc('::Totem::LedPwm::Pulse', 'LedPwm/Interfaces/Types.hpp', (FieldDesc(name='peak', type_name='::Totem::LedPwm::Brightness', kind='scalar', default=None, width=2, signed=False), FieldDesc(name='riseMs', type_name='unsigned int', kind='scalar', default=50, width=4, signed=False), FieldDesc(name='holdMs', type_name='unsigned int', kind='scalar', default=0, width=4, signed=False), FieldDesc(name='fallMs', type_name='unsigned int', kind='scalar', default=150, width=4, signed=False), FieldDesc(name='curve', type_name='::Totem::LedPwm::Curve', kind='enum', default='SmoothStep', width=1, signed=False, enum_name='::Totem::LedPwm::Curve'),)),
    '::Totem::PubSubBackend::Header': ModelDesc('::Totem::PubSubBackend::Header', 'PubSubBackend/Interfaces/Wire.hpp', (FieldDesc(name='timestampMs', type_name='unsigned int', kind='scalar', default=None, width=4, signed=False), FieldDesc(name='timestampUs', type_name='unsigned long long', kind='scalar', default=None, width=8, signed=False), FieldDesc(name='messageId', type_name='unsigned int', kind='scalar', default=None, width=4, signed=False), FieldDesc(name='topic', type_name='unsigned int', kind='scalar', default=None, width=4, signed=False), FieldDesc(name='source', type_name='unsigned char', kind='scalar', default=None, width=1, signed=False), FieldDesc(name='trafficClass', type_name='::Totem::PubSubBackend::TrafficClass', kind='enum', default='Noncritical', width=1, signed=False, enum_name='::Totem::PubSubBackend::TrafficClass'), FieldDesc(name='payloadSize', type_name='unsigned short', kind='scalar', default=None, width=2, signed=False),)),
    '::Totem::PubSubBackend::detail::PubSubEvent': ModelDesc('::Totem::PubSubBackend::detail::PubSubEvent', 'PubSubBackend/detail/Wire.hpp', (FieldDesc(name='topic', type_name='unsigned int', kind='scalar', default=None, width=4, signed=False), FieldDesc(name='type', type_name='::Totem::PubSubBackend::detail::SubscribeEventType', kind='enum', default=None, width=1, signed=False, enum_name='::Totem::PubSubBackend::detail::SubscribeEventType'),)),
    '::Totem::Wheel::WheelState': ModelDesc('::Totem::Wheel::WheelState', 'Wheel/Interfaces/Wire.hpp', (FieldDesc(name='position', type_name='Angle<unsigned short>', kind='unsupported', default=None), FieldDesc(name='delta', type_name='Angle<unsigned short>', kind='unsupported', default=None),)),
}
