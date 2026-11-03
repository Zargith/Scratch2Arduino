#include "main_window.hpp"

#include "s2a/version.hpp"

#include <QLabel>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>
#include <string_view>

namespace s2a::native {

namespace {

QString toQString(std::string_view text) {
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

QLabel* makeLabel(const QString& text, const QString& style, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setAlignment(Qt::AlignCenter);
    label->setWordWrap(true);
    label->setStyleSheet(style);
    return label;
}

} // namespace

MainWindow::MainWindow(Language language, QWidget* parent) : QMainWindow(parent) {
    const QString appTitle = toQString(translate(Text::AppTitle, language));
    const QString variant = toQString(translate(Text::VariantNative, language));
    setWindowTitle(appTitle + QStringLiteral(" — ") + variant);
    resize(960, 640);

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->addStretch();
    layout->addWidget(makeLabel(
        appTitle, QStringLiteral("color: #00878f; font-size: 36px; font-weight: bold;"), central));
    layout->addWidget(makeLabel(toQString(translate(Text::Welcome, language)),
                                QStringLiteral("font-size: 17px;"), central));
    layout->addWidget(makeLabel(toQString(translate(Text::ComingSoon, language)),
                                QStringLiteral("font-size: 17px;"), central));

    auto* badge = makeLabel(variant + QStringLiteral(" · v") + QString::fromUtf8(version::kString),
                            QStringLiteral("background: #ff8c1a; color: white; font-weight: bold;"
                                           "border-radius: 12px; padding: 4px 14px;"),
                            central);
    layout->addWidget(badge, 0, Qt::AlignHCenter);
    layout->addStretch();
    layout->setContentsMargins(48, 32, 48, 32);
    layout->setSpacing(16);
    setCentralWidget(central);
}

} // namespace s2a::native
