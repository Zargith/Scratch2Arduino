#include "canvas.hpp"

#include "painter.hpp"

#include <QApplication>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace s2a::native {

namespace {

constexpr double kPaletteScale = 0.75;
constexpr double kDragThreshold = 4;
constexpr double kMinZoom = 0.4;
constexpr double kMaxZoom = 2.5;

QString qt(const std::string& text) {
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

std::string utf8(const QString& text) {
    return text.toUtf8().toStdString();
}

QRectF qt(const Rect& rect) {
    return {rect.x, rect.y, rect.w, rect.h};
}

const BlockArg* argOf(const BlockGeometry& geometry, const std::string& name) {
    return geometry.definition != nullptr ? geometry.definition->findArg(name) : nullptr;
}

} // namespace

Canvas::Canvas(Language language, QWidget* parent) : QWidget(parent), language_(language) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(640, 400);
    project_ = project::newProject(language);
    rebuildPalette();
    relayout();
}

LayoutContext Canvas::context() const {
    return {BlockCatalog::builtin(), project_, BoardDatabase::builtin().find(project_.boardId), language_, fontMeasure()};
}

void Canvas::setProject(project::Project project) {
    project_ = std::move(project);
    drag_.reset();
    preview_.reset();
    zoom_ = 1.0;
    offset_ = {-40, -40};
    rebuildPalette();
    relayout();
}

void Canvas::setLanguage(Language language) {
    language_ = language;
    rebuildPalette();
    relayout();
}

void Canvas::setBoard(const std::string& boardId) {
    project_.boardId = boardId;
    relayout();
}

void Canvas::addVariable(const std::string& name) {
    const bool exists = std::any_of(project_.variables.begin(), project_.variables.end(),
                                    [&name](const project::Variable& variable) { return variable.name == name; });
    if (name.empty() || exists) {
        return;
    }
    project_.variables.push_back({project::newId(), name, 0});
    rebuildPalette();
    relayout();
    edited();
}

void Canvas::edited() {
    highlighted_.clear();
    update();
    emit changed();
}

void Canvas::highlight(std::vector<std::string> blockIds) {
    highlighted_ = std::move(blockIds);
    for (const BlockGeometry& geometry : blocks_) {
        if (!highlighted_.empty() && geometry.block->id == highlighted_.front()) {
            // Bring the block near the top-left of the scripts area.
            offset_ = QPointF(geometry.bounds.x - 60, geometry.bounds.y - 60);
            break;
        }
    }
    update();
}

void Canvas::setRunning(std::vector<std::string> blockIds) {
    if (blockIds != running_) {
        running_ = std::move(blockIds);
        update();
    }
}

void Canvas::zoomBy(double factor) {
    zoom_ = std::clamp(zoom_ * factor, kMinZoom, kMaxZoom);
    update();
}

void Canvas::resetZoom() {
    zoom_ = 1.0;
    update();
}

void Canvas::relayout() {
    blocks_ = layoutProject(context());
    update();
}

void Canvas::rebuildPalette() {
    palette_.clear();
    categoryTops_.clear();
    variableButtonTop_ = -1;
    const BlockCatalog& catalog = BlockCatalog::builtin();
    const LayoutContext layout = context();
    std::vector<BlockCategory> categories = catalog.categories();
    std::sort(categories.begin(), categories.end(), [](const auto& a, const auto& b) { return a.order < b.order; });
    double y = 12;
    for (const BlockCategory& category : categories) {
        categoryTops_.emplace_back(category.id, y);
        y += 28; // title
        const bool variables = category.id == "variables";
        if (variables) {
            variableButtonTop_ = y;
            y += 44;
        }
        if (!variables || !project_.variables.empty()) { // variable blocks need a variable
            for (const BlockDefinition* definition : catalog.blocksIn(category.id)) {
                palette_.push_back({{makeBlock(*definition, project_)}, {}});
                // Geometries point into the item's own stack, whose blocks never move.
                PaletteItem& item = palette_.back();
                y += layoutStack(layout, item.stack, {12, y}, -1, item.geometry) + 12;
            }
        }
        y += 16;
    }
    paletteHeight_ = y;
}

