#pragma once

#include <string>
#include <vector>
#include <map>
#include <functional>
#include "ClibUtil/editorID.hpp"
#include <nlohmann/json.hpp> 
#include <mutex>
#include <unordered_set>
#include <optional>

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
    int bonusLevel = 0;
};

class Manager {
public:
    static Manager* GetSingleton() {
        static Manager singleton;
        return &singleton;
    }

    void PopulateAllLists();
    void RefreshLists(std::string_view a_signatures);
    void LoadCustomSkills(); 

    static std::string ToUTF8(std::string_view a_str);
    const std::vector<InternalFormInfo>& GetList(const std::string& typeName);
    void RegisterReadyCallback(std::function<void()> callback);

    // --- NOVOS MÉTODOS DE SKILL ---
    void AddCustomSkillXP(const std::string& skillId, float xpAmount);
    void AddCustomSkillXPForActor(RE::Actor* actor, const std::string& skillId, float xpAmount);
    void AddCustomSkillXPForActorID(RE::FormID actorFormID, const std::string& skillId, float xpAmount);
    int GetCustomSkillLevelForActorID(RE::FormID actorFormID, const std::string& skillId);
    float GetCustomSkillXPForActorID(RE::FormID actorFormID, const std::string& skillId);
    int GetCustomSkillTotalLevelForActorID(RE::FormID actorFormID, const std::string& skillId);
    int GetCustomSkillBonusForActorID(RE::FormID actorFormID, const std::string& skillId);
    void ModCustomSkillBonusForActorID(RE::FormID actorFormID, const std::string& skillId, int amount);
    void SetCustomSkillBonusForActorID(RE::FormID actorFormID, const std::string& skillId, int amount);
    bool HasCustomPerkForActorID(RE::FormID actorFormID, const std::string& perkId);
    bool AddCustomPerkForActorID(RE::FormID actorFormID, const std::string& perkId);
    bool RemoveCustomPerkForActorID(RE::FormID actorFormID, const std::string& perkId);
    int GetCustomSkillLevel(RE::Actor* actor, const std::string& skillId);
    float GetCustomSkillXP(RE::Actor* actor, const std::string& skillId);
    int GetCustomSkillTotalLevel(RE::Actor* actor, const std::string& skillId);
    int GetCustomSkillBonus(RE::Actor* actor, const std::string& skillId);
    void ModCustomSkillBonus(RE::Actor* actor, const std::string& skillId, int amount);
    void SetCustomSkillBonus(RE::Actor* actor, const std::string& skillId, int amount);
    void SetCustomSkillLevel(RE::Actor* actor, const std::string& skillId, int level);
    void SetCustomSkillXP(RE::Actor* actor, const std::string& skillId, float xp);
    bool HasCustomPerk(RE::Actor* actor, const std::string& perkId);
    bool AddCustomPerk(RE::Actor* actor, const std::string& perkId);
    bool RemoveCustomPerk(RE::Actor* actor, const std::string& perkId);
    void RemoveCustomSkillState(const std::string& skillId);
    float GetRequiredXP(const std::string& skillId, int level);

    // --- NOVOS MÉTODOS DE SAVE/LOAD (SKSE) ---
    void Save(SKSE::SerializationInterface* a_intfc);
    void Load(SKSE::SerializationInterface* a_intfc);
    void Revert(SKSE::SerializationInterface* a_intfc);

    // Dicionários para manter as skills na memória
    std::map<std::string, CustomSkill> customSkillsData;
    std::map<std::string, CustomSkillState> playerCustomSkills;
    std::map<RE::FormID, std::map<std::string, float>> actorCustomSkillXP;

    const InternalFormInfo* GetInfoByID(const std::string& type, RE::FormID id);
    bool _isPopulated = false;
private:
    Manager() = default;
    
    template <typename T>
    void PopulateList(const std::string& a_typeName, std::function<bool(T*)> a_filter = nullptr);
    std::map<std::string, std::vector<InternalFormInfo>> _dataStore;
    std::vector<std::function<void()>> _readyCallbacks;

    std::string GetCustomSkillActorValueName(const std::string& skillId) const;
    RE::ActorValue ResolveCustomSkillActorValue(const std::string& skillId) const;
    RE::Actor* ResolveActorFromFormID(RE::FormID actorFormID) const;
    void EnsureActorValueGeneratorConfig();
    void SyncLegacyPlayerStateToActorValues();
    RE::FormID GetActorXPKey(RE::Actor* actor) const;
    float GetActorXP(RE::Actor* actor, const std::string& skillId);
    void SetActorXP(RE::Actor* actor, const std::string& skillId, float xp);
};
