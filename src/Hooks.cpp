#include "Hooks.h"
#include "InputEventHandler.h"
#include "Manager.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <Windows.h>

extern nlohmann::json GetSettings();

static void ApplyInitialInputModeForMenuOpen();

namespace MenuHooks {

    class LevelUpMenuHook {
    public:
        static RE::UI_MESSAGE_RESULTS ProcessMessage_Hook(RE::LevelUpMenu* a_this, RE::UIMessage& a_message) {
            auto result = _ProcessMessage(a_this, a_message);
            if (a_message.type == RE::UI_MESSAGE_TYPE::kShow) {
                // 1. Oculta os gráficos (Flash/SWF) do LevelUpMenu vanilla
                if (a_this->uiMovie) {
                    a_this->uiMovie->SetVisible(false);
                    RE::GFxValue alpha(0.0);
                    a_this->uiMovie->SetVariable("_root._alpha", &alpha);
                }
            }
            else if (a_message.type == RE::UI_MESSAGE_TYPE::kHide) {
			}
            // Chama a função original para evitar quebrar a pilha de menus da engine
            return result;
        }

        static void Install() {
            // O índice de ProcessMessage no IMenu é 4
            REL::Relocation<std::uintptr_t> vtable(RE::VTABLE_LevelUpMenu[0]);
            _ProcessMessage = vtable.write_vfunc(0x4, ProcessMessage_Hook);

            logger::info("Hook na VTable do LevelUpMenu instalado com sucesso!");
        }

    private:
        static inline REL::Relocation<decltype(ProcessMessage_Hook)> _ProcessMessage;
    };

    class StatsMenuHook {
    public:
        // Essa função substituirá o ProcessMessage original do StatsMenu
        static RE::UI_MESSAGE_RESULTS ProcessMessage_Hook(RE::StatsMenu* a_this, RE::UIMessage& a_message) {

            if (a_message.type == RE::UI_MESSAGE_TYPE::kShow) {
                //a_this->menuFlags.set(RE::UI_MENU_FLAGS::kFreezeFramePause);
                ApplyInitialInputModeForMenuOpen();
                Prisma::Show();
                ApplyInitialInputModeForMenuOpen();
            }
            else if (a_message.type == RE::UI_MESSAGE_TYPE::kHide) {
                Prisma::Hide();
            }

            // 1. Chamamos a função original PRIMEIRO. 
            // Isso permite que o Skyrim processe a câmera e tente rodar o "fadeIn" original.
            auto result = _ProcessMessage(a_this, a_message);

            // 2. AGORA ocultamos a interface vanilla. 
            // Como fazemos isso depois do _ProcessMessage, nós sobrescrevemos a animação da engine.
            if (a_this->uiMovie) {
                // Tenta ocultar a renderização do SWF
                a_this->uiMovie->SetVisible(false);

                // Garantia extra: Força a opacidade do menu original para 0
                RE::GFxValue alpha(0.0);
                a_this->uiMovie->SetVariable("_root._alpha", &alpha);
            }
            
            auto& runtimeData = a_this->GetRuntimeData();
            if (runtimeData.skydomeNode) runtimeData.skydomeNode->local.scale = 0.0f;
            if (runtimeData.starsNode) runtimeData.starsNode->local.scale = 0.0f;
            if (runtimeData.linesNode) runtimeData.linesNode->local.scale = 0.0f;
            return result;
        }

        static void Install() {
            // StatsMenu::VTABLE[0] contém as funções de IMenu. ProcessMessage é o índice 4.
            REL::Relocation<std::uintptr_t> vtable(RE::VTABLE_StatsMenu[0]);
            _ProcessMessage = vtable.write_vfunc(0x4, ProcessMessage_Hook);

            logger::info("Hook na VTable do StatsMenu instalado com sucesso!");
        }

    private:
        static inline REL::Relocation<decltype(ProcessMessage_Hook)> _ProcessMessage;
    };
}

struct ProcessInputQueueHook {
    static void thunk(RE::BSTEventSource<RE::InputEvent*>* a_dispatcher, RE::InputEvent* const* a_event) {
        a_event = InputEventHandler::Process(const_cast<RE::InputEvent**>(a_event));
        originalFunction(a_dispatcher, a_event);
    }
    static inline REL::Relocation<decltype(thunk)> originalFunction;
    static void install() {
        auto& trampoline = SKSE::GetTrampoline();
        originalFunction = trampoline.write_call<5>(REL::RelocationID(67315, 68617, 67315).address() + REL::Relocate(0x7B, 0x7B, 0x81), thunk);
    }
};

