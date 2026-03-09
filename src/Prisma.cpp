#include "Prisma.h"
#include "PrismaUI_API.h"

PRISMA_UI_API::IVPrismaUI1* PrismaUI = nullptr;
static PrismaView view;

void Prisma::Install() {
    PrismaUI = reinterpret_cast<PRISMA_UI_API::IVPrismaUI1*>(PRISMA_UI_API::RequestPluginAPI());


    PrismaUI->RegisterJSListener(view, "hideWindow", [](const char* data) -> void {
        PrismaUI->Hide(view);
    });
}

void Prisma::Show() {

    if (!createdView) {
        createdView = true;

        #ifdef DEV_SERVER
        constexpr const char* path = "http://localhost:5173";
        #else
        constexpr const char* path = PRODUCT_NAME "/index.html";
        #endif


        view = PrismaUI->CreateView(path, [](PrismaView view) -> void { 
            PrismaUI->Focus(view, true);
        });

        return;
    }
    PrismaUI->Show(view);
    PrismaUI->Focus(view, true);
    RE::UIBlurManager::GetSingleton()->IncrementBlurCount();
}

void Prisma::Hide() {
    PrismaUI->Unfocus(view);
    PrismaUI->Hide(view); 
    RE::UIBlurManager::GetSingleton()->DecrementBlurCount();
}

bool Prisma::IsHidden() { return PrismaUI->IsHidden(view); }
