#pragma once

#include <nlohmann/json.hpp>

namespace ModMenu {
    void Register();
    void UIRender();
    void BaseRender();
    void RulesRender();
    void CategoriesRender();
    void ResourcesRender();
    void CodesRender();
    void MaintenanceRender();
    void LoadLanguage();
    const char* GetLoc(const std::string& key, const char* fallback);
}

// Shared persistence API. The gameplay and Prisma UI intentionally keep using
// these free functions while the SKSE menu becomes the only settings editor.
nlohmann::json GetSettings();
nlohmann::json GetUISettings();
nlohmann::json GetLevelRules();
nlohmann::json GetCustomResources();
nlohmann::json GetEffectiveSettings(int targetLevel, RE::Actor* actor);
void SaveSettingsToFile(const nlohmann::json& settings);
void SaveUISettingsToFile(const nlohmann::json& settings);
void SaveLevelRulesToFile(const nlohmann::json& rules);
void SaveResourcesToFile(const nlohmann::json& resources);
void DeleteResourceByID(const std::string& id);
