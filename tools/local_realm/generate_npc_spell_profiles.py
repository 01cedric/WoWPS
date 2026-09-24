#!/usr/bin/env python3
"""Regenerate the NPC SmartAI script family from pinned AC SQL and build 12340 DBC.

A creature script owner (an entry, or a spawn guid whose guid script replaces
the entry script) is installed only when its whole SmartAI script is
represented locally, row by row:

* every row is either installed by the creature-talk companion or is one the
  local runtime reproduces: events UPDATE_IC/OOC/UPDATE, HEALTH_PCT, AGGRO,
  DEATH, SPELLHIT, RANGE, RESPAWN, VICTIM_CASTING, RESET, LINK, AREA_CASTING and
  AREA_RANGE; targets none/self/victim, the four hostile selectors, the action
  invoker, closest player, threat list and closest enemy; actions CAST and
  SELF_CAST (cast flags INTERRUPT_PREVIOUS, TRIGGERED, AURA_NOT_PRESENT,
  COMBAT_MOVE, MAIN_SPELL and a targets limit), AUTO_ATTACK,
  ALLOW_COMBAT_MOVEMENT, the event-phase actions, EVADE, REMOVEAURASFROMSPELL,
  CALL_FOR_HELP, ATTACK_START, CALL_TIMED_ACTIONLIST (lists made of the same
  rows) and INTERRUPT_SPELL; links between installed rows, event phase masks,
  flags NOT_REPEATABLE / DONT_RESET / WHILE_CHARMED (local creatures are never
  charmed) and no attached SMART_EVENT conditions. Rows carrying instance
  difficulty flags never load outside a dungeon (SmartScript::FillScript); an
  owner spawned only in dungeon maps installs its normal-mode rows;
* at least one row casts;
* each cast spell has no spell script, no spell conditions, no spell_group /
  spell_linked_spell / spell_custom_attr row, no SpellInfoCorrections/SpellMgr
  special case, at most a mana cost, and a shape the creature caster decoder
  reproduces: a single enemy, the caster itself, an ally (the caster), the
  enemies around the caster or the target, a cone, the allies around the
  caster, or a chain from the target; effects SCHOOL_DAMAGE, the melee weapon
  effects, HEALTH_LEECH, HEAL, INTERRUPT_CAST, KNOCK_BACK(_DEST), DUMMY, and
  APPLY_AURA with PERIODIC_DAMAGE, PERIODIC_LEECH, PERIODIC_HEAL,
  MOD_DECREASE_SPEED, MOD_RESISTANCE / MOD_RESISTANCE_PCT on armor (a
  reduction), MOD_STUN, MOD_ROOT, MOD_FEAR, MOD_CONFUSE, MOD_SILENCE,
  MOD_DAMAGE_TAKEN, MOD_DAMAGE_PERCENT_TAKEN, MOD_HEALING_PCT, MOD_MELEE_HASTE,
  MOD_DAMAGE_PERCENT_DONE, MOD_DAMAGE_DONE, MOD_ATTACK_POWER and the auras with
  no local effect (MOD_STAT, MOD_SCALE, DUMMY, TRANSFORM); channels whose ticks
  are periodic damage or leech. The client decoder re-checks every column at
  runtime and fails closed.

Inputs are the SQL archives in this directory (the same pinned revision as the
rest of the local realm; spell_script_names in spell_script_source_sql.tar.gz,
spell_cone / spell_jump_distance in spell_geometry_source_sql.tar.gz) plus the
client's Spell.dbc, SpellRadius.dbc, SpellRange.dbc, SpellDuration.dbc,
SpellCastTimes.dbc, SummonProperties.dbc (2.38) and Map.dbc.

2.38: the family also carries the summons (SUMMON_CREATURE rows and creature
SUMMON effects, checked against SummonProperties.dbc and the creature
template), the escort paths (the `waypoints` table rows of ESCORT_START
rows, written to local_npc_waypoints_generated.inc), the movement and text
timer rows, the persistent area auras, and every timed action list whose
rows are all installed. Run compile_creature_talk.py (with --summon-report)
before this tool and once more after it, until the text groups and the
report's summonEntries converge; then patch_summon_catalog.py adds the
summoned creature definitions the catalog lacks.

2.39: the waypoint_data patrol family (WAYPOINT_START / WAYPOINT_DATA_RANDOM
checked against the paths patch_motion_catalog.py carries in paths.pack),
DO_ACTION, instance data, npc flags, CROSS_CAST, passenger / distance
events, the spell decoder additions (CREATE_ITEM from EffectItemType, self
stuns / roots, invisibility, self instakill, permanent triggers, coded
SCRIPT_EFFECT no-ops with spell_scripts.sql from the spell script archive)
and local_npc_reward_flags_generated.inc (the templates with
CREATURE_FLAG_EXTRA_NO_PLAYER_DAMAGE_REQ). Tool order for a changed family:
compile_creature_talk.py -> this tool -> both again until they converge ->
patch_summon_catalog.py -> patch_motion_catalog.py (each patch refreshes the
manifest fingerprint).

2.40: the gossip family (GOSSIP_HELLO, GOSSIP_SELECT against the (menu,
option) pairs the catalog's gossip.pack carries, RECEIVE_EMOTE, CLOSE_GOSSIP,
SEND_GOSSIP_MENU / SET_GOSSIP_MENU against the carried menus and texts,
OFFER_QUEST / FAIL_QUEST against the catalog's quests, ADD_ITEM / REMOVE_ITEM
against its items). Run patch_gossip_catalog.py before this tool.
"""
import argparse
import hashlib
import json
import math
import struct
import sys
import tarfile
import tempfile
from collections import Counter, defaultdict
from pathlib import Path

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parent.parent
sys.path.insert(0, str(HERE))
from import_azerothcore import sql_rows  # noqa: E402

PIN = '4e80596cdaa21fa31830522f6f2d7ed8750bfffd'
ARCHIVES = {
    "creature_template": "world_source_sql.tar.gz",
    "creature_template_model": "world_source_sql.tar.gz",
    "creature": "world_source_sql.tar.gz",
    "creature_classlevelstats": "world_source_sql.tar.gz",
    "smart_scripts": "world_script_source_sql.tar.gz",
    "waypoints": "world_script_source_sql.tar.gz",
    "waypoint_data": "world_script_source_sql.tar.gz",
    "conditions": "vendor_source_sql.tar.gz",
}
SOURCE_HASHES = {
    "spell_script_names.sql": "9cb7ccf86ae2542fb4dae77f49944dc47f2865080c9367a9f7402a04db4d4da1",
    "spell_scripts.sql": "b918ef1f85d076e0665a3967cb595ed202b948a43ff6fd01efa7176303730d83",
    # spell_group, spell_group_stack_rules, spell_custom_attr, spell_linked_spell,
    # spell_proc and SpellInfoCorrections.cpp / SpellMgr.cpp at the pin.
    "spell_rules_source_sql.tar.gz": "24b8a83b8e24d968a8b7aa356ae3ce6cbd9bd5c641994df7d3bef6ba3f324fa0",
    # spell_cone and spell_jump_distance at the pin.
    "spell_geometry_source_sql.tar.gz": "b503e54a1d7e60eb86ec406f4787cdac2bfc24a50647c5b0d7ebf0a148ee0f59",
    "Spell.dbc": "d5cce1a83550dcfa9eb2f0251dbb11fd24c272534b2b1a9b230924a44d817ab3",
    "gtNPCManaCostScaler.dbc": None,  # recorded in the report; 100 float rows
}
UNIT_FLAG2_REGENERATE_POWER = 0x800
SPELL_ATTR0_PASSIVE = 0x40
SPELL_ATTR1_USE_ALL_MANA = 0x2
CLASSES = (1, 2, 4, 8)
FLAG_NOT_REPEATABLE, FLAG_DIFFICULTY_0, FLAG_DIFFICULTY_ALL, FLAG_DONT_RESET, FLAG_WHILE_CHARMED = 0x1, 0x2, 0x1e, 0x100, 0x200
CAST_INTERRUPT_PREVIOUS, CAST_TRIGGERED, CAST_AURA_NOT_PRESENT, CAST_COMBAT_MOVE, CAST_MAIN_SPELL = 1, 2, 32, 64, 0x400
CAST_FLAGS = CAST_INTERRUPT_PREVIOUS | CAST_TRIGGERED | CAST_AURA_NOT_PRESENT | CAST_COMBAT_MOVE | CAST_MAIN_SPELL
MAX_TIMER_MS = 3600000
MAX_ROWS_PER_OWNER = 24
# SMART_EVENT ids the runtime reproduces (SmartScript::ProcessEvent).
EV_UPDATE_IC, EV_UPDATE_OOC, EV_HEALTH_PCT, EV_MANA_PCT, EV_AGGRO, EV_KILL, EV_DEATH, EV_EVADE, EV_SPELLHIT = 0, 1, 2, 3, 4, 5, 6, 7, 8
EV_RANGE, EV_OOC_LOS, EV_RESPAWN, EV_TARGET_HEALTH_PCT, EV_VICTIM_CASTING, EV_FRIENDLY_HEALTH = 9, 10, 11, 12, 13, 14
EV_FRIENDLY_MISSING_BUFF, EV_TARGET_MANA_PCT, EV_ACCEPTED_QUEST, EV_REWARD_QUEST, EV_REACHED_HOME, EV_HAS_AURA = 16, 18, 19, 20, 21, 23
EV_TARGET_BUFFED, EV_RESET, EV_IC_LOS, EV_SPELLHIT_TARGET, EV_DAMAGED, EV_DAMAGED_TARGET, EV_CORPSE_REMOVED = 24, 25, 26, 31, 32, 33, 36
EV_AI_INIT, EV_DATA_SET, EV_RECEIVE_HEAL, EV_TIMED_EVENT_TRIGGERED, EV_UPDATE, EV_LINK, EV_JUST_CREATED = 37, 38, 53, 59, 60, 61, 63
EV_EVENT_PHASE_CHANGE, EV_IS_BEHIND_TARGET, EV_FRIENDLY_HEALTH_PCT, EV_COUNTER_SET = 66, 67, 74, 77
EV_NEAR_PLAYERS, EV_NEAR_PLAYERS_NEGATION, EV_AREA_CASTING, EV_AREA_RANGE = 101, 102, 105, 106
# 2.38: summons, escort paths, point movement, the text timer.
EV_SUMMONED_UNIT, EV_MOVEMENTINFORM, EV_SUMMON_DESPAWNED, EV_ESCORT_START, EV_ESCORT_REACHED = 17, 34, 35, 39, 40
EV_TEXT_OVER, EV_JUST_SUMMONED, EV_ESCORT_PAUSED, EV_ESCORT_RESUMED, EV_ESCORT_STOPPED, EV_ESCORT_ENDED = 52, 54, 55, 56, 57, 58
EV_FOLLOW_COMPLETED, EV_SUMMONED_UNIT_DIES, EV_SUMMONED_UNIT_EVADE = 65, 82, 107
# 2.39: waypoint_data patrols, DoAction, vehicle passengers, creature
# distances, crowd-controlled friends.
EV_WAYPOINT_REACHED, EV_WAYPOINT_ENDED, EV_ACTION_DONE, EV_PASSENGER_BOARDED, EV_PASSENGER_REMOVED = 108, 109, 72, 27, 28
EV_DISTANCE_CREATURE, EV_FRIENDLY_IS_CC = 75, 15
# 2.40: gossip and text emotes.
EV_RECEIVE_EMOTE, EV_GOSSIP_SELECT, EV_GOSSIP_HELLO = 22, 62, 64
EVENTS = {EV_UPDATE_IC, EV_UPDATE_OOC, EV_HEALTH_PCT, EV_MANA_PCT, EV_AGGRO, EV_KILL, EV_DEATH, EV_EVADE, EV_SPELLHIT, EV_RANGE,
          EV_OOC_LOS, EV_RESPAWN, EV_TARGET_HEALTH_PCT, EV_VICTIM_CASTING, EV_FRIENDLY_HEALTH, EV_FRIENDLY_MISSING_BUFF,
          EV_TARGET_MANA_PCT, EV_ACCEPTED_QUEST, EV_REWARD_QUEST, EV_REACHED_HOME, EV_HAS_AURA, EV_TARGET_BUFFED, EV_RESET,
          EV_IC_LOS, EV_SPELLHIT_TARGET, EV_DAMAGED, EV_DAMAGED_TARGET, EV_CORPSE_REMOVED, EV_AI_INIT, EV_DATA_SET,
          EV_RECEIVE_HEAL, EV_TIMED_EVENT_TRIGGERED, EV_UPDATE, EV_LINK, EV_JUST_CREATED, EV_EVENT_PHASE_CHANGE,
          EV_IS_BEHIND_TARGET, EV_FRIENDLY_HEALTH_PCT, EV_COUNTER_SET, EV_NEAR_PLAYERS, EV_NEAR_PLAYERS_NEGATION,
          EV_AREA_CASTING, EV_AREA_RANGE,
          EV_SUMMONED_UNIT, EV_MOVEMENTINFORM, EV_SUMMON_DESPAWNED, EV_ESCORT_START, EV_ESCORT_REACHED, EV_TEXT_OVER,
          EV_JUST_SUMMONED, EV_ESCORT_PAUSED, EV_ESCORT_RESUMED, EV_ESCORT_STOPPED, EV_ESCORT_ENDED, EV_FOLLOW_COMPLETED,
          EV_SUMMONED_UNIT_DIES, EV_SUMMONED_UNIT_EVADE,
          EV_WAYPOINT_REACHED, EV_WAYPOINT_ENDED, EV_ACTION_DONE, EV_PASSENGER_BOARDED, EV_PASSENGER_REMOVED,
          EV_DISTANCE_CREATURE, EV_FRIENDLY_IS_CC,
          EV_RECEIVE_EMOTE, EV_GOSSIP_SELECT, EV_GOSSIP_HELLO}
# Events whose invoker is a player (the action target types that read it).
PLAYER_INVOKER_EVENTS = {EV_AGGRO, EV_KILL, EV_SPELLHIT, EV_RANGE, EV_OOC_LOS, EV_TARGET_HEALTH_PCT, EV_VICTIM_CASTING,
                         EV_TARGET_MANA_PCT, EV_ACCEPTED_QUEST, EV_REWARD_QUEST, EV_TARGET_BUFFED, EV_IC_LOS, EV_SPELLHIT_TARGET,
                         EV_DAMAGED, EV_DAMAGED_TARGET, EV_RECEIVE_HEAL, EV_IS_BEHIND_TARGET, EV_AREA_CASTING, EV_AREA_RANGE,
                         EV_DEATH, EV_FOLLOW_COMPLETED, EV_PASSENGER_BOARDED, EV_PASSENGER_REMOVED,
                         EV_RECEIVE_EMOTE, EV_GOSSIP_SELECT, EV_GOSSIP_HELLO}
# SMARTAI_TARGETS ids (SmartScript::GetTargets).
TG_NONE, TG_SELF, TG_VICTIM, TG_HOSTILE_SECOND, TG_HOSTILE_LAST, TG_HOSTILE_RANDOM, TG_HOSTILE_RANDOM_NOT_TOP = 0, 1, 2, 3, 4, 5, 6
TG_INVOKER, TG_CREATURE_RANGE, TG_CREATURE_GUID, TG_CREATURE_DISTANCE, TG_STORED, TG_INVOKER_PARTY = 7, 9, 10, 11, 12, 16
TG_PLAYER_RANGE, TG_PLAYER_DISTANCE, TG_CLOSEST_CREATURE, TG_CLOSEST_PLAYER, TG_THREAT_LIST, TG_CLOSEST_ENEMY = 17, 18, 19, 21, 24, 25
TG_CLOSEST_FRIENDLY, TG_LOOT_RECIPIENTS, TG_FARTHEST, TG_PLAYER_WITH_AURA = 26, 27, 28, 201
# 2.38: a position from the row's own coordinates, the summoner, a random
# point and the creature's own summons.
TG_POSITION, TG_OWNER_OR_SUMMONER, TG_RANDOM_POINT, TG_SUMMONED_CREATURES = 8, 23, 202, 204
TARGETS = {TG_NONE, TG_SELF, TG_VICTIM, TG_HOSTILE_SECOND, TG_HOSTILE_LAST, TG_HOSTILE_RANDOM, TG_HOSTILE_RANDOM_NOT_TOP,
           TG_INVOKER, TG_CREATURE_RANGE, TG_CREATURE_GUID, TG_CREATURE_DISTANCE, TG_STORED, TG_INVOKER_PARTY, TG_PLAYER_RANGE,
           TG_PLAYER_DISTANCE, TG_CLOSEST_CREATURE, TG_CLOSEST_PLAYER, TG_THREAT_LIST, TG_CLOSEST_ENEMY, TG_CLOSEST_FRIENDLY,
           TG_LOOT_RECIPIENTS, TG_FARTHEST, TG_PLAYER_WITH_AURA,
           TG_POSITION, TG_OWNER_OR_SUMMONER, TG_RANDOM_POINT, TG_SUMMONED_CREATURES}
CREATURE_TARGETS = {TG_CREATURE_RANGE, TG_CREATURE_GUID, TG_CREATURE_DISTANCE, TG_CLOSEST_CREATURE, TG_CLOSEST_FRIENDLY,
                    TG_OWNER_OR_SUMMONER, TG_SUMMONED_CREATURES}
PLAYER_TARGETS = {TG_VICTIM, TG_HOSTILE_SECOND, TG_HOSTILE_LAST, TG_HOSTILE_RANDOM, TG_HOSTILE_RANDOM_NOT_TOP, TG_INVOKER,
                  TG_INVOKER_PARTY, TG_PLAYER_RANGE, TG_PLAYER_DISTANCE, TG_CLOSEST_PLAYER, TG_THREAT_LIST, TG_CLOSEST_ENEMY,
                  TG_LOOT_RECIPIENTS, TG_FARTHEST, TG_PLAYER_WITH_AURA}