QPointF Canvas::paletteBlockPosition(const std::string& type) const {
    for (const PaletteItem& item : palette_) {
        if (!item.stack.empty() && item.stack.front().type == type && !item.geometry.empty()) {
            const Rect& bounds = item.geometry.front().bounds;
            return {kCategoryWidth + (bounds.x + 8) * kPaletteScale, (bounds.y + bounds.h / 2) * kPaletteScale - paletteScroll_};
        }
    }
    return {-1, -1};
}

QPointF Canvas::blockPosition(const std::string& id) const {
    for (const BlockGeometry& geometry : blocks_) {
        if (geometry.block->id == id) {
            return toScreen({geometry.bounds.x, geometry.bounds.y});
        }
    }
    return {-1, -1};
}

QPointF Canvas::toCanvas(QPointF screen) const {
    return (screen - QPointF(kCategoryWidth + kPaletteWidth, 0)) / zoom_ + offset_;
}

QPointF Canvas::toScreen(QPointF canvas) const {
    return (canvas - offset_) * zoom_ + QPointF(kCategoryWidth + kPaletteWidth, 0);
}

QPointF Canvas::toPalette(QPointF screen) const {
    return {(screen.x() - kCategoryWidth) / kPaletteScale, (screen.y() + paletteScroll_) / kPaletteScale};
}

bool Canvas::overPalette(QPointF screen) const {
    return screen.x() < kCategoryWidth + kPaletteWidth;
}

void Canvas::layoutDrag() {
    if (!drag_) {
        return;
    }
    drag_->geometry.clear();
    drag_->height = layoutStack(context(), drag_->blocks, drag_->origin, -1, drag_->geometry);
}

void Canvas::startDrag(QPointF screen) {
    const QPointF press = *pressAt_;
    Drag drag;
    if (!pressedBlock_.empty()) {
        const auto found = std::find_if(blocks_.begin(), blocks_.end(),
                                        [this](const BlockGeometry& g) { return g.block->id == pressedBlock_; });
        if (found == blocks_.end()) {
            return;
        }
        drag.origin = {found->bounds.x, found->bounds.y};
        drag.grab = toCanvas(press) - QPointF(drag.origin.x, drag.origin.y);
        auto detached = detach(project_, pressedBlock_);
        if (!detached) {
            return;
        }
        drag.blocks = std::move(*detached);
    } else if (pressedPalette_ >= 0) {
        const PaletteItem& item = palette_[static_cast<std::size_t>(pressedPalette_)];
        const Rect bounds = item.geometry.front().bounds;
        const QPointF inPalette = toPalette(press);
        drag.grab = inPalette - QPointF(bounds.x, bounds.y);
        const QPointF origin = toCanvas(screen) - drag.grab;
        drag.origin = {origin.x(), origin.y()};
        const BlockDefinition* definition = BlockCatalog::builtin().find(item.stack.front().type);
        drag.blocks.push_back(definition != nullptr ? makeBlock(*definition, project_) : item.stack.front());
    } else {
        return;
    }
    drag_ = std::move(drag);
    relayout();
    layoutDrag();
}

void Canvas::finishDrag(QPointF screen) {
    Drag drag = std::move(*drag_);
    drag_.reset();
    preview_.reset();
    if (!overPalette(screen)) { // dropped on the palette = deleted
        Drop drop = findDrop(blocks_, BlockCatalog::builtin(), drag.blocks, drag.origin, drag.height);
        drop.at = drag.origin;
        applyDrop(project_, drop, std::move(drag.blocks));
    }
    relayout();
    edited();
}

