#include "Manager.h"

void Manager::PopulateAllLists() {
    if (_isPopulated) return;

    logger::info("Iniciando escaneamento de FormTypes...");

    PopulateList<RE::TESRace>("Race", [](RE::TESRace* race) -> bool {
        return race->GetPlayable();
        });

    // Filtro para Perk: Verifica a flag kNonPlayable e o data.playable
    PopulateList<RE::BGSPerk>("Perk", [](RE::BGSPerk* perk) -> bool {
        if ((perk->formFlags & RE::BGSPerk::RecordFlags::kNonPlayable) != 0) return false;
        if (!perk->data.playable) return false;
        return true;
        });

    PopulateList<RE::TESQuest>("Quest", [](RE::TESQuest* quest) -> bool {
        return quest != nullptr;
        });

    PopulateList<RE::SpellItem>("Spell", [](RE::SpellItem* spell) -> bool {
        return spell != nullptr;
        });

    PopulateList<RE::BGSLocation>("Location", [](RE::BGSLocation* loc) -> bool {
        return loc != nullptr && loc->worldLocMarker;
        });

    PopulateList<RE::TESFaction>("Faction", [](RE::TESFaction* faction) -> bool {
        return faction != nullptr;
        });

    /*PopulateList<RE::TESFaction>("Faction");*/
    /*PopulateList<RE::SpellItem>("Spell");
    PopulateList<RE::TESShout>("Shout");
    PopulateList<RE::TESNPC>("NPC");
    PopulateList<RE::TESObjectWEAP>("Weapon");
    PopulateList<RE::TESObjectARMO>("Armor");*/
    
    // --- NOVOS TIPOS ADICIONADOS ---
    /*PopulateList<RE::AlchemyItem>("Potion");
    PopulateList<RE::IngredientItem>("Ingredient");
    PopulateList<RE::ScrollItem>("Scroll");
    PopulateList<RE::TESObjectBOOK>("Book");
    PopulateList<RE::TESAmmo>("Ammo");
    PopulateList<RE::TESObjectMISC>("Misc");
    PopulateList<RE::TESKey>("Key");
    PopulateList<RE::TESClass>("Class");
    PopulateList<RE::BGSLocation>("Location");*/
    _isPopulated = true;
    for (auto cb : _readyCallbacks) {
        if (cb) cb();
    }
    _readyCallbacks.clear();
}

const std::vector<InternalFormInfo>& Manager::GetList(const std::string& typeName) {
    static std::vector<InternalFormInfo> empty;
    auto it = _dataStore.find(typeName);
    if (it != _dataStore.end()) {
        return it->second;
    }
    return empty;
}

void Manager::RegisterReadyCallback(std::function<void()> callback) {
    if (_isPopulated) {
        callback();
    } else {
        _readyCallbacks.push_back(callback);
    }
}

bool IsValidUTF8(const std::string& string) {
    int c, i, ix, n, j;
    for (i = 0, ix = string.length(); i < ix; i++) {
        c = (unsigned char)string[i];
        if (c <= 0x7f) n = 0; // 0bbbbbbb
        else if ((c & 0xE0) == 0xC0) n = 1; // 110bbbbb
        else if (c == 0xED && i < (ix - 1) && ((unsigned char)string[i + 1] & 0xA0) == 0xA0) return false; // U+d800 to U+dfff
        else if ((c & 0xF0) == 0xE0) n = 2; // 1110bbbb
        else if ((c & 0xF8) == 0xF0) n = 3; // 11110bbb
        else return false;
        for (j = 0; j < n && i < ix; j++) { // n bytes matching 10bbbbbb
            if ((++i == ix) || (((unsigned char)string[i] & 0xC0) != 0x80))
                return false;
        }
    }
    return true;
}