# A stored list holds whatever was stored; the runtime sorts it at use.
MIXED_TARGETS = {TG_STORED, TG_INVOKER}
# Targets that name a place rather than a unit (only the actions listed with
# them read the row's coordinates).
POSITION_TARGETS = {TG_POSITION, TG_RANDOM_POINT}
UNIT_TARGETS = TARGETS - {TG_NONE} - POSITION_TARGETS
# SMART_ACTION ids (SmartScript::ProcessAction).
AC_TALK, AC_SET_FACTION, AC_SOUND, AC_PLAY_EMOTE, AC_SET_REACT_STATE, AC_RANDOM_EMOTE, AC_CAST = 1, 2, 4, 5, 8, 10, 11
AC_THREAT_SINGLE_PCT, AC_THREAT_ALL_PCT, AC_SET_EMOTE_STATE, AC_SET_UNIT_FLAG, AC_REMOVE_UNIT_FLAG = 13, 14, 17, 18, 19
AC_AUTO_ATTACK, AC_COMBAT_MOVE, AC_SET_PHASE, AC_INC_PHASE, AC_EVADE, AC_FLEE, AC_COMBAT_STOP, AC_REMOVE_AURAS = 20, 21, 22, 23, 24, 25, 27, 28
AC_RANDOM_PHASE, AC_RANDOM_PHASE_RANGE, AC_CALL_KILLEDMONSTER, AC_DIE, AC_CALL_FOR_HELP, AC_SET_SHEATH = 30, 31, 33, 37, 39, 40
AC_FORCE_DESPAWN, AC_SET_INVINCIBILITY_HP, AC_SET_DATA, AC_SET_ACTIVE, AC_ATTACK_START, AC_KILL_UNIT = 41, 42, 45, 48, 49, 51
AC_SET_RUN, AC_SET_COUNTER, AC_STORE_TARGET_LIST, AC_SET_ORIENTATION, AC_CREATE_TIMED_EVENT, AC_TRIGGER_TIMED_EVENT = 59, 63, 64, 66, 67, 73
AC_REMOVE_TIMED_EVENT, AC_ADD_AURA, AC_CALL_SCRIPT_RESET, AC_SET_RANGED_MOVEMENT, AC_TIMED_LIST, AC_SELF_CAST = 74, 75, 78, 79, 80, 85
AC_SET_UNIT_BYTES1, AC_REMOVE_UNIT_BYTES1, AC_INTERRUPT, AC_SET_DYNAMIC_FLAG, AC_ADD_DYNAMIC_FLAG, AC_REMOVE_DYNAMIC_FLAG = 90, 91, 92, 94, 95, 96
AC_SET_HEALTH_REGEN, AC_SET_POWER, AC_ADD_POWER, AC_REMOVE_POWER, AC_RANDOM_SOUND, AC_SET_CORPSE_DELAY = 102, 108, 109, 110, 115, 116
AC_DISABLE_EVADE, AC_REMOVE_AURAS_BY_TYPE, AC_SET_SIGHT_DIST, AC_ADD_THREAT, AC_TRIGGER_RANDOM_TIMED_EVENT = 117, 120, 121, 123, 125
AC_SET_HEALTH_PCT, AC_SET_COMBAT_DISTANCE, AC_ADD_IMMUNITY, AC_REMOVE_IMMUNITY, AC_SET_EVENT_FLAG_RESET = 142, 205, 208, 209, 211
AC_ATTACK_STOP, AC_SET_SCALE, AC_PLAY_SPELL_VISUAL = 224, 227, 229
# 2.38: summons, escort paths, point / random / follow movement, facing,
# walk/run, roots, visibility, zone combat, random timed lists.
AC_SUMMON_CREATURE, AC_FOLLOW, AC_SET_IN_COMBAT_WITH_ZONE, AC_MOUNT, AC_MOVE_FORWARD, AC_SET_VISIBILITY = 12, 29, 38, 43, 46, 47
AC_ESCORT_START, AC_ESCORT_PAUSE, AC_ESCORT_STOP, AC_SET_FLY, AC_SET_SWIM, AC_ESCORT_RESUME, AC_MOVE_TO_POS = 53, 54, 55, 60, 61, 65, 69
AC_EQUIP, AC_CALL_RANDOM_TIMED_LIST, AC_CALL_RANDOM_RANGE_TIMED_LIST, AC_RANDOM_MOVE, AC_JUMP_TO_POS = 71, 87, 88, 89, 97
AC_SET_HOME_POS, AC_SET_ROOT, AC_SET_MOVEMENT_FLAGS, AC_SET_HOVER = 101, 103, 204, 207
# 2.39: waypoint_data patrols, DoAction, instance data, npc flags, cross
# casts, stored-list hand-over, movement speed and stops.
AC_WAYPOINT_START, AC_WAYPOINT_DATA_RANDOM, AC_MOVEMENT_STOP, AC_MOVEMENT_PAUSE, AC_MOVEMENT_RESUME = 232, 233, 234, 235, 236
AC_DO_ACTION, AC_SET_INST_DATA, AC_SET_INST_DATA64, AC_SET_NPC_FLAG, AC_ADD_NPC_FLAG, AC_REMOVE_NPC_FLAG = 223, 34, 35, 81, 82, 83
AC_CROSS_CAST, AC_SEND_TARGET_TO_TARGET, AC_SET_MOVEMENT_SPEED, AC_STOP_MOTION, AC_MOVE_TO_POS_TARGET = 86, 100, 136, 212, 201
# 2.40: the gossip page and the quest actions.
AC_FAIL_QUEST, AC_OFFER_QUEST, AC_CLOSE_GOSSIP, AC_SEND_GOSSIP_MENU, AC_SET_GOSSIP_MENU = 6, 7, 72, 98, 240
AC_ADD_ITEM, AC_REMOVE_ITEM = 56, 57
# UNIT_NPC_FLAGS the runtime resolves (gossip, quest giver, vendors, trainers,
# repair, flight master, banker, innkeeper, auctioneer, stable, spellclick).
NPC_FLAG_LOCAL = 0x1 | 0x2 | 0x10 | 0x20 | 0x40 | 0x80 | 0x100 | 0x200 | 0x400 | 0x800 | 0x1000 | 0x2000 | 0x4000 | 0x8000 | 0x10000 | 0x20000 | 0x40000 | 0x80000 | 0x100000 | 0x1000000
# CROSS_CAST caster targets the runtime resolves: creature selectors and the creature itself.
CROSS_CASTER_TARGETS = {TG_SELF, TG_CREATURE_RANGE, TG_CREATURE_GUID, TG_CREATURE_DISTANCE, TG_STORED, TG_CLOSEST_CREATURE, TG_OWNER_OR_SUMMONER}
# Actions with no local effect beyond their parameters being sound: sounds,
# emotes, sheath, stand state, dynamic flags, scale, visuals, mounts, flying /
# swimming / hovering, equipment and movement flags (the client is not told;
# Creature::SetActive and REMOVE_AURAS_BY_TYPE have no handler at the pin
# either). SET_RUN and SET_ORIENTATION became real in 2.38.
COSMETIC_ACTIONS = {AC_SOUND, AC_PLAY_EMOTE, AC_RANDOM_EMOTE, AC_SET_EMOTE_STATE, AC_SET_SHEATH, AC_SET_ACTIVE,
                    AC_SET_UNIT_BYTES1, AC_REMOVE_UNIT_BYTES1, AC_SET_DYNAMIC_FLAG, AC_ADD_DYNAMIC_FLAG,
                    AC_REMOVE_DYNAMIC_FLAG, AC_RANDOM_SOUND, AC_REMOVE_AURAS_BY_TYPE, AC_SET_SCALE, AC_PLAY_SPELL_VISUAL,
                    AC_MOUNT, AC_SET_FLY, AC_SET_SWIM, AC_EQUIP, AC_SET_MOVEMENT_FLAGS, AC_SET_HOVER}
ACTIONS = {AC_TALK, AC_SET_FACTION, AC_SET_REACT_STATE, AC_CAST, AC_THREAT_SINGLE_PCT, AC_THREAT_ALL_PCT, AC_SET_UNIT_FLAG,
           AC_REMOVE_UNIT_FLAG, AC_AUTO_ATTACK, AC_COMBAT_MOVE, AC_SET_PHASE, AC_INC_PHASE, AC_EVADE, AC_FLEE, AC_COMBAT_STOP,
           AC_REMOVE_AURAS, AC_RANDOM_PHASE, AC_RANDOM_PHASE_RANGE, AC_CALL_KILLEDMONSTER, AC_DIE, AC_CALL_FOR_HELP,
           AC_FORCE_DESPAWN, AC_SET_INVINCIBILITY_HP, AC_SET_DATA, AC_ATTACK_START, AC_KILL_UNIT, AC_SET_COUNTER,
           AC_STORE_TARGET_LIST, AC_CREATE_TIMED_EVENT, AC_TRIGGER_TIMED_EVENT, AC_REMOVE_TIMED_EVENT, AC_ADD_AURA,
           AC_CALL_SCRIPT_RESET, AC_SET_RANGED_MOVEMENT, AC_TIMED_LIST, AC_SELF_CAST, AC_INTERRUPT, AC_SET_HEALTH_REGEN,
           AC_SET_POWER, AC_ADD_POWER, AC_REMOVE_POWER, AC_SET_CORPSE_DELAY, AC_DISABLE_EVADE, AC_SET_SIGHT_DIST, AC_ADD_THREAT,
           AC_TRIGGER_RANDOM_TIMED_EVENT, AC_SET_HEALTH_PCT, AC_SET_COMBAT_DISTANCE, AC_ADD_IMMUNITY, AC_REMOVE_IMMUNITY,
           AC_SET_EVENT_FLAG_RESET, AC_ATTACK_STOP,
           AC_SUMMON_CREATURE, AC_FOLLOW, AC_SET_IN_COMBAT_WITH_ZONE, AC_MOVE_FORWARD, AC_SET_VISIBILITY, AC_ESCORT_START,
           AC_ESCORT_PAUSE, AC_ESCORT_STOP, AC_ESCORT_RESUME, AC_MOVE_TO_POS, AC_CALL_RANDOM_TIMED_LIST,
           AC_CALL_RANDOM_RANGE_TIMED_LIST, AC_RANDOM_MOVE, AC_JUMP_TO_POS, AC_SET_HOME_POS, AC_SET_ROOT, AC_SET_RUN,
           AC_SET_ORIENTATION,
           AC_WAYPOINT_START, AC_WAYPOINT_DATA_RANDOM, AC_MOVEMENT_STOP, AC_MOVEMENT_PAUSE, AC_MOVEMENT_RESUME, AC_DO_ACTION,
           AC_SET_INST_DATA, AC_SET_INST_DATA64, AC_SET_NPC_FLAG, AC_ADD_NPC_FLAG, AC_REMOVE_NPC_FLAG, AC_CROSS_CAST,
           AC_SEND_TARGET_TO_TARGET, AC_SET_MOVEMENT_SPEED, AC_STOP_MOTION, AC_MOVE_TO_POS_TARGET,
           AC_FAIL_QUEST, AC_OFFER_QUEST, AC_CLOSE_GOSSIP, AC_SEND_GOSSIP_MENU, AC_SET_GOSSIP_MENU, AC_ADD_ITEM, AC_REMOVE_ITEM} | COSMETIC_ACTIONS
SPELL_ACTIONS = {AC_CAST, AC_SELF_CAST}
TALK_ACTIONS = {AC_TALK, AC_FLEE}
LIST_ACTIONS = {AC_TIMED_LIST, AC_CALL_RANDOM_TIMED_LIST, AC_CALL_RANDOM_RANGE_TIMED_LIST}
# The actions whose row coordinates (target_x..target_o) mean something: a
# summon or move destination, an offset from a unit target, a home position,
# a facing. Position targets are admitted for these alone.
POSITION_ACTIONS = {AC_SUMMON_CREATURE, AC_MOVE_TO_POS, AC_JUMP_TO_POS, AC_SET_HOME_POS, AC_SET_ORIENTATION, AC_MOVE_TO_POS_TARGET}
# TempSummonType 1..8 (TIMED_OR_DEAD, TIMED_OR_CORPSE, TIMED, TIMED_OUT_OF_COMBAT,
# CORPSE, CORPSE_TIMED, DEAD, MANUAL); 10 (TIMED_OOC_ALIVE) is AC-only and unused.
SUMMON_TYPES = {1, 2, 3, 4, 5, 6, 7, 8}
# SummonProperties.dbc categories and types the creature caster reproduces:
# wild / ally / pet summons of the plain, guardian, minion and wild kinds. Totems,
# companions, vehicles, puppets, lightwells and Jeeves are refused.
SUMMON_CATEGORIES = {0, 1, 2}
SUMMON_PROP_TYPES = {0, 1, 2, 3, 6, 7, 8}
SUMMON_PROP_FLAG_PERSONAL = 0x10
# SummonProperties ids whose summon count is the effect's BasePoints (Spell::EffectSummonType).
SUMMON_COUNT_PROPERTIES = {64, 61, 1101, 66, 648, 2301, 1061, 1261, 629, 181, 715, 1562, 833, 1161, 713}
MAX_SUMMONS_PER_CAST = 10
# UNIT_FIELD_FLAGS bits the runtime models (Unit::SetFlag): NON_ATTACKABLE,
# DISABLE_MOVE, IMMUNE_TO_PC, IMMUNE_TO_NPC (no creature ever attacks a local
# creature), PACIFIED, SILENCED, STUNNED, IN_COMBAT, NOT_SELECTABLE.
UNIT_FLAG_LOCAL = 0x2 | 0x4 | 0x100 | 0x200 | 0x2000 | 0x20000 | 0x40000 | 0x80000 | 0x2000000
# Unit::ApplySpellImmune types: SCHOOL (2), DAMAGE (3), MECHANIC (5), ID (6).
IMMUNITY_TYPES = {2, 3, 5, 6}


def dbc_table(path):
    data = path.read_bytes()
    magic, count, fields, size, strings = struct.unpack_from('<4s4I', data)
    assert magic == b'WDBC' and fields * 4 == size and len(data) == 20 + count * size + strings
    return {row[0]: row for row in (struct.unpack_from('<' + str(fields) + 'I', data, 20 + i * size) for i in range(count))}


spell_table = dbc_table


def load_tables():
    tables = {}
    with tempfile.TemporaryDirectory() as directory:
        for table, archive in ARCHIVES.items():
            with tarfile.open(HERE / archive) as tar:
                tar.extract(table + ".sql", directory, filter="data")
            tables[table] = list(sql_rows(Path(directory) / (table + ".sql"), table))
    return tables


AURA_PERIODIC_DAMAGE, AURA_DUMMY, AURA_MOD_CONFUSE, AURA_MOD_FEAR, AURA_PERIODIC_HEAL = 3, 4, 5, 7, 8
AURA_MOD_STUN, AURA_MOD_DAMAGE_DONE, AURA_MOD_DAMAGE_TAKEN, AURA_MOD_RESISTANCE, AURA_MOD_ROOT, AURA_MOD_SILENCE = 12, 13, 14, 22, 26, 27
AURA_MOD_STAT, AURA_MOD_DECREASE_SPEED, AURA_PERIODIC_LEECH, AURA_TRANSFORM, AURA_MOD_SCALE = 29, 33, 53, 56, 61
AURA_MOD_DAMAGE_PERCENT_DONE, AURA_MOD_DAMAGE_PERCENT_TAKEN, AURA_MOD_ATTACK_POWER, AURA_MOD_RESISTANCE_PCT = 79, 87, 99, 101
AURA_MOD_HEALING_PCT, AURA_MOD_MELEE_HASTE = 118, 138
AURA_INTERRUPT_TAKE_DAMAGE = 0x2
EFFECT_SCHOOL_DAMAGE, EFFECT_DUMMY, EFFECT_APPLY_AURA, EFFECT_HEALTH_LEECH, EFFECT_HEAL = 2, 3, 6, 9, 10
EFFECT_WEAPON_DAMAGE_NOSCHOOL, EFFECT_WEAPON_PERCENT_DAMAGE, EFFECT_WEAPON_DAMAGE, EFFECT_INTERRUPT_CAST = 17, 31, 58, 68
EFFECT_TRIGGER_SPELL, EFFECT_KNOCK_BACK, EFFECT_NORMALIZED_WEAPON_DMG, EFFECT_KNOCK_BACK_DEST = 64, 98, 121, 144
AURA_PERIODIC_TRIGGER_SPELL = 23
CREATURE_FLAG_EXTRA_NO_PLAYER_DAMAGE_REQ = 0x200
# Target shapes the creature caster decoder keeps (local_npc_spell_import.hpp).
SHAPE_ENEMY, SHAPE_SELF, SHAPE_AOE, SHAPE_AOE_TARGET, SHAPE_CONE, SHAPE_AOE_ALLY, SHAPE_AOE_DEST = 0, 1, 2, 3, 4, 5, 6
HOSTILE_SHAPES = {SHAPE_ENEMY, SHAPE_AOE, SHAPE_AOE_TARGET, SHAPE_CONE, SHAPE_AOE_DEST}
EFFECT_SUMMON, EFFECT_PERSISTENT_AREA_AURA = 28, 27
# 2.39: instakill (on the caster), script effects without a script row, self
# stuns / roots, invisibility, permanent periodic triggers, self triggers.
EFFECT_INSTAKILL, EFFECT_SCRIPT_EFFECT, AURA_MOD_INVISIBILITY = 1, 77, 18
# Spell::EffectScriptEffect's own switch (SpellEffects.cpp at the pin): the ids
# it handles in code; any other script effect only starts a spell_scripts row.
SCRIPT_EFFECT_CODED = {22539, 22972, 22975, 22976, 22977, 22978, 22979, 22980, 22981, 22982, 22983, 22984, 22985, 31666, 32307,
                       41931, 52173, 54640, 57347, 57349, 58418, 58420, 58428, 60243, 61263}
