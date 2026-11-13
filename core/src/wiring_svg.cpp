// Wiring diagram drawn from a wiring::Plan, in the style of a breadboard simulator (Tinkercad):
// the board as it looks, a breadboard whose + and − rails are fed by the board, the small parts
// plugged into the breadboard (LED and its resistor, button across the middle channel,
// potentiometer, buzzer...), the modules (sensors, screens, servo...) above it, and one coloured
// jumper wire per connection.
//
// Routing: a wire leaves a pin of the board vertically (up for the top header, down for the
// bottom one) to its own horizontal lane, goes along it to the column of its target, then
// straight to it. Lanes are ordered so that the horizontal part of a wire does not cross the
// vertical part of another one near the board; no two wires share a segment.
#include "s2a/wiring.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <utility>
#include <sstream>

namespace s2a::wiring {

namespace {

// --- small SVG helpers -------------------------------------------------------------------

struct Point {
    double x = 0;
    double y = 0;
};

std::string escape(const std::string& text) {
    std::string out;
    for (const char c : text) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default: out += c;
        }
    }
    return out;
}

std::string num(double value) {
    std::ostringstream out;
    out << std::round(value * 10) / 10;
    return out.str();
}

/// UTF-8 characters of `text` (for the width of a label).
std::size_t characters(const std::string& text) {
    return static_cast<std::size_t>(std::count_if(text.begin(), text.end(), [](char c) {
        return (static_cast<unsigned char>(c) & 0xC0) != 0x80;
    }));
}

class Svg {
public:
    /// Attributes for the next element written (the virtual board finds its parts by them): `data-sim="led"`.
    void mark(std::string attributes) { pending_ = " " + std::move(attributes); }
    /// A group of elements (one component of the diagram), closed by close().
    void open(const std::string& attributes) { out_ << "<g " << attributes << ">\n"; }
    void close() { out_ << "</g>\n"; }

    void rect(double x, double y, double w, double h, const std::string& fill, const std::string& stroke = {},
              double radius = 0, double width = 1) {
        out_ << "<rect" << take() << R"( x=")" << num(x) << R"(" y=")" << num(y) << R"(" width=")" << num(w) << R"(" height=")"
             << num(h) << R"(" rx=")" << num(radius) << R"(" fill=")" << fill << '"';
        if (!stroke.empty()) {
            out_ << R"( stroke=")" << stroke << R"(" stroke-width=")" << num(width) << '"';
        }
        out_ << "/>\n";
    }
    void circle(double x, double y, double r, const std::string& fill, const std::string& stroke = {}, double width = 1) {
        out_ << "<circle" << take() << R"( cx=")" << num(x) << R"(" cy=")" << num(y) << R"(" r=")" << num(r) << R"(" fill=")" << fill
             << '"';
        if (!stroke.empty()) {
            out_ << R"( stroke=")" << stroke << R"(" stroke-width=")" << num(width) << '"';
        }
        out_ << "/>\n";
    }
    void line(const std::vector<Point>& points, const std::string& colour, double width) {
        out_ << "<polyline" << take() << R"( fill="none" stroke-linejoin="round" stroke-linecap="round" stroke=")" << colour
             << R"(" stroke-width=")" << num(width) << R"(" points=")";
        for (const Point& point : points) {
            out_ << num(point.x) << ',' << num(point.y) << ' ';
        }
        out_ << "\"/>\n";
    }
    void polygon(const std::vector<Point>& points, const std::string& fill, const std::string& stroke = {}) {
        out_ << "<polygon" << take() << R"( fill=")" << fill << '"';
        if (!stroke.empty()) {
            out_ << R"( stroke=")" << stroke << '"';
        }
        out_ << R"( points=")";
        for (const Point& point : points) {
            out_ << num(point.x) << ',' << num(point.y) << ' ';
        }
        out_ << "\"/>\n";
    }
    void text(Point at, const std::string& content, double size, const std::string& colour,
              const char* anchor = "start", bool bold = false, double rotate = 0) {
        out_ << "<text" << take() << R"( x=")" << num(at.x) << R"(" y=")" << num(at.y) << R"(" font-size=")" << num(size)
             << R"(" fill=")" << colour << R"(" text-anchor=")" << anchor << '"';
        if (bold) {
            out_ << R"( font-weight="bold")";
        }
        if (rotate != 0) {
            out_ << R"( transform="rotate()" << num(rotate) << ' ' << num(at.x) << ' ' << num(at.y) << ")\"";
        }
        out_ << '>' << escape(content) << "</text>\n";
    }
    [[nodiscard]] std::string str() const { return out_.str(); }

private:
    std::string take() { return std::exchange(pending_, std::string{}); }

    std::ostringstream out_;
    std::string pending_;
};

/// Key of a component for the virtual board: its kind and the board pins of its signals ("output|8"),
/// as the editor names its parts (frontend/src/simulation.ts).
std::string partKey(const Component& component) {
    std::string key = component.kind;
    for (const Connection& connection : component.connections) {
        static const std::set<std::string> kPower{"GND", "5V", "3V3", "VCC", "VIN", "SDA", "SCL"};
        if (!connection.role.empty() && kPower.count(connection.role) == 0) {
            key += "|" + connection.boardPin;
        }
    }
    return key;
}

/// The halo of a light (an LED of the diagram, lit by the virtual board): invisible in a picture.
void halo(Svg& svg, double x, double y, double r, const std::string& colour) {
    svg.mark(R"(data-sim="halo" style="color:)" + colour + R"(" opacity="0")");
    svg.circle(x, y, r, "url(#s2a-glow)");
}

// Wire colours, as in most kits: red for +, black for GND, bright colours for signals.
constexpr const char* kRed = "#D32F2F";
constexpr const char* kBlack = "#212121";
const char* const kSignals[] = {"#FBC02D", "#1976D2", "#388E3C", "#F57C00", "#7B1FA2",
                                "#00ACC1", "#6D4C41", "#E91E63", "#689F38", "#F5F5F5"};

std::string darker(const std::string& colour) {
    if (colour.size() != 7 || colour[0] != '#') {
        return "#000000";
    }
    std::ostringstream out;
    out << '#';
    for (int i = 0; i < 3; ++i) {
        const int value = std::stoi(colour.substr(1 + 2 * i, 2), nullptr, 16) * 55 / 100;
        const char* digits = "0123456789ABCDEF";
        out << digits[value / 16] << digits[value % 16];
    }
    return out.str();
}

/// "220 Ω" in "220 Ω resistor" / "résistance 220 Ω".
std::string value(const std::string& via) {
    static const std::regex kValue("([0-9]+[.,]?[0-9]* ?k?Ω)");
    std::smatch match;
    return std::regex_search(via, match, kValue) ? match.str(1) : std::string{};
}

// --- breadboard ------------------------------------------------------------------------

/// Columns 1..n; rows a-e and f-j (joined by column), + and − rails at the top and bottom.
struct Breadboard {
    Point origin;
    int columns = 30;
    static constexpr double kPitch = 20;
    static constexpr double kHeight = 362;

