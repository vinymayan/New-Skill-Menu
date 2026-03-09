// lib/locales.ts

export type Language = string;

// Cache de traduções: { "en": { ... }, "pt": { ... } }
const translations: Record<string, any> = {};

// Idioma atual selecionado
let currentLanguage: Language = 'en';

/**
 * Define o idioma ativo.
 * Nota: Isso não força re-renderização do React sozinho, o App.tsx deve gerenciar o estado.
 */
export const setLanguage = (lang: Language) => {
    currentLanguage = lang;
};

/**
 * Retorna o idioma atual configurado no módulo.
 */
export const getLanguage = () => currentLanguage;

/**
 * Injeta ou atualiza dados de tradução para um idioma.
 */
export const addTranslation = (lang: string, data: any) => {
    translations[lang] = { ...translations[lang], ...data };
};

/**
 * Verifica se existem dados carregados para o idioma.
 */
export const hasTranslation = (lang: string): boolean => {
    return !!translations[lang] && Object.keys(translations[lang]).length > 0;
};

/**
 * Função auxiliar para buscar valor em um objeto aninhado dado um array de chaves.
 */
const getNestedValue = (obj: any, keys: string[]): string | undefined => {
    let current = obj;
    for (const key of keys) {
        if (current === undefined || current === null) return undefined;
        current = current[key];
    }
    return typeof current === 'string' || typeof current === 'number' ? String(current) : undefined;
};

/**
 * Função principal de tradução.
 * Tenta buscar no idioma atual. Se falhar, busca em 'en'. Se falhar, retorna a chave.
 */
export const t = (path: string, args?: Record<string, string | number>): string => {
    const keys = path.split('.');

    // 1. Tenta no idioma atual
    let result = getNestedValue(translations[currentLanguage], keys);

    // 2. Se não achou e não estamos em inglês, tenta fallback para 'en'
    if (result === undefined && currentLanguage !== 'en') {
        result = getNestedValue(translations['en'], keys);
    }

    // 3. Se ainda não achou, retorna a própria chave para facilitar debug
    if (result === undefined) {
        return path;
    }

    // 4. Interpolação de variáveis: {valor}
    if (args) {
        for (const [key, value] of Object.entries(args)) {
            result = result.replace(new RegExp(`\\{${key}\\}`, 'g'), String(value));
        }
    }

    return result;
};