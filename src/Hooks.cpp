#include "Hooks.h"
#include "InputEventHandler.h"
#include "Manager.h"

namespace MenuHooks {

    class LevelUpMenuHook {
    public:
        static RE::UI_MESSAGE_RESULTS ProcessMessage_Hook(RE::LevelUpMenu* a_this, RE::UIMessage& a_message) {

            if (a_message.type == RE::UI_MESSAGE_TYPE::kShow) {
                // 1. Oculta os gráficos (Flash/SWF) do LevelUpMenu vanilla
                if (a_this->uiMovie) {
                    a_this->uiMovie->SetVisible(false);
                    RE::GFxValue alpha(0.0);
                    a_this->uiMovie->SetVariable("_root._alpha", &alpha);
                }
                a_this->menuFlags.set(RE::UI_MENU_FLAGS::kFreezeFrameBackground);
            }else if (a_message.type == RE::UI_MESSAGE_TYPE::kHide) {
                a_this->menuFlags.set(RE::UI_MENU_FLAGS::kFreezeFrameBackground);
                a_this->menuFlags.set(RE::UI_MENU_FLAGS::kPausesGame);
                return RE::UI_MESSAGE_RESULTS::kHandled;
			}
            // Chama a função original para evitar quebrar a pilha de menus da engine
            return _ProcessMessage(a_this, a_message);
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
                a_this->menuFlags.set(RE::UI_MENU_FLAGS::kFreezeFrameBackground);
                a_this->menuFlags.set(RE::UI_MENU_FLAGS::kPausesGame);
                Prisma::Show();
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
            //if (runtimeData.skydomeNode) runtimeData.skydomeNode->local.scale = 0.0f;
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

bool OnInput(RE::InputEvent* event) { 
    if (!event) return false;
    if (event->device != RE::INPUT_DEVICE::kKeyboard) return false;
    auto button = event->AsButtonEvent();
    if (!button) return false;
    if (!button->IsDown()) return false;
    if (!Prisma::IsHidden() && (button->GetIDCode() == RE::BSWin32KeyboardDevice::Keys::kTab || 
        button->GetIDCode() == RE::BSWin32KeyboardDevice::Keys::kEscape)) {
        // Bloqueia o fechamento via TAB se o jogador tiver um level up pendente
        auto player = RE::PlayerCharacter::GetSingleton();
        if (player) {
            auto playerSkills = player->GetPlayerRuntimeData().skills;
            if (playerSkills && playerSkills->CanLevelUp()) {
                logger::debug("OnInput: TAB ignorado pois existe um Level Up pendente.");
                return true; // Bloqueia e consome o input
            }
        }

        logger::debug("OnInput: TAB pressionado enquanto o menu Prisma está aberto. Chamando Prisma::Hide()...");
        Prisma::TriggerExitAnimation();
        return true; // Bloqueia o TAB de fazer outras coisas no jogo
    }
    

    return false;
}


void Hooks::Install() {
    SKSE::AllocTrampoline(64);
    ProcessInputQueueHook::install();
    InputEventHandler::Register(OnInput);
    MenuHooks::StatsMenuHook::Install();
    MenuHooks::LevelUpMenuHook::Install();

}