// Altere a implementação:
std::string Manager::ToUTF8(std::string_view a_str) {
    if (a_str.empty()) return "";

    std::string srcString(a_str);

    // 1. VERIFICAÇÃO DE SEGURANÇA: Se já for UTF-8 válido, não converta!
    // Isso corrige o problema onde o Skyrim SE já manda UTF-8 e o código quebrava.
    if (IsValidUTF8(srcString)) {
        return srcString;
    }

    // 2. Se não for UTF-8 (é ANSI legado do Windows, comum em mods antigos ou RU/CN), converta.
    // Nota: Para Russo específico, se CP_ACP (padrão do sistema) não funcionar, 
    // troque CP_ACP por 1251 (Cyrillic) hardcoded aqui.
    int wlen = MultiByteToWideChar(CP_ACP, 0, srcString.c_str(), -1, nullptr, 0);
    if (wlen <= 0) return srcString; // Falha, retorna original

    std::wstring wstr(wlen, 0);
    MultiByteToWideChar(CP_ACP, 0, srcString.c_str(), -1, &wstr[0], wlen);

    int u8len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (u8len <= 0) return srcString;

    std::string u8str(u8len, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &u8str[0], u8len, nullptr, nullptr);

    // Remove null terminator extra se houver
    if (!u8str.empty() && u8str.back() == '\0') u8str.pop_back();

    return u8str;
}

template <typename T>
void Manager::PopulateList(const std::string& a_typeName, std::function<bool(T*)> a_filter) {
    auto dataHandler = RE::TESDataHandler::GetSingleton();
    if (!dataHandler) return;

    auto& list = _dataStore[a_typeName];
    list.clear();

    const auto& forms = dataHandler->GetFormArray<T>();
    list.reserve(forms.size());

    for (const auto& form : forms) {
        if (!form) continue;

        if (form->IsDeleted() || form->IsIgnored()) {
            continue;
        }

        if (a_filter && !a_filter(form)) {
            continue;
        }
        // Variáveis de auxílio para o log de erro caso o catch seja acionado
        RE::FormID currentID = 0;
        std::string currentPlugin = "Unknown";

        try {
            currentID = form->GetFormID();

            // Obtém o nome do plugin de origem antes de qualquer processamento complexo
            if (auto file = form->GetFile(0)) {
                currentPlugin = std::string(file->GetFilename());
            }
            else {
                currentPlugin = "Dynamic";
            }

            InternalFormInfo info;
            info.formID = currentID;
            info.formType = a_typeName;
            info.pluginName = ToUTF8(currentPlugin);

            // EditorID: clib_util pode lançar exceções em contextos raros de memória
            std::string rawEditorID = clib_util::editorID::get_editorID(form);
            info.editorID = ToUTF8(rawEditorID);

            std::string rawName = "";
            if (form->Is(RE::FormType::NPC)) {
                if (auto npc = form->As<RE::TESNPC>()) {
                    rawName = npc->fullName.c_str();
                }
            }
            else if (auto fullName = form->As<RE::TESFullName>()) {
                rawName = fullName->fullName.c_str();
            }

            // A conversão UTF-8 é um ponto comum de falha se a string estiver corrompida
            info.name = ToUTF8(rawName);

            // NOVO: Pegar a Descrição e o Próximo Perk (se for BGSPerk)
            info.description = "";
            info.nextPerkId = "";
            if (auto perk = form->As<RE::BGSPerk>()) {
                // Pega a descrição (Herda de TESDescription)
                RE::BSString descStr;
                perk->TESDescription::GetDescription(descStr, perk);
                info.description = ToUTF8(descStr.c_str());


                // Pega a ID do Rank seguinte
                if (perk->nextPerk) {
                    auto npFile = perk->nextPerk->GetFile(0);
                    // CORREÇÃO: Forçando a conversão explícita para std::string
                    std::string npPlugin = npFile ? std::string(npFile->GetFilename()) : "Dynamic";
                    uint32_t npLocalID = (perk->nextPerk->GetFormID() & 0xFF000000) == 0xFE000000 ? (perk->nextPerk->GetFormID() & 0xFFF) : (perk->nextPerk->GetFormID() & 0xFFFFFF);
                    info.nextPerkId = fmt::format("{}|{:X}", npPlugin, npLocalID);
                }
            }

            list.push_back(info);
        }
        catch (const std::exception& e) {
            // Log detalhado com FormID em Hexadecimal e o erro específico
            logger::error("[PopulateList] Critical error on item {:08X} of plugin '{}' (Type: {}). Error: {}",
                currentID, currentPlugin, a_typeName, e.what());
        }
        catch (...) {
            // Captura erros desconhecidos que não herdam de std::exception
            logger::error("[PopulateList] Uknown error on item {:08X} of plugin '{}' (Type: {})",
                currentID, currentPlugin, a_typeName);
        }
    }
    logger::info("Carregados {} itens do tipo {}", list.size(), a_typeName);
}