    [[nodiscard]] double width() const { return columns * kPitch + 40; }
    [[nodiscard]] double colX(int column) const { return origin.x + 30 + (column - 1) * kPitch; }
    [[nodiscard]] double rowY(char row) const {
        const int index = row - 'a';
        return origin.y + 78 + index * kPitch + (index >= 5 ? 24 : 0);
    }
    [[nodiscard]] Point hole(int column, char row) const { return {colX(column), rowY(row)}; }
    enum class Rail { TopMinus, TopPlus, BottomPlus, BottomMinus };
    [[nodiscard]] double railY(Rail rail) const {
        switch (rail) {
        case Rail::TopMinus: return origin.y + 18;
        case Rail::TopPlus: return origin.y + 38;
        case Rail::BottomPlus: return origin.y + kHeight - 38;
        case Rail::BottomMinus: return origin.y + kHeight - 18;
        }
        return origin.y;
    }
    [[nodiscard]] Point rail(Rail which, int column) const { return {colX(column), railY(which)}; }

    void draw(Svg& svg) const {
        const double x = origin.x;
        const double y = origin.y;
        svg.rect(x + 4, y + 6, width(), kHeight, "#B0BEC5", {}, 8);
        svg.rect(x, y, width(), kHeight, "#FAFAFA", "#CFD8DC", 8, 2);
        svg.rect(x + 10, y + 176, width() - 20, 14, "#E0E0E0", {}, 3); // middle channel
        for (const Rail rail : {Rail::TopMinus, Rail::TopPlus, Rail::BottomPlus, Rail::BottomMinus}) {
            const bool minus = rail == Rail::TopMinus || rail == Rail::BottomMinus;
            const double offset = (rail == Rail::TopMinus || rail == Rail::BottomPlus) ? -11 : 11;
            const double ry = railY(rail);
            svg.line({{x + 18, ry + offset}, {x + width() - 18, ry + offset}}, minus ? "#1976D2" : kRed, 2);
            svg.text({x + 10, ry + 5}, minus ? "−" : "+", 14, minus ? "#1976D2" : kRed, "middle", true);
            for (int column = 1; column <= columns; ++column) {
                if ((column - 1) % 6 != 5) {
                    svg.rect(colX(column) - 3.5, ry - 3.5, 7, 7, "#9E9E9E", {}, 1);
                }
            }
        }
        for (char row = 'a'; row <= 'j'; ++row) {
            const double ry = rowY(row);
            svg.text({x + 16, ry + 4}, std::string(1, row), 10, "#90A4AE", "middle");
            for (int column = 1; column <= columns; ++column) {
                svg.rect(colX(column) - 3.5, ry - 3.5, 7, 7, "#9E9E9E", {}, 1);
            }
        }
        for (int column = 1; column <= columns; column += column == 1 ? 4 : 5) {
            svg.text({colX(column), y + 68}, std::to_string(column), 9, "#90A4AE", "middle");
        }
    }
};

using Rail = Breadboard::Rail;

// --- parts --------------------------------------------------------------------------------

void leg(Svg& svg, Point a, Point b) { svg.line({a, b}, "#9E9E9E", 2.5); }

void resistor(Svg& svg, Point a, Point b, const std::string& label) {
    leg(svg, a, b);
    const Point m{(a.x + b.x) / 2, (a.y + b.y) / 2};
    svg.rect(m.x - 6, m.y - 15, 12, 30, "#D7CCC8", "#A1887F", 5);
    const bool tenK = label.find('k') != std::string::npos;
    const char* bands[] = {tenK ? "#6D4C41" : kRed, tenK ? "#212121" : kRed, tenK ? "#F57C00" : "#6D4C41"};
    for (int i = 0; i < 3; ++i) {
        svg.rect(m.x - 6, m.y - 9.5 + i * 6, 12, 3, bands[i]);
    }
    if (!label.empty()) {
        svg.text({m.x + 10, m.y + 4}, label, 10, "#455A64", "start", true);
    }
}

/// A module: a board with a row of header pins below; returns the pin positions.
std::vector<Point> module(Svg& svg, Point at, double w, double h, const std::string& colour,
                          const std::vector<std::string>& pins, const std::string& label,
                          const std::string& labelColour = "#FFFFFF") {
    svg.rect(at.x + 3, at.y + 5, w, h, "#B0BEC5", {}, 6);
    svg.rect(at.x, at.y, w, h, colour, darker(colour), 6, 2);
    std::vector<Point> points;
    for (std::size_t i = 0; i < pins.size(); ++i) {
        const Point pin{at.x + 20 + Breadboard::kPitch * static_cast<double>(i), at.y + h + 10};
        svg.rect(pin.x - 3, at.y + h, 6, 10, "#BDBDBD");
        svg.rect(pin.x - 7, pin.y - 6, 14, 12, "#212121", {}, 1);
        const bool turn = characters(pins[i]) > 2;
        svg.text({pin.x + (turn ? 3 : 0), at.y + h - (turn ? 6 : 6)}, pins[i], 9, labelColour, turn ? "start" : "middle",
                 true, turn ? -90 : 0);
        points.push_back(pin);
    }
    if (!label.empty()) {
        svg.text({at.x + w / 2, at.y + 16}, label, 11, labelColour, "middle", true);
    }
    return points;
}

/// Width and height of the drawn module of `kind` with `pins` pins.
std::pair<double, double> moduleSize(const std::string& kind, std::size_t pins) {
    // The bottom 36 px of a module hold the names of its pins.
    const double minimum = 20 * static_cast<double>(pins + 1);
    if (kind == "lcd_i2c" || kind == "lcd16") {
        return {std::max(minimum, 300.0), 140};
    }
    if (kind == "ultrasonic") {
        return {std::max(minimum, 150.0), 110};
    }
    if (kind == "matrix") {
        return {std::max(minimum, 130.0), 170};
    }
    if (kind == "neopixel") {
        return {std::max(minimum, 220.0), 90};
    }
    if (kind == "dc_motor") {
        return {std::max(minimum, 140.0), 130};
    }
    if (kind == "servo") {
        return {std::max(minimum, 110.0), 110};
    }
    if (kind == "oled") {
        return {std::max(minimum, 110.0), 130};
    }
    if (kind == "dht") {
        return {std::max(minimum, 90.0), 130};
    }
    if (kind == "bluetooth_module") {
        return {std::max(minimum, 120.0), 130};
    }
    if (kind == "joystick") {
        return {std::max(minimum, 120.0), 140};
    }
    if (kind == "stepper") {
        return {std::max(minimum, 170.0), 150};
    }
    if (kind == "tm1637") {
        return {std::max(minimum, 160.0), 120};
    }
    if (kind == "pir") {
        return {std::max(minimum, 110.0), 130};
    }
    if (kind == "obstacle_sensor") {
        return {std::max(minimum, 90.0), 140};
    }
    return {std::max(minimum, 100.0), 110};
}

