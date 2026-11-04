#pragma once

#include "editing.hpp"
#include "layout.hpp"

#include "s2a/boards.hpp"
#include "s2a/i18n.hpp"
#include "s2a/project.hpp"

#include <QPointF>
#include <QPointer>
#include <QWidget>
#include <optional>
#include <string>
#include <vector>

class QLineEdit;

namespace s2a::native {

/// The block editor: palette on the left, scripts on the right. Edits never raise errors;
/// problems are reported by the validator when the user builds.
class Canvas : public QWidget {
    Q_OBJECT

public:
    explicit Canvas(Language language, QWidget* parent = nullptr);

    [[nodiscard]] const project::Project& project() const { return project_; }
    void setProject(project::Project project);
    void setLanguage(Language language);
    void setBoard(const std::string& boardId);
    void addVariable(const std::string& name);
    /// Outlines these blocks and scrolls to the first one (problems found by Check/Upload).
    /// The outline goes away at the next edit.
    void highlight(std::vector<std::string> blockIds);
    void zoomBy(double factor);
    void resetZoom();

    /// Widget position of the palette block of `type` (tests), or (-1, -1).
    [[nodiscard]] QPointF paletteBlockPosition(const std::string& type) const;
    /// Widget position of the top-left corner of block `id` in the scripts area (tests).
    [[nodiscard]] QPointF blockPosition(const std::string& id) const;

    static constexpr int kCategoryWidth = 64;
    static constexpr int kPaletteWidth = 250;

signals:
    void changed();                 ///< The project was edited.
    void variableRequested();       ///< "Make a variable" was clicked.

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    struct PaletteItem {
        project::Stack stack; ///< One block.
        std::vector<BlockGeometry> geometry;
    };
    struct Drag {
        project::Stack blocks;
        Point origin;        ///< Top-left of the first block, canvas coordinates.
        QPointF grab;        ///< Mouse position relative to `origin`, canvas units.
        double height = 0;
        std::vector<BlockGeometry> geometry;
    };

    [[nodiscard]] LayoutContext context() const;
    void relayout();
    void rebuildPalette();
    void layoutDrag();

    [[nodiscard]] QPointF toCanvas(QPointF screen) const;
    [[nodiscard]] QPointF toScreen(QPointF canvas) const;
    [[nodiscard]] QPointF toPalette(QPointF screen) const;
    [[nodiscard]] bool overPalette(QPointF screen) const;

    void startDrag(QPointF screen);
    void finishDrag(QPointF screen);
    void editPart(const std::string& blockId, const Part& part);
    void paintPalette(QPainter& painter);
    void edited();

    Language language_;
    project::Project project_;
    std::vector<BlockGeometry> blocks_;
    std::vector<PaletteItem> palette_;
    std::vector<std::pair<std::string, double>> categoryTops_; ///< Category id, y in the palette.
    double variableButtonTop_ = -1;
    double paletteHeight_ = 0;
    double paletteScroll_ = 0;

    double zoom_ = 1.0;
    QPointF offset_{-40, -40}; ///< Canvas point shown at the top-left of the scripts area.

    // Mouse interaction.
    std::optional<QPointF> pressAt_;
    std::string pressedBlock_;   ///< Block under the press (scripts area).
    int pressedPalette_ = -1;    ///< Palette item under the press.
    bool panning_ = false;
    QPointF panFrom_;
    std::optional<Drag> drag_;
    std::optional<Drop> preview_;
    QPointer<QLineEdit> editor_;
    std::vector<std::string> highlighted_;
};

} // namespace s2a::native
