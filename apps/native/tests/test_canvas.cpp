// UI tests of the native block editor, run with the offscreen Qt platform. Set
// S2A_SCREENSHOT_DIR to also save screenshots of what is drawn.
#include "canvas.hpp"
#include "main_window.hpp"

#include "s2a/project.hpp"

#include <QApplication>
#include <QDir>
#include <QMouseEvent>
#include <QPixmap>
#include <QTest>
#include <fstream>
#include <sstream>

namespace {

using s2a::Language;
using s2a::native::Canvas;

void send(QWidget* widget, QEvent::Type type, QPointF at, Qt::MouseButtons buttons) {
    const Qt::MouseButton button = type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton;
    QMouseEvent event(type, at, widget->mapToGlobal(at), button, buttons, Qt::NoModifier);
    QApplication::sendEvent(widget, &event);
}

/// Press, move in small steps (as a hand would), release.
void drag(QWidget* widget, QPointF from, QPointF to) {
    send(widget, QEvent::MouseButtonPress, from, Qt::LeftButton);
    for (int step = 1; step <= 10; ++step) {
        send(widget, QEvent::MouseMove, from + (to - from) * step / 10.0, Qt::LeftButton);
    }
    send(widget, QEvent::MouseButtonRelease, to, Qt::NoButton);
}

void screenshot(QWidget* widget, const QString& name) {
    const QString folder = qEnvironmentVariable("S2A_SCREENSHOT_DIR");
    if (!folder.isEmpty()) {
        widget->grab().save(QDir(folder).filePath(name + QStringLiteral(".png")));
    }
}

s2a::project::Project example(const char* name) {
    std::ifstream file(std::string(S2A_EXAMPLES_DIR) + "/" + name + "/project.json");
    std::stringstream content;
    content << file.rdbuf();
    auto project = s2a::project::fromJson(content.str());
    if (!project.ok()) {
        qFatal("cannot read example %s", name);
    }
    return std::move(project).value();
}

} // namespace

class CanvasTest : public QObject {
    Q_OBJECT

private slots:
    void dragsABlockFromThePaletteUnderTheHat() {
        Canvas canvas(Language::French);
        canvas.resize(1200, 700);
        canvas.show();
        const std::string hat = canvas.project().scripts.at(0).blocks.at(0).id;
        const QPointF below = canvas.blockPosition(hat) + QPointF(10, 70); // the hat is ~56 high
        const QPointF from = canvas.paletteBlockPosition("pins_builtin_led");
        QVERIFY(from.x() > 0);
        drag(&canvas, from, below);
        QCOMPARE(canvas.project().scripts.size(), std::size_t{1});
        QCOMPARE(canvas.project().scripts.at(0).blocks.size(), std::size_t{2});
        QCOMPARE(canvas.project().scripts.at(0).blocks.at(1).type, std::string("pins_builtin_led"));
        screenshot(&canvas, QStringLiteral("palette-drag"));
    }

    void dropsFarAwayAsANewScriptAndDeletesOnThePalette() {
        Canvas canvas(Language::English);
        canvas.resize(1200, 700);
        canvas.show();
        drag(&canvas, canvas.paletteBlockPosition("control_wait"), QPointF(900, 500));
        QCOMPARE(canvas.project().scripts.size(), std::size_t{2});
        const std::string wait = canvas.project().scripts.at(1).blocks.at(0).id;
        // Dragging it back onto the palette deletes it.
        drag(&canvas, canvas.blockPosition(wait) + QPointF(8, 8), QPointF(150, 300));
        QCOMPARE(canvas.project().scripts.size(), std::size_t{1});
    }

    void movesAStackWithTheBlocksBelow() {
        Canvas canvas(Language::English);
        canvas.resize(1200, 700);
        canvas.show();
        canvas.setProject(example("blink"));
        drag(&canvas, canvas.blockPosition("forever") + QPointF(8, 8), QPointF(900, 450));
        QCOMPARE(canvas.project().scripts.size(), std::size_t{2});
        QCOMPARE(canvas.project().scripts.at(0).blocks.size(), std::size_t{1}); // the hat stays
        QCOMPARE(canvas.project().scripts.at(1).blocks.at(0).statements.at("SUBSTACK").size(), std::size_t{4});
    }

    void showsEveryBlockOfTheExample() {
        s2a::native::MainWindow window(Language::French);
        window.resize(1400, 860);
        window.show();
        auto* canvas = window.findChild<Canvas*>();
        QVERIFY(canvas != nullptr);
        canvas->setProject(example("all-blocks"));
        QCOMPARE(canvas->project().scripts.size(), std::size_t{2});
        screenshot(&window, QStringLiteral("all-blocks"));
    }
};

QTEST_MAIN(CanvasTest)
#include "test_canvas.moc"