// Declare a função onde quer que ela esteja no seu código
extern nlohmann::json GetLoadedSkillTreeConfigs();

void Manager::LoadCustomSkills() {
    nlohmann::json configs = GetLoadedSkillTreeConfigs();

    for (const auto& j : configs) {
        if (j.contains("isVanilla") && !j["isVanilla"].get<bool>()) {
            CustomSkill skill;
            skill.id = j.value("name", "UnknownSkill"); // O name agora atua puramente como Unique ID interno

            // LÊ O DISPLAY NAME (Se não existir, cai pro nome base)
            skill.displayName = j.value("displayName", skill.id);

            skill.initialLevel = j.value("initialLevel", 15);
            skill.isVanilla = false;
            skill.advancesPlayerLevel = j.value("advancesPlayerLevel", false);

            if (j.contains("experienceFormula")) {
                auto& exp = j["experienceFormula"];
                skill.expFormula.useMult = exp.value("useMult", 1.0f);
                skill.expFormula.useOffset = exp.value("useOffset", 0.0f);
                skill.expFormula.improveMult = exp.value("improveMult", 1.0f);
                skill.expFormula.improveOffset = exp.value("improveOffset", 0.0f);
            }

            customSkillsData[skill.id] = skill;

            if (playerCustomSkills.find(skill.id) == playerCustomSkills.end()) {
                playerCustomSkills[skill.id] = { skill.initialLevel, 0.0f, 0 };
            }

            logger::info("Custom Skill carregada: {} (Display: {})", skill.id, skill.displayName);
        }
    }
}

using json = nlohmann::json;
extern json GetEffectiveSettings(int targetLevel);
extern json GetUISettings();
extern json GetSettings();

// --- CONTROLE DE NOTIFICAÇÕES ---
static std::mutex _notificationMutex;
// Armazena quais skills já têm uma thread aguardando disparar.
static std::unordered_set<std::string> _pendingNotifications;