static std::string ToLowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

static void SendInputMode(const char* mode);

static bool SendControllerActionThrottled(const char* action, uint64_t delayMs = 160) {
    static uint64_t lastSentMs = 0;
    static std::string lastAction;

    const uint64_t now = GetTickCount64();
    SendInputMode("controller");
    if (lastAction == action && now - lastSentMs < delayMs) {
        return true;
    }

    lastAction = action;
    lastSentMs = now;

    Prisma::SendKeyPress(std::string("nsm:") + action);
    return true;
}

static bool SendControllerAnalogActionThrottled(const char* action, uint64_t delayMs = 180) {
    static uint64_t lastAnalogSentMs = 0;

    const uint64_t now = GetTickCount64();
    SendInputMode("controller");

    if (now - lastAnalogSentMs < delayMs) {
        return true;
    }

    lastAnalogSentMs = now;
    Prisma::SendKeyPress(std::string("nsm:") + action);
    return true;
}

static bool StringContains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

static bool g_nsmControllerPointerMode = false;
static float g_savedCursorX = 0.0f;
static float g_savedCursorY = 0.0f;
static bool g_hasSavedCursorPosition = false;
static bool g_lastInputWasController = false;

static void SetPrismaCursorHiddenByInputMode(bool hide) {
    auto cursor = RE::MenuCursor::GetSingleton();
    if (!cursor) {
        g_nsmControllerPointerMode = hide;
        return;
    }

    if (hide) {
        if (!g_nsmControllerPointerMode) {
            g_savedCursorX = cursor->cursorPosX;
            g_savedCursorY = cursor->cursorPosY;
            g_hasSavedCursorPosition = true;
        }

        g_nsmControllerPointerMode = true;

        cursor->cursorPosX = -10000.0f;
        cursor->cursorPosY = -10000.0f;
        return;
    }

    if (g_nsmControllerPointerMode && g_hasSavedCursorPosition) {
        cursor->cursorPosX = g_savedCursorX;
        cursor->cursorPosY = g_savedCursorY;
    }

    g_nsmControllerPointerMode = false;
}

static void MaintainControllerCursorHidden() {
    if (!g_nsmControllerPointerMode) {
        return;
    }

    auto cursor = RE::MenuCursor::GetSingleton();
    if (!cursor) {
        return;
    }

    cursor->cursorPosX = -10000.0f;
    cursor->cursorPosY = -10000.0f;
}

static void SendInputMode(const char* mode) {
    static std::string lastMode;
    static uint64_t lastSentMs = 0;

    const uint64_t now = GetTickCount64();
    if (lastMode == mode && now - lastSentMs < 250) {
        return;
    }

    lastMode = mode;
    lastSentMs = now;

    const bool mouseMode = std::string_view(mode) == "mouse";

    Prisma::SetInputCaptureForPointerMode(mouseMode);
    SetPrismaCursorHiddenByInputMode(!mouseMode);

    Prisma::SendKeyPress(std::string("nsm:input_") + mode);
}

static void RememberInputDevice(RE::InputEvent* event) {
    if (!event) {
        return;
    }

    const auto device = event->GetDevice();
    if (device == RE::INPUT_DEVICE::kGamepad) {
        g_lastInputWasController = true;
        return;
    }

    if (device == RE::INPUT_DEVICE::kKeyboard || device == RE::INPUT_DEVICE::kMouse) {
        g_lastInputWasController = false;
    }
}

static void ApplyInitialInputModeForMenuOpen() {
    if (g_lastInputWasController) {
        SetPrismaCursorHiddenByInputMode(true);
        Prisma::SetInputCaptureForPointerMode(false);
        Prisma::SendKeyPress("nsm:input_controller");
    } else {
        SetPrismaCursorHiddenByInputMode(false);
        Prisma::SetInputCaptureForPointerMode(true);
        Prisma::SendKeyPress("nsm:input_mouse");
    }
}

