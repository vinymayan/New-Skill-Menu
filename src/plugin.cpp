#include "Plugin.h"
#include "Hooks.h"
#include "InputEventHandler.h"
#include "Manager.h"
#include "SkillMenuAPI.h"


extern void ApplyVanillaInitialLevels();

extern "C" __declspec(dllexport) void* GetSkillMenuAPI() {
    static SkillMenuAPI::Interface api{
        SkillMenuAPI::Version,
        // GetCustomSkillLevel (Retorna o Base)
        [](const char* skillId) -> int {
            auto mgr = Manager::GetSingleton();
            if (mgr->playerCustomSkills.find(skillId) != mgr->playerCustomSkills.end()) {
                return mgr->playerCustomSkills[skillId].currentLevel;
            }
            return 1;
        },
        // AddCustomSkillXP
        [](const char* skillId, float xpAmount) {
            Manager::GetSingleton()->AddCustomSkillXP(skillId, xpAmount);
        },
        // GetCustomSkillXP
        [](const char* skillId) -> float {
            auto mgr = Manager::GetSingleton();
            if (mgr->playerCustomSkills.find(skillId) != mgr->playerCustomSkills.end()) {
                return mgr->playerCustomSkills[skillId].currentXP;
            }
            return 0.0f;
        },
        // GetSkillFormulaValue
        [](const char* skillId, int valueType) -> float {
            auto& data = Manager::GetSingleton()->customSkillsData;
            if (data.contains(skillId)) {
                auto& formula = data[skillId].expFormula;
                switch (valueType) {
                    case 0: return formula.useMult;
                    case 1: return formula.useOffset;
                    case 2: return formula.improveMult;
                    case 3: return formula.improveOffset;
                    default: return 0.0f;
                }
            }
            return 0.0f;
        },
        // --- V2 API ---
        // GetCustomSkillTotalLevel
        [](const char* skillId) -> int {
            auto mgr = Manager::GetSingleton();
            if (mgr->playerCustomSkills.find(skillId) != mgr->playerCustomSkills.end()) {
                auto& state = mgr->playerCustomSkills[skillId];
                return state.currentLevel + state.bonusLevel;
            }
            return 1;
        },
        // GetCustomSkillBonus
        [](const char* skillId) -> int {
            auto mgr = Manager::GetSingleton();
            if (mgr->playerCustomSkills.find(skillId) != mgr->playerCustomSkills.end()) {
                return mgr->playerCustomSkills[skillId].bonusLevel;
            }
            return 0;
        },
        // ModCustomSkillBonus
        [](const char* skillId, int amount) {
            auto mgr = Manager::GetSingleton();
            if (mgr->playerCustomSkills.find(skillId) != mgr->playerCustomSkills.end()) {
                mgr->playerCustomSkills[skillId].bonusLevel += amount;
                Prisma::SendUpdateToUI(); // Atualiza a UI se o menu estiver aberto
            }
        },
        // SetCustomSkillBonus
        [](const char* skillId, int amount) {
            auto mgr = Manager::GetSingleton();
            if (mgr->playerCustomSkills.find(skillId) != mgr->playerCustomSkills.end()) {
                mgr->playerCustomSkills[skillId].bonusLevel = amount;
                Prisma::SendUpdateToUI(); // Atualiza a UI se o menu estiver aberto
            }
        }
    };
    return &api;
}

// ==========================================
// API Papyrus
// ==========================================
namespace PapyrusAPI {
    float GetSkillFormulaValue(RE::StaticFunctionTag*, RE::BSFixedString skillId, int valueType) {
        auto& data = Manager::GetSingleton()->customSkillsData;
        if (data.contains(skillId.c_str())) {
            auto& formula = data[skillId.c_str()].expFormula;
            switch (valueType) {
            case 0: return formula.useMult;
            case 1: return formula.useOffset;
            case 2: return formula.improveMult;
            case 3: return formula.improveOffset;
            }
        }
        return 0.0f;
    }
    void AddCustomSkillXP(RE::StaticFunctionTag*, RE::BSFixedString skillId, float xp) {
        Manager::GetSingleton()->AddCustomSkillXP(skillId.c_str(), xp);
    }

    int GetCustomSkillLevel(RE::StaticFunctionTag*, RE::BSFixedString skillId) {
        return Manager::GetSingleton()->playerCustomSkills[skillId.c_str()].currentLevel;
    }

