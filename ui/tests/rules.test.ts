import assert from 'node:assert/strict';
import test from 'node:test';
import { validateRules } from '../src/lib/rules.ts';

test('accepts actor-scoped resource rewards', () => {
    assert.deepEqual(validateRules([{
        level: 5,
        scope: 'actor',
        actorKey: 'base:Skyrim.esm|A2C8E',
        maxPerkPoints: 20,
        maxResetsPerActor: 2,
        resourceRewards: [{ resourceId: 'Vampirism', amount: 1 }],
    }]), []);
});

test('rejects missing actor identity and malformed rewards', () => {
    const errors = validateRules([{
        level: 2,
        scope: 'actor',
        resourceRewards: [{ resourceId: '', amount: -1 }],
    }]);
    assert.equal(errors.some(error => error.field === 'actorKey'), true);
    assert.equal(errors.some(error => error.field === 'resourceRewards'), true);
});

test('rejects invalid limits', () => {
    const errors = validateRules([{ level: 1, maxPerkPoints: -2 }]);
    assert.equal(errors.some(error => error.field === 'maxPerkPoints'), true);
});
