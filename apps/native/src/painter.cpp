#include "painter.hpp"

#include <QColor>
#include <QFontMetricsF>
#include <QPolygonF>
#include <QString>
#include <algorithm>
#include <variant>
#include <vector>

namespace s2a::native {

namespace {

using namespace metrics;

const QColor kFieldText(0x57, 0x5E, 0x75);

QString qt(const std::string& text) {
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

QRectF qt(const Rect& rect) {
    return {rect.x, rect.y, rect.w, rect.h};
}

void hexagon(QPainterPath& path, const QRectF& rect) {
    const qreal half = rect.height() / 2;
    path.addPolygon(QPolygonF{{rect.left(), rect.center().y()},
                              {rect.left() + half, rect.top()},
                              {rect.right() - half, rect.top()},
                              {rect.right(), rect.center().y()},
                              {rect.right() - half, rect.bottom()},
                              {rect.left() + half, rect.bottom()},
                              {rect.left(), rect.center().y()}});
    path.closeSubpath();
}

/// Notch seen from above: a dip of kNotchDepth, drawn from `from` towards `to` along y.
void notch(QPainterPath& path, qreal from, qreal to, qreal y) {
    const qreal direction = to > from ? 1 : -1;
    path.lineTo(from, y);
    path.lineTo(from + direction * 4, y + kNotchDepth);
    path.lineTo(to - direction * 4, y + kNotchDepth);
    path.lineTo(to, y);
}

} // namespace

QFont blockFont() {
    QFont font;
    font.setPixelSize(13);
    font.setWeight(QFont::DemiBold);
    return font;
}

Measure fontMeasure() {
    return [metrics = QFontMetricsF(blockFont())](const std::string& text) { return metrics.horizontalAdvance(qt(text)); };
}

QPainterPath blockPath(const BlockGeometry& geometry) {
    const QRectF bounds = qt(geometry.bounds);
    QPainterPath path;
    if (geometry.shape == BlockShape::Reporter) {
        path.addRoundedRect(bounds, bounds.height() / 2, bounds.height() / 2);
        return path;
    }
    if (geometry.shape == BlockShape::Boolean) {
        hexagon(path, bounds);
        return path;
    }

    const qreal x = bounds.left();
    const qreal right = bounds.right();
    const qreal bottom = bounds.bottom();
    qreal top = bounds.top();
    if (geometry.shape == BlockShape::Hat) {
        top += kHatHeight;
        path.moveTo(x, top);
        path.cubicTo(x + 20, top - kHatHeight - 4, x + 60, top - kHatHeight - 4, x + 84, top);
        path.lineTo(right, top);
    } else {
        path.moveTo(x, top);
        notch(path, x + kNotchX, x + kNotchX + kNotchWidth, top);
        path.lineTo(right, top);
    }

    // Right side, row by row; C-block mouths go in to the left arm and back.
    std::vector<std::variant<Rect, StatementSlot>> segments(geometry.rows.begin(), geometry.rows.end());
    segments.insert(segments.end(), geometry.mouths.begin(), geometry.mouths.end());
    const auto topOf = [](const auto& segment) {
        return std::visit([](const auto& item) {
            if constexpr (std::is_same_v<std::decay_t<decltype(item)>, Rect>) {
                return item.y;
            } else {
                return item.rect.y;
            }
        }, segment);
    };
    std::sort(segments.begin(), segments.end(), [&](const auto& a, const auto& b) { return topOf(a) < topOf(b); });
    for (const auto& segment : segments) {
        if (const auto* row = std::get_if<Rect>(&segment)) {
            path.lineTo(right, row->bottom());
            continue;
        }
        const Rect& mouth = std::get<StatementSlot>(segment).rect;
        const qreal inner = x + kIndent;
        path.lineTo(right, mouth.y);
        notch(path, inner + kNotchX + kNotchWidth, inner + kNotchX, mouth.y);
        path.lineTo(inner, mouth.y);
        path.lineTo(inner, mouth.bottom());
        path.lineTo(right, mouth.bottom());
    }
    path.lineTo(right, bottom);
    if (!geometry.terminal) {
        notch(path, x + kNotchX + kNotchWidth, x + kNotchX, bottom);
    }
    path.lineTo(x, bottom);
    path.closeSubpath();
    return path;
}

void paintBlock(QPainter& painter, const BlockGeometry& geometry, qreal opacity) {
    painter.save();
    painter.setOpacity(opacity);
    painter.setRenderHint(QPainter::Antialiasing);
    const QColor colour(qt(geometry.colour));
    painter.setPen(QPen(colour.darker(125), 1));
    painter.setBrush(colour);
    painter.drawPath(blockPath(geometry));

    painter.setFont(blockFont());
    for (const Part& part : geometry.parts) {
        const QRectF rect = qt(part.rect);
        switch (part.kind) {
        case PartKind::Label:
            painter.setPen(Qt::white);
            painter.drawText(rect, Qt::AlignVCenter | Qt::AlignLeft, qt(part.text));
            break;
        case PartKind::Field: {
            painter.setPen(QPen(colour.darker(140), 1));
            painter.setBrush(colour.darker(115));
            painter.drawRoundedRect(rect, 4, 4);
            painter.setPen(Qt::white);
            painter.drawText(rect.adjusted(8, 0, -kArrow - 4, 0), Qt::AlignVCenter | Qt::AlignLeft, qt(part.text));
            const QPointF arrow(rect.right() - kArrow + 1, rect.center().y() - 2);
            painter.setBrush(Qt::white);
            painter.setPen(Qt::NoPen);
            painter.drawPolygon(QPolygonF{arrow, arrow + QPointF(8, 0), arrow + QPointF(4, 5)});
            break;
        }
        case PartKind::ValueInput:
            if (part.hasBlock) {
                break; // the reporter paints itself
            }
            if (part.argKind == ArgKind::Boolean) {
                QPainterPath slot;
                hexagon(slot, rect);
                painter.setPen(Qt::NoPen);
                painter.setBrush(colour.darker(125));
                painter.drawPath(slot);
                break;
            }
            painter.setPen(QPen(colour.darker(125), 1));
            painter.setBrush(Qt::white);
            painter.drawRoundedRect(rect, rect.height() / 2, rect.height() / 2);
            painter.setPen(kFieldText);
            painter.drawText(rect, Qt::AlignCenter, qt(part.text));
            break;
        }
    }
    painter.restore();
}

} // namespace s2a::native