void Canvas::editPart(const std::string& blockId, const Part& part) {
    project::Block* block = findBlock(project_, blockId);
    const auto geometry = std::find_if(blocks_.begin(), blocks_.end(),
                                       [&blockId](const BlockGeometry& g) { return g.block->id == blockId; });
    if (block == nullptr || geometry == blocks_.end()) {
        return;
    }
    const BlockArg* arg = argOf(*geometry, part.name);
    if (arg == nullptr) {
        return;
    }

    if (part.kind == PartKind::Field) {
        QMenu menu(this);
        std::vector<std::pair<QString, std::string>> choices;
        if (arg->kind == ArgKind::Dropdown) {
            for (const DropdownOption& option : arg->options) {
                choices.emplace_back(qt(option.label.get(language_)), option.value);
            }
        } else if (arg->kind == ArgKind::Pin) {
            if (const Board* board = BoardDatabase::builtin().find(project_.boardId)) {
                for (const Pin& pin : board->pins) {
                    if (!arg->pinCapability || pin.has(*arg->pinCapability)) {
                        choices.emplace_back(qt(pin.label), pin.label);
                    }
                }
            }
        } else if (arg->kind == ArgKind::Variable) {
            for (const project::Variable& variable : project_.variables) {
                choices.emplace_back(qt(variable.name), variable.id);
            }
        }
        for (const auto& [label, value] : choices) {
            QAction* action = menu.addAction(label);
            action->setData(qt(value));
        }
        QAction* makeVariable = nullptr;
        if (arg->kind == ArgKind::Variable) {
            menu.addSeparator();
            makeVariable = menu.addAction(qt(s2a::tr("editor.make_variable", language_)));
        }
        QAction* chosen = menu.exec(mapToGlobal(toScreen({part.rect.x, part.rect.bottom()}).toPoint()));
        if (chosen == nullptr) {
            return;
        }
        if (chosen == makeVariable) {
            emit variableRequested();
            return;
        }
        block->fields[part.name] = utf8(chosen->data().toString());
        relayout();
        edited();
        return;
    }

    if (part.kind == PartKind::ValueInput && !part.hasBlock && part.argKind != ArgKind::Boolean) {
        delete editor_;
        auto* editor = new QLineEdit(qt(part.text), this);
        editor_ = editor;
        const QPointF topLeft = toScreen({part.rect.x, part.rect.y});
        editor->setGeometry(QRectF(topLeft, QSizeF(std::max(part.rect.w, 48.0), part.rect.h) * zoom_).toRect());
        editor->setAlignment(Qt::AlignCenter);
        editor->selectAll();
        editor->show();
        editor->setFocus();
        const std::string name = part.name;
        connect(editor, &QLineEdit::editingFinished, this, [this, editor, blockId, name]() {
            if (project::Block* target = findBlock(project_, blockId)) {
                target->inputs[name].value = utf8(editor->text());
                edited();
            }
            editor->deleteLater();
            relayout();
        });
    }
}

void Canvas::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF scripts(kCategoryWidth + kPaletteWidth, 0, width() - kCategoryWidth - kPaletteWidth, height());
    painter.fillRect(scripts, QColor(0xF9, 0xF9, 0xF9));

    // Dotted grid.
    painter.save();
    painter.setClipRect(scripts);
    painter.setPen(QPen(QColor(0xDD, 0xDD, 0xDD), 2));
    const double step = 40 * zoom_;
    const QPointF start = toScreen({std::floor(offset_.x() / 40) * 40, std::floor(offset_.y() / 40) * 40});
    for (double x = start.x(); x < width(); x += step) {
        for (double y = start.y(); y < height(); y += step) {
            painter.drawPoint(QPointF(x, y));
        }
    }

    painter.translate(scripts.topLeft());
    painter.scale(zoom_, zoom_);
    painter.translate(-offset_);
    // Live mode: a yellow glow under the scripts that are running.
    std::vector<int> runningScripts;
    for (const BlockGeometry& geometry : blocks_) {
        if (std::find(running_.begin(), running_.end(), geometry.block->id) != running_.end()) {
            runningScripts.push_back(geometry.script);
        }
    }
    if (!runningScripts.empty()) {
        painter.setPen(QPen(QColor(0xFF, 0xF2, 0x00), 8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        for (const BlockGeometry& geometry : blocks_) {
            if (std::find(runningScripts.begin(), runningScripts.end(), geometry.script) != runningScripts.end()) {
                painter.drawPath(blockPath(geometry));
            }
        }
    }
    for (const BlockGeometry& geometry : blocks_) {
        paintBlock(painter, geometry);
    }
    for (const BlockGeometry& geometry : blocks_) {
        if (std::find(highlighted_.begin(), highlighted_.end(), geometry.block->id) != highlighted_.end()) {
            painter.setPen(QPen(QColor(0xE5, 0x48, 0x4D), 4));
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(blockPath(geometry));
        }
    }
    if (preview_ && drag_) {
        // Where the dragged blocks will snap.
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 60));
        const auto target = std::find_if(blocks_.begin(), blocks_.end(),
                                         [this](const BlockGeometry& g) { return g.block->id == preview_->target; });
        if (target != blocks_.end()) {
            QRectF marker;
            switch (preview_->kind) {
            case Drop::Kind::After:
                marker = {target->bounds.x, target->bounds.bottom() - 3, 120, 6};
                break;
            case Drop::Kind::Before:
                marker = {target->bounds.x, target->bounds.y - 3, 120, 6};
                break;
            case Drop::Kind::Into:
                for (const StatementSlot& slot : target->mouths) {
                    if (slot.name == preview_->name) {
                        marker = {slot.rect.x, slot.rect.y - 3, 120, 6};
                    }
                }
                break;
            case Drop::Kind::PlugInput:
                for (const Part& part : target->parts) {
                    if (part.name == preview_->name) {
                        marker = qt(part.rect).adjusted(-3, -3, 3, 3);
                    }
                }
                break;
            case Drop::Kind::NewScript:
                break;
            }
            painter.drawRoundedRect(marker, 3, 3);
        }
    }
    painter.restore();

    paintPalette(painter);

    if (drag_) {
        painter.save();
        painter.translate(scripts.topLeft());
        painter.scale(zoom_, zoom_);
        painter.translate(-offset_);
        for (const BlockGeometry& geometry : drag_->geometry) {
            paintBlock(painter, geometry, 0.85);
        }
        painter.restore();
    }
}

