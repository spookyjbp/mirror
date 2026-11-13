// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>

#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

#include "hid_core/frontend/emulated_controller.h"
#include "hid_core/hid_core.h"
#include "hid_core/hid_types.h"
#include "yuzu/home_menu.h"
#include "yuzu/util/gamescope.h"

namespace {

/// The buttons that make Home a hotkey combination rather than a press of its own: the face
/// buttons, sticks' clicks, shoulders, +/- and the D-pad (not stick tilts)
constexpr u64 COMBO_BUTTONS = 0xFFFF;

constexpr u64 Bit(Core::HID::NpadButton button) {
    return static_cast<u64>(button);
}

constexpr u64 UP = Bit(Core::HID::NpadButton::Up) | Bit(Core::HID::NpadButton::StickLUp);
constexpr u64 DOWN = Bit(Core::HID::NpadButton::Down) | Bit(Core::HID::NpadButton::StickLDown);
constexpr u64 ACCEPT = Bit(Core::HID::NpadButton::A);
constexpr u64 BACK = Bit(Core::HID::NpadButton::B);

/// Poll every 16 ms; a held direction repeats after 400 ms, then every 120 ms
constexpr int POLL_MS = 16;
constexpr int REPEAT_DELAY_TICKS = 25;
constexpr int REPEAT_RATE_TICKS = 8;

constexpr std::size_t WATCHED_CONTROLLERS = 9; // Players 1-8 and handheld

} // Anonymous namespace

HomeButtonWatcher::HomeButtonWatcher(Core::HID::HIDCore& hid_core, QObject* parent)
    : QObject(parent) {
    watched.resize(WATCHED_CONTROLLERS);
    for (std::size_t i = 0; i < WATCHED_CONTROLLERS; ++i) {
        auto& entry = watched[i];
        entry.controller = hid_core.GetEmulatedControllerByIndex(i);
        if (entry.controller == nullptr) {
            continue;
        }
        Core::HID::ControllerUpdateCallback callback{
            .on_change = [this, i](Core::HID::ControllerTriggerType type) {
                OnControllerUpdate(i, type);
            },
            .is_npad_service = false,
        };
        entry.callback_key = entry.controller->SetCallback(callback);
    }
}

HomeButtonWatcher::~HomeButtonWatcher() {
    for (auto& entry : watched) {
        if (entry.controller != nullptr) {
            entry.controller->DeleteCallback(entry.callback_key);
        }
    }
}

void HomeButtonWatcher::OnControllerUpdate(std::size_t index,
                                           Core::HID::ControllerTriggerType type) {
    if (type != Core::HID::ControllerTriggerType::Button) {
        return;
    }
    bool released_alone = false;
    {
        std::scoped_lock lock{mutex};
        auto& entry = watched[index];
        const bool home = entry.controller->GetHomeButtons().home != 0;
        const bool others = (static_cast<u64>(entry.controller->GetNpadButtons().raw) & COMBO_BUTTONS) != 0 ||
                            entry.controller->GetCaptureButtons().capture != 0;
        if (home && !entry.held) {
            entry.held = true;
            entry.combo = others;
        } else if (home) {
            entry.combo = entry.combo || others;
        } else if (entry.held) {
            entry.held = false;
            released_alone = !entry.combo;
        }
    }
    if (released_alone) {
        // (From the input thread: queued to the receivers in the GUI thread)
        emit HomePressed();
    }
}