SPELL_SCRIPT_ROWS = set()  # spell ids with spell_scripts rows (filled by main / the callers)
POSITIVE_KINDS = {'heal', 'periodicHeal', 'haste', 'damagePct', 'damageFlat', 'attackPower', 'cosmetic'}
CONTROL_KINDS = {'stun': 1, 'root': 2, 'fear': 3, 'confuse': 4, 'silence': 5}
SPELL_RULES = {}
# 2.38: creature entries a summon may name (profiles() fills it from the
# templates: a model, no trigger flag, no vehicle; the catalog patch tool
# adds their definitions, tools/local_realm/patch_summon_catalog.py).
SUMMONABLE = set()


def summonable_entries(tables):
    templates = {r['entry']: r for r in tables['creature_template']}
    models = {}
    for r in sorted(tables['creature_template_model'], key=lambda r: r['idx']):
        if r['creaturedisplayid']:
            models.setdefault(r['creatureid'], r)
    out = set()
    for entry, t in templates.items():
        if entry in models and not t['flags_extra'] & 128 and not t['vehicleid'] and 1 <= t['minlevel'] <= 83:
            out.add(entry)
    return out


def load_spell_rules():
    """Spell ids the source treats specially outside Spell.dbc."""
    if SPELL_RULES:
        return SPELL_RULES
    import re
    special = set()
    archive = HERE / 'spell_rules_source_sql.tar.gz'
    assert hashlib.sha256(archive.read_bytes()).hexdigest() == SOURCE_HASHES['spell_rules_source_sql.tar.gz'], 'Unexpected spell rules archive'
    with tempfile.TemporaryDirectory() as directory, tarfile.open(archive) as tar:
        tar.extractall(directory, filter='data')
        root = Path(directory)
        for row in sql_rows(root / 'spell_group.sql', 'spell_group'):
            special.add(abs(row['spell_id']))
        for row in sql_rows(root / 'spell_linked_spell.sql', 'spell_linked_spell'):
            special.update((abs(row['spell_trigger']), abs(row['spell_effect'])))
        for row in sql_rows(root / 'spell_custom_attr.sql', 'spell_custom_attr'):
            special.add(row['spell_id'])
        # spell_proc: the explicit proc entries; every other proc aura takes
        # the Spell.dbc columns (SpellMgr::LoadSpellProcs' generated entries).
        proc = {}
        for row in sql_rows(root / 'spell_proc.sql', 'spell_proc'):
            proc[abs(row['spellid'])] = row
        SPELL_RULES['proc'] = proc
        # Hard-coded C++ corrections: any id-like number is treated as named.
        for name in ('SpellInfoCorrections.cpp', 'SpellMgr.cpp'):
            special.update(int(v) for v in re.findall(r'\b(\d{2,6})\b', (root / name).read_text(errors='replace')))
    SPELL_RULES['special'] = special
    geometry = HERE / 'spell_geometry_source_sql.tar.gz'
    assert hashlib.sha256(geometry.read_bytes()).hexdigest() == SOURCE_HASHES['spell_geometry_source_sql.tar.gz'], 'Unexpected spell geometry archive'
    with tempfile.TemporaryDirectory() as directory, tarfile.open(geometry) as tar:
        tar.extractall(directory, filter='data')
        root = Path(directory)
        SPELL_RULES['cone'] = {row['id']: row['conedegrees'] for row in sql_rows(root / 'spell_cone.sql', 'spell_cone')}
        SPELL_RULES['jump'] = {row['id']: row['jumpdistance'] for row in sql_rows(root / 'spell_jump_distance.sql', 'spell_jump_distance')}
    return SPELL_RULES


def as_int(value):
    return value - (1 << 32) if value & 0x80000000 else value


def as_float(value):
    return struct.unpack('<f', struct.pack('<I', value))[0]


class Dbc:
    """The client tables the creature caster decoder reads."""

    def __init__(self, directory):
        self.spells = dbc_table(directory / 'Spell.dbc')
        self.radii = dbc_table(directory / 'SpellRadius.dbc')
        self.ranges = dbc_table(directory / 'SpellRange.dbc')
        self.durations = dbc_table(directory / 'SpellDuration.dbc')
        self.casts = dbc_table(directory / 'SpellCastTimes.dbc')
        self.maps = dbc_table(directory / 'Map.dbc')
        # 2.38: SummonProperties.dbc (id, category, faction, type, slot, flags).
        self.summons = dbc_table(directory / 'SummonProperties.dbc')


def spell_shape(effect_target):
    """(EffectImplicitTargetA, B) -> local target shape, or None.

    2.38 adds SHAPE_AOE_DEST: the enemies around the spell's destination,
    which Spell::InitExplicitTargets sets to the explicit unit target's
    position (or the caster's without one)."""
    return {(6, 0): SHAPE_ENEMY, (25, 0): SHAPE_ENEMY, (1, 0): SHAPE_SELF, (21, 0): SHAPE_SELF, (20, 0): SHAPE_SELF, (33, 0): SHAPE_SELF,
            (22, 15): SHAPE_AOE, (18, 16): SHAPE_AOE, (53, 16): SHAPE_AOE_TARGET,
            (24, 0): SHAPE_CONE, (54, 0): SHAPE_CONE, (104, 0): SHAPE_CONE,
            (22, 30): SHAPE_AOE_ALLY, (18, 31): SHAPE_AOE_ALLY,
            (16, 0): SHAPE_AOE_DEST, (63, 16): SHAPE_AOE_DEST}.get(effect_target)


# 2.38: where a destination-only effect (a summon, a persistent area aura)
# lands. Spell::SelectImplicitCasterDestTargets / TargetDestTargets /
# DestDestTargets: the caster (18, 22+dest), the summon spot in front-left of
# the caster (32), a direction from the caster at the effect radius (41-44,
# 47-50), a random point around the caster (72: random distance, 73 / 18+86:
# at the radius), the explicit target's position (53, 63, 16, 28) or that
# position for a dynamic object (28).
DEST_CASTER, DEST_SUMMON, DEST_FRONT, DEST_BACK, DEST_RIGHT, DEST_LEFT = 1, 2, 3, 4, 5, 6
DEST_FRONT_RIGHT, DEST_BACK_RIGHT, DEST_BACK_LEFT, DEST_FRONT_LEFT, DEST_RANDOM, DEST_RADIUS, DEST_TARGET = 7, 8, 9, 10, 11, 12, 13
DEST_BY_TARGET = {18: DEST_CASTER, 32: DEST_SUMMON, 47: DEST_FRONT, 48: DEST_BACK, 49: DEST_RIGHT, 50: DEST_LEFT,
                  41: DEST_FRONT_RIGHT, 42: DEST_BACK_RIGHT, 43: DEST_BACK_LEFT, 44: DEST_FRONT_LEFT, 72: DEST_RANDOM, 73: DEST_RADIUS,
                  53: DEST_TARGET, 63: DEST_TARGET, 16: DEST_TARGET, 28: DEST_TARGET}
CASTER_DESTS = {DEST_CASTER, DEST_SUMMON, DEST_FRONT, DEST_BACK, DEST_RIGHT, DEST_LEFT, DEST_FRONT_RIGHT, DEST_BACK_RIGHT,
                DEST_BACK_LEFT, DEST_FRONT_LEFT, DEST_RANDOM, DEST_RADIUS}


def spell_dest(effect_target):
    """(TargetA, TargetB) of a destination-only effect -> DEST_* code, or None.

    TargetB 0 takes TargetA's destination; SRC_CASTER (22) with a destination
    B behaves as that B alone; DEST_CASTER (18) with DEST_DEST_RANDOM (86) is
    a random point at the radius; DEST_CASTER with a unit-area B (16, 28)
    keeps the caster as destination."""
    a, b = effect_target
    if b == 0:
        return DEST_BY_TARGET.get(a)
    if a == 22 and b in DEST_BY_TARGET:
        return DEST_BY_TARGET[b]
    if a == 18 and b == 86:
        return DEST_RADIUS
    if a == 18 and b in (16, 28):
        return DEST_CASTER
    if a in (53, 63, 16) and b in (16, 28):
        return DEST_TARGET
    return None


# Kinds an aura effect resolves to; the shape decides who receives them. Player
# targets (enemy shapes) take the view kinds, the creature itself or its allies
# take the buff kinds; both take the shared ones.
PLAYER_AURA_KINDS = {'periodic', 'periodicLeech', 'periodicTrigger', 'slow', 'armor', 'armorPct', 'resistance', 'stun', 'root', 'fear',
                     'confuse', 'silence', 'damageTakenFlat', 'damageTakenPct', 'healingPct', 'haste', 'damagePct', 'damageFlat',
                     'attackPower', 'castSpeed', 'hitChance', 'disarm', 'dodge', 'parry', 'block', 'cosmetic'}
CREATURE_AURA_KINDS = {'periodicHeal', 'periodicTrigger', 'slow', 'speed', 'armor', 'armorPct', 'resistance', 'damageTakenFlat',
                       'damageTakenPct', 'healingPct', 'haste', 'damagePct', 'damageFlat', 'attackPower', 'castSpeed', 'hitChance',
                       'dodge', 'parry', 'block', 'absorb', 'schoolImmunity', 'damageImmunity', 'mechanicImmunity', 'maxHealth',
                       'maxHealthPct', 'damageShield', 'procTrigger', 'procDamage', 'cosmetic',
                       'selfStun', 'selfRoot', 'invisible'}
# Direct (non-aura) kinds by side: hostile ones need an enemy shape, friendly
# ones the creature itself or an ally, the utility ones a player target that
# is neither rolled against nor threatened.
HOSTILE_KINDS = {'damage', 'weapon', 'weaponPercent', 'leech', 'interrupt', 'knockback', 'trigger', 'powerBurn', 'powerDrain',
                 'dispel', 'dispelMechanic', 'charge', 'threatPct'}
FRIENDLY_KINDS = {'heal', 'healPct', 'healMax', 'energize', 'energizePct', 'extraAttacks', 'instakillSelf', 'triggerSelf'}
UTILITY_KINDS = {'killCredit', 'createItem'}
TIMED_KINDS = {'periodic', 'periodicLeech', 'periodicHeal', 'periodicTrigger', 'slow', 'speed', 'armor', 'armorPct', 'resistance',
               'stun', 'root', 'fear', 'confuse', 'silence', 'damageTakenFlat', 'damageTakenPct', 'healingPct', 'haste',
               'selfStun', 'selfRoot', 'invisible',
               'castSpeed', 'hitChance', 'disarm', 'dodge', 'parry', 'block', 'damagePct', 'damageFlat', 'attackPower',
               'absorb', 'schoolImmunity', 'damageImmunity', 'mechanicImmunity', 'maxHealth', 'maxHealthPct',
               'damageShield', 'procTrigger', 'procDamage'}
AURA_MOD_CASTING_SPEED_NOT_STACK, AURA_MOD_HIT_CHANCE, AURA_MOD_DISARM, AURA_MELEE_SLOW = 65, 54, 67, 193
AURA_SCHOOL_ABSORB, AURA_SCHOOL_IMMUNITY, AURA_DAMAGE_IMMUNITY, AURA_MECHANIC_IMMUNITY, AURA_DISPEL_IMMUNITY = 69, 39, 40, 77, 41
AURA_MOD_INCREASE_HEALTH, AURA_MOD_INCREASE_HEALTH_PERCENT, AURA_MOD_DODGE_PERCENT, AURA_MOD_PARRY_PERCENT = 34, 133, 49, 47
AURA_MOD_BLOCK_PERCENT, AURA_DAMAGE_SHIELD, AURA_PROC_TRIGGER_SPELL, AURA_PROC_TRIGGER_DAMAGE, AURA_MOD_INCREASE_SPEED = 51, 15, 42, 43, 31
AURA_MOD_REGEN, AURA_MOD_POWER_REGEN, AURA_MOD_HEALTH_REGEN_PERCENT, AURA_MOD_RANGED_ATTACK_POWER = 84, 85, 88, 124
AURA_MOD_SHAPESHIFT, AURA_MOD_SKILL, AURA_MOD_TAUNT, AURA_MOD_TOTAL_STAT_PERCENTAGE = 36, 30, 11, 137
EFFECT_POWER_DRAIN, EFFECT_ADD_EXTRA_ATTACKS, EFFECT_CREATE_ITEM, EFFECT_ENERGIZE, EFFECT_APPLY_AREA_AURA_PARTY = 8, 19, 24, 30, 35
EFFECT_DISPEL, EFFECT_SPAWN, EFFECT_POWER_BURN, EFFECT_HEAL_MAX_HEALTH, EFFECT_HEAL_MECHANICAL, EFFECT_CHARGE = 38, 46, 62, 67, 75, 96
EFFECT_DISPEL_MECHANIC, EFFECT_ATTACK_ME, EFFECT_MODIFY_THREAT_PERCENT, EFFECT_KILL_CREDIT2, EFFECT_HEAL_PCT, EFFECT_ENERGIZE_PCT = 108, 114, 125, 134, 136, 137
# Spell.dbc proc columns and the proc flags a local creature can observe.
PROC_TAKEN_FLAGS = 0x8 | 0x20 | 0x80 | 0x200 | 0x2000 | 0x20000 | 0x80000 | 0x100000
PROC_DONE_FLAGS = 0x4 | 0x10 | 0x40 | 0x100 | 0x1000 | 0x10000 | 0x40000 | 0x400000 | 0x800000
PROC_LOCAL_FLAGS = PROC_TAKEN_FLAGS | PROC_DONE_FLAGS | 0x2 | 0x1 | 0x1000000
MAX_AURA_MS = 3600000
SPELL_INFO = {}


