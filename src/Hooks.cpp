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
                //return RE::UI_MESSAGE_RESULTS::kHandled;
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
    auto button = event->AsButtonEvent();
    if (!button) return false;
    if (!button->IsDown()) return false;
    if (!Prisma::IsHidden()) {
        auto userEvents = RE::UserEvents::GetSingleton();
        auto action = button->QUserEvent();

        // Se a ação for ESC (cancel) ou TAB (tweenMenu)...
        if (action == userEvents->cancel || action == userEvents->tweenMenu) {
            if (Prisma::IsLevelUpMenuOpen()) {
                logger::debug("OnInput: Voltar ignorado pois o menu de Level Up esta ativo.");
                return true; // Bloqueia e consome o input
            }
            logger::debug("OnInput: Botao Voltar pressionado. Chamando Prisma::TriggerBack()...");
            Prisma::TriggerBack(); // Avisa o JavaScript
        }
        if (action == userEvents->up) { Prisma::SendKeyPress("w"); return true; }
        if (action == userEvents->down) { Prisma::SendKeyPress("s"); return true; }
        if (action == userEvents->left) { Prisma::SendKeyPress("a"); return true; }
        if (action == userEvents->right) { Prisma::SendKeyPress("d"); return true; }
        if (action == userEvents->accept) { Prisma::SendKeyPress("enter"); return true; }
    }

    
    return false;
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
        Prisma::Show();
        return BSEventNotifyControl::kContinue;
    }
};

void Hooks::Install() {
    SKSE::AllocTrampoline(64);
    ProcessInputQueueHook::install();
    InputEventHandler::Register(OnInput);
    //MenuHooks::StatsMenuHook::Install();
    //MenuHooks::LevelUpMenuHook::Install();
    static MenuEvents menuSink;
    static ModEvents modSink;

    RE::UI::GetSingleton()->AddEventSink<MenuOpenCloseEvent>(&menuSink);
    SKSE::GetModCallbackEventSource()->AddEventSink(&modSink);
}
