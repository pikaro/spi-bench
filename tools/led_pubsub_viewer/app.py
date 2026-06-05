from __future__ import annotations

from dataclasses import dataclass
import html
import json
from pathlib import Path
from typing import Any

from totem_wire import ENUMS, MODELS, FieldDesc, default_model

from .adapter import (
    RenderError,
    parse_topic,
    payload_bytes_for_event,
    render_event_trace,
    renderer_binary,
    renderer_json_for_event,
)
from .catalog import (
    ANIMATION_COMMAND_MODELS,
    EventDefinition,
    all_events,
    command_defaults,
    payload_defaults,
)
from .connection import PubSubConnection

FIELD_DROPDOWN_LAYER = 1000
EVENT_MENU_LAYER = 2000


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
        self.manager.preload_fonts(
            [{"name": "noto_sans", "point_size": 14, "style": "bold", "antialiased": 1}]
        )
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
        self.selected_event = next(
            (event for event in self.events if event.renderable),
            self.events[0],
        )
        self.connection = PubSubConnection()

        self.static_elements: list[Any] = []
        self.controls: list[FormControl] = []
        self.command_controls: list[FormControl] = []
        self.form_container: Any | None = None
        self.event_menu_panel: Any | None = None
        self.event_menu_container: Any | None = None
        self.event_menu_elements: list[Any] = []
        self.event_menu_buttons: dict[Any, EventDefinition] = {}
        self.layout: Layout | None = None

        self.preview_surface: Any | None = None
        self.trace: Any | None = None
        self.current_frame = 0
        self.playing = False
        self.play_accum = 0.0
        self._updating_frame_entry = False
        self.render_dirty = True
        self.rendered_signature = ""
        self._play_button_state = ""

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
                    if event.type == self.pygame.MOUSEBUTTONDOWN:
                        self._handle_mouse_down(event.pos)
                self._handle_ui_event(event)
                self.manager.process_events(event)

            active_window = self.window_visible and self.window_focused
            if active_window and self.playing:
                self._advance_playback(dt)
            self._poll_connection(active_window)

            can_draw = self.window_visible and (self.window_focused or bool(events))
            if can_draw and (self.needs_redraw or self.playing):
                self.manager.update(dt)
                self._raise_open_dropdowns()
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

        self._close_event_menu()
        self._kill_static_ui()
        if self.form_container is not None:
            self.form_container.kill()
            self.form_container = None

        self.layout = self._compute_layout()
        self._rebuild_form(command_values=command_values, payload_values=payload_values)
        self._build_static_ui(ip_text=ip_text, topic_text=topic_text)
        self._raise_event_selector()
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

        event_w = max(240, min(500, int(width * 0.42)))
        self.event_button = self._track(
            elements.UIButton(
                relative_rect=pygame.Rect(margin, 54, event_w, 30),
                text=self.selected_event.label,
                manager=self.manager,
            )
        )
        self._left_align_button(self.event_button)
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
        self.publish_button = self._track(
            elements.UIButton(
                relative_rect=pygame.Rect(x, 54, 86, 30),
                text="Publish",
                manager=self.manager,
            )
        )
        x += 94
        self.rebuild_button = self._track(
            elements.UIButton(
                relative_rect=pygame.Rect(x, 54, 88, 30),
                text="Rebuild",
                manager=self.manager,
            )
        )
        x += 98
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
                relative_rect=pygame.Rect(layout.preview_rect.x + 50, layout.transport_y, 112, 28),
                text="Play",
                manager=self.manager,
            )
        )
        self.next_button = self._track(
            elements.UIButton(
                relative_rect=pygame.Rect(layout.preview_rect.x + 170, layout.transport_y, 42, 28),
                text=">",
                manager=self.manager,
            )
        )
        self._track(
            elements.UILabel(
                relative_rect=pygame.Rect(layout.preview_rect.x + 222, layout.transport_y, 46, 28),
                text="Frame",
                manager=self.manager,
            )
        )
        self.frame_entry = self._track(
            elements.UITextEntryLine(
                relative_rect=pygame.Rect(layout.preview_rect.x + 274, layout.transport_y, 62, 28),
                manager=self.manager,
                initial_text=str(self.current_frame),
            )
        )
        self.frame_label = self._track(
            elements.UILabel(
                relative_rect=pygame.Rect(
                    layout.preview_rect.x + 346,
                    layout.transport_y,
                    max(100, layout.preview_rect.width - 346),
                    28,
                ),
                text="0 / 0",
                manager=self.manager,
            )
        )

    def _raise_event_selector(self) -> None:
        if hasattr(self, "event_button") and hasattr(self.event_button, "change_layer"):
            self.event_button.change_layer(EVENT_MENU_LAYER)
        self._raise_event_menu()

    def _raise_dropdown(self, dropdown: Any) -> None:
        if hasattr(dropdown, "change_layer"):
            dropdown.change_layer(FIELD_DROPDOWN_LAYER)
        self._left_align_dropdown(dropdown)

    def _form_dropdowns(self) -> list[Any]:
        return [
            control.widget
            for control in (*self.command_controls, *self.controls)
            if control.field.kind in {"enum", "bool"}
        ]

    def _close_form_dropdowns(self) -> None:
        for dropdown in self._form_dropdowns():
            menu_states = getattr(dropdown, "menu_states", {})
            expanded_state = (
                menu_states.get("expanded") if isinstance(menu_states, dict) else None
            )
            if getattr(dropdown, "current_state", None) is expanded_state:
                if hasattr(dropdown, "unfocus"):
                    dropdown.unfocus()
                if hasattr(dropdown, "update"):
                    dropdown.update(0.0)

    def _raise_clicked_dropdown(self, pos: tuple[int, int]) -> None:
        for dropdown in self._form_dropdowns():
            rect = getattr(dropdown, "rect", None)
            if rect is not None and rect.collidepoint(pos):
                self._raise_dropdown(dropdown)
                return

    def _handle_mouse_down(self, pos: tuple[int, int]) -> None:
        if self.event_menu_container is not None:
            button_rect = getattr(self.event_button, "rect", None)
            menu_rect = getattr(self.event_menu_container, "rect", None)
            inside_button = button_rect is not None and button_rect.collidepoint(pos)
            inside_menu = menu_rect is not None and menu_rect.collidepoint(pos)
            if not inside_button and not inside_menu:
                self._close_event_menu()
        self._raise_clicked_dropdown(pos)

    def _raise_open_dropdowns(self) -> None:
        self._raise_event_menu()
        for dropdown in self._form_dropdowns():
            self._raise_open_dropdown(dropdown)

    def _raise_open_dropdown(self, dropdown: Any) -> None:
        state = getattr(dropdown, "current_state", None)
        selection_list = getattr(state, "options_selection_list", None)
        if selection_list is None:
            self._left_align_dropdown(dropdown)
            return

        self._raise_dropdown(dropdown)
        self._change_layer(selection_list, FIELD_DROPDOWN_LAYER + 3)
        self._change_layer(
            getattr(selection_list, "list_and_scroll_bar_container", None),
            FIELD_DROPDOWN_LAYER + 4,
        )
        self._change_layer(
            getattr(selection_list, "item_list_container", None),
            FIELD_DROPDOWN_LAYER + 5,
        )
        self._raise_scroll_bar(
            getattr(selection_list, "scroll_bar", None),
            FIELD_DROPDOWN_LAYER + 5,
        )
        for item in getattr(selection_list, "item_list", []):
            button = item.get("button_element")
            self._change_layer(button, FIELD_DROPDOWN_LAYER + 6)
            self._left_align_button(button)

    @staticmethod
    def _change_layer(element: Any, layer: int) -> None:
        if element is not None and hasattr(element, "change_layer"):
            element.change_layer(layer)

    def _raise_scroll_bar(self, scroll_bar: Any, layer: int) -> None:
        self._change_layer(scroll_bar, layer)
        for attr in ("top_button", "bottom_button", "sliding_button"):
            self._change_layer(getattr(scroll_bar, attr, None), layer + 1)

    def _style_event_menu_panel(self, panel: Any) -> None:
        if panel is None or not hasattr(panel, "rebuild"):
            return
        panel.background_colour = self.pygame.Color(30, 34, 39)
        panel.border_colour = self.pygame.Color(70, 76, 84)
        panel.border_width = {"left": 1, "right": 1, "top": 1, "bottom": 1}
        panel.shadow_width = 2
        panel.shape = "rectangle"
        panel.shape_corner_radius = [2, 2, 2, 2]
        panel.rebuild()

    def _style_event_menu_header(self, label: Any) -> None:
        if label is None or not hasattr(label, "rebuild"):
            return
        font_dict = self.manager.get_theme().get_font_dictionary()
        label.font = font_dict.find_font(14, "noto_sans", bold=True)
        label.bg_colour = self.pygame.Color(30, 34, 39, 0)
        label.text_colour = self.pygame.Color(224, 228, 232)
        label.text_horiz_alignment = "left"
        label.text_horiz_alignment_padding = 4
        label.text_vert_alignment = "center"
        label.text_vert_alignment_padding = 0
        label.rebuild()

    def _style_event_menu_button(self, button: Any, *, selected: bool = False) -> None:
        if button is None or not hasattr(button, "rebuild"):
            return
        if (
            getattr(button, "_totem_menu_styled", False)
            and getattr(button, "_totem_menu_selected", None) == selected
        ):
            return

        palette = {
            "normal_bg": (30, 34, 39),
            "hovered_bg": (49, 55, 63),
            "selected_bg": (43, 74, 96),
            "active_bg": (43, 74, 96),
            "normal_border": (30, 34, 39),
            "hovered_border": (49, 55, 63),
            "selected_border": (43, 74, 96),
            "active_border": (43, 74, 96),
            "normal_text": (208, 213, 218),
            "hovered_text": (236, 240, 244),
            "selected_text": (245, 247, 249),
            "active_text": (245, 247, 249),
        }
        for key, value in palette.items():
            button.colours[key] = self.pygame.Color(*value)
        button.border_width = {"left": 0, "right": 0, "top": 0, "bottom": 0}
        button.shadow_width = 0
        button.border_overlap = 0
        button.shape = "rectangle"
        button.shape_corner_radius = [0, 0, 0, 0]
        button.text_horiz_alignment = "left"
        button.text_horiz_alignment_padding = 18
        button.text_vert_alignment = "center"
        button.text_vert_alignment_padding = 0
        button._totem_left_aligned = True
        button._totem_menu_styled = True
        button._totem_menu_selected = selected
        button.rebuild()

    @staticmethod
    def _left_align_button(button: Any) -> None:
        if button is None or getattr(button, "_totem_left_aligned", False):
            return
        if not hasattr(button, "rebuild"):
            return
        button.text_horiz_alignment = "left"
        button.text_horiz_alignment_padding = 8
        button._totem_left_aligned = True
        button.rebuild()

    def _left_align_dropdown(self, dropdown: Any) -> None:
        state = getattr(dropdown, "current_state", None)
        self._left_align_button(getattr(state, "selected_option_button", None))
        selection_list = getattr(state, "options_selection_list", None)
        if selection_list is None:
            return
        for item in getattr(selection_list, "item_list", []):
            self._left_align_button(item.get("button_element"))

    def _resize(self, size: tuple[int, int]) -> None:
        self.window_size = (max(640, size[0]), max(420, size[1]))
        self.screen = self.pygame.display.set_mode(self.window_size, self.pygame.RESIZABLE)
        self.manager.set_window_resolution(self.window_size)
        self._build_ui(preserve_values=True)

    def _handle_ui_event(self, event: Any) -> None:
        gui = self.pygame_gui
        if event.type == gui.UI_BUTTON_PRESSED:
            if event.ui_element == self.event_button:
                self._toggle_event_menu()
            elif event.ui_element in self.event_menu_buttons:
                self._select_event(self.event_menu_buttons[event.ui_element])
            elif event.ui_element == self.connect_button:
                self.connection.connect(self.ip_entry.get_text().strip())
            elif event.ui_element == self.disconnect_button:
                self.connection.disconnect()
            elif event.ui_element == self.publish_button:
                self._publish_selected()
            elif event.ui_element == self.rebuild_button:
                self._rebuild_renderer()
            elif event.ui_element == self.prev_button:
                self._step_frame(-1)
            elif event.ui_element == self.next_button:
                self._step_frame(1)
            elif event.ui_element == self.play_button:
                self._render_or_toggle_playback()
        elif event.type == gui.UI_DROP_DOWN_MENU_CHANGED:
            self._refresh_preview_text()
        elif event.type == gui.UI_TEXT_ENTRY_CHANGED:
            if event.ui_element == self.frame_entry and not self._updating_frame_entry:
                self._jump_to_frame_text()
            elif event.ui_element != self.ip_entry:
                self._refresh_preview_text()

    @staticmethod
    def _dropdown_text(value: object) -> str:
        if isinstance(value, tuple) and value:
            return str(value[0])
        return str(value)

    @staticmethod
    def _event_category(event: EventDefinition) -> str:
        return event.label.split(" / ", 1)[0]

    @staticmethod
    def _event_display_text(event: EventDefinition) -> str:
        parts = event.label.split(" / ", 1)
        return parts[1] if len(parts) == 2 else event.label

    def _event_groups(self) -> dict[str, list[EventDefinition]]:
        groups: dict[str, list[EventDefinition]] = {}
        for event in self.events:
            groups.setdefault(self._event_category(event), []).append(event)
        return groups

    def _toggle_event_menu(self) -> None:
        if self.event_menu_container is None:
            self._open_event_menu()
        else:
            self._close_event_menu()

    def _open_event_menu(self) -> None:
        self._close_event_menu()
        self._close_form_dropdowns()
        pygame = self.pygame
        elements = self.pygame_gui.elements
        layout = self.layout
        assert layout is not None

        button_rect = self.event_button.relative_rect
        menu_x = button_rect.x
        menu_y = button_rect.bottom + 4
        menu_w = max(260, button_rect.width)
        menu_h = max(140, min(420, self.window_size[1] - menu_y - layout.margin))
        content_w = max(220, menu_w - 22)
        menu_rect = pygame.Rect(menu_x, menu_y, menu_w, menu_h)

        self.event_menu_panel = elements.UIPanel(
            relative_rect=menu_rect,
            starting_height=EVENT_MENU_LAYER,
            manager=self.manager,
        )
        self._style_event_menu_panel(self.event_menu_panel)
        self.event_menu_container = elements.UIScrollingContainer(
            relative_rect=menu_rect,
            manager=self.manager,
        )
        self.event_menu_elements = []
        self.event_menu_buttons = {}

        y = 6
        for category, events in self._event_groups().items():
            header = elements.UILabel(
                relative_rect=pygame.Rect(10, y, content_w - 18, 22),
                text=category,
                manager=self.manager,
                container=self.event_menu_container,
            )
            self._style_event_menu_header(header)
            self.event_menu_elements.append(header)
            self._change_layer(header, EVENT_MENU_LAYER + 2)
            y += 24
            for event in events:
                button = elements.UIButton(
                    relative_rect=pygame.Rect(8, y, content_w - 16, 24),
                    text=self._event_display_text(event),
                    manager=self.manager,
                    container=self.event_menu_container,
                )
                self._style_event_menu_button(button, selected=event == self.selected_event)
                if event == self.selected_event and hasattr(button, "select"):
                    button.select()
                self.event_menu_elements.append(button)
                self.event_menu_buttons[button] = event
                y += 24
            y += 8

        self.event_menu_container.set_scrollable_area_dimensions(
            (content_w, max(y + 8, menu_h)),
        )
        self._raise_event_menu()
        self.needs_redraw = True

    def _close_event_menu(self) -> None:
        if self.event_menu_container is not None:
            self.event_menu_container.kill()
        if self.event_menu_panel is not None:
            self.event_menu_panel.kill()
        self.event_menu_panel = None
        self.event_menu_container = None
        self.event_menu_elements = []
        self.event_menu_buttons = {}

    def _raise_event_menu(self) -> None:
        if self.event_menu_container is not None:
            self._change_layer(self.event_menu_panel, EVENT_MENU_LAYER)
            self.event_menu_container.change_layer(EVENT_MENU_LAYER + 1)
            self._change_layer(
                getattr(self.event_menu_container, "scrollable_container", None),
                EVENT_MENU_LAYER + 2,
            )
            self._raise_scroll_bar(
                getattr(self.event_menu_container, "vert_scroll_bar", None),
                EVENT_MENU_LAYER + 6,
            )
            self._raise_scroll_bar(
                getattr(self.event_menu_container, "horiz_scroll_bar", None),
                EVENT_MENU_LAYER + 6,
            )
            for element in self.event_menu_elements:
                self._change_layer(element, EVENT_MENU_LAYER + 4)
            for button in self.event_menu_buttons:
                self._change_layer(button, EVENT_MENU_LAYER + 5)
                self._style_event_menu_button(
                    button,
                    selected=self.event_menu_buttons[button] == self.selected_event,
                )

    def _select_event(self, event: EventDefinition) -> None:
        self.selected_event = event
        self._close_event_menu()
        if hasattr(self, "event_button"):
            self.event_button.set_text(event.label)
            self._left_align_button(self.event_button)
        self.topic_entry.set_text(str(event.topic))
        self._close_trace()
        self.preview_surface = None
        self.current_frame = 0
        self.playing = False
        self.play_accum = 0.0
        self._rebuild_form()
        self._raise_event_selector()
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
                self.selected_event.payload_model,
                command_defaults_values,
                y,
                command=True,
                fields=command_fields,
            )

        payload_model = self.selected_event.config_model or self.selected_event.payload_model
        if payload_model not in ANIMATION_COMMAND_MODELS or self.selected_event.config_model is not None:
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
        if event.payload_model not in ANIMATION_COMMAND_MODELS:
            return ()
        hidden = {"type", "kind", "payloadSize", "payload"}
        model = MODELS[event.payload_model]
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
            signature = self._render_signature()
            if signature != self.rendered_signature:
                self.render_dirty = True
                self.playing = False
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
        self._update_frame_controls()
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
            self.rendered_signature = self._render_signature()
            self.render_dirty = False
            self.result_label.set_text(f"Rendered {self._frame_count()} frames")
        except Exception as exc:
            self.preview_surface = None
            self._show_error("Render error", exc)
        self.needs_redraw = True

    def _rebuild_renderer(self) -> None:
        try:
            renderer_binary(Path(__file__).resolve().parents[2], force_rebuild=True)
            self.result_label.set_text("Renderer rebuilt")
            self.render_dirty = True
            self.playing = False
            self._update_frame_controls()
        except Exception as exc:
            self._show_error("Rebuild error", exc)
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

    def _render_or_toggle_playback(self) -> None:
        if self.render_dirty or self.trace is None:
            self._render_selected()
            if self.trace is None or self.render_dirty:
                return
            self.playing = True
            self.play_accum = 0.0
            self._update_frame_controls()
            self.needs_redraw = True
            return
        self._toggle_playback()

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
            if not self.selected_event.renderable:
                self.play_button.set_text("No Preview")
                self.play_button.disable()
                self._set_play_button_state("disabled")
            else:
                if not self.play_button.is_enabled:
                    self.play_button.enable()
                if self.render_dirty or self.trace is None:
                    self.play_button.set_text("Render + Play")
                    self._set_play_button_state("dirty")
                else:
                    self.play_button.set_text("Pause" if self.playing else "Play")
                    self._set_play_button_state("ready")
        if hasattr(self, "frame_label"):
            self.frame_label.set_text(f"{self.current_frame} / {max(0, count - 1)}")
        if hasattr(self, "frame_entry"):
            self._updating_frame_entry = True
            self.frame_entry.set_text(str(self.current_frame))
            self._updating_frame_entry = False

    def _set_play_button_state(self, state: str) -> None:
        if self._play_button_state == state:
            return
        self._play_button_state = state
        palette = {
            "dirty": ((145, 46, 42), (175, 61, 55), (104, 31, 31)),
            "ready": ((38, 118, 73), (48, 145, 88), (31, 88, 56)),
            "disabled": ((55, 58, 62), (55, 58, 62), (55, 58, 62)),
        }[state]
        normal, hovered, selected = (self.pygame.Color(*item) for item in palette)
        self.play_button.colours["normal_bg"] = normal
        self.play_button.colours["hovered_bg"] = hovered
        self.play_button.colours["selected_bg"] = selected
        self.play_button.colours["active_bg"] = selected
        self.play_button.rebuild()

    def _render_signature(self) -> str:
        return json.dumps(
            {
                "event": self.selected_event.label,
                "command": self._command_values(),
                "payload": self._payload_values(),
            },
            sort_keys=True,
            default=str,
        )

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
