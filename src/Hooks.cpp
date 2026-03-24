#include "Hooks.h"
#include "InputEventHandler.h"
#include "Manager.h"

extern nlohmann::json GetSettings();


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
                //a_this->menuFlags.set(RE::UI_MENU_FLAGS::kFreezeFrameBackground);
                logger::debug("LevelUpMenu: ProcessMessage kShow recebido. MenuFlags atualizados para congelar o fundo.");
                Prisma::SetLevelUpMenuOpen(true);
                logger::debug("[DEBUG] Chamando SendUpdateToUI via LevelUpMenuHook (kShow)"); 
                Prisma::SendUpdateToUI();
            }
            else if (a_message.type == RE::UI_MESSAGE_TYPE::kHide) {
                Prisma::SetLevelUpMenuOpen(false);
                logger::debug("[DEBUG] Chamando SendUpdateToUI via LevelUpMenuHook (kHide)"); 
                Prisma::SendUpdateToUI();
                //a_this->menuFlags.set(RE::UI_MENU_FLAGS::kFreezeFrameBackground);
                //a_this->menuFlags.set(RE::UI_MENU_FLAGS::kPausesGame);
                return RE::UI_MESSAGE_RESULTS::kHandled;
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

bool OnInput(RE::InputEvent* event) { 
    if (!event) return false;
    if (event->device != RE::INPUT_DEVICE::kKeyboard) return false;
    auto button = event->AsButtonEvent();
    if (!button) return false;
    if (!button->IsDown()) return false;
    if (!Prisma::IsHidden()) {
        auto userEvents = RE::UserEvents::GetSingleton();
        auto action = button->QUserEvent();

        // Se a ação for ESC (cancel) ou TAB (tweenMenu)...
        if (action == userEvents->cancel || action == userEvents->tweenMenu) {

            // Bloqueia o fechamento via TAB/ESC apenas se o Menu de Level Up estiver aberto
            if (Prisma::IsLevelUpMenuOpen()) {
                logger::debug("OnInput: Voltar ignorado pois o menu de Level Up esta ativo.");
                return true; // Bloqueia e consome o input
            }
            logger::debug("OnInput: Botao Voltar pressionado. Chamando Prisma::TriggerBack()...");
            Prisma::TriggerBack(); // Avisa o JavaScript
        }
        return true;
    }
    /*if (button->GetIDCode() == RE::BSWin32KeyboardDevice::Keys::kF3) {
        auto player = RE::PlayerCharacter::GetSingleton();
        player->AddSkillExperience(RE::ActorValue::kHeavyArmor, 1000.0f);
        Manager::GetSingleton()->AddCustomSkillXP("testarone", 1000.0f);
        return true;
    }*/
    
    

    return false;
}


void Hooks::Install() {
    SKSE::AllocTrampoline(64);
    ProcessInputQueueHook::install();
    InputEventHandler::Register(OnInput);
    MenuHooks::StatsMenuHook::Install();
    MenuHooks::LevelUpMenuHook::Install();

}