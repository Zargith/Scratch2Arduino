#pragma once

#include "s2a/i18n.hpp"

#include <QMainWindow>
#include <QString>

class QAction;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace s2a::native {

class Canvas;

/// Variant B window: block canvas, project files, board and port, Check and Upload.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(Language language, QWidget* parent = nullptr);

    /// Opens a .s2a file (also used for a file given on the command line).
    bool openFile(const QString& path);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    [[nodiscard]] QString text(const char* key) const;
    void buildUi();
    void retranslate();
    void refreshPorts();
    void updateTitle();
    void setStatus(const QString& message, const QString& details = {}, bool error = false);
    [[nodiscard]] bool confirmDiscard();

    void newProject();
    void openProject();
    bool saveProject(bool askPath);
    void showCode();
    void build(bool upload);

    Language language_;
    Canvas* canvas_ = nullptr;
    QLineEdit* name_ = nullptr;
    QComboBox* board_ = nullptr;
    QComboBox* port_ = nullptr;
    QComboBox* languageBox_ = nullptr;
    QLabel* status_ = nullptr;
    QPushButton* details_ = nullptr;
    QAction* newAction_ = nullptr;
    QAction* openAction_ = nullptr;
    QAction* saveAction_ = nullptr;
    QAction* codeAction_ = nullptr;
    QAction* checkAction_ = nullptr;
    QAction* uploadAction_ = nullptr;
    QAction* refreshAction_ = nullptr;
    QString path_;
    QString statusDetails_;
    bool dirty_ = false;
    bool busy_ = false;
};

} // namespace s2a::native
