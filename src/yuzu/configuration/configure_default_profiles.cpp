// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>

#include "yuzu/configuration/configure_default_profiles.h"
#include "yuzu/configuration/input_profiles.h"

ConfigureDefaultProfiles::ConfigureDefaultProfiles(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Default Controller Profiles"));
    setMinimumWidth(460);

    auto* layout = new QVBoxLayout(this);

    auto* note = new QLabel(
        tr("Each game starts with these profiles. To give a game its own, right-click it in the "
           "game list and choose Controller Profile (or Configure Game > Input Profiles).<br><br>"
           "Profiles are made in Emulation > Configure > Controls: set up a player's buttons, "
           "then press New under Profiles.<br>Edits to a profile reach every game that uses "
           "it."),
        this);
    note->setWordWrap(true);
    layout->addWidget(note);

    InputProfiles profiles;
    const auto names = profiles.GetInputProfileNames();
    const auto current = InputProfiles::GetDefaultProfiles();

    auto* form = new QFormLayout();
    for (std::size_t i = 0; i < comboboxes.size(); ++i) {
        auto* combobox = new QComboBox(this);
        combobox->addItem(tr("None (use global input configuration)"), QString{});
        for (const auto& name : names) {
            combobox->addItem(QString::fromStdString(name), QString::fromStdString(name));
        }
        if (!current[i].empty()) {
            const int found = combobox->findData(QString::fromStdString(current[i]));
            if (found >= 0) {
                combobox->setCurrentIndex(found);
            } else {
                // A default whose file is gone: shown so it isn't dropped silently
                combobox->addItem(tr("%1 (missing)").arg(QString::fromStdString(current[i])),
                                  QString::fromStdString(current[i]));
                combobox->setCurrentIndex(combobox->count() - 1);
            }
        }
        comboboxes[i] = combobox;
        form->addRow(tr("Player %1").arg(i + 1), combobox);
    }
    layout->addLayout(form);

    if (names.empty()) {
        auto* empty = new QLabel(tr("<i>No profiles yet.</i>"), this);
        layout->addWidget(empty);
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

ConfigureDefaultProfiles::~ConfigureDefaultProfiles() = default;

void ConfigureDefaultProfiles::ApplyConfiguration() {
    std::array<std::string, 8> names{};
    for (std::size_t i = 0; i < comboboxes.size(); ++i) {
        names[i] = comboboxes[i]->currentData().toString().toStdString();
    }
    InputProfiles::SetDefaultProfiles(names);
}
