#pragma once
#include "PrismaUI_API.h"
#include <miniz.h>

class Prisma {
    
public:
    static inline bool createdView = false;
    static inline bool MouseMode = false;
    bool HandleInput(RE::InputEvent* a_event);
    static void SendKeyPress(const std::string& key);
    static void PreloadLocalization();
    static void Install();
    static void SendUpdateToUI();
    static void Show();
    static void TriggerExitAnimation();
    static void Hide();
    static bool IsHidden();
    static void SetLevelUpMenuOpen(bool isOpen);
    static bool IsLevelUpMenuOpen();
    static void TriggerBack();
};





