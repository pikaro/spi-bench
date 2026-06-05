from __future__ import annotations

from dataclasses import dataclass
import html
from pathlib import Path
from typing import Any

from totem_wire import ENUMS, MODELS, FieldDesc, default_model

from .adapter import (
    RenderError,
    parse_topic,
    payload_bytes_for_event,
    render_event_trace,
    renderer_json_for_event,
)
from .catalog import (
    ANIMATION_COMMAND_MODEL,
    EventDefinition,
    all_events,
    command_defaults,
    payload_defaults,
)
from .connection import PubSubConnection


@dataclass
class FormControl:
    path: tuple[str, ...]
    field: FieldDesc
    widget: Any
    kind: str


@dataclass(frozen=True)
class Layout:
    margin: int
    form_rect: Any
    preview_text_rect: Any
    preview_rect: Any
    transport_y: int
    field_label_width: int
    field_widget_x: int
    field_widget_width: int
    form_area_width: int


class ViewerApp:
    def __init__(self) -> None:
        try:
            import pygame
            import pygame_gui
        except ModuleNotFoundError as exc:
            raise RuntimeError(
                "pygame and pygame_gui are required for the integrated viewer"
            ) from exc

        self.pygame = pygame
        self.pygame_gui = pygame_gui
        pygame.init()
        self.window_size = (1220, 760)
        self.screen = pygame.display.set_mode(self.window_size, pygame.RESIZABLE)
        pygame.display.set_caption("Totem LED PubSub Viewer")
        self.clock = pygame.time.Clock()
        self.manager = pygame_gui.UIManager(self.window_size)
        self.window_visible = True
        self.window_focused = True
        self._window_hidden_events = {
            value
            for name in ("WINDOWHIDDEN", "WINDOWMINIMIZED")
            if (value := getattr(pygame, name, None)) is not None
        }
        self._window_visible_events = {
            value
            for name in ("WINDOWSHOWN", "WINDOWRESTORED", "WINDOWEXPOSED")
            if (value := getattr(pygame, name, None)) is not None
        }

        self.events = all_events()
        if not self.events:
            raise RuntimeError("no generated wire events are available")
        self.selected_event = self.events[0]
        self.connection = PubSubConnection()

        self.static_elements: list[Any] = []
        self.controls: list[FormControl] = []
        self.command_controls: list[FormControl] = []
        self.form_container: Any | None = None
        self.layout: Layout | None = None

        self.preview_surface: Any | None = None
        self.trace: Any | None = None
        self.current_frame = 0
        self.playing = False
        self.play_accum = 0.0
        self._updating_frame_entry = False

        self.needs_redraw = True
        self._last_connection_poll_s = -10.0
        self._last_status_text = ""

        self._build_ui()
        self._select_event(self.selected_event)

    def run(self) -> None:
        running = True
        self.clock.tick()
        while running:
            active_window = self.window_visible and self.window_focused
            if active_window and self.playing:
                dt = self.clock.tick(30) / 1000.0
                events = self.pygame.event.get()
            else:
                first_event = self.pygame.event.wait(250 if self.window_visible else 1000)
                dt = min(self.clock.tick() / 1000.0, 0.25)
                events = []
                if first_event.type != self.pygame.NOEVENT:
                    events.append(first_event)
                events.extend(self.pygame.event.get())

            if events:
                self.needs_redraw = True
            for event in events:
                if event.type == self.pygame.QUIT:
                    running = False
                elif event.type == self.pygame.VIDEORESIZE:
                    self._resize(event.size)
                elif event.type == getattr(self.pygame, "WINDOWFOCUSLOST", None):
                    self.window_focused = False
                    self.needs_redraw = False
                elif event.type == getattr(self.pygame, "WINDOWFOCUSGAINED", None):
                    self.window_focused = True
                    self.needs_redraw = True
                elif event.type in self._window_hidden_events:
                    self.window_visible = False
                    self.needs_redraw = False
                elif event.type in self._window_visible_events:
                    self.window_visible = True
                    self.needs_redraw = True
                elif event.type == self.pygame.KEYDOWN and event.key == self.pygame.K_ESCAPE:
                    self.window_focused = True
                    running = False
                elif event.type in {
                    self.pygame.MOUSEBUTTONDOWN,
                    self.pygame.KEYDOWN,
                }:
                    self.window_focused = True
                self._handle_ui_event(event)
                self.manager.process_events(event)

            active_window = self.window_visible and self.window_focused
            if active_window and self.playing:
                self._advance_playback(dt)
            self._poll_connection(active_window)

            can_draw = self.window_visible and (self.window_focused or bool(events))
            if can_draw and (self.needs_redraw or self.playing):
                self.manager.update(dt)
                self.screen.fill((24, 27, 31))
                self._draw_preview()
                self.manager.draw_ui(self.screen)
                self.pygame.display.flip()
                self.needs_redraw = False

        self._close_trace()
        self.connection.disconnect()
        self.pygame.quit()

    def _compute_layout(self) -> Layout:
        width, height = self.window_size
        pygame = self.pygame
        margin = 12
        gap = 12
        content_top = 98
        bottom = height - margin
        available_h = max(220, bottom - content_top)

        if width >= 980:
            form_w = max(330, min(560, int(width * 0.43)))
            right_x = margin + form_w + gap
            right_w = max(300, width - right_x - margin)
            form_rect = pygame.Rect(margin, content_top, form_w, available_h)
            preview_text_h = max(96, min(170, int(height * 0.21)))
            preview_text_rect = pygame.Rect(right_x, content_top, right_w, preview_text_h)
            transport_y = preview_text_rect.bottom + 8
            preview_rect = pygame.Rect(
                right_x,
                transport_y + 36,
                right_w,
                max(120, bottom - (transport_y + 36)),
            )
        else:
            form_h = max(220, int(available_h * 0.48))
            form_rect = pygame.Rect(margin, content_top, width - 2 * margin, form_h)
            preview_text_h = max(84, min(130, int(available_h * 0.18)))
            preview_text_rect = pygame.Rect(
                margin,
                form_rect.bottom + gap,
                width - 2 * margin,
                preview_text_h,
            )
            transport_y = preview_text_rect.bottom + 8
            preview_rect = pygame.Rect(
                margin,
                transport_y + 36,
                width - 2 * margin,
                max(90, bottom - (transport_y + 36)),
            )

        form_area_width = max(260, form_rect.width - 22)
        label_width = max(104, min(190, int(form_area_width * 0.34)))
        widget_x = label_width + 20
        widget_width = max(120, form_area_width - widget_x - 12)
        return Layout(
            margin=margin,
            form_rect=form_rect,
            preview_text_rect=preview_text_rect,
            preview_rect=preview_rect,
            transport_y=transport_y,
            field_label_width=label_width,
            field_widget_x=widget_x,
            field_widget_width=widget_width,
            form_area_width=form_area_width,
        )

    def _build_ui(self, *, preserve_values: bool = False) -> None:
        ip_text = getattr(self, "ip_entry", None).get_text() if preserve_values else "192.168.179.5"
        topic_text = getattr(self, "topic_entry", None).get_text() if preserve_values else str(self.selected_event.topic)
        command_values = self._safe_command_values() if preserve_values else None
        payload_values = self._safe_payload_values() if preserve_values else None

        self._kill_static_ui()
        if self.form_container is not None:
            self.form_container.kill()
            self.form_container = None

        self.layout = self._compute_layout()
        self._rebuild_form(command_values=command_values, payload_values=payload_values)
        self._build_static_ui(ip_text=ip_text, topic_text=topic_text)
        self._raise_header_controls()
        self._update_frame_controls()
        self._refresh_preview_text()
        self.needs_redraw = True

    def _track(self, element: Any) -> Any:
        self.static_elements.append(element)
        return element

    def _kill_static_ui(self) -> None:
        for element in self.static_elements:
            element.kill()
        self.static_elements = []

    def _build_static_ui(self, *, ip_text: str, topic_text: str) -> None:
        pygame = self.pygame
        elements = self.pygame_gui.elements
        layout = self.layout
        assert layout is not None
        width, _height = self.window_size
        margin = layout.margin

        ip_entry_w = max(120, min(190, width // 5))
        self._track(
            elements.UILabel(
                relative_rect=pygame.Rect(margin, 12, 58, 28),
                text="MCU IP",
                manager=self.manager,
            )
        )
        self.ip_entry = self._track(
            elements.UITextEntryLine(
                relative_rect=pygame.Rect(margin + 62, 12, ip_entry_w, 28),
                manager=self.manager,
                initial_text=ip_text,
            )
        )
        x = margin + 62 + ip_entry_w + 8
        self.connect_button = self._track(
            elements.UIButton(
                relative_rect=pygame.Rect(x, 12, 92, 28),
                text="Connect",
                manager=self.manager,
            )
        )
        x += 100
        self.disconnect_button = self._track(
            elements.UIButton(
                relative_rect=pygame.Rect(x, 12, 106, 28),
                text="Disconnect",
                manager=self.manager,
            )
        )
        status_x = x + 118
        self.status_label = self._track(
            elements.UILabel(
                relative_rect=pygame.Rect(
                    status_x,
                    12,
                    max(120, width - status_x - margin),
                    28,
                ),
                text=self._last_status_text or "Status: disconnected",
                manager=self.manager,
            )
        )

        event_w = max(240, min(480, int(width * 0.42)))
        self.event_dropdown = self._track(
            elements.UIDropDownMenu(
                options_list=[event.label for event in self.events],
                starting_option=self.selected_event.label,
                relative_rect=pygame.Rect(margin, 54, event_w, 30),
                manager=self.manager,
            )
        )
        x = margin + event_w + 10
        self._track(
            elements.UILabel(
                relative_rect=pygame.Rect(x, 54, 52, 30),
                text="Topic",
                manager=self.manager,
            )
        )
        x += 54
        self.topic_entry = self._track(
            elements.UITextEntryLine(
                relative_rect=pygame.Rect(x, 54, 102, 30),
                manager=self.manager,
                initial_text=topic_text,
            )
        )
        x += 114
        self.render_button = self._track(
            elements.UIButton(
                relative_rect=pygame.Rect(x, 54, 82, 30),
                text="Render",
                manager=self.manager,
            )
        )
        x += 90
        self.publish_button = self._track(
            elements.UIButton(
                relative_rect=pygame.Rect(x, 54, 86, 30),
                text="Publish",
                manager=self.manager,
            )
        )
        x += 96
        self.result_label = self._track(
            elements.UILabel(
                relative_rect=pygame.Rect(x, 54, max(90, width - x - margin), 30),
                text="",
                manager=self.manager,
            )
        )

        self.preview_box = self._track(
            elements.UITextBox(
                html_text="",
                relative_rect=layout.preview_text_rect,
                manager=self.manager,
            )
        )
        self.prev_button = self._track(
            elements.UIButton(
                relative_rect=pygame.Rect(layout.preview_rect.x, layout.transport_y, 42, 28),
                text="<",
                manager=self.manager,
            )
        )
        self.play_button = self._track(
            elements.UIButton(
                relative_rect=pygame.Rect(layout.preview_rect.x + 50, layout.transport_y, 70, 28),
                text="Play",
                manager=self.manager,
            )
        )
        self.next_button = self._track(
            elements.UIButton(
                relative_rect=pygame.Rect(layout.preview_rect.x + 128, layout.transport_y, 42, 28),
                text=">",
                manager=self.manager,
            )
        )
        self._track(
            elements.UILabel(
                relative_rect=pygame.Rect(layout.preview_rect.x + 180, layout.transport_y, 46, 28),
                text="Frame",
                manager=self.manager,
            )
        )
        self.frame_entry = self._track(
            elements.UITextEntryLine(
                relative_rect=pygame.Rect(layout.preview_rect.x + 232, layout.transport_y, 62, 28),
                manager=self.manager,
                initial_text=str(self.current_frame),
            )
        )
        self.frame_label = self._track(
            elements.UILabel(
                relative_rect=pygame.Rect(
                    layout.preview_rect.x + 304,
                    layout.transport_y,
                    max(100, layout.preview_rect.width - 304),
                    28,
                ),
                text="0 / 0",
                manager=self.manager,
            )
        )

    def _raise_header_controls(self) -> None:
        for element in self.static_elements:
            if hasattr(element, "change_layer"):
                element.change_layer(100)

    def _resize(self, size: tuple[int, int]) -> None:
        self.window_size = (max(640, size[0]), max(420, size[1]))
        self.screen = self.pygame.display.set_mode(self.window_size, self.pygame.RESIZABLE)
        self.manager.set_window_resolution(self.window_size)
        self._build_ui(preserve_values=True)

    def _handle_ui_event(self, event: Any) -> None:
        gui = self.pygame_gui
        if event.type == gui.UI_BUTTON_PRESSED:
            if event.ui_element == self.connect_button:
                self.connection.connect(self.ip_entry.get_text().strip())
            elif event.ui_element == self.disconnect_button:
                self.connection.disconnect()
            elif event.ui_element == self.render_button:
                self._render_selected()
            elif event.ui_element == self.publish_button:
                self._publish_selected()
            elif event.ui_element == self.prev_button:
                self._step_frame(-1)
            elif event.ui_element == self.next_button:
                self._step_frame(1)
            elif event.ui_element == self.play_button:
                self._toggle_playback()
        elif event.type == gui.UI_DROP_DOWN_MENU_CHANGED:
            if event.ui_element == self.event_dropdown:
                self._select_event(self._event_by_label(self._dropdown_text(event.text)))
            else:
                self._refresh_preview_text()
        elif event.type == gui.UI_TEXT_ENTRY_CHANGED:
            if event.ui_element == self.frame_entry and not self._updating_frame_entry:
                self._jump_to_frame_text()
            elif event.ui_element != self.ip_entry:
                self._refresh_preview_text()

    def _event_by_label(self, label: str) -> EventDefinition:
        for event in self.events:
            if event.label == label:
                return event
        raise KeyError(label)

    @staticmethod
    def _dropdown_text(value: object) -> str:
        if isinstance(value, tuple) and value:
            return str(value[0])
        return str(value)

    def _select_event(self, event: EventDefinition) -> None:
        self.selected_event = event
        self.topic_entry.set_text(str(event.topic))
        self._close_trace()
        self.preview_surface = None
        self.current_frame = 0
        self.playing = False
        self.play_accum = 0.0
        self._rebuild_form()
        self._raise_header_controls()
        self._update_frame_controls()
        self._refresh_preview_text()
        self.needs_redraw = True

    def _rebuild_form(
        self,
        *,
        command_values: dict[str, object] | None = None,
        payload_values: dict[str, object] | None = None,
    ) -> None:
        if self.form_container is not None:
            self.form_container.kill()
        self.controls = []
        self.command_controls = []

        elements = self.pygame_gui.elements
        layout = self.layout
        assert layout is not None
        self.form_container = elements.UIScrollingContainer(
            relative_rect=layout.form_rect,
            manager=self.manager,
        )

        y = 8
        command_defaults_values = command_defaults(self.selected_event)
        if command_values:
            command_defaults_values.update(command_values)
        command_fields = self._visible_command_fields(self.selected_event)
        if command_fields:
            y = self._add_section_label("Command", y)
            y = self._add_fields(
                ANIMATION_COMMAND_MODEL,
                command_defaults_values,
                y,
                command=True,
                fields=command_fields,
            )

        payload_model = self.selected_event.config_model or self.selected_event.payload_model
        if payload_model != ANIMATION_COMMAND_MODEL or self.selected_event.config_model is not None:
            payload_defaults_values = payload_defaults(self.selected_event)
            if payload_values:
                payload_defaults_values.update(payload_values)
            title = "Config" if self.selected_event.config_model else "Payload"
            y = self._add_section_label(title, y + 8)
            y = self._add_fields(payload_model, payload_defaults_values, y)

        self.form_container.set_scrollable_area_dimensions(
            (layout.form_area_width, max(y + 24, layout.form_rect.height))
        )

    def _visible_command_fields(self, event: EventDefinition) -> tuple[FieldDesc, ...]:
        if event.payload_model != ANIMATION_COMMAND_MODEL:
            return ()
        hidden = {"type", "kind", "payloadSize", "payload"}
        model = MODELS[ANIMATION_COMMAND_MODEL]
        if event.payload_template:
            return tuple(field for field in model.fields if field.name not in hidden)
        return model.fields

    def _add_section_label(self, text: str, y: int) -> int:
        layout = self.layout
        assert layout is not None
        self.pygame_gui.elements.UILabel(
            relative_rect=self.pygame.Rect(8, y, layout.form_area_width - 18, 24),
            text=text,
            manager=self.manager,
            container=self.form_container,
        )
        return y + 28

    def _add_fields(
        self,
        model_name: str,
        values: dict[str, object],
        y: int,
        *,
        prefix: tuple[str, ...] = (),
        command: bool = False,
        fields: tuple[FieldDesc, ...] | None = None,
    ) -> int:
        layout = self.layout
        assert layout is not None
        model = MODELS[model_name]
        for field in fields or model.fields:
            value = values.get(field.name)
            if field.kind == "model" and field.model_name:
                self.pygame_gui.elements.UILabel(
                    relative_rect=self.pygame.Rect(18, y, layout.form_area_width - 32, 24),
                    text=".".join([*prefix, field.name]),
                    manager=self.manager,
                    container=self.form_container,
                )
                nested_default = default_model(field.model_name)
                if isinstance(value, dict):
                    nested_default.update(value)
                y = self._add_fields(
                    field.model_name,
                    nested_default,
                    y + 26,
                    prefix=(*prefix, field.name),
                    command=command,
                )
                continue

            label = ".".join([*prefix, field.name])
            self.pygame_gui.elements.UILabel(
                relative_rect=self.pygame.Rect(18, y, layout.field_label_width, 26),
                text=label,
                manager=self.manager,
                container=self.form_container,
            )
            widget = self._make_field_widget(field, value, y)
            control = FormControl(
                path=(*prefix, field.name),
                field=field,
                widget=widget,
                kind=field.kind,
            )
            if command:
                self.command_controls.append(control)
            else:
                self.controls.append(control)
            y += 34
        return y

    def _make_field_widget(self, field: FieldDesc, value: object, y: int) -> Any:
        elements = self.pygame_gui.elements
        layout = self.layout
        assert layout is not None
        rect = self.pygame.Rect(
            layout.field_widget_x,
            y,
            layout.field_widget_width,
            28,
        )
        if field.kind == "enum" and field.enum_name:
            options = [item.name for item in ENUMS[field.enum_name].values]
            selected = str(value) if value is not None else (options[0] if options else "")
            if selected not in options and options:
                selected = options[0]
            return elements.UIDropDownMenu(
                options_list=options,
                starting_option=selected,
                relative_rect=rect,
                manager=self.manager,
                container=self.form_container,
            )
        if field.kind == "bool":
            selected = "True" if bool(value) else "False"
            return elements.UIDropDownMenu(
                options_list=["False", "True"],
                starting_option=selected,
                relative_rect=rect,
                manager=self.manager,
                container=self.form_container,
            )
        text = self._value_text(field, value)
        widget = elements.UITextEntryLine(
            relative_rect=rect,
            manager=self.manager,
            container=self.form_container,
            initial_text=text,
        )
        if field.kind == "unsupported":
            widget.disable()
            widget.set_text("unsupported")
        return widget

    def _value_text(self, field: FieldDesc, value: object) -> str:
        if field.kind == "array":
            if isinstance(value, list):
                return bytes(int(item) & 0xFF for item in value).hex()
            if isinstance(value, (bytes, bytearray)):
                return bytes(value).hex()
        return "" if value is None else str(value)

    def _collect_values(self, controls: list[FormControl]) -> dict[str, object]:
        values: dict[str, object] = {}
        for control in controls:
            value = self._control_value(control)
            self._set_nested(values, control.path, value)
        return values

    def _safe_command_values(self) -> dict[str, object]:
        try:
            return self._command_values()
        except Exception:
            return {}

    def _safe_payload_values(self) -> dict[str, object]:
        try:
            return self._payload_values()
        except Exception:
            return {}

    def _control_value(self, control: FormControl) -> object:
        field = control.field
        if field.kind in {"enum", "bool"}:
            selected = getattr(control.widget, "selected_option", None)
            text = self._dropdown_text(selected if selected is not None else "")
            return text == "True" if field.kind == "bool" else text
        text = control.widget.get_text().strip()
        if field.kind == "array":
            return text
        if field.kind == "scalar":
            return int(text, 0) if text else 0
        return None

    @staticmethod
    def _set_nested(target: dict[str, object], path: tuple[str, ...], value: object) -> None:
        cursor = target
        for part in path[:-1]:
            child = cursor.setdefault(part, {})
            if not isinstance(child, dict):
                child = {}
                cursor[part] = child
            cursor = child
        cursor[path[-1]] = value

    def _command_values(self) -> dict[str, object]:
        return self._collect_values(self.command_controls)

    def _payload_values(self) -> dict[str, object]:
        return self._collect_values(self.controls)

    def _refresh_preview_text(self) -> None:
        try:
            payload = payload_bytes_for_event(
                self.selected_event,
                command_values=self._command_values(),
                payload_values=self._payload_values(),
            )
            lines = [
                f"event: {self.selected_event.label}",
                f"topic: {self.topic_entry.get_text().strip() or self.selected_event.topic}",
                f"payload: {payload.hex()}",
            ]
            if self.selected_event.renderable:
                render_json = renderer_json_for_event(
                    self.selected_event,
                    command_values=self._command_values(),
                    payload_values=self._payload_values(),
                )
                lines.append("render_json:")
                lines.append(str(render_json))
            self.preview_box.set_text("<br>".join(html.escape(line) for line in lines))
            self.result_label.set_text("")
        except Exception as exc:
            self._show_error("Input error", exc)
        self.needs_redraw = True

    def _render_selected(self) -> None:
        if not self.selected_event.renderable:
            self.result_label.set_text("Preview unavailable")
            self.needs_redraw = True
            return
        try:
            self._close_trace()
            self.trace = render_event_trace(
                self.selected_event,
                command_values=self._command_values(),
                payload_values=self._payload_values(),
                root=Path(__file__).resolve().parents[2],
            )
            self.current_frame = 0
            self.playing = False
            self.play_accum = 0.0
            self._set_frame(0)
            self.result_label.set_text(f"Rendered {self._frame_count()} frames")
        except Exception as exc:
            self.preview_surface = None
            self._show_error("Render error", exc)
        self.needs_redraw = True

    def _frame_image(self, frame: int) -> Any:
        from led_render.viewer import frame_image

        return frame_image(
            self.trace,
            frame,
            plane="rgb_final",
            scale=10,
            glare=True,
            layout="radial",
            show_frame_label=True,
            brightness=180,
        )

    def _frame_count(self) -> int:
        return int(self.trace.header.frame_count) if self.trace is not None else 0

    def _set_frame(self, frame: int) -> None:
        if self.trace is None:
            self.preview_surface = None
            self.current_frame = 0
            self._update_frame_controls()
            return
        count = self._frame_count()
        self.current_frame = max(0, min(frame, count - 1))
        image = self._frame_image(self.current_frame)
        self.preview_surface = self.pygame.surfarray.make_surface(image.swapaxes(0, 1))
        self._update_frame_controls()
        self.needs_redraw = True

    def _step_frame(self, delta: int) -> None:
        if self.trace is None:
            return
        self.playing = False
        self._set_frame(self.current_frame + delta)

    def _jump_to_frame_text(self) -> None:
        if self.trace is None:
            return
        text = self.frame_entry.get_text().strip()
        if not text:
            return
        try:
            self.playing = False
            self._set_frame(int(text, 0))
        except ValueError:
            self.result_label.set_text("Invalid frame")
            self.needs_redraw = True

    def _toggle_playback(self) -> None:
        if self.trace is None:
            return
        if self.current_frame >= self._frame_count() - 1:
            self._set_frame(0)
        self.playing = not self.playing
        self.play_accum = 0.0
        self._update_frame_controls()
        self.needs_redraw = True

    def _advance_playback(self, dt: float) -> None:
        if self.trace is None:
            self.playing = False
            return
        fps = max(1.0, float(self.trace.fps or 30.0))
        self.play_accum += dt * fps
        steps = int(self.play_accum)
        if steps <= 0:
            return
        self.play_accum -= steps
        next_frame = self.current_frame + steps
        if next_frame >= self._frame_count():
            next_frame = self._frame_count() - 1
            self.playing = False
        self._set_frame(next_frame)

    def _update_frame_controls(self) -> None:
        count = self._frame_count()
        if hasattr(self, "play_button"):
            self.play_button.set_text("Pause" if self.playing else "Play")
        if hasattr(self, "frame_label"):
            self.frame_label.set_text(f"{self.current_frame} / {max(0, count - 1)}")
        if hasattr(self, "frame_entry"):
            self._updating_frame_entry = True
            self.frame_entry.set_text(str(self.current_frame))
            self._updating_frame_entry = False

    def _publish_selected(self) -> None:
        try:
            payload = payload_bytes_for_event(
                self.selected_event,
                command_values=self._command_values(),
                payload_values=self._payload_values(),
            )
            topic = parse_topic(self.topic_entry.get_text(), self.selected_event.topic)
            self.connection.publish(
                topic=topic,
                payload=payload,
                traffic_class=self.selected_event.traffic_class,
            )
            self.result_label.set_text(f"Published {len(payload)} bytes")
        except Exception as exc:
            self._show_error("Publish error", exc)
        self.needs_redraw = True

    def _poll_connection(self, active_window: bool) -> None:
        now_s = self.pygame.time.get_ticks() / 1000.0
        interval_s = 0.35 if active_window else 2.0
        if now_s - self._last_connection_poll_s < interval_s:
            return
        self._last_connection_poll_s = now_s
        state = self.connection.poll()
        text = f"Status: {state.status} {state.detail}".strip()
        if text != self._last_status_text:
            self._last_status_text = text
            self.status_label.set_text(text)
            self.needs_redraw = True

    def _show_error(self, title: str, exc: Exception) -> None:
        self.result_label.set_text(title)
        if isinstance(exc, RenderError):
            detail = exc.details()
        else:
            detail = f"{type(exc).__name__}: {exc}"
        self.preview_box.set_text(
            "<br>".join(html.escape(line) for line in detail.splitlines())
        )
        self.needs_redraw = True

    def _draw_preview(self) -> None:
        pygame = self.pygame
        layout = self.layout
        assert layout is not None
        rect = layout.preview_rect
        pygame.draw.rect(self.screen, (14, 16, 18), rect)
        pygame.draw.rect(self.screen, (64, 70, 78), rect, 1)
        if self.preview_surface is None:
            return
        surface = self.preview_surface
        source_w, source_h = surface.get_size()
        scale = min(rect.width / source_w, rect.height / source_h)
        target_size = (max(1, int(source_w * scale)), max(1, int(source_h * scale)))
        target = pygame.transform.smoothscale(surface, target_size)
        x = rect.x + (rect.width - target_size[0]) // 2
        y = rect.y + (rect.height - target_size[1]) // 2
        self.screen.blit(target, (x, y))

    def _close_trace(self) -> None:
        if self.trace is not None:
            self.trace.close()
            self.trace = None


def run_app() -> None:
    ViewerApp().run()
