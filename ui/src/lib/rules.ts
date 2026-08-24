export interface RuleValidationError {
    index: number;
    field: string;
    message: string;
}

type RuleRecord = Record<string, unknown>;

const numericFields = [
    'perksPerLevel',
    'skillPointsPerLevel',
    'maxSkillPointsSpendablePerLevel',
    'skillCap',
] as const;

export function validateRules(rules: unknown): RuleValidationError[] {
    if (!Array.isArray(rules)) {
        return [{ index: -1, field: 'rules', message: 'Rules must be an array.' }];
    }

    const errors: RuleValidationError[] = [];
    const scopes = new Set(['', 'all', 'player', 'followers', 'actor']);
    rules.forEach((rawRule, index) => {
        if (!rawRule || typeof rawRule !== 'object') {
            errors.push({ index, field: 'rule', message: 'Rule must be an object.' });
            return;
        }
        const rule = rawRule as RuleRecord;
        if (!Number.isInteger(rule.level) || Number(rule.level) < 0) {
            errors.push({ index, field: 'level', message: 'Level must be a non-negative integer.' });
        }
        const scope = typeof rule.scope === 'string' ? rule.scope : '';
        if (!scopes.has(scope)) {
            errors.push({ index, field: 'scope', message: 'Unknown rule scope.' });
        }
        if (scope === 'actor' && !rule.actorKey) {
            errors.push({ index, field: 'actorKey', message: 'Actor-scoped rules require actorKey.' });
        }
        numericFields.forEach(field => {
            const value = rule[field];
            if (value !== undefined && (!Number.isInteger(value) || Number(value) < -1)) {
                errors.push({
                    index,
                    field,
                    message: 'Value must be an integer greater than or equal to -1.',
                });
            }
        });
        if (rule.maxPerkPoints !== undefined &&
            (!Number.isInteger(rule.maxPerkPoints) || Number(rule.maxPerkPoints) < 0)) {
            errors.push({
                index,
                field: 'maxPerkPoints',
                message: 'Maximum perk points must be a non-negative integer.',
            });
        }
        if (rule.maxResetsPerActor !== undefined &&
            (!Number.isInteger(rule.maxResetsPerActor) || Number(rule.maxResetsPerActor) < -1)) {
            errors.push({
                index,
                field: 'maxResetsPerActor',
                message: 'Maximum resets must be -1 or a non-negative integer.',
            });
        }
        if (rule.resourceRewards !== undefined) {
            if (!Array.isArray(rule.resourceRewards)) {
                errors.push({
                    index,
                    field: 'resourceRewards',
                    message: 'Resource rewards must be an array.',
                });
            } else if (rule.resourceRewards.some(reward => {
                if (!reward || typeof reward !== 'object') return true;
                const value = reward as RuleRecord;
                return typeof value.resourceId !== 'string' ||
                    value.resourceId.length === 0 ||
                    typeof value.amount !== 'number' ||
                    !Number.isFinite(value.amount) ||
                    value.amount < 0;
            })) {
                errors.push({
                    index,
                    field: 'resourceRewards',
                    message: 'Each reward needs a resource and a non-negative amount.',
                });
            }
        }
    });
    return errors;
}
