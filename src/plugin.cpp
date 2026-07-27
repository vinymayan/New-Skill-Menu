#include "Plugin.h"
#include "Hooks.h"
#include "InputEventHandler.h"
#include "Manager.h"
#include "SkillMenuAPI.h"


extern void ApplyVanillaInitialLevels();

extern "C" __declspec(dllexport) void* GetSkillMenuAPI() {
    static SkillMenuAPI::Interface api{
        SkillMenuAPI::Version,
        [](const char* skillId) -> int {
            if (!skillId) return 1;
            return Manager::GetSingleton()->GetCustomSkillLevel(RE::PlayerCharacter::GetSingleton(), skillId);
        },
        [](const char* skillId, float xpAmount) {
            if (!skillId) return;
            Manager::GetSingleton()->AddCustomSkillXP(skillId, xpAmount);
        },
        [](const char* skillId) -> float {
            if (!skillId) return 0.0f;
            return Manager::GetSingleton()->GetCustomSkillXP(RE::PlayerCharacter::GetSingleton(), skillId);
        },
        [](const char* skillId, int valueType) -> float {
            if (!skillId) return 0.0f;
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
        [](const char* skillId) -> int {
            if (!skillId) return 1;
            return Manager::GetSingleton()->GetCustomSkillTotalLevel(RE::PlayerCharacter::GetSingleton(), skillId);
        },
        [](const char* skillId) -> int {
            if (!skillId) return 0;
            return Manager::GetSingleton()->GetCustomSkillBonus(RE::PlayerCharacter::GetSingleton(), skillId);
        },
        [](const char* skillId, int amount) {
            if (!skillId) return;
            Manager::GetSingleton()->ModCustomSkillBonus(RE::PlayerCharacter::GetSingleton(), skillId, amount);
        },
        [](const char* skillId, int amount) {
            if (!skillId) return;
            Manager::GetSingleton()->SetCustomSkillBonus(RE::PlayerCharacter::GetSingleton(), skillId, amount);
        },
        [](RE::FormID actorFormID, const char* skillId, float xpAmount) {
            if (!skillId) return;
            Manager::GetSingleton()->AddCustomSkillXPForActorID(actorFormID, skillId, xpAmount);
        },
        [](RE::FormID actorFormID, const char* skillId) -> int {
            if (!skillId) return 1;
            return Manager::GetSingleton()->GetCustomSkillLevelForActorID(actorFormID, skillId);
        },
        [](RE::FormID actorFormID, const char* skillId) -> float {
            if (!skillId) return 0.0f;
            return Manager::GetSingleton()->GetCustomSkillXPForActorID(actorFormID, skillId);
        },
        [](RE::FormID actorFormID, const char* skillId) -> int {
            if (!skillId) return 1;
            return Manager::GetSingleton()->GetCustomSkillTotalLevelForActorID(actorFormID, skillId);
        },
        [](RE::FormID actorFormID, const char* skillId) -> int {
            if (!skillId) return 0;
            return Manager::GetSingleton()->GetCustomSkillBonusForActorID(actorFormID, skillId);
        },
        [](RE::FormID actorFormID, const char* skillId, int amount) {
            if (!skillId) return;
            Manager::GetSingleton()->ModCustomSkillBonusForActorID(actorFormID, skillId, amount);
        },
        [](RE::FormID actorFormID, const char* skillId, int amount) {
            if (!skillId) return;
            Manager::GetSingleton()->SetCustomSkillBonusForActorID(actorFormID, skillId, amount);
        },
        [](RE::FormID actorFormID, const char* perkId) -> bool {
            if (!perkId) return false;
            return Manager::GetSingleton()->HasCustomPerkForActorID(actorFormID, perkId);
        },
        [](RE::FormID actorFormID, const char* perkId) -> bool {
            if (!perkId) return false;
            return Manager::GetSingleton()->AddCustomPerkForActorID(actorFormID, perkId);
        },
        [](RE::FormID actorFormID, const char* perkId) -> bool {
            if (!perkId) return false;
            return Manager::GetSingleton()->RemoveCustomPerkForActorID(actorFormID, perkId);
        }
    };
    return &api;
}
// ==========================================
// API Papyrus
// ==========================================
namespace PapyrusAPI {
    static RE::Actor* PlayerActor() {
        return RE::PlayerCharacter::GetSingleton();
    }

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
        return Manager::GetSingleton()->GetCustomSkillLevel(PlayerActor(), skillId.c_str());
    }

    float GetCustomSkillXP(RE::StaticFunctionTag*, RE::BSFixedString skillId) {
        return Manager::GetSingleton()->GetCustomSkillXP(PlayerActor(), skillId.c_str());
    }

    int GetCustomSkillTotalLevel(RE::StaticFunctionTag*, RE::BSFixedString skillId) {
        return Manager::GetSingleton()->GetCustomSkillTotalLevel(PlayerActor(), skillId.c_str());
    }

    int GetCustomSkillBonus(RE::StaticFunctionTag*, RE::BSFixedString skillId) {
        return Manager::GetSingleton()->GetCustomSkillBonus(PlayerActor(), skillId.c_str());
    }

    void ModCustomSkillBonus(RE::StaticFunctionTag*, RE::BSFixedString skillId, int amount) {
        Manager::GetSingleton()->ModCustomSkillBonus(PlayerActor(), skillId.c_str(), amount);
    }

    void SetCustomSkillBonus(RE::StaticFunctionTag*, RE::BSFixedString skillId, int amount) {
        Manager::GetSingleton()->SetCustomSkillBonus(PlayerActor(), skillId.c_str(), amount);
    }

    void AddCustomSkillXPForActor(RE::StaticFunctionTag*, int actorFormID, RE::BSFixedString skillId, float xp) {
        Manager::GetSingleton()->AddCustomSkillXPForActorID(static_cast<RE::FormID>(actorFormID), skillId.c_str(), xp);
    }

    int GetCustomSkillLevelForActor(RE::StaticFunctionTag*, int actorFormID, RE::BSFixedString skillId) {
        return Manager::GetSingleton()->GetCustomSkillLevelForActorID(static_cast<RE::FormID>(actorFormID), skillId.c_str());
    }

    float GetCustomSkillXPForActor(RE::StaticFunctionTag*, int actorFormID, RE::BSFixedString skillId) {
        return Manager::GetSingleton()->GetCustomSkillXPForActorID(static_cast<RE::FormID>(actorFormID), skillId.c_str());
    }

    int GetCustomSkillTotalLevelForActor(RE::StaticFunctionTag*, int actorFormID, RE::BSFixedString skillId) {
        return Manager::GetSingleton()->GetCustomSkillTotalLevelForActorID(static_cast<RE::FormID>(actorFormID), skillId.c_str());
    }

    int GetCustomSkillBonusForActor(RE::StaticFunctionTag*, int actorFormID, RE::BSFixedString skillId) {
        return Manager::GetSingleton()->GetCustomSkillBonusForActorID(static_cast<RE::FormID>(actorFormID), skillId.c_str());
    }

    void ModCustomSkillBonusForActor(RE::StaticFunctionTag*, int actorFormID, RE::BSFixedString skillId, int amount) {
        Manager::GetSingleton()->ModCustomSkillBonusForActorID(static_cast<RE::FormID>(actorFormID), skillId.c_str(), amount);
    }

    void SetCustomSkillBonusForActor(RE::StaticFunctionTag*, int actorFormID, RE::BSFixedString skillId, int amount) {
        Manager::GetSingleton()->SetCustomSkillBonusForActorID(static_cast<RE::FormID>(actorFormID), skillId.c_str(), amount);
    }

    bool HasCustomPerkForActor(RE::StaticFunctionTag*, int actorFormID, RE::BSFixedString perkId) {
        return Manager::GetSingleton()->HasCustomPerkForActorID(static_cast<RE::FormID>(actorFormID), perkId.c_str());
    }

    bool AddCustomPerkForActor(RE::StaticFunctionTag*, int actorFormID, RE::BSFixedString perkId) {
        return Manager::GetSingleton()->AddCustomPerkForActorID(static_cast<RE::FormID>(actorFormID), perkId.c_str());
    }

    bool RemoveCustomPerkForActor(RE::StaticFunctionTag*, int actorFormID, RE::BSFixedString perkId) {
        return Manager::GetSingleton()->RemoveCustomPerkForActorID(static_cast<RE::FormID>(actorFormID), perkId.c_str());
    }

    int GetAPIVersion(RE::StaticFunctionTag*) {
        return SkillMenuAPI::Version;
    }

    bool Bind(RE::BSScript::IVirtualMachine* vm) {
        vm->RegisterFunction("AddCustomSkillXP", "NewSkillMenu", AddCustomSkillXP);
        vm->RegisterFunction("GetCustomSkillLevel", "NewSkillMenu", GetCustomSkillLevel);
        vm->RegisterFunction("GetCustomSkillXP", "NewSkillMenu", GetCustomSkillXP);
        vm->RegisterFunction("GetSkillFormulaValue", "NewSkillMenu", GetSkillFormulaValue);
        vm->RegisterFunction("GetCustomSkillTotalLevel", "NewSkillMenu", GetCustomSkillTotalLevel);
        vm->RegisterFunction("GetCustomSkillBonus", "NewSkillMenu", GetCustomSkillBonus);
        vm->RegisterFunction("ModCustomSkillBonus", "NewSkillMenu", ModCustomSkillBonus);
        vm->RegisterFunction("SetCustomSkillBonus", "NewSkillMenu", SetCustomSkillBonus);

        vm->RegisterFunction("AddCustomSkillXPForActor", "NewSkillMenu", AddCustomSkillXPForActor);
        vm->RegisterFunction("GetCustomSkillLevelForActor", "NewSkillMenu", GetCustomSkillLevelForActor);
        vm->RegisterFunction("GetCustomSkillXPForActor", "NewSkillMenu", GetCustomSkillXPForActor);
        vm->RegisterFunction("GetCustomSkillTotalLevelForActor", "NewSkillMenu", GetCustomSkillTotalLevelForActor);
        vm->RegisterFunction("GetCustomSkillBonusForActor", "NewSkillMenu", GetCustomSkillBonusForActor);
        vm->RegisterFunction("ModCustomSkillBonusForActor", "NewSkillMenu", ModCustomSkillBonusForActor);
        vm->RegisterFunction("SetCustomSkillBonusForActor", "NewSkillMenu", SetCustomSkillBonusForActor);
        vm->RegisterFunction("HasCustomPerkForActor", "NewSkillMenu", HasCustomPerkForActor);
        vm->RegisterFunction("AddCustomPerkForActor", "NewSkillMenu", AddCustomPerkForActor);
        vm->RegisterFunction("RemoveCustomPerkForActor", "NewSkillMenu", RemoveCustomPerkForActor);

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
namespace {
    bool hasDFG = false;

    class DynamicFormsGeneratorListener : public RE::BSTEventSink<SKSE::ModCallbackEvent> {
    public:
        static DynamicFormsGeneratorListener* GetSingleton()
        {
            static DynamicFormsGeneratorListener singleton;
            return &singleton;
        }

        void Register()
        {
            if (auto dispatcher = SKSE::GetModCallbackEventSource()) {
                dispatcher->AddEventSink(this);
            }
        }

        RE::BSEventNotifyControl ProcessEvent(const SKSE::ModCallbackEvent* a_event, RE::BSTEventSource<SKSE::ModCallbackEvent>*) override
        {
            if (!a_event) return RE::BSEventNotifyControl::kContinue;

            std::string_view eventName = a_event->eventName.c_str();
            if (eventName == "DynamicFormsGeneratorLoaded") {
                Manager::GetSingleton()->PopulateAllLists();
                return RE::BSEventNotifyControl::kContinue;
            }
            if (eventName == "DynamicFormsGeneratorUpdated") {
                Manager::GetSingleton()->RefreshLists(a_event->strArg.c_str());
                return RE::BSEventNotifyControl::kContinue;
            }

            return RE::BSEventNotifyControl::kContinue;
        }
    };
}

void OnMessage(SKSE::MessagingInterface::Message* message) {
    if (message->type == SKSE::MessagingInterface::kPostLoad) {
        hasDFG = GetModuleHandleA("DynamicFormsGenerator.dll") != nullptr;
        if (hasDFG) {
            logger::info("DynamicFormsGenerator.dll found");
        }
    }
    if (message->type == SKSE::MessagingInterface::kDataLoaded) {
        if (!hasDFG) {
            Manager::GetSingleton()->PopulateAllLists();
        }
        GenerateAllVanillaTrees();
        Manager::GetSingleton()->LoadCustomSkills();
        Prisma::PreloadLocalization();
        PlayerLevel::Register();
        Prisma::Install();
        if (GetModuleHandleA("MouseMode.dll")) {
            Prisma::MouseMode = true;
            logger::info("MouseMode.dll founded");
        }
        else {
            Prisma::MouseMode = false;
            logger::info("MouseMode.dll not found.");
        }
        //TriggerEventHandler::Register();
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
    DynamicFormsGeneratorListener::GetSingleton()->Register();
    return true;
}