void Canvas::paintPalette(QPainter& painter) {
    const BlockCatalog& catalog = BlockCatalog::builtin();
    painter.fillRect(QRectF(0, 0, kCategoryWidth, height()), Qt::white);
    painter.fillRect(QRectF(kCategoryWidth, 0, kPaletteWidth, height()), QColor(0xF0, 0xF2, 0xF5));
    painter.setPen(QColor(0xDD, 0xDD, 0xDD));
    painter.drawLine(QPointF(kCategoryWidth + kPaletteWidth, 0), QPointF(kCategoryWidth + kPaletteWidth, height()));

    // Category column.
    QFont small = blockFont();
    small.setPixelSize(11);
    small.setWeight(QFont::Normal);
    painter.setFont(small);
    double y = 10;
    for (const auto& [id, top] : categoryTops_) {
        const BlockCategory* category = catalog.findCategory(id);
        if (category == nullptr) {
            continue;
        }
        painter.setPen(QColor(category->colour.c_str()).darker(120));
        painter.setBrush(QColor(category->colour.c_str()));
        painter.drawEllipse(QPointF(kCategoryWidth / 2.0, y + 12), 11, 11);
        painter.setPen(QColor(0x57, 0x5E, 0x75));
        painter.drawText(QRectF(0, y + 26, kCategoryWidth, 16), Qt::AlignCenter, qt(category->name.get(language_)));
        y += 56;
    }

    // Blocks.
    painter.save();
    painter.setClipRect(QRectF(kCategoryWidth, 0, kPaletteWidth, height()));
    painter.translate(kCategoryWidth, -paletteScroll_);
    painter.scale(kPaletteScale, kPaletteScale);
    QFont title = blockFont();
    title.setPixelSize(16);
    painter.setFont(title);
    for (const auto& [id, top] : categoryTops_) {
        if (const BlockCategory* category = catalog.findCategory(id)) {
            painter.setPen(QColor(0x57, 0x5E, 0x75));
            painter.drawText(QRectF(12, top, kPaletteWidth / kPaletteScale, 24), Qt::AlignVCenter | Qt::AlignLeft,
                             qt(category->name.get(language_)));
        }
    }
    if (variableButtonTop_ >= 0) {
        const QRectF button(12, variableButtonTop_, 220, 34);
        painter.setPen(QColor(0xC6, 0xC6, 0xC6));
        painter.setBrush(Qt::white);
        painter.drawRoundedRect(button, 6, 6);
        painter.setPen(QColor(0x57, 0x5E, 0x75));
        painter.setFont(blockFont());
        painter.drawText(button, Qt::AlignCenter, qt(s2a::tr("editor.make_variable", language_)));
    }
    for (const PaletteItem& item : palette_) {
        for (const BlockGeometry& geometry : item.geometry) {
            paintBlock(painter, geometry);
        }
    }
    painter.restore();
}

