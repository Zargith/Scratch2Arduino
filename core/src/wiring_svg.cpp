// Wiring diagram drawn from a wiring::Plan (Phase 13, part 2).
//
// Layout, from left to right: the board (its headers as on the real one), a channel where each
// wire has its own vertical track, the + and GND rails, then the components as numbered cards
// stacked from the top, one pad per leg. A wire from a pin on the top (bottom) edge of the board
// goes up (down) to its own horizontal lane, right to its track, then to the height of its pad:
// no two wires share a segment. Power legs go straight to a rail; the board feeds the rails.
#include "s2a/wiring.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <regex>
#include <sstream>

namespace s2a::wiring {

namespace {

constexpr double kMargin = 30;
constexpr double kTitle = 50;
constexpr double kLane = 14;      ///< Between two lanes (above / below the board).
constexpr double kTrack = 13;     ///< Between two vertical tracks of the channel.
constexpr double kCardWidth = 380;
constexpr double kPadPitch = 24;
constexpr double kCardHead = 44;  ///< Title of a card, above its first pad.
constexpr double kCardGap = 18;

const char* const kSignalColours[] = {"#1976D2", "#F9A825", "#388E3C", "#7B1FA2", "#00ACC1",
                                      "#6D4C41", "#E91E63", "#689F38", "#5C6BC0", "#FF7043"};
constexpr const char* kPlus = "#D32F2F";
constexpr const char* kGround = "#212121";

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

struct Point {
    double x = 0;
    double y = 0;
};

/// A pin of the drawn board, where its wire leaves.
struct BoardPin {
    Point at;
    BoardHeader::Side side = BoardHeader::Side::Top;
};

/// "220 Ω" in "220 Ω resistor" / "résistance 220 Ω" (the value printed by the resistor).
std::string value(const std::string& via) {
    static const std::regex kValue("([0-9]+[.,]?[0-9]* ?k?Ω)");
    std::smatch match;
    return std::regex_search(via, match, kValue) ? match.str(1) : std::string{};
}

class Drawing {
public:
    Drawing(const Plan& plan, const Board& board, std::string title)
        : plan_(plan), board_(board), title_(std::move(title)) {}

    std::string render() {
        placeBoardPins();
        collectWires();
        layout();
        std::ostringstream svg;
        body_ = &svg;
        drawRails();
        drawBoard();
        drawCards();
        drawWires();
        std::ostringstream out;
        out << R"(<svg xmlns="http://www.w3.org/2000/svg" width=")" << num(width_) << R"(" height=")" << num(height_)
            << R"(" viewBox="0 0 )" << num(width_) << ' ' << num(height_)
            << R"(" font-family="DejaVu Sans, Segoe UI, Helvetica, Arial, sans-serif">)" << '\n'
            << R"(<rect width="100%" height="100%" fill="#ECEFF1"/>)" << '\n'
            << text({kMargin, kMargin + 8}, title_, 22, "#263238", "start", true) << svg.str() << "</svg>\n";
        return out.str();
    }

private:
    struct Wire {
        std::size_t card = 0;
        std::size_t pad = 0;
        std::string boardPin;
        std::string via;
        enum class Kind { Signal, PlusRail, GroundRail, FeedPlus, FeedGround } kind = Kind::Signal;
        const char* colour = kGround;
        // Route, filled by layout().
        double lane = 0;  ///< y of the lane (or of the pin for the right side).
        double track = 0; ///< x of the vertical track (signals).
    };

    [[nodiscard]] std::string plusLabel() const { return board_.voltage >= 4.5 ? "5V" : "3V3"; }