static bool HandleKeyboardMouseInput(RE::InputEvent* event, RE::UserEvents* userEvents) {
    if (!event || !userEvents) return false;

    SendInputMode("mouse");

    auto button = event->AsButtonEvent();
    if (!button || !button->IsDown()) {
        return false;
    }

    const auto action = button->QUserEvent();
    const std::string actionName = ToLowerAscii(action.c_str());
    const uint32_t idCode = button->GetIDCode();
    const bool isMouseButton = button->GetDevice() == RE::INPUT_DEVICE::kMouse;

    if (isMouseButton) {
        return false;
    }

    if (action == userEvents->cancel ||
        action == userEvents->tweenMenu ||
        StringContains(actionName, "cancel") ||
        StringContains(actionName, "tweenmenu") ||
        StringContains(actionName, "tween menu") ||
        StringContains(actionName, "journal") ||
        StringContains(actionName, "escape") ||
        StringContains(actionName, "tab") ||
        idCode == 1 ||   // Escape
        idCode == 15) {  // Tab
        Prisma::TriggerBack();
        return true;
    }

    return false;
}

static bool HandleControllerButtonInput(RE::ButtonEvent* button, RE::UserEvents* userEvents) {
    if (!button || !userEvents || !button->IsDown()) return false;

    const auto action = button->QUserEvent();
    const std::string actionName = ToLowerAscii(action.c_str());
    const uint32_t idCode = button->GetIDCode();

    logger::debug("NSM gamepad button: id={} action='{}'", idCode, actionName);

    if (action == userEvents->cancel || action == userEvents->tweenMenu ||
        StringContains(actionName, "cancel") || StringContains(actionName, "tweenmenu") ||
        StringContains(actionName, "back")) {
        if (Prisma::IsLevelUpMenuOpen()) {
            logger::debug("OnInput: Voltar ignorado pois o menu de Level Up esta ativo.");
            return true;
        }
        logger::debug("OnInput: Botao Voltar pressionado. Chamando Prisma::TriggerBack()...");
        Prisma::TriggerBack();
        return true;
    }

    if (action == userEvents->up || actionName == "up" || StringContains(actionName, "dpad up")) {
        return SendControllerActionThrottled("move_up", 80);
    }
    if (action == userEvents->down || actionName == "down" || StringContains(actionName, "dpad down")) {
        return SendControllerActionThrottled("move_down", 80);
    }
    if (action == userEvents->left || actionName == "left" || StringContains(actionName, "dpad left")) {
        return SendControllerActionThrottled("move_left", 80);
    }
    if (action == userEvents->right || actionName == "right" || StringContains(actionName, "dpad right")) {
        return SendControllerActionThrottled("move_right", 80);
    }

    if (action == userEvents->accept || idCode == 0x1000 || idCode == 4096 ||
        StringContains(actionName, "accept") || StringContains(actionName, "activate") ||
        StringContains(actionName, "select") || StringContains(actionName, "jump")) {
        return SendControllerActionThrottled("confirm", 120);
    }

    if (idCode == 9 || idCode == 0x0009 ||
        StringContains(actionName, "left attack") || StringContains(actionName, "left hand") ||
        StringContains(actionName, "left trigger") || StringContains(actionName, "ltrigger") ||
        StringContains(actionName, "block") || StringContains(actionName, "zoom out")) {
        return SendControllerActionThrottled("page_left", 120);
    }

    if (idCode == 10 || idCode == 0x000A ||
        StringContains(actionName, "right attack") || StringContains(actionName, "right hand") ||
        StringContains(actionName, "right trigger") || StringContains(actionName, "rtrigger") ||
        StringContains(actionName, "attack") || StringContains(actionName, "zoom in")) {
        return SendControllerActionThrottled("page_right", 120);
    }

    if (idCode == 0x0100 || idCode == 256 || StringContains(actionName, "left shoulder") || StringContains(actionName, "left bumper")) {
        return SendControllerActionThrottled("rank_left", 120);
    }
    if (idCode == 0x0200 || idCode == 512 || StringContains(actionName, "right shoulder") || StringContains(actionName, "right bumper")) {
        return SendControllerActionThrottled("rank_right", 120);
    }

    logger::info("NSM unmapped gamepad button: id={} action='{}'", idCode, actionName);
    return false;
}

