Scriptname NewSkillMenu 

; Retorna a versão da API do SkillMenu (Definida em SkillMenuAPI.h)
Int Function GetAPIVersion() Global Native

; Adiciona uma quantidade específica de experiência a uma custom skill.
; Equivalente a: Manager::GetSingleton()->AddCustomSkillXP(skillId, xp)
Function AddCustomSkillXP(String skillId, Float xp) Global Native

; Retorna o nível BASE atual de uma custom skill (Sem bônus).
; Usa o backend actor-aware com o player como ator padrao
Int Function GetCustomSkillLevel(String skillId) Global Native

; Retorna a experiência atual (absoluta ou relativa ao nível, dependendo da sua implementação interna) da skill.
; Usa o co-save de XP do backend actor-aware com o player como ator padrao
Float Function GetCustomSkillXP(String skillId) Global Native

; Retorna os valores da fórmula de leveling configurados no JSON/Data.
; valueType 0: useMult
; valueType 1: useOffset
; valueType 2: improveMult
; valueType 3: improveOffset
Float Function GetSkillFormulaValue(String skillId, Int valueType) Global Native


; =======================================================
; V2 API - SISTEMA DE BÔNUS (BUFFS/DEBUFFS)
; =======================================================

; Retorna o Nível Total da skill (Nível Base + Bônus)
Int Function GetCustomSkillTotalLevel(String skillId) Global Native

; Retorna apenas o valor de Bônus atual da skill
Int Function GetCustomSkillBonus(String skillId) Global Native

; Adiciona ou subtrai (se negativo) um valor ao bônus atual da skill
Function ModCustomSkillBonus(String skillId, Int amount) Global Native

; Define o bônus da skill para um valor exato (Ex: SetCustomSkillBonus("Athletics", 0) para limpar)
Function SetCustomSkillBonus(String skillId, Int amount) Global Native
; =======================================================
; V3 API - FORMID-AWARE CUSTOM SKILLS/PERKS
; =======================================================

Function AddCustomSkillXPForActor(Int actorFormID, String skillId, Float xp) Global Native

Int Function GetCustomSkillLevelForActor(Int actorFormID, String skillId) Global Native

Float Function GetCustomSkillXPForActor(Int actorFormID, String skillId) Global Native

Int Function GetCustomSkillTotalLevelForActor(Int actorFormID, String skillId) Global Native

Int Function GetCustomSkillBonusForActor(Int actorFormID, String skillId) Global Native

Function ModCustomSkillBonusForActor(Int actorFormID, String skillId, Int amount) Global Native

Function SetCustomSkillBonusForActor(Int actorFormID, String skillId, Int amount) Global Native

Bool Function HasCustomPerkForActor(Int actorFormID, String perkId) Global Native

Bool Function AddCustomPerkForActor(Int actorFormID, String perkId) Global Native

Bool Function RemoveCustomPerkForActor(Int actorFormID, String perkId) Global Native

; =======================================================
; V4 API - ACTOR-SPECIFIC ECONOMY
; =======================================================

Int Function GetActorPerkPoints(Int actorFormID) Global Native

; Returns the actor's resulting balance.
Int Function ModActorPerkPoints(Int actorFormID, Int amount) Global Native

Float Function GetActorResource(Int actorFormID, String resourceId) Global Native

; Positive values credit; negative values debit when sufficient.
Bool Function ModActorResource(Int actorFormID, String resourceId, Float amount) Global Native

; =======================================================
; V5 API - AVAILABLE CUSTOM SKILLS
; =======================================================

; Returns the IDs accepted by AddCustomSkillXP and its actor-aware variant.
String[] Function GetAvailableSkills() Global Native
