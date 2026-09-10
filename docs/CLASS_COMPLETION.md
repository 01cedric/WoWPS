# Class support — remaining work

The supported subset is described in [the README](../README.md). Complete
class parity requires the following work:

| System | Required implementation and verification |
|---|---|
| Hunter/Warlock pets | Summon/tame/dismiss, owner authority, pet spells/action bar, AI and threat, happiness/feeding as applicable, save/rejoin/death/instance behavior |
| Warrior stances / Druid forms | Form transitions, equipment/resource restrictions, form action bars, power/armor/movement changes, aura removal and save/session reset |
| Procs | Event masks, chance/PPM, internal cooldowns, triggered targets, recursion guards, combat log and reproducible authority tests |
| Auras | Stacking, exclusivity, duration, dispel/steal, absorbs, shields, stat/family modifiers, crowd control, immunities, death/removal and UI |
| Weapon/combo combat | Weapon requirements, normalized hits, next-swing queue, finishers/combo points, avoidance/critical rules and rank interactions |
| DK / Paladin / Shaman | Diseases, death runes, seals/judgments, totems, auras and resource/effect interactions |
| Channels/areas | Tick ownership, interrupts, dynamic targets, radius/LOS, periodic resource costs and cleanup |
| Full talent trees | Every rank effect, dependencies, previews, resets, dual specializations and glyphs; all ten classes |
| Acceptance | Per-effect tests, adversarial LAN/replay/disconnect tests, save migration and real PS4 tests with all classes |
