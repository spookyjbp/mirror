// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <sstream>

#include <fmt/ranges.h>

#include "common/fs/fs.h"
#include "common/fs/path_util.h"
#include "common/logging.h"
#include "frontend_common/config.h"
#include "yuzu/configuration/input_profiles.h"

namespace FS = Common::FS;

namespace {

std::filesystem::path ProfilePath(std::string_view profile_name) {
    return FS::GetEdenPath(FS::EdenPath::ConfigDir) / "input" / fmt::format("{}.ini", profile_name);
}

bool ProfileExistsInFilesystem(std::string_view profile_name) {
    return FS::Exists(ProfilePath(profile_name));
}

bool IsINI(const std::filesystem::path& filename) {
    return filename.extension() == ".ini";
}

std::filesystem::path GetNameWithoutExtension(std::filesystem::path filename) {
    return filename.replace_extension();
}

} // namespace

InputProfiles::InputProfiles() {
    const auto input_profile_loc = FS::GetEdenPath(FS::EdenPath::ConfigDir) / "input";

    if (!FS::IsDir(input_profile_loc)) {
        return;
    }

    FS::IterateDirEntries(
        input_profile_loc,
        [this](const std::filesystem::path& full_path) {
            const auto filename = full_path.filename();
            const auto name_without_ext =
                Common::FS::PathToUTF8String(GetNameWithoutExtension(filename));

            if (IsINI(filename) && IsProfileNameValid(name_without_ext)) {
                map_profiles.insert_or_assign(
                    name_without_ext,
                    std::make_unique<QtConfig>(name_without_ext, Config::ConfigType::InputProfile));
            }

            return true;
        },
        FS::DirEntryFilter::File);
}

InputProfiles::~InputProfiles() = default;

std::vector<std::string> InputProfiles::GetInputProfileNames() {
    std::vector<std::string> profile_names;
    profile_names.reserve(map_profiles.size());

    auto it = map_profiles.cbegin();
    while (it != map_profiles.cend()) {
        const auto& [profile_name, config] = *it;
        if (!ProfileExistsInFilesystem(profile_name)) {
            it = map_profiles.erase(it);
            continue;
        }

        profile_names.push_back(profile_name);
        ++it;
    }

    std::stable_sort(profile_names.begin(), profile_names.end());

    return profile_names;
}

bool InputProfiles::IsProfileNameValid(std::string_view profile_name) {
    return profile_name.find_first_of("<>:;\"/\\|,.!?*") == std::string::npos;
}

bool InputProfiles::CreateProfile(const std::string& profile_name, std::size_t player_index) {
    if (ProfileExistsInMap(profile_name)) {
        return false;
    }

    map_profiles.insert_or_assign(
        profile_name, std::make_unique<QtConfig>(profile_name, Config::ConfigType::InputProfile));

    return SaveProfile(profile_name, player_index);
}

bool InputProfiles::DeleteProfile(const std::string& profile_name) {
    if (!ProfileExistsInMap(profile_name)) {
        return false;
    }

    if (!ProfileExistsInFilesystem(profile_name) ||
        FS::RemoveFile(map_profiles[profile_name]->GetConfigFilePath())) {
        map_profiles.erase(profile_name);
    }

    const bool deleted =
        !ProfileExistsInMap(profile_name) && !ProfileExistsInFilesystem(profile_name);
    if (deleted) {
        auto defaults = GetDefaultProfiles();
        std::replace(defaults.begin(), defaults.end(), profile_name, std::string{});
        SetDefaultProfiles(defaults);
    }
    return deleted;
}

bool InputProfiles::LoadProfile(const std::string& profile_name, std::size_t player_index) {
    if (!ProfileExistsInMap(profile_name)) {
        return false;
    }

    if (!ProfileExistsInFilesystem(profile_name)) {
        map_profiles.erase(profile_name);
        return false;
    }

    LOG_INFO(Config, "Loading input profile `{}`", profile_name);

    map_profiles[profile_name]->ReadQtControlPlayerValues(player_index);
    return true;
}

bool InputProfiles::SaveProfile(const std::string& profile_name, std::size_t player_index) {
    if (!ProfileExistsInMap(profile_name)) {
        return false;
    }

    map_profiles[profile_name]->SaveQtControlPlayerValues(player_index);
    return true;
}

bool InputProfiles::RenameProfile(const std::string& profile_name, const std::string& new_name) {
    if (!ProfileExistsInMap(profile_name) || ProfileExistsInMap(new_name) ||
        ProfileExistsInFilesystem(new_name) ||
        !FS::RenameFile(ProfilePath(profile_name), ProfilePath(new_name))) {
        return false;
    }
    map_profiles.erase(profile_name);
    map_profiles.insert_or_assign(
        new_name, std::make_unique<QtConfig>(new_name, Config::ConfigType::InputProfile));
    auto defaults = GetDefaultProfiles();
    std::replace(defaults.begin(), defaults.end(), profile_name, new_name);
    SetDefaultProfiles(defaults);
    return true;
}