void Manager::AddCustomSkillXP(const std::string& skillId, float xpAmount) {
    // 1. Bloqueia para atualizar os dados brutos de XP de forma segura
    std::lock_guard<std::mutex> updateLock(_notificationMutex);

    if (customSkillsData.find(skillId) == customSkillsData.end()) return;

    auto& state = playerCustomSkills[skillId];
    auto& data = customSkillsData[skillId];

    // Snapshot do nível ANTES de aplicar o XP
    int startLevelSnapshot = state.currentLevel;

    // --- APLICAÇÃO MATEMÁTICA DE XP ---
    float finalXp = (xpAmount * data.expFormula.useMult) + data.expFormula.useOffset;
    state.currentXP += finalXp;

    json settings = GetSettings();
    int maxCap = settings["base"].value("skillCap", 100);

    // Lógica de Level Up (Simulação)
    float reqXp = GetRequiredXP(skillId, state.currentLevel);

    // Loop para múltiplos level ups de uma vez
    while (state.currentXP >= (reqXp - 0.001f) && state.currentLevel < maxCap) {
        state.currentXP -= reqXp;
        if (state.currentXP < 0.0f) state.currentXP = 0.0f;

        state.currentLevel++;
        reqXp = GetRequiredXP(skillId, state.currentLevel);

        // Se configurado, avança o nível do personagem (Progressão Vanilla)
        if (data.advancesPlayerLevel) {
            auto player = RE::PlayerCharacter::GetSingleton();
            if (player) {
                auto& rt = player->GetPlayerRuntimeData();
                if (rt.skills && rt.skills->data) {
                    rt.skills->data->xp += static_cast<float>(state.currentLevel);
                }
            }
        }
    }

    // Cap level check
    if (state.currentLevel >= maxCap) {
        state.currentXP = 0.0f;
        reqXp = 1.0f;
    }

    // --- LÓGICA VISUAL (DEBOUNCE) ---

    // Se já existe uma notificação agendada (pendente) para essa skill, 
    // NÃO criamos outra thread. A thread existente pegará o valor acumulado atualizado.
    if (_pendingNotifications.find(skillId) != _pendingNotifications.end()) {
        return;
    }

    // Marca como pendente para bloquear novas threads
    _pendingNotifications.insert(skillId);
    std::string dispName = data.displayName;

    // Cria uma thread destacada para esperar o XP "acumular" (Debounce)
    std::thread([this, skillId, dispName, startLevelSnapshot]() {
        // Espera 1.5s. Se o jogador ganhar mais XP nesse tempo, a thread espera e pega tudo junto.
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));

        // Agenda a execução na Thread Principal (UI Task)
        SKSE::GetTaskInterface()->AddUITask([this, skillId, dispName, startLevelSnapshot]() {
            int currentRealLevel;
            float currentRealXP;
            float reqXpForCalc;

            // Bloqueio rápido apenas para ler os dados finais e limpar a flag
            {
                std::lock_guard<std::mutex> guard(_notificationMutex);

                // Remove da lista de pendentes DENTRO da task da UI.
                // Isso impede condições de corrida.
                _pendingNotifications.erase(skillId);

                if (playerCustomSkills.find(skillId) == playerCustomSkills.end()) return;

                auto& s = playerCustomSkills[skillId];
                currentRealLevel = s.currentLevel;
                currentRealXP = s.currentXP;
                reqXpForCalc = GetRequiredXP(skillId, currentRealLevel);
            }

            // --- CÁLCULO DE PORCENTAGEM SEGURO ---
            if (reqXpForCalc <= 0.001f) reqXpForCalc = 1.0f; // Evita divisão por zero

            float endPct = currentRealXP / reqXpForCalc;
            float startPct = 0.0f;

            // Define o ponto de partida da barra
            if (currentRealLevel > startLevelSnapshot) {
                // Se subiu de nível, a barra começa vazia (0%) e vai até onde parou no novo nível
                startPct = 0.0f;
            }
            else {
                // Se não subiu de nível, simulamos uma animação curta (progresso atual - 5%)
                // Isso evita ter que guardar o "xp anterior" exato, que complica o código.
                startPct = std::max(0.0f, endPct - 0.05f);
            }

            // CLAMP CRÍTICO: O Flash do Skyrim trava se receber valores < 0 ou > 1 ou NaN
            startPct = std::clamp(startPct, 0.0f, 1.0f);
            endPct = std::clamp(endPct, 0.0f, 1.0f);

            // Se for muito pequeno (erro de float) e não houve level up, ignoramos para não travar
            if (std::abs(endPct - startPct) < 0.001f && currentRealLevel == startLevelSnapshot) {
                return;
            }

            // --- INTERAÇÃO COM A UI (SCALEFORM) ---
            const auto ui = RE::UI::GetSingleton();
            if (!ui) return;

            const auto menu = ui->GetMenu<RE::HUDMenu>(RE::HUDMenu::MENU_NAME);
            if (!menu || !menu->uiMovie) return;

            auto movie = menu->uiMovie;
            RE::GFxValue questUpdateInstance;

            // Pega a instância base do gerenciador de notificações
            if (movie->GetVariable(&questUpdateInstance, "_root.HUDMovieBaseInstance.QuestUpdateBaseInstance")) {

                // NOTA: Não existe um método "Clear" exposto publicamente no Scaleform vanilla seguro.
                // A melhor forma de "limpar" é garantir que não enviamos lixo (validado acima)
                // e confiar na fila interna do QuestUpdateBaseInstance que gerencia sequências.
                // Se você realmente quiser tentar limpar, seria necessário acesso avançado aos arrays do Flash,
                // o que é arriscado. A validação de input acima resolve 99% dos "stuck bars".

                json uiSettings = GetUISettings();
                std::string finalName = (uiSettings.value("hideLockedTreeNames", false) && currentRealLevel <= 0)
                    ? "????" : dispName;

                RE::GFxValue args[8];
                args[0] = finalName.c_str();       // aNotificationText
                args[1] = "";                      // aStatus
                args[2] = "UISkillIncreaseSD";     // aSoundID (Som oficial de Skill Up)
                args[3] = 0;                       // aObjectiveCount
                args[4] = 1;                       // aNotificationType (1 = Skill)
                args[5] = currentRealLevel;        // aLevel (Nível ATUAL)
                args[6] = startPct;                // aStartPercent (0.0 a 1.0)
                args[7] = endPct;                  // aEndPercent (0.0 a 1.0)

                // Dispara a notificação
                questUpdateInstance.Invoke("ShowNotification", nullptr, args, 8);
            }
            });
        }).detach(); // Solta a thread para rodar em paralelo
}