void Canvas::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        return;
    }
    if (editor_) {
        editor_->clearFocus(); // commits the value
    }
    const QPointF screen = event->position();
    pressAt_ = screen;
    pressedBlock_.clear();
    pressedPalette_ = -1;

    if (screen.x() < kCategoryWidth) {
        const int index = static_cast<int>((screen.y() - 10) / 56);
        if (index >= 0 && static_cast<std::size_t>(index) < categoryTops_.size()) {
            paletteScroll_ = categoryTops_[static_cast<std::size_t>(index)].second * kPaletteScale - 8;
            update();
        }
        pressAt_.reset();
        return;
    }
    if (overPalette(screen)) {
        const QPointF point = toPalette(screen);
        if (variableButtonTop_ >= 0 && QRectF(12, variableButtonTop_, 220, 34).contains(point)) {
            pressAt_.reset();
            emit variableRequested();
            return;
        }
        for (std::size_t i = 0; i < palette_.size(); ++i) {
            if (!palette_[i].geometry.empty() && palette_[i].geometry.front().bounds.contains({point.x(), point.y()})) {
                pressedPalette_ = static_cast<int>(i);
            }
        }
        return;
    }
    const QPointF point = toCanvas(screen);
    const int hit = hitBlock(blocks_, {point.x(), point.y()});
    if (hit >= 0) {
        pressedBlock_ = blocks_[static_cast<std::size_t>(hit)].block->id;
    } else {
        panning_ = true;
        panFrom_ = screen;
        setCursor(Qt::ClosedHandCursor);
    }
}

void Canvas::mouseMoveEvent(QMouseEvent* event) {
    const QPointF screen = event->position();
    if (drag_) {
        const QPointF origin = toCanvas(screen) - drag_->grab;
        drag_->origin = {origin.x(), origin.y()};
        layoutDrag();
        Drop drop = findDrop(blocks_, BlockCatalog::builtin(), drag_->blocks, drag_->origin, drag_->height);
        preview_ = overPalette(screen) || drop.kind == Drop::Kind::NewScript ? std::nullopt : std::optional<Drop>(drop);
        update();
        return;
    }
    if (panning_) {
        offset_ -= (screen - panFrom_) / zoom_;
        panFrom_ = screen;
        update();
        return;
    }
    if (pressAt_ && (event->buttons() & Qt::LeftButton) != 0 &&
        QLineF(*pressAt_, screen).length() > kDragThreshold && (!pressedBlock_.empty() || pressedPalette_ >= 0)) {
        startDrag(screen);
    }
}

void Canvas::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        return;
    }
    const QPointF screen = event->position();
    if (panning_) {
        panning_ = false;
        unsetCursor();
    } else if (drag_) {
        finishDrag(screen);
    } else if (!pressedBlock_.empty()) {
        // A click: edit the field or the value under the mouse.
        const QPointF point = toCanvas(screen);
        const auto geometry = std::find_if(blocks_.begin(), blocks_.end(),
                                           [this](const BlockGeometry& g) { return g.block->id == pressedBlock_; });
        if (geometry != blocks_.end()) {
            for (const Part& part : geometry->parts) {
                if (part.kind != PartKind::Label && part.rect.contains({point.x(), point.y()})) {
                    const Part copy = part;
                    editPart(pressedBlock_, copy);
                    break;
                }
            }
        }
    }
    pressAt_.reset();
    pressedBlock_.clear();
    pressedPalette_ = -1;
}

void Canvas::wheelEvent(QWheelEvent* event) {
    const QPointF delta = QPointF(event->angleDelta()) / 120.0;
    if ((event->modifiers() & Qt::ControlModifier) != 0) {
        const QPointF anchor = toCanvas(event->position());
        zoomBy(delta.y() > 0 ? 1.1 : 1 / 1.1);
        offset_ += anchor - toCanvas(event->position()); // zoom around the mouse
    } else if (overPalette(event->position())) {
        paletteScroll_ = std::clamp(paletteScroll_ - delta.y() * 60, 0.0,
                                    std::max(0.0, paletteHeight_ * kPaletteScale - height() + 40));
    } else {
        offset_ -= QPointF(delta.x(), delta.y()) * 60 / zoom_;
    }
    update();
}

} // namespace s2a::native
