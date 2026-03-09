#pragma once
#include "PrismaUI_API.h"
#include <nlohmann/json.hpp> 
#include <miniz.h>

class Prisma {
    
public:
    static inline bool createdView = false;
    bool HandleInput(RE::InputEvent* a_event);
    static void PreloadLocalization();
    static void Install();
    static void SendUpdateToUI();
    static void Show();
    static void TriggerExitAnimation();
    static void Hide();
    static bool IsHidden();
};