def spell_blocker(spell, dbc, scripted, spell_conditions, depth=0):
    """None when the creature caster decoder reproduces the spell, else the reason.

    Mirrors decodeLocalNpcGenericSpell / decodeClientSpell(creatureCaster) column
    for column; anything the C++ refuses at runtime leaves its owner inert, so
    the two must agree. On success SPELL_INFO records the resolved shape.
    """
    spells = dbc.spells
    row = spells.get(spell)
    if row is None:
        return "spell missing"
    if spell in scripted or spell in spell_conditions:
        return "spell script or condition"
    rules = load_spell_rules()
    if spell in rules['special']:
        return "spell group, link, custom attribute or C++ special case"
    if row[4] & SPELL_ATTR0_PASSIVE:
        return "passive spell"
    if row[213] > 3:
        return "classless spell"
    # decodeClientSpell metadata the creature path shares.
    if row[2] > 11 or row[3] > 31 or any(row[83 + k] > 31 for k in range(3)) or row[214] > 2 or row[49] > 255:
        return "unsupported spell metadata"
    # Creatures never take ordinary spell cooldowns at the pin (Spell::SendSpellCooldown
    # returns before adding one), so the category metadata is not checked.
    if row[1] > 100000 or row[208] > 1000:
        return "unsupported spell metadata"
    if row[41] not in (0, 1, 3, 5, 6) or (row[41] == 5 and (row[42] or row[204])) or row[226] or row[44] or row[45]:
        return "spell power cost"
    if (row[42] or row[204]) and (row[41] != 0 or row[5] & SPELL_ATTR1_USE_ALL_MANA):
        return "spell power cost"
    if any(row[c] for c in range(20, 28)) or any(as_int(row[52 + r]) > 0 for r in range(8)) or row[50] or row[51]:
        return "aura, reagent or totem requirement"
    if as_int(row[68]) >= 0 and as_int(row[68]) not in (2, 4):
        return "unsupported spell metadata"
    auras = [row[95 + k] if row[71 + k] == EFFECT_APPLY_AURA else 0 for k in range(3)]
    # Spell.dbc proc columns (34 flags, 35 chance, 36 charges): a proc trigger
    # aura owns them (SpellMgr::LoadSpellProcs' generated entry, or the
    # spell_proc row), a control aura spends charges on damage taken
    # (AuraEffect::CheckEffectProc: one charge per damaging hit) and any other
    # spell carrying them is refused.
    proc_flags, proc_chance, proc_charges = row[34], row[35], row[36]
    proc_aura = any(a in (AURA_PROC_TRIGGER_SPELL, AURA_PROC_TRIGGER_DAMAGE) for a in auras)
    proc_row = rules['proc'].get(spell)
    if proc_aura:
        if proc_row is not None:
            if proc_row['procflags']:
                proc_flags = proc_row['procflags']
            if proc_row['charges']:
                proc_charges = proc_row['charges']
            if proc_row['chance'] or proc_row['procsperminute']:
                proc_chance = proc_row['chance']
            if proc_row['procsperminute'] or proc_row['schoolmask'] or proc_row['spellfamilyname'] or proc_row['spelltypemask'] not in (0, 7) or \
                    proc_row['spellphasemask'] not in (0, 2) or proc_row['hitmask'] or proc_row['attributesmask'] or proc_row['disableeffectsmask'] or \
                    proc_row['cooldown'] or not float(proc_row['chance']).is_integer():
                return "proc definition"
            proc_chance = int(proc_chance)
        if not proc_flags & PROC_LOCAL_FLAGS or proc_flags & ~PROC_LOCAL_FLAGS or proc_chance > 100 or proc_charges > 99:
            return "proc definition"
        # The generated entry filters the triggering spell by the aura effects'
        # SpellClassMask (columns 122-130); none of the local creature auras may
        # carry one.
        if any(row[122 + c] for c in range(9)):
            return "proc definition"
    elif proc_flags or proc_charges:
        if proc_row is not None or proc_chance > 100 or proc_charges > 99 or proc_flags & ~PROC_TAKEN_FLAGS or not proc_flags & 0x100000 or \
                not any(a in (AURA_MOD_STUN, AURA_MOD_ROOT, AURA_MOD_FEAR, AURA_MOD_CONFUSE, AURA_TRANSFORM) for a in auras):
            return "proc definition"
    cast = dbc.casts.get(row[28])
    if cast is None or as_int(cast[1]) < 0 or as_int(cast[1]) > 60000 or as_int(cast[2]) != 0:
        return "unsupported cast time"
    duration_ms = 0
    permanent = False
    if row[40]:
        duration = dbc.durations.get(row[40])
        if duration is None or as_int(duration[2]) != 0:
            return "unsupported duration"
        if 0 < as_int(duration[1]) <= MAX_AURA_MS:
            duration_ms = as_int(duration[1])
        # SpellDuration -1 / -1: an aura that lasts until removed (2.39: a
        # permanent periodic trigger on the creature itself).
        permanent = as_int(duration[1]) == -1 and as_int(duration[3]) == -1
    rng = dbc.ranges.get(row[46])
    if rng is None or rng[5] > 2:
        return "unsupported range"
    for column in (1, 2, 3, 4):
        value = as_float(rng[column])
        if not math.isfinite(value) or value < 0 or value > 50000:
            return "unsupported range"
    if as_float(rng[3]) < as_float(rng[1]) or as_float(rng[4]) < as_float(rng[2]):
        return "unsupported range"
    if row[42] > 100000 or row[204] > 100 or row[29] > 3600000 or row[30] > 3600000 or row[206] > 60000:
        return "unsupported spell metadata"
    if row[225] > 127:
        return "unsupported spell metadata"
    channeled = bool(row[5] & 0x44)
    kinds, real_shapes, cosmetic_shapes, chain = [], set(), set(), 0
    control = None
    trigger_self_hostile = False
    summon_entry, ground = 0, False
    unit_auras = False
    for k in range(3):
        effect = row[71 + k]
        if not effect:
            continue
        target = (row[86 + k], row[89 + k])
        # APPLY_AREA_AURA_PARTY: a creature's party is itself (Unit::GetPartyMembers).
        if effect == EFFECT_APPLY_AREA_AURA_PARTY:
            effect, target = EFFECT_APPLY_AURA, (1, 0)
        shape = spell_shape(target)
        aura = row[95 + k] if effect in (EFFECT_APPLY_AURA, EFFECT_PERSISTENT_AREA_AURA) else 0
        base, dice, per_level = as_int(row[80 + k]), as_int(row[74 + k]), as_float(row[77 + k])
        amount = base + (1 if dice >= 1 else 0)
        misc = as_int(row[110 + k])
        amplitude, trigger = row[98 + k], row[116 + k]
        if base < -100000 or base > 100000 or dice < 0 or dice > 100000 or not math.isfinite(per_level) or abs(per_level) > 10000:
            return "unsupported spell effects"
        if trigger and not (effect == EFFECT_TRIGGER_SPELL or (effect == EFFECT_APPLY_AURA and aura in (AURA_PERIODIC_TRIGGER_SPELL, AURA_PROC_TRIGGER_SPELL))):
            return "unsupported spell effects"
        # 2.38: a destination-only effect (a summon, a persistent area aura)
        # takes its place from the pair; a cosmetic effect (DUMMY, SPAWN,
        # ATTACK_ME) at a destination or with no target at all imposes no shape.
        dest = None
        if effect in (EFFECT_SUMMON, EFFECT_PERSISTENT_AREA_AURA):
            dest = spell_dest(target)
            if dest is None:
                return "unsupported spell effects"
            shape = SHAPE_AOE if dest in CASTER_DESTS else SHAPE_AOE_DEST
        elif effect in (EFFECT_DUMMY, EFFECT_SPAWN, EFFECT_ATTACK_ME) and shape is None and (target == (0, 0) or spell_dest(target) is not None):
            kinds.append('cosmetic')
            continue
        if shape is None:
            return "unsupported spell effects"
        radius_yd = 0.0
        if shape in (SHAPE_AOE, SHAPE_AOE_TARGET, SHAPE_CONE, SHAPE_AOE_ALLY, SHAPE_AOE_DEST) and effect not in (EFFECT_SUMMON, EFFECT_PERSISTENT_AREA_AURA):
            radius = dbc.radii.get(row[92 + k])
            if radius is None or not 0 < as_float(radius[1]) <= 100 or as_float(radius[2]) != 0 or as_float(radius[3]) < as_float(radius[1]):
                return "unsupported spell effects"
            radius_yd = as_float(radius[1])
        elif effect in (EFFECT_SUMMON, EFFECT_PERSISTENT_AREA_AURA):
            # The radius is the placement distance of a directional or random
            # destination and the spread of several summons (no row: 0).
            if row[92 + k]:
                radius = dbc.radii.get(row[92 + k])
                if radius is None or not 0 <= as_float(radius[1]) <= 100 or as_float(radius[2]) != 0 or as_float(radius[3]) < as_float(radius[1]):
                    return "unsupported spell effects"
                radius_yd = as_float(radius[1])
        if row[104 + k] > 1:
            if shape != SHAPE_ENEMY:
                return "unsupported spell effects"
            multiplier = as_float(row[216 + k])
            if not math.isfinite(multiplier) or multiplier <= 0 or multiplier > 10 or (chain and chain != row[104 + k]):
                return "unsupported spell effects"
            chain = row[104 + k]
        aura_effect = effect in (EFFECT_APPLY_AURA, EFFECT_PERSISTENT_AREA_AURA)
        kind = None
        if effect == EFFECT_SUMMON:
            # Spell::EffectSummonType: the entry (MiscValue), the SummonProperties
            # row (MiscValueB), the count from BasePoints for the listed
            # properties, the duration from the spell; a wild summon keeps its
            # template faction, an ally / pet summon takes the summoner's.
            props = dbc.summons.get(row[113 + k])
            if props is None or misc <= 0 or summon_entry or props[1] not in SUMMON_CATEGORIES or props[3] not in SUMMON_PROP_TYPES or \
                    props[5] & SUMMON_PROP_FLAG_PERSONAL or misc not in SUMMONABLE:
                return "unsupported summon"
            count = 1
            if row[113 + k] in SUMMON_COUNT_PROPERTIES:
                if dice > 1 or per_level != 0:
                    return "unsupported summon"
                count = amount if amount > 0 else 1
            if count > MAX_SUMMONS_PER_CAST or (row[40] and not duration_ms and not permanent):
                return "unsupported summon"
            summon_entry = misc
            kind = 'summon'
        elif effect == EFFECT_PERSISTENT_AREA_AURA:
            # Spell::EffectPersistentAA: a dynamic object of the effect radius
            # for the spell's duration; its aura effect lands on the enemies
            # inside it (DynObjAura::FillTargetMap) and leaves with them.
            if ground or radius_yd <= 0 or not duration_ms or target[1] not in (0, 16, 28):
                return "unsupported ground aura"
            ground = True
        if kind is not None:
            pass
        elif effect == EFFECT_SCHOOL_DAMAGE:
            kind = 'damage'
        elif effect == EFFECT_INSTAKILL:
            # Spell::EffectInstaKill on the caster itself (Unit::Kill(me, me)).
            if shape != SHAPE_SELF:
                return "unsupported spell effects"
            kind = 'instakillSelf'
        elif effect == EFFECT_SCRIPT_EFFECT:
            # Spell::EffectScriptEffect: the coded ids do something; any other
            # spell only starts its spell_scripts rows - none: nothing happens.
            if spell in SCRIPT_EFFECT_CODED or spell in SPELL_SCRIPT_ROWS:
                return "unsupported spell effects"
            kind = 'cosmetic'
        elif effect in (EFFECT_WEAPON_DAMAGE, EFFECT_WEAPON_DAMAGE_NOSCHOOL, EFFECT_NORMALIZED_WEAPON_DMG):
            # Spell::EffectWeaponDmg: the creature's weapon roll plus a bonus; a
            # non-melee damage class rolls the magic hit table first.
            if amount < 0:
                return "unsupported spell effects"
            kind = 'weapon'
        elif effect == EFFECT_WEAPON_PERCENT_DAMAGE:
            if per_level != 0 or not 0 < amount <= 1000 or amount + dice > 1000:
                return "unsupported spell effects"
            kind = 'weaponPercent'
        elif effect == EFFECT_HEALTH_LEECH:
            multiplier = as_float(row[101 + k])
            if not math.isfinite(multiplier) or multiplier < 0 or multiplier > 10:
                return "unsupported spell effects"
            kind = 'leech'
        elif effect in (EFFECT_HEAL, EFFECT_HEAL_MECHANICAL):
            kind = 'heal'
        elif effect == EFFECT_HEAL_PCT:
            if dice > 1 or not 0 < amount <= 100:
                return "unsupported spell effects"
            kind = 'healPct'
        elif effect == EFFECT_HEAL_MAX_HEALTH:
            kind = 'healMax'
        elif effect in (EFFECT_ENERGIZE, EFFECT_ENERGIZE_PCT):
            # Spell::EffectEnergize(Pct): mana only; a creature without mana gains nothing.
            if misc != 0 or amount < 0 or (effect == EFFECT_ENERGIZE_PCT and (dice > 1 or amount > 100)):
                return "unsupported spell effects"
            kind = 'energize' if effect == EFFECT_ENERGIZE else 'energizePct'
        elif effect == EFFECT_INTERRUPT_CAST:
            if not duration_ms:
                return "unsupported spell effects"
            kind = 'interrupt'
        elif effect in (EFFECT_KNOCK_BACK, EFFECT_KNOCK_BACK_DEST):
            if misc < 0 or misc > 10000 or amount < 0 or amount > 10000 or (misc <= 1 and amount <= 1):
                return "unsupported spell effects"
            kind = 'knockback'
        elif effect in (EFFECT_DUMMY, EFFECT_SPAWN, EFFECT_ATTACK_ME):
            # EffectNULL / EffectAttackMe on a player: nothing happens.
            kind = 'cosmetic'
        elif effect == EFFECT_POWER_BURN:
            multiplier = as_float(row[101 + k])
            if misc != 0 or amount < 0 or not math.isfinite(multiplier) or multiplier < 0 or multiplier > 10:
                return "unsupported spell effects"
            kind = 'powerBurn'
        elif effect == EFFECT_POWER_DRAIN:
            multiplier = as_float(row[101 + k])
            if misc != 0 or amount < 0 or not math.isfinite(multiplier) or multiplier < 0 or multiplier > 10:
                return "unsupported spell effects"
            kind = 'powerDrain'
        elif effect == EFFECT_DISPEL:
            # Spell::EffectDispel: a magic (1), curse (2), disease (3) or poison
            # (4) dispel of `amount` auras.
            if misc not in (1, 2, 3, 4) or dice > 1 or not 0 < amount <= 10:
                return "unsupported spell effects"
            kind = 'dispel'
        elif effect == EFFECT_DISPEL_MECHANIC:
            if misc <= 0 or misc > 31 or dice > 1 or not 0 < amount <= 10:
                return "unsupported spell effects"
            kind = 'dispelMechanic'
        elif effect == EFFECT_CHARGE:
            kind = 'charge'
        elif effect == EFFECT_MODIFY_THREAT_PERCENT:
            if dice > 1 or not -100 <= amount <= 1000:
                return "unsupported spell effects"
            kind = 'threatPct'
        elif effect == EFFECT_KILL_CREDIT2:
            if misc <= 0:
                return "unsupported spell effects"
            kind = 'killCredit'
        elif effect == EFFECT_CREATE_ITEM:
            # Spell::EffectCreateItem: EffectItemType (column 107 + k; 2.39 fix -
            # MiscValue was read before and every creature item spell refused),
            # a count of CalcValue clamped to at least one.
            if as_int(row[107 + k]) <= 0 or dice > 1 or amount > 20 or amount < 0:
                return "unsupported spell effects"
            kind = 'createItem'
        elif effect == EFFECT_ADD_EXTRA_ATTACKS:
            if dice > 1 or not 0 < amount <= 10:
                return "unsupported spell effects"
            kind = 'extraAttacks'
        elif effect == EFFECT_TRIGGER_SPELL or (effect == EFFECT_APPLY_AURA and aura == AURA_PERIODIC_TRIGGER_SPELL):
            # Spell::EffectTriggerSpell / HandlePeriodicTriggerSpellAuraTick: the
            # triggered spell is cast by the creature at the same target (or by its
            # own shape around the creature) and may not trigger further.
            if depth or not trigger or trigger == spell:
                return "unsupported spell effects"
            if effect == EFFECT_APPLY_AURA and not (amplitude > 0 and (amplitude <= duration_ms or (permanent and shape == SHAPE_SELF))):
                return "unsupported spell effects"
            inner = spell_blocker(trigger, dbc, scripted, spell_conditions, depth + 1)
            if inner:
                return "unsupported spell effects"
            if effect == EFFECT_TRIGGER_SPELL and shape == SHAPE_SELF:
                # 2.39: the creature casts the triggered spell on itself; a
                # caster-centred triggered spell keeps its own shape. One
                # trigger per spell (the runtime carries a single triggered id).
                inner_shape = spell_shape_of(trigger, spells)
                if inner_shape in (None, SHAPE_ENEMY) or 'triggerSelf' in kinds or 'trigger' in kinds:
                    return "unsupported spell effects"
                if inner_shape != SHAPE_SELF:
                    shape = inner_shape
                    trigger_self_hostile = True
                kind = 'triggerSelf'
                kinds.append(kind)
                real_shapes.add(shape)
                continue
            # HandlePeriodicTriggerSpellAuraTick: the aura target casts unless the
            # triggered spell needs an explicit unit target; on the creature
            # itself any caster-relative shape works, on a player only a
            # creature-cast enemy shape does.
            if effect == EFFECT_APPLY_AURA and (spell_shape_of(trigger, spells) != SHAPE_ENEMY) != (shape == SHAPE_SELF):
                return "unsupported spell effects"
            if effect == EFFECT_APPLY_AURA and shape == SHAPE_SELF and spell_shape_of(trigger, spells) == SHAPE_ENEMY:
                return "unsupported spell effects"
            kind = 'trigger' if effect == EFFECT_TRIGGER_SPELL else 'periodicTrigger'
        elif aura_effect and aura == AURA_PROC_TRIGGER_SPELL:
            # AuraEffect::HandleProcTriggerSpellAuraProc: the aura owner casts
            # the triggered spell at the other party of the proc event.
            if depth or not trigger or trigger == spell or not proc_flags:
                return "unsupported spell effects"
            if spell_blocker(trigger, dbc, scripted, spell_conditions, depth + 1):
                return "unsupported spell effects"
            if spell_shape_of(trigger, spells) not in (SHAPE_ENEMY, SHAPE_SELF, SHAPE_AOE, SHAPE_CONE, SHAPE_AOE_ALLY):
                return "unsupported spell effects"
            kind = 'procTrigger'
        elif aura_effect and aura == AURA_PROC_TRIGGER_DAMAGE:
            if not proc_flags or amount <= 0 or not row[225]:
                return "unsupported spell effects"
            kind = 'procDamage'
        elif aura_effect and aura == AURA_PERIODIC_DAMAGE:
            if not 0 < amplitude <= duration_ms:
                return "unsupported spell effects"
            kind = 'periodic'
        elif aura_effect and aura == AURA_PERIODIC_LEECH:
            multiplier = as_float(row[101 + k])
            if not 0 < amplitude <= duration_ms or not math.isfinite(multiplier) or multiplier < 0 or multiplier > 10:
                return "unsupported spell effects"
            kind = 'periodicLeech'
        elif aura_effect and aura == AURA_PERIODIC_HEAL:
            if not 0 < amplitude <= duration_ms or amount <= 0:
                return "unsupported spell effects"
            kind = 'periodicHeal'
        elif aura_effect and aura == AURA_MOD_DECREASE_SPEED:
            # Fixed slow: CalcValue never scales speed auras; no per-level term.
            # On the creature itself it slows its own chase.
            if dice > 1 or not -100 < amount < 0:
                return "unsupported spell effects"
            kind = 'slow'
        elif aura_effect and aura == AURA_MOD_INCREASE_SPEED:
            if dice > 1 or not 0 < amount <= 1000:
                return "unsupported spell effects"
            kind = 'speed'
        elif aura_effect and aura == AURA_MOD_RESISTANCE:
            # Armor (misc value 1 = SPELL_SCHOOL_MASK_NORMAL) or the magic
            # resistances, any sign (a buff on the caster, a reduction on players).
            if misc <= 0 or misc > 127 or per_level > 1000 or per_level < -1000 or abs(amount) > 100000 or amount + dice > 100000:
                return "unsupported spell effects"
            if misc & 1 and misc != 1:
                return "unsupported spell effects"
            kind = 'armor' if misc == 1 else 'resistance'
        elif aura_effect and aura == AURA_MOD_RESISTANCE_PCT:
            if misc != 1 or dice > 1 or not -100 < amount <= 1000 or amount == 0:
                return "unsupported spell effects"
            kind = 'armorPct'
        elif aura_effect and aura in (AURA_MOD_STUN, AURA_MOD_ROOT) and shape == SHAPE_SELF:
            # 2.39: the creature stuns / roots itself (Unit::SetControlled on
            # the aura owner) for the duration.
            kind = 'selfStun' if aura == AURA_MOD_STUN else 'selfRoot'
        elif aura_effect and aura == AURA_MOD_INVISIBILITY and shape == SHAPE_SELF:
            # 2.39: an invisible creature (no player detects a creature's
            # invisibility) - unseen and unaggroed for the duration.
            kind = 'invisible'
        elif aura_effect and aura in (AURA_MOD_STUN, AURA_MOD_ROOT, AURA_MOD_FEAR, AURA_MOD_CONFUSE, AURA_MOD_SILENCE):
            kind = {AURA_MOD_STUN: 'stun', AURA_MOD_ROOT: 'root', AURA_MOD_FEAR: 'fear', AURA_MOD_CONFUSE: 'confuse',
                    AURA_MOD_SILENCE: 'silence'}[aura]
            if control:
                return "unsupported spell effects"
            control = kind
        elif aura_effect and aura == AURA_MOD_DAMAGE_TAKEN:
            if misc <= 0 or misc > 127 or dice > 1 or amount == 0 or abs(amount) > 100000:
                return "unsupported spell effects"
            kind = 'damageTakenFlat'
        elif aura_effect and aura == AURA_MOD_DAMAGE_PERCENT_TAKEN:
            if misc <= 0 or misc > 127 or dice > 1 or not -100 < amount <= 1000 or amount == 0:
                return "unsupported spell effects"
            kind = 'damageTakenPct'
        elif aura_effect and aura == AURA_MOD_HEALING_PCT:
            if dice > 1 or not -100 <= amount < 0:
                return "unsupported spell effects"
            kind = 'healingPct'
        elif aura_effect and aura in (AURA_MOD_MELEE_HASTE, AURA_MELEE_SLOW):
            # Unit::ApplyAttackTimePercentMod either way (HandleModMeleeRangedSpeedPct
            # for MELEE_SLOW); -100 doubles the attack time.
            if dice > 1 or not -100 <= amount <= 1000 or amount == 0:
                return "unsupported spell effects"
            kind = 'haste'
        elif aura_effect and aura == AURA_MOD_CASTING_SPEED_NOT_STACK:
            if dice > 1 or not -100 < amount <= 1000 or amount == 0:
                return "unsupported spell effects"
            kind = 'castSpeed'
        elif aura_effect and aura == AURA_MOD_HIT_CHANCE:
            if dice > 1 or not -100 <= amount <= 100 or amount == 0:
                return "unsupported spell effects"
            kind = 'hitChance'
        elif aura_effect and aura == AURA_MOD_DISARM:
            kind = 'disarm'
        elif aura_effect and aura in (AURA_MOD_DODGE_PERCENT, AURA_MOD_PARRY_PERCENT, AURA_MOD_BLOCK_PERCENT):
            if dice > 1 or not -100 <= amount <= 100 or amount == 0:
                return "unsupported spell effects"
            kind = {AURA_MOD_DODGE_PERCENT: 'dodge', AURA_MOD_PARRY_PERCENT: 'parry', AURA_MOD_BLOCK_PERCENT: 'block'}[aura]
        elif aura_effect and aura == AURA_MOD_DAMAGE_PERCENT_DONE:
            if misc <= 0 or misc > 127 or dice > 1 or not -99 <= amount <= 1000 or amount == 0:
                return "unsupported spell effects"
            kind = 'damagePct'
        elif aura_effect and aura == AURA_MOD_DAMAGE_DONE:
            if misc <= 0 or misc > 127 or dice > 1 or amount == 0 or abs(amount) > 100000:
                return "unsupported spell effects"
            kind = 'damageFlat'
        elif aura_effect and aura == AURA_MOD_ATTACK_POWER:
            if dice > 1 or amount == 0 or abs(amount) > 100000:
                return "unsupported spell effects"
            kind = 'attackPower'
        elif aura_effect and aura == AURA_SCHOOL_ABSORB:
            if misc <= 0 or misc > 127 or amount <= 0 or amount + dice > 1000000:
                return "unsupported spell effects"
            kind = 'absorb'
        elif aura_effect and aura in (AURA_SCHOOL_IMMUNITY, AURA_DAMAGE_IMMUNITY):
            if misc <= 0 or misc > 127:
                return "unsupported spell effects"
            kind = 'schoolImmunity' if aura == AURA_SCHOOL_IMMUNITY else 'damageImmunity'
        elif aura_effect and aura == AURA_MECHANIC_IMMUNITY:
            if misc <= 0 or misc > 31:
                return "unsupported spell effects"
            kind = 'mechanicImmunity'
        elif aura_effect and aura == AURA_MOD_INCREASE_HEALTH:
            if dice > 1 or amount == 0 or abs(amount) > 1000000:
                return "unsupported spell effects"
            kind = 'maxHealth'
        elif aura_effect and aura == AURA_MOD_INCREASE_HEALTH_PERCENT:
            if dice > 1 or not -99 <= amount <= 1000 or amount == 0:
                return "unsupported spell effects"
            kind = 'maxHealthPct'
        elif aura_effect and aura == AURA_DAMAGE_SHIELD:
            if amount <= 0 or amount + dice > 100000 or not row[225]:
                return "unsupported spell effects"
            kind = 'damageShield'
        elif aura_effect and aura in (AURA_MOD_STAT, AURA_MOD_SCALE, AURA_DUMMY, AURA_TRANSFORM, AURA_MOD_REGEN,
                                                       AURA_MOD_POWER_REGEN, AURA_MOD_HEALTH_REGEN_PERCENT, AURA_MOD_RANGED_ATTACK_POWER,
                                                       AURA_MOD_SHAPESHIFT, AURA_MOD_SKILL, AURA_DISPEL_IMMUNITY, AURA_MOD_TAUNT, AURA_DUMMY):
            # Auras with no local effect: tracked for their duration only.
            # (Creature::UpdateStats ignores stats; ranged attack power, regen
            # and shapeshift forms of creatures are not modelled; a taunt on a
            # player does nothing.)
            kind = 'cosmetic'
        elif aura_effect and aura == AURA_MOD_TOTAL_STAT_PERCENTAGE:
            if shape not in (SHAPE_SELF, SHAPE_AOE_ALLY):
                return "unsupported spell effects"
            kind = 'cosmetic'
        else:
            return "unsupported spell effects"
        if kind != 'cosmetic' and kind in kinds:
            return "unsupported spell effects"
        if effect == EFFECT_PERSISTENT_AREA_AURA:
            # The ground aura lands on players alone: the player-side aura kinds
            # (a periodic trigger or proc would need the object as caster).
            if kind not in PLAYER_AURA_KINDS or kind == 'periodicTrigger':
                return "unsupported ground aura"
        elif effect == EFFECT_APPLY_AURA and kind in TIMED_KINDS:
            unit_auras = True
        friendly_shape = shape in (SHAPE_SELF, SHAPE_AOE_ALLY)
        if kind == 'summon':
            pass
        elif kind in FRIENDLY_KINDS or kind in CREATURE_AURA_KINDS - PLAYER_AURA_KINDS:
            # A friendly effect on the any-unit target is the caster itself.
            if not friendly_shape:
                if target != (25, 0):
                    return "unsupported spell effects"
                shape = SHAPE_SELF
        elif kind in HOSTILE_KINDS or kind in UTILITY_KINDS or kind in PLAYER_AURA_KINDS - CREATURE_AURA_KINDS:
            if friendly_shape:
                return "unsupported spell effects"
        if kind in TIMED_KINDS and not duration_ms and not (kind == 'periodicTrigger' and permanent and shape == SHAPE_SELF):
            return "unsupported spell effects"
        kinds.append(kind)
        if kind == 'cosmetic':
            cosmetic_shapes.add(shape)
        else:
            real_shapes.add(shape)
    real = [k for k in kinds if k != 'cosmetic']
    if not kinds:
        return "unsupported spell effects"
    if ground and unit_auras:
        return "unsupported ground aura"
    # Cosmetic effects never decide the shape while a real effect exists; a
    # spell of cosmetic effects without any shape is a self cast.
    shapes = real_shapes or cosmetic_shapes
    if not shapes:
        shapes = {SHAPE_SELF}
    if len(shapes) != 1:
        return "unsupported spell effects"
    shape = next(iter(shapes))
    # Spell::SearchChainTargets runs per effect: every real effect must chain
    # alike, since the runtime lands all of them on every chain target.
    if chain:
        index = 0
        for k in range(3):
            if not row[71 + k]:
                continue
            if kinds[index] != 'cosmetic' and row[104 + k] != chain:
                return "unsupported spell effects"
            index += 1
    # Weapon effects and plain school damage are never mixed in one spell.
    if 'damage' in kinds and ('weapon' in kinds or 'weaponPercent' in kinds):
        return "unsupported spell effects"
    if 'leech' in kinds and ('damage' in kinds or 'weapon' in kinds or 'weaponPercent' in kinds):
        return "unsupported spell effects"
    if ('weapon' in kinds or 'weaponPercent' in kinds) and shape not in (SHAPE_ENEMY, SHAPE_CONE, SHAPE_AOE):
        return "unsupported spell effects"
    # A next-swing special (SPELL_ATTR0_ON_NEXT_SWING) lands with the next
    # melee swing: on the swing's victim, or on the creature itself.
    if row[4] & 0x404 and shape in (SHAPE_AOE_TARGET, SHAPE_AOE_ALLY, SHAPE_AOE_DEST):
        return "unsupported spell effects"
    if channeled:
        # A channel holds the creature for its duration; its ticks are the
        # periodic effects, everything else lands with the channel start.
        if chain or not duration_ms or row[4] & 0x404 or shape in (SHAPE_AOE_TARGET, SHAPE_AOE_DEST):
            return "channeled spell"
    if shape in (SHAPE_AOE_TARGET, SHAPE_AOE_DEST) and any(k in ('weapon', 'weaponPercent') for k in kinds):
        return "unsupported spell effects"
    if row[212] > 255:
        return "unsupported spell effects"
    utility = bool(real) and all(k in UTILITY_KINDS for k in real)
    if (summon_entry or ground) and (row[4] & 0x404 or channeled or chain):
        return "unsupported spell effects"
    SPELL_INFO[spell] = {'shape': shape, 'positive': shape in (SHAPE_SELF, SHAPE_AOE_ALLY), 'utility': utility, 'kinds': kinds,
                         'proc': proc_flags if proc_aura else 0, 'summon': summon_entry, 'ground': ground,
                         'triggerSelfHostile': trigger_self_hostile}
    return None


