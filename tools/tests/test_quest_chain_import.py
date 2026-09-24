#!/usr/bin/env python3
"""Quest gate compiler regressions; source semantics, not copied implementation."""
import copy
import json
import itertools
from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/local_realm"))
from import_quest_chains import (ACTIVE, COMPLETE, NONE, REWARDED, Unsupported,
                                 canonical, compile_rules, conjunction, convert, read_pack)


def source(ids):
    templates = {q: {"id": q} for q in ids}
    addons = {q: {"id": q} for q in ids}
    catalog = {q: {"id": q, "title": f"Quest {q}", "prerequisite": 0} for q in ids}
    return catalog, templates, addons


def condition(quest, referenced, kind=8, group=0, negative=0, extra=0):
    return {"sourcetypeorreferenceid": 19, "sourceentry": quest, "sourcegroup": 0,
            "sourceid": 0, "elsegroup": group, "conditiontypeorreference": kind,
            "conditiontarget": 0, "conditionvalue1": referenced, "conditionvalue2": extra,
            "conditionvalue3": 0, "negativecondition": negative, "scriptname": ""}


def admits(gate, states, items=None, bank=None, ranks=None, spells=()):
    def matches(p):
        if "questId" in p:
            return bool(states.get(p["questId"], NONE) & p["statusMask"])
        if "itemId" in p:
            matched = (items or {}).get(p["itemId"], 0) + ((bank or {}).get(p["itemId"], 0) if p["includeBank"] else 0) >= p["count"]
        elif "factionId" in p:
            matched = bool(p["rankMask"] & (1 << (ranks or {}).get(p["factionId"], 3)))
        else:
            matched = p["spellId"] in spells
        return not matched if p["negated"] else matched
    previous = gate.get("orderedPrevious", [])
    if previous:
        first = next((rule for rule in previous if matches(rule["when"])), None)
        if first is None or not all(matches(p) for p in first["require"]):
            return False
    return not gate["unsupportedReason"] and any(
        all(matches(p) for p in branch)
        for branch in gate["alternatives"])


def gates(catalog, templates, addons, conditions=()):
    return {q["id"]: q for q in compile_rules(catalog, templates, addons, conditions)[0]}


