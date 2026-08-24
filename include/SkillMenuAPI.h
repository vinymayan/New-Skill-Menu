#pragma once
#include <stdint.h>
#include "RE/T/TESForm.h"

namespace SkillMenuAPI {
    constexpr const auto Name = "SkillMenuAPI";
    constexpr const uint32_t Version = 5;

    struct SkillListView {
        const char* const* items;
        uint32_t count;
    };

    // A estrutura de interface que seu mod vai expor
    struct Interface {
        uint32_t interfaceVersion;

        // Retorna o nível atual de uma skill customizada
        int (*GetCustomSkillLevel)(const char* skillId);

        // Adiciona XP a uma skill customizada e trata o Level Up
        void (*AddCustomSkillXP)(const char* skillId, float xpAmount);

        // Retorna a quantidade de XP atual da barra
        float (*GetCustomSkillXP)(const char* skillId);

        float (*GetSkillFormulaValue)(const char* skillId, int valueType);
        // valueType: 0=useMult, 1=useOffset, 2=improveMult, 3=improveOffset

        // ================= V2 API =================
        // Retorna Nível Base + Bônus
        int (*GetCustomSkillTotalLevel)(const char* skillId);

        // Retorna apenas o Bônus atual
        int (*GetCustomSkillBonus)(const char* skillId);

        // Modifica o Bônus (pode ser negativo para penalidades)
        void (*ModCustomSkillBonus)(const char* skillId, int amount);

        // Define o Bônus para um valor exato
        void (*SetCustomSkillBonus)(const char* skillId, int amount);

        // ================= V3 API =================
        void (*AddCustomSkillXPForActor)(RE::FormID actorFormID, const char* skillId, float xpAmount);
        int (*GetCustomSkillLevelForActor)(RE::FormID actorFormID, const char* skillId);
        float (*GetCustomSkillXPForActor)(RE::FormID actorFormID, const char* skillId);
        int (*GetCustomSkillTotalLevelForActor)(RE::FormID actorFormID, const char* skillId);
        int (*GetCustomSkillBonusForActor)(RE::FormID actorFormID, const char* skillId);
        void (*ModCustomSkillBonusForActor)(RE::FormID actorFormID, const char* skillId, int amount);
        void (*SetCustomSkillBonusForActor)(RE::FormID actorFormID, const char* skillId, int amount);
        bool (*HasCustomPerkForActor)(RE::FormID actorFormID, const char* perkId);
        bool (*AddCustomPerkForActor)(RE::FormID actorFormID, const char* perkId);
        bool (*RemoveCustomPerkForActor)(RE::FormID actorFormID, const char* perkId);

        // V4: actor-specific economy.
        int (*GetActorPerkPoints)(RE::FormID actorFormID);
        int (*ModActorPerkPoints)(RE::FormID actorFormID, int amount);
        float (*GetActorResource)(RE::FormID actorFormID, const char* resourceId);
        bool (*ModActorResource)(
            RE::FormID actorFormID,
            const char* resourceId,
            float amount);

        // V5: each view is valid until the next call to its respective getter.
        SkillListView (*GetAvailableSkills)();
        SkillListView (*GetAvailableResources)();
    };
}