void moduleBody(Svg& svg, const std::string& kind, Point at, double w, double /*h*/, const std::string& name) {
    if (kind == "ultrasonic") {
        svg.rect(at.x + 8, at.y + 26, w - 16, 4, "#0D47A1");
        for (const double cx : {at.x + w / 2 - 34, at.x + w / 2 + 34}) {
            svg.circle(cx, at.y + 40, 24, "#CFD8DC", "#78909C", 3);
            svg.circle(cx, at.y + 40, 14, "#455A64");
        }
        svg.text({at.x + w / 2, at.y + 16}, "HC-SR04", 10, "#FFFFFF", "middle", true);
    } else if (kind == "dht") {
        svg.rect(at.x + w / 2 - 30, at.y + 14, 60, 70, "#42A5F5", "#1E88E5", 4, 2);
        for (int row = 0; row < 5; ++row) {
            for (int col = 0; col < 4; ++col) {
                svg.rect(at.x + w / 2 - 23 + col * 13, at.y + 22 + row * 12, 8, 6, "#1565C0", {}, 1);
            }
        }
        svg.text({at.x + w / 2, at.y + 10}, "DHT", 9, "#FFFFFF", "middle", true);
    } else if (kind == "servo") {
        svg.rect(at.x + 10, at.y + 20, w - 20, 50, "#1E88E5", "#0D47A1", 6, 2);
        svg.circle(at.x + 40, at.y + 45, 12, "#FFFFFF");
        svg.mark(R"(data-sim="arm" data-cx=")" + num(at.x + 40) + R"(" data-cy=")" + num(at.y + 45) + '"'); // 90° (up)
        svg.line({{at.x + 40, at.y + 45}, {at.x + 40, at.y + 6}}, "#FFFFFF", 10);
        svg.text({at.x + w - 18, at.y + 50}, "SG90", 10, "#FFFFFF", "end", true);
    } else if (kind == "dc_motor") {
        svg.rect(at.x + 12, at.y + 12, 46, 46, "#212121", {}, 3);
        for (int i = 0; i < 5; ++i) {
            svg.line({{at.x + 17 + i * 9, at.y + 16}, {at.x + 17 + i * 9, at.y + 54}}, "#616161", 3);
        }
        svg.rect(at.x + w - 52, at.y + 12, 40, 24, "#1E88E5", {}, 2);
        svg.text({at.x + w - 32, at.y + 28}, "OUT", 9, "#FFFFFF", "middle", true);
        svg.text({at.x + w / 2, at.y + 78}, "L298N", 12, "#FFFFFF", "middle", true);
    } else if (kind == "neopixel") {
        const char* colours[] = {"#E53935", "#FB8C00", "#FDD835", "#43A047", "#1E88E5", "#8E24AA", "#616161", "#616161"};
        for (int i = 0; i < 8; ++i) {
            svg.rect(at.x + 14 + i * ((w - 28) / 8), at.y + 14, 18, 18, "#FAFAFA", "#BDBDBD", 2);
            svg.mark(R"(data-sim="pixel" data-index=")" + std::to_string(i) + '"');
            svg.circle(at.x + 23 + i * ((w - 28) / 8), at.y + 23, 6, colours[i]);
        }
        svg.text({at.x + w / 2, at.y + 48}, "NeoPixel", 10, "#FFFFFF", "middle", true);
    } else if (kind == "matrix") {
        svg.rect(at.x + w / 2 - 50, at.y + 10, 100, 100, "#1B1B1B", {}, 3);
        for (int row = 0; row < 8; ++row) {
            for (int col = 0; col < 8; ++col) {
                const bool lit = (row + col) % 3 == 0;
                svg.mark(R"(data-sim="dot" data-row=")" + std::to_string(row) + R"(" data-col=")" + std::to_string(col) + '"');
                svg.circle(at.x + w / 2 - 43 + col * 12.3, at.y + 17 + row * 12.3, 4.5, lit ? "#F44336" : "#4E1C1A");
            }
        }
        svg.text({at.x + w / 2, at.y + 126}, "MAX7219", 10, "#FFFFFF", "middle", true);
    } else if (kind == "lcd_i2c" || kind == "lcd16") {
        svg.rect(at.x + 14, at.y + 14, w - 28, 66, "#1B5E20", {}, 4);
        svg.rect(at.x + 22, at.y + 22, w - 44, 50, "#9CCC65", {}, 2);
        svg.mark(R"(data-sim="line" data-row="1" xml:space="preserve")");
        svg.text({at.x + 30, at.y + 42}, "Scratch2Arduino", 15, "#1B5E20", "start", true);
        svg.mark(R"(data-sim="line" data-row="2" xml:space="preserve")");
        svg.text({at.x + 30, at.y + 64}, "21", 15, "#1B5E20", "start", true);
        svg.text({at.x + w - 14, at.y + 96}, kind == "lcd16" ? "LCD 16x2" : "LCD 16x2 I2C", 10, "#FFFFFF", "end",
                 true);
    } else if (kind == "bluetooth_module") {
        // The small HC-05 board on its blue carrier: antenna track at the top, chip below.
        svg.rect(at.x + 14, at.y + 10, w - 28, 72, "#2E7D32", "#1B5E20", 3, 2);
        std::vector<Point> antenna;
        for (int i = 0; i <= 8; ++i) {
            const double x = at.x + 24 + i * ((w - 48) / 8);
            antenna.push_back({x, at.y + (i % 2 == 0 ? 18 : 30)});
        }
        svg.line(antenna, "#FDD835", 2);
        svg.rect(at.x + w / 2 - 18, at.y + 40, 36, 30, "#212121", {}, 2);
        svg.text({at.x + w / 2, at.y + 98}, "HC-05", 11, "#FFFFFF", "middle", true);
    } else if (kind == "oled") {
        svg.rect(at.x + 12, at.y + 14, w - 24, 64, "#000000", {}, 3);
        svg.open(R"(data-sim="sample")"); // what a picture shows; the virtual board writes the 8 lines
        svg.line({{at.x + 22, at.y + 34}, {at.x + w - 30, at.y + 34}}, "#4FC3F7", 3);
        svg.line({{at.x + 22, at.y + 50}, {at.x + w - 50, at.y + 50}}, "#4FC3F7", 3);
        svg.close();
        for (int row = 1; row <= 8; ++row) {
            svg.mark(R"(data-sim="line" data-row=")" + std::to_string(row) + R"(" xml:space="preserve")");
            svg.text({at.x + 16, at.y + 13 + row * 8}, "", 7, "#4FC3F7", "start", false);
        }
        svg.text({at.x + w / 2, at.y + 94}, "OLED", 10, "#FFFFFF", "middle", true);
    } else if (kind == "tm1637") {
        // Four red digits showing 1234, and the colon.
        svg.rect(at.x + 10, at.y + 10, w - 20, 48, "#212121", "#000000", 3, 1);
        svg.mark(R"(data-sim="digits" xml:space="preserve")");
        svg.text({at.x + w / 2, at.y + 46}, "12:34", 30, "#FF1744", "middle", true);
        svg.text({at.x + w / 2, at.y + 74}, "TM1637", 10, "#FFFFFF", "middle", true);
    } else if (kind == "stepper") {
        // ULN2003 board (its chip and four LEDs) and the 28BYJ-48 motor plugged into its socket.
        svg.rect(at.x + 12, at.y + 14, 52, 34, "#212121", {}, 2);
        svg.text({at.x + 38, at.y + 35}, "ULN2003", 8, "#FFFFFF", "middle", true);
        for (int led = 0; led < 4; ++led) {
            svg.circle(at.x + 18 + led * 13, at.y + 62, 4, "#E53935", "#B71C1C", 1);
        }
        svg.rect(at.x + 14, at.y + 76, 48, 12, "#ECEFF1", "#B0BEC5", 2, 1); // the white socket
        const Point motor{at.x + w - 46, at.y + 52};
        svg.line({{at.x + 62, at.y + 82}, {motor.x - 26, motor.y + 20}}, "#FB8C00", 3); // its five wires
        svg.circle(motor.x, motor.y, 34, "#CFD8DC", "#78909C", 2);
        svg.circle(motor.x, motor.y - 12, 9, "#ECEFF1", "#90A4AE", 2);
        svg.mark(R"(data-sim="shaft" data-cx=")" + num(motor.x) + R"(" data-cy=")" + num(motor.y - 12) + '"');
        svg.rect(motor.x - 2.5, motor.y - 20, 5, 16, "#FFD54F", "#F9A825", 1); // its shaft
        svg.text({motor.x, motor.y + 22}, "28BYJ-48", 8, "#37474F", "middle", true);
    } else if (kind == "joystick") {
        // KY-023: the square base of the stick and its round grey thumb cap.
        const Point mid{at.x + w / 2, at.y + 52};
        svg.rect(mid.x - 34, mid.y - 34, 68, 68, "#424242", "#212121", 6, 2);
        svg.open(R"(data-sim="press")"); // its push button (SW), pressed with the mouse on the virtual board
        svg.mark(R"(data-sim="cap")");
        svg.circle(mid.x, mid.y, 27, "#9E9E9E", "#616161", 2);
        svg.circle(mid.x, mid.y, 18, "#BDBDBD", "#757575", 1);
        for (int ridge = -2; ridge <= 2; ++ridge) {
            svg.line({{mid.x - 12, mid.y + ridge * 5}, {mid.x + 12, mid.y + ridge * 5}}, "#9E9E9E", 1.5);
        }
        svg.close();
    } else if (kind == "pir") {
        // HC-SR501: the white Fresnel dome on its square frame.
        const Point mid{at.x + w / 2, at.y + 48};
        svg.open(R"(data-sim="press")"); // someone moves in front: held with the mouse on the virtual board
        svg.rect(mid.x - 38, mid.y - 38, 76, 76, "#ECEFF1", "#B0BEC5", 4, 2);
        svg.mark(R"(data-sim="cap")");
        svg.circle(mid.x, mid.y, 32, "#FAFAFA", "#CFD8DC", 2);
        for (const double r : {24.0, 16.0, 8.0}) {
            svg.circle(mid.x, mid.y, r, "none", "#E0E0E0", 1.5);
        }
        svg.close();
        svg.text({at.x + w / 2, at.y + 100}, "PIR", 10, "#FFFFFF", "middle", true);
    } else if (kind == "obstacle_sensor") {
        // FC-51: the infrared emitter (clear) and receiver (dark) at the front, its blue screw.
        svg.open(R"(data-sim="press")"); // a hand in front: held with the mouse on the virtual board
        svg.mark(R"(data-sim="cap")");
        svg.circle(at.x + w / 2 - 14, at.y + 22, 10, "#E3F2FD", "#90CAF9", 2);
        svg.circle(at.x + w / 2 + 14, at.y + 22, 10, "#263238", "#000000", 2);
        svg.close();
        svg.rect(at.x + w / 2 - 12, at.y + 46, 24, 24, "#1E88E5", "#0D47A1", 2, 2);
        svg.circle(at.x + w / 2, at.y + 58, 7, "#FFFFFF", "#90A4AE", 1);
        svg.line({{at.x + w / 2 - 5, at.y + 58}, {at.x + w / 2 + 5, at.y + 58}}, "#455A64", 2);
        svg.mark(R"(data-sim="indicator")"); // lights up when something is in front
        svg.circle(at.x + w / 2 + 26, at.y + 82, 4, "#5D1F1F");
        svg.text({at.x + w / 2, at.y + 98}, "FC-51", 10, "#FFFFFF", "middle", true);
    } else {
        svg.circle(at.x + w / 2, at.y + 34, 12, "#FFCC80", "#EF6C00", 2);
        svg.text({at.x + w / 2, at.y + 66}, name.substr(0, std::min<std::size_t>(name.size(), 14)), 9, "#FFFFFF", "middle",
                 true);
    }
}

