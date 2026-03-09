#pragma once

#include <string>
#include <vector>
#include <map>
#include <functional>
#include "ClibUtil/editorID.hpp"
#include <nlohmann/json.hpp> 
#include <mutex>
#include <unordered_set>

struct InternalFormInfo {
    RE::FormID formID;
    std::string editorID;
    std::string name;
    std::string pluginName;
    std::string formType;
    std::string description; 
    std::string nextPerkId;  

    // Helper for UI
    std::string GetDisplayName() const {
        if (!name.empty()) return name;
        if (!editorID.empty()) return editorID;
        return std::to_string(formID);
    }
};


struct ExperienceFormula {
    float useMult = 1.0f;
    float useOffset = 0.0f;
    float improveMult = 1.0f;
    float improveOffset = 0.0f;
};

struct CustomSkill {
    std::string id;
    std::string displayName;
    bool isVanilla = false;
    int initialLevel = 10;
    ExperienceFormula expFormula;
    bool advancesPlayerLevel = false;
};

struct CustomSkillState {
    int currentLevel = 15;
    float currentXP = 0.0f;
};

class Manager {
public:
    static Manager* GetSingleton() {
        static Manager singleton;
        return &singleton;
    }

    void PopulateAllLists();
    void LoadCustomSkills(); 

    static std::string ToUTF8(std::string_view a_str);
    const std::vector<InternalFormInfo>& GetList(const std::string& typeName);
    void RegisterReadyCallback(std::function<void()> callback);

    // --- NOVOS MÉTODOS DE SKILL ---
    void AddCustomSkillXP(const std::string& skillId, float xpAmount);
    float GetRequiredXP(const std::string& skillId, int level);

    // --- NOVOS MÉTODOS DE SAVE/LOAD (SKSE) ---
    void Save(SKSE::SerializationInterface* a_intfc);
    void Load(SKSE::SerializationInterface* a_intfc);
    void Revert(SKSE::SerializationInterface* a_intfc);

    // Dicionários para manter as skills na memória
    std::map<std::string, CustomSkill> customSkillsData;
    std::map<std::string, CustomSkillState> playerCustomSkills;

    const InternalFormInfo* GetInfoByID(const std::string& type, RE::FormID id);
private:
    Manager() = default;
    
    template <typename T>
    void PopulateList(const std::string& a_typeName, std::function<bool(T*)> a_filter = nullptr);

    bool _isPopulated = false;
    std::map<std::string, std::vector<InternalFormInfo>> _dataStore;
    std::vector<std::function<void()>> _readyCallbacks;
};