def spell_shape_of(spell, spells):
    """The single target shape of an admitted spell (spell_blocker's resolution)."""
    info = SPELL_INFO.get(spell)
    return info['shape'] if info else None


def spell_positive(spell, spells):
    """SpellInfo::IsPositive for an admitted spell: only friendly shapes are positive."""
    info = SPELL_INFO.get(spell)
    return bool(info and info['positive'])


def spell_utility(spell):
    """A player-targeted spell that neither rolls to hit nor threatens (kill credit, item)."""
    info = SPELL_INFO.get(spell)
    return bool(info and info['utility'])


def timed(params, first, flags, start=2):
    """min/max initial + repeatMin/repeatMax (SmartScript::InitTimer/RecalcTimer).

    2.39: a repeat of 0 without NOT_REPEATABLE re-arms the row at once, so it
    fires on every update while its condition holds - exactly what RecalcTimer
    (0, 0) does at the pin; earlier checkpoints refused such rows."""
    lo, hi = params[start], params[start + 1]
    if first and (params[0] > params[1] or params[1] > MAX_TIMER_MS):
        return False
    return lo <= hi <= MAX_TIMER_MS


def row_blocker(r, owner, ctx, in_list=False):
    """None when the runtime reproduces the SmartAI row, else the reason.

    ctx: dbc, scripted, spell_conditions, smart_conditions, lists, list_ok (memo),
    talk_groups ((entry, group) pairs the creature-talk companion carries),
    entries (owner -> creature entry), spawns.
    """
    params = [r['event_param%d' % i] for i in range(1, 7)]
    action = [r['action_param%d' % i] for i in range(1, 7)]
    target = [r['target_param%d' % i] for i in range(1, 5)]
    event, action_type, target_type = r['event_type'], r['action_type'], r['target_type']
    flags = r['event_flags']
    if flags & ~(FLAG_NOT_REPEATABLE | FLAG_DIFFICULTY_0 | FLAG_DONT_RESET | FLAG_WHILE_CHARMED):
        return "unsupported event flags"
    if not 1 <= r['event_chance'] <= 100:
        return "unsupported event chance"
    if r['event_phase_mask'] > 0xfff:
        return "unsupported event phase"
    if (owner, r['id'] + 1) in ctx['smart_conditions']:
        return "SmartAI condition"
    if in_list:
        # Timed action list rows: the delay in min/max, the event type is
        # replaced by the list's timer type (SmartScript::SetScript9).
        if event != EV_UPDATE_IC or r['link'] or r['event_phase_mask'] or any(params[2:]) or params[0] > params[1] or params[1] > MAX_TIMER_MS:
            return "unsupported timed list row"
    elif event not in EVENTS:
        return "unsupported event"
    elif event in (EV_UPDATE_IC, EV_UPDATE_OOC, EV_UPDATE):
        if any(params[4:]) or not timed(params, True, flags):
            return "unsupported event parameters"
    elif event in (EV_HEALTH_PCT, EV_MANA_PCT, EV_TARGET_HEALTH_PCT, EV_TARGET_MANA_PCT):
        if any(params[4:]) or not 0 <= params[0] <= params[1] <= 100 or not timed(params, False, flags):
            return "unsupported event parameters"
    elif event == EV_FRIENDLY_HEALTH:
        # hpDeficit, radius, repeatMin, repeatMax
        if not 0 < params[1] <= 100 or any(params[4:]) or not timed(params, False, flags):
            return "unsupported event parameters"
    elif event == EV_FRIENDLY_HEALTH_PCT:
        # min (initial timer), max, repeatMin, repeatMax, hpPct, radius
        if not timed(params, True, flags) or not 0 < params[4] <= 100 or params[5] > 100:
            return "unsupported event parameters"
        if target_type not in (TG_SELF, TG_INVOKER, TG_CREATURE_RANGE, TG_CREATURE_GUID, TG_CREATURE_DISTANCE, TG_CLOSEST_CREATURE, TG_CLOSEST_PLAYER):
            return "unsupported event parameters"
        if target_type in (TG_SELF, TG_INVOKER) and not params[5]:
            return "unsupported event parameters"
    elif event == EV_FRIENDLY_MISSING_BUFF:
        # spell, radius, repeatMin, repeatMax, onlyInCombat
        if not params[0] or not 0 < params[1] <= 100 or params[4] > 1 or params[5] or not timed(params, False, flags):
            return "unsupported event parameters"
    elif event in (EV_HAS_AURA, EV_TARGET_BUFFED):
        # spell, count, repeatMin, repeatMax
        if not params[0] or params[1] > 255 or any(params[4:]) or not timed(params, False, flags):
            return "unsupported event parameters"
    elif event in (EV_RANGE, EV_AREA_RANGE, EV_AREA_CASTING):
        if not timed(params, True, flags) or params[4] > params[5] or params[5] > 300:
            return "unsupported event parameters"
    elif event == EV_IS_BEHIND_TARGET:
        # min/max initial timer, repeatMin/Max, rangeMin/rangeMax (0 = never)
        if not timed(params, True, flags) or params[4] > params[5] or params[5] > 100 or not params[5]:
            return "unsupported event parameters"
    elif event in (EV_SPELLHIT, EV_SPELLHIT_TARGET):
        if params[1] > 127 or params[2] > params[3] or params[3] > MAX_TIMER_MS or any(params[4:]):
            return "unsupported event parameters"
    elif event == EV_VICTIM_CASTING:
        if params[0] > params[1] or params[1] > MAX_TIMER_MS or any(params[3:]) or (not params[1] and not flags & FLAG_NOT_REPEATABLE):
            return "unsupported event parameters"
    elif event == EV_RESPAWN:
        if params[0] > 1 or (params[0] == 0 and any(params[1:])) or (params[0] == 1 and any(params[2:])):
            return "unsupported event parameters"
    elif event == EV_KILL:
        # cooldownMin, cooldownMax, playerOnly, creature entry: only players die
        # to local creatures, so an entry filter never matches.
        if params[0] > params[1] or params[1] > MAX_TIMER_MS or params[2] > 1 or params[3] or any(params[4:]):
            return "unsupported event parameters"
    elif event in (EV_OOC_LOS, EV_IC_LOS):
        # hostilityMode, maxDist, cooldownMin, cooldownMax, playerOnly
        if params[0] > 2 or not 0 < params[1] <= 100 or params[2] > params[3] or params[3] > MAX_TIMER_MS or params[4] > 1 or params[5]:
            return "unsupported event parameters"
    elif event in (EV_ACCEPTED_QUEST, EV_REWARD_QUEST):
        if params[1] > params[2] or params[2] > MAX_TIMER_MS or any(params[3:]):
            return "unsupported event parameters"
    elif event in (EV_DAMAGED, EV_DAMAGED_TARGET, EV_RECEIVE_HEAL):
        # minAmount, maxAmount, cooldownMin, cooldownMax (DAMAGED: rangeMin is
        # the "health below percent" mode)
        if params[0] > params[1] or params[2] > params[3] or params[3] > MAX_TIMER_MS or params[5] or \
                (params[4] and (event != EV_DAMAGED or params[4] > 100)):
            return "unsupported event parameters"
    elif event in (EV_DATA_SET, EV_COUNTER_SET):
        if params[2] > params[3] or params[3] > MAX_TIMER_MS or any(params[4:]):
            return "unsupported event parameters"
    elif event == EV_TIMED_EVENT_TRIGGERED:
        if any(params[1:]):
            return "unsupported event parameters"
    elif event == EV_EVENT_PHASE_CHANGE:
        if not 0 < params[0] <= 0xfff or any(params[1:]):
            return "unsupported event parameters"
    elif event in (EV_NEAR_PLAYERS, EV_NEAR_PLAYERS_NEGATION):
        # minCount, radius, firstTimer, repeatMin, repeatMax
        if not params[0] or not 0 < params[1] <= 100 or params[2] > MAX_TIMER_MS or params[3] > params[4] or params[4] > MAX_TIMER_MS or params[5] or \
                (not params[4] and not flags & FLAG_NOT_REPEATABLE):
            return "unsupported event parameters"
    elif event in (EV_AGGRO, EV_DEATH, EV_RESET, EV_AI_INIT, EV_EVADE, EV_REACHED_HOME, EV_CORPSE_REMOVED, EV_JUST_CREATED,
                   EV_JUST_SUMMONED, EV_FOLLOW_COMPLETED):
        if any(params):
            return "unsupported event parameters"
    elif event in (EV_SUMMONED_UNIT, EV_SUMMONED_UNIT_DIES, EV_SUMMONED_UNIT_EVADE, EV_SUMMON_DESPAWNED):
        # creature entry (0 any), cooldownMin, cooldownMax
        if params[1] > params[2] or params[2] > MAX_TIMER_MS or any(params[3:]):
            return "unsupported event parameters"
    elif event == EV_MOVEMENTINFORM:
        # movement type (0 any; 2 waypoint_data patrol (2.39), 8 point, 16
        # effect, 17 escort), point id
        if params[0] not in (0, 2, 8, 16, 17) or any(params[2:]):
            return "unsupported event parameters"
    elif event in (EV_WAYPOINT_REACHED, EV_WAYPOINT_ENDED):
        # pointId (0 any), pathId (0 any: compared with the creature's loaded path)
        if any(params[2:]) or (params[1] and params[1] not in ctx['patrol_paths']):
            return "unsupported event parameters"
    elif event == EV_ACTION_DONE:
        # eventId (DoAction's id), cooldownMin, cooldownMax
        if params[1] > params[2] or params[2] > MAX_TIMER_MS or any(params[3:]):
            return "unsupported event parameters"
    elif event == EV_GOSSIP_HELLO:
        # filter: 0 always, 1 GossipHello only; 2 (reportUse only) never fires for a creature
        if params[0] > 1 or any(params[1:]):
            return "unsupported event parameters"
    elif event == EV_GOSSIP_SELECT:
        # menuId, optionId: the option must be one the gossip catalog carries
        if any(params[2:]) or (params[0], params[1]) not in ctx['gossip_options']:
            return "unsupported event parameters"
    elif event == EV_RECEIVE_EMOTE:
        # emoteId (TextEmotes.dbc), cooldownMin, cooldownMax; the legacy
        # condition triple (params 4-6) is not evaluated locally
        if not params[0] or params[0] > 10000 or params[1] > params[2] or params[2] > MAX_TIMER_MS or any(params[3:]):
            return "unsupported event parameters"
    elif event in (EV_PASSENGER_BOARDED, EV_PASSENGER_REMOVED):
        # cooldownMin, cooldownMax: the creature must be a vehicle
        if params[0] > params[1] or params[1] > MAX_TIMER_MS or any(params[2:]):
            return "unsupported event parameters"
        if not (ctx['templates'].get(ctx['entries'].get(owner), {}) or {}).get('vehicleid'):
            return "unsupported event parameters"
    elif event == EV_DISTANCE_CREATURE:
        # guid, entry, distance, repeat: a spawn guid the catalog carries or a creature entry
        if (params[0] and params[0] not in ctx['spawns']) or (not params[0] and not params[1]) or not 0 < params[2] <= 200 or \
                params[3] > MAX_TIMER_MS or any(params[4:]):
            return "unsupported event parameters"
    elif event == EV_FRIENDLY_IS_CC:
        # radius, repeatMin, repeatMax
        if not 0 < params[0] <= 100 or params[1] > params[2] or params[2] > MAX_TIMER_MS or any(params[3:]):
            return "unsupported event parameters"
    elif event in (EV_ESCORT_START, EV_ESCORT_REACHED, EV_ESCORT_PAUSED, EV_ESCORT_RESUMED, EV_ESCORT_STOPPED, EV_ESCORT_ENDED):
        # pointId (0 any), pathId (0 any)
        if any(params[2:]):
            return "unsupported event parameters"
    elif event == EV_TEXT_OVER:
        # text group, creature entry of the talker (0 any)
        if params[0] > 255 or any(params[2:]):
            return "unsupported event parameters"
    if event == EV_LINK and not in_list and r['id'] not in ctx['linked'].get(owner, set()):
        return "unreferenced link row"
    if action_type not in ACTIONS:
        return "unsupported action"
    if target_type not in TARGETS:
        return "unsupported target"
    if target_type in (TG_HOSTILE_SECOND, TG_HOSTILE_LAST, TG_HOSTILE_RANDOM, TG_HOSTILE_RANDOM_NOT_TOP):
        # maxDist, playerOnly, powerType + 1 (2.39: mana 1, rage 2, energy 4, runic power 7; focus 3 is a pet's)
        if target[1] > 1 or target[2] not in (0, 1, 2, 4, 7) or target[3]:
            return "unsupported target parameters"
    elif target_type == TG_CLOSEST_PLAYER:
        if not 0 < target[0] <= 100 or any(target[1:]):
            return "unsupported target parameters"
    elif target_type == TG_CREATURE_RANGE:
        if target[1] > target[2] or target[2] > 300 or target[3] > 2:
            return "unsupported target parameters"
    elif target_type == TG_CREATURE_DISTANCE:
        if target[1] > 300 or target[2] > 2 or target[3]:
            return "unsupported target parameters"
    elif target_type == TG_CLOSEST_CREATURE:
        if target[1] > 300 or target[2] > 1 or target[3]:
            return "unsupported target parameters"
    elif target_type == TG_CREATURE_GUID:
        if not target[0] or target[2] or target[3]:
            return "unsupported target parameters"
    elif target_type == TG_THREAT_LIST:
        if target[0] > 100 or any(target[1:]):
            return "unsupported target parameters"
    elif target_type == TG_CLOSEST_ENEMY:
        if target[0] > 100 or target[1] > 1 or any(target[2:]):
            return "unsupported target parameters"
    elif target_type == TG_STORED:
        if any(target[1:]):
            return "unsupported target parameters"
    elif target_type == TG_PLAYER_RANGE:
        # minDist, maxDist (GetPlayerListInGrid, then the minimum)
        if target[0] > target[1] or target[1] > 100 or any(target[2:]):
            return "unsupported target parameters"
    elif target_type == TG_PLAYER_DISTANCE:
        if target[0] > 100 or any(target[1:]):
            return "unsupported target parameters"
    elif target_type == TG_CLOSEST_FRIENDLY:
        # maxDist, playerOnly
        if not 0 < target[0] <= 100 or target[1] > 1 or any(target[2:]):
            return "unsupported target parameters"
    elif target_type == TG_FARTHEST:
        # maxDist, playerOnly, isInLos
        if target[0] > 100 or target[1] > 1 or target[2] > 1 or target[3]:
            return "unsupported target parameters"
    elif target_type == TG_PLAYER_WITH_AURA:
        # spellId, negation, distMax, distMin
        if not target[0] or target[1] > 1 or target[2] > 100 or target[3] > target[2]:
            return "unsupported target parameters"
    elif target_type == TG_RANDOM_POINT:
        # range, amount (summons), self (0: around the row's coordinates)
        if not 0 < target[0] <= 100 or not 0 < target[1] <= MAX_SUMMONS_PER_CAST or target[2] > 1 or target[3]:
            return "unsupported target parameters"
    elif target_type == TG_SUMMONED_CREATURES:
        if any(target[1:]):
            return "unsupported target parameters"
    elif any(target):
        return "unsupported target parameters"
    # 2.38: the row's own coordinates (a destination, an offset, a facing).
    coords = [r['target_x'], r['target_y'], r['target_z'], r['target_o']]
    if any(not math.isfinite(float(c)) for c in coords) or abs(float(coords[0])) > 100000 or abs(float(coords[1])) > 100000 or \
            abs(float(coords[2])) > 20000 or abs(float(coords[3])) > 100:
        return "unsupported target coordinates"
    if target_type in POSITION_TARGETS and action_type not in POSITION_ACTIONS:
        return "unsupported target"
    if target_type == TG_RANDOM_POINT and action_type not in (AC_SUMMON_CREATURE, AC_MOVE_TO_POS, AC_JUMP_TO_POS):
        return "unsupported target"
    creature_only = target_type in CREATURE_TARGETS or target_type == TG_SELF
    player_only = target_type in PLAYER_TARGETS
    if action_type in SPELL_ACTIONS:
        if action[1] & ~CAST_FLAGS or action[2] or action[3] > 255 or any(action[4:]):
            return "unsupported cast parameters"
        if action_type == AC_SELF_CAST and target_type != TG_SELF:
            return "unsupported cast parameters"
        if target_type == TG_NONE:
            return "unsupported cast parameters"
        reason = spell_blocker(action[0], ctx['dbc'], ctx['scripted'], ctx['spell_conditions'])
        if reason:
            return reason
        positive = spell_positive(action[0], ctx['dbc'].spells)
        shape = spell_shape_of(action[0], ctx['dbc'].spells)
        # A friendly spell lands on the creature itself or a creature target; a
        # hostile one needs a player target unless the spell is caster-centred
        # (the explicit target is only the range reference then). A stored
        # list or the invoker may hold either; the runtime sorts them.
        if SPELL_INFO[action[0]].get('triggerSelfHostile') and target_type != TG_SELF:
            return "unsupported cast target"
        # 2.39: a caster-only spell (every effect on TARGET_UNIT_CASTER) cast at
        # any unit lands on the caster regardless of the explicit target; an
        # enemy-only spell cast at the creature itself fails CheckExplicitTarget
        # (SPELL_FAILED_BAD_TARGETS) and does nothing - both are carried.
        if positive and not (creature_only or target_type in MIXED_TARGETS) and shape != SHAPE_SELF:
            return "unsupported cast target"
        if not positive and not (player_only or target_type in MIXED_TARGETS) and \
                not (target_type == TG_SELF and shape in (SHAPE_AOE, SHAPE_AOE_TARGET, SHAPE_CONE, SHAPE_AOE_DEST, SHAPE_ENEMY)):
            return "unsupported cast target"
    elif action_type == AC_TALK:
        # textGroupID, duration, useTalkTarget, delay: the talker is the
        # creature itself (target none/self/player, or a creature target with
        # useTalkTarget) or the creature target; its entry must carry the text
        # group in the creature-talk companion.
        if action[1] > MAX_TIMER_MS or action[2] > 1 or action[3] > MAX_TIMER_MS or any(action[4:]):
            return "unsupported talk parameters"
        talkers = []
        own_entry = ctx.get('list_entry') if in_list else ctx['entries'].get(owner)
        if target_type in (TG_NONE, TG_SELF) or player_only or target_type in MIXED_TARGETS or action[2]:
            talkers.append(own_entry)
        elif target_type in (TG_CREATURE_RANGE, TG_CREATURE_DISTANCE, TG_CLOSEST_CREATURE, TG_SUMMONED_CREATURES):
            talkers.append(target[0] if target[0] else None)
        elif target_type == TG_CREATURE_GUID:
            spawn = ctx['spawns'].get(target[0])
            talkers.append(spawn['id1'] if spawn else None)
        else:
            return "unsupported talk parameters"
        if any(t is None or (t, action[0]) not in ctx['talk_groups'] for t in talkers):
            return "missing creature_text group"
    elif action_type in (AC_AUTO_ATTACK, AC_COMBAT_MOVE):
        if action[0] > 1 or any(action[1:]) or target_type not in (TG_NONE, TG_SELF):
            return "unsupported action parameters"
    elif action_type == AC_SET_PHASE:
        if action[0] > 12 or any(action[1:]) or target_type not in (TG_NONE, TG_SELF):
            return "unsupported action parameters"
    elif action_type == AC_INC_PHASE:
        if action[0] > 12 or action[1] > 12 or any(action[2:]) or target_type not in (TG_NONE, TG_SELF):
            return "unsupported action parameters"
    elif action_type == AC_RANDOM_PHASE:
        if any(a > 12 for a in action) or not any(action) or target_type not in (TG_NONE, TG_SELF):
            return "unsupported action parameters"
    elif action_type == AC_RANDOM_PHASE_RANGE:
        if action[0] > action[1] or action[1] > 12 or any(action[2:]) or target_type not in (TG_NONE, TG_SELF):
            return "unsupported action parameters"
    elif action_type == AC_EVADE:
        if any(action) or target_type != TG_SELF:
            return "unsupported action parameters"
    elif action_type == AC_FLEE:
        if action[0] > 1 or any(action[1:]) or target_type not in (TG_NONE, TG_SELF):
            return "unsupported action parameters"
    elif action_type == AC_REMOVE_AURAS:
        # spell, charges (0: the whole aura; a charge count drops that many
        # stacks - carried as a removal), onlyOwnedAuras
        if not action[0] or action[1] > 255 or action[2] > 1 or any(action[3:]) or target_type not in UNIT_TARGETS:
            return "unsupported action parameters"
    elif action_type == AC_CALL_FOR_HELP:
        if not 0 < action[0] <= 100 or action[1] > 1 or any(action[2:]) or target_type != TG_SELF:
            return "unsupported action parameters"
    elif action_type == AC_ATTACK_START:
        if any(action) or not (player_only or target_type in MIXED_TARGETS):
            return "unsupported action parameters"
    elif action_type == AC_INTERRUPT:
        if action[0] > 1 or action[2] > 1 or any(action[3:]) or target_type not in UNIT_TARGETS:
            return "unsupported action parameters"
    elif action_type == AC_TIMED_LIST:
        # 2.39: SetScript9 runs the list on each creature target (its entry
        # is the talker of the list's TALK rows) or on the creature itself.
        if in_list or action[1] > 2 or action[2] > 1 or any(action[3:]) or not (target_type == TG_SELF or target_type in CREATURE_TARGETS):
            return "unsupported action parameters"
        if target_type == TG_SELF:
            list_entry = ctx['entries'].get(owner)
        elif target_type in (TG_CREATURE_RANGE, TG_CREATURE_DISTANCE, TG_CLOSEST_CREATURE, TG_SUMMONED_CREATURES):
            list_entry = target[0] if target[0] else None
        elif target_type == TG_CREATURE_GUID:
            spawn = ctx['spawns'].get(target[0])
            list_entry = spawn['id1'] if spawn else None
        else:
            return "unsupported action parameters"
        # The target must run SmartAI itself (CAST_AI(SmartAI, ...) at the pin).
        if list_entry is None or (ctx['templates'].get(list_entry) or {}).get('ainame') != 'SmartAI':
            return "unsupported action parameters"
        reason = list_blocker(action[0], ctx, list_entry)
        if reason:
            return reason
    elif action_type in (AC_CALL_RANDOM_TIMED_LIST, AC_CALL_RANDOM_RANGE_TIMED_LIST):
        # script9 ids 1-6 / a range of ids, on the creature itself (SetScript9
        # on each creature target; TARGET_NONE is an error at the pin).
        if in_list or target_type != TG_SELF:
            return "unsupported action parameters"
        ids = [a for a in action if a] if action_type == AC_CALL_RANDOM_TIMED_LIST else list(range(action[0], action[1] + 1))
        if not ids or len(ids) > 24 or (action_type == AC_CALL_RANDOM_RANGE_TIMED_LIST and (any(action[2:]) or action[0] > action[1])):
            return "unsupported action parameters"
        for list_id in ids:
            reason = list_blocker(list_id, ctx, ctx['entries'].get(owner))
            if reason:
                return reason
    elif action_type == AC_SET_FACTION:
        if any(action[1:]) or not creature_only:
            return "unsupported action parameters"
    elif action_type == AC_SET_REACT_STATE:
        if action[0] > 2 or any(action[1:]) or not creature_only:
            return "unsupported action parameters"
    elif action_type in (AC_THREAT_SINGLE_PCT, AC_THREAT_ALL_PCT):
        if action[0] > 1000 or action[1] > 1000 or any(action[2:]) or (action_type == AC_THREAT_SINGLE_PCT and not player_only):
            return "unsupported action parameters"
    elif action_type in (AC_SET_UNIT_FLAG, AC_REMOVE_UNIT_FLAG):
        # flag, type (0 = UNIT_FIELD_FLAGS). 2.39: any bit is carried; the
        # runtime honours the UNIT_FLAG_LOCAL ones and keeps the rest as state.
        if action[1] or not action[0] or any(action[2:]) or not creature_only:
            return "unsupported action parameters"
    elif action_type in (AC_COMBAT_STOP, AC_CALL_SCRIPT_RESET):
        if any(action) or target_type not in (TG_NONE, TG_SELF):
            return "unsupported action parameters"
    elif action_type == AC_CALL_KILLEDMONSTER:
        if not action[0] or any(action[1:]) or not (target_type in (TG_NONE, TG_SELF) or player_only or target_type in MIXED_TARGETS):
            return "unsupported action parameters"
    elif action_type == AC_DIE:
        if action[0] > MAX_TIMER_MS or any(action[1:]) or target_type not in (TG_NONE, TG_SELF):
            return "unsupported action parameters"
    elif action_type == AC_FORCE_DESPAWN:
        # delay ms, forceRespawnTimer s, removeObjectFromWorld
        if action[0] > MAX_TIMER_MS or action[1] > 86400 or action[2] > 1 or any(action[3:]) or not creature_only:
            return "unsupported action parameters"
    elif action_type == AC_SET_INVINCIBILITY_HP:
        if action[1] > 100 or any(action[2:]) or not creature_only:
            return "unsupported action parameters"
    elif action_type == AC_SET_DATA:
        if any(action[2:]) or not creature_only:
            return "unsupported action parameters"
    elif action_type == AC_KILL_UNIT:
        if any(action) or target_type not in UNIT_TARGETS:
            return "unsupported action parameters"
    elif action_type == AC_SET_COUNTER:
        # counterId, value, reset, subtract
        if action[2] > 1 or action[3] > 1 or any(action[4:]) or not creature_only:
            return "unsupported action parameters"
    elif action_type == AC_STORE_TARGET_LIST:
        if any(action[1:]) or target_type == TG_NONE:
            return "unsupported action parameters"
    elif action_type == AC_CREATE_TIMED_EVENT:
        # id, min, max, repeatMin, repeatMax, chance
        if action[1] > action[2] or action[2] > MAX_TIMER_MS or action[3] > action[4] or action[4] > MAX_TIMER_MS or action[5] > 100 or \
                target_type not in (TG_NONE, TG_SELF):
            return "unsupported action parameters"
    elif action_type in (AC_TRIGGER_TIMED_EVENT, AC_REMOVE_TIMED_EVENT):
        if any(action[1:]) or target_type not in (TG_NONE, TG_SELF):
            return "unsupported action parameters"
    elif action_type == AC_TRIGGER_RANDOM_TIMED_EVENT:
        if action[0] > action[1] or any(action[2:]) or target_type not in (TG_NONE, TG_SELF):
            return "unsupported action parameters"
    elif action_type == AC_ADD_AURA:
        # Unit::AddAura(spell, target): the aura lands without a cast; a
        # friendly aura on the creature or a creature target, a hostile one on
        # a player target.
        if any(action[1:]) or target_type not in UNIT_TARGETS:
            return "unsupported action parameters"
        reason = spell_blocker(action[0], ctx['dbc'], ctx['scripted'], ctx['spell_conditions'])
        if reason:
            return reason
        info = SPELL_INFO[action[0]]
        if not any(k in TIMED_KINDS or k == 'cosmetic' for k in info['kinds']) or any(k in HOSTILE_KINDS | FRIENDLY_KINDS | UTILITY_KINDS for k in info['kinds']) or \
                info['summon'] or info['ground']:
            return "unsupported action parameters"
        if info['positive'] and not (creature_only or target_type in MIXED_TARGETS):
            return "unsupported action parameters"
        if not info['positive'] and not (player_only or target_type in MIXED_TARGETS):
            return "unsupported action parameters"
    elif action_type == AC_SET_RANGED_MOVEMENT:
        if action[0] > 100 or action[1] > 360 or any(action[2:]) or not creature_only:
            return "unsupported action parameters"
    elif action_type == AC_SET_HEALTH_REGEN:
        if action[0] > 1 or any(action[1:]) or not creature_only:
            return "unsupported action parameters"
    elif action_type in (AC_SET_POWER, AC_ADD_POWER, AC_REMOVE_POWER):
        # powerType (mana only), newPower
        if action[0] or action[1] > 1000000 or any(action[2:]) or not creature_only:
            return "unsupported action parameters"
    elif action_type == AC_SET_CORPSE_DELAY:
        if action[0] > 86400 or any(action[1:]) or not creature_only:
            return "unsupported action parameters"
    elif action_type == AC_DISABLE_EVADE:
        if action[0] > 1 or any(action[1:]) or target_type not in (TG_NONE, TG_SELF):
            return "unsupported action parameters"
    elif action_type == AC_SET_SIGHT_DIST:
        if action[0] > 200 or any(action[1:]) or not creature_only:
            return "unsupported action parameters"
    elif action_type == AC_ADD_THREAT:
        if action[0] > 1000000 or action[1] > 1000000 or any(action[2:]) or not (player_only or target_type in MIXED_TARGETS):
            return "unsupported action parameters"
    elif action_type == AC_SET_HEALTH_PCT:
        if not 0 < action[0] <= 100 or any(action[1:]) or not creature_only:
            return "unsupported action parameters"
    elif action_type == AC_SET_COMBAT_DISTANCE:
        if action[0] > 100 or any(action[1:]) or not creature_only:
            return "unsupported action parameters"
    elif action_type in (AC_ADD_IMMUNITY, AC_REMOVE_IMMUNITY):
        # type, id, value (Unit::ApplySpellImmune)
        if action[0] not in IMMUNITY_TYPES or any(action[3:]) or not creature_only:
            return "unsupported action parameters"
        if action[0] in (2, 3) and (not 0 < action[2] <= 127 or action[1]):
            return "unsupported action parameters"
        if action[0] == 5 and (not 0 < action[2] <= 31 or action[1]):
            return "unsupported action parameters"
        if action[0] == 6 and (not action[2] or action[1]):
            return "unsupported action parameters"
    elif action_type == AC_SET_EVENT_FLAG_RESET:
        if action[0] > 1 or any(action[1:]) or target_type not in (TG_NONE, TG_SELF):
            return "unsupported action parameters"
    elif action_type == AC_ATTACK_STOP:
        if any(action) or not creature_only:
            return "unsupported action parameters"
    # ---- 2.38
    elif action_type == AC_SUMMON_CREATURE:
        # creature, TempSummonType, duration ms, attackInvoker (2: the invoker
        # itself), attackScriptOwner, flags (none): at the creature targets'
        # positions plus the row offset, the row position, or random points.
        if action[0] not in SUMMONABLE or action[1] not in SUMMON_TYPES or action[2] > MAX_TIMER_MS or action[3] > 2 or action[4] > 1 or action[5]:
            return "unsupported summon parameters"
        if target_type == TG_NONE:
            return "unsupported summon parameters"
    elif action_type == AC_FOLLOW:
        # distance, angle (degrees above 6), end creature entry, credit,
        # creditType (0 kill credit, 1 quest event), aliveState
        if action[0] > 100 or action[1] > 360 or action[4] > 1 or action[5] > 1:
            return "unsupported action parameters"
        if target_type not in (TG_NONE, TG_SELF) and target_type not in UNIT_TARGETS:
            return "unsupported action parameters"
    elif action_type == AC_SET_IN_COMBAT_WITH_ZONE:
        if any(action) or target_type not in UNIT_TARGETS:
            return "unsupported action parameters"
    elif action_type == AC_MOVE_FORWARD:
        if not 0 < action[0] <= 100 or any(action[1:]):
            return "unsupported action parameters"
    elif action_type == AC_SET_VISIBILITY:
        if action[0] > 1 or any(action[1:]) or not creature_only:
            return "unsupported action parameters"
    elif action_type == AC_ESCORT_START:
        # run/walk (0 own, 1 walk, 2 run, 3 fly as run), pathId, canRepeat,
        # quest, despawnTime, reactState; player targets are the escort's
        # own targets (SMART_ESCORT_TARGETS).
        if action[0] > 3 or action[1] not in ctx['paths'] or action[2] > 1 or action[4] > MAX_TIMER_MS or action[5] > 2:
            return "unsupported escort parameters"
        ctx['used_paths'].add(action[1])
    elif action_type == AC_ESCORT_PAUSE:
        if action[0] > MAX_TIMER_MS or any(action[1:]):
            return "unsupported action parameters"
    elif action_type == AC_ESCORT_STOP:
        if action[0] > MAX_TIMER_MS or action[2] > 1 or any(action[3:]):
            return "unsupported action parameters"
    elif action_type == AC_ESCORT_RESUME:
        if any(action):
            return "unsupported action parameters"
    elif action_type == AC_MOVE_TO_POS:
        # pointId, transport (none), controlled, contactDistance, combatReach,
        # disableForceDestination; the row position, a random point or a unit
        # target plus the row offset.
        if action[1] or action[2] > 1 or action[3] > 100 or action[4] > 1 or action[5] > 1:
            return "unsupported action parameters"
        if target_type == TG_NONE:
            return "unsupported action parameters"
    elif action_type == AC_RANDOM_MOVE:
        if action[0] > 100 or any(action[1:]) or not (target_type in (TG_NONE, TG_SELF) or target_type in CREATURE_TARGETS):
            return "unsupported action parameters"
    elif action_type == AC_JUMP_TO_POS:
        # speedXY, speedZ, selfJump
        if not 0 < action[0] <= 200 or action[1] > 200 or action[2] > 1 or any(action[3:]) or target_type == TG_NONE:
            return "unsupported action parameters"
    elif action_type == AC_SET_HOME_POS:
        if action[0] > 1 or any(action[1:]) or not (creature_only or target_type == TG_POSITION):
            return "unsupported action parameters"
    elif action_type == AC_SET_ROOT:
        if action[0] > 1 or any(action[1:]) or not creature_only:
            return "unsupported action parameters"
    elif action_type == AC_SET_RUN:
        if action[0] > 1 or any(action[1:]) or not (creature_only or target_type == TG_NONE):
            return "unsupported action parameters"
    elif action_type == AC_SET_ORIENTATION:
        # quickChange, random, turnAngle (degrees); the home facing (self), the
        # row's facing (position) or a unit to face.
        if action[0] > 1 or action[1] > 1 or action[2] > 360 or any(action[3:]):
            return "unsupported action parameters"
    # ---- 2.39
    elif action_type == AC_WAYPOINT_START:
        # pathId (waypoint_data), repeat, pathSource (0: waypoint_data; the
        # SmartWaypointMgr source is unused at the pin)
        if action[0] not in ctx['patrol_paths'] or action[1] > 1 or action[2] or any(action[3:]) or not creature_only:
            return "unsupported action parameters"
    elif action_type == AC_WAYPOINT_DATA_RANDOM:
        # pathId1, pathId2 (a random path of the range), repeat
        if not action[0] or action[1] < action[0] or action[1] - action[0] >= 64 or action[2] > 1 or any(action[3:]) or not creature_only:
            return "unsupported action parameters"
        if any(path not in ctx['patrol_paths'] for path in range(action[0], action[1] + 1)):
            return "unsupported action parameters"
    elif action_type == AC_MOVEMENT_STOP:
        if any(action) or not (creature_only or target_type == TG_NONE):
            return "unsupported action parameters"
    elif action_type in (AC_MOVEMENT_PAUSE, AC_MOVEMENT_RESUME):
        # timer (0: until resumed / no override)
        if action[0] > MAX_TIMER_MS or any(action[1:]) or not (creature_only or target_type == TG_NONE):
            return "unsupported action parameters"
    elif action_type == AC_DO_ACTION:
        # actionId, instanceTarget (0: the creature targets' AI), isNegative
        if action[1] or action[2] > 1 or any(action[3:]) or not creature_only:
            return "unsupported action parameters"
    elif action_type in (AC_SET_INST_DATA, AC_SET_INST_DATA64):
        # field, data, type (0 SetData): stored per instance, no instance script reads it
        if action[2] or any(action[3:]) or target_type not in (TG_NONE, TG_SELF):
            return "unsupported action parameters"
    elif action_type == AC_CLOSE_GOSSIP:
        if any(action) or not (player_only or target_type in MIXED_TARGETS):
            return "unsupported action parameters"
    elif action_type == AC_SEND_GOSSIP_MENU:
        # menuId (0 clears the menus), npcTextId (0: the menu's own text)
        if any(action[2:]) or not (player_only or target_type in MIXED_TARGETS):
            return "unsupported action parameters"
        if (action[0] and action[0] not in ctx['gossip_menus']) or (action[1] and action[1] not in ctx['gossip_texts']):
            return "unsupported action parameters"
    elif action_type == AC_SET_GOSSIP_MENU:
        if any(action[1:]) or not creature_only or (action[0] and action[0] not in ctx['gossip_menus']):
            return "unsupported action parameters"
    elif action_type in (AC_ADD_ITEM, AC_REMOVE_ITEM):
        # itemId (an item the local catalog carries), count
        if action[0] not in ctx['items'] or action[1] > 1000 or any(action[2:]) or not (player_only or target_type in MIXED_TARGETS):
            return "unsupported action parameters"
    elif action_type in (AC_FAIL_QUEST, AC_OFFER_QUEST):
        # questId (a quest the local catalog carries), directAdd (OFFER_QUEST)
        if action[0] not in ctx['quests'] or any(action[2:]) or (action_type == AC_FAIL_QUEST and action[1]) or action[1] > 1:
            return "unsupported action parameters"
        if not (player_only or target_type in MIXED_TARGETS):
            return "unsupported action parameters"
    elif action_type in (AC_SET_NPC_FLAG, AC_ADD_NPC_FLAG, AC_REMOVE_NPC_FLAG):
        if action[0] & ~NPC_FLAG_LOCAL or any(action[1:]) or not creature_only:
            return "unsupported action parameters"
    elif action_type == AC_CROSS_CAST:
        # spell, castFlags, caster target type + its three params: the casters
        # (creatures) cast the spell at the row's targets
        if action[1] & ~CAST_FLAGS or action[2] not in CROSS_CASTER_TARGETS or target_type == TG_NONE:
            return "unsupported action parameters"
        caster = {'target_type': action[2], 'target_param1': action[3], 'target_param2': action[4], 'target_param3': action[5], 'target_param4': 0}
        if action[2] == TG_CREATURE_RANGE and (caster['target_param2'] > caster['target_param3'] or caster['target_param3'] > 100):
            return "unsupported action parameters"
        if action[2] == TG_CREATURE_DISTANCE and (caster['target_param2'] > 100 or caster['target_param3'] > 2):
            return "unsupported action parameters"
        if action[2] == TG_CLOSEST_CREATURE and (caster['target_param2'] > 100 or caster['target_param3'] > 1):
            return "unsupported action parameters"
        if action[2] == TG_CREATURE_GUID and not caster['target_param1']:
            return "unsupported action parameters"
        if action[2] in (TG_SELF, TG_STORED, TG_OWNER_OR_SUMMONER) and (action[4] or action[5]):
            return "unsupported action parameters"
        if action[2] == TG_SELF and action[3]:
            return "unsupported action parameters"
        reason = spell_blocker(action[0], ctx['dbc'], ctx['scripted'], ctx['spell_conditions'])
        if reason:
            return reason
        positive = spell_positive(action[0], ctx['dbc'].spells)
        shape = spell_shape_of(action[0], ctx['dbc'].spells)
        if positive and not (creature_only or target_type in MIXED_TARGETS):
            return "unsupported cast target"
        if not positive and not (player_only or target_type in MIXED_TARGETS) and \
                not (target_type == TG_SELF and shape in (SHAPE_AOE, SHAPE_AOE_TARGET, SHAPE_CONE, SHAPE_AOE_DEST)):
            return "unsupported cast target"
    elif action_type == AC_SEND_TARGET_TO_TARGET:
        # id: the creature's stored list copied to the creature targets' scripts
        if any(action[1:]) or not creature_only:
            return "unsupported action parameters"
    elif action_type == AC_SET_MOVEMENT_SPEED:
        # movementType (0 walk, 1 run), speedInteger, speedFraction
        if action[0] > 1 or action[1] > 50 or action[2] > 999 or any(action[3:]) or not creature_only:
            return "unsupported action parameters"
    elif action_type == AC_STOP_MOTION:
        # stopMoving, movementExpired
        if action[0] > 1 or action[1] > 1 or any(action[2:]) or not (creature_only or target_type == TG_NONE):
            return "unsupported action parameters"
    elif action_type == AC_MOVE_TO_POS_TARGET:
        # pointId, disableForceDestination: the creature targets walk to the row position
        if action[1] > 1 or any(action[2:]) or not creature_only:
            return "unsupported action parameters"
    elif action_type in COSMETIC_ACTIONS:
        pass
    return None


