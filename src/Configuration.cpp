#include "Configuration.h"

#include "ActorIdentityService.h"
#include "Manager.h"
#include "Prisma.h"
#include "RequirementService.h"
#include "ResetService.h"
#include "RosterService.h"
#include "SKSEMenuFramework.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <unordered_map>

namespace ImGui = ImGuiMCP;
using json = nlohmann::json;

namespace {
    const std::filesystem::path kLocalizationDirectory = "Data/Viny Mods/NSM/Localization";
    std::unordered_map<std::string, std::string> language;
    RE::FormID maintenanceActorID = player_refid;

    std::string ToLower(std::string value) {
        std::ranges::transform(value, value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }

    void FlattenLanguage(const json& value, const std::string& prefix = {}) {
        if (!value.is_object()) return;
        for (const auto& [key, child] : value.items()) {
            const auto fullKey = prefix.empty() ? key : prefix + "." + key;
            if (child.is_string()) language[fullKey] = child.get<std::string>();
            else FlattenLanguage(child, fullKey);
        }
    }

    RE::FormID ParseFormID(const std::string& value) {
        if (value.empty()) return 0;
        if (const auto separator = value.find('|'); separator != std::string::npos) {
            try {
                const auto localID = static_cast<RE::FormID>(std::stoul(value.substr(separator + 1), nullptr, 16));
                const auto data = RE::TESDataHandler::GetSingleton();
                return data ? data->LookupFormID(localID, value.substr(0, separator)) : 0;
            } catch (...) { return 0; }
        }
        try { return static_cast<RE::FormID>(std::stoul(value, nullptr, 16)); }
        catch (...) { return 0; }
    }

    std::string NormalizeFormID(RE::FormID formID) {
        const auto form = RE::TESForm::LookupByID(formID);
        if (!form) return {};
        const auto file = form->GetFile(0);
        if (!file) return std::format("{:X}", formID);
        const auto localID = (formID & 0xFF000000) == 0xFE000000 ? formID & 0xFFF : formID & 0xFFFFFF;
        return std::format("{}|{:X}", file->GetFilename(), localID);
    }

    bool DrawDropdown(const char* label, const std::string& category, RE::FormID& currentFormID, float customWidth = -1.0f) {
        bool changed = false;
        const auto& fullList = Manager::GetSingleton()->GetList(category);
        if (fullList.empty()) return false;

        std::vector<const char*> comboItems;
        std::vector<int> mapToFull;
        comboItems.push_back(ModMenu::GetLoc("menu.opt_none", "None"));
        mapToFull.push_back(-1);

        int localSelection = 0;
        for (std::size_t i = 0; i < fullList.size(); ++i) {
            comboItems.push_back(fullList[i].cachedDisplayName.c_str());
            mapToFull.push_back(static_cast<int>(i));
            if (fullList[i].formID == currentFormID) localSelection = static_cast<int>(i) + 1;
        }

        ImGui::PushID(label);
        std::string displayLabel = label;
        if (const auto hashPos = displayLabel.find("##"); hashPos != std::string::npos) displayLabel.resize(hashPos);
        ImGui::Text("%s:", displayLabel.c_str());
        ImGui::SameLine();
        if (customWidth > 0.0f) ImGui::SetNextItemWidth(customWidth);

        if (ImGui::BeginCombo("##drop", comboItems[localSelection])) {
            static std::map<std::string, std::string> searchBuffers;
            char searchBuf[256]{};
            if (searchBuffers.contains(label)) strcpy_s(searchBuf, searchBuffers[label].c_str());
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputText("##busca", searchBuf, sizeof(searchBuf))) searchBuffers[label] = searchBuf;
            ImGui::Separator();

            const auto search = ToLower(searchBuf);
            ImGui::BeginChild("##scroll", ImGui::ImVec2(0, 200), false);
            for (int i = 0; i < static_cast<int>(comboItems.size()); ++i) {
                if (!search.empty() && ToLower(comboItems[i]).find(search) == std::string::npos) continue;
                const bool selected = localSelection == i;
                if (ImGui::Selectable(comboItems[i], selected)) {
                    const int originalIndex = mapToFull[i];
                    currentFormID = originalIndex < 0 ? 0 : fullList[originalIndex].formID;
                    searchBuffers[label].clear();
                    changed = true;
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndChild();
            ImGui::EndCombo();
        }
        ImGui::PopID();
        return changed;
    }

    bool DrawFormString(const char* label, const char* category, std::string& value) {
        auto formID = ParseFormID(value);
        if (!DrawDropdown(label, category, formID, 360.0f)) return false;
        value = NormalizeFormID(formID);
        return true;
    }

    bool DrawBool(json& parent, const char* key, const char* label, bool fallback = false) {
        bool value = parent.value(key, fallback);
        if (!ImGui::Checkbox(label, &value)) return false;
        parent[key] = value;
        return true;
    }

    bool DrawInt(json& parent, const char* key, const char* label, int fallback, int minimum, int maximum) {
        int value = std::clamp(parent.value(key, fallback), minimum, maximum);
        bool changed = false;
        ImGui::PushID(label);
        ImGui::SetNextItemWidth(180.0f);
        changed |= ImGui::SliderInt("##slider", &value, minimum, maximum);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(180.0f);
        changed |= ImGui::InputInt(label, &value);
        ImGui::PopID();
        value = std::clamp(value, minimum, maximum);
        if (changed) parent[key] = value;
        return changed;
    }

    bool DrawFloat(json& parent, const char* key, const char* label, float fallback, float minimum, float maximum) {
        float value = std::clamp(parent.value(key, fallback), minimum, maximum);
        bool changed = false;
        ImGui::PushID(label);
        ImGui::SetNextItemWidth(180.0f);
        changed |= ImGui::SliderFloat("##slider", &value, minimum, maximum, "%.2f");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(180.0f);
        changed |= ImGui::InputFloat(label, &value, 0.0f, 0.0f, "%.2f");
        ImGui::PopID();
        value = std::clamp(value, minimum, maximum);
        if (changed) parent[key] = value;
        return changed;
    }

    bool DrawColor(json& parent, const char* key, const char* label, const std::array<float, 4>& fallback) {
        auto color = fallback;
        const auto stored = parent.find(key);
        if (stored != parent.end() && stored->is_array() && stored->size() == color.size()) {
            for (std::size_t i = 0; i < color.size(); ++i) {
                if ((*stored)[i].is_number()) color[i] = std::clamp((*stored)[i].get<float>(), 0.0f, 1.0f);
            }
        }

        constexpr ImGui::ImGuiColorEditFlags flags = ImGui::ImGuiColorEditFlags_AlphaBar | ImGui::ImGuiColorEditFlags_AlphaPreviewHalf;
        if (!ImGui::ColorEdit4(label, color.data(), flags)) return false;
        parent[key] = color;
        return true;
    }

    bool DrawOptionalInt(json& parent, const char* key, const char* label, int fallback, int minimum, int maximum) {
        ImGui::PushID(key);
        if (!parent.contains(key)) {
            ImGui::Text("%s: %s", label, ModMenu::GetLoc("common.base", "Base"));
            ImGui::SameLine();
            const bool changed = ImGui::Button("Override");
            if (changed) parent[key] = fallback;
            ImGui::PopID();
            return changed;
        }
        bool changed = DrawInt(parent, key, label, fallback, minimum, maximum);
        ImGui::SameLine();
        if (ImGui::Button("Use base")) { parent.erase(key); changed = true; }
        ImGui::PopID();
        return changed;
    }

    bool DrawOptionalFloat(json& parent, const char* key, const char* label, float fallback, float minimum, float maximum) {
        ImGui::PushID(key);
        if (!parent.contains(key)) {
            ImGui::Text("%s: %s", label, ModMenu::GetLoc("common.base", "Base"));
            ImGui::SameLine();
            const bool changed = ImGui::Button("Override");
            if (changed) parent[key] = fallback;
            ImGui::PopID();
            return changed;
        }
        bool changed = DrawFloat(parent, key, label, fallback, minimum, maximum);
        ImGui::SameLine();
        if (ImGui::Button("Use base")) { parent.erase(key); changed = true; }
        ImGui::PopID();
        return changed;
    }

    bool DrawString(json& parent, const char* key, const char* label, std::size_t capacity = 256) {
        std::vector<char> buffer(capacity, '\0');
        const auto value = parent.value(key, "");
        strcpy_s(buffer.data(), buffer.size(), value.c_str());
        if (!ImGui::InputText(label, buffer.data(), buffer.size())) return false;
        parent[key] = buffer.data();
        return true;
    }

    void NotifyPrisma() {
        Prisma::SendUpdateToUI();
    }

    bool DrawActorRule(json& rule, const char* label) {
        const auto actors = RosterService::GetSelectableActors(GetSettings());
        if (actors.empty()) return false;
        int selected = 0;
        std::vector<const char*> names;
        names.reserve(actors.size());
        const auto currentKey = rule.value("actorKey", "");
        for (std::size_t i = 0; i < actors.size(); ++i) {
            names.push_back(actors[i]->GetName());
            if (ActorIdentityService::RuleKey(actors[i]) == currentKey) selected = static_cast<int>(i);
        }
        if (!ImGui::Combo(label, &selected, names.data(), static_cast<int>(names.size()))) return false;
        rule["actorKey"] = ActorIdentityService::RuleKey(actors[selected]);
        rule["actorName"] = actors[selected]->GetName();
        return true;
    }
}

void ModMenu::LoadLanguage() {
    language.clear();
    if (!std::filesystem::exists(kLocalizationDirectory)) return;
    for (const auto& entry : std::filesystem::directory_iterator(kLocalizationDirectory)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
        try {
            std::ifstream file(entry.path());
            FlattenLanguage(json::parse(file));
        } catch (const std::exception& error) {
            logger::warn("Could not load localization file {}: {}", entry.path().string(), error.what());
        }
    }
}

const char* ModMenu::GetLoc(const std::string& key, const char* fallback) {
    const auto found = language.find(key);
    return found == language.end() ? fallback : found->second.c_str();
}

void ModMenu::UIRender() {
    auto settings = GetUISettings();
    bool changed = false;
    const char* previewModes[] = { "Full", "Background", "Tree", "None" };
    const std::array<const char*, 4> previewValues = { "full", "bg", "tree", "none" };
    int preview = 0;
    const auto current = settings.value("columnPreviewMode", "full");
    for (int i = 0; i < 4; ++i) if (current == previewValues[i]) preview = i;
    if (ImGui::Combo(GetLoc("ui_options.column_preview_label", "Column preview"), &preview, previewModes, 4)) {
        settings["columnPreviewMode"] = previewValues[preview];
        changed = true;
    }
    changed |= DrawBool(settings, "hideLockedTreeNames", GetLoc("ui_options.hide_locked_names", "Hide locked tree names"), true);
    changed |= DrawBool(settings, "hideLockedTreeBG", GetLoc("ui_options.hide_locked_bg", "Hide locked tree backgrounds"));
    changed |= DrawBool(settings, "performanceMode", GetLoc("ui_options.performance_mode", "Performance mode"));
    changed |= DrawBool(settings, "hidePerkNames", GetLoc("ui_options.hide_perk_names", "Hide perk names"));

    if (ImGui::CollapsingHeader(GetLoc("ui_options.typography", "Typography"), ImGui::ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent();
        changed |= DrawInt(settings, "normalTextSizePercent", GetLoc("ui_options.normal_text_size", "Normal text size (%)"), 100, 50, 200);
        changed |= DrawInt(settings, "headerTextSizePercent", GetLoc("ui_options.header_text_size", "Header text size (%)"), 100, 50, 200);
        changed |= DrawInt(settings, "carouselTextSizePercent", GetLoc("ui_options.carousel_text_size", "Carousel text size (%)"), 100, 50, 200);
        changed |= DrawInt(settings, "treeTitleTextSizePercent", GetLoc("ui_options.tree_title_text_size", "Tree title size (%)"), 100, 50, 200);
        changed |= DrawInt(settings, "perkTitleTextSizePercent", GetLoc("ui_options.perk_title_text_size", "Perk title size (%)"), 100, 50, 200);
        changed |= DrawInt(settings, "perkTextSizePercent", GetLoc("ui_options.perk_text_size", "Perk text size (%)"), 100, 50, 200);
        changed |= DrawInt(settings, "bottomTextSizePercent", GetLoc("ui_options.bottom_text_size", "Bottom panel text size (%)"), 100, 50, 200);
        ImGui::Unindent();
    }

    if (ImGui::CollapsingHeader(GetLoc("ui_options.editor_and_bars", "Editor and bars"), ImGui::ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent();
        changed |= DrawInt(settings, "editorTextSizePercent", GetLoc("ui_options.editor_text_size", "Editor text size (%)"), 100, 50, 200);
        changed |= DrawInt(settings, "editorButtonScalePercent", GetLoc("ui_options.editor_button_scale", "Editor button scale (%)"), 100, 50, 200);
        changed |= DrawInt(settings, "barWidthPercent", GetLoc("ui_options.bar_width", "Bar width (%)"), 100, 50, 200);
        changed |= DrawInt(settings, "barHeightPercent", GetLoc("ui_options.bar_height", "Bar height (%)"), 100, 50, 200);
        ImGui::Unindent();
    }

    if (ImGui::CollapsingHeader(GetLoc("ui_options.popups", "Popups"), ImGui::ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent();
        changed |= DrawInt(settings, "popupMinWidthPixels", GetLoc("ui_options.popup_min_width", "Minimum width (px)"), 300, 180, 800);
        changed |= DrawInt(settings, "popupMaxWidthPixels", GetLoc("ui_options.popup_max_width", "Maximum width (px)"), 520, 240, 1200);
        changed |= DrawInt(settings, "popupPaddingPixels", GetLoc("ui_options.popup_padding", "Inner padding (px)"), 16, 4, 64);
        const int minimum = settings.value("popupMinWidthPixels", 300);
        if (settings.value("popupMaxWidthPixels", 520) < minimum) {
            settings["popupMaxWidthPixels"] = minimum;
            changed = true;
        }
        ImGui::Unindent();
    }

    if (ImGui::CollapsingHeader(GetLoc("ui_options.level_up", "Level Up"), ImGui::ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent();
        changed |= DrawInt(settings, "levelUpModalWidthPercent", GetLoc("ui_options.level_up_modal_width", "Modal width (% of screen)"), 82, 50, 95);
        changed |= DrawInt(settings, "levelUpModalHeightPercent", GetLoc("ui_options.level_up_modal_height", "Modal maximum height (% of screen)"), 86, 50, 95);
        changed |= DrawInt(settings, "levelUpTitleSizePercent", GetLoc("ui_options.level_up_title_size", "Title size (%)"), 125, 50, 200);
        changed |= DrawInt(settings, "levelUpTextSizePercent", GetLoc("ui_options.level_up_text_size", "Text size (%)"), 115, 50, 200);
        changed |= DrawInt(settings, "levelUpButtonScalePercent", GetLoc("ui_options.level_up_button_scale", "Button scale (%)"), 120, 50, 200);
        changed |= DrawInt(settings, "levelUpSpacingPercent", GetLoc("ui_options.level_up_spacing", "Spacing (%)"), 120, 50, 200);
        ImGui::Unindent();
    }

    if (ImGui::CollapsingHeader(GetLoc("ui_options.colors", "Colors"), ImGui::ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent();
        changed |= DrawColor(settings, "primaryTextColor", GetLoc("ui_options.primary_text_color", "Primary text"), { 1.0f, 1.0f, 1.0f, 1.0f });
        changed |= DrawColor(settings, "secondaryTextColor", GetLoc("ui_options.secondary_text_color", "Secondary text"), { 0.8f, 0.8f, 0.8f, 1.0f });
        changed |= DrawColor(settings, "accentColor", GetLoc("ui_options.accent_color", "UI accent"), { 0.302f, 0.816f, 0.882f, 1.0f });
        changed |= DrawColor(settings, "lockedTextColor", GetLoc("ui_options.locked_text_color", "Locked text"), { 0.667f, 0.667f, 0.667f, 1.0f });
        changed |= DrawColor(settings, "successColor", GetLoc("ui_options.success_color", "Requirement met"), { 0.298f, 0.686f, 0.314f, 1.0f });
        changed |= DrawColor(settings, "dangerColor", GetLoc("ui_options.danger_color", "Requirement unmet / error"), { 1.0f, 0.322f, 0.322f, 1.0f });
        changed |= DrawColor(settings, "backgroundColor", GetLoc("ui_options.background_color", "Main background"), { 0.0f, 0.0f, 0.0f, 0.6f });
        ImGui::Unindent();
    }

    if (ImGui::Button(GetLoc("ui_options.reset_appearance", "Reset UI appearance"))) {
        const json defaults = {
            {"normalTextSizePercent", 100}, {"headerTextSizePercent", 100}, {"carouselTextSizePercent", 100},
            {"treeTitleTextSizePercent", 100}, {"perkTitleTextSizePercent", 100}, {"perkTextSizePercent", 100},
            {"bottomTextSizePercent", 100}, {"editorTextSizePercent", 100}, {"editorButtonScalePercent", 100},
            {"barWidthPercent", 100}, {"barHeightPercent", 100}, {"popupMinWidthPixels", 300},
            {"popupMaxWidthPixels", 520}, {"popupPaddingPixels", 16},
            {"levelUpModalWidthPercent", 82}, {"levelUpModalHeightPercent", 86},
            {"levelUpTitleSizePercent", 125}, {"levelUpTextSizePercent", 115},
            {"levelUpButtonScalePercent", 120}, {"levelUpSpacingPercent", 120},
            {"primaryTextColor", { 1.0, 1.0, 1.0, 1.0 }},
            {"secondaryTextColor", { 0.8, 0.8, 0.8, 1.0 }}, {"accentColor", { 0.302, 0.816, 0.882, 1.0 }},
            {"lockedTextColor", { 0.667, 0.667, 0.667, 1.0 }}, {"successColor", { 0.298, 0.686, 0.314, 1.0 }},
            {"dangerColor", { 1.0, 0.322, 0.322, 1.0 }}, {"backgroundColor", { 0.0, 0.0, 0.0, 0.6 }}
        };
        for (const auto& [key, value] : defaults.items()) settings[key] = value;
        changed = true;
    }
    ImGui::Separator();
    changed |= DrawBool(settings, "enableEditorMode", GetLoc("ui_options.enable_editor", "Enable editor mode"));
    if (changed) { SaveUISettingsToFile(settings); NotifyPrisma(); }
}

void ModMenu::BaseRender() {
    auto settings = GetSettings();
    auto& base = settings["base"];
    bool changed = false;
    changed |= DrawInt(base, "perksPerLevel", "Perks per level", 1, 0, 100);
    changed |= DrawInt(base, "skillPointsPerLevel", "Skill points per level", 1, 0, 100);
    changed |= DrawInt(base, "maxSkillPointsSpendablePerLevel", "Maximum skill points spendable per level", 10, 0, 1000);
    changed |= DrawInt(base, "skillCap", "Skill cap", 100, 1, 1000);
    changed |= DrawBool(base, "useDynamicSkillCap", "Use dynamic skill cap", true);
    if (base.value("useDynamicSkillCap", true)) {
        ImGui::Indent();
        changed |= DrawFloat(base, "skillCapPerLevelMult", "Skill cap per level multiplier", 2.0f, 0.0f, 100.0f);
        changed |= DrawBool(base, "applyRacialBonusToCap", "Apply racial bonus to cap", true);
        ImGui::Unindent();
    }
    changed |= DrawBool(base, "enableLegendary", "Enable legendary skills", true);
    changed |= DrawBool(base, "refillAttributesOnLevelUp", "Refill attributes on level up");
    changed |= DrawBool(base, "useBaseSkillLevel", "Use base skill level", true);
    changed |= DrawBool(base, "applyVanillaInitialLevels", "Apply vanilla initial levels", true);
    changed |= DrawFloat(base, "healthIncrease", "Health increase", 10.0f, 0.0f, 10000.0f);
    changed |= DrawFloat(base, "magickaIncrease", "Magicka increase", 10.0f, 0.0f, 10000.0f);
    changed |= DrawFloat(base, "staminaIncrease", "Stamina increase", 10.0f, 0.0f, 10000.0f);
    changed |= DrawInt(base, "maxPerkPoints", "Maximum perk points", 255, 0, 1000000);
    changed |= DrawInt(base, "maxResetsPerActor", "Maximum resets per actor (-1 = unlimited)", -1, -1, 1000000);

    if (ImGui::CollapsingHeader("Carry weight", ImGui::ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* methods[] = { "None", "Automatic", "Linked attributes" };
        const std::array<const char*, 3> values = { "none", "auto", "linked" };
        int method = 0;
        for (int i = 0; i < 3; ++i) if (base.value("carryWeightMethod", "none") == values[i]) method = i;
        if (ImGui::Combo("Method", &method, methods, 3)) { base["carryWeightMethod"] = values[method]; changed = true; }
        changed |= DrawFloat(base, "carryWeightIncrease", "Carry weight increase", 0.0f, 0.0f, 10000.0f);
        if (method == 2) {
            auto linked = base.value("carryWeightLinkedAttributes", json::array());
            for (const char* attribute : { "Health", "Magicka", "Stamina" }) {
                bool enabled = std::ranges::find(linked, attribute) != linked.end();
                if (ImGui::Checkbox(attribute, &enabled)) {
                    if (enabled) {
                        linked.push_back(attribute);
                    } else {
                        json filtered = json::array();
                        for (const auto& item : linked) if (item != attribute) filtered.push_back(item);
                        linked = std::move(filtered);
                    }
                    base["carryWeightLinkedAttributes"] = linked;
                    changed = true;
                }
            }
        }
    }

    if (ImGui::CollapsingHeader("Follower detection")) {
        auto& followers = settings["followerDetection"];
        changed |= DrawBool(followers, "allowHumanoidTeammates", "Allow humanoid PlayerTeammate actors", true);
        changed |= DrawBool(followers, "allowSummoned", "Allow summoned teammates");
        for (const auto& [key, label] : std::array{
            std::pair{ "currentFollowerFactions", "Current follower factions (one Plugin|FormID per line)" },
            std::pair{ "potentialFollowerFactions", "Potential follower factions (one Plugin|FormID per line)" } }) {
            std::string text;
            for (const auto& item : followers.value(key, json::array())) {
                if (!text.empty()) text += '\n';
                text += item.get<std::string>();
            }
            char buffer[2048]{};
            strcpy_s(buffer, text.c_str());
            if (ImGui::InputTextMultiline(label, buffer, sizeof(buffer), ImGui::ImVec2(-1, 100))) {
                json values = json::array();
                std::istringstream lines(buffer);
                for (std::string line; std::getline(lines, line);) if (!line.empty()) values.push_back(line);
                followers[key] = values;
                changed = true;
            }
        }
    }
    if (changed) { SaveSettingsToFile(settings); NotifyPrisma(); }
}

void ModMenu::RulesRender() {
    auto rules = GetLevelRules();
    bool changed = false;
    int removeIndex = -1;
    for (std::size_t index = 0; index < rules.size(); ++index) {
        auto& rule = rules[index];
        ImGui::PushID(static_cast<int>(index));
        const auto title = std::format("Rule {}", index + 1);
        if (ImGui::CollapsingHeader(title.c_str(), ImGui::ImGuiTreeNodeFlags_DefaultOpen)) {
            changed |= DrawInt(rule, "level", "Level", 2, 0, 100000);
            const char* scopes[] = { "All actors", "Player", "Followers", "Specific actor" };
            const std::array<const char*, 4> scopeValues = { "all", "player", "followers", "actor" };
            int scope = 1;
            for (int i = 0; i < 4; ++i) if (rule.value("scope", "player") == scopeValues[i]) scope = i;
            if (ImGui::Combo("Scope", &scope, scopes, 4)) {
                rule["scope"] = scopeValues[scope];
                if (scope != 3) { rule.erase("actorKey"); rule.erase("actorName"); }
                changed = true;
            }
            if (scope == 3) changed |= DrawActorRule(rule, "Actor");
            for (const auto& [key, label] : std::array{
                std::pair{"perksPerLevel", "Perks per level"}, std::pair{"skillPointsPerLevel", "Skill points per level"},
                std::pair{"maxSkillPointsSpendablePerLevel", "Maximum spendable skill points"}, std::pair{"skillCap", "Skill cap"},
                std::pair{"maxPerkPoints", "Maximum perk points"}, std::pair{"maxResetsPerActor", "Maximum resets (-1 = unlimited)"} }) {
                changed |= DrawOptionalInt(rule, key, label, key == std::string_view("maxResetsPerActor") ? -1 : 0, -1, 1000000);
            }
            changed |= DrawOptionalFloat(rule, "healthIncrease", "Health increase", 0.0f, 0.0f, 10000.0f);
            changed |= DrawOptionalFloat(rule, "magickaIncrease", "Magicka increase", 0.0f, 0.0f, 10000.0f);
            changed |= DrawOptionalFloat(rule, "staminaIncrease", "Stamina increase", 0.0f, 0.0f, 10000.0f);
            changed |= DrawOptionalFloat(rule, "carryWeightIncrease", "Carry weight increase", 0.0f, 0.0f, 10000.0f);

            auto resources = GetCustomResources();
            auto& rewards = rule["resourceRewards"];
            if (!rewards.is_array()) rewards = json::array();
            int removeReward = -1;
            if (ImGui::CollapsingHeader("Resource rewards")) {
                for (std::size_t rewardIndex = 0; rewardIndex < rewards.size(); ++rewardIndex) {
                    auto& reward = rewards[rewardIndex];
                    ImGui::PushID(static_cast<int>(rewardIndex));
                    std::vector<std::string> resourceLabels;
                    std::vector<const char*> resourceNames;
                    int selectedResource = 0;
                    for (std::size_t resourceIndex = 0; resourceIndex < resources.size(); ++resourceIndex) {
                        resourceLabels.push_back(resources[resourceIndex].value("name", resources[resourceIndex].value("id", "Resource")));
                        if (resources[resourceIndex].value("id", "") == reward.value("resourceId", "")) selectedResource = static_cast<int>(resourceIndex);
                    }
                    for (const auto& label : resourceLabels) resourceNames.push_back(label.c_str());
                    if (!resources.empty() && ImGui::Combo("Resource", &selectedResource, resourceNames.data(), static_cast<int>(resourceNames.size()))) {
                        reward["resourceId"] = resources[selectedResource].value("id", "");
                        changed = true;
                    }
                    changed |= DrawFloat(reward, "amount", "Amount", 1.0f, 0.0f, 1000000.0f);
                    if (ImGui::Button("Delete reward")) removeReward = static_cast<int>(rewardIndex);
                    ImGui::PopID();
                }
                if (removeReward >= 0) { rewards.erase(rewards.begin() + removeReward); changed = true; }
                if (!resources.empty() && ImGui::Button("Add resource reward")) {
                    rewards.push_back({ {"resourceId", resources[0].value("id", "")}, {"amount", 1} });
                    changed = true;
                }
            }
            if (ImGui::Button("Delete rule")) removeIndex = static_cast<int>(index);
        }
        ImGui::PopID();
    }
    if (removeIndex >= 0) { rules.erase(rules.begin() + removeIndex); changed = true; }
    if (ImGui::Button("Add rule")) { rules.push_back({ {"level", 2}, {"scope", "player"} }); changed = true; }
    const auto errors = RequirementService::ValidateRules(rules);
    if (!errors.empty()) ImGui::TextDisabled("Rule validation failed (%zu errors).", errors.size());
    if (changed && errors.empty()) { SaveLevelRulesToFile(rules); NotifyPrisma(); }
}

void ModMenu::CategoriesRender() {
    auto settings = GetSettings();
    auto& categories = settings["categories"];
    bool changed = false;
    int removeIndex = -1;
    for (std::size_t i = 0; i < categories.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        char buffer[128]{};
        strcpy_s(buffer, categories[i].get_ref<const std::string&>().c_str());
        if (ImGui::InputText("Category", buffer, sizeof(buffer))) { categories[i] = buffer; changed = true; }
        ImGui::SameLine();
        if (ImGui::Button("Delete")) removeIndex = static_cast<int>(i);
        ImGui::PopID();
    }
    if (removeIndex >= 0) { categories.erase(categories.begin() + removeIndex); changed = true; }
    if (ImGui::Button("Add category")) { categories.push_back("New Category"); changed = true; }
    if (changed) { SaveSettingsToFile(settings); NotifyPrisma(); }
}

void ModMenu::ResourcesRender() {
    auto resources = GetCustomResources();
    bool changed = false;
    std::string deleteID;
    for (std::size_t i = 0; i < resources.size(); ++i) {
        auto& resource = resources[i];
        ImGui::PushID(static_cast<int>(i));
        const auto title = resource.value("name", resource.value("id", "Resource"));
        if (ImGui::CollapsingHeader(title.c_str(), ImGui::ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("ID: %s", resource.value("id", "").c_str());
            changed |= DrawString(resource, "name", "Display name");
            std::string global = resource.value("glob", "");
            if (DrawFormString("Global", "Global", global)) {
                resource["glob"] = global;
                if (!global.empty()) resource["actorValue"] = "";
                changed = true;
            }
            if (DrawString(resource, "actorValue", "Actor Value")) {
                if (!resource.value("actorValue", "").empty()) resource["glob"] = "";
                changed = true;
            }
            if (!resource.value("glob", "").empty()) {
                changed |= DrawBool(resource, "npcUsesActorValue", "NPCs use Actor Value");
                if (resource.value("npcUsesActorValue", false)) {
                    changed |= DrawString(resource, "npcActorValue", "NPC Actor Value");
                }
            }
            if (ImGui::Button("Delete resource")) deleteID = resource.value("id", "");
        }
        ImGui::PopID();
    }
    if (!deleteID.empty()) { DeleteResourceByID(deleteID); NotifyPrisma(); return; }
    if (ImGui::Button("Add resource")) {
        const auto id = std::format("resource_{}", resources.size() + 1);
        resources.push_back({
            {"id", id},
            {"name", "New Resource"},
            {"glob", ""},
            {"actorValue", ""},
            {"npcUsesActorValue", false},
            {"npcActorValue", ""}
        });
        changed = true;
    }
    if (changed) { SaveResourcesToFile(resources); NotifyPrisma(); }
}

void ModMenu::CodesRender() {
    auto settings = GetSettings();
    auto& codes = settings["codes"];
    bool changed = false;
    int removeIndex = -1;
    for (std::size_t i = 0; i < codes.size(); ++i) {
        auto& code = codes[i];
        ImGui::PushID(static_cast<int>(i));
        const auto title = code.value("code", "Code");
        if (ImGui::CollapsingHeader(title.c_str(), ImGui::ImGuiTreeNodeFlags_DefaultOpen)) {
            changed |= DrawString(code, "code", "Code");
            changed |= DrawInt(code, "maxUses", "Maximum uses (-1 = unlimited)", 1, -1, 1000000);
            changed |= DrawBool(code, "isEditorCode", "Editor code");
            ImGui::Text("Current uses: %d", code.value("currentUses", 0));
            auto& rewards = code["rewards"];
            changed |= DrawInt(rewards, "perkPoints", "Perk points reward", 0, 0, 1000000);
            changed |= DrawFloat(rewards, "health", "Health reward", 0.0f, 0.0f, 1000000.0f);
            changed |= DrawFloat(rewards, "magicka", "Magicka reward", 0.0f, 0.0f, 1000000.0f);
            changed |= DrawFloat(rewards, "stamina", "Stamina reward", 0.0f, 0.0f, 1000000.0f);
            if (ImGui::Button("Delete code")) removeIndex = static_cast<int>(i);
        }
        ImGui::PopID();
    }
    if (removeIndex >= 0) { codes.erase(codes.begin() + removeIndex); changed = true; }
    if (ImGui::Button("Add code")) {
        codes.push_back({ {"code", "NEWCODE"}, {"maxUses", 1}, {"currentUses", 0}, {"rewards", json::object()}, {"isEditorCode", false} });
        changed = true;
    }
    if (changed) { SaveSettingsToFile(settings); NotifyPrisma(); }
}

void ModMenu::MaintenanceRender() {
    const auto actors = RosterService::GetSelectableActors(GetSettings());
    if (actors.empty()) return;
    int selected = 0;
    std::vector<const char*> names;
    for (std::size_t i = 0; i < actors.size(); ++i) {
        names.push_back(actors[i]->GetName());
        if (actors[i]->GetFormID() == maintenanceActorID) selected = static_cast<int>(i);
    }
    if (ImGui::Combo("Actor", &selected, names.data(), static_cast<int>(names.size()))) maintenanceActorID = actors[selected]->GetFormID();
    if (ImGui::Button("Reset all purchased perks")) ImGui::OpenPopup("Confirm perk reset");
    if (ImGui::BeginPopupModal("Confirm perk reset")) {
        ImGui::TextWrapped("Reset all perks purchased through NSM for %s?", actors[selected]->GetName());
        if (ImGui::Button("Confirm")) {
            const auto effective = GetEffectiveSettings(actors[selected]->GetLevel(), actors[selected]);
            ResetService::Execute(actors[selected], {}, GetCustomResources(), effective.value("maxPerkPoints", 255), effective.value("maxResetsPerActor", -1), true);
            NotifyPrisma();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void ModMenu::Register() {
    LoadLanguage();
    GetSettings();
    GetUISettings();
    GetLevelRules();
    GetCustomResources();
    if (!SKSEMenuFramework::IsInstalled()) {
        logger::warn("SKSE Menu Framework not found; NSM will use saved/default settings.");
        return;
    }
    SKSEMenuFramework::SetSection("NSM");
    SKSEMenuFramework::AddSectionItem(GetLoc("menu.ui_options", "UI Options"), UIRender);
    SKSEMenuFramework::AddSectionItem(GetLoc("menu.base_settings", "Base Settings"), BaseRender);
    SKSEMenuFramework::AddSectionItem(GetLoc("menu.level_rules", "Level Rules"), RulesRender);
    SKSEMenuFramework::AddSectionItem(GetLoc("menu.categories", "Categories"), CategoriesRender);
    SKSEMenuFramework::AddSectionItem(GetLoc("menu.resources", "Resources"), ResourcesRender);
    SKSEMenuFramework::AddSectionItem(GetLoc("menu.codes", "Codes"), CodesRender);
    SKSEMenuFramework::AddSectionItem(GetLoc("menu.maintenance", "Maintenance"), MaintenanceRender);
}