// Calculo do Threshold de XP (Pode ser ajustado para simular 100% a curva vanilla se quiser)
float Manager::GetRequiredXP(const std::string& skillId, int level) {
    if (customSkillsData.find(skillId) != customSkillsData.end()) {
        auto& exp = customSkillsData[skillId].expFormula;

        // FÓRMULA AJUSTADA (Quadrática):
        // Cria uma curva onde níveis altos exigem mais XP.
        // Exemplo (Offset 0, Mult 1.0):
        // Nível 15: ~127 XP
        // Nível 50: ~1300 XP
        // Nível 100: ~5100 XP
        float linearPart = exp.improveMult * level;
        float curvedPart = std::pow(level, 2.0f) * 0.5f; 

        return exp.improveOffset + linearPart + curvedPart;
    }
    // Fallback padrão vanilla-ish
    return 100.0f + (level * 10.0f);
}



// --- LOGICA DE SAVE / LOAD DO SKSE ---
void Manager::Save(SKSE::SerializationInterface* a_intfc) {
    if (!a_intfc->OpenRecord('SKIL', 2)) return; 

    std::size_t count = playerCustomSkills.size();
    a_intfc->WriteRecordData(&count, sizeof(count));

    for (const auto& [id, state] : playerCustomSkills) {
        std::size_t idLen = id.length();
        a_intfc->WriteRecordData(&idLen, sizeof(idLen));
        a_intfc->WriteRecordData(id.data(), idLen);

        a_intfc->WriteRecordData(&state.currentLevel, sizeof(state.currentLevel));
        a_intfc->WriteRecordData(&state.currentXP, sizeof(state.currentXP));
        a_intfc->WriteRecordData(&state.bonusLevel, sizeof(state.bonusLevel)); 
    }
}

void Manager::Load(SKSE::SerializationInterface* a_intfc) {
    uint32_t type;
    uint32_t version;
    uint32_t length;

    while (a_intfc->GetNextRecordInfo(type, version, length)) {
        if (type == 'SKIL') {
            std::size_t count;
            if (!a_intfc->ReadRecordData(&count, sizeof(count))) continue;

            for (std::size_t i = 0; i < count; ++i) {
                std::size_t idLen;
                if (!a_intfc->ReadRecordData(&idLen, sizeof(idLen))) break;

                std::string id(idLen, '\0');
                if (!a_intfc->ReadRecordData(id.data(), idLen)) break;

                CustomSkillState state;
                state.bonusLevel = 0; // Padrão se for save antigo

                if (!a_intfc->ReadRecordData(&state.currentLevel, sizeof(state.currentLevel))) break;
                if (!a_intfc->ReadRecordData(&state.currentXP, sizeof(state.currentXP))) break;

                // <--- LÊ O BÔNUS APENAS SE A VERSÃO DO SAVE FOR 2 OU MAIOR
                if (version >= 2) {
                    if (!a_intfc->ReadRecordData(&state.bonusLevel, sizeof(state.bonusLevel))) break;
                }

                playerCustomSkills[id] = state;
            }
        }
    }
}

// Limpa a memória quando o jogador vai pro menu principal ou da load em outro save
void Manager::Revert(SKSE::SerializationInterface* a_intfc) {
    playerCustomSkills.clear();
    for (const auto& [id, data] : customSkillsData) {
        playerCustomSkills[id] = { data.initialLevel, 0.0f, 0 }; // <--- Zera o bônus também
    }
}
// Adicione esta função no final do arquivo ou junto com os outros métodos públicos

const InternalFormInfo* Manager::GetInfoByID(const std::string& type, RE::FormID id) {
    // Acesso direto ao map _dataStore
    auto it = _dataStore.find(type);
    if (it != _dataStore.end()) {
        const auto& list = it->second;
        // Busca linear para encontrar o FormID correspondente
        for (const auto& item : list) {
            if (item.formID == id) {
                return &item;
            }
        }
    }
    return nullptr;
}