def list_blocker(list_id, ctx, entry=None):
    """None when every row of the timed action list is reproduced for a
    creature of `entry` (2.38: TALK rows speak that entry's text groups)."""
    memo = ctx['list_ok']
    key = (list_id, entry)
    if key in memo:
        return memo[key]
    rows = ctx['lists'].get(list_id)
    reason = None
    if not rows:
        reason = "missing timed action list"
    elif len(rows) > MAX_ROWS_PER_OWNER:
        reason = "unsupported timed list row"
    else:
        previous = ctx.get('list_entry')
        ctx['list_entry'] = entry
        for r in sorted(rows, key=lambda r: r['id']):
            reason = row_blocker(r, list_id, ctx, in_list=True)
            if reason:
                break
        ctx['list_entry'] = previous
    memo[key] = reason
    return reason


def profiles(tables, dbc, scripted, talk_rows, talk_groups=frozenset()):
    templates = {r['entry']: r for r in tables['creature_template']}
    scripts, lists = defaultdict(list), defaultdict(list)
    for row in tables['smart_scripts']:
        if row['source_type'] == 0:
            scripts[row['entryorguid']].append(row)
        elif row['source_type'] == 9:
            lists[row['entryorguid']].append(row)
    spawns = {r['guid']: r for r in tables['creature']}
    spawned_maps = defaultdict(set)
    for r in tables['creature']:
        spawned_maps[r['id1']].add(r['map'])
    dungeon_maps = {map_id for map_id, row in dbc.maps.items() if row[2] != 0}
    conditions = tables['conditions']
    entries = {}
    for owner in scripts:
        if owner < 0:
            spawn = spawns.get(-owner)
            if spawn is not None:
                entries[owner] = spawn['id1']
        else:
            entries[owner] = owner
    SUMMONABLE.clear()
    SUMMONABLE.update(summonable_entries(tables))
    paths = defaultdict(list)
    for row in tables['waypoints']:
        paths[row['entry']].append(row)
    # SmartWaypointMgr::LoadFromDB expects point ids 1..n in order.
    valid_paths = {path for path, rows in paths.items() if [r['pointid'] for r in sorted(rows, key=lambda r: r['pointid'])] == list(range(1, len(rows) + 1))
                   and len(rows) <= 255 and all(math.isfinite(float(r['position_x'])) and math.isfinite(float(r['position_y'])) and math.isfinite(float(r['position_z'])) for r in rows)}
    # 2.39: the waypoint_data paths the catalog can carry (patch_motion_catalog.py's rule).
    from patch_motion_catalog import compile_paths
    patrol_paths = set(compile_paths(tables['waypoint_data'])[0])
    # 2.40: the gossip menus / texts / options the catalog's gossip packs carry
    # (patch_gossip_catalog.py) and the quests its quests.pack carries.
    from patch_gossip_catalog import catalog_gossip, keyed_pack_index
    catalog_dir = PROJECT / 'assets/local_realm/catalog'
    gossip_menus, gossip_texts, gossip_options = catalog_gossip(catalog_dir)
    catalog_quests = set(keyed_pack_index(catalog_dir / 'quests.pack')[1])
    catalog_items = set(keyed_pack_index(catalog_dir / 'items.pack')[1])
    ctx = {'dbc': dbc, 'scripted': scripted, 'lists': lists, 'list_ok': {}, 'patrol_paths': patrol_paths, 'templates': templates,
           'gossip_menus': gossip_menus, 'gossip_texts': gossip_texts, 'gossip_options': gossip_options, 'quests': catalog_quests, 'items': catalog_items,
           'smart_conditions': {(r['sourceentry'], r['sourcegroup']) for r in conditions
                                if r['sourcetypeorreferenceid'] == 22 and r['sourceid'] == 0},
           'spell_conditions': {r['sourceentry'] for r in conditions if r['sourcetypeorreferenceid'] in (13, 17)},
           'linked': defaultdict(set), 'talk_groups': talk_groups, 'entries': entries, 'spawns': spawns,
           'paths': valid_paths, 'used_paths': set()}
    installed, list_rows, blocked = [], [], Counter()
    used_lists = set()
    for owner in sorted(scripts):
        rows = sorted(scripts[owner], key=lambda r: r['id'])
        if owner < 0:
            spawn = spawns.get(-owner)
            if spawn is None:
                continue  # a guid script without a spawn never runs
            entry = spawn['id1']
        else:
            entry = owner
        template = templates.get(entry)
        # 2.38: every SmartAI creature script with a row of its own (beyond the
        # creature-talk companion's) is a candidate, casts or not.
        if not any((owner, r['id']) not in talk_rows for r in rows):
            continue
        reason = None
        if not template or template['ainame'] != 'SmartAI' or template['scriptname']:
            reason = "not a plain SmartAI creature"
        elif template['unit_class'] not in CLASSES:
            reason = "unsupported creature class"
        else:
            # SmartScript::FillScript: difficulty-flagged rows never load outside
            # a dungeon; inside one, the normal-mode rows (DIFFICULTY_0) load.
            # An entry spawned in both kinds of map is installed only when both
            # readings agree.
            world_rows = [r for r in rows if not r['event_flags'] & FLAG_DIFFICULTY_ALL]
            dungeon_rows = [r for r in rows if not r['event_flags'] & FLAG_DIFFICULTY_ALL or r['event_flags'] & FLAG_DIFFICULTY_0]
            maps = spawned_maps.get(entry, set())
            if maps & dungeon_maps and maps - dungeon_maps and [r['id'] for r in world_rows] != [r['id'] for r in dungeon_rows]:
                reason = "mixed instance script"
            rows = dungeon_rows if maps & dungeon_maps else world_rows
        if not reason:
            own = [r for r in rows if (owner, r['id']) not in talk_rows]
            ids = {r['id'] for r in rows}
            ctx['linked'][owner] = {r['link'] for r in own if r['link']}
            if len(own) > MAX_ROWS_PER_OWNER:
                reason = "too many rows"
            elif any(r['link'] and (r['link'] not in ids or r['link'] == r['id'] or (owner, r['link']) in talk_rows) for r in rows):
                reason = "link outside the installed rows"
            elif not own:
                reason = "no rows of its own"
            else:
                used_before = set(ctx['used_paths'])
                for r in own:
                    reason = row_blocker(r, owner, ctx)
                    if reason:
                        break
                if reason:
                    ctx['used_paths'] = used_before
        if reason:
            blocked[reason] += 1
            continue
        for r in own:
            installed.append(compile_row(r, owner, entry, template, dbc.spells, False))
            if r['action_type'] == AC_TIMED_LIST:
                used_lists.add(r['action_param1'])
            elif r['action_type'] == AC_CALL_RANDOM_TIMED_LIST:
                used_lists.update(a for a in (r['action_param%d' % i] for i in range(1, 7)) if a)
            elif r['action_type'] == AC_CALL_RANDOM_RANGE_TIMED_LIST:
                used_lists.update(range(r['action_param1'], r['action_param2'] + 1))
    for list_id in sorted(used_lists):
        for r in sorted(lists[list_id], key=lambda r: r['id']):
            list_rows.append(compile_row(r, list_id, 0, None, dbc.spells, True))
    # 2.38: the escort paths the installed rows start, and the creature
    # entries they or their spells summon (the catalog patch tool adds their
    # definitions).
    waypoints = []
    for path in sorted(ctx['used_paths']):
        for r in sorted(paths[path], key=lambda r: r['pointid']):
            waypoints.append({'path': path, 'point': r['pointid'], 'x': float(r['position_x']), 'y': float(r['position_y']), 'z': float(r['position_z'])})
    summoned = set()
    for r in installed + list_rows:
        if r['action'] == AC_SUMMON_CREATURE:
            summoned.add(r['a'][0])
        spell = r['a'][0] if r['action'] in SPELL_ACTIONS else 0
        info = SPELL_INFO.get(spell) if spell else None
        if info and info['summon']:
            summoned.add(info['summon'])
        if info:
            for inner in (dbc.spells[spell][116 + k] for k in range(3) if dbc.spells[spell][71 + k] in (EFFECT_TRIGGER_SPELL, EFFECT_APPLY_AURA)):
                inner_info = SPELL_INFO.get(inner) if inner else None
                if inner_info and inner_info['summon']:
                    summoned.add(inner_info['summon'])
    return installed, list_rows, blocked, waypoints, sorted(summoned)


