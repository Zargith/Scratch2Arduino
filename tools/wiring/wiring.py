#!/usr/bin/env python3
"""Draws the wiring diagrams of the example projects (examples/projects/<name>/wiring.png).

Breadboard-style pictures, in the spirit of Tinkercad: an Arduino Uno, a breadboard, the
components and coloured jumper wires going to the pins used by the blocks of each example.

    python3 tools/wiring/wiring.py            # every example
    python3 tools/wiring/wiring.py thermometer

Needs Pillow (`pip install pillow`, or `uvx --from pillow python tools/wiring/wiring.py`).
When an example changes its pins, update its function at the end of this file and run it again.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
SCALE = 2  # drawn twice as big, then reduced: smooth edges
WIDTH, HEIGHT = 1600, 1000

# Wire colours, as in most kits: red for 5V, black for GND, bright colours for signals.
RED, BLACK = "#D32F2F", "#212121"
ORANGE, YELLOW, GREEN, BLUE = "#F57C00", "#FBC02D", "#388E3C", "#1976D2"
PURPLE, WHITE, BROWN = "#7B1FA2", "#F5F5F5", "#6D4C41"

FONT_FILES = [
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
    "/Library/Fonts/Arial.ttf",
    "C:/Windows/Fonts/arial.ttf",
]
BOLD_FILES = [
    "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
    "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
    "/Library/Fonts/Arial Bold.ttf",
    "C:/Windows/Fonts/arialbd.ttf",
]
_fonts: dict[tuple[int, bool], ImageFont.FreeTypeFont] = {}


def font(size: int, bold: bool = False) -> ImageFont.FreeTypeFont:
    key = (size, bold)
    if key not in _fonts:
        for candidate in BOLD_FILES if bold else FONT_FILES:
            if Path(candidate).exists():
                _fonts[key] = ImageFont.truetype(candidate, size * SCALE)
                break
        else:
            _fonts[key] = ImageFont.load_default(size * SCALE)
    return _fonts[key]


def darker(colour: str, factor: float = 0.6) -> str:
    r, g, b = (int(colour[i : i + 2], 16) for i in (1, 3, 5))
    return "#%02X%02X%02X" % (int(r * factor), int(g * factor), int(b * factor))


class Canvas:
    """Drawing in logical pixels (the image is SCALE times bigger until it is saved)."""

    def __init__(self, title: str) -> None:
        self.image = Image.new("RGB", (WIDTH * SCALE, HEIGHT * SCALE), "#ECEFF1")
        self.draw = ImageDraw.Draw(self.image)
        self.wires: list[tuple[list[tuple[float, float]], str]] = []
        self.text(24, 18, title, 26, "#263238", bold=True)

    @staticmethod
    def _p(points):
        return [(x * SCALE, y * SCALE) for x, y in points]

    def rect(self, x, y, w, h, fill, outline=None, radius=0, width=1):
        box = (x * SCALE, y * SCALE, (x + w) * SCALE, (y + h) * SCALE)
        self.draw.rounded_rectangle(box, radius * SCALE, fill=fill, outline=outline, width=width * SCALE)

    def circle(self, x, y, r, fill, outline=None, width=1):
        box = ((x - r) * SCALE, (y - r) * SCALE, (x + r) * SCALE, (y + r) * SCALE)
        self.draw.ellipse(box, fill=fill, outline=outline, width=width * SCALE)

    def line(self, points, colour, width=2):
        self.draw.line(self._p(points), fill=colour, width=int(width * SCALE), joint="curve")

    def polygon(self, points, fill, outline=None):
        self.draw.polygon(self._p(points), fill=fill, outline=outline)

    def text(self, x, y, text, size=12, colour="#FFFFFF", bold=False, anchor="la", angle=0):
        if angle == 0:
            self.draw.text((x * SCALE, y * SCALE), text, fill=colour, font=font(size, bold), anchor=anchor)
            return
        # Rotated label, centred on (x, y).
        f = font(size, bold)
        left, top, right, bottom = f.getbbox(text)
        label = Image.new("RGBA", (right - left + 4, bottom - top + 4), (0, 0, 0, 0))
        ImageDraw.Draw(label).text((2 - left, 2 - top), text, fill=colour, font=f)
        label = label.rotate(angle, expand=True)
        self.image.paste(label, (int(x * SCALE - label.width / 2), int(y * SCALE - label.height / 2)), label)

    def wire(self, points, colour):
        """Jumper wire through `points`, drawn on top of everything when the picture is saved."""
        self.wires.append((points, colour))

    def save(self, target: Path) -> None:
        for points, colour in self.wires:
            self.line(points, "#000000" if colour == BLACK else darker(colour, 0.55), 7)
            self.line(points, colour, 4.5)
            for x, y in (points[0], points[-1]):
                self.rect(x - 4, y - 4, 8, 8, "#424242", radius=1)
                self.circle(x, y, 2.2, "#BDBDBD")
        self.image.resize((WIDTH, HEIGHT), Image.LANCZOS).save(target, optimize=True)


def path(start, *moves):
    """Orthogonal wire: a start point, then ("x", value) / ("y", value) moves or points."""
    points = [start]
    x, y = start
    for move in moves:
        if move[0] == "x":
            x = move[1]
        elif move[0] == "y":
            y = move[1]
        else:
            x, y = move
        points.append((x, y))
    return points


# --- Arduino Uno -------------------------------------------------------------------------

class Uno:
    """Arduino Uno seen from above, USB plug on the left."""

    TOP = ["SCL", "SDA", "AREF", "GND", "13", "12", "~11", "~10", "~9", "8", None,
           "7", "~6", "~5", "4", "~3", "2", "TX 1", "RX 0"]
    BOTTOM = [None, "IOREF", "RESET", "3.3V", "5V", "GND", "GND", "VIN", None, "A0", "A1", "A2", "A3", "A4", "A5"]
    PITCH = 17

    def __init__(self, c: Canvas, x: float, y: float) -> None:
        self.c, self.x, self.y = c, x, y
        self.w, self.h = 520, 400
        self.pins: dict[str, tuple[float, float]] = {}
        self._draw()

    def _header(self, labels, start_x, y, label_y, power=False):
        c = self.c
        x = start_x
        for label in labels:
            if label is None:
                x += self.PITCH * 0.6
                continue
            c.rect(x - 8, y - 8, 16, 16, "#1E1E1E")
            c.rect(x - 3.5, y - 3.5, 7, 7, "#3A3A3A")
            name = label.replace("~", "").replace("TX ", "").replace("RX ", "")
            if power and name == "GND" and "GND_power" not in self.pins:
                self.pins["GND_power"] = (x, y)  # the GND next to 5V, used for the power wires
            self.pins.setdefault(name, (x, y))
            c.text(x, label_y, label, 9, "#FFFFFF", angle=90)
            x += self.PITCH

    def _draw(self):
        c, x, y, w, h = self.c, self.x, self.y, self.w, self.h
        c.rect(x + 4, y + 6, w, h, "#B0BEC5", radius=18)  # shadow
        c.rect(x, y, w, h, "#00878F", outline="#006468", radius=18, width=2)
        for hx, hy in ((x + 22, y + 22), (x + w - 22, y + 22), (x + 22, y + h - 22), (x + w - 70, y + h - 22)):
            c.circle(hx, hy, 9, "#ECEFF1", outline="#B0BEC5", width=2)
        # USB plug and power jack on the left edge.
        c.rect(x - 24, y + 60, 96, 74, "#B0BEC5", outline="#78909C", radius=4, width=2)
        c.rect(x - 16, y + 70, 70, 54, "#CFD8DC", radius=3)
        c.rect(x - 18, y + 270, 78, 60, "#212121", radius=6)
        c.circle(x + 6, y + 300, 13, "#424242")
        # Microcontroller.
        c.rect(x + 250, y + 230, 210, 54, "#263238", radius=3)
        for i in range(14):
            c.rect(x + 256 + i * 14.7, y + 222, 6, 8, "#B0BEC5")
            c.rect(x + 256 + i * 14.7, y + 284, 6, 8, "#B0BEC5")
        c.text(x + 355, y + 257, "ATMEGA328P", 11, "#90A4AE", anchor="mm")
        c.text(x + 200, y + 150, "UNO", 46, "#FFFFFF", bold=True, anchor="mm")
        c.text(x + 200, y + 192, "ARDUINO", 16, "#B2EBF2", bold=True, anchor="mm")
        # Built-in LED "L", wired to pin 13.
        c.rect(x + 190, y + 100, 12, 7, "#FFEB3B")
        c.text(x + 210, y + 103, "L", 10, anchor="lm")
        # Headers.
        self._header(self.TOP, x + 160, y + 18, y + 54)
        self._header(self.BOTTOM, x + 175, y + h - 18, y + h - 52, power=True)
        c.text(x + 330, y + 96, "DIGITAL (PWM ~)", 11, "#E0F7FA", anchor="mm")
        c.text(x + 255, y + h - 92, "POWER", 11, "#E0F7FA", anchor="mm")
        c.text(x + 420, y + h - 92, "ANALOG IN", 11, "#E0F7FA", anchor="mm")

    def pin(self, name: str) -> tuple[float, float]:
        return self.pins[name]


# --- Breadboard --------------------------------------------------------------------------

class Breadboard:
    """Columns 1..n; rows a-e and f-j (joined by column), + and - rails at the top and bottom."""

    ROWS = "abcdefghij"
    RAIL_LINE = {"top-": -11, "top+": 11, "bot+": -11, "bot-": 11}  # coloured line beside each rail

    def __init__(self, c: Canvas, x: float, y: float, columns: int = 30) -> None:
        self.c, self.x, self.y, self.columns = c, x, y, columns
        self.pitch = 20
        self.w = columns * self.pitch + 40
        self.h = 362
        self._draw()

    def _row_y(self, row: str) -> float:
        index = self.ROWS.index(row)
        return self.y + 78 + index * self.pitch + (24 if index >= 5 else 0)

    def _rail_y(self, rail: str) -> float:
        return {"top-": self.y + 18, "top+": self.y + 38,
                "bot+": self.y + self.h - 38, "bot-": self.y + self.h - 18}[rail]

    def _col_x(self, column: int) -> float:
        return self.x + 30 + (column - 1) * self.pitch

    def hole(self, column: int, row: str) -> tuple[float, float]:
        return self._col_x(column), self._row_y(row)

    def rail(self, rail: str, column: int) -> tuple[float, float]:
        return self._col_x(column), self._rail_y(rail)

    def _draw(self):
        c, x, y = self.c, self.x, self.y
        c.rect(x + 4, y + 6, self.w, self.h, "#B0BEC5", radius=8)
        c.rect(x, y, self.w, self.h, "#FAFAFA", outline="#CFD8DC", radius=8, width=2)
        c.rect(x + 10, y + 176, self.w - 20, 14, "#E0E0E0", radius=3)  # middle channel
        for rail, offset in self.RAIL_LINE.items():
            colour = BLUE if rail.endswith("-") else RED
            ry = self._rail_y(rail)
            c.line([(x + 18, ry + offset), (x + self.w - 18, ry + offset)], colour, 2)
            c.text(x + 10, ry, "-" if rail.endswith("-") else "+", 13, colour, bold=True, anchor="mm")
            for column in range(1, self.columns + 1):
                if (column - 1) % 6 != 5:
                    c.rect(self._col_x(column) - 3.5, ry - 3.5, 7, 7, "#9E9E9E", radius=1)
        for row in self.ROWS:
            ry = self._row_y(row)
            c.text(x + 14, ry, row, 10, "#90A4AE", anchor="mm")
            for column in range(1, self.columns + 1):
                c.rect(self._col_x(column) - 3.5, ry - 3.5, 7, 7, "#9E9E9E", radius=1)
        for column in (1, 5, 10, 15, 20, 25, 30):
            if column <= self.columns:
                c.text(self._col_x(column), y + 64, str(column), 9, "#90A4AE", anchor="mm")


# --- Components ----------------------------------------------------------------------------

def leg(c: Canvas, top, bottom, colour="#9E9E9E"):
    c.line([top, bottom], colour, 2.5)


def resistor(c: Canvas, a, b, label="220 Ω"):
    """Resistor between holes a and b."""
    leg(c, a, b)
    mx, my = (a[0] + b[0]) / 2, (a[1] + b[1]) / 2
    vertical = abs(a[0] - b[0]) < abs(a[1] - b[1])
    w, h = (12, 30) if vertical else (30, 12)
    c.rect(mx - w / 2, my - h / 2, w, h, "#D7CCC8", outline="#A1887F", radius=5)
    for i, colour in enumerate(("#E53935", "#E53935", "#6D4C41")):  # 220 Ω: red, red, brown
        offset = -8 + i * 6
        if vertical:
            c.rect(mx - w / 2, my + offset - 1.5, w, 3, colour)
        else:
            c.rect(mx + offset - 1.5, my - h / 2, 3, h, colour)
    if label:
        c.text(mx + 9, my + 22, label, 9, "#455A64", anchor="mm")


def rgb_led(c: Canvas, board: Breadboard, column: int, row: str = "c"):
    """Common-cathode RGB LED, legs R, - (longest), G, B in four neighbouring columns."""
    holes = [board.hole(column + i, row) for i in range(4)]
    cx = (holes[0][0] + holes[3][0]) / 2
    top = holes[0][1] - 62
    for i, (hx, hy) in enumerate(holes):
        leg(c, (cx - 9 + i * 6, top + 30), (hx, hy))
    c.rect(cx - 17, top + 22, 34, 10, "#E0E0E0", radius=2)
    c.rect(cx - 15, top - 6, 30, 32, "#F5F5F5", outline="#BDBDBD", radius=14)
    c.circle(cx - 5, top + 6, 4, "#EF5350")
    c.circle(cx + 5, top + 6, 4, "#66BB6A")
    c.circle(cx, top + 14, 4, "#42A5F5")
    for i, name in enumerate(("R", "-", "G", "B")):
        c.text(holes[i][0] + 9, holes[i][1] - 12, name, 9, "#455A64", bold=True, anchor="mm")
    return holes


def led(c: Canvas, board: Breadboard, column: int, colour: str, row: str = "c"):
    """5 mm LED: + leg (longest, anode) in `column`, - leg in the next column."""
    plus, minus = board.hole(column, row), board.hole(column + 1, row)
    cx, top = (plus[0] + minus[0]) / 2, plus[1] - 58
    leg(c, (cx - 4, top + 30), plus)
    leg(c, (cx + 4, top + 30), minus)
    c.rect(cx - 14, top + 22, 28, 8, colour, outline=darker(colour), radius=2)
    c.rect(cx - 12, top - 4, 24, 30, colour, outline=darker(colour), radius=12)
    c.circle(cx - 4, top + 6, 3, "#FFFFFF")
    c.text(plus[0] - 8, plus[1] + 1, "+", 10, "#455A64", bold=True, anchor="mm")
    return plus, minus


def buzzer(c: Canvas, board: Breadboard, column: int, row: str = "c"):
    """Piezo buzzer, + leg in `column`, - leg two columns further."""
    plus, minus = board.hole(column, row), board.hole(column + 2, row)
    cx, cy = (plus[0] + minus[0]) / 2, plus[1] - 34
    leg(c, (cx - 8, cy), plus)
    leg(c, (cx + 8, cy), minus)
    c.circle(cx, cy, 25, "#212121", outline="#000000", width=2)
    c.circle(cx, cy, 5, "#424242")
    c.text(cx - 13, cy - 13, "+", 13, "#FFFFFF", bold=True, anchor="mm")
    return plus, minus


def button(c: Canvas, board: Breadboard, column: int):
    """Push button across the middle channel: pressing joins `column` and `column` + 2."""
    holes = [board.hole(column, "e"), board.hole(column + 2, "e"), board.hole(column, "f"), board.hole(column + 2, "f")]
    c.rect(holes[0][0] - 8, holes[0][1] - 6, 56, holes[2][1] - holes[0][1] + 12, "#90A4AE", outline="#607D8B",
           radius=4, width=2)
    c.circle(holes[0][0] + 20, (holes[0][1] + holes[2][1]) / 2, 13, "#263238")
    return holes


def potentiometer(c: Canvas, board: Breadboard, column: int, row: str = "f"):
    """10 kΩ potentiometer, legs in three neighbouring columns."""
    holes = [board.hole(column + i, row) for i in range(3)]
    cx, cy = holes[1][0], holes[1][1] + 34
    for hx, hy in holes:
        leg(c, (hx, cy - 16), (hx, hy))
    c.rect(cx - 30, cy - 22, 60, 44, "#1565C0", outline="#0D47A1", radius=4, width=2)
    c.circle(cx, cy, 16, "#ECEFF1", outline="#90A4AE", width=2)
    c.line([(cx, cy), (cx + 8, cy - 12)], "#455A64", 3)
    c.text(cx + 36, cy, "10 kΩ", 10, "#455A64", anchor="lm")
    return holes


def module(c: Canvas, x, y, w, h, colour, pins, side="bottom", pitch=20, label=None, label_colour="#FFFFFF"):
    """PCB module with a row of header pins on one side; returns pin name -> wire end."""
    c.rect(x + 3, y + 5, w, h, "#B0BEC5", radius=6)
    c.rect(x, y, w, h, colour, outline=darker(colour), radius=6, width=2)
    points = {}
    first = (w if side in ("bottom", "top") else h) / 2 - (len(pins) - 1) * pitch / 2
    for i, name in enumerate(pins):
        if side in ("bottom", "top"):
            px = x + first + i * pitch
            py = y + h + 10 if side == "bottom" else y - 10
            c.rect(px - 7, py - 6, 14, 12, "#212121", radius=1)
            c.text(px, y + h - 14 if side == "bottom" else y + 14, name, 9, label_colour, anchor="mm",
                   angle=90 if len(name) > 3 else 0)
        else:
            py = y + first + i * pitch
            px = x - 10 if side == "left" else x + w + 10
            c.rect(px - 6, py - 7, 12, 14, "#212121", radius=1)
            c.text(x + 8 if side == "left" else x + w - 8, py, name, 9, label_colour,
                   anchor="lm" if side == "left" else "rm")
        points[name] = (px, py)
    if label:
        c.text(x + w / 2, y + 12, label, 11, label_colour, bold=True, anchor="mm")
    return points


def servo(c: Canvas, x, y):
    """SG90 servo with its brown/red/orange cable ending in a connector."""
    c.rect(x + 3, y + 5, 120, 64, "#B0BEC5", radius=4)
    c.rect(x - 14, y + 22, 148, 10, "#1E88E5", outline="#1565C0", radius=2)
    c.rect(x, y, 120, 64, "#1E88E5", outline="#1565C0", radius=4, width=2)
    c.polygon([(x + 30, y + 20), (x + 38, y + 20), (x + 36, y - 34), (x + 32, y - 34)], "#FAFAFA", "#BDBDBD")
    c.circle(x + 34, y + 20, 15, "#FAFAFA", outline="#BDBDBD", width=2)
    c.circle(x + 34, y + 20, 5, "#E0E0E0")
    c.text(x + 82, y + 46, "SG90", 12, "#FFFFFF", bold=True, anchor="mm")
    for i, colour in enumerate((BROWN, RED, ORANGE)):
        c.line([(x + 94 + i * 6, y + 64), (x + 88 + i * 12, y + 104)], colour, 4)
    c.rect(x + 80, y + 102, 40, 16, "#212121", radius=2)
    pins = {name: (x + 88 + i * 12, y + 118) for i, name in enumerate(("GND", "5V", "SIG"))}
    c.text(x + 128, y + 110, "marron GND · rouge 5V · orange SIG", 9, "#455A64", anchor="lm")
    return pins


def ultrasonic(c: Canvas, x, y):
    pins = module(c, x, y, 200, 110, "#1565C0", ["VCC", "Trig", "Echo", "Gnd"], label="HC-SR04")
    for cx in (x + 42, x + 158):
        c.circle(cx, y + 50, 28, "#CFD8DC", outline="#90A4AE", width=3)
        c.circle(cx, y + 50, 19, "#37474F")
        for r in range(5, 18, 5):
            c.circle(cx, y + 50, r, None, outline="#546E7A", width=1)
    return pins


def dht11(c: Canvas, x, y):
    pins = module(c, x, y, 96, 140, "#1565C0", ["VCC", "DATA", "GND"], pitch=26)
    c.rect(x + 18, y + 18, 60, 70, "#42A5F5", outline="#1E88E5", radius=4, width=2)
    for row in range(5):
        for col in range(4):
            c.rect(x + 25 + col * 13, y + 26 + row * 12, 8, 6, "#1565C0", radius=1)
    c.text(x + 48, y + 100, "DHT11", 11, "#FFFFFF", bold=True, anchor="mm")
    return pins


def lcd_i2c(c: Canvas, x, y, lines=("Temperature (C)", "21")):
    pins = module(c, x, y, 340, 120, "#2E7D32", ["GND", "VCC", "SDA", "SCL"], side="left")
    c.rect(x + 50, y + 14, 270, 88, "#1B1B1B", radius=4)
    c.rect(x + 60, y + 24, 250, 68, "#64B5F6", radius=2)
    for i, line in enumerate(lines):
        c.text(x + 68, y + 42 + i * 30, line, 18, "#0D47A1", anchor="lm")
    c.text(x + 185, y + 111, "LCD 16x2 I2C", 9, "#C8E6C9", anchor="mm")
    return pins


LCD16_PINS = ["VSS", "VDD", "V0", "RS", "RW", "E", "D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7", "A", "K"]


def lcd16(c: Canvas, x, y, lines=("Temperature (C)", "21")):
    """16x2 LCD without I2C module: its 16 pins in a row under the screen."""
    pins = module(c, x, y, 600, 160, "#2E7D32", LCD16_PINS, side="bottom", pitch=32)
    c.rect(x + 40, y + 14, 520, 104, "#1B1B1B", radius=4)
    c.rect(x + 52, y + 26, 496, 80, "#64B5F6", radius=2)
    for i, line in enumerate(lines):
        c.text(x + 64, y + 46 + i * 36, line, 22, "#0D47A1", anchor="lm")
    c.text(x + 300, y - 8, "LCD 16x2 (16 broches / pins)", 10, "#455A64", anchor="mm")
    return pins


def potentiometer_free(c: Canvas, x, y):
    """10 kΩ potentiometer seen from above, its three legs pointing down; returns leg -> wire end."""
    c.rect(x + 3, y + 5, 64, 48, "#B0BEC5", radius=4)
    c.rect(x, y, 64, 48, "#1565C0", outline="#0D47A1", radius=4, width=2)
    c.circle(x + 32, y + 24, 16, "#ECEFF1", outline="#90A4AE", width=2)
    c.line([(x + 32, y + 24), (x + 40, y + 12)], "#455A64", 3)
    c.text(x + 32, y - 9, "10 kΩ", 10, "#455A64", anchor="mm")
    legs = {}
    for i, name in enumerate(("-", "W", "+")):
        lx = x + 12 + i * 20
        leg(c, (lx, y + 48), (lx, y + 66))
        legs[name] = (lx, y + 66)
    return legs


def legend(c: Canvas, x, y, rows, title):
    """Small table: one line per connection, for the wires that are hard to follow."""
    c.rect(x, y, 330, 26 + 17 * len(rows), "#FFFFFF", outline="#CFD8DC", radius=6, width=2)
    c.text(x + 12, y + 14, title, 11, "#263238", bold=True, anchor="lm")
    for i, (left, right) in enumerate(rows):
        c.text(x + 12, y + 34 + i * 17, left, 10, "#37474F", bold=True, anchor="lm")
        c.text(x + 100, y + 34 + i * 17, right, 10, "#37474F", anchor="lm")


def oled(c: Canvas, x, y, lines=("Jeu de reaction", "", "3")):
    pins = module(c, x, y, 150, 120, "#1A237E", ["GND", "VCC", "SCL", "SDA"], side="top", pitch=26)
    c.rect(x + 12, y + 28, 126, 70, "#000000", radius=3)
    for i, line in enumerate(lines):
        c.text(x + 18, y + 40 + i * 18, line, 11, "#4FC3F7", anchor="lm")
    c.text(x + 75, y + 110, "OLED SSD1306", 9, "#C5CAE9", anchor="mm")
    return pins


def matrix(c: Canvas, x, y, image=(0x00, 0x66, 0xFF, 0xFF, 0xFF, 0x7E, 0x3C, 0x18)):
    pins = module(c, x, y, 230, 190, "#1565C0", ["VCC", "GND", "DIN", "CS", "CLK"], side="left", pitch=24)
    c.rect(x + 58, y + 14, 160, 160, "#212121", radius=4)
    for row in range(8):
        for col in range(8):
            on = image[row] & (0x80 >> col)
            c.circle(x + 69 + col * 20, y + 25 + row * 20, 7, "#FF1744" if on else "#4E342E")
    c.text(x + 138, y + 182, "MAX7219", 9, "#BBDEFB", anchor="mm")
    return pins


def neopixel(c: Canvas, x, y, count=8, lit=5):
    w = 60 + count * 50
    c.rect(x + 3, y + 5, w, 60, "#B0BEC5", radius=4)
    c.rect(x, y, w, 60, "#FAFAFA", outline="#BDBDBD", radius=4, width=2)
    for i in range(count):
        cx = x + 75 + i * 50
        c.rect(cx - 15, y + 15, 30, 30, "#FFFFFF", outline="#9E9E9E", radius=3)
        c.circle(cx, y + 30, 10, "#66BB6A" if i < lit else "#EEEEEE", outline="#BDBDBD")
    pins = {}
    for i, name in enumerate(("5V", "DIN", "GND")):
        py = y + 12 + i * 18
        c.rect(x + 6, py - 6, 16, 12, "#FFB300", radius=2)
        c.text(x + 26, py, name, 9, "#455A64", anchor="lm")
        pins[name] = (x + 14, py)
    c.text(x + w / 2, y + 76, "NeoPixel WS2812 x %d" % count, 10, "#455A64", anchor="mm")
    return pins


def power_rails(c: Canvas, uno: Uno, board: Breadboard, lane_y: float, plus="bot+", minus="bot-"):
    """5V and GND of the Arduino to the + and - rails of the breadboard."""
    five, gnd = uno.pin("5V"), uno.pin("GND_power")
    c.wire(path(five, ("y", lane_y + 14), ("x", board.x - 26), ("y", board.rail(plus, 1)[1]), board.rail(plus, 1)), RED)
    c.wire(path(gnd, ("y", lane_y), ("x", board.x - 12), ("y", board.rail(minus, 2)[1]), board.rail(minus, 2)), BLACK)


# --- Examples ------------------------------------------------------------------------------

def blink(c: Canvas):
    uno = Uno(c, 540, 330)
    c.circle(uno.x + 196, uno.y + 103, 24, None, outline="#FBC02D", width=4)
    c.text(800, 250, "La LED « L » de la carte est reliée à la broche 13 : aucun câblage.", 18, "#37474F",
           anchor="mm")
    c.text(800, 278, "The on-board \"L\" LED is wired to pin 13: nothing to wire.", 15, "#607D8B", anchor="mm")


def potentiometer_on_board(c: Canvas, uno: Uno, board: Breadboard, column: int, lane_y: float):
    """Potentiometer in the lower half: - leg to GND, + leg to 5V, middle leg to A0."""
    potentiometer(c, board, column)
    c.wire(path(board.hole(column, "j"), ("y", board.rail("bot-", column)[1])), BLACK)
    c.wire(path(board.hole(column + 2, "j"), ("y", board.rail("bot+", column + 2)[1])), RED)
    middle = board.hole(column + 1, "j")
    c.wire(path(uno.pin("A0"), ("y", lane_y), ("x", middle[0] + 0), middle), YELLOW)


def servo_potentiometer(c: Canvas):
    uno = Uno(c, 60, 300)
    board = Breadboard(c, 700, 450, 26)
    power_rails(c, uno, board, uno.y + uno.h + 40)
    potentiometer_on_board(c, uno, board, 6, board.y + board.h + 40)
    s = servo(c, 1060, 130)
    c.wire(path(uno.pin("9"), ("y", 270), ("x", s["SIG"][0]), s["SIG"]), ORANGE)
    right = board.x + board.w
    c.wire(path(board.rail("bot+", 25), ("x", right + 20), ("y", 330), ("x", s["5V"][0]), s["5V"]), RED)
    c.wire(path(board.rail("bot-", 26), ("x", right + 36), ("y", 345), ("x", s["GND"][0] - 0), s["GND"]), BLACK)


def distance_alarm(c: Canvas):
    uno = Uno(c, 60, 380)
    board = Breadboard(c, 720, 480, 30)
    power_rails(c, uno, board, uno.y + uno.h + 60)
    rgb_led(c, board, 4)
    for i in (0, 2, 3):  # one resistor per colour, across the middle channel
        resistor(c, board.hole(4 + i, "e"), board.hole(4 + i, "f"), "")
    channel_y = (board.hole(1, "e")[1] + board.hole(1, "f")[1]) / 2
    c.text(board.hole(8, "e")[0] + 4, channel_y, "3 × 220 Ω", 10, "#455A64", anchor="lm")
    # Longest leg of the LED (column 5) and - of the buzzer: jumpers down to the - rail.
    c.wire(path(board.hole(5, "e"), ("y", board.rail("bot-", 5)[1])), BLACK)
    buzzer(c, board, 15)
    c.wire(path(board.hole(17, "e"), ("y", board.rail("bot-", 17)[1])), BLACK)
    sensor = ultrasonic(c, 1220, 120)
    # Signals: up from the digital header, right above the breadboard, down its left side.
    for pin, column, row, lane, lane_x, colour in (("9", 4, "g", 340, 680, RED), ("10", 6, "h", 325, 692, GREEN),
                                                   ("6", 7, "i", 310, 704, BLUE)):
        c.wire(path(uno.pin(pin), ("y", lane), ("x", lane_x), ("y", board.hole(column, row)[1]),
                    board.hole(column, row)), colour)
    c.wire(path(uno.pin("8"), ("y", 295), ("x", board.hole(15, "a")[0]), board.hole(15, "a")), PURPLE)
    c.wire(path(uno.pin("12"), ("y", 265), ("x", sensor["Trig"][0]), sensor["Trig"]), YELLOW)
    c.wire(path(uno.pin("11"), ("y", 280), ("x", sensor["Echo"][0]), sensor["Echo"]), ORANGE)
    right = board.x + board.w
    c.wire(path(board.rail("bot+", 29), ("x", right + 20), ("y", 300), ("x", sensor["VCC"][0]), sensor["VCC"]), RED)
    c.wire(path(board.rail("bot-", 30), ("x", right + 36), ("y", 315), ("x", sensor["Gnd"][0]), sensor["Gnd"]), BLACK)


def thermometer(c: Canvas):
    uno = Uno(c, 60, 420)
    board = Breadboard(c, 700, 580, 24)
    power_rails(c, uno, board, uno.y + uno.h + 40, plus="top+", minus="top-")
    sensor = dht11(c, 790, 220)
    screen = lcd_i2c(c, 1180, 260)
    c.wire(path(uno.pin("3"), ("y", 400), ("x", sensor["DATA"][0]), sensor["DATA"]), YELLOW)
    c.wire(path(board.rail("top+", 4), ("x", sensor["VCC"][0]), sensor["VCC"]), RED)
    c.wire(path(board.rail("top-", 8), ("x", sensor["GND"][0]), sensor["GND"]), BLACK)
    c.wire(path(board.rail("top-", 20), ("y", 530), ("x", 1110), ("y", screen["GND"][1]), screen["GND"]), BLACK)
    c.wire(path(board.rail("top+", 22), ("y", 545), ("x", 1125), ("y", screen["VCC"][1]), screen["VCC"]), RED)
    c.wire(path(uno.pin("A4"), ("y", uno.y + uno.h + 100), ("x", 1570), ("y", 230), ("x", 1140),
                ("y", screen["SDA"][1]), screen["SDA"]), BLUE)
    c.wire(path(uno.pin("A5"), ("y", uno.y + uno.h + 86), ("x", 1555), ("y", 244), ("x", 1152),
                ("y", screen["SCL"][1]), screen["SCL"]), GREEN)


def potentiometer_upper(c: Canvas, board: Breadboard, column: int):
    """10 kΩ potentiometer in the upper half: legs in row e, wires from row a (above the knob)."""
    holes = [board.hole(column + i, "e") for i in range(3)]
    cx, cy = holes[1][0], holes[1][1] - 34
    for hx, hy in holes:
        leg(c, (hx, cy + 16), (hx, hy))
    c.rect(cx - 30, cy - 22, 60, 44, "#1565C0", outline="#0D47A1", radius=4, width=2)
    c.circle(cx, cy, 16, "#ECEFF1", outline="#90A4AE", width=2)
    c.line([(cx, cy), (cx + 8, cy - 12)], "#455A64", 3)
    c.text(cx + 36, cy, "10 kΩ", 10, "#455A64", anchor="lm")
    return [board.hole(column + i, "a") for i in range(3)]


def thermometer_lcd16(c: Canvas):
    """Same program as the thermometer, with a 16-pin LCD. The Uno sits on the right so that the
    six signal wires reach the screen without crossing (RS 12, E 11, D4 10, D5 9, D6 8, D7 7)."""
    uno = Uno(c, 1040, 540)
    board = Breadboard(c, 60, 540, 30)
    screen = lcd16(c, 560, 90)
    sensor = module(c, 150, 190, 120, 140, "#1565C0", ["DATA", "VCC", "GND"], side="left", pitch=26)
    c.rect(198, 208, 60, 70, "#42A5F5", outline="#1E88E5", radius=4, width=2)
    for row in range(5):
        for col in range(4):
            c.rect(205 + col * 13, 216 + row * 12, 8, 6, "#1565C0", radius=1)
    c.text(228, 300, "DHT11", 11, "#FFFFFF", bold=True, anchor="mm")

    # Power: 5V and GND of the Uno to the + and - rails, under the breadboard.
    five, gnd = uno.pin("5V"), uno.pin("GND_power")
    c.wire(path(five, ("y", 962), ("x", 40), ("y", board.rail("top+", 1)[1]), board.rail("top+", 1)), RED)
    c.wire(path(gnd, ("y", 976), ("x", 26), ("y", board.rail("top-", 2)[1]), board.rail("top-", 2)), BLACK)

    # Signals, from the digital header up and left to the screen.
    for index, (pin, name, colour) in enumerate((("12", "RS", ORANGE), ("11", "E", YELLOW), ("10", "D4", GREEN),
                                                 ("9", "D5", BLUE), ("8", "D6", PURPLE), ("7", "D7", WHITE))):
        c.wire(path(uno.pin(pin), ("y", 292 + index * 15), ("x", screen[name][0]), screen[name]), colour)

    def lane(rail, column, y, target, colour):
        c.wire(path(board.rail(rail, column), ("y", y), ("x", target[0]), target), colour)

    # Screen power and contrast (potentiometer: - to GND, + to 5V, middle leg to V0).
    lane("top-", 13, 392, screen["VSS"], BLACK)
    lane("top+", 14, 404, screen["VDD"], RED)
    minus, middle, plus = potentiometer_upper(c, board, 19)
    c.wire(path(minus, board.rail("top-", 19)), BLACK)
    c.wire(path(plus, board.rail("top+", 21)), RED)
    c.wire(path(middle, ("y", 416), ("x", screen["V0"][0]), screen["V0"]), BROWN)
    lane("top-", 22, 428, screen["RW"], BLACK)
    # Backlight: A through a 220 Ω resistor to 5V, K to GND.
    a = screen["A"]
    lane("top+", 23, 452, (a[0], 436), RED)
    resistor(c, (a[0], 436), (a[0], 400), "")
    c.text(a[0] - 12, 418, "220 Ω", 10, "#455A64", anchor="rm")
    c.wire(path((a[0], 400), a), RED)
    lane("top-", 25, 466, screen["K"], BLACK)

    # DHT11: data to pin 3 over the top of the screen, power from the rails.
    c.wire(path(uno.pin("3"), ("y", 60), ("x", 96), ("y", sensor["DATA"][1]), sensor["DATA"]), YELLOW)
    c.wire(path(board.rail("top+", 3), ("y", 512), ("x", 112), ("y", sensor["VCC"][1]), sensor["VCC"]), RED)
    c.wire(path(board.rail("top-", 4), ("y", 500), ("x", 126), ("y", sensor["GND"][1]), sensor["GND"]), BLACK)

    legend(c, 670, 640, [("VSS, RW, K", "GND (-)"), ("VDD", "5V (+)"), ("V0", "potentiomètre / potentiometer"),
                        ("A", "220 Ω → 5V (+)"), ("RS, E", "12, 11"), ("D4…D7", "10, 9, 8, 7"),
                        ("D0…D3", "rien / not wired")], "LCD 16 broches / 16-pin LCD")


def reaction_game(c: Canvas):
    uno = Uno(c, 60, 430)
    board = Breadboard(c, 700, 600, 24)
    power_rails(c, uno, board, uno.y + uno.h + 36, plus="top+", minus="top-")
    led_matrix = matrix(c, 1320, 120)
    screen = oled(c, 800, 150)
    button(c, board, 4)  # pressing joins columns 4 and 6
    buzzer(c, board, 12)
    c.wire(path(board.hole(6, "a"), board.rail("top-", 6)), BLACK)
    c.wire(path(board.hole(14, "a"), board.rail("top-", 14)), BLACK)
    c.wire(path(uno.pin("2"), ("y", 390), ("x", 670), ("y", board.hole(4, "i")[1]), board.hole(4, "i")), WHITE)
    c.wire(path(uno.pin("8"), ("y", 375), ("x", 685), ("y", board.hole(12, "b")[1]), board.hole(12, "b")), PURPLE)
    c.wire(path(uno.pin("12"), ("y", 350), ("x", 1260), ("y", led_matrix["DIN"][1]), led_matrix["DIN"]), YELLOW)
    c.wire(path(uno.pin("10"), ("y", 320), ("x", 1290), ("y", led_matrix["CS"][1]), led_matrix["CS"]), ORANGE)
    c.wire(path(uno.pin("11"), ("y", 335), ("x", 1275), ("y", led_matrix["CLK"][1]), led_matrix["CLK"]), GREEN)
    c.wire(path(board.rail("top+", 20), ("y", 570), ("x", 1230), ("y", led_matrix["VCC"][1]), led_matrix["VCC"]), RED)
    c.wire(path(board.rail("top-", 22), ("y", 585), ("x", 1215), ("y", led_matrix["GND"][1]), led_matrix["GND"]),
           BLACK)
    c.wire(path(board.rail("top-", 3), ("y", 555), ("x", 740), ("y", 110), ("x", screen["GND"][0]), screen["GND"]),
           BLACK)
    c.wire(path(board.rail("top+", 4), ("y", 540), ("x", 755), ("y", 95), ("x", screen["VCC"][0]), screen["VCC"]), RED)
    c.wire(path(uno.pin("A5"), ("y", uno.y + uno.h + 82), ("x", 26), ("y", 80), ("x", screen["SCL"][0]),
                screen["SCL"]), GREEN)
    c.wire(path(uno.pin("A4"), ("y", uno.y + uno.h + 68), ("x", 40), ("y", 65), ("x", screen["SDA"][0]),
                screen["SDA"]), BLUE)


def light_gauge(c: Canvas):
    uno = Uno(c, 60, 360)
    board = Breadboard(c, 700, 500, 24)
    power_rails(c, uno, board, uno.y + uno.h + 40)
    potentiometer_on_board(c, uno, board, 6, board.y + board.h + 40)
    strip = neopixel(c, 760, 200)
    c.wire(path(uno.pin("5"), ("y", strip["DIN"][1]), strip["DIN"]), GREEN)
    right = board.x + board.w
    c.wire(path(board.rail("bot+", 22), ("x", right + 80), ("y", 150), ("x", 730), ("y", strip["5V"][1]),
                strip["5V"]), RED)
    c.wire(path(board.rail("bot-", 24), ("x", right + 96), ("y", 135), ("x", 715), ("y", strip["GND"][1]),
                strip["GND"]), BLACK)


def traffic_light(c: Canvas):
    uno = Uno(c, 60, 380)
    board = Breadboard(c, 720, 480, 24)
    power_rails(c, uno, board, uno.y + uno.h + 60)
    for index, (pin, colour, wire) in enumerate((("12", "#E53935", RED), ("11", "#FB8C00", ORANGE),
                                                 ("10", "#43A047", GREEN))):
        column = 5 + index * 5
        led(c, board, column, colour)
        resistor(c, board.hole(column, "e"), board.hole(column, "f"), "220 Ω" if index == 0 else "")
        c.wire(path(board.hole(column + 1, "e"), ("y", board.rail("bot-", column + 1)[1])), BLACK)
        lane, lane_x = 340 - index * 15, 680 + index * 12
        target = board.hole(column, "ghi"[index])  # one row per wire, so that none hides another
        c.wire(path(uno.pin(pin), ("y", lane), ("x", lane_x), ("y", target[1]), target), wire)


EXAMPLES = {
    "blink": blink,
    "servo-potentiometer": servo_potentiometer,
    "distance-alarm": distance_alarm,
    "thermometer": thermometer,
    "thermometer-lcd16": thermometer_lcd16,
    "reaction-game": reaction_game,
    "light-gauge": light_gauge,
    "traffic-light": traffic_light,
}


def main(names: list[str]) -> int:
    for name in names or list(EXAMPLES):
        folder = ROOT / "examples" / "projects" / name
        project = json.loads((folder / "project.json").read_text(encoding="utf-8"))
        canvas = Canvas(project["name"] + " — Arduino Uno")
        EXAMPLES[name](canvas)
        canvas.save(folder / "wiring.png")
        print((folder / "wiring.png").relative_to(ROOT))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
