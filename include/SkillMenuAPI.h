#pragma once
#include <stdint.h>

namespace SkillMenuAPI {
    constexpr const auto Name = "SkillMenuAPI";
    constexpr const uint32_t Version = 1;

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
    };
}