std::string moduleColour(const std::string& kind) {
    if (kind == "stepper") {
        return "#2E7D32";
    }
    if (kind == "tm1637") {
        return "#1565C0";
    }
    if (kind == "joystick" || kind == "obstacle_sensor") {
        return kind == "joystick" ? "#1565C0" : "#283593";
    }
    if (kind == "pir") {
        return "#2E7D32";
    }
    if (kind == "ultrasonic" || kind == "dht" || kind == "servo") {
        return kind == "servo" ? "#ECEFF1" : "#1565C0";
    }
    if (kind == "dc_motor") {
        return "#C62828";
    }
    if (kind == "neopixel" || kind == "matrix" || kind == "oled") {
        return "#263238";
    }
    if (kind == "lcd_i2c" || kind == "lcd16") {
        return "#2E7D32";
    }
    if (kind == "bluetooth_module") {
        return "#1565C0";
    }
    return "#6A1B9A";
}

// --- the diagram ------------------------------------------------------------------------

/// Parts plugged into the breadboard, with the number of columns they take.
int breadboardColumns(const std::string& kind) {
    if (kind == "output" || kind == "light_sensor") {
        return 2;
    }
    if (kind == "button" || kind == "potentiometer" || kind == "buzzer") {
        return 3;
    }
    if (kind == "rgb_led") {
        return 4;
    }
    return 0;
}

/// For a part on the breadboard (plugged into the lower half, seen from above): the hole of each
/// connection (column offset, row). A signal arrives from above in a free hole of the column of
/// its leg (through the resistor across the middle channel for an LED); power legs go down to
/// the bottom rails with a short jumper from row j.
std::vector<std::pair<int, char>> breadboardHoles(const std::string& kind) {
    if (kind == "output") {
        return {{0, 'b'}, {1, 'j'}};
    }
    if (kind == "button") {
        return {{0, 'b'}, {2, 'j'}};
    }
    if (kind == "potentiometer") {
        return {{0, 'j'}, {1, 'f'}, {2, 'j'}};
    }
    if (kind == "buzzer") {
        return {{0, 'f'}, {2, 'j'}};
    }
    if (kind == "rgb_led") {
        return {{0, 'b'}, {2, 'b'}, {3, 'b'}, {1, 'j'}};
    }
    if (kind == "light_sensor") {
        return {{0, 'j'}, {1, 'f'}, {1, 'j'}};
    }
    return {};
}

/// For a wire arriving from below (bottom header of the board): a free hole of the lower half
/// joined to the leg, when there is one; otherwise the wire goes round and arrives from above.
std::optional<std::pair<int, char>> holeFromBelow(const std::string& kind, std::size_t connection) {
    if (kind == "button" && connection == 0) {
        return std::pair{0, 'i'};
    }
    if (kind == "potentiometer" && connection == 1) {
        return std::pair{1, 'j'};
    }
    if (kind == "buzzer" && connection == 0) {
        return std::pair{0, 'j'};
    }
    if (kind == "light_sensor" && connection == 1) {
        return std::pair{1, 'i'};
    }
    return std::nullopt;
}

class Diagram {
public:
    Diagram(const Plan& plan, const Board& board, std::string title)
        : plan_(plan), board_(board), title_(std::move(title)) {}

