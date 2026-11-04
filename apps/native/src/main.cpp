#include "main_window.hpp"

#include "s2a/i18n.hpp"
#include "s2a/version.hpp"

#include <QApplication>
#include <QLocale>
#include <QStringList>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QString::fromUtf8(s2a::version::kAppName));
    QApplication::setApplicationVersion(QString::fromUtf8(s2a::version::kString));

    const s2a::Language language =
        s2a::languageFromLocale(QLocale::system().name().toStdString());

    s2a::native::MainWindow window(language);
    // A project given on the command line (double-click on a .s2a file).
    if (const QStringList args = QApplication::arguments(); args.size() > 1) {
        (void)window.openFile(args.at(1));
    }
    window.show();
    return QApplication::exec();
}