    void placeBoardPins() {
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
                if (entry->pin != "-" && pins_.find(entry->pin) == pins_.end()) { // the first GND or 5V
                    pins_[entry->pin] = {at, header.side};
                }
                offset += header.pitch;
            }
        }
    }

    /// The drawn pin of `label`; pins missing from the drawing get a pad on the right edge.
    BoardPin& boardPin(const std::string& label) {
        if (const auto found = pins_.find(label); found != pins_.end()) {
            return found->second;
        }
        const double width = board_.drawing.width > 0 ? board_.drawing.width : 300;
        const Point at{width - 14, 24 + 17 * static_cast<double>(extra_++)};
        labels_.push_back({at, BoardHeader::Side::Right, label});
        return pins_[label] = {at, BoardHeader::Side::Right};
    }

    void collectWires() {
        std::size_t colour = 0;
        bool plusUsed = false;
        bool groundUsed = false;
        for (std::size_t card = 0; card < plan_.components.size(); ++card) {
            const auto& connections = plan_.components[card].connections;
            for (std::size_t pad = 0; pad < connections.size(); ++pad) {
                Wire wire;
                wire.card = card;
                wire.pad = pad;
                wire.boardPin = connections[pad].boardPin;
                wire.via = value(connections[pad].via);
                if (wire.boardPin == "GND") {
                    wire.kind = Wire::Kind::GroundRail;
                    groundUsed = true;
                } else if (wire.boardPin == plusLabel()) {
                    wire.kind = Wire::Kind::PlusRail;
                    wire.colour = kPlus;
                    plusUsed = true;
                } else {
                    wire.colour = wire.boardPin == "5V" || wire.boardPin == "VIN"
                                      ? "#F57C00"
                                      : kSignalColours[colour++ % std::size(kSignalColours)];
                    (void)boardPin(wire.boardPin);
                }
                wires_.push_back(std::move(wire));
            }
        }
        if (plusUsed) {
            Wire feed;
            feed.kind = Wire::Kind::FeedPlus;
            feed.boardPin = plusLabel();
            feed.colour = kPlus;
            (void)boardPin(feed.boardPin);
            wires_.push_back(std::move(feed));
        }
        if (groundUsed) {
            Wire feed;
            feed.kind = Wire::Kind::FeedGround;
            feed.boardPin = "GND";
            (void)boardPin(feed.boardPin);
            wires_.push_back(std::move(feed));
        }
    }

    [[nodiscard]] bool fromBoard(const Wire& wire) const {
        return wire.kind == Wire::Kind::Signal || wire.kind == Wire::Kind::FeedPlus ||
               wire.kind == Wire::Kind::FeedGround;
    }

    void layout() {
        // Lanes: the pins furthest right get the lanes closest to the board, so that the
        // horizontal part of a wire never crosses the vertical part of another one.
        std::vector<Wire*> top;
        std::vector<Wire*> bottom;
        for (Wire& wire : wires_) {
            if (!fromBoard(wire)) {
                continue;
            }
            const BoardPin& pin = pins_.at(wire.boardPin);
            if (pin.side == BoardHeader::Side::Top) {
                top.push_back(&wire);
            } else if (pin.side == BoardHeader::Side::Bottom) {
                bottom.push_back(&wire);
            }
        }
        const auto byX = [this](const Wire* a, const Wire* b) {
            return pins_.at(a->boardPin).at.x > pins_.at(b->boardPin).at.x;
        };
        std::stable_sort(top.begin(), top.end(), byX);
        std::stable_sort(bottom.begin(), bottom.end(), byX);

        const double boardWidth = board_.drawing.width > 0 ? board_.drawing.width : 300;
        const double boardHeight = board_.drawing.height > 0 ? board_.drawing.height : 200;
        boardOrigin_ = {kMargin + 10, kMargin + kTitle + 30 + 24 + kLane * static_cast<double>(top.size())};
        for (std::size_t i = 0; i < top.size(); ++i) {
            top[i]->lane = boardOrigin_.y - 24 - kLane * static_cast<double>(i);
        }
        for (std::size_t i = 0; i < bottom.size(); ++i) {
            bottom[i]->lane = boardOrigin_.y + boardHeight + 24 + kLane * static_cast<double>(i);
        }
        for (Wire& wire : wires_) {
            if (fromBoard(wire) && pins_.at(wire.boardPin).side == BoardHeader::Side::Right) {
                const Point at = pins_.at(wire.boardPin).at;
                // A pin with another one on its right (double header) passes between its pins.
                wire.lane = boardOrigin_.y + at.y + (hasRightNeighbour(at) ? 8.5 : 0);
            }
        }

        // Tracks of the channel: a lane further from the board gets a track further right, so
        // that a horizontal part ends before the vertical parts of the wires below it.
        std::vector<Wire*> signals;
        for (Wire& wire : wires_) {
            if (wire.kind == Wire::Kind::Signal) {
                signals.push_back(&wire);
            }
        }
        const double middle = boardOrigin_.y + boardHeight / 2;
        std::stable_sort(signals.begin(), signals.end(), [middle](const Wire* a, const Wire* b) {
            return std::abs(a->lane - middle) < std::abs(b->lane - middle);
        });
        double track = boardOrigin_.x + boardWidth + 40;
        for (Wire* wire : signals) {
            wire->track = track;
            track += kTrack;
        }
        plusRail_ = track + 24;
        groundRail_ = plusRail_ + 18;
        cardLeft_ = groundRail_ + 70;

        double y = kMargin + kTitle + 30; // room for the names of the rails
        for (const Component& component : plan_.components) {
            cardTops_.push_back(y);
            y += cardHeight(component) + kCardGap;
        }
        const double cardsBottom = y;
        const double boardBottom = boardOrigin_.y + boardHeight + 24 + kLane * static_cast<double>(bottom.size());
        railTop_ = std::min(kMargin + kTitle, boardOrigin_.y - 24 - kLane * static_cast<double>(top.size())) + 30;
        railBottom_ = std::max(cardsBottom - kCardGap, boardBottom);
        width_ = cardLeft_ + kCardWidth + kMargin;
        height_ = std::max(cardsBottom, boardBottom + 10) + kMargin;
    }

    /// Whether a drawn pin stands on the right of `at`, on the same row.
    [[nodiscard]] bool hasRightNeighbour(Point at) const {
        return std::any_of(labels_.begin(), labels_.end(), [at](const Label& label) {
            return std::abs(label.at.y - at.y) < 1 && label.at.x > at.x + 1;
        });
    }

    [[nodiscard]] bool hasLeftNeighbour(Point at) const {
        return std::any_of(labels_.begin(), labels_.end(), [at](const Label& label) {
            return std::abs(label.at.y - at.y) < 1 && label.at.x < at.x - 1 && label.side == BoardHeader::Side::Right;
        });
    }

    static double cardHeight(const Component& component) {
        return std::max(110.0, kCardHead + kPadPitch * static_cast<double>(component.connections.size()) + 8);
    }

    [[nodiscard]] Point pad(std::size_t card, std::size_t index) const {
        return {cardLeft_, cardTops_.at(card) + kCardHead + kPadPitch * static_cast<double>(index)};
    }

    [[nodiscard]] Point onBoard(const std::string& label) const {
        const BoardPin& pin = pins_.at(label);
        return {boardOrigin_.x + pin.at.x, boardOrigin_.y + pin.at.y};
    }

    // --- drawing ------------------------------------------------------------------------

    static std::string text(Point at, const std::string& content, double size, const char* colour,
                            const char* anchor = "start", bool bold = false, double rotate = 0) {
        std::ostringstream out;
        out << R"(<text x=")" << num(at.x) << R"(" y=")" << num(at.y) << R"(" font-size=")" << num(size)
            << R"(" fill=")" << colour << R"(" text-anchor=")" << anchor << '"';
        if (bold) {
            out << R"( font-weight="bold")";
        }
        if (rotate != 0) {
            out << R"( transform="rotate()" << num(rotate) << ' ' << num(at.x) << ' ' << num(at.y) << ")\"";
        }
        out << '>' << escape(content) << "</text>\n";
        return out.str();
    }

    void rect(double x, double y, double w, double h, const char* fill, const char* stroke = nullptr,
              double radius = 0, double strokeWidth = 1) {
        *body_ << R"(<rect x=")" << num(x) << R"(" y=")" << num(y) << R"(" width=")" << num(w) << R"(" height=")"
               << num(h) << R"(" rx=")" << num(radius) << R"(" fill=")" << fill << '"';
        if (stroke != nullptr) {
            *body_ << R"( stroke=")" << stroke << R"(" stroke-width=")" << num(strokeWidth) << '"';
        }
        *body_ << "/>\n";
    }

    void circle(double x, double y, double r, const char* fill, const char* stroke = nullptr, double strokeWidth = 1) {
        *body_ << R"(<circle cx=")" << num(x) << R"(" cy=")" << num(y) << R"(" r=")" << num(r) << R"(" fill=")"
               << fill << '"';
        if (stroke != nullptr) {
            *body_ << R"( stroke=")" << stroke << R"(" stroke-width=")" << num(strokeWidth) << '"';
        }
        *body_ << "/>\n";
    }

    void line(const std::vector<Point>& points, const char* colour, double width) {
        *body_ << R"(<polyline fill="none" stroke-linejoin="round" stroke-linecap="round" stroke=")" << colour
               << R"(" stroke-width=")" << num(width) << R"(" points=")";
        for (const Point& point : points) {
            *body_ << num(point.x) << ',' << num(point.y) << ' ';
        }
        *body_ << "\"/>\n";
    }

    void drawRails() {
        for (const auto& [x, colour, label] :
             {std::tuple{plusRail_, kPlus, std::string("+ ") + plusLabel()}, std::tuple{groundRail_, "#1565C0", std::string("GND")}}) {
            line({{x, railTop_}, {x, railBottom_}}, colour, 3);
            *body_ << text({x + 4, railTop_ - 6}, label, 11, colour, "start", true, -90);
        }
    }

    void drawBoard() {
        const BoardDrawing& drawing = board_.drawing;
        const double w = drawing.width > 0 ? drawing.width : 300;
        const double h = drawing.height > 0 ? drawing.height : 200;
        const Point o = boardOrigin_;
        rect(o.x + 4, o.y + 6, w, h, "#B0BEC5", nullptr, 14);
        rect(o.x, o.y, w, h, drawing.colour.empty() ? "#00878F" : drawing.colour.c_str(), "#37474F", 14, 1.5);
        *body_ << text({o.x + w / 2, o.y + h / 2 + 10}, drawing.title.empty() ? board_.name : drawing.title,
                       std::min(40.0, h / 5), "#FFFFFF", "middle", true);
        std::map<std::string, bool> used;
        for (const Wire& wire : wires_) {
            if (fromBoard(wire)) {
                used[wire.boardPin] = true;
            }
        }
        for (const Label& label : labels_) {
            const Point at{o.x + label.at.x, o.y + label.at.y};
            rect(at.x - 6, at.y - 6, 12, 12, "#1E1E1E");
            rect(at.x - 3, at.y - 3, 6, 6, "#9E9E9E");
            const double size = 9;
            if (label.side == BoardHeader::Side::Right) {
                const bool outer = hasLeftNeighbour(label.at); // double header: names on the outside
                *body_ << text({outer ? at.x + 10 : at.x - 10, at.y + 3}, label.text, size, "#FFFFFF",
                               outer ? "start" : "end");
            } else {
                const double dy = label.side == BoardHeader::Side::Top ? 12 : -12;
                *body_ << text({at.x + 3, at.y + dy}, label.text, size, "#FFFFFF",
                               label.side == BoardHeader::Side::Top ? "end" : "start", false, -90);
            }
        }
        for (const auto& [pin, yes] : used) {
            const Point at = onBoard(pin);
            circle(at.x, at.y, 9, "none", "#FFEB3B", 2.5);
        }
    }

    void drawCards() {
        for (std::size_t card = 0; card < plan_.components.size(); ++card) {
            const Component& component = plan_.components[card];
            const double top = cardTops_[card];
            const double h = cardHeight(component);
            rect(cardLeft_ + 3, top + 4, kCardWidth, h, "#B0BEC5", nullptr, 10);
            rect(cardLeft_, top, kCardWidth, h, "#FFFFFF", "#90A4AE", 10, 1.5);
            circle(cardLeft_ + 22, top + 20, 12, "#1976D2");
            *body_ << text({cardLeft_ + 22, top + 25}, std::to_string(card + 1), 13, "#FFFFFF", "middle", true);
            *body_ << text({cardLeft_ + 42, top + 25}, component.name, 13, "#263238", "start", true);
            for (std::size_t index = 0; index < component.connections.size(); ++index) {
                const Point at = pad(card, index);
                rect(at.x - 6, at.y - 6, 12, 12, "#212121", nullptr, 2);
                *body_ << text({at.x + 14, at.y + 4}, component.connections[index].componentPin, 11, "#37474F");
            }
            icon(component.kind, cardLeft_ + kCardWidth - 62, top + h / 2 + 6);
        }
    }

    /// A small picture of the part, centred on (x, y).
    void icon(const std::string& kind, double x, double y) {
        if (kind == "output") {
            line({{x - 5, y + 10}, {x - 5, y + 28}}, "#9E9E9E", 2);
            line({{x + 5, y + 10}, {x + 5, y + 22}}, "#9E9E9E", 2);
            rect(x - 13, y - 22, 26, 34, "#E53935", "#B71C1C", 12, 1.5);
        } else if (kind == "rgb_led") {
            rect(x - 15, y - 22, 30, 36, "#F5F5F5", "#9E9E9E", 13, 1.5);
            circle(x - 6, y - 8, 4, "#E53935");
            circle(x + 6, y - 8, 4, "#43A047");
            circle(x, y + 2, 4, "#1E88E5");
        } else if (kind == "button" || kind == "input") {
            rect(x - 22, y - 22, 44, 44, "#90A4AE", "#607D8B", 5, 1.5);
            circle(x, y, 12, "#263238");
        } else if (kind == "potentiometer") {
            rect(x - 26, y - 20, 52, 40, "#1565C0", "#0D47A1", 4, 1.5);
            circle(x, y, 14, "#ECEFF1", "#90A4AE", 1.5);
            line({{x, y}, {x + 7, y - 10}}, "#455A64", 3);
        } else if (kind == "light_sensor" || kind == "analog_input") {
            circle(x, y, 18, "#FFCC80", "#EF6C00", 1.5);
            line({{x - 10, y - 6}, {x - 4, y + 6}, {x + 2, y - 6}, {x + 8, y + 6}, {x + 12, y - 2}}, "#BF360C", 2);
        } else if (kind == "ultrasonic") {
            rect(x - 34, y - 18, 68, 36, "#1565C0", "#0D47A1", 4, 1.5);
            circle(x - 16, y, 12, "#CFD8DC", "#78909C", 2);
            circle(x + 16, y, 12, "#CFD8DC", "#78909C", 2);
        } else if (kind == "dht") {
            rect(x - 18, y - 24, 36, 48, "#42A5F5", "#1E88E5", 4, 1.5);
            for (int row = 0; row < 4; ++row) {
                for (int col = 0; col < 3; ++col) {
                    rect(x - 13 + col * 10, y - 18 + row * 10, 6, 5, "#1565C0", nullptr, 1);
                }
            }
        } else if (kind == "buzzer") {
            circle(x, y, 22, "#212121");
            circle(x, y, 5, "#424242");
            *body_ << text({x - 10, y - 8}, "+", 12, "#FFFFFF", "middle", true);
        } else if (kind == "servo") {
            rect(x - 30, y - 14, 60, 30, "#1E88E5", "#0D47A1", 4, 1.5);
            circle(x - 12, y + 1, 7, "#FFFFFF");
            line({{x - 12, y + 1}, {x - 12, y - 24}}, "#FFFFFF", 6);
        } else if (kind == "dc_motor") {
            circle(x, y, 22, "#B0BEC5", "#607D8B", 2);
            *body_ << text({x, y + 7}, "M", 20, "#37474F", "middle", true);
        } else if (kind == "neopixel") {
            rect(x - 34, y - 10, 68, 20, "#212121", nullptr, 3);
            const char* colours[] = {"#E53935", "#43A047", "#1E88E5", "#FDD835"};
            for (int i = 0; i < 4; ++i) {
                circle(x - 24 + i * 16, y, 5, colours[i]);
            }
        } else if (kind == "matrix") {
            rect(x - 24, y - 24, 48, 48, "#212121", nullptr, 3);
            for (int row = 0; row < 4; ++row) {
                for (int col = 0; col < 4; ++col) {
                    circle(x - 15 + col * 10, y - 15 + row * 10, 3.5, (row + col) % 2 == 0 ? "#F44336" : "#5D1A17");
                }
            }
        } else if (kind == "lcd_i2c" || kind == "lcd16") {
            rect(x - 36, y - 18, 72, 36, "#2E7D32", "#1B5E20", 3, 1.5);
            rect(x - 30, y - 12, 60, 24, "#9CCC65", nullptr, 2);
        } else if (kind == "oled") {
            rect(x - 26, y - 22, 52, 44, "#263238", nullptr, 4);
            rect(x - 20, y - 14, 40, 26, "#000000", nullptr, 1);
            line({{x - 15, y - 6}, {x + 12, y - 6}}, "#4FC3F7", 2);
            line({{x - 15, y + 2}, {x + 6, y + 2}}, "#4FC3F7", 2);
        } else {
            rect(x - 24, y - 18, 48, 36, "#CFD8DC", "#90A4AE", 4, 1.5);
        }
    }

    void resistor(double x, double y, const std::string& label) {
        rect(x - 14, y - 5, 28, 10, "#D7CCC8", "#8D6E63", 4, 1);
        rect(x - 7, y - 5, 3, 10, "#E53935");
        rect(x - 1, y - 5, 3, 10, "#E53935");
        rect(x + 5, y - 5, 3, 10, "#6D4C41");
        *body_ << text({x, y - 9}, label, 10, "#4E342E", "middle", true);
    }

    void wire(const std::vector<Point>& points, const char* colour) {
        line(points, colour == kGround ? "#000000" : "#37474F", 6);
        line(points, colour, 3.5);
        for (const Point& end : {points.front(), points.back()}) {
            circle(end.x, end.y, 3, "#616161");
        }
    }

    void drawWires() {
        for (const Wire& w : wires_) {
            std::vector<Point> points;
            if (w.kind == Wire::Kind::PlusRail || w.kind == Wire::Kind::GroundRail) {
                const Point end = pad(w.card, w.pad);
                const double rail = w.kind == Wire::Kind::PlusRail ? plusRail_ : groundRail_;
                points = {end, {rail, end.y}};
            } else {
                const Point start = onBoard(w.boardPin);
                const BoardPin& pin = pins_.at(w.boardPin);
                points.push_back(start);
                if (pin.side == BoardHeader::Side::Right) {
                    points.push_back({start.x + 10, start.y});
                    points.push_back({start.x + 10, w.lane});
                } else {
                    points.push_back({start.x, w.lane});
                }
                const double laneY = points.back().y;
                if (w.kind == Wire::Kind::FeedPlus || w.kind == Wire::Kind::FeedGround) {
                    points.push_back({w.kind == Wire::Kind::FeedPlus ? plusRail_ : groundRail_, laneY});
                } else {
                    const Point end = pad(w.card, w.pad);
                    points.push_back({w.track, laneY});
                    points.push_back({w.track, end.y});
                    points.push_back(end);
                }
            }
            wire(points, w.colour);
            if (!w.via.empty()) {
                const Point end = pad(w.card, w.pad);
                resistor((groundRail_ + cardLeft_) / 2, end.y, w.via);
            }
        }
    }

    struct Label {
        Point at;
        BoardHeader::Side side;
        std::string text;
    };

    const Plan& plan_;
    const Board& board_;
    std::string title_;
    std::ostringstream* body_ = nullptr;
    std::map<std::string, BoardPin> pins_;
    std::vector<Label> labels_;
    std::size_t extra_ = 0;
    std::vector<Wire> wires_;
    Point boardOrigin_;
    double plusRail_ = 0;
    double groundRail_ = 0;
    double cardLeft_ = 0;
    std::vector<double> cardTops_;
    double railTop_ = 0;
    double railBottom_ = 0;
    double width_ = 0;
    double height_ = 0;
};

} // namespace

std::string svg(const Plan& plan, const Board& board, const std::string& title) {
    return Drawing(plan, board, title).render();
}

} // namespace s2a::wiring