bool InputProfiles::DuplicateProfile(const std::string& profile_name, const std::string& new_name) {
    if (!ProfileExistsInMap(profile_name) || ProfileExistsInMap(new_name) ||
        ProfileExistsInFilesystem(new_name)) {
        return false;
    }
    std::error_code error;
    std::filesystem::copy_file(ProfilePath(profile_name), ProfilePath(new_name), error);
    if (error) {
        return false;
    }
    map_profiles.insert_or_assign(
        new_name, std::make_unique<QtConfig>(new_name, Config::ConfigType::InputProfile));
    return true;
}

std::optional<Settings::PlayerInput> InputProfiles::PeekProfile(const std::string& profile_name,
                                                                std::size_t player_index) {
    if (!ProfileExistsInMap(profile_name)) {
        return std::nullopt;
    }
    auto& player = Settings::values.players.GetValue()[player_index];
    const auto kept = player;
    map_profiles[profile_name]->ReadQtControlPlayerValues(player_index);
    auto read = player;
    player = kept;
    return read;
}

std::array<std::string, 8> InputProfiles::GetDefaultProfiles() {
    std::array<std::string, 8> names{};
    std::stringstream entries{Settings::values.default_input_profiles.GetValue()};
    std::size_t index = 0;
    for (std::string entry; index < names.size() && std::getline(entries, entry, ','); ++index) {
        names[index] = entry;
    }
    return names;
}

void InputProfiles::SetDefaultProfiles(const std::array<std::string, 8>& names) {
    std::string joined;
    for (std::size_t i = 0; i < names.size(); ++i) {
        joined += (i ? "," : "") + names[i];
    }
    // (Nothing stored when no player has one)
    if (joined.find_first_not_of(',') == std::string::npos) {
        joined.clear();
    }
    Settings::values.default_input_profiles.SetValue(joined);
}

bool InputProfiles::ApplyGameProfiles(bool game_config_loaded) {
    static constexpr std::size_t HANDHELD_INDEX = 8;
    auto& players = Settings::values.players;
    const auto defaults = GetDefaultProfiles();
    const bool any_default = std::any_of(defaults.begin(), defaults.end(),
                                         [](const std::string& name) { return !name.empty(); });
    if (!game_config_loaded) {
        if (!any_default) {
            players.SetGlobal(true);
            return false;
        }
        // The defaults go on a copy of the user's players, for this game only
        players.SetGlobal(false);
        players.GetValue() = players.GetValue(true);
        for (auto& player : players.GetValue()) {
            player.profile_name.clear();
        }
    }
    if (players.UsingGlobal()) {
        return false;
    }

    auto& game = players.GetValue();
    const auto& global = players.GetValue(true);
    std::optional<InputProfiles> profiles;
    bool applied = false;
    for (std::size_t i = 0; i < defaults.size(); ++i) {
        auto& player = game[i];
        const bool own = !player.profile_name.empty();
        if (!own) {
            // No profile of the game's own: the user's player, then its default if any
            player = global[i];
            player.profile_name.clear();
            if (i == 0) {
                game[HANDHELD_INDEX] = global[HANDHELD_INDEX];
                game[HANDHELD_INDEX].profile_name.clear();
            }
        }
        const std::string name = own ? player.profile_name : defaults[i];
        if (name.empty()) {
            continue;
        }
        if (!profiles) {
            profiles.emplace();
        }
        const auto read = profiles->PeekProfile(name, i);
        if (!read) {
            if (own) {
                LOG_WARNING(Config,
                            "Player {} input profile `{}` is gone: using the copy saved with "
                            "the game",
                            i + 1, name);
            } else {
                LOG_WARNING(Config, "Player {} default input profile `{}` is gone", i + 1, name);
            }
            continue;
        }
        player.controller_type = read->controller_type;
        player.buttons = read->buttons;
        player.analogs = read->analogs;
        player.motions = read->motions;
        player.profile_name = name;
        applied = true;
        LOG_INFO(Config, "Player {} uses {} input profile `{}`", i + 1,
                 own ? "the game's" : "the default", name);

        if (i == 0 && player.controller_type == Settings::ControllerType::Handheld) {
            game[HANDHELD_INDEX] = player;
        }
    }
    return applied;
}

bool InputProfiles::ProfileExistsInMap(const std::string& profile_name) const {
    return map_profiles.find(profile_name) != map_profiles.end();
}