static bool HandleControllerThumbstickInput(RE::ThumbstickEvent* stick) {
    if (!stick) return false;

    if (!stick->IsLeft()) return true;

    constexpr float deadzone = 0.85f;
    const float x = stick->xValue;
    const float y = stick->yValue;

    if (std::hypot(x, y) < deadzone) {
        return true;
    }

    char action[64];
    std::snprintf(action, sizeof(action), "analog_move:%.3f:%.3f", x, y);
    return SendControllerAnalogActionThrottled(action, 180);
}

bool OnInput(RE::InputEvent* event) { 
    if (!event) return false;

    RememberInputDevice(event);

    if (Prisma::IsHidden()) return false;

    MaintainControllerCursorHidden();

    auto userEvents = RE::UserEvents::GetSingleton();
    if (!userEvents) return false;

    if (event->GetDevice() != RE::INPUT_DEVICE::kGamepad) {
        return HandleKeyboardMouseInput(event, userEvents);
    }

    if (auto button = event->AsButtonEvent()) {
        return HandleControllerButtonInput(button, userEvents);
    }

    if (auto stick = event->AsThumbstickEvent()) {
        return HandleControllerThumbstickInput(stick);
    }

    return true;
}

using namespace RE;
void Inject(std::string_view a_menuName) {
    const auto ui = RE::UI::GetSingleton();
    if (!ui) return;

    const auto menu = ui->GetMenu(a_menuName);
    if (!menu) {
        return;
    }

    const auto movie = menu->uiMovie;
    if (!movie) {
        return;
    }

    RE::GFxValue _root;
    movie->GetVariable(&_root, "_root");

    RE::GFxValue args[2];
    args[0] = RE::GFxValue("NoStats");
    args[1] = RE::GFxValue(1298);
    _root.Invoke("createEmptyMovieClip", nullptr, args, 2);
    if (movie->GetVariable(&_root, "_root.NoStats")) {
        RE::GFxValue args2[1];
        args2[0] = RE::GFxValue("nostats_inject.swf");
        _root.Invoke("loadMovie", nullptr, args2, 1);
    }
}


class MenuEvents : public RE::BSTEventSink<MenuOpenCloseEvent> {
public:
    BSEventNotifyControl ProcessEvent(const MenuOpenCloseEvent* event, BSTEventSource<MenuOpenCloseEvent>*) {
        if (event->opening && event->menuName == TweenMenu::MENU_NAME) {
            Inject(TweenMenu::MENU_NAME);
        }
        else if (event->opening && event->menuName == "PrismaUI_FocusMenu") {
            auto ui = RE::UI::GetSingleton();
            if (ui && !Prisma::IsHidden()) {
                auto focusMenu = ui->GetMenu("PrismaUI_FocusMenu");
                if (focusMenu) {
                    focusMenu->menuFlags.set(RE::UI_MENU_FLAGS::kFreezeFrameBackground, RE::UI_MENU_FLAGS::kTopmostRenderedMenu);
                }
            }
        }
        return BSEventNotifyControl::kContinue;
    }
};

class ModEvents : public RE::BSTEventSink<SKSE::ModCallbackEvent> {
public:
    BSEventNotifyControl ProcessEvent(const SKSE::ModCallbackEvent* a_event,
        RE::BSTEventSource<SKSE::ModCallbackEvent>*) {
        if (!a_event || a_event->eventName != "NSM_Open"sv) return BSEventNotifyControl::kContinue;
		logger::info("NSM_Open event received, showing Prisma...");
        auto msgQueue = RE::UIMessageQueue::GetSingleton();
        if (msgQueue) {
            // 2. Envia o comando para esconder (kHide) o TweenMenu
            msgQueue->AddMessage(RE::TweenMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
        }
        ApplyInitialInputModeForMenuOpen();
        Prisma::Show();
        ApplyInitialInputModeForMenuOpen();
        return BSEventNotifyControl::kContinue;
    }
};

void Hooks::Install() {
    SKSE::AllocTrampoline(64);
    ProcessInputQueueHook::install();
    InputEventHandler::Register(OnInput);
    MenuHooks::StatsMenuHook::Install();
    MenuHooks::LevelUpMenuHook::Install();
    static MenuEvents menuSink;
    static ModEvents modSink;

    RE::UI::GetSingleton()->AddEventSink<MenuOpenCloseEvent>(&menuSink);
    SKSE::GetModCallbackEventSource()->AddEventSink(&modSink);
}
