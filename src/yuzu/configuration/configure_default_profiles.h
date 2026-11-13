// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>

#include <QDialog>

class QComboBox;

/// Picks each player's default input profile: the one every game uses unless the game picks its
/// own in its properties (Input Profiles)
class ConfigureDefaultProfiles : public QDialog {
    Q_OBJECT

public:
    explicit ConfigureDefaultProfiles(QWidget* parent = nullptr);
    ~ConfigureDefaultProfiles() override;

    /// Stores the choices in the settings
    void ApplyConfiguration();

private:
    std::array<QComboBox*, 8> comboboxes{};
};