    std::string render() {
        plan();
        Svg svg;
        breadboard_.draw(svg);
        drawBoard(svg);
        drawParts(svg);
        drawJumpers(svg);
        drawWires(svg);
        std::ostringstream out;
        out << R"(<svg xmlns="http://www.w3.org/2000/svg" width=")" << num(width_) << R"(" height=")" << num(height_)
            << R"(" viewBox="0 0 )" << num(width_) << ' ' << num(height_)
            << R"(" font-family="DejaVu Sans, Segoe UI, Helvetica, Arial, sans-serif">)" << '\n'
            << R"(<rect width="100%" height="100%" fill="#ECEFF1"/>)" << '\n'
            // The glow of the lights when the virtual board runs (their colour comes from `color`).
            << R"(<defs><radialGradient id="s2a-glow"><stop offset="0%" stop-color="currentColor" stop-opacity="0.95"/>)"
            << R"(<stop offset="100%" stop-color="currentColor" stop-opacity="0"/></radialGradient></defs>)" << '\n';
        Svg head;
        head.text({30, 40}, title_, 24, "#263238", "start", true);
        out << head.str() << svg.str() << "</svg>\n";
        return out.str();
    }

private:
    /// Where a connection ends on the diagram.
    struct End {
        Point at;
        bool toRail = false; ///< Jumper to a rail of the breadboard (drawn with the part).
        Rail rail = Rail::TopMinus;
        std::string boardPin; ///< Otherwise: a wire from this pin of the board.
        std::string via;      ///< Value of the part in between, for a module (drawn on the wire).
    };
    struct Placed {
        bool onBreadboard = false;
        int column = 0;       ///< First column on the breadboard.
        Point at;             ///< Module: top-left corner.
        double w = 0;
        double h = 0;
        std::vector<End> ends; ///< One per connection.
    };
    struct BoardPin {
        Point at; ///< On the board, from its top-left corner.
        BoardHeader::Side side = BoardHeader::Side::Top;
    };
    struct Label {
        Point at;
        BoardHeader::Side side;
        std::string text;
    };
    struct Wire {
        std::string pin;
        Point target;
        std::string colour;
        std::string via;
        double lane = 0;
        double track = 0; ///< x of a vertical track, for detours (0: none).
        double lane2 = 0; ///< Second lane (wire from the bottom header to a module above).
        bool toModule = false;
    };

    [[nodiscard]] std::string plusLabel() const { return board_.voltage >= 4.5 ? "5V" : "3V3"; }
    [[nodiscard]] double boardWidth() const { return board_.drawing.width > 0 ? board_.drawing.width : 300; }
    [[nodiscard]] double boardHeight() const { return board_.drawing.height > 0 ? board_.drawing.height : 200; }

    void readBoardPins() {
        for (const BoardHeader& header : board_.drawing.headers) {
            double offset = 0;
            for (const auto& entry : header.pins) {
                if (!entry) {
                    offset += header.pitch * 0.6;
                    continue;
                }
                const Point at = header.side == BoardHeader::Side::Right ? Point{header.x, header.y + offset}
                                                                         : Point{header.x + offset, header.y};
                labels_.push_back({at, header.side, entry->label});
                if (entry->pin != "-" && pins_.find(entry->pin) == pins_.end()) {
                    pins_[entry->pin] = {at, header.side};
                }
                offset += header.pitch;
            }
        }
    }

    BoardPin& boardPin(const std::string& label) {
        if (const auto found = pins_.find(label); found != pins_.end()) {
            return found->second;
        }
        const Point at{boardWidth() - 14, 24 + 17 * static_cast<double>(extra_++)};
        labels_.push_back({at, BoardHeader::Side::Right, label});
        return pins_[label] = {at, BoardHeader::Side::Right};
    }

    [[nodiscard]] Point onBoard(const std::string& label) const {
        const BoardPin& pin = pins_.at(label);
        return {boardOrigin_.x + pin.at.x, boardOrigin_.y + pin.at.y};
    }

    /// Places the board, the breadboard and the parts, then gives each wire its lane.
    void plan() {
        readBoardPins();
        // Columns of the parts on the breadboard, and the modules above it.
        int column = 3;
        std::size_t modules = 0;
        double modulesWidth = 0;
        double modulesHeight = 0;
        for (const Component& component : plan_.components) {
            Placed placed;
            const int columns = breadboardColumns(component.kind);
            if (columns > 0) {
                placed.onBreadboard = true;
                placed.column = column;
                column += columns + 2;
            } else {
                const auto [w, h] = moduleSize(component.kind, component.connections.size());
                placed.w = w;
                placed.h = h;
                modulesWidth += w + 30;
                modulesHeight = std::max(modulesHeight, h + 20);
                ++modules;
            }
            placed_.push_back(std::move(placed));
        }
        breadboard_.columns = std::max({30, column + 1, static_cast<int>(std::ceil(modulesWidth / Breadboard::kPitch)) + 3});

        // Vertical layout: title, modules, lanes, then the board and the breadboard side by side.
        std::vector<std::size_t> topWires;
        std::vector<std::size_t> bottomWires;
        const double modulesTop = 70;
        const double modulesBottom = modules > 0 ? modulesTop + modulesHeight + 10 : modulesTop - 20;

        // Ends of every connection (the positions need the final origin: computed relative first).
        collectWires();
        for (std::size_t i = 0; i < wires_.size(); ++i) {
            const BoardPin& pin = pins_.at(wires_[i].pin);
            (pin.side == BoardHeader::Side::Bottom ? bottomWires : topWires).push_back(i);
        }
        // Wires from the bottom header to a module, or to a part reached from above, go round the
        // breadboard: they need a top lane too.
        std::size_t detours = 0;
        for (const std::size_t i : bottomWires) {
            const auto [c, k] = wireOf_[i];
            fromBelow_[i] = c != SIZE_MAX && placed_[c].onBreadboard &&
                            holeFromBelow(plan_.components[c].kind, k).has_value();
            if (c != SIZE_MAX && !fromBelow_[i]) {
                ++detours;
            }
        }
        const double lanesTop = modulesBottom + 30;
        const double lanesHeight = 14 * static_cast<double>(topWires.size() + detours);
        const double top = lanesTop + lanesHeight + 30;
        boardOrigin_ = {40, top + 40};
        double gap = 70;
        for (const std::size_t i : topWires) {
            if (pins_.at(wires_[i].pin).side == BoardHeader::Side::Right) {
                gap += 13;
            }
        }
        breadboard_.origin = {boardOrigin_.x + boardWidth() + gap, top};

        // Modules above the breadboard, their pins between two columns of holes.
        double x = breadboard_.colX(2) + Breadboard::kPitch / 2 - 20;
        for (Placed& placed : placed_) {
            if (!placed.onBreadboard) {
                placed.at = {x, modulesTop + (modulesHeight - 20 - placed.h)};
                x += placed.w + 30;
            }
        }
        resolveEnds();

        // Lanes: pins furthest right get the lanes closest to the board.
        const auto byX = [this](std::size_t a, std::size_t b) {
            return onBoard(wires_[a].pin).x > onBoard(wires_[b].pin).x;
        };
        std::stable_sort(topWires.begin(), topWires.end(), byX);
        std::stable_sort(bottomWires.begin(), bottomWires.end(), byX);
        double lane = top - 20;
        double track = breadboard_.origin.x - 20;
        for (const std::size_t i : topWires) {
            wires_[i].lane = lane;
            lane -= 14;
            if (pins_.at(wires_[i].pin).side == BoardHeader::Side::Right) {
                wires_[i].track = track; // from the right side of the board up to the lane
                track -= 13;
            }
        }
        const double low = std::max(boardOrigin_.y + boardHeight(), breadboard_.origin.y + Breadboard::kHeight);
        double bottomLane = low + 24;
        double detourTrack = breadboard_.origin.x + breadboard_.width() + 30;
        for (const std::size_t i : bottomWires) {
            wires_[i].lane = bottomLane;
            bottomLane += 14;
            if (wireOf_[i].first != SIZE_MAX && !fromBelow_[i]) {
                wires_[i].track = detourTrack;
                detourTrack += 13;
                wires_[i].lane2 = lane;
                lane -= 14;
            }
        }
        width_ = std::max({breadboard_.origin.x + breadboard_.width(), x, detourTrack}) + 40;
        height_ = bottomLane + 30;
    }

    void collectWires() {
        // Ends are filled by resolveEnds(); here: which connection needs a wire from the board.
        for (std::size_t c = 0; c < plan_.components.size(); ++c) {
            const Component& component = plan_.components[c];
            for (const Connection& connection : component.connections) {
                End end;
                if (connection.boardPin == "GND" || connection.boardPin == plusLabel()) {
                    end.toRail = true;
                } else {
                    end.boardPin = connection.boardPin;
                    (void)boardPin(connection.boardPin);
                }
                end.via = value(connection.via);
                placed_[c].ends.push_back(std::move(end));
            }
        }
        bool plus = false;
        bool ground = false;
        for (std::size_t c = 0; c < placed_.size(); ++c) {
            for (std::size_t k = 0; k < placed_[c].ends.size(); ++k) {
                const End& end = placed_[c].ends[k];
                if (end.toRail) {
                    (plan_.components[c].connections[k].boardPin == "GND" ? ground : plus) = true;
                    continue;
                }
                wireOf_.push_back({c, k});
                wires_.push_back({end.boardPin, {}, kSignals[wires_.size() % std::size(kSignals)], end.via});
                wires_.back().toModule = !placed_[c].onBreadboard;
                targetsModule_.push_back(!placed_[c].onBreadboard);
                fromBelow_.push_back(false);
                if (end.boardPin == "5V" || end.boardPin == "VIN") {
                    wires_.back().colour = "#FF7043";
                }
            }
        }
        if (plus || ground) { // the board feeds the rails of the breadboard
            for (const auto& [pin, colour] : {std::pair{plusLabel(), kRed}, std::pair{std::string("GND"), kBlack}}) {
                (void)boardPin(pin);
                feeds_.push_back(wires_.size());
                wireOf_.push_back({SIZE_MAX, 0});
                wires_.push_back({pin, {}, colour, {}});
                targetsModule_.push_back(false);
                fromBelow_.push_back(false);
            }
        }
    }

    void resolveEnds() {
        for (std::size_t c = 0; c < placed_.size(); ++c) {
            Placed& placed = placed_[c];
            const Component& component = plan_.components[c];
            if (placed.onBreadboard) {
                const auto holes = breadboardHoles(component.kind);
                for (std::size_t k = 0; k < placed.ends.size() && k < holes.size(); ++k) {
                    End& end = placed.ends[k];
                    end.at = breadboard_.hole(placed.column + holes[k].first, holes[k].second);
                    const bool ground = component.connections[k].boardPin == "GND";
                    end.rail = ground ? Rail::BottomMinus : Rail::BottomPlus;
                }
            } else {
                for (std::size_t k = 0; k < placed.ends.size(); ++k) {
                    End& end = placed.ends[k];
                    end.at = {placed.at.x + 20 + Breadboard::kPitch * static_cast<double>(k), placed.at.y + placed.h + 10};
                    end.rail = component.connections[k].boardPin == "GND" ? Rail::TopMinus : Rail::TopPlus;
                }
            }
        }
        for (std::size_t i = 0; i < wires_.size(); ++i) {
            const auto [c, k] = wireOf_[i];
            if (c == SIZE_MAX) { // feed of a rail, at the start of the bottom (or top) rails
                const bool bottom = pins_.at(wires_[i].pin).side == BoardHeader::Side::Bottom;
                const bool ground = wires_[i].pin == "GND";
                const Rail rail = bottom ? (ground ? Rail::BottomMinus : Rail::BottomPlus)
                                         : (ground ? Rail::TopMinus : Rail::TopPlus);
                wires_[i].target = breadboard_.rail(rail, ground ? 2 : 1);
            } else if (fromBelow_[i]) {
                const auto hole = holeFromBelow(plan_.components[c].kind, k);
                wires_[i].target = breadboard_.hole(placed_[c].column + hole->first, hole->second);
            } else {
                wires_[i].target = placed_[c].ends[k].at;
            }
        }
    }

    // --- drawing --------------------------------------------------------------------------

    void drawBoard(Svg& svg) {
        const Point o = boardOrigin_;
        const double w = boardWidth();
        const double h = boardHeight();
        const std::string colour = board_.drawing.colour.empty() ? "#00878F" : board_.drawing.colour;
        const bool arduino = board_.architecture == "avr";
        svg.rect(o.x + 4, o.y + 6, w, h, "#B0BEC5", {}, 14);
        svg.rect(o.x, o.y, w, h, colour, darker(colour), 14, 2);
        if (arduino) {
            // USB plug and power jack on the left edge, the microcontroller in the middle.
            svg.rect(o.x - 22, o.y + h * 0.15, 90, h * 0.18, "#B0BEC5", "#78909C", 4, 2);
            svg.rect(o.x - 14, o.y + h * 0.15 + 8, 64, h * 0.18 - 16, "#CFD8DC", {}, 3);
            if (h > 250) {
                svg.rect(o.x - 18, o.y + h * 0.68, 76, 56, "#212121", {}, 6);
                svg.rect(o.x + w * 0.48, o.y + h * 0.58, w * 0.4, 50, "#263238", {}, 3);
                for (int i = 0; i < 14; ++i) {
                    svg.rect(o.x + w * 0.48 + 6 + i * (w * 0.4 - 12) / 14, o.y + h * 0.58 - 8, 6, 8, "#B0BEC5");
                    svg.rect(o.x + w * 0.48 + 6 + i * (w * 0.4 - 12) / 14, o.y + h * 0.58 + 50, 6, 8, "#B0BEC5");
                }
            }
            for (const Point hole : {Point{22, 22}, Point{w - 22, 22}, Point{22, h - 22}, Point{w - 70, h - 22}}) {
                if (h > 250) {
                    svg.circle(o.x + hole.x, o.y + hole.y, 9, "#ECEFF1", "#B0BEC5", 2);
                }
            }
            svg.text({o.x + w * 0.42, o.y + h * 0.42}, board_.drawing.title, std::min(46.0, h / 4), "#FFFFFF", "middle",
                     true);
            // The "L" LED of the board (its built-in LED), lit by the virtual board.
            if (const Pin* led = board_.findPin(board_.builtinLed); led != nullptr && h > 250) {
                const Point at{o.x + w * 0.38, o.y + h * 0.2};
                svg.open(R"(data-part="board-led|)" + led->label + '"');
                halo(svg, at.x, at.y, 22, "#FFEB3B");
                svg.mark(R"(data-sim="led")");
                svg.rect(at.x - 8, at.y - 4, 16, 8, "#FFF9C4", "#BDB76B", 1, 1);
                svg.text({at.x + 14, at.y + 4}, "L", 10, "#FFFFFF", "start", true);
                svg.close();
            }
            svg.text({o.x + w * 0.42, o.y + h * 0.42 + std::min(46.0, h / 4) * 0.6}, "ARDUINO", std::min(16.0, h / 9),
                     "#B2EBF2", "middle", true);
        } else {
            // USB plug on the right, the metal shield of the radio module on the left.
            svg.rect(o.x + w - 30, o.y + h / 2 - 25, 56, 50, "#B0BEC5", "#78909C", 4, 2);
            svg.rect(o.x + 12, o.y + h / 2 - 50, 52, 100, "#455A64", {}, 2);
            for (int i = 0; i < 5; ++i) {
                svg.line({{o.x + 20 + i * 9, o.y + h / 2 - 42}, {o.x + 20 + i * 9, o.y + h / 2 + 42}}, "#D4AF37", 2);
            }
            svg.rect(o.x + 70, o.y + h / 2 - 50, 150, 100, "#B0BEC5", "#90A4AE", 3, 2);
            svg.text({o.x + 145, o.y + h / 2 + 8}, board_.mcu, 22, "#455A64", "middle", true);
            svg.text({o.x + w * 0.65, o.y + h / 2 + 8}, board_.drawing.title, 16, "#FFFFFF", "middle", true);
        }
        std::map<std::string, bool> used;
        for (const Wire& wire : wires_) {
            used[wire.pin] = true;
        }
        for (const Label& label : labels_) {
            const Point at{o.x + label.at.x, o.y + label.at.y};
            svg.rect(at.x - 7, at.y - 7, 14, 14, "#1E1E1E");
            svg.rect(at.x - 3, at.y - 3, 6, 6, "#9E9E9E");
            if (label.side == BoardHeader::Side::Right) {
                svg.text({at.x - 11, at.y + 3}, label.text, 9, "#FFFFFF", "end");
            } else {
                const double dy = label.side == BoardHeader::Side::Top ? 13 : -13;
                svg.text({at.x + 3, at.y + dy}, label.text, 9, "#FFFFFF", label.side == BoardHeader::Side::Top ? "end" : "start",
                         false, -90);
            }
        }
        for (const auto& [pin, yes] : used) {
            const Point at = onBoard(pin);
            svg.circle(at.x, at.y, 10, "none", "#FFEB3B", 3);
        }
    }

    void drawParts(Svg& svg) {
        std::size_t leds = 0;
        for (std::size_t c = 0; c < placed_.size(); ++c) {
            const Placed& placed = placed_[c];
            const Component& component = plan_.components[c];
            const std::string number = std::to_string(c + 1);
            svg.open(R"(data-part=")" + partKey(component) + '"');
            if (!placed.onBreadboard) {
                std::vector<std::string> names;
                for (const Connection& connection : component.connections) {
                    names.push_back(connection.shortPin);
                }
                const std::string colour = moduleColour(component.kind);
                module(svg, placed.at, placed.w, placed.h, colour, names, {},
                       component.kind == "servo" ? "#37474F" : "#FFFFFF");
                moduleBody(svg, component.kind, placed.at, placed.w, placed.h, component.name);
                badge(svg, {placed.at.x - 6, placed.at.y - 6}, number);
                svg.close();
                continue;
            }
            const int col = placed.column;
            const Breadboard& b = breadboard_;
            const std::string kind = component.kind;
            auto resistorValue = [&](std::size_t k) {
                return k < placed.ends.size() ? placed.ends[k].via : std::string{};
            };
            Point badgeAt;
            if (kind == "output" || kind == "rgb_led") {
                // Legs in row h; the resistor joins the + leg's column to the upper half.
                const bool rgb = kind == "rgb_led";
                const int legs = rgb ? 4 : 2;
                const Point first = b.hole(col, 'h');
                const Point last = b.hole(col + legs - 1, 'h');
                const Point centre{(first.x + last.x) / 2, first.y};
                if (rgb) {
                    halo(svg, centre.x, centre.y, 60, "#FFFFFF");
                    svg.mark(R"(data-sim="led")");
                    svg.circle(centre.x, centre.y, 26, "#F5F5F5", "#9E9E9E", 2);
                    svg.circle(centre.x - 8, centre.y - 6, 6, "#EF5350");
                    svg.circle(centre.x + 8, centre.y - 6, 6, "#66BB6A");
                    svg.circle(centre.x, centre.y + 8, 6, "#42A5F5");
                    const char* names[] = {"R", "−", "G", "B"};
                    for (int i = 0; i < 4; ++i) {
                        svg.text({b.colX(col + i), b.rowY('j') + 16}, names[i], 10, "#455A64", "middle", true);
                    }
                    for (const int leg : {0, 2, 3}) {
                        resistor(svg, b.hole(col + leg, 'e'), b.hole(col + leg, 'f'), leg == 0 ? resistorValue(0) : std::string{});
                    }
                } else {
                    // Successive LEDs in different colours, as in a kit: red, yellow, green, blue.
                    static const char* const kLeds[] = {"#E53935", "#FDD835", "#43A047", "#1E88E5"};
                    const std::string colour = kLeds[leds++ % std::size(kLeds)];
                    halo(svg, centre.x, centre.y, 42, colour);
                    svg.mark(R"(data-sim="led")");
                    svg.circle(centre.x, centre.y, 15, colour, darker(colour), 2);
                    svg.circle(centre.x - 5, centre.y - 5, 4, "#FFFFFF");
                    svg.line({{centre.x + 11, centre.y - 10}, {centre.x + 11, centre.y + 10}}, darker(colour), 2); // flat side: −
                    svg.text({first.x - 4, first.y + 30}, "+", 12, "#455A64", "middle", true);
                    resistor(svg, b.hole(col, 'e'), b.hole(col, 'f'), resistorValue(0));
                }
                badgeAt = {centre.x - (rgb ? 34 : 24), centre.y - (rgb ? 26 : 20)};
            } else if (kind == "button") {
                // A 4-leg push button (6 mm tactile switch) across the middle channel, as in the
                // kits: its legs in rows e and f of two columns. The legs facing each other across
                // the channel are always joined; pressing joins the two columns.
                const Point topLeft = b.hole(col, 'e');
                const Point bottomRight = b.hole(col + 2, 'f');
                for (const Point leg : {topLeft, b.hole(col + 2, 'e'), b.hole(col, 'f'), bottomRight}) {
                    const double toward = leg.y < (topLeft.y + bottomRight.y) / 2 ? 1 : -1; // legs bend under the body
                    svg.line({{leg.x, leg.y}, {leg.x, leg.y + toward * 9}}, "#B0BEC5", 4);
                    svg.circle(leg.x, leg.y, 3.5, "#CFD8DC", "#78909C", 1);
                }
                const double margin = 3;
                const double left = topLeft.x + margin;
                const double top = topLeft.y + 6;
                const double width = bottomRight.x - topLeft.x - 2 * margin;
                const double height = bottomRight.y - topLeft.y - 12;
                svg.open(R"(data-sim="press")"); // pressed with the mouse on the virtual board
                svg.rect(left, top, width, height, "#37474F", "#263238", 3, 2); // body
                const Point mid{(topLeft.x + bottomRight.x) / 2, (topLeft.y + bottomRight.y) / 2};
                svg.mark(R"(data-sim="cap")");
                svg.circle(mid.x, mid.y, std::min(width, height) * 0.36, "#212121", "#000000", 1.5); // cap
                svg.circle(mid.x - 3, mid.y - 3, std::min(width, height) * 0.12, "#424242");
                svg.close();
                badgeAt = {topLeft.x - 22, topLeft.y - 18};
            } else if (kind == "potentiometer") {
                const Point mid = b.hole(col + 1, 'h');
                svg.rect(mid.x - 30, mid.y - 26, 60, 52, "#1565C0", "#0D47A1", 4, 2);
                svg.circle(mid.x, mid.y, 17, "#ECEFF1", "#90A4AE", 2);
                svg.mark(R"(data-sim="knob" data-cx=")" + num(mid.x) + R"(" data-cy=")" + num(mid.y) + '"');
                svg.line({{mid.x, mid.y}, {mid.x, mid.y - 15}}, "#455A64", 3); // in the middle position
                svg.text({mid.x + 34, mid.y + 4}, "10 kΩ", 10, "#455A64", "start", true);
                badgeAt = {mid.x - 40, mid.y - 30};
            } else if (kind == "buzzer") {
                const Point mid = b.hole(col + 1, 'h');
                svg.mark(R"(data-sim="sound" opacity="0")"); // rings while it sounds
                svg.circle(mid.x, mid.y, 34, "none", "#FB8C00", 3);
                svg.circle(mid.x, mid.y, 26, "#212121", "#000000", 2);
                svg.circle(mid.x, mid.y, 6, "#424242");
                svg.text({mid.x - 15, mid.y - 7}, "+", 13, "#FFFFFF", "middle", true);
                badgeAt = {mid.x - 36, mid.y - 26};
            } else if (kind == "light_sensor") {
                const Point a = b.hole(col, 'h');
                const Point d = b.hole(col + 1, 'h');
                const Point mid{(a.x + d.x) / 2, a.y};
                svg.circle(mid.x, mid.y, 14, "#FFCC80", "#EF6C00", 2);
                svg.line({{mid.x - 8, mid.y - 4}, {mid.x - 4, mid.y + 4}, {mid.x, mid.y - 4}, {mid.x + 4, mid.y + 4},
                          {mid.x + 8, mid.y - 4}},
                         "#BF360C", 2);
                // Its 10 kΩ resistor goes from the column of leg 2 straight into the − rail.
                resistor(svg, b.hole(col + 1, 'j'), b.rail(Rail::BottomMinus, col + 1), resistorValue(2));
                badgeAt = {mid.x - 26, mid.y - 22};
            }
            badge(svg, badgeAt, number);
            svg.close();
        }
    }

    static void badge(Svg& svg, Point at, const std::string& number) {
        svg.circle(at.x, at.y, 11, "#1976D2", "#FFFFFF", 2);
        svg.text({at.x, at.y + 4}, number, 12, "#FFFFFF", "middle", true);
    }

    /// Short wires from the parts to the rails, and the bridges between the rails.
    void drawJumpers(Svg& svg) {
        bool top = false;
        for (std::size_t c = 0; c < placed_.size(); ++c) {
            for (std::size_t k = 0; k < placed_[c].ends.size(); ++k) {
                const End& end = placed_[c].ends[k];
                if (!end.toRail) {
                    continue;
                }
                if (placed_[c].onBreadboard && plan_.components[c].kind == "light_sensor" && k == 2) {
                    continue; // its resistor is drawn into the rail
                }
                const bool ground = end.rail == Rail::TopMinus || end.rail == Rail::BottomMinus;
                top = top || end.rail == Rail::TopMinus || end.rail == Rail::TopPlus;
                const Point railPoint{end.at.x, breadboard_.railY(end.rail)};
                jumper(svg, {end.at, railPoint}, ground ? kBlack : kRed);
                if (!placed_[c].onBreadboard && !end.via.empty()) {
                    resistor(svg, {end.at.x, end.at.y + 6}, {end.at.x, end.at.y + 46}, end.via);
                }
            }
        }
        if (!feeds_.empty()) {
            // The board feeds one pair of rails; the other pair is joined to it at the right end.
            const double x = breadboard_.origin.x + breadboard_.width() + 14;
            const int last = breadboard_.columns - 1;
            jumper(svg, {breadboard_.rail(Rail::TopPlus, last), {x, breadboard_.railY(Rail::TopPlus)},
                         {x, breadboard_.railY(Rail::BottomPlus)}, breadboard_.rail(Rail::BottomPlus, last)},
                   kRed);
            jumper(svg, {breadboard_.rail(Rail::TopMinus, last + 1), {x + 12, breadboard_.railY(Rail::TopMinus)},
                         {x + 12, breadboard_.railY(Rail::BottomMinus)}, breadboard_.rail(Rail::BottomMinus, last + 1)},
                   kBlack);
        }
        (void)top;
    }

    static void jumper(Svg& svg, const std::vector<Point>& points, const std::string& colour) {
        svg.line(points, colour == kBlack ? "#000000" : darker(colour), 7);
        svg.line(points, colour, 4.5);
        for (const Point end : {points.front(), points.back()}) {
            svg.rect(end.x - 4, end.y - 4, 8, 8, "#424242", {}, 1);
            svg.circle(end.x, end.y, 2.2, "#BDBDBD");
        }
    }

    void drawWires(Svg& svg) {
        for (const Wire& wire : wires_) {
            const Point start = onBoard(wire.pin);
            const BoardPin& pin = pins_.at(wire.pin);
            std::vector<Point> points{start};
            if (pin.side == BoardHeader::Side::Right) {
                points.push_back({wire.track, start.y});
                points.push_back({wire.track, wire.lane});
            } else {
                points.push_back({start.x, wire.lane});
            }
            if (wire.lane2 != 0) { // round the breadboard, up to a lane above it
                points.push_back({wire.track, wire.lane});
                points.push_back({wire.track, wire.lane2});
                points.push_back({wire.target.x, wire.lane2});
            } else {
                points.push_back({wire.target.x, wire.lane});
            }
            points.push_back(wire.target);
            jumper(svg, points, wire.colour);
            if (!wire.via.empty() && wire.toModule) { // parts on the breadboard draw their own resistors
                resistor(svg, {wire.target.x, wire.target.y + 8}, {wire.target.x, wire.target.y + 48}, wire.via);
            }
        }
    }

    const Plan& plan_;
    const Board& board_;
    std::string title_;
    Breadboard breadboard_;
    Point boardOrigin_;
    std::map<std::string, BoardPin> pins_;
    std::vector<Label> labels_;
    std::size_t extra_ = 0;
    std::vector<Placed> placed_;
    std::vector<Wire> wires_;
    std::vector<std::pair<std::size_t, std::size_t>> wireOf_; ///< (component, connection), SIZE_MAX for a feed.
    std::vector<bool> targetsModule_;
    std::vector<bool> fromBelow_; ///< Wire arriving in the lower half from below.
    std::vector<std::size_t> feeds_;
    double width_ = 0;
    double height_ = 0;
};

} // namespace

std::string svg(const Plan& plan, const Board& board, const std::string& title) {
    return Diagram(plan, board, title).render();
}

} // namespace s2a::wiring
