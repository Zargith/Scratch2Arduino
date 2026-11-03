#include "main_window.hpp"

#include "s2a/i18n.hpp"
#include "s2a/version.hpp"

#include <QApplication>
#include <QLocale>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QString::fromUtf8(s2a::version::kAppName));
    QApplication::setApplicationVersion(QString::fromUtf8(s2a::version::kString));

    const s2a::Language language =
        s2a::languageFromLocale(QLocale::system().name().toStdString());

    s2a::native::MainWindow window(language);
    window.show();
    return QApplication::exec();
}