def compile_row(r, owner, entry, template, spells, in_list):
    spell = r['action_param1'] if r['action_type'] in SPELL_ACTIONS else 0
    srow = spells.get(spell) if spell else None
    return {
        'owner': owner, 'row': r['id'], 'entry': entry, 'event': r['event_type'] if not in_list else EV_UPDATE,
        'p': [r['event_param%d' % i] for i in range(1, 7)], 'chance': r['event_chance'],
        'flags': r['event_flags'] & (FLAG_NOT_REPEATABLE | FLAG_DONT_RESET), 'phaseMask': r['event_phase_mask'], 'link': r['link'],
        'action': r['action_type'], 'a': [r['action_param%d' % i] for i in range(1, 7)],
        'target': r['target_type'], 't': [r['target_param%d' % i] for i in range(1, 5)],
        'xyzo': [float(r['target_x']), float(r['target_y']), float(r['target_z']), float(r['target_o'])],
        'unitClass': template['unit_class'] if template else 0, 'expansion': min(2, max(0, template['exp'])) if template else 0,
        # SpellEffectInfo::CalcValue creature scaling: SPELL_ATTR0 0x80000 and
        # a SpellLevel. Whether an individual effect scales (it has no
        # RealPointsPerLevel) is decided per effect at runtime.
        'scales': bool(srow is not None and srow[4] & 0x80000 and srow[39] > 0),
        'spellLevel': srow[39] if srow is not None else 0,
        # SpellInfo::CalcPowerCost scales creature costs by attribute alone.
        'costScales': bool(srow is not None and srow[4] & 0x80000 and srow[39] > 0),
        'manaModifier': float(template['manamodifier']) if template else 0.0,
        'regenMana': bool(template['unit_flags2'] & UNIT_FLAG2_REGENERATE_POWER) if template else False,
        'name': template['name'] if template else 'timed action list %d' % owner,
    }