class QuestChainCompilerTests(unittest.TestCase):
    def test_next_quest_is_an_alternative_to_explicit_previous(self):
        c, t, a = source([1, 2, 3])
        a[3]["prevquestid"] = 1
        a[2]["nextquestid"] = 3
        c[3]["prerequisite"] = 1
        rule = gates(c, t, a)[3]
        self.assertFalse(admits(rule, {}))
        self.assertTrue(admits(rule, {1: REWARDED}))
        self.assertTrue(admits(rule, {2: REWARDED}))
        self.assertFalse(admits(rule, {2: COMPLETE}))

    def test_negative_group_requires_every_reward(self):
        c, t, a = source([1, 2, 3])
        a[1]["exclusivegroup"] = a[2]["exclusivegroup"] = -10
        a[3]["prevquestid"] = 1
        rule = gates(c, t, a)[3]
        self.assertFalse(admits(rule, {1: REWARDED}))
        self.assertFalse(admits(rule, {1: REWARDED, 2: COMPLETE}))
        self.assertTrue(admits(rule, {1: REWARDED, 2: REWARDED}))

    def test_positive_group_excludes_other_started_and_rewarded_choices(self):
        c, t, a = source([1, 2])
        a[1]["exclusivegroup"] = a[2]["exclusivegroup"] = 10
        rule = gates(c, t, a)[1]
        self.assertTrue(admits(rule, {}))
        for state in (ACTIVE, COMPLETE, REWARDED):
            self.assertFalse(admits(rule, {2: state}))

    def test_negative_previous_retains_pinned_nonrepeatable_status_semantics(self):
        c, t, a = source([1, 2])
        a[2]["prevquestid"] = -1
        rule = gates(c, t, a)[2]
        self.assertFalse(admits(rule, {}))
        for state in (ACTIVE, COMPLETE, REWARDED):
            self.assertTrue(admits(rule, {1: state}))

    def test_negative_group_with_an_alternative_does_not_weaken_core_ordering(self):
        c, t, a = source([1, 2, 3, 4])
        a[1]["exclusivegroup"] = a[2]["exclusivegroup"] = -10
        a[4]["prevquestid"] = 1
        a[3]["nextquestid"] = 4
        rule = gates(c, t, a)[4]
        self.assertTrue(rule["unsupportedReason"])
        self.assertFalse(admits(rule, {1: REWARDED, 3: REWARDED}))

    def test_same_negative_group_alternatives_match_source_for_every_state(self):
        c, t, a = source([1, 2, 3, 4])
        for q in (1, 2, 3):
            a[q].update(exclusivegroup=-10, nextquestid=4)
        rule = gates(c, t, a)[4]
        self.assertEqual(len(rule["orderedPrevious"]), 3)
        for values in itertools.product((NONE, ACTIVE, COMPLETE, REWARDED), repeat=3):
            state = dict(zip((1, 2, 3), values))
            for order in itertools.permutations((1, 2, 3)):
                # Pinned core: first rewarded predecessor returns immediately,
                # rejecting if any other member is not rewarded.
                expected = False
                for q in order:
                    if state[q] == REWARDED:
                        expected = all(state[m] == REWARDED for m in (1, 2, 3) if m != q)
                        break
                self.assertEqual(admits(rule, state), expected)

    def test_negative_previous_group_preserves_asymmetric_source_check(self):
        c, t, a = source([1, 2, 3])
        a[1]["exclusivegroup"] = a[2]["exclusivegroup"] = -10
        a[3]["prevquestid"] = -1
        rule = gates(c, t, a)[3]
        for one, two in itertools.product((NONE, ACTIVE, COMPLETE, REWARDED), repeat=2):
            self.assertEqual(admits(rule, {1: one, 2: two}), one != NONE and two == NONE)

    def test_item_bank_reputation_spell_and_negation(self):
        c, t, a = source([1])
        for kind, identifier, extra in ((2, 99, 3), (5, 72, (1 << 4) | (1 << 5)), (25, 54197, 0)):
            for negative in (0, 1):
                row = condition(1, identifier, kind, negative=negative, extra=extra)
                if kind == 2:
                    row["conditionvalue3"] = 7  # upstream converts any nonzero to true
                rule = gates(c, t, a, [row])[1]
                self.assertFalse(rule["unsupportedReason"])
                self.assertEqual(admits(rule, {}), bool(negative))
                self.assertEqual(admits(rule, {}, items={99: 1}, bank={99: 2}, ranks={72: 4}, spells=[54197]), not negative)
        row = condition(1, 99, 2, extra=3)
        self.assertFalse(admits(gates(c, t, a, [row])[1], {}, items={99: 1}, bank={99: 2}))

    def test_player_conditions_join_quest_else_groups_and_fail_closed(self):
        c, t, a = source([1, 2])
        rows = [condition(2, 1), condition(2, 44, 25), condition(2, 77, 2, group=1, extra=2)]
        rule = gates(c, t, a, rows)[2]
        self.assertFalse(admits(rule, {1: REWARDED}))
        self.assertTrue(admits(rule, {1: REWARDED}, spells=[44]))
        self.assertTrue(admits(rule, {}, items={77: 2}))
        for bad in (condition(2, 77, 2), condition(2, 72, 5, extra=256), condition(2, 44, 25, extra=1)):
            self.assertTrue(gates(c, t, a, rows + [bad])[2]["unsupportedReason"])
        contradictory = [condition(2, 44, 25), condition(2, 44, 25, negative=1)]
        self.assertTrue(gates(c, t, a, contradictory)[2]["unsupportedReason"])

    def test_repeatable_and_all_seasonal_sort_categories_are_blocked(self):
        for sort in (-22, -284, -366, -369, -370, -374, -376):
            c, t, a = source([1, 2])
            t[1]["questsortid"] = sort
            a[2]["prevquestid"] = 1
            self.assertTrue(gates(c, t, a)[2]["unsupportedReason"])
        c, t, a = source([1]); t[1]["questinfoid"] = 41
        self.assertFalse(gates(c, t, a)[1]["unsupportedReason"])

    def test_breadcrumb_is_bidirectional_but_reward_unlocks_target(self):
        c, t, a = source([1, 2])
        a[1]["breadcrumbforquestid"] = 2
        rules = gates(c, t, a)
        for state in (ACTIVE, COMPLETE, REWARDED):
            self.assertFalse(admits(rules[1], {2: state}))
        for state in (ACTIVE, COMPLETE):
            self.assertFalse(admits(rules[2], {1: state}))
        self.assertTrue(admits(rules[2], {1: REWARDED}))
        self.assertTrue(admits(rules[2], {}))

    def test_reward_next_chain_prevents_backwards_acceptance(self):
        c, t, a = source([1, 2])
        t[1]["rewardnextquest"] = 2
        rules = gates(c, t, a)
        self.assertFalse(admits(rules[1], {2: REWARDED}))
        self.assertFalse(admits(rules[2], {1: COMPLETE}))
        self.assertTrue(admits(rules[2], {1: REWARDED}))

    def test_else_groups_are_or_and_rows_in_each_group_are_and(self):
        c, t, a = source([1, 2, 3, 4])
        rows = [condition(4, 1), condition(4, 2), condition(4, 3, group=1)]
        rule = gates(c, t, a, rows)[4]
        self.assertFalse(admits(rule, {1: REWARDED}))
        self.assertTrue(admits(rule, {1: REWARDED, 2: REWARDED}))
        self.assertTrue(admits(rule, {3: REWARDED}))

    def test_conditions_and_previous_chain_both_apply(self):
        c, t, a = source([1, 2, 3])
        a[3]["prevquestid"] = 1
        rule = gates(c, t, a, [condition(3, 2)])[3]
        self.assertFalse(admits(rule, {1: REWARDED}))
        self.assertFalse(admits(rule, {2: REWARDED}))
        self.assertTrue(admits(rule, {1: REWARDED, 2: REWARDED}))

    def test_taken_condition_is_only_incomplete_and_inversion_is_exact(self):
        c, t, a = source([1, 2])
        positive = gates(c, t, a, [condition(2, 1, kind=9)])[2]
        negative = gates(c, t, a, [condition(2, 1, kind=9, negative=1)])[2]
        for state in (NONE, ACTIVE, COMPLETE, REWARDED):
            self.assertEqual(admits(positive, {1: state}), state == ACTIVE)
            self.assertEqual(admits(negative, {1: state}), state != ACTIVE)

    def test_upstream_quest_state_bits_are_translated(self):
        c, t, a = source([1, 2])
        for source_mask, local_mask in ((1, NONE), (2, COMPLETE), (8, ACTIVE), (64, REWARDED), (74, 14)):
            rule = gates(c, t, a, [condition(2, 1, kind=47, extra=source_mask)])[2]
            for state in (NONE, ACTIVE, COMPLETE, REWARDED):
                self.assertEqual(admits(rule, {1: state}), bool(local_mask & state))

    def test_unknown_or_failed_conditions_block_entire_gate_even_with_or(self):
        c, t, a = source([1, 2])
        for row in (condition(2, 1, kind=26), condition(2, 1, kind=47, extra=32), condition(2, 1, kind=-1)):
            rule = gates(c, t, a, [condition(2, 1, group=1), row])[2]
            self.assertTrue(rule["unsupportedReason"])
            self.assertFalse(admits(rule, {1: REWARDED}))

    def test_missing_positive_content_stays_required_for_historical_saves(self):
        c, t, a = source([1, 2])
        del c[1]
        a[2]["prevquestid"] = 1
        rules, details = compile_rules(c, t, a, [])
        self.assertFalse(admits(rules[0], {}))
        self.assertTrue(admits(rules[0], {1: REWARDED}))
        self.assertEqual(details[0]["unavailablePositiveDependencies"], [1])
        self.assertFalse(details[0]["positiveDependencyReachableFromFreshSave"])

    def test_cycles_are_never_marked_reachable_from_fresh_save(self):
        c, t, a = source([1, 2, 3])
        a[1]["prevquestid"], a[2]["prevquestid"] = 2, 1
        details = compile_rules(c, t, a, [])[1]
        self.assertEqual([q["positiveDependencyReachableFromFreshSave"] for q in details], [False, False, True])

    def test_bounds_contradictions_and_branch_absorption(self):
        self.assertEqual(canonical([((1, 8), (1, 1))]), [])
        self.assertEqual(canonical([(), ((1, 8),)]), [()])
        self.assertEqual(canonical([((1, 14),), ((1, 8), (2, 8))]), [((1, 14),)])
        with self.assertRaises(Unsupported):
            canonical([tuple((q, 8) for q in range(1, 18))])
        with self.assertRaises(Unsupported):
            canonical([((q, 8),) for q in range(1, 18)])
        self.assertEqual(conjunction([((1, 14),)], [((1, 8),)]), [((1, 8),)])

    def test_source_inputs_remain_unchanged(self):
        c, t, a = source([1, 2]); a[1]["nextquestid"] = 2
        before = copy.deepcopy((c, t, a))
        compile_rules(c, t, a, [])
        self.assertEqual((c, t, a), before)

    def test_malformed_pack_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "quests.pack"
            row = b'{"id":1}'
            valid = struct.pack("<8sII", b"WPCAT01\0", 1, 16) + struct.pack("<IQI", 1, 32, len(row)) + row
            path.write_bytes(valid)
            self.assertEqual(list(read_pack(path)), [1])
            for malformed in (valid + b"x", valid[:-1], valid[:16], valid.replace(b'"id":1', b'"id":2')):
                path.write_bytes(malformed)
                with self.assertRaises(ValueError):
                    read_pack(path)

    def test_actual_archives_compile_reproducibly_and_fix_real_condition(self):
        with tempfile.TemporaryDirectory() as directory:
            companion, report = Path(directory) / "gates.json", Path(directory) / "report.json"
            counts = convert(ROOT, companion, report)
            first = companion.read_bytes(), report.read_bytes()
            self.assertEqual(counts["inputCatalogQuests"], len(read_pack(ROOT / "assets/local_realm/catalog/quests.pack")))
            self.assertEqual(counts["compiledGates"] + counts["blockedGates"], counts["inputCatalogQuests"])
            self.assertEqual((counts["inputCatalogQuests"], counts["compiledGates"], counts["blockedGates"]), (950, 947, 3))
            self.assertEqual(counts["orderedPreviousGates"], 14)
            rules = {q["id"]: q for q in json.loads(first[0])["quests"]}
            # Real Quest374 must wait for rewarded427; original catalog omitted it.
            self.assertFalse(admits(rules[374], {}))
            self.assertTrue(admits(rules[374], {427: REWARDED}))
            # Real breadcrumb163 points to5. Rewarding163 unlocks5, merely
            # accepting it must not expose the next quest.
            self.assertFalse(admits(rules[5], {163: ACTIVE}))
            self.assertTrue(admits(rules[5], {163: REWARDED}))
            self.assertFalse(admits(rules[163], {5: ACTIVE}))
            convert(ROOT, companion, report)
            self.assertEqual(first, (companion.read_bytes(), report.read_bytes()))


if __name__ == "__main__":
    unittest.main(verbosity=2)
