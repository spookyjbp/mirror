// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <mutex>
#include <vector>

#include <QDialog>
#include <QTimer>

#include "common/common_types.h"

class QLabel;
class QPushButton;
class QWidget;

namespace Core::HID {
class EmulatedController;
class HIDCore;
enum class ControllerTriggerType;
} // namespace Core::HID

/// Watches the controllers' Home button. A press and release of Home on its own emits
/// HomePressed; Home held together with another button (a Home+X hotkey) does not.
class HomeButtonWatcher : public QObject {
    Q_OBJECT

public:
    explicit HomeButtonWatcher(Core::HID::HIDCore& hid_core, QObject* parent = nullptr);
    ~HomeButtonWatcher() override;

signals:
    void HomePressed();

private:
    struct Watched {
        Core::HID::EmulatedController* controller = nullptr;
        int callback_key = 0;
        bool held = false;
        bool combo = false;
    };

    void OnControllerUpdate(std::size_t index, Core::HID::ControllerTriggerType type);

    std::vector<Watched> watched;
    std::mutex mutex;
};

/// The in-game menu the Home button opens: Resume, Game Settings, Settings and Exit Game.
/// Driven by any controller (D-pad or left stick, A to pick, B or Home to go back), the keyboard
/// or the mouse.
class HomeMenu : public QDialog {
    Q_OBJECT

public:
    enum class Choice {
        Resume,
        GameSettings,
        Settings,
        ExitGame,
    };

    explicit HomeMenu(QWidget* parent, Core::HID::HIDCore& hid_core);
    ~HomeMenu() override;

    /// Shows the menu over a window, for a game
    void Open(QWidget* over, const QString& game_title, bool exit_locked);

    /// The Home button while the menu is open: back, or out of the menu
    void HomeButton();

    /// Whether any controller button is held (to resume only once A is let go)
    [[nodiscard]] bool AnyButtonHeld() const;

signals:
    void Chosen(HomeMenu::Choice choice);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void reject() override;
    void paintEvent(QPaintEvent* event) override;

private:
    void ShowPage(bool confirm_exit);
    void MoveFocus(int step);
    void Activate();
    void Back();
    void PollControllers();
    void Fit();
    [[nodiscard]] std::vector<QPushButton*> Buttons() const;

    std::array<Core::HID::EmulatedController*, 9> controllers{};
    QTimer poll_timer;
    u64 previous_buttons = 0;
    u64 repeat_button = 0;
    int repeat_ticks = 0;

    QWidget* card = nullptr;
    QLabel* title = nullptr;
    QLabel* subtitle = nullptr;
    QLabel* hint = nullptr;
    QPushButton* resume_button = nullptr;
    QPushButton* game_settings_button = nullptr;
    QPushButton* settings_button = nullptr;
    QPushButton* exit_button = nullptr;
    QPushButton* confirm_exit_button = nullptr;
    QPushButton* cancel_exit_button = nullptr;

    QWidget* covered = nullptr;
    QString game_name;
    bool locked = false;
    bool confirming = false;
};