HomeMenu::HomeMenu(QWidget* parent, Core::HID::HIDCore& hid_core) : QDialog(parent) {
    for (std::size_t i = 0; i < controllers.size(); ++i) {
        controllers[i] = hid_core.GetEmulatedControllerByIndex(i);
    }

    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::CustomizeWindowHint |
                   (Gamescope::IsRunningUnder() ? Qt::X11BypassWindowManagerHint
                                                : Qt::WindowFlags{}));
    setWindowModality(Qt::WindowModal);
    setAttribute(Qt::WA_TranslucentBackground);
    setObjectName(QStringLiteral("HomeMenu"));

    card = new QWidget(this);
    card->setObjectName(QStringLiteral("HomeMenuCard"));
    card->setAttribute(Qt::WA_StyledBackground);

    title = new QLabel(card);
    title->setObjectName(QStringLiteral("HomeMenuTitle"));
    title->setAlignment(Qt::AlignCenter);
    title->setWordWrap(true);
    subtitle = new QLabel(card);
    subtitle->setObjectName(QStringLiteral("HomeMenuSubtitle"));
    subtitle->setAlignment(Qt::AlignCenter);
    subtitle->setWordWrap(true);

    const auto make_button = [this](const QString& text) {
        auto* button = new QPushButton(text, card);
        button->setFocusPolicy(Qt::StrongFocus);
        button->setCursor(Qt::PointingHandCursor);
        return button;
    };
    resume_button = make_button(tr("Resume"));
    game_settings_button = make_button(tr("Game Settings"));
    settings_button = make_button(tr("Settings"));
    exit_button = make_button(tr("Exit Game"));
    exit_button->setObjectName(QStringLiteral("HomeMenuDanger"));
    confirm_exit_button = make_button(tr("Exit Game"));
    confirm_exit_button->setObjectName(QStringLiteral("HomeMenuDanger"));
    cancel_exit_button = make_button(tr("Cancel"));

    hint = new QLabel(card);
    hint->setObjectName(QStringLiteral("HomeMenuHint"));
    hint->setAlignment(Qt::AlignCenter);

    auto* layout = new QVBoxLayout(card);
    layout->addWidget(title);
    layout->addWidget(subtitle);
    layout->addSpacing(8);
    for (auto* button : {resume_button, game_settings_button, settings_button, exit_button,
                         confirm_exit_button, cancel_exit_button}) {
        layout->addWidget(button);
    }
    layout->addSpacing(4);
    layout->addWidget(hint);

    connect(resume_button, &QPushButton::clicked, this, [this] { emit Chosen(Choice::Resume); });
    connect(game_settings_button, &QPushButton::clicked, this,
            [this] { emit Chosen(Choice::GameSettings); });
    connect(settings_button, &QPushButton::clicked, this,
            [this] { emit Chosen(Choice::Settings); });
    connect(exit_button, &QPushButton::clicked, this, [this] { ShowPage(true); });
    connect(confirm_exit_button, &QPushButton::clicked, this,
            [this] { emit Chosen(Choice::ExitGame); });
    connect(cancel_exit_button, &QPushButton::clicked, this, [this] { ShowPage(false); });

    poll_timer.setInterval(POLL_MS);
    connect(&poll_timer, &QTimer::timeout, this, &HomeMenu::PollControllers);
}

HomeMenu::~HomeMenu() = default;

void HomeMenu::Open(QWidget* over, const QString& game_title, bool exit_locked) {
    covered = over;
    game_name = game_title;
    locked = exit_locked;
    ShowPage(false);
    Fit();
    show();
    raise();
    activateWindow();
    resume_button->setFocus(Qt::OtherFocusReason);
    // Buttons held as the menu opens (the Home press itself) don't count as presses
    previous_buttons = ~u64{0};
    repeat_button = 0;
    poll_timer.start();
}

void HomeMenu::HomeButton() {
    if (confirming) {
        ShowPage(false);
        return;
    }
    emit Chosen(Choice::Resume);
}

bool HomeMenu::AnyButtonHeld() const {
    for (const auto* controller : controllers) {
        if (controller != nullptr && (static_cast<u64>(controller->GetNpadButtons().raw) & COMBO_BUTTONS) != 0) {
            return true;
        }
    }
    return false;
}

void HomeMenu::ShowPage(bool confirm_exit) {
    confirming = confirm_exit;
    resume_button->setVisible(!confirm_exit);
    game_settings_button->setVisible(!confirm_exit);
    settings_button->setVisible(!confirm_exit);
    exit_button->setVisible(!confirm_exit);
    confirm_exit_button->setVisible(confirm_exit);
    cancel_exit_button->setVisible(confirm_exit);

    if (confirm_exit) {
        title->setText(tr("Exit %1?").arg(game_name.isEmpty() ? tr("the game") : game_name));
        subtitle->setText(locked ? tr("The game is saving or busy right now. Exiting may lose "
                                      "or corrupt your save data.")
                                 : tr("Any unsaved progress will be lost."));
        subtitle->setProperty("warning", locked);
        cancel_exit_button->setFocus(Qt::OtherFocusReason);
        hint->setText(tr("Ⓐ Select    Ⓑ Back"));
    } else {
        title->setText(tr("Paused"));
        subtitle->setText(game_name);
        subtitle->setProperty("warning", false);
        if (isVisible()) {
            exit_button->setFocus(Qt::OtherFocusReason);
        }
        hint->setText(tr("Ⓐ Select    Ⓑ / Home Resume"));
    }
    subtitle->setVisible(!subtitle->text().isEmpty());
    // (Re-polish so the warning property picks its color)
    subtitle->style()->unpolish(subtitle);
    subtitle->style()->polish(subtitle);
    if (isVisible()) {
        Fit();
    }
}

std::vector<QPushButton*> HomeMenu::Buttons() const {
    if (confirming) {
        return {confirm_exit_button, cancel_exit_button};
    }
    return {resume_button, game_settings_button, settings_button, exit_button};
}

