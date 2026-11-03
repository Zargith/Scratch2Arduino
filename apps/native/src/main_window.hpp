#pragma once

#include "s2a/i18n.hpp"

#include <QMainWindow>

namespace s2a::native {

/// Phase 0 welcome window; the block canvas replaces it in Phase 3b.
class MainWindow : public QMainWindow {
public:
    explicit MainWindow(Language language, QWidget* parent = nullptr);
};

} // namespace s2a::native
