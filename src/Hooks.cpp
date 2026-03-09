#include "Hooks.h"
#include "InputEventHandler.h"
#include "Prisma.h"

struct ProcessInputQueueHook {
    static void thunk(RE::BSTEventSource<RE::InputEvent*>* a_dispatcher, RE::InputEvent* const* a_event) {
        a_event = InputEventHandler::Process(const_cast<RE::InputEvent**>(a_event));
        originalFunction(a_dispatcher, a_event);
    }
    static inline REL::Relocation<decltype(thunk)> originalFunction;
    static void install() {
        SKSE::AllocTrampoline(14);
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
    if (button->GetIDCode() == RE::BSWin32KeyboardDevice::Keys::kF2) {
        if (Prisma::IsHidden()) {
            Prisma::Show();
        } else {
            Prisma::Hide();
        }
        return true;
    }
    return false;
}



void Hooks::Install() {
    ProcessInputQueueHook::install();
    InputEventHandler::Register(OnInput);
}