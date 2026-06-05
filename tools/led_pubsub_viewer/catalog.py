from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import re

from totem_wire import ENUMS, MODELS, default_model


ANIMATION_COMMAND_MODEL = "::Totem::LedDisplay::AnimationCommand"
ANIMATION_CONFIG_PREFIX = "::Totem::LedDisplay::Animations::"
ANIMATION_CONFIG_SUFFIX = "Config"
PUBSUB_TOPIC_ENUM = "::Totem::Data::PubSub::Topic"


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
    return model_name.strip(":").split("::")[-1]


def animation_name_for_config(model_name: str) -> str | None:
    if not model_name.startswith(ANIMATION_CONFIG_PREFIX):
        return None
    short = short_model_name(model_name)
    if not short.endswith(ANIMATION_CONFIG_SUFFIX):
        return None
    return short[: -len(ANIMATION_CONFIG_SUFFIX)]


def _spec_defaults(model_name: str, animation_name: str) -> tuple[str | None, int | None]:
    include_path = MODELS[model_name].include_path
    header = repo_root() / "include" / include_path
    if not header.exists():
        return None, None

    text = header.read_text(encoding="utf-8")
    match = re.search(
        rf"struct\s+{re.escape(animation_name)}Spec\s*\{{(?P<body>.*?)\n\}};",
        text,
        re.S,
    )
    if match is None:
        return None, None

    body = match.group("body")
    layer = None
    lifetime_ms = None
    layer_match = re.search(r"defaultLayer\s*=\s*Layer::([A-Za-z_][A-Za-z0-9_]*)", body)
    if layer_match is not None:
        layer = layer_match.group(1)
    lifetime_match = re.search(
        r"defaultLifetimeMs\s*=\s*([0-9]+)", body
    ) or re.search(r"defaultDurationMs\s*=\s*([0-9]+)", body)
    if lifetime_match is not None:
        lifetime_ms = int(lifetime_match.group(1))
    return layer, lifetime_ms


def _animation_event(model_name: str, animation_name: str) -> EventDefinition:
    default_layer, default_lifetime_ms = _spec_defaults(model_name, animation_name)
    template = {
        "type": "Play",
        "kind": animation_name,
    }
    if default_layer is not None:
        template["layer"] = default_layer
    if default_lifetime_ms is not None:
        template["lifetimeMs"] = default_lifetime_ms
    return EventDefinition(
        label=f"Animation / Play {animation_name}",
        topic_name="Animation",
        topic=topic_value("Animation"),
        traffic_class=0,
        payload_model=ANIMATION_COMMAND_MODEL,
        payload_template=template,
        config_model=model_name,
        renderable=True,
        animation_name=animation_name,
        default_layer=default_layer,
        default_lifetime_ms=default_lifetime_ms,
    )


def _animation_control_events() -> list[EventDefinition]:
    topic = topic_value("Animation")
    return [
        EventDefinition(
            label="Animation / Stop",
            topic_name="Animation",
            topic=topic,
            traffic_class=0,
            payload_model=ANIMATION_COMMAND_MODEL,
            payload_template={"type": "Stop", "kind": "None", "payloadSize": 0},
        ),
        EventDefinition(
            label="Animation / Set Layer Active",
            topic_name="Animation",
            topic=topic,
            traffic_class=0,
            payload_model=ANIMATION_COMMAND_MODEL,
            payload_template={"type": "SetLayerActive", "kind": "None"},
            config_model="::Totem::LedDisplay::LayerActive",
        ),
        EventDefinition(
            label="Animation / Set Layer Opacity",
            topic_name="Animation",
            topic=topic,
            traffic_class=0,
            payload_model=ANIMATION_COMMAND_MODEL,
            payload_template={"type": "SetLayerOpacity", "kind": "None"},
            config_model="::Totem::LedDisplay::LayerOpacity",
        ),
        EventDefinition(
            label="Animation / Fade Layer Swap",
            topic_name="Animation",
            topic=topic,
            traffic_class=0,
            payload_model=ANIMATION_COMMAND_MODEL,
            payload_template={"type": "FadeLayerSwap", "kind": "None"},
            config_model="::Totem::LedDisplay::LayerFadeSwap",
        ),
    ]


DIRECT_MODEL_TOPICS = {
    "::Totem::Audio::BeatEvent": "Beat",
    "::Totem::Audio::FftFrame": "FftFrame",
    "::Totem::Audio::PeakEvent": "Peak",
    "::Totem::Buttons::ButtonEvent": "Button",
    "::Totem::LedPwm::CommandEvent": "LedPwm",
    "::Totem::PubSubBackend::detail::PubSubEvent": "PubSub",
    "::Totem::Wheel::WheelState": "Wheel",
}


def _direct_payload_event(model_name: str) -> EventDefinition:
    topic_name = DIRECT_MODEL_TOPICS.get(model_name)
    topic = topic_value(topic_name) if topic_name else 0
    label_prefix = "Payload" if topic_name else "Wire Model"
    return EventDefinition(
        label=f"{label_prefix} / {short_model_name(model_name)}",
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

    excluded = animation_config_models | {
        ANIMATION_COMMAND_MODEL,
        "::Totem::LedDisplay::LayerActive",
        "::Totem::LedDisplay::LayerFadeSwap",
        "::Totem::LedDisplay::LayerOpacity",
    }
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