void HomeMenu::MoveFocus(int step) {
    const auto buttons = Buttons();
    const auto current = std::find_if(buttons.begin(), buttons.end(),
                                      [](const QPushButton* button) { return button->hasFocus(); });
    int index = current == buttons.end() ? 0 : static_cast<int>(current - buttons.begin()) + step;
    index = std::clamp(index, 0, static_cast<int>(buttons.size()) - 1);
    buttons[static_cast<std::size_t>(index)]->setFocus(Qt::OtherFocusReason);
}

void HomeMenu::Activate() {
    for (auto* button : Buttons()) {
        if (button->hasFocus()) {
            button->click();
            return;
        }
    }
}

void HomeMenu::Back() {
    if (confirming) {
        ShowPage(false);
        return;
    }
    emit Chosen(Choice::Resume);
}

void HomeMenu::PollControllers() {
    if (!isVisible() || !isEnabled()) {
        return;
    }
    u64 buttons = 0;
    for (const auto* controller : controllers) {
        if (controller != nullptr) {
            buttons |= static_cast<u64>(controller->GetNpadButtons().raw);
        }
    }
    const u64 pressed = buttons & ~previous_buttons;
    previous_buttons = buttons;

    // Hold a direction to repeat it
    const u64 held_direction = buttons & (UP | DOWN);
    if (held_direction != 0 && held_direction == repeat_button) {
        if (++repeat_ticks >= REPEAT_DELAY_TICKS &&
            (repeat_ticks - REPEAT_DELAY_TICKS) % REPEAT_RATE_TICKS == 0) {
            MoveFocus((held_direction & UP) != 0 ? -1 : 1);
        }
    } else {
        repeat_button = held_direction;
        repeat_ticks = 0;
    }

    if ((pressed & UP) != 0) {
        MoveFocus(-1);
    } else if ((pressed & DOWN) != 0) {
        MoveFocus(1);
    } else if ((pressed & ACCEPT) != 0) {
        Activate();
    } else if ((pressed & BACK) != 0) {
        Back();
    }
}

void HomeMenu::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
    case Qt::Key_Up:
        MoveFocus(-1);
        return;
    case Qt::Key_Down:
        MoveFocus(1);
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
        Activate();
        return;
    default:
        QDialog::keyPressEvent(event);
    }
}

void HomeMenu::reject() {
    // Escape: back, never a plain close (the main window decides what closing does)
    Back();
}

void HomeMenu::paintEvent(QPaintEvent* event) {
    // Dim the game behind the menu
    QPainter painter(this);
    painter.fillRect(rect(), QColor(0, 0, 0, 150));
    QDialog::paintEvent(event);
}

void HomeMenu::Fit() {
    if (covered == nullptr) {
        return;
    }
    const QRect area(covered->mapToGlobal(QPoint(0, 0)), covered->size());
    setGeometry(area);

    // Sized to the window, like the console's own menus
    const int base = std::clamp(area.height() / 34, 14, 30);
    const int card_width = std::clamp(area.width() / 3, base * 16, base * 24);
    const QString style =
        QStringLiteral(
            "#HomeMenuCard { background-color: rgba(28, 28, 34, 235); border-radius: %1px; }"
            "#HomeMenuCard QLabel { color: #f0f0f0; background: transparent; }"
            "#HomeMenuCard #HomeMenuTitle { font-size: %2px; font-weight: bold; }"
            "#HomeMenuCard #HomeMenuSubtitle { font-size: %3px; color: #b8b8c0; }"
            "#HomeMenuCard #HomeMenuSubtitle[warning=\"true\"] { color: #ffb347; }"
            "#HomeMenuCard #HomeMenuHint { font-size: %4px; color: #8c8c96; }"
            "#HomeMenuCard QPushButton { font-size: %3px; color: #f0f0f0; text-align: left;"
            "  padding: %5px %6px; border: 2px solid transparent; border-radius: %7px;"
            "  background-color: rgba(255, 255, 255, 18); }"
            "#HomeMenuCard QPushButton:hover { background-color: rgba(255, 255, 255, 34); }"
            "#HomeMenuCard QPushButton:focus { border-color: #3d8bfd;"
            "  background-color: rgba(61, 139, 253, 60); }"
            "#HomeMenuCard QPushButton#HomeMenuDanger { color: #ff8a8a; }")
            .arg(base)
            .arg(base * 3 / 2)
            .arg(base)
            .arg(std::max(11, base * 3 / 4))
            .arg(base / 2)
            .arg(base)
            .arg(base / 2);
    card->setStyleSheet(style);
    card->layout()->setContentsMargins(base * 3 / 2, base * 3 / 2, base * 3 / 2, base);
    card->layout()->setSpacing(base / 2);
    card->setFixedWidth(card_width);
    card->adjustSize();
    card->move((area.width() - card->width()) / 2, (area.height() - card->height()) / 2);
}