    float GetCustomSkillXP(RE::StaticFunctionTag*, RE::BSFixedString skillId) {
        return Manager::GetSingleton()->playerCustomSkills[skillId.c_str()].currentXP;
    }

    // --- V2 Papyrus ---
    int GetCustomSkillTotalLevel(RE::StaticFunctionTag*, RE::BSFixedString skillId) {
        auto& state = Manager::GetSingleton()->playerCustomSkills[skillId.c_str()];
        return state.currentLevel + state.bonusLevel;
    }
    int GetCustomSkillBonus(RE::StaticFunctionTag*, RE::BSFixedString skillId) {
        return Manager::GetSingleton()->playerCustomSkills[skillId.c_str()].bonusLevel;
    }
    void ModCustomSkillBonus(RE::StaticFunctionTag*, RE::BSFixedString skillId, int amount) {
        Manager::GetSingleton()->playerCustomSkills[skillId.c_str()].bonusLevel += amount;
        Prisma::SendUpdateToUI();
    }
    void SetCustomSkillBonus(RE::StaticFunctionTag*, RE::BSFixedString skillId, int amount) {
        Manager::GetSingleton()->playerCustomSkills[skillId.c_str()].bonusLevel = amount;
        Prisma::SendUpdateToUI();
    }

    int GetAPIVersion(RE::StaticFunctionTag*) {
        return SkillMenuAPI::Version;
    }

    bool Bind(RE::BSScript::IVirtualMachine* vm) {
        vm->RegisterFunction("AddCustomSkillXP", "NewSkillMenu", AddCustomSkillXP);
        vm->RegisterFunction("GetCustomSkillLevel", "NewSkillMenu", GetCustomSkillLevel);
        vm->RegisterFunction("GetCustomSkillXP", "NewSkillMenu", GetCustomSkillXP);
        vm->RegisterFunction("GetSkillFormulaValue", "NewSkillMenu", GetSkillFormulaValue);

        // Registrar V2
        vm->RegisterFunction("GetCustomSkillTotalLevel", "NewSkillMenu", GetCustomSkillTotalLevel);
        vm->RegisterFunction("GetCustomSkillBonus", "NewSkillMenu", GetCustomSkillBonus);
        vm->RegisterFunction("ModCustomSkillBonus", "NewSkillMenu", ModCustomSkillBonus);
        vm->RegisterFunction("SetCustomSkillBonus", "NewSkillMenu", SetCustomSkillBonus);

        vm->RegisterFunction("GetAPIVersion", "NewSkillMenu", GetAPIVersion);
        return true;
    }
}

// ==========================================
// CALLABACKS DE SERIALIZAÇÃO (Save/Load)
// ==========================================
void OnSerializationSave(SKSE::SerializationInterface* a_intfc) {
    Manager::GetSingleton()->Save(a_intfc);
}
void OnSerializationLoad(SKSE::SerializationInterface* a_intfc) {
    Manager::GetSingleton()->Load(a_intfc);
}
void OnSerializationRevert(SKSE::SerializationInterface* a_intfc) {
    Manager::GetSingleton()->Revert(a_intfc);
    Prisma::Hide();
    Prisma::SendUpdateToUI();
}

extern void GenerateAllVanillaTrees();


void OnMessage(SKSE::MessagingInterface::Message* message) {
    if (message->type == SKSE::MessagingInterface::kDataLoaded) {
        Manager::GetSingleton()->PopulateAllLists();
        GenerateAllVanillaTrees();
        Manager::GetSingleton()->LoadCustomSkills();
        Prisma::PreloadLocalization();
        PlayerLevel::Register();
        Prisma::Install();
	}
    if (message->type == SKSE::MessagingInterface::kNewGame) {
        ApplyVanillaInitialLevels();
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse);
    SetupLog();
    logger::info("Plugin loaded");
    SKSE::GetMessagingInterface()->RegisterListener(OnMessage);

    // Registra a Serialização no Save (Identificador PRSM)
    auto serialization = SKSE::GetSerializationInterface();
    serialization->SetUniqueID('NSMV');
    serialization->SetSaveCallback(OnSerializationSave);
    serialization->SetLoadCallback(OnSerializationLoad);
    serialization->SetRevertCallback(OnSerializationRevert);

    // Registra a API do Papyrus
    auto papyrus = SKSE::GetPapyrusInterface();
    papyrus->Register(PapyrusAPI::Bind);
    Hooks::Install();
    return true;
}