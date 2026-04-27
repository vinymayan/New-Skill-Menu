#pragma once
#include "Prisma.h"
namespace Hooks {
    // Nossa classe que irá "ouvir" sempre que o Skyrim tentar abrir ou fechar um menu
    class MenuInterceptor : public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
    public:
        static MenuInterceptor* GetSingleton() {
            static MenuInterceptor singleton;
            return &singleton;
        }

        RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override;
    };
    void InstallMenuOverrides();
    void Install();
}


class PlayerLevel : public RE::BSTEventSink<RE::LevelIncrease::Event> {
public:
    static PlayerLevel* GetSingleton() {
        static PlayerLevel singleton;
        return &singleton;
    }

    // A assinatura deve receber 'RE::LevelIncrease::Event'
    RE::BSEventNotifyControl ProcessEvent(const RE::LevelIncrease::Event* a_event, RE::BSTEventSource<RE::LevelIncrease::Event>*) override {
        if (!a_event) {
            return RE::BSEventNotifyControl::kContinue;
        }

        logger::info("Player subiu para o nível {}. Iniciando scan de regras para atores próximos.", a_event->newLevel);

        // 1. Aplicar regras ao próprio Player
        auto player = RE::PlayerCharacter::GetSingleton();
        if (player) {

        }


        return RE::BSEventNotifyControl::kContinue;
    }

    static void Register() {
        // CORREÇÃO: Usar a fonte dedicada definida em LevelIncrease.h, não o ScriptEventSourceHolder
        auto eventSource = RE::LevelIncrease::GetEventSource();
        if (eventSource) {
            eventSource->AddEventSink(GetSingleton());
            logger::info("PlayerLevel sink registrado com sucesso.");
        }
        else {
            logger::error("Falha ao obter RE::LevelIncrease::GetEventSource()!");
        }
    }
};

