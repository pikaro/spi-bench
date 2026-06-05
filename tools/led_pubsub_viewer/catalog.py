from __future__ import annotations

import re
from dataclasses import dataclass
from pathlib import Path

from totem_wire import ENUMS, MODELS, default_model

ANIMATION_PLAY_COMMAND_MODEL = '::Totem::LedDisplay::AnimationPlayCommand'
ANIMATION_UPDATE_COMMAND_MODEL = '::Totem::LedDisplay::AnimationUpdateCommand'
ANIMATION_COMMAND_MODELS = {
    ANIMATION_PLAY_COMMAND_MODEL,
    ANIMATION_UPDATE_COMMAND_MODEL,
}
ANIMATION_CONFIG_PREFIX = '::Totem::LedDisplay::Animations::'
ANIMATION_CONFIG_SUFFIX = 'Config'
PUBSUB_TOPIC_ENUM = '::Totem::Data::PubSub::Topic'


@dataclass(frozen=True)
class EventDefinition:
    label: str
    topic_name: str | None
    topic: int
    traffic_class: int
    payload_model: str
    payload_template: dict[str, object]
    config_model: str | None = None
    renderable: bool = False
    animation_name: str | None = None
    default_layer: str | None = None
    default_lifetime_ms: int | None = None

    @property
    def publishable_by_default(self) -> bool:
        return self.topic != 0


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def enum_value(enum_name: str, item_name: str) -> int:
    return ENUMS[enum_name].value_by_name(item_name)


def topic_value(name: str) -> int:
    return enum_value(PUBSUB_TOPIC_ENUM, name)


def short_model_name(model_name: str) -> str:
    return model_name.strip(':').split('::')[-1]


def animation_name_for_config(model_name: str) -> str | None:
    if not model_name.startswith(ANIMATION_CONFIG_PREFIX):
        return None
    short = short_model_name(model_name)
    if not short.endswith(ANIMATION_CONFIG_SUFFIX):
        return None
    return short[: -len(ANIMATION_CONFIG_SUFFIX)]


def _spec_defaults(model_name: str, animation_name: str) -> tuple[str | None, int | None]:
    include_path = MODELS[model_name].include_path
    header = repo_root() / 'include' / include_path
    if not header.exists():
        return None, None

    text = header.read_text(encoding='utf-8')
    match = re.search(
        rf'struct\s+{re.escape(animation_name)}Spec\s*\{{(?P<body>.*?)\n\}};',
        text,
        re.DOTALL,
    )
    if match is None:
        return None, None

    body = match.group('body')
    layer = None
    lifetime_ms = None
    layer_match = re.search(r'defaultLayer\s*=\s*Layer::([A-Za-z_][A-Za-z0-9_]*)', body)
    if layer_match is not None:
        layer = layer_match.group(1)
    lifetime_match = re.search(r'defaultLifetimeMs\s*=\s*([0-9]+)', body) or re.search(
        r'defaultDurationMs\s*=\s*([0-9]+)', body,
    )
    if lifetime_match is not None:
        lifetime_ms = int(lifetime_match.group(1))
    return layer, lifetime_ms


def _animation_event(model_name: str, animation_name: str) -> EventDefinition:
    default_layer, default_lifetime_ms = _spec_defaults(model_name, animation_name)
    template = {
        'kind': animation_name,
    }
    if default_layer is not None:
        template['layer'] = default_layer
    if default_lifetime_ms is not None:
        template['lifetimeMs'] = default_lifetime_ms
    return EventDefinition(
        label=f'Animation / Play {animation_name}',
        topic_name='AnimationPlay',
        topic=topic_value('AnimationPlay'),
        traffic_class=0,
        payload_model=ANIMATION_PLAY_COMMAND_MODEL,
        payload_template=template,
        config_model=model_name,
        renderable=True,
        animation_name=animation_name,
        default_layer=default_layer,
        default_lifetime_ms=default_lifetime_ms,
    )


def _animation_control_events() -> list[EventDefinition]:
    return [
        EventDefinition(
            label='Playback / Stop',
            topic_name='AnimationStop',
            topic=topic_value('AnimationStop'),
            traffic_class=0,
            payload_model='::Totem::LedDisplay::AnimationStopCommand',
            payload_template={},
        ),
        EventDefinition(
            label='Display / Hue Offset',
            topic_name='AnimationSetHueOffset',
            topic=topic_value('AnimationSetHueOffset'),
            traffic_class=0,
            payload_model='::Totem::LedDisplay::AnimationSetHueOffsetCommand',
            payload_template={},
        ),
        EventDefinition(
            label='Display / Rotation Offset',
            topic_name='AnimationSetRotationOffset',
            topic=topic_value('AnimationSetRotationOffset'),
            traffic_class=0,
            payload_model='::Totem::LedDisplay::AnimationSetRotationOffsetCommand',
            payload_template={},
        ),
        EventDefinition(
            label='Display / Brightness',
            topic_name='AnimationSetBrightness',
            topic=topic_value('AnimationSetBrightness'),
            traffic_class=0,
            payload_model='::Totem::LedDisplay::AnimationSetBrightnessCommand',
            payload_template={},
        ),
        EventDefinition(
            label='Layer / Active',
            topic_name='AnimationSetLayerActive',
            topic=topic_value('AnimationSetLayerActive'),
            traffic_class=0,
            payload_model='::Totem::LedDisplay::AnimationSetLayerActiveCommand',
            payload_template={},
        ),
        EventDefinition(
            label='Layer / Opacity',
            topic_name='AnimationSetLayerOpacity',
            topic=topic_value('AnimationSetLayerOpacity'),
            traffic_class=0,
            payload_model='::Totem::LedDisplay::AnimationSetLayerOpacityCommand',
            payload_template={},
        ),
        EventDefinition(
            label='Layer / Fade Swap',
            topic_name='AnimationFadeLayerSwap',
            topic=topic_value('AnimationFadeLayerSwap'),
            traffic_class=0,
            payload_model='::Totem::LedDisplay::AnimationFadeLayerSwapCommand',
            payload_template={},
        ),
    ]


DIRECT_MODEL_TOPICS = {
    '::Totem::Audio::BeatEvent': 'Beat',
    '::Totem::Audio::FftFrame': 'FftFrame',
    '::Totem::Audio::PeakEvent': 'Peak',
    '::Totem::Buttons::ButtonEvent': 'Button',
    '::Totem::LedPwm::CommandEvent': 'LedPwm',
    '::Totem::PubSubBackend::detail::PubSubEvent': 'PubSub',
    '::Totem::Wheel::WheelState': 'Wheel',
}


def _direct_payload_event(model_name: str) -> EventDefinition:
    topic_name = DIRECT_MODEL_TOPICS.get(model_name)
    topic = topic_value(topic_name) if topic_name else 0
    label_prefix = 'Payload' if topic_name else 'Wire Model'
    return EventDefinition(
        label=f'{label_prefix} / {short_model_name(model_name)}',
        topic_name=topic_name,
        topic=topic,
        traffic_class=0,
        payload_model=model_name,
        payload_template={},
    )


def all_events() -> list[EventDefinition]:
    events: list[EventDefinition] = []
    animation_config_models = set()
    for model_name in sorted(MODELS):
        animation_name = animation_name_for_config(model_name)
        if animation_name is None:
            continue
        animation_config_models.add(model_name)
        events.append(_animation_event(model_name, animation_name))

    events.extend(_animation_control_events())

    animation_control_models = {
        '::Totem::LedDisplay::AnimationStopCommand',
        '::Totem::LedDisplay::AnimationSetHueOffsetCommand',
        '::Totem::LedDisplay::AnimationSetRotationOffsetCommand',
        '::Totem::LedDisplay::AnimationSetBrightnessCommand',
        '::Totem::LedDisplay::AnimationSetLayerActiveCommand',
        '::Totem::LedDisplay::AnimationSetLayerOpacityCommand',
        '::Totem::LedDisplay::AnimationFadeLayerSwapCommand',
    }
    excluded = animation_config_models | animation_control_models | ANIMATION_COMMAND_MODELS
    for model_name in sorted(MODELS):
        if model_name in excluded:
            continue
        events.append(_direct_payload_event(model_name))

    return sorted(events, key=lambda event: event.label)


def event_by_label(label: str) -> EventDefinition:
    for event in all_events():
        if event.label == label:
            return event
    raise KeyError(label)


def command_defaults(event: EventDefinition) -> dict[str, object]:
    values = default_model(event.payload_model)
    values.update(event.payload_template)
    return values


def payload_defaults(event: EventDefinition) -> dict[str, object]:
    model_name = event.config_model or event.payload_model
    values = default_model(model_name)
    if event.config_model is None:
        values.update(event.payload_template)
    return values
