#include "main_window.hpp"

#include "canvas.hpp"

#include "s2a/arduino_cli.hpp"
#include "s2a/boards.hpp"
#include "s2a/build.hpp"
#include "s2a/codegen.hpp"
#include "s2a/project.hpp"
#include "s2a/version.hpp"

#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStandardPaths>
#include <QStatusBar>
#include <QThread>
#include <QToolBar>
#include <QVBoxLayout>
#include <memory>
#include <vector>

namespace s2a::native {

namespace {

QString qt(const std::string& text) {
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

std::string utf8(const QString& text) {
    return text.toUtf8().toStdString();
}

std::filesystem::path pathOf(const QString& file) {
    return std::filesystem::u8path(utf8(file));
}

/// Runs `work` on a thread, then `done` on the GUI thread with its result.
template <typename Result, typename Work, typename Done>
void inBackground(QObject* owner, Work work, Done done) {
    auto result = std::make_shared<Result>();
    QThread* thread = QThread::create([result, task = std::move(work)]() { *result = task(); });
    QObject::connect(thread, &QThread::finished, owner, [thread, result, callback = std::move(done)]() {
        callback(*result);
        thread->deleteLater();
    });
    thread->start();
}

struct PortList {
    std::vector<toolchain::DetectedPort> ports;
    std::string error;
};

} // namespace

MainWindow::MainWindow(Language language, QWidget* parent) : QMainWindow(parent), language_(language) {
    resize(1280, 800);
    buildUi();
    retranslate();
    updateTitle();
    refreshPorts();
}

QString MainWindow::text(const char* key) const {
    return qt(s2a::tr(key, language_));
}

void MainWindow::buildUi() {
    canvas_ = new Canvas(language_, this);
    setCentralWidget(canvas_);
    connect(canvas_, &Canvas::changed, this, [this]() {
        dirty_ = true;
        updateTitle();
    });
    connect(canvas_, &Canvas::variableRequested, this, [this]() {
        bool ok = false;
        const QString name = QInputDialog::getText(this, text("editor.make_variable"), text("editor.variable_prompt"),
                                                   QLineEdit::Normal, {}, &ok);
        if (ok) {
            canvas_->addVariable(utf8(name.trimmed()));
        }
    });

    auto* bar = addToolBar(QStringLiteral("main"));
    bar->setMovable(false);
    bar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    newAction_ = bar->addAction(QString(), this, &MainWindow::newProject);
    openAction_ = bar->addAction(QString(), this, &MainWindow::openProject);
    saveAction_ = bar->addAction(QString(), this, [this]() { (void)saveProject(false); });
    newAction_->setShortcut(QKeySequence::New);
    openAction_->setShortcut(QKeySequence::Open);
    saveAction_->setShortcut(QKeySequence::Save);
    bar->addSeparator();

    name_ = new QLineEdit(bar);
    name_->setMaximumWidth(200);
    bar->addWidget(name_);
    connect(name_, &QLineEdit::textEdited, this, [this]() {
        dirty_ = true;
        updateTitle();
    });

    board_ = new QComboBox(bar);
    for (const Board& board : BoardDatabase::builtin().boards()) {
        board_->addItem(qt(board.name), qt(board.id));
    }
    board_->setCurrentIndex(board_->findData(qt(std::string(BoardDatabase::kDefaultBoardId))));
    bar->addWidget(board_);
    connect(board_, &QComboBox::activated, this, [this]() {
        canvas_->setBoard(utf8(board_->currentData().toString()));
        dirty_ = true;
        updateTitle();
    });

    port_ = new QComboBox(bar);
    port_->setMinimumWidth(180);
    bar->addWidget(port_);
    refreshAction_ = bar->addAction(QStringLiteral("⟳"), this, &MainWindow::refreshPorts);
    bar->addSeparator();

    bar->addAction(QStringLiteral("−"), this, [this]() { canvas_->zoomBy(1 / 1.2); });
    bar->addAction(QStringLiteral("="), this, [this]() { canvas_->resetZoom(); });
    bar->addAction(QStringLiteral("+"), this, [this]() { canvas_->zoomBy(1.2); });

    auto* spacer = new QWidget(bar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    bar->addWidget(spacer);
    codeAction_ = bar->addAction(QString(), this, &MainWindow::showCode);
    checkAction_ = bar->addAction(QString(), this, [this]() { build(false); });
    uploadAction_ = bar->addAction(QString(), this, [this]() { build(true); });
    languageBox_ = new QComboBox(bar);
    languageBox_->addItem(QStringLiteral("FR"), static_cast<int>(Language::French));
    languageBox_->addItem(QStringLiteral("EN"), static_cast<int>(Language::English));
    languageBox_->setCurrentIndex(language_ == Language::English ? 1 : 0);
    bar->addWidget(languageBox_);
    connect(languageBox_, &QComboBox::activated, this, [this]() {
        language_ = static_cast<Language>(languageBox_->currentData().toInt());
        canvas_->setLanguage(language_);
        retranslate();
    });

    status_ = new QLabel(this);
    status_->setWordWrap(true);
    details_ = new QPushButton(this);
    details_->setFlat(true);
    details_->hide();
    connect(details_, &QPushButton::clicked, this, [this]() {
        QMessageBox box(this);
        box.setWindowTitle(text("editor.details"));
        box.setText(status_->text());
        box.setDetailedText(statusDetails_);
        box.exec();
    });
    statusBar()->addWidget(status_, 1);
    statusBar()->addPermanentWidget(details_);

    name_->setText(qt(canvas_->project().name));
}

void MainWindow::retranslate() {
    newAction_->setText(text("editor.new"));
    openAction_->setText(text("editor.open"));
    saveAction_->setText(text("editor.save"));
    codeAction_->setText(text("editor.code"));
    checkAction_->setText(text("editor.check"));
    uploadAction_->setText(text("editor.upload"));
    refreshAction_->setToolTip(text("editor.refresh"));
    name_->setPlaceholderText(text("editor.project_name"));
    details_->setText(text("editor.details"));
    port_->setPlaceholderText(text("editor.no_ports"));
    updateTitle();
}

void MainWindow::updateTitle() {
    const QString file = path_.isEmpty() ? name_->text() : QFileInfo(path_).fileName();
    setWindowTitle((dirty_ ? QStringLiteral("• ") : QString()) + file + QStringLiteral(" — ") + text("app.title") +
                   QStringLiteral(" (") + text("app.variant.native") + QStringLiteral(")"));
}

void MainWindow::setStatus(const QString& message, const QString& details, bool error) {
    status_->setText(message);
    status_->setStyleSheet(error ? QStringLiteral("color: #c2410c; font-weight: 600;") : QString());
    statusDetails_ = details;
    details_->setVisible(!details.isEmpty());
}

void MainWindow::refreshPorts() {
    inBackground<PortList>(
        this,
        []() {
            PortList list;
            const auto executable = toolchain::locateArduinoCli();
            if (executable.empty()) {
                list.error = "cli.arduino_cli_missing";
                return list;
            }
            const toolchain::ArduinoCli cli(executable, toolchain::Directories::defaults());
            if (auto ports = cli.detectPorts(); ports.ok()) {
                list.ports = std::move(ports).value();
            }
            return list;
        },
        [this](const PortList& list) {
            const QString current = port_->currentData().toString();
            port_->clear();
            for (const auto& port : list.ports) {
                if (port.protocol != "serial") {
                    continue;
                }
                const Board* board = BoardDatabase::builtin().find(port.boardId);
                port_->addItem(board != nullptr ? qt(port.label + " — " + board->name) : qt(port.label), qt(port.address));
                if (board != nullptr && port_->count() == 1) {
                    board_->setCurrentIndex(board_->findData(qt(board->id)));
                    canvas_->setBoard(board->id);
                }
            }
            if (const int index = port_->findData(current); index >= 0) {
                port_->setCurrentIndex(index);
            }
            if (!list.error.empty()) {
                setStatus(qt(s2a::tr(list.error, language_)), {}, true);
            }
        });
}

bool MainWindow::confirmDiscard() {
    if (!dirty_) {
        return true;
    }
    const auto answer = QMessageBox::question(this, text("editor.unsaved_title"), text("editor.unsaved_text"),
                                              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (answer == QMessageBox::Save) {
        return saveProject(false);
    }
    return answer == QMessageBox::Discard;
}

void MainWindow::newProject() {
    if (!confirmDiscard()) {
        return;
    }
    project::Project project = project::newProject(language_);
    project.boardId = utf8(board_->currentData().toString());
    canvas_->setProject(std::move(project));
    name_->setText(qt(canvas_->project().name));
    path_.clear();
    dirty_ = false;
    setStatus({});
    updateTitle();
}

void MainWindow::openProject() {
    if (!confirmDiscard()) {
        return;
    }
    const QString file = QFileDialog::getOpenFileName(this, text("editor.open"), QDir::homePath(),
                                                      text("editor.file_filter") + QStringLiteral(" (*.s2a)"));
    if (!file.isEmpty()) {
        (void)openFile(file);
    }
}

bool MainWindow::openFile(const QString& path) {
    auto loaded = project::load(pathOf(path));
    if (!loaded.ok()) {
        setStatus(qt(s2a::tr(errorMessageKey(loaded.error().code), language_)), qt(loaded.error().detail), true);
        return false;
    }
    project::Project project = std::move(loaded).value();
    if (const int index = board_->findData(qt(project.boardId)); index >= 0) {
        board_->setCurrentIndex(index);
    }
    name_->setText(qt(project.name));
    canvas_->setProject(std::move(project));
    path_ = path;
    dirty_ = false;
    setStatus(text("editor.opened").arg(QDir::toNativeSeparators(path)));
    updateTitle();
    return true;
}

bool MainWindow::saveProject(bool askPath) {
    QString file = path_;
    if (askPath || file.isEmpty()) {
        const QString folder = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        const QString base = name_->text().isEmpty() ? QStringLiteral("projet") : name_->text();
        file = QFileDialog::getSaveFileName(this, text("editor.save"), QDir(folder).filePath(base + QStringLiteral(".s2a")),
                                            text("editor.file_filter") + QStringLiteral(" (*.s2a)"));
        if (file.isEmpty()) {
            return false;
        }
        if (!file.endsWith(QStringLiteral(".s2a"))) {
            file += QStringLiteral(".s2a");
        }
    }
    project::Project project = canvas_->project();
    project.name = utf8(name_->text());
    if (const auto saved = project::save(project, pathOf(file)); !saved.ok()) {
        setStatus(qt(s2a::tr(errorMessageKey(saved.error().code), language_)), qt(saved.error().detail), true);
        return false;
    }
    path_ = file;
    dirty_ = false;
    setStatus(text("editor.saved").arg(QDir::toNativeSeparators(file)));
    updateTitle();
    return true;
}

void MainWindow::showCode() {
    project::Project project = canvas_->project();
    project.name = utf8(name_->text());
    QDialog dialog(this);
    dialog.setWindowTitle(text("editor.code"));
    dialog.resize(720, 560);
    auto* layout = new QVBoxLayout(&dialog);
    auto* note = new QLabel(text("editor.code_note"), &dialog);
    note->setWordWrap(true);
    layout->addWidget(note);
    auto* code = new QPlainTextEdit(qt(codegen::generate(project).source), &dialog);
    code->setReadOnly(true);
    code->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    layout->addWidget(code);
    dialog.exec();
}

void MainWindow::build(bool upload) {
    if (busy_) {
        return;
    }
    const std::string port = utf8(port_->currentData().toString());
    if (upload && port.empty()) {
        setStatus(text("editor.no_port"), {}, true);
        return;
    }
    project::Project project = canvas_->project();
    project.name = utf8(name_->text());
    busy_ = true;
    checkAction_->setEnabled(false);
    uploadAction_->setEnabled(false);
    setStatus(text(upload ? "editor.uploading" : "editor.building"));
    const Language language = language_;
    inBackground<toolchain::BuildResult>(
        this,
        [project = std::move(project), target = upload ? port : std::string(), language]() {
            const auto executable = toolchain::locateArduinoCli();
            if (executable.empty()) {
                toolchain::BuildResult result;
                result.message = s2a::tr("cli.arduino_cli_missing", language);
                return result;
            }
            const toolchain::ArduinoCli cli(executable, toolchain::Directories::defaults());
            toolchain::BuildOptions options;
            options.port = target;
            return toolchain::buildProject(cli, project, language, options);
        },
        [this](const toolchain::BuildResult& result) {
            busy_ = false;
            checkAction_->setEnabled(true);
            uploadAction_->setEnabled(true);
            QString message = qt(result.message);
            if (result.ok && !result.warnings.empty()) {
                message += QStringLiteral("  ⚠ ") + qt(result.warnings.front());
            }
            setStatus(message, qt(result.details), !result.ok);
            canvas_->highlight(result.ok ? result.warningBlockIds : result.blockIds);
        });
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (busy_ || !confirmDiscard()) {
        event->ignore();
        return;
    }
    event->accept();
}

} // namespace s2a::native
