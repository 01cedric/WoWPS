#pragma once
// ============================================================================
// M2 Animation IDs - AnimationData.dbc
//
// Complete list from https://wowdev.wiki/M2/AnimationList
// Community names in comments describe what each animation looks like in-game.
// Organized by World of Warcraft expansion for easier management.
// ============================================================================

#include <cstdint>
#include <memory>

namespace wowee {
namespace pipeline { class DBCFile; }
namespace rendering {
namespace anim {

// ============================================================================
// Classic (Vanilla WoW 1.x) - Core character & creature animations
// IDs 0–145
// ============================================================================

constexpr uint32_t STAND                      = 0;   // Idle standing pose
constexpr uint32_t DEATH                      = 1;   // Death animation
constexpr uint32_t SPELL                      = 2;   // Generic spell cast
constexpr uint32_t STOP                       = 3;   // Transition to stop
constexpr uint32_t WALK                       = 4;   // Walking forward
constexpr uint32_t RUN                        = 5;   // Running forward
constexpr uint32_t DEAD                       = 6;   // Corpse on the ground
constexpr uint32_t RISE                       = 7;   // Rising from death (resurrection)
constexpr uint32_t STAND_WOUND               = 8;   // Wounded idle stance
constexpr uint32_t COMBAT_WOUND              = 9;   // Wounded combat idle
constexpr uint32_t COMBAT_CRITICAL           = 10;  // Critical hit reaction
constexpr uint32_t SHUFFLE_LEFT              = 11;  // Strafe walk left
constexpr uint32_t SHUFFLE_RIGHT             = 12;  // Strafe walk right
constexpr uint32_t WALK_BACKWARDS            = 13;  // Walking backwards / backpedal
constexpr uint32_t STUN                      = 14;  // Stunned
constexpr uint32_t HANDS_CLOSED              = 15;  // Hands closed (weapon grip idle)
constexpr uint32_t ATTACK_UNARMED            = 16;  // Unarmed melee attack
constexpr uint32_t ATTACK_1H                 = 17;  // One-handed melee attack
constexpr uint32_t ATTACK_2H                 = 18;  // Two-handed melee attack
constexpr uint32_t ATTACK_2H_LOOSE           = 19;  // Polearm/staff two-hand attack
constexpr uint32_t PARRY_UNARMED             = 20;  // Unarmed parry
constexpr uint32_t PARRY_1H                  = 21;  // One-handed weapon parry
constexpr uint32_t PARRY_2H                  = 22;  // Two-handed weapon parry
constexpr uint32_t PARRY_2H_LOOSE            = 23;  // Polearm/staff parry
constexpr uint32_t SHIELD_BLOCK              = 24;  // Shield block
constexpr uint32_t READY_UNARMED             = 25;  // Unarmed combat ready stance
constexpr uint32_t READY_1H                  = 26;  // One-handed weapon ready stance
constexpr uint32_t READY_2H                  = 27;  // Two-handed weapon ready stance
constexpr uint32_t READY_2H_LOOSE            = 28;  // Polearm/staff ready stance
constexpr uint32_t READY_BOW                 = 29;  // Bow ready stance
constexpr uint32_t DODGE                     = 30;  // Dodge
constexpr uint32_t SPELL_PRECAST             = 31;  // Spell precast wind-up
constexpr uint32_t SPELL_CAST                = 32;  // Spell cast
constexpr uint32_t SPELL_CAST_AREA           = 33;  // Area-of-effect spell cast
constexpr uint32_t NPC_WELCOME               = 34;  // NPC greeting animation
constexpr uint32_t NPC_GOODBYE               = 35;  // NPC farewell animation
constexpr uint32_t BLOCK                     = 36;  // Block
constexpr uint32_t JUMP_START                = 37;  // Jump takeoff
constexpr uint32_t JUMP                      = 38;  // Mid-air jump loop
constexpr uint32_t JUMP_END                  = 39;  // Jump landing
constexpr uint32_t FALL                      = 40;  // Falling
constexpr uint32_t SWIM_IDLE                 = 41;  // Treading water
constexpr uint32_t SWIM                      = 42;  // Swimming forward
constexpr uint32_t SWIM_LEFT                 = 43;  // Swim strafe left
constexpr uint32_t SWIM_RIGHT                = 44;  // Swim strafe right
constexpr uint32_t SWIM_BACKWARDS            = 45;  // Swim backwards
constexpr uint32_t ATTACK_BOW                = 46;  // Bow attack
constexpr uint32_t FIRE_BOW                  = 47;  // Fire bow shot
constexpr uint32_t READY_RIFLE               = 48;  // Rifle/gun ready stance
constexpr uint32_t ATTACK_RIFLE              = 49;  // Rifle/gun attack
constexpr uint32_t LOOT                      = 50;  // Looting / bending down to pick up
constexpr uint32_t READY_SPELL_DIRECTED      = 51;  // Directed spell ready
constexpr uint32_t READY_SPELL_OMNI          = 52;  // Omni spell ready
constexpr uint32_t SPELL_CAST_DIRECTED       = 53;  // Directed spell cast
constexpr uint32_t SPELL_CAST_OMNI           = 54;  // Omni spell cast
constexpr uint32_t BATTLE_ROAR               = 55;  // Battle shout / roar
constexpr uint32_t READY_ABILITY             = 56;  // Ability ready stance
constexpr uint32_t SPECIAL_1H                = 57;  // Special one-handed attack
constexpr uint32_t SPECIAL_2H                = 58;  // Special two-handed attack
constexpr uint32_t SHIELD_BASH               = 59;  // Shield bash
constexpr uint32_t EMOTE_TALK                = 60;  // /talk
constexpr uint32_t EMOTE_EAT                 = 61;  // /eat
constexpr uint32_t EMOTE_WORK                = 62;  // /work
constexpr uint32_t EMOTE_USE_STANDING        = 63;  // Standing use animation
constexpr uint32_t EMOTE_EXCLAMATION         = 64;  // NPC exclamation (!)
constexpr uint32_t EMOTE_QUESTION            = 65;  // NPC question (?)
constexpr uint32_t EMOTE_BOW                 = 66;  // /bow
constexpr uint32_t EMOTE_WAVE                = 67;  // /wave
constexpr uint32_t EMOTE_CHEER               = 68;  // /cheer
constexpr uint32_t EMOTE_DANCE               = 69;  // /dance
constexpr uint32_t EMOTE_LAUGH               = 70;  // /laugh
constexpr uint32_t EMOTE_SLEEP               = 71;  // /sleep
constexpr uint32_t EMOTE_SIT_GROUND          = 72;  // /sit on ground
constexpr uint32_t EMOTE_RUDE                = 73;  // /rude
constexpr uint32_t EMOTE_ROAR                = 74;  // /roar
constexpr uint32_t EMOTE_KNEEL               = 75;  // /kneel
constexpr uint32_t EMOTE_KISS                = 76;  // /kiss
constexpr uint32_t EMOTE_CRY                 = 77;  // /cry
constexpr uint32_t EMOTE_CHICKEN             = 78;  // /chicken - flap arms and strut
constexpr uint32_t EMOTE_BEG                 = 79;  // /beg
constexpr uint32_t EMOTE_APPLAUD             = 80;  // /applaud
constexpr uint32_t EMOTE_SHOUT               = 81;  // /shout
constexpr uint32_t EMOTE_FLEX                = 82;  // /flex - show off muscles
constexpr uint32_t EMOTE_SHY                 = 83;  // /shy
constexpr uint32_t EMOTE_POINT               = 84;  // /point
constexpr uint32_t ATTACK_1H_PIERCE          = 85;  // One-handed pierce (dagger stab)
constexpr uint32_t ATTACK_2H_LOOSE_PIERCE    = 86;  // Polearm/staff pierce
constexpr uint32_t ATTACK_OFF                = 87;  // Off-hand attack
constexpr uint32_t ATTACK_OFF_PIERCE         = 88;  // Off-hand pierce attack
constexpr uint32_t SHEATHE                   = 89;  // Sheathe weapons
constexpr uint32_t HIP_SHEATHE               = 90;  // Hip sheathe
constexpr uint32_t MOUNT                     = 91;  // Mounted idle
constexpr uint32_t RUN_RIGHT                 = 92;  // Strafe run right
constexpr uint32_t RUN_LEFT                  = 93;  // Strafe run left
constexpr uint32_t MOUNT_SPECIAL             = 94;  // Mount rearing / special move
constexpr uint32_t KICK                      = 95;  // Kick
constexpr uint32_t SIT_GROUND_DOWN           = 96;  // Transition: standing → sitting
constexpr uint32_t SITTING                   = 97;  // Sitting on ground loop
constexpr uint32_t SIT_GROUND_UP             = 98;  // Transition: sitting → standing
constexpr uint32_t SLEEP_DOWN                = 99;  // Transition: standing → sleeping
constexpr uint32_t SLEEP                     = 100; // Sleeping loop
constexpr uint32_t SLEEP_UP                  = 101; // Transition: sleeping → standing
constexpr uint32_t SIT_CHAIR_LOW             = 102; // Sit in low chair
constexpr uint32_t SIT_CHAIR_MED             = 103; // Sit in medium chair
constexpr uint32_t SIT_CHAIR_HIGH            = 104; // Sit in high chair
constexpr uint32_t LOAD_BOW                  = 105; // Nock/load bow
constexpr uint32_t LOAD_RIFLE               = 106; // Load rifle/gun
constexpr uint32_t ATTACK_THROWN             = 107; // Thrown weapon attack
constexpr uint32_t READY_THROWN              = 108; // Thrown weapon ready
constexpr uint32_t HOLD_BOW                  = 109; // Hold bow idle
constexpr uint32_t HOLD_RIFLE               = 110; // Hold rifle/gun idle
constexpr uint32_t HOLD_THROWN              = 111; // Hold thrown weapon idle
constexpr uint32_t LOAD_THROWN              = 112; // Load thrown weapon
constexpr uint32_t EMOTE_SALUTE              = 113; // /salute
constexpr uint32_t KNEEL_START               = 114; // Transition: standing → kneeling
constexpr uint32_t KNEEL_LOOP                = 115; // Kneeling loop
constexpr uint32_t KNEEL_END                 = 116; // Transition: kneeling → standing
constexpr uint32_t ATTACK_UNARMED_OFF        = 117; // Off-hand unarmed attack
constexpr uint32_t SPECIAL_UNARMED           = 118; // Special unarmed attack
constexpr uint32_t STEALTH_WALK              = 119; // Stealth walking (rogue sneak)
constexpr uint32_t STEALTH_STAND             = 120; // Stealth standing idle
constexpr uint32_t KNOCKDOWN                 = 121; // Knocked down
constexpr uint32_t EATING_LOOP               = 122; // Eating loop (food/drink)
constexpr uint32_t USE_STANDING_LOOP         = 123; // Use standing loop
constexpr uint32_t CHANNEL_CAST_DIRECTED     = 124; // Channeled directed cast
constexpr uint32_t CHANNEL_CAST_OMNI         = 125; // Channeled omni cast
constexpr uint32_t WHIRLWIND                 = 126; // Whirlwind attack (warrior)
constexpr uint32_t BIRTH                     = 127; // Creature birth/spawn
constexpr uint32_t USE_STANDING_START        = 128; // Use standing start
constexpr uint32_t USE_STANDING_END          = 129; // Use standing end
constexpr uint32_t CREATURE_SPECIAL          = 130; // Creature special ability
constexpr uint32_t DROWN                     = 131; // Drowning
constexpr uint32_t DROWNED                   = 132; // Drowned corpse underwater
constexpr uint32_t FISHING_CAST              = 133; // Fishing cast
constexpr uint32_t FISHING_LOOP              = 134; // Fishing idle loop
constexpr uint32_t FLY                       = 135; // Flying generic
constexpr uint32_t EMOTE_WORK_NO_SHEATHE     = 136; // Work emote (no weapon sheathe)
constexpr uint32_t EMOTE_STUN_NO_SHEATHE     = 137; // Stun emote (no weapon sheathe)
constexpr uint32_t EMOTE_USE_STANDING_NO_SHEATHE = 138; // Use standing (no weapon sheathe)
constexpr uint32_t SPELL_SLEEP_DOWN          = 139; // Spell-induced sleep down
constexpr uint32_t SPELL_KNEEL_START         = 140; // Spell-induced kneel start
constexpr uint32_t SPELL_KNEEL_LOOP          = 141; // Spell-induced kneel loop
constexpr uint32_t SPELL_KNEEL_END           = 142; // Spell-induced kneel end
constexpr uint32_t SPRINT                    = 143; // Sprint / Custom Spell 01
constexpr uint32_t IN_FLIGHT                 = 144; // In-flight (flight path travel)
constexpr uint32_t SPAWN                     = 145; // Object/creature spawn animation

// ============================================================================
// The Burning Crusade (TBC 2.x) - Flying mounts, game objects, stealth run
// IDs 146–199
// ============================================================================

constexpr uint32_t CLOSE                     = 146; // Game object close
constexpr uint32_t CLOSED                    = 147; // Game object closed loop
constexpr uint32_t OPEN                      = 148; // Game object open
constexpr uint32_t DESTROY                   = 149; // Game object destroy
constexpr uint32_t DESTROYED                 = 150; // Game object destroyed state
constexpr uint32_t UNSHEATHE                 = 151; // Unsheathe weapons
constexpr uint32_t SHEATHE_ALT               = 152; // Sheathe weapons (alternate)
constexpr uint32_t ATTACK_UNARMED_NO_SHEATHE = 153; // Unarmed attack (no sheathe)
constexpr uint32_t STEALTH_RUN               = 154; // Stealth running (rogue sprint)
constexpr uint32_t READY_CROSSBOW            = 155; // Crossbow ready stance
constexpr uint32_t ATTACK_CROSSBOW           = 156; // Crossbow attack
constexpr uint32_t EMOTE_TALK_EXCLAMATION    = 157; // /talk with exclamation
constexpr uint32_t FLY_IDLE                  = 158; // Flying mount idle / hovering
constexpr uint32_t FLY_FORWARD               = 159; // Flying mount forward
constexpr uint32_t FLY_BACKWARDS             = 160; // Flying mount backwards
constexpr uint32_t FLY_LEFT                  = 161; // Flying mount strafe left
constexpr uint32_t FLY_RIGHT                 = 162; // Flying mount strafe right
constexpr uint32_t FLY_UP                    = 163; // Flying mount ascending
constexpr uint32_t FLY_DOWN                  = 164; // Flying mount descending
constexpr uint32_t FLY_LAND_START            = 165; // Flying mount land start
constexpr uint32_t FLY_LAND_RUN              = 166; // Flying mount land run
constexpr uint32_t FLY_LAND_END              = 167; // Flying mount land end
constexpr uint32_t EMOTE_TALK_QUESTION       = 168; // /talk with question
constexpr uint32_t EMOTE_READ                = 169; // /read (reading animation)
constexpr uint32_t EMOTE_SHIELDBLOCK         = 170; // Shield block emote
constexpr uint32_t EMOTE_CHOP                = 171; // Chopping emote (lumber)
constexpr uint32_t EMOTE_HOLDRIFLE           = 172; // Hold rifle emote
constexpr uint32_t EMOTE_HOLDBOW             = 173; // Hold bow emote
constexpr uint32_t EMOTE_HOLDTHROWN          = 174; // Hold thrown weapon emote
constexpr uint32_t CUSTOM_SPELL_02           = 175; // Custom spell animation 02
constexpr uint32_t CUSTOM_SPELL_03           = 176; // Custom spell animation 03
constexpr uint32_t CUSTOM_SPELL_04           = 177; // Custom spell animation 04
constexpr uint32_t CUSTOM_SPELL_05           = 178; // Custom spell animation 05
constexpr uint32_t CUSTOM_SPELL_06           = 179; // Custom spell animation 06
constexpr uint32_t CUSTOM_SPELL_07           = 180; // Custom spell animation 07
constexpr uint32_t CUSTOM_SPELL_08           = 181; // Custom spell animation 08
constexpr uint32_t CUSTOM_SPELL_09           = 182; // Custom spell animation 09
constexpr uint32_t CUSTOM_SPELL_10           = 183; // Custom spell animation 10
constexpr uint32_t EMOTE_STATE_DANCE         = 184; // /dance state (looping dance)

// ============================================================================
// Wrath of the Lich King (WotLK 3.x) - Vehicles, reclined, crafting, etc.
// IDs 185+
// ============================================================================

constexpr uint32_t FLY_STAND                 = 185; // Flying stand (hover in place)
constexpr uint32_t EMOTE_STATE_LAUGH         = 186; // /laugh state loop
constexpr uint32_t EMOTE_STATE_POINT         = 187; // /point state loop
constexpr uint32_t EMOTE_STATE_EAT           = 188; // /eat state loop
constexpr uint32_t EMOTE_STATE_WORK          = 189; // /work state loop (crafting NPC)
constexpr uint32_t EMOTE_STATE_SIT_GROUND    = 190; // /sit ground state loop
constexpr uint32_t EMOTE_STATE_HOLD_BOW      = 191; // Hold bow state loop
constexpr uint32_t EMOTE_STATE_HOLD_RIFLE    = 192; // Hold rifle state loop
constexpr uint32_t EMOTE_STATE_HOLD_THROWN   = 193; // Hold thrown state loop
constexpr uint32_t FLY_COMBAT_WOUND          = 194; // Flying wounded
constexpr uint32_t FLY_COMBAT_CRITICAL       = 195; // Flying critical hit reaction
constexpr uint32_t RECLINED                  = 196; // Reclined / laid back pose
constexpr uint32_t EMOTE_STATE_ROAR          = 197; // /roar state loop
constexpr uint32_t EMOTE_USE_STANDING_LOOP_2 = 198; // Use standing loop variant
constexpr uint32_t EMOTE_STATE_APPLAUD       = 199; // /applaud state loop
constexpr uint32_t READY_FIST                = 200; // Fist weapon ready stance
constexpr uint32_t SPELL_CHANNEL_DIRECTED_OMNI = 201; // Channel directed omni
constexpr uint32_t SPECIAL_ATTACK_1H_OFF     = 202; // Special off-hand one-handed attack
constexpr uint32_t ATTACK_FIST_1H            = 203; // Fist weapon one-hand attack
constexpr uint32_t ATTACK_FIST_1H_OFF        = 204; // Fist weapon off-hand attack
constexpr uint32_t PARRY_FIST_1H             = 205; // Fist weapon parry

constexpr uint32_t READY_FIST_1H             = 206; // Fist weapon one-hand ready
constexpr uint32_t EMOTE_STATE_READ_AND_TALK = 207; // Read and talk NPC loop
constexpr uint32_t EMOTE_STATE_WORK_NO_SHEATHE = 208; // Work no sheathe state loop
constexpr uint32_t FLY_RUN                   = 209; // Flying run (fast forward flight)
constexpr uint32_t EMOTE_STATE_KNEEL_2       = 210; // Kneel state variant
constexpr uint32_t EMOTE_STATE_SPELL_KNEEL   = 211; // Spell kneel state loop
constexpr uint32_t EMOTE_STATE_USE_STANDING  = 212; // Use standing state
constexpr uint32_t EMOTE_STATE_STUN          = 213; // Stun state loop
constexpr uint32_t EMOTE_STATE_STUN_NO_SHEATHE = 214; // Stun no sheathe state
constexpr uint32_t EMOTE_TRAIN               = 215; // /train - choo choo!
constexpr uint32_t EMOTE_DEAD                = 216; // /dead - play dead
constexpr uint32_t EMOTE_STATE_DANCE_ONCE    = 217; // Single dance animation
constexpr uint32_t FLY_DEATH                 = 218; // Flying death
constexpr uint32_t FLY_STAND_WOUND          = 219; // Flying wounded stand
constexpr uint32_t FLY_SHUFFLE_LEFT          = 220; // Flying strafe left
constexpr uint32_t FLY_SHUFFLE_RIGHT         = 221; // Flying strafe right
constexpr uint32_t FLY_WALK_BACKWARDS        = 222; // Flying walk backwards
constexpr uint32_t FLY_STUN                  = 223; // Flying stunned
constexpr uint32_t FLY_HANDS_CLOSED          = 224; // Flying hands closed
constexpr uint32_t FLY_ATTACK_UNARMED        = 225; // Flying unarmed attack
constexpr uint32_t FLY_ATTACK_1H             = 226; // Flying one-hand attack
constexpr uint32_t FLY_ATTACK_2H             = 227; // Flying two-hand attack
constexpr uint32_t FLY_ATTACK_2H_LOOSE       = 228; // Flying polearm attack
constexpr uint32_t FLY_SPELL                 = 229; // Flying spell - generic spell while flying
constexpr uint32_t FLY_STOP                  = 230; // Flying stop
constexpr uint32_t FLY_WALK                  = 231; // Flying walk
constexpr uint32_t FLY_DEAD                  = 232; // Flying dead (corpse mid-air)
constexpr uint32_t FLY_RISE                  = 233; // Flying rise - resurrection mid-air
constexpr uint32_t FLY_RUN_2                 = 234; // Flying run variant
constexpr uint32_t FLY_FALL                  = 235; // Flying fall
constexpr uint32_t FLY_SWIM_IDLE             = 236; // Flying swim idle
constexpr uint32_t FLY_SWIM                  = 237; // Flying swim
constexpr uint32_t FLY_SWIM_LEFT             = 238; // Flying swim left
constexpr uint32_t FLY_SWIM_RIGHT            = 239; // Flying swim right
constexpr uint32_t FLY_SWIM_BACKWARDS        = 240; // Flying swim backwards
constexpr uint32_t FLY_ATTACK_BOW            = 241; // Flying bow attack
constexpr uint32_t FLY_FIRE_BOW              = 242; // Flying fire bow
constexpr uint32_t FLY_READY_RIFLE           = 243; // Flying rifle ready
constexpr uint32_t FLY_ATTACK_RIFLE          = 244; // Flying rifle attack

// ── WotLK Vehicle & extended movement animations ──────────────────────────

constexpr uint32_t TOTEM_SMALL               = 245; // Small totem idle (shaman)
constexpr uint32_t TOTEM_MEDIUM              = 246; // Medium totem idle
constexpr uint32_t TOTEM_LARGE               = 247; // Large totem idle
constexpr uint32_t FLY_LOOT                  = 248; // Flying loot
constexpr uint32_t FLY_READY_SPELL_DIRECTED  = 249; // Flying directed spell ready
constexpr uint32_t FLY_READY_SPELL_OMNI      = 250; // Flying omni spell ready
constexpr uint32_t FLY_SPELL_CAST_DIRECTED   = 251; // Flying directed spell cast
constexpr uint32_t FLY_SPELL_CAST_OMNI       = 252; // Flying omni spell cast
constexpr uint32_t FLY_BATTLE_ROAR           = 253; // Flying battle shout
constexpr uint32_t FLY_READY_ABILITY         = 254; // Flying ability ready
constexpr uint32_t FLY_SPECIAL_1H            = 255; // Flying special one-hand
constexpr uint32_t FLY_SPECIAL_2H            = 256; // Flying special two-hand
constexpr uint32_t FLY_SHIELD_BASH           = 257; // Flying shield bash
constexpr uint32_t FLY_EMOTE_TALK            = 258; // Flying emote talk
constexpr uint32_t FLY_EMOTE_EAT             = 259; // Flying emote eat
constexpr uint32_t FLY_EMOTE_WORK            = 260; // Flying emote work
constexpr uint32_t FLY_EMOTE_USE_STANDING    = 261; // Flying emote use standing
constexpr uint32_t FLY_EMOTE_BOW             = 262; // Flying emote bow
constexpr uint32_t FLY_EMOTE_WAVE            = 263; // Flying emote wave
constexpr uint32_t FLY_EMOTE_CHEER           = 264; // Flying emote cheer
constexpr uint32_t FLY_EMOTE_DANCE           = 265; // Flying emote dance
constexpr uint32_t FLY_EMOTE_LAUGH           = 266; // Flying emote laugh
constexpr uint32_t FLY_EMOTE_SLEEP           = 267; // Flying emote sleep
constexpr uint32_t FLY_EMOTE_SIT_GROUND      = 268; // Flying emote sit ground
constexpr uint32_t FLY_EMOTE_RUDE            = 269; // Flying emote rude
constexpr uint32_t FLY_EMOTE_ROAR            = 270; // Flying emote roar
constexpr uint32_t FLY_EMOTE_KNEEL           = 271; // Flying emote kneel
constexpr uint32_t FLY_EMOTE_KISS            = 272; // Flying emote kiss
constexpr uint32_t FLY_EMOTE_CRY             = 273; // Flying emote cry
constexpr uint32_t FLY_EMOTE_CHICKEN         = 274; // Flying emote chicken
constexpr uint32_t FLY_EMOTE_BEG             = 275; // Flying emote beg
constexpr uint32_t FLY_EMOTE_APPLAUD         = 276; // Flying emote applaud
constexpr uint32_t FLY_EMOTE_SHOUT           = 277; // Flying emote shout
constexpr uint32_t FLY_EMOTE_FLEX            = 278; // Flying emote flex
constexpr uint32_t FLY_EMOTE_SHY             = 279; // Flying emote shy
constexpr uint32_t FLY_EMOTE_POINT           = 280; // Flying emote point
constexpr uint32_t FLY_ATTACK_1H_PIERCE      = 281; // Flying one-hand pierce
constexpr uint32_t FLY_ATTACK_2H_LOOSE_PIERCE = 282; // Flying polearm pierce
constexpr uint32_t FLY_ATTACK_OFF            = 283; // Flying off-hand attack
constexpr uint32_t FLY_ATTACK_OFF_PIERCE     = 284; // Flying off-hand pierce
constexpr uint32_t FLY_SHEATHE               = 285; // Flying sheathe
constexpr uint32_t FLY_HIP_SHEATHE           = 286; // Flying hip sheathe
constexpr uint32_t FLY_MOUNT                 = 287; // Flying mounted
constexpr uint32_t FLY_RUN_RIGHT             = 288; // Flying strafe run right
constexpr uint32_t FLY_RUN_LEFT              = 289; // Flying strafe run left
constexpr uint32_t FLY_MOUNT_SPECIAL         = 290; // Flying mount special
constexpr uint32_t FLY_KICK                  = 291; // Flying kick
constexpr uint32_t FLY_SIT_GROUND_DOWN       = 292; // Flying sit ground down
constexpr uint32_t FLY_SITTING               = 293; // Flying sitting
constexpr uint32_t FLY_SIT_GROUND_UP         = 294; // Flying sit ground up
constexpr uint32_t FLY_SLEEP_DOWN            = 295; // Flying sleep down
constexpr uint32_t FLY_SLEEP                 = 296; // Flying sleeping
constexpr uint32_t FLY_SLEEP_UP              = 297; // Flying sleep up
constexpr uint32_t FLY_SIT_CHAIR_LOW         = 298; // Flying sit chair low
constexpr uint32_t FLY_SIT_CHAIR_MED         = 299; // Flying sit chair med
constexpr uint32_t FLY_SIT_CHAIR_HIGH        = 300; // Flying sit chair high
constexpr uint32_t FLY_LOAD_BOW              = 301; // Flying load bow
constexpr uint32_t FLY_LOAD_RIFLE            = 302; // Flying load rifle
constexpr uint32_t FLY_ATTACK_THROWN          = 303; // Flying thrown attack
constexpr uint32_t FLY_READY_THROWN           = 304; // Flying thrown ready
constexpr uint32_t FLY_HOLD_BOW              = 305; // Flying hold bow
constexpr uint32_t FLY_HOLD_RIFLE            = 306; // Flying hold rifle
constexpr uint32_t FLY_HOLD_THROWN           = 307; // Flying hold thrown
constexpr uint32_t FLY_LOAD_THROWN           = 308; // Flying load thrown
constexpr uint32_t FLY_EMOTE_SALUTE          = 309; // Flying emote salute
constexpr uint32_t FLY_KNEEL_START           = 310; // Flying kneel start
constexpr uint32_t FLY_KNEEL_LOOP            = 311; // Flying kneel loop
constexpr uint32_t FLY_KNEEL_END             = 312; // Flying kneel end
constexpr uint32_t FLY_ATTACK_UNARMED_OFF    = 313; // Flying off-hand unarmed
constexpr uint32_t FLY_SPECIAL_UNARMED       = 314; // Flying special unarmed
constexpr uint32_t FLY_STEALTH_WALK          = 315; // Flying stealth walk
constexpr uint32_t FLY_STEALTH_STAND         = 316; // Flying stealth stand
constexpr uint32_t FLY_KNOCKDOWN             = 317; // Flying knockdown
constexpr uint32_t FLY_EATING_LOOP           = 318; // Flying eating loop
constexpr uint32_t FLY_USE_STANDING_LOOP     = 319; // Flying use standing loop
constexpr uint32_t FLY_CHANNEL_CAST_DIRECTED = 320; // Flying directed channel
constexpr uint32_t FLY_CHANNEL_CAST_OMNI     = 321; // Flying omni channel
constexpr uint32_t FLY_WHIRLWIND             = 322; // Flying whirlwind
constexpr uint32_t FLY_BIRTH                 = 323; // Flying birth/spawn
constexpr uint32_t FLY_USE_STANDING_START    = 324; // Flying use standing start
constexpr uint32_t FLY_USE_STANDING_END      = 325; // Flying use standing end
constexpr uint32_t FLY_CREATURE_SPECIAL      = 326; // Flying creature special
constexpr uint32_t FLY_DROWN                 = 327; // Flying drown
constexpr uint32_t FLY_DROWNED               = 328; // Flying drowned
constexpr uint32_t FLY_FISHING_CAST          = 329; // Flying fishing cast
constexpr uint32_t FLY_FISHING_LOOP          = 330; // Flying fishing loop
constexpr uint32_t FLY_FLY                   = 331; // Flying fly
constexpr uint32_t FLY_EMOTE_WORK_NO_SHEATHE = 332; // Flying work no sheathe
constexpr uint32_t FLY_EMOTE_STUN_NO_SHEATHE = 333; // Flying stun no sheathe
constexpr uint32_t FLY_EMOTE_USE_STANDING_NO_SHEATHE = 334; // Flying use standing no sheathe
constexpr uint32_t FLY_SPELL_SLEEP_DOWN      = 335; // Flying spell sleep down
constexpr uint32_t FLY_SPELL_KNEEL_START     = 336; // Flying spell kneel start
constexpr uint32_t FLY_SPELL_KNEEL_LOOP      = 337; // Flying spell kneel loop
constexpr uint32_t FLY_SPELL_KNEEL_END       = 338; // Flying spell kneel end
constexpr uint32_t FLY_SPRINT                = 339; // Flying sprint
constexpr uint32_t FLY_IN_FLIGHT             = 340; // Flying in-flight
constexpr uint32_t FLY_SPAWN                 = 341; // Flying spawn
constexpr uint32_t FLY_CLOSE                 = 342; // Flying close
constexpr uint32_t FLY_CLOSED                = 343; // Flying closed
constexpr uint32_t FLY_OPEN                  = 344; // Flying open
constexpr uint32_t FLY_DESTROY               = 345; // Flying destroy
constexpr uint32_t FLY_DESTROYED             = 346; // Flying destroyed
constexpr uint32_t FLY_UNSHEATHE             = 347; // Flying unsheathe
constexpr uint32_t FLY_SHEATHE_ALT           = 348; // Flying sheathe alt
constexpr uint32_t FLY_ATTACK_UNARMED_NO_SHEATHE = 349; // Flying unarmed no sheathe
constexpr uint32_t FLY_STEALTH_RUN           = 350; // Flying stealth run
constexpr uint32_t FLY_READY_CROSSBOW        = 351; // Flying crossbow ready
constexpr uint32_t FLY_ATTACK_CROSSBOW       = 352; // Flying crossbow attack
constexpr uint32_t FLY_EMOTE_TALK_EXCLAMATION = 353; // Flying talk exclamation
constexpr uint32_t FLY_EMOTE_TALK_QUESTION   = 354; // Flying talk question
constexpr uint32_t FLY_EMOTE_READ            = 355; // Flying emote read

// ── WotLK extended creature animations ────────────────────────────────────

constexpr uint32_t EMOTE_HOLD_CROSSBOW       = 356; // Hold crossbow emote
constexpr uint32_t FLY_EMOTE_HOLD_BOW        = 357; // Flying hold bow emote
constexpr uint32_t FLY_EMOTE_HOLD_RIFLE      = 358; // Flying hold rifle emote
constexpr uint32_t FLY_EMOTE_HOLD_THROWN     = 359; // Flying hold thrown emote
constexpr uint32_t FLY_EMOTE_HOLD_CROSSBOW   = 360; // Flying hold crossbow emote
constexpr uint32_t FLY_CUSTOM_SPELL_02       = 361; // Flying custom spell 02
constexpr uint32_t FLY_CUSTOM_SPELL_03       = 362; // Flying custom spell 03
constexpr uint32_t FLY_CUSTOM_SPELL_04       = 363; // Flying custom spell 04
constexpr uint32_t FLY_CUSTOM_SPELL_05       = 364; // Flying custom spell 05
constexpr uint32_t FLY_CUSTOM_SPELL_06       = 365; // Flying custom spell 06
constexpr uint32_t FLY_CUSTOM_SPELL_07       = 366; // Flying custom spell 07
constexpr uint32_t FLY_CUSTOM_SPELL_08       = 367; // Flying custom spell 08
constexpr uint32_t FLY_CUSTOM_SPELL_09       = 368; // Flying custom spell 09
constexpr uint32_t FLY_CUSTOM_SPELL_10       = 369; // Flying custom spell 10
constexpr uint32_t FLY_EMOTE_STATE_DANCE     = 370; // Flying dance state
constexpr uint32_t EMOTE_EAT_NO_SHEATHE      = 371; // Eat emote (no weapon sheathe)
constexpr uint32_t MOUNT_RUN_RIGHT           = 372; // Mounted strafe run right
constexpr uint32_t MOUNT_RUN_LEFT            = 373; // Mounted strafe run left
constexpr uint32_t MOUNT_WALK_BACKWARDS      = 374; // Mounted walk backwards
constexpr uint32_t MOUNT_SWIM_IDLE           = 375; // Mounted swimming idle
constexpr uint32_t MOUNT_SWIM                = 376; // Mounted swimming forward
constexpr uint32_t MOUNT_SWIM_LEFT           = 377; // Mounted swimming left
constexpr uint32_t MOUNT_SWIM_RIGHT          = 378; // Mounted swimming right
constexpr uint32_t MOUNT_SWIM_BACKWARDS      = 379; // Mounted swimming backwards
constexpr uint32_t MOUNT_FLIGHT_IDLE         = 380; // Mounted flight idle (hovering)
constexpr uint32_t MOUNT_FLIGHT_FORWARD      = 381; // Mounted flight forward
constexpr uint32_t MOUNT_FLIGHT_BACKWARDS    = 382; // Mounted flight backwards
constexpr uint32_t MOUNT_FLIGHT_LEFT         = 383; // Mounted flight left
constexpr uint32_t MOUNT_FLIGHT_RIGHT        = 384; // Mounted flight right
constexpr uint32_t MOUNT_FLIGHT_UP           = 385; // Mounted flight ascending
constexpr uint32_t MOUNT_FLIGHT_DOWN         = 386; // Mounted flight descending
constexpr uint32_t MOUNT_FLIGHT_LAND_START   = 387; // Mounted flight land start
constexpr uint32_t MOUNT_FLIGHT_LAND_RUN     = 388; // Mounted flight land run
constexpr uint32_t MOUNT_FLIGHT_LAND_END     = 389; // Mounted flight land end
constexpr uint32_t FLY_EMOTE_STATE_LAUGH     = 390; // Flying laugh state
constexpr uint32_t FLY_EMOTE_STATE_POINT     = 391; // Flying point state
constexpr uint32_t FLY_EMOTE_STATE_EAT       = 392; // Flying eat state
constexpr uint32_t FLY_EMOTE_STATE_WORK      = 393; // Flying work state
constexpr uint32_t FLY_EMOTE_STATE_SIT_GROUND = 394; // Flying sit ground state
constexpr uint32_t FLY_EMOTE_STATE_HOLD_BOW  = 395; // Flying hold bow state
constexpr uint32_t FLY_EMOTE_STATE_HOLD_RIFLE = 396; // Flying hold rifle state
constexpr uint32_t FLY_EMOTE_STATE_HOLD_THROWN = 397; // Flying hold thrown state
constexpr uint32_t FLY_EMOTE_STATE_ROAR      = 398; // Flying roar state
constexpr uint32_t FLY_RECLINED              = 399; // Flying reclined
constexpr uint32_t EMOTE_TRAIN_2             = 400; // /train variant - choo choo!
constexpr uint32_t EMOTE_DEAD_2              = 401; // /dead variant (play dead)
constexpr uint32_t FLY_EMOTE_USE_STANDING_LOOP_2 = 402; // Flying use standing loop
constexpr uint32_t FLY_EMOTE_STATE_APPLAUD   = 403; // Flying applaud state
constexpr uint32_t FLY_READY_FIST            = 404; // Flying fist ready
constexpr uint32_t FLY_SPELL_CHANNEL_DIRECTED_OMNI = 405; // Flying channel directed omni
constexpr uint32_t FLY_SPECIAL_ATTACK_1H_OFF = 406; // Flying special off-hand
constexpr uint32_t FLY_ATTACK_FIST_1H        = 407; // Flying fist attack
constexpr uint32_t FLY_ATTACK_FIST_1H_OFF    = 408; // Flying fist off-hand
constexpr uint32_t FLY_PARRY_FIST_1H         = 409; // Flying fist parry
constexpr uint32_t FLY_READY_FIST_1H         = 410; // Flying fist one-hand ready
constexpr uint32_t FLY_EMOTE_STATE_READ_AND_TALK = 411; // Flying read and talk state
constexpr uint32_t FLY_EMOTE_STATE_WORK_NO_SHEATHE = 412; // Flying work no sheathe state
constexpr uint32_t FLY_EMOTE_STATE_KNEEL_2   = 413; // Flying kneel state variant
constexpr uint32_t FLY_EMOTE_STATE_SPELL_KNEEL = 414; // Flying spell kneel state
constexpr uint32_t FLY_EMOTE_STATE_USE_STANDING = 415; // Flying use standing state
constexpr uint32_t FLY_EMOTE_STATE_STUN      = 416; // Flying stun state
constexpr uint32_t FLY_EMOTE_STATE_STUN_NO_SHEATHE = 417; // Flying stun no sheathe state
constexpr uint32_t FLY_EMOTE_TRAIN           = 418; // Flying train emote
constexpr uint32_t FLY_EMOTE_DEAD            = 419; // Flying dead emote
constexpr uint32_t FLY_EMOTE_STATE_DANCE_ONCE = 420; // Flying single dance
constexpr uint32_t FLY_EMOTE_EAT_NO_SHEATHE  = 421; // Flying eat no sheathe
constexpr uint32_t FLY_MOUNT_RUN_RIGHT       = 422; // Flying mount run right
constexpr uint32_t FLY_MOUNT_RUN_LEFT        = 423; // Flying mount run left
constexpr uint32_t FLY_MOUNT_WALK_BACKWARDS  = 424; // Flying mount walk backwards
constexpr uint32_t FLY_MOUNT_SWIM_IDLE       = 425; // Flying mount swim idle
constexpr uint32_t FLY_MOUNT_SWIM            = 426; // Flying mount swim
constexpr uint32_t FLY_MOUNT_SWIM_LEFT       = 427; // Flying mount swim left
constexpr uint32_t FLY_MOUNT_SWIM_RIGHT      = 428; // Flying mount swim right
constexpr uint32_t FLY_MOUNT_SWIM_BACKWARDS  = 429; // Flying mount swim backwards
constexpr uint32_t FLY_MOUNT_FLIGHT_IDLE     = 430; // Flying mount flight idle
constexpr uint32_t FLY_MOUNT_FLIGHT_FORWARD  = 431; // Flying mount flight forward
constexpr uint32_t FLY_MOUNT_FLIGHT_BACKWARDS = 432; // Flying mount flight backwards
constexpr uint32_t FLY_MOUNT_FLIGHT_LEFT     = 433; // Flying mount flight left
constexpr uint32_t FLY_MOUNT_FLIGHT_RIGHT    = 434; // Flying mount flight right
constexpr uint32_t FLY_MOUNT_FLIGHT_UP       = 435; // Flying mount flight up
constexpr uint32_t FLY_MOUNT_FLIGHT_DOWN     = 436; // Flying mount flight down
constexpr uint32_t FLY_MOUNT_FLIGHT_LAND_START = 437; // Flying mount flight land start
constexpr uint32_t FLY_MOUNT_FLIGHT_LAND_RUN = 438; // Flying mount flight land run
constexpr uint32_t FLY_MOUNT_FLIGHT_LAND_END = 439; // Flying mount flight land end
constexpr uint32_t FLY_TOTEM_SMALL           = 440; // Flying small totem
constexpr uint32_t FLY_TOTEM_MEDIUM          = 441; // Flying medium totem
constexpr uint32_t FLY_TOTEM_LARGE           = 442; // Flying large totem
constexpr uint32_t FLY_EMOTE_HOLD_CROSSBOW_2 = 443; // Flying hold crossbow (variant)

// ── WotLK vehicle-specific & late additions ───────────────────────────────

constexpr uint32_t VEHICLE_GRAB               = 444; // Vehicle: grab object
constexpr uint32_t VEHICLE_THROW              = 445; // Vehicle: throw object
constexpr uint32_t FLY_VEHICLE_GRAB           = 446; // Flying vehicle grab
constexpr uint32_t FLY_VEHICLE_THROW          = 447; // Flying vehicle throw
constexpr uint32_t GUILD_CHAMPION_1           = 448; // Guild champion pose 1
constexpr uint32_t GUILD_CHAMPION_2           = 449; // Guild champion pose 2
constexpr uint32_t FLY_GUILD_CHAMPION_1       = 450; // Flying guild champion 1
constexpr uint32_t FLY_GUILD_CHAMPION_2       = 451; // Flying guild champion 2

// Total number of animation IDs (0–451 inclusive)
constexpr uint32_t ANIM_COUNT                 = 452;

/// Return the symbolic name for an animation ID (e.g. 0 → "STAND").
/// Returns "UNKNOWN" for IDs outside the known range.
const char* nameFromId(uint32_t id);

/// Validate animation_ids.hpp constants against AnimationData.dbc.
/// Logs warnings for IDs present in DBC but missing from constants, and vice versa.
void validateAgainstDBC(const std::shared_ptr<wowee::pipeline::DBCFile>& dbc);

} // namespace anim
} // namespace rendering
} // namespace wowee