def format_row(r):
    values = [r['owner'], r['row'], r['entry'], r['event'], *r['p'], r['chance'], r['flags'], r['phaseMask'], r['link'],
              r['action'], *r['a'], r['target'], *r['t'], r['unitClass'], r['expansion'], int(r['scales']), r['spellLevel'],
              int(r['costScales']), int(r['regenMana'])]
    text = ','.join(('%d' % v if v < 0 else str(v) + 'u') for v in values)
    coords = ','.join(repr(float(c)) + 'f' for c in r['xyzo'])
    return '{' + text + ',' + repr(r['manaModifier']) + 'f,' + coords + '}, // ' + r['name'].replace('*/', '')


HEADER = ('// AzerothCore ' + PIN + '; generated by tools/local_realm/generate_npc_spell_profiles.py; do not hand-edit.\n'
          '// owner,row,entry,event,p1..p6,chance,flags,phaseMask,link,action,a1..a6,target,t1..t4,unitClass,expansion,scales,spellLevel,costScales,regenMana,manaModifier,x,y,z,o')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('dbc', type=Path, help='directory containing the WotLK 3.3.5a Spell.dbc')
    parser.add_argument('--output', type=Path, default=PROJECT / 'include/game')
    parser.add_argument('--report', type=Path, default=PROJECT / 'docs/NPC_SPELL_PROFILE_REPORT.json')
    args = parser.parse_args()
    assert hashlib.sha256((args.dbc / 'Spell.dbc').read_bytes()).hexdigest() == SOURCE_HASHES['Spell.dbc'], 'Unexpected Spell.dbc'
    with tempfile.TemporaryDirectory() as directory, tarfile.open(HERE / 'spell_script_source_sql.tar.gz') as tar:
        tar.extract('spell_script_names.sql', directory, filter='data')
        names = Path(directory) / 'spell_script_names.sql'
        assert hashlib.sha256(names.read_bytes()).hexdigest() == SOURCE_HASHES['spell_script_names.sql'], 'Unexpected spell_script_names.sql'
        scripted = {abs(r['spell_id']) for r in sql_rows(names, 'spell_script_names')}
        tar.extract('spell_scripts.sql', directory, filter='data')
        scripts = Path(directory) / 'spell_scripts.sql'
        assert hashlib.sha256(scripts.read_bytes()).hexdigest() == SOURCE_HASHES['spell_scripts.sql'], 'Unexpected spell_scripts.sql'
        SPELL_SCRIPT_ROWS.clear()
        SPELL_SCRIPT_ROWS.update(r['id'] & 0xffffff for r in sql_rows(scripts, 'spell_scripts'))
    tables = load_tables()
    dbc = Dbc(args.dbc)
    talk = json.loads((PROJECT / 'assets/local_realm/creature_talk.json').read_text())
    talk_rows = {(r['owner'], r['row']) for r in talk['rules']}
    talk_groups = {(g['entry'], g['group']) for g in talk.get('textGroups', [])}
    rows, list_rows, blocked, waypoints, summoned = profiles(tables, dbc, scripted, talk_rows, talk_groups)

    lines = [HEADER] + [format_row(r) for r in rows]
    (args.output / 'local_npc_spell_profiles_generated.inc').write_text('\n'.join(lines) + '\n')
    lines = [HEADER] + [format_row(r) for r in list_rows]
    (args.output / 'local_npc_spell_lists_generated.inc').write_text('\n'.join(lines) + '\n')

    stats = {(r['class'], r['level']): r for r in tables['creature_classlevelstats']}
    lines = ['// creature_classlevelstats BaseDamage[class 1,2,4,8][expansion 0..2][level 0..83]; 0 is invalid.']
    for cls in CLASSES:
        lines.append('{ // class ' + str(cls))
        for exp, column in enumerate(('damage_base', 'damage_exp1', 'damage_exp2')):
            values = ['0.f'] + [repr(float(stats[(cls, level)][column])) + 'f' for level in range(1, 84)]
            assert all(float(stats[(cls, level)][column]) > 0 for level in range(1, 84))
            lines.append('  {' + ','.join(values) + '},')
        lines.append('},')
    (args.output / 'local_npc_spell_scaling_generated.inc').write_text('\n'.join(lines) + '\n')
    lines = ['// creature_classlevelstats BaseMana[class 1,2,4,8][level 0..83]; 0 means no mana.']
    for cls in CLASSES:
        lines.append('{' + ','.join(['0u'] + [str(int(stats[(cls, level)]['basemana'])) + 'u' for level in range(1, 84)]) + '}, // class ' + str(cls))
    (args.output / 'local_npc_spell_mana_generated.inc').write_text('\n'.join(lines) + '\n')
    scaler = (args.dbc / 'gtNPCManaCostScaler.dbc').read_bytes()
    magic, count, fields, size, _ = struct.unpack_from('<4s4I', scaler)
    assert magic == b'WDBC' and count == 100 and fields == 1 and size == 4
    ratios = [struct.unpack_from('<f', scaler, 20 + 4 * i)[0] for i in range(count)]
    assert all(r > 0 for r in ratios)
    lines = ['// gtNPCManaCostScaler.dbc ratio, index = level - 1 (SpellInfo::CalcPowerCost).']
    lines.append(','.join(repr(r) + 'f' for r in ratios))
    (args.output / 'local_npc_spell_mana_scaler_generated.inc').write_text('\n'.join(lines) + '\n')
    rules = load_spell_rules()
    spells = sorted({r['a'][0] for r in rows + list_rows if r['action'] in SPELL_ACTIONS or r['action'] == AC_ADD_AURA})
    lines = ['// spell_cone (degrees) and spell_jump_distance (yards) rows of the installed spells; id,cone,jump.']
    for spell in spells:
        if spell in rules['cone'] or spell in rules['jump']:
            lines.append('{%du,%d,%du},' % (spell, rules['cone'].get(spell, 0), rules['jump'].get(spell, 0)))
    (args.output / 'local_npc_spell_geometry_generated.inc').write_text('\n'.join(lines) + '\n')
    # 2.38: the SmartAI escort paths (waypoints table) the installed rows start; path,point,x,y,z.
    lines = ['// AzerothCore ' + PIN + ' waypoints rows of the installed ESCORT_START paths; path,point,x,y,z.']
    for w in waypoints:
        lines.append('{%du,%du,%rf,%rf,%rf},' % (w['path'], w['point'], w['x'], w['y'], w['z']))
    (args.output / 'local_npc_waypoints_generated.inc').write_text('\n'.join(lines) + '\n')
    # 2.39: CREATURE_FLAG_EXTRA_NO_PLAYER_DAMAGE_REQ (Creature::IsDamageEnoughForLootingAndReward).
    no_req = sorted(r['entry'] for r in tables['creature_template'] if r['flags_extra'] & CREATURE_FLAG_EXTRA_NO_PLAYER_DAMAGE_REQ)
    lines = ['// AzerothCore ' + PIN + ' creature_template entries with CREATURE_FLAG_EXTRA_NO_PLAYER_DAMAGE_REQ; sorted.']
    lines.append(','.join('%du' % e for e in no_req) + ',')
    (args.output / 'local_npc_reward_flags_generated.inc').write_text('\n'.join(lines) + '\n')

    cast_rows = [r for r in rows if r['action'] in SPELL_ACTIONS or r['action'] == AC_ADD_AURA]
    summon_rows = [r for r in rows + list_rows if r['action'] == AC_SUMMON_CREATURE or (r['action'] in SPELL_ACTIONS and SPELL_INFO.get(r['a'][0], {}).get('summon'))]
    report = {'schemaVersion': 5, 'revision': PIN,
              'counts': {'owners': len({r['owner'] for r in rows}), 'entries': len({r['entry'] for r in rows}),
                         'rows': len(rows), 'castRows': len(cast_rows), 'listRows': len(list_rows),
                         'lists': len({r['owner'] for r in list_rows}), 'spells': len(spells), 'gameplayVerified': 0,
                         'summonRows': len(summon_rows), 'summonEntries': len(summoned), 'waypointPaths': len({w['path'] for w in waypoints}),
                         'waypoints': len(waypoints), 'groundAuraSpells': sum(1 for sp in spells if SPELL_INFO.get(sp, {}).get('ground'))},
              'summonEntries': summoned,
              'blockedOwners': dict(sorted(blocked.items())),
              'rowsByEvent': dict(sorted(Counter(r['event'] for r in rows).items())),
              'rowsByAction': dict(sorted(Counter(r['action'] for r in rows).items())),
              'rowsByTarget': dict(sorted(Counter(r['target'] for r in rows).items())),
              'spells': dict(sorted(Counter(r['a'][0] for r in cast_rows).items())),
              'inputs': {**{name: hashlib.sha256((HERE / archive).read_bytes()).hexdigest() for name, archive in sorted(ARCHIVES.items())},
                         'gtNPCManaCostScaler.dbc': hashlib.sha256(scaler).hexdigest(),
                         'spell_rules_source_sql.tar.gz': hashlib.sha256((HERE / 'spell_rules_source_sql.tar.gz').read_bytes()).hexdigest(),
                         'spell_geometry_source_sql.tar.gz': hashlib.sha256((HERE / 'spell_geometry_source_sql.tar.gz').read_bytes()).hexdigest()}}
    args.report.write_text(json.dumps(report, indent=1, sort_keys=True) + '\n')
    print(json.dumps(report['counts']))


if __name__ == '__main__':
    main()
