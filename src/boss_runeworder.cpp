/*
 * Author:
 * 2020-2021 Trickerer <https://github.com/trickerer>
 */

/* ScriptData
SDName: boss_runeworder
Version: 1.0.11
%Complete: 100
Category: Custom
Comments:
  Using
    Spell.dbc 500000-500250
    SpellIcon.dbc 500000-500032
    creature_template 500000-5000002
    creature_equip_template 500000
    creature_text 500000
EndScriptData */

#include "Chat.h"
#include "CreatureAIImpl.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "ScriptedCreature.h"
#include "ScriptMgr.h"
#include "Spell.h"
#include "SpellAuraEffects.h"
#include "SpellMgr.h"
#include "SpellScript.h"
#include "SpellScriptLoader.h"
#include "WorldSession.h"

//AzerothCore support
#ifdef AC_PLATFORM
 #define UNIT_FLAG_UNINTERACTIBLE UNIT_FLAG_NOT_SELECTABLE
 #define TC_LOG_ERROR LOG_ERROR
 #define Milliseconds uint32
 #define SelectTargetRandom SELECT_TARGET_RANDOM
 #define GetThreat me->GetThreatMgr().GetThreat
 #define ModifyThreatByPercent me->GetThreatMgr().ModifyThreatByPercent
 #define AddThreat me->AddThreat
 #define ResetThreatList me->GetThreatMgr().ClearAllThreat
#else
 #define SelectTargetRandom SelectTargetMethod::Random
#endif

//debug
#define __RUNEWORDER_DEBUG 0

#if __RUNEWORDER_DEBUG
# define LOG(filterType__, ...) TC_LOG_ERROR(filterType__, __VA_ARGS__)
#else
# define LOG(...) (void)0
#endif

constexpr uint32 POINT_PUT_DELAY = 2100;
constexpr size_t MAX_RUNE_POINTS = 12;

constexpr uint32 CAST_TIME_SUMMON_CARVER = 1000;
constexpr uint32 CAST_TIME_ACTIVATE_RUNE = 2000;
constexpr uint32 CAST_TIME_ACTIVATE_RUNEWORD = 6000;

constexpr uint32 INCINERATE_CHECK_TIMER = 3000;
constexpr uint32 SPEED_UPDATE_TIMER = 2000;

#define FRENZY_PRED(a,b) ((_myphase == PHASE_FRENZY) ? (b) : (a))

constexpr int32 SKILL_LIFE_STEAL = int32(SKILL_GENERIC_DND);

constexpr float DIST_THRESHOLD = 0.25f;
constexpr uint32 UNMATCH_THRESHOLD = 1;

constexpr uint32 WEIGHT_RUNE_LOW = 2;
constexpr uint32 WEIGHT_RUNE_MID = 3;
constexpr uint32 WEIGHT_RUNE_HI = 4;

constexpr size_t MIN_RUNE_PATTERN_LENGTH = 5;
constexpr size_t MAX_RUNE_PATTERN_LENGTH = 9;

constexpr size_t MIN_RUNEWORD_LENGTH = 2;
constexpr size_t MAX_RUNEWORD_LENGTH = 3;

constexpr uint32 FRENZY_TIMER = 3 * MINUTE * IN_MILLISECONDS;
constexpr float FRENZY_HP_THRESHOLD = 20.f;

enum CarverSpells
{
    SPELL_FLAMES                            = 500094
};

enum CarverEvents
{
    EVENT_FLAMES                = 1
};

enum RuneworderTexts
{
    SAY_AGGRO       = 0,
    SAY_KILL        = 1,
    SAY_DEATH       = 2,
    SAY_RUNEWORD    = 3,
    SAY_FRENZY      = 4,
    SAY_RUNE_FAIL   = 5
};

enum RuneworderSpells : uint32
{
    SPELL_COSMETIC_SCALE                    = 500096, //scale aura, visual only
    SPELL_COSMETIC_FLAMES                   = 500097, //dummy aura, visual only
    SPELL_FRENZY                            = 500098, //spelldam/attspeed/scale 75%/150%/25%
    SPELL_RUNIC_WITHDRAWAL                  = 500000, //enrage physdam/attspeed 25%/15%, stacks
    SPELL_LAUNCH_RUNE_CARVER                = 500001,
    SPELL_ACTIVATE_RUNE                     = 500002,
    SPELL_RUNEWORD                          = 500003,
    SPELL_VISUAL_RUNE_ACTIVATION            = 500004,
    SPELL_INCINERATE                        = 500005,
    SPELL_RAIN_OF_FIRE                      = 500095,
    SPELL_VISUAL_RUNE_CHANNEL               = 500006,

    //runes
    //All rune base spells should hit caster, add dummy eff if necessary
    SPELL_EL_SELF                           = 500007,//chance to hit +10%, +20% armor
    SPELL_ELD_SELF                          = 500008,//block +20%
    SPELL_TIR_SELF                          = 500009,//dummy
    SPELL_NEF_SELF                          = 500010,//ranged dam taken -15%, (ctc knockback + stun 25% notstackable)
    SPELL_ETH_SELF                          = 500011,//+20% armor pen
    SPELL_ITH_SELF                          = 500012,//+500 damage, +100 spell dam
    SPELL_TAL_SELF                          = 500013,//natu res +100
    SPELL_RAL_SELF                          = 500014,//fire res +100
    SPELL_ORT_SELF                          = 500015,//arca res +100
    SPELL_THUL_SELF                         = 500016,//cold res +100
    SPELL_AMN_SELF                          = 500017,//heal 2% max hp on hit, thorns 125
    SPELL_SOL_SELF                          = 500018,//+750 damage, +150 spell dam
    SPELL_SHAEL_SELF                        = 500019,//attspeed +30%
    SPELL_DOL_SELF                          = 500020,//regen 1% hp/5 sec, (ctc fear when hit / being hit 20% notstackable)
    SPELL_HEL_SELF                          = 500021,//allstats +20%
    SPELL_IO_SELF                           = 500022,//maxhp +30%
    SPELL_LUM_SELF                          = 500023,//(maxmana +20%) dummy
    SPELL_KO_SELF                           = 500024,//dodge +10%
    SPELL_FAL_SELF                          = 500025,//parry +10%
    SPELL_LEM_SELF                          = 500026,//dummy
    SPELL_PUL_SELF                          = 500027,//expertise +8
    SPELL_UM_SELF                           = 500028,//all res +150
    SPELL_MAL_SELF                          = 500029,//spell dam taken -15%
    SPELL_IST_SELF                          = 500030,//resist magic/curse/disease 20%
    SPELL_GUL_SELF                          = 500031,//+50% AP
    SPELL_VEX_SELF                          = 500032,//20% reflect fire/shadow
    SPELL_OHM_SELF                          = 500033,//20% reflect frost/nature
    SPELL_LO_SELF                           = 500034,//20% reflect arcane/holy, crit +20%
    SPELL_SUR_SELF                          = 500035,//(ctc blind when hit 10% notstackable)
    SPELL_BER_SELF                          = 500036,//phys damage taken -20%, 35% ctc cast hp damage 12.5%
    SPELL_JAH_SELF                          = 500037,//maxhp +50%
    SPELL_CHAM_SELF                         = 500038,//ctc AOE Freeze when damage done
    SPELL_ZOD_SELF                          = 500039,//damage taken -500, unkillable

    SPELL_EL_TARGETS                        = 500207,//e chance to hit -10%
    SPELL_ELD_TARGETS                       = 500208,//e block -20%
    SPELL_TIR_TARGETS                       = 500209,//e spell cost +10%, periodic leech 100/5s
    //SPELL_NEF_TARGETS                       = 500210,//
    SPELL_ETH_TARGETS                       = 500211,//e mana regen -20% (from spirit)
    SPELL_ITH_TARGETS                       = 500212,//e damage -5%
    SPELL_TAL_TARGETS                       = 500213,//e periodic natu damage
    SPELL_RAL_TARGETS                       = 500214,//e periodic fire damage
    SPELL_ORT_TARGETS                       = 500215,//e periodic arca damage
    SPELL_THUL_TARGETS                      = 500216,//e periodic cold damage
    //SPELL_AMN_TARGETS                       = 500217,//
    SPELL_SOL_TARGETS                       = 500218,//e damage -10%
    SPELL_SHAEL_TARGETS                     = 500219,//e crit chance taken +5%
    //SPELL_DOL_TARGETS                       = 500220,//
    SPELL_HEL_TARGETS                       = 500221,//e allstats -10%
    SPELL_IO_TARGETS                        = 500222,//e maxhp -5%
    SPELL_LUM_TARGETS                       = 500223,//e maxmana -8%
    SPELL_KO_TARGETS                        = 500224,//e dodge -10%
    SPELL_FAL_TARGETS                       = 500225,//e parry -10%
    SPELL_LEM_TARGETS                       = 500226,//e expertise -8
    SPELL_PUL_TARGETS                       = 500227,//e defense -50
    SPELL_UM_TARGETS                        = 500228,//e all res -150, bleed
    SPELL_MAL_TARGETS                       = 500229,//e spell dam taken +10%, healing taken -15%
    //SPELL_IST_TARGETS                       = 500230,//
    SPELL_GUL_TARGETS                       = 500231,//e -20% AP, -20% ranged AP
    SPELL_VEX_TARGETS                       = 500232,//e periodic leech 200/5s
    SPELL_OHM_TARGETS                       = 500233,//e damage -15%
    SPELL_LO_TARGETS                        = 500234,//e crit -10%
    SPELL_SUR_TARGETS                       = 500235,//e maxmana -15%
    //SPELL_BER_TARGETS                       = 500236,//
    SPELL_JAH_TARGETS                       = 500237,//e maxhp -10%, defense -100
    //SPELL_CHAM_TARGETS                      = 500238,//
    SPELL_ZOD_TARGETS                       = 500239,//e armor -10000, crit dam taken +50%, ct die on hit

    //triggered/additional spells
    //SPELL_NEF_TRIGGERED                     = 500050,//knockback, triggered (not casted in script)
    //SPELL_AMN_TRIGGERED                     = 500051,//heal 1% max hp, triggered (not casted in script)
    //SPELL_DOL_TRIGGERED                     = 500052,//fear, triggered (not casted in script)
    //SPELL_SUR_TRIGGERED                     = 500053,//blind, triggered (not casted in script)
    SPELL_CRUSHING_BLOW_TRIGGERED           = 500054,//crushing blow, triggered, SCRIPTED
    //SPELL_CHAM_TRIGGERED                    = 500055,//aoe freeze, triggered, (not casted in script)
    //SPELL_ZOD_TRIGGERED                     = 500056,//instakill, triggered, (not casted in script)
    //runewords
    SPELL_MALICE_TRIGGERED                  = 500057,//self periodic pct damage, hidden, (casted in script)
    SPELL_LIFESTEAL                         = 500058,//life steal X%, (casted in script)
    SPELL_THORNS_AURA                       = 500059,//damage pct reflect, SCRIPTED
    SPELL_THORNS_AURA_DAMAGE                = 500060,//thorns damage blank, (casted in script)
    //SPELL_CHILLED_TRIGGERED                 = 500061,//all speeds -15%, rhyme triggered, (not casted in script)
    //SPELL_HOWL_TRIGGERED                    = 500062,//AoE fear 6 sec, myth triggered, (not casted in script)
    //SPELL_BONE_ARMOR_TRIGGERED              = 500063,//absorb 200k physical dam, white triggered, (not casted in script)
    //SPELL_DECREPIFY_TRIGGERED               = 500064,//slow all 50%, armor -50%, lawbringer triggered, (not casted in script)
    //SPELL_FLAME_TRIGGERED                   = 500065,//fire dam ~3k, enlightenment triggered, (not casted in script)
    //SPELL_BLAZE_PERIODIC_TRIGGERED          = 500066,//small AoE med fire dam, enlightenment triggered, (not casted in script)
    SPELL_STATIC_FIELD_TRIGGERED            = 500067,//static field, triggered, SCRIPTED
    //SPELL_BONE_ARMOR_TRIGGERED              = 500069,//absorb 200k spell dam, rain triggered, (not casted in script)
    //SPELL_VENOM_TRIGGERED                   = 500070,//nat dam ~3k, venom triggered, (not casted in script)
    //SPELL_POISON_CLOUD_PERIODIC_TRIGGERED   = 500071,//small AoE med nat dam, venom triggered, (not casted in script)
    //SPELL_IMPENDING_DELIRIUM_PERIODIC_TRIGGERED = 500072,//AoE sha dam, delirium triggered, (not casted in script)
    //SPELL_DELIRIUM_PERIODIC_TRIGGERED       = 500073,//8 sec stun, delirium periodic triggered, (not casted in script)
    //SPELL_DELIRIUM_PERIODIC_TRIGGERED2      = 500074,//dummy, delirium periodic triggered, (not casted in script)
    //SPELL_WHIRLWIND_PERIODIC_TRIGGERED      = 500075,//trigger, chaos triggered, (not casted in script)
    //SPELL_WHIRLWIND_TRIGGERED               = 500076,//125% weap dam AoE, whirlwind periodic triggered, (not casted in script)
    //SPELL_FLAME_BREATH_PERIODIC_TRIGGERED   = 500077,//med fire dam AoE big cone, incr fire dam taken, dragon triggered, (not casted in script)
    //SPELL_SLEEP_PERIODIC_TRIGGERED          = 500078,//sleep AoE 6 targets, dragon triggered, (not casted in script)
    SPELL_TELEPORT                          = 500079,//tp to random spot clearing threat, enigma triggered, (casted in script)
    SPELL_TELEPORT_ROOT                     = 500080,//root 1.5s, (casted in script)
    SPELL_PERIODIC_TELEPORT_DUMMY           = 500081,//dummy, (checked in script)

    //runewords
    //All runeword spells should hit caster, add dummy eff if necessary
    SPELL_STEEL                             = 500100,//25% haste, phys damage +20% +200
    SPELL_NADIR                             = 500101,//-35% chance to get hit
    SPELL_MALICE                            = 500102,//+33% phys damage, ignore 5k armor
    SPELL_STEALTH                           = 500103,//+25% chance to hit, mag damage taken -450
    SPELL_LEAF                              = 500104,//fire dam +50%, frost dam taken -50%, armor +7500
    SPELL_ZEPHYR                            = 500105,//25% haste, phys damage +33%
    SPELL_ANCIENTS_PLEDGE                   = 500106,//armor +50%, nat,fir,arc res +100, hol,fro,sha res +300
    SPELL_STRENGTH                          = 500107,//phys dam +35%, life steal 70%, 25% crushing blow
    SPELL_EDGE                              = 500108,//35% haste, life steal 70%, thorns 10%
    SPELL_KINGS_GRACE                       = 500109,//phys dam +100%, life steal 70%, 15% cth
    SPELL_RADIANCE                          = 500110,//armor +50%, mag damage taken -300, max hp +10%
    SPELL_LORE                              = 500111,//all dam +50%
    SPELL_RHYME                             = 500112,//all res +250, parry +20%, attackers -15% attack/cast speeds
    SPELL_PEACE                             = 500113,//crit +25%, all dam +30%, ranged dam taken -33%
    SPELL_MYTH                              = 500114,//all dam +30%, regen 1% hp/4 sec, ctc AoE fear
    SPELL_BLACK                             = 500115,//phys dam +120%, att speed +15%, 40% crushing blow
    SPELL_WHITE                             = 500116,//40% haste, mag damage taken -400, periodic Bone Armor 500063
    SPELL_SMOKE                             = 500117,//armor +75%, all res +500, ranged dam taken -60%
    SPELL_SPLENDOR                          = 500118,//armor +100%, all dam +20%
    SPELL_MELODY                            = 500119,//armor +50%, all dam +45%, mag damage taken -700
    SPELL_LIONHEART                         = 500120,//all dam +25%, all dam taken -25%, health +25%
    SPELL_TREACHERY                         = 500121,//45% haste, all dam +30%, ct be hit by spells -75% (186)
    SPELL_WEALTH                            = 500122,//100% crit
    SPELL_LAWBRINGER                        = 500123,//armor pen +50%, ranged dam taken -50%, 20% ctc AoE Decrepify
    SPELL_ENLIGHTENMENT                     = 500124,//all dam +30%, fire dam on attack, Blaze
    SPELL_CRESCENT_MOON                     = 500125,//phys dam +200%, mag damage taken -1000, ctc Static Field
    SPELL_DURESS                            = 500126,//armor +175%, att speed +30%, 15% crushing blow
    SPELL_GLOOM                             = 500127,//armor +230%, all res +300, Dim Vision aura
    SPELL_PRUDENCE                          = 500128,//armor +150%, all res +300, mag damage taken -1500
    SPELL_RAIN                              = 500129,//all dam +30%, periodic Cyclone Armor 500069
    SPELL_VENOM                             = 500130,//nat dam on attack, periodic Poison Cloud 500071
    //SPELL_SANCTUARY                         = 500131,//armor +250%, all res +700, parry +50%
    SPELL_DELIRIUM                          = 500132,//all dam +30%, periodic Impending Delirium 500072
    SPELL_PRINCIPLE                         = 500133,//all dam +30%, max hp +20%
    SPELL_CHAOS                             = 500134,//35% haste, phys dam +225%, periodic Whirlwind  500075
    SPELL_WIND                              = 500135,//55% haste, phys dam +200%
    SPELL_DRAGON                            = 500136,//all dam taken -50%, periodic Flame Breath 500077
    SPELL_DREAM                             = 500137,//dodge +33%, periodic AoE Sleep 500078
    SPELL_FURY                              = 500138,//60% haste, crit +33%, expertise +60
    SPELL_ENIGMA                            = 500139 //all dam +75%, all dam taken -35%, periodic Teleport 500079
};

enum RuneworderPhases
{
    PHASE_NONE                  = 0,
    PHASE_FRENZY                = 1
};

enum RuneworderEvents
{
    EVENT_CARVER                = 1,
    EVENT_POINT_PUT             = 2,
    EVENT_RUNE_ASSEMBLE         = 3,
    EVENT_POINTS_UNSUMMON       = 4,
    EVENT_RUNEWORD_ASSEMBLE     = 5,
    EVENT_FRENZY                = 6,
    EVENT_RAIN_OF_FIRE          = 7,
    EVENT_INCINERATE            = 8,
    //runewords
    EVENT_ENIGMA_TELEPORT       = 9
};

enum NPCs
{
    NPC_BOSS_RUNEWORDER         = 500000,
    NPC_RUNE_CARVER_STALKER     = 500001,
    NPC_RUNE_POINT_BUNNY        = 500002
};

enum StrokeTypes : uint8
{
    NO_STROKE                   = 0,
    LINE                        = 1, //171-180
    LINE_REV                    = 2, //0-15
    CURVE_L                     = 3, //141-170
    CURVE_M /*sharper*/         = 4, //121-140
    CURVE_H /*even sharper*/    = 5, //101-120
    TURN_CUBIC                  = 6, //81-100
    TURN_SHARP                  = 7, //16-80
    TURN_REVERSE                = 8  //marker
};

enum StrokeTypeDefs : uint32
{
    STDEF_CAN_BE_EMPTY          = (1 << (NO_STROKE)), //should not come 2+ in a row, only with lines
    STDEF_LINE                  = (1 << (LINE)),
    STDEF_LINE_REV              = (1 << (LINE_REV)),
    STDEF_CURVE_L               = (1 << (CURVE_L)),
    STDEF_CURVE_M               = (1 << (CURVE_M)),
    STDEF_CURVE_H               = (1 << (CURVE_H)),
    STDEF_TURN_CUBIC            = (1 << (TURN_CUBIC)),
    STDEF_TURN_SHARP            = (1 << (TURN_SHARP)),
    STDEF_REV                   = (1 << (TURN_REVERSE)), //should not combine with line-based

    //DO NOT add any new combinations
    ST_LINE                     = (STDEF_LINE),
    ST_LINE_OR_NOTHING          = (STDEF_LINE | STDEF_CAN_BE_EMPTY),

    ST_LINE_REV /*YES*/         = (STDEF_LINE_REV),

    ST_LINE_OR_CURVE_L          = (STDEF_LINE | STDEF_CURVE_L),
    ST_LINE_OR_CURVE_M          = (STDEF_LINE | STDEF_CURVE_M),
    ST_LINE_OR_CURVE_LM         = (STDEF_LINE | STDEF_CURVE_L | STDEF_CURVE_M),

    ST_CURVE_L                  = (STDEF_CURVE_L),
    ST_CURVE_L_R                = (STDEF_CURVE_L | STDEF_REV),
    ST_CURVE_LM                 = (STDEF_CURVE_L | STDEF_CURVE_M),
    ST_CURVE_LM_R               = (STDEF_CURVE_L | STDEF_CURVE_M | STDEF_REV),
    ST_CURVE_M                  = (STDEF_CURVE_M),
    ST_CURVE_M_R                = (STDEF_CURVE_M | STDEF_REV),
    ST_CURVE_MH                 = (STDEF_CURVE_M | STDEF_CURVE_H),
    ST_CURVE_MH_R               = (STDEF_CURVE_M | STDEF_CURVE_H | STDEF_REV),
    ST_CURVE_H                  = (STDEF_CURVE_H),
    ST_CURVE_H_R                = (STDEF_CURVE_H | STDEF_REV),
    ST_CUBIC                    = (STDEF_TURN_CUBIC),
    ST_CUBIC_R                  = (STDEF_TURN_CUBIC | STDEF_REV),
    ST_CUBIC_OR_CURVE_M         = (STDEF_TURN_CUBIC | STDEF_CURVE_M),
    ST_CUBIC_OR_CURVE_M_R       = (STDEF_TURN_CUBIC | STDEF_CURVE_M | STDEF_REV),
    ST_CUBIC_OR_CURVE_H         = (STDEF_TURN_CUBIC | STDEF_CURVE_H),
    ST_CUBIC_OR_CURVE_H_R       = (STDEF_TURN_CUBIC | STDEF_CURVE_H | STDEF_REV),
    ST_CUBIC_OR_SHARP           = (STDEF_TURN_CUBIC | STDEF_TURN_SHARP),
    ST_CUBIC_OR_SHARP_R         = (STDEF_TURN_CUBIC | STDEF_TURN_SHARP | STDEF_REV),
    ST_SHARP                    = (STDEF_TURN_SHARP),
    ST_SHARP_R                  = (STDEF_TURN_SHARP | STDEF_REV),

    ST_CURVE_LMH                = (ST_CURVE_L | ST_CURVE_M | ST_CURVE_H),
    ST_CURVE_LMH_R              = (ST_CURVE_LMH | STDEF_REV),
    ST_CURVE_CMH                = (STDEF_TURN_CUBIC | STDEF_CURVE_M | STDEF_CURVE_H),
    ST_CURVE_CMH_R              = (ST_CURVE_CMH | STDEF_REV),
    ST_CURVE_CLMH               = (ST_CURVE_L | ST_CURVE_M | ST_CURVE_H | ST_CUBIC),
    ST_CURVE_CLMH_R             = (ST_CURVE_CLMH | STDEF_REV),
    ST_CURVE_ANY                = (ST_CURVE_L | ST_CURVE_M | ST_CURVE_H | ST_CUBIC | ST_SHARP),
    ST_CURVE_ANY_R              = (ST_CURVE_ANY | STDEF_REV),

    ST_LINETYPES                = (ST_LINE | ST_LINE_REV)
};

enum RuneTypes : uint32
{
    RUNE_EL1                    = 0, // do not edit, used as index
    RUNE_EL2,
    RUNE_ELD1,
    RUNE_ELD2,
    RUNE_TIR1,
    RUNE_TIR2,
    RUNE_NEF,
    RUNE_ETH1,
    RUNE_ETH2,
    RUNE_ITH1,
    RUNE_ITH2,
    RUNE_TAL1,
    RUNE_TAL2,
    RUNE_TAL3,
    RUNE_RAL,
    RUNE_ORT1,
    RUNE_ORT2,
    RUNE_ORT3,
    RUNE_THUL,
    RUNE_AMN,
    RUNE_SOL,
    RUNE_SHAEL1,
    RUNE_SHAEL2,
    RUNE_DOL1,
    RUNE_DOL2,
    RUNE_HEL1,
    RUNE_HEL2,
    RUNE_IO1,
    RUNE_IO2,
    RUNE_IO3,
    RUNE_LUM,
    RUNE_KO,
    RUNE_FAL1,
    RUNE_FAL2,
    RUNE_FAL3,
    RUNE_FAL4,
    RUNE_FAL5,
    RUNE_FAL6,
    RUNE_LEM1,
    RUNE_LEM2,
    RUNE_LEM3,
    RUNE_LEM4,
    RUNE_PUL,
    RUNE_UM1,
    RUNE_UM2,
    RUNE_UM3,
    RUNE_MAL1,
    RUNE_MAL2,
    RUNE_IST,
    RUNE_GUL1,
    RUNE_GUL2,
    RUNE_GUL3,
    RUNE_VEX1,
    RUNE_VEX2,
    RUNE_VEX3,
    RUNE_VEX4,
    RUNE_VEX5,
    RUNE_VEX6,
    RUNE_OHM,
    RUNE_LO1,
    RUNE_LO2,
    RUNE_LO3,
    RUNE_LO4,
    RUNE_SUR1,
    RUNE_SUR2,
    RUNE_BER,
    RUNE_JAH1,
    RUNE_JAH2,
    RUNE_JAH3,
    RUNE_JAH4,
    RUNE_JAH5,
    RUNE_CHAM1,
    RUNE_CHAM2,
    RUNE_CHAM3,
    RUNE_CHAM4,
    RUNE_CHAM5,
    RUNE_CHAM6,
    RUNE_CHAM7,
    RUNE_CHAM8,
    RUNE_CHAM9,
    RUNE_ZOD1,
    RUNE_ZOD2,

    MAX_RUNE_TYPES,

    RUNE_INVALID                = 255
};

enum RunewordTypes : uint32
{
    //there are 83 runewords total, not gonna do them all, only 2 and 3 runes (40 total)
    //no repeating runes allowed
    RUNEWORD_STEEL              = 0, // tirel, do not edit, used as index
    RUNEWORD_NADIR, //neftir
    RUNEWORD_MALICE, //itheleth
    RUNEWORD_STEALTH, //taleth
    RUNEWORD_LEAF, //tirral
    RUNEWORD_ZEPHYR, //orteth
    RUNEWORD_ANCIENTS_PLEDGE, //ralorttal
    RUNEWORD_STRENGTH, //amntir
    RUNEWORD_EDGE, //tirtalamn
    RUNEWORD_KINGS_GRACE, //amnralthul
    RUNEWORD_RADIANCE, //nefsolith
    RUNEWORD_LORE, //ortsol
    RUNEWORD_RHYME, //shaeleth
    RUNEWORD_PEACE, //shaelthulamn
    RUNEWORD_MYTH, //helamnnef
    RUNEWORD_BLACK, //thulionef
    RUNEWORD_WHITE, //dolio
    RUNEWORD_SMOKE, //neflum
    RUNEWORD_SPLENDOR, //ethlum
    RUNEWORD_MELODY, //shaelkonef
    RUNEWORD_LIONHEART, //hellumfal
    RUNEWORD_TREACHERY, //shaelthullem
    RUNEWORD_WEALTH, //lemkotir
    RUNEWORD_LAWBRINGER, //amnlemko
    RUNEWORD_ENLIGHTENMENT, //pulralsol
    RUNEWORD_CRESCENT_MOON, //shaelumtir
    RUNEWORD_DURESS, //shaelumthul
    RUNEWORD_GLOOM, //falumpul
    RUNEWORD_PRUDENCE, //maltir
    RUNEWORD_RAIN, //ortmalith
    RUNEWORD_VENOM, //taldolmal
    //RUNEWORD_SANCTUARY, //kokomal
    RUNEWORD_DELIRIUM, //lemistio
    RUNEWORD_PRINCIPLE, //ralguleld
    RUNEWORD_CHAOS, //falohmum
    RUNEWORD_WIND, //surel
    RUNEWORD_DRAGON, //surlosol
    RUNEWORD_DREAM, //iojahpul
    RUNEWORD_FURY, //jahguleth
    RUNEWORD_ENIGMA, //jahithber

    MAX_RUNEWORD_TYPES,

    RUNEWORD_INVALID            = 255
};


struct Stroke
{
public:
    explicit constexpr Stroke(uint8 type, bool reverse) : type(type), reverse(reverse)
    {
        if ((type == LINE || type == LINE_REV) && reverse)
            throw -1;
    }
    uint8 type;
    bool reverse;
};

typedef std::vector<Stroke> Strokes;

template<size_t N>
using StrokeTypeDefsArray = std::array<StrokeTypeDefs, N>;

struct RunePattern
{
private:
    template<size_t N>
    inline static constexpr uint32 get_min_size(StrokeTypeDefsArray<N> const& m_strokes)
    {
        uint32 minsize = 0;
        for (size_t i = 0; i < N; ++i)
            if (!(m_strokes[i] & STDEF_CAN_BE_EMPTY))
                ++minsize;
        return minsize;
    }

public:
    template<size_t N>
    explicit constexpr RunePattern(const uint8 m_type, StrokeTypeDefsArray<N> const& m_strokes) :
        type(m_type), size(N), strokeSequence(m_strokes.data()), minSize(get_min_size(m_strokes))
    {
        if (size < MIN_RUNE_PATTERN_LENGTH || size > MAX_RUNE_PATTERN_LENGTH)
            throw -1;

        if (strokeSequence[0] & STDEF_REV)
            throw -2;

        if (strokeSequence[0] & STDEF_CAN_BE_EMPTY)
            throw -3;
    }

    bool Matches(Strokes const& compSeq) const;

    template<size_t N>
    static constexpr bool Matches(std::array<Stroke, N> const& compSeq, RunePattern const& pattern);

//private:
    const uint8 type;
    const uint8 size;
    const uint8 minSize;
    StrokeTypeDefs const* strokeSequence;
};

typedef std::vector<RuneworderSpells> RuneSpellVec;

template<size_t N>
using RuneSpellsArray = std::array<RuneworderSpells, N>;

struct RunewordPattern
{
public:
    template<size_t N>
    explicit constexpr RunewordPattern(uint32 m_type, RuneSpellsArray<N> const& m_runes) :
    type(m_type), size(N), runeSpellList(m_runes.data())
    {
        if (size < MIN_RUNEWORD_LENGTH || size > MAX_RUNEWORD_LENGTH)
            throw -4;
    }

    bool Contains(RuneSpellVec const& compSeq) const;

    template<size_t M, size_t N>
    static constexpr bool Contains(std::array<RuneworderSpells, N> const& compSeq, RunewordPattern const& pattern);

    uint8 GetSize() const { return size; }
    RuneworderSpells const* GetRuneSpellList() const { return runeSpellList; }

//private:
    const uint32 type;
    const uint32 size;
    RuneworderSpells const* runeSpellList;
};

//EL
constexpr uint32 RUNE_SIZE_EL1 = 7;
constexpr uint32 RUNE_SIZE_EL2 = 5;
constexpr StrokeTypeDefsArray<RUNE_SIZE_EL1> RuneStrokesEL1 =
{
    ST_CURVE_MH, ST_CURVE_LM, ST_CUBIC_OR_CURVE_H_R, ST_SHARP_R, ST_LINE, ST_CURVE_LM, ST_CURVE_LM
};
constexpr StrokeTypeDefsArray<RUNE_SIZE_EL2> RuneStrokesEL2 =
{
    ST_CURVE_MH, ST_CURVE_LMH_R, ST_SHARP_R, ST_LINE_OR_CURVE_LM, ST_CURVE_LMH
};
constexpr RunePattern Rune_EL1 = RunePattern(RUNE_EL1, RuneStrokesEL1);
constexpr RunePattern Rune_EL2 = RunePattern(RUNE_EL2, RuneStrokesEL2);
//ELD
constexpr uint32 RUNE_SIZE_ELD1 = 8;
constexpr uint32 RUNE_SIZE_ELD2 = 5;
constexpr StrokeTypeDefsArray<RUNE_SIZE_ELD1> RuneStrokesELD1 =
{
    ST_CURVE_L, ST_LINE_REV, ST_LINE, ST_CURVE_LM, ST_CURVE_MH, ST_LINE_REV, ST_CURVE_LM, ST_SHARP
};
constexpr StrokeTypeDefsArray<RUNE_SIZE_ELD2> RuneStrokesELD2 =
{
    ST_CURVE_L, ST_LINE_REV, ST_LINE_OR_CURVE_L, ST_CURVE_MH, ST_CURVE_LM
};
constexpr RunePattern Rune_ELD1 = RunePattern(RUNE_ELD1, RuneStrokesELD1);
constexpr RunePattern Rune_ELD2 = RunePattern(RUNE_ELD2, RuneStrokesELD2);
//TIR
constexpr uint32 RUNE_SIZE_TIR1 = 9;
constexpr uint32 RUNE_SIZE_TIR2 = 7;
constexpr StrokeTypeDefsArray<RUNE_SIZE_TIR1> RuneStrokesTIR1 =
{
    ST_SHARP, ST_LINE, ST_SHARP_R, ST_LINE, ST_SHARP_R, ST_SHARP_R, ST_CURVE_LM, ST_CURVE_MH, ST_CURVE_L
};
constexpr StrokeTypeDefsArray<RUNE_SIZE_TIR2> RuneStrokesTIR2 =
{
    ST_SHARP, ST_LINE_OR_NOTHING, ST_SHARP_R, ST_LINE_OR_NOTHING, ST_SHARP_R, ST_SHARP, ST_LINE_OR_NOTHING
};
constexpr RunePattern Rune_TIR1 = RunePattern(RUNE_TIR1, RuneStrokesTIR1);
constexpr RunePattern Rune_TIR2 = RunePattern(RUNE_TIR2, RuneStrokesTIR2);
//NEF
constexpr uint32 RUNE_SIZE_NEF = 5;
constexpr StrokeTypeDefsArray<RUNE_SIZE_NEF> RuneStrokesNEF =
{
    ST_CUBIC_OR_SHARP, ST_LINE_OR_CURVE_L, ST_LINE_REV, ST_CUBIC_OR_SHARP, ST_CURVE_LMH_R
};
constexpr RunePattern Rune_NEF = RunePattern(RUNE_NEF, RuneStrokesNEF);
//ETH
constexpr uint32 RUNE_SIZE_ETH1 = 5;
constexpr uint32 RUNE_SIZE_ETH2 = 5;
constexpr StrokeTypeDefsArray<RUNE_SIZE_ETH1> RuneStrokesETH1 =
{
    ST_LINE_OR_CURVE_LM, ST_LINE_REV, ST_CUBIC_OR_SHARP_R, ST_CUBIC_OR_SHARP_R, ST_CUBIC_OR_SHARP_R
};
constexpr StrokeTypeDefsArray<RUNE_SIZE_ETH2> RuneStrokesETH2 =
{
    ST_LINE_OR_CURVE_LM, ST_LINE_REV, ST_CUBIC_OR_SHARP, ST_CUBIC_OR_SHARP_R, ST_CUBIC_OR_SHARP_R
};
constexpr RunePattern Rune_ETH1 = RunePattern(RUNE_ETH1, RuneStrokesETH1);
constexpr RunePattern Rune_ETH2 = RunePattern(RUNE_ETH2, RuneStrokesETH2);
//ITH
constexpr uint32 RUNE_SIZE_ITH1 = 7;
constexpr uint32 RUNE_SIZE_ITH2 = 7;
constexpr StrokeTypeDefsArray<RUNE_SIZE_ITH1> RuneStrokesITH1 =
{
    ST_CURVE_MH, ST_LINE_OR_NOTHING, ST_SHARP, ST_SHARP_R, ST_SHARP_R, ST_LINE_OR_NOTHING, ST_CURVE_MH
};
constexpr StrokeTypeDefsArray<RUNE_SIZE_ITH2> RuneStrokesITH2 =
{
    ST_CURVE_MH, ST_LINE_OR_NOTHING, ST_CUBIC_OR_SHARP, ST_CURVE_MH_R, ST_CUBIC_OR_SHARP_R, ST_LINE_OR_NOTHING, ST_CURVE_MH
};
constexpr RunePattern Rune_ITH1 = RunePattern(RUNE_ITH1, RuneStrokesITH1);
constexpr RunePattern Rune_ITH2 = RunePattern(RUNE_ITH2, RuneStrokesITH2);
//TAL
constexpr uint32 RUNE_SIZE_TAL = 5;
constexpr StrokeTypeDefsArray<RUNE_SIZE_TAL> RuneStrokesTAL1 =
{
    ST_SHARP, ST_LINE, ST_SHARP_R, ST_SHARP, ST_LINE
};
constexpr StrokeTypeDefsArray<RUNE_SIZE_TAL> RuneStrokesTAL2 =
{
    ST_LINE, ST_SHARP, ST_SHARP, ST_LINE, ST_SHARP_R
};
constexpr StrokeTypeDefsArray<RUNE_SIZE_TAL> RuneStrokesTAL3 =
{
    ST_SHARP, ST_SHARP, ST_LINE, ST_SHARP_R, ST_SHARP
};
constexpr RunePattern Rune_TAL1 = RunePattern(RUNE_TAL1, RuneStrokesTAL1);
constexpr RunePattern Rune_TAL2 = RunePattern(RUNE_TAL2, RuneStrokesTAL2);
constexpr RunePattern Rune_TAL3 = RunePattern(RUNE_TAL3, RuneStrokesTAL3);
//RAL
constexpr uint32 RUNE_SIZE_RAL = 5;
constexpr StrokeTypeDefsArray<RUNE_SIZE_RAL> RuneStrokesRAL =
{
    ST_CURVE_LMH, ST_CURVE_LMH_R, ST_CURVE_MH, ST_CURVE_LMH, ST_CURVE_MH
};
constexpr RunePattern Rune_RAL = RunePattern(RUNE_RAL, RuneStrokesRAL);
//ORT
constexpr uint32 RUNE_SIZE_ORT1 = 9;
constexpr uint32 RUNE_SIZE_ORT2 = 7;
constexpr uint32 RUNE_SIZE_ORT3 = 7;
constexpr StrokeTypeDefsArray<RUNE_SIZE_ORT1> RuneStrokesORT1 =
{
    ST_CURVE_LM, ST_CURVE_MH_R, ST_SHARP_R, ST_CUBIC_OR_CURVE_H_R, ST_SHARP_R,
    ST_CUBIC_OR_CURVE_H_R, ST_SHARP_R, ST_CURVE_MH_R, ST_CURVE_LM_R
};
constexpr StrokeTypeDefsArray<RUNE_SIZE_ORT2> RuneStrokesORT2 =
{
    ST_CURVE_LMH, ST_SHARP_R, ST_CUBIC_OR_CURVE_H_R, ST_SHARP_R, ST_CUBIC_OR_CURVE_H_R, ST_SHARP_R, ST_CURVE_LMH_R
};
constexpr StrokeTypeDefsArray<RUNE_SIZE_ORT3> RuneStrokesORT3 =
{
    ST_CURVE_LMH, ST_SHARP_R, ST_CURVE_LM_R, ST_LINE_OR_NOTHING, ST_CURVE_LM, ST_SHARP_R, ST_CURVE_LMH_R
};
constexpr RunePattern Rune_ORT1 = RunePattern(RUNE_ORT1, RuneStrokesORT1);
constexpr RunePattern Rune_ORT2 = RunePattern(RUNE_ORT2, RuneStrokesORT2);
constexpr RunePattern Rune_ORT3 = RunePattern(RUNE_ORT3, RuneStrokesORT3);
//THUL
constexpr uint32 RUNE_SIZE_THUL = 6;
constexpr StrokeTypeDefsArray<RUNE_SIZE_THUL> RuneStrokesTHUL =
{
    ST_LINE, ST_SHARP, ST_LINE_OR_NOTHING, ST_SHARP_R, ST_SHARP_R, ST_LINE_OR_NOTHING
};
constexpr RunePattern Rune_THUL = RunePattern(RUNE_THUL, RuneStrokesTHUL);
//AMN
constexpr uint32 RUNE_SIZE_AMN = 6;
constexpr StrokeTypeDefsArray<RUNE_SIZE_AMN> RuneStrokesAMN =
{
    ST_CUBIC_OR_CURVE_H, ST_CURVE_LMH, ST_CURVE_MH, ST_CUBIC_OR_SHARP, ST_CUBIC_OR_SHARP_R, ST_LINE_OR_CURVE_L
};
constexpr RunePattern Rune_AMN = RunePattern(RUNE_AMN, RuneStrokesAMN);
//SOL
constexpr uint32 RUNE_SIZE_SOL = 5;
constexpr StrokeTypeDefsArray<RUNE_SIZE_SOL> RuneStrokesSOL =
{
    ST_SHARP, ST_CUBIC_OR_SHARP_R, ST_CURVE_LMH, ST_CURVE_LMH, ST_CURVE_ANY_R
};
constexpr RunePattern Rune_SOL = RunePattern(RUNE_SOL, RuneStrokesSOL);
//SHAEL
constexpr uint32 RUNE_SIZE_SHAEL1 = 7;
constexpr uint32 RUNE_SIZE_SHAEL2 = 6;
constexpr StrokeTypeDefsArray<RUNE_SIZE_SHAEL1> RuneStrokesSHAEL1 =
{
    ST_CURVE_MH, ST_CUBIC_OR_SHARP, ST_LINE_REV, ST_LINE, ST_LINE_REV, ST_CUBIC_OR_SHARP_R, ST_CURVE_ANY_R
};
constexpr StrokeTypeDefsArray<RUNE_SIZE_SHAEL2> RuneStrokesSHAEL2 =
{
    ST_CUBIC_OR_CURVE_H, ST_CUBIC_OR_CURVE_H, ST_LINE_REV, ST_LINE, ST_LINE_REV, ST_CUBIC_OR_SHARP_R
};
constexpr RunePattern Rune_SHAEL1 = RunePattern(RUNE_SHAEL1, RuneStrokesSHAEL1);
constexpr RunePattern Rune_SHAEL2 = RunePattern(RUNE_SHAEL2, RuneStrokesSHAEL2);
//DOL
constexpr uint32 RUNE_SIZE_DOL1 = 6;
constexpr uint32 RUNE_SIZE_DOL2 = 6;
constexpr StrokeTypeDefsArray<RUNE_SIZE_DOL1> RuneStrokesDOL1 =
{
    ST_CURVE_LMH, ST_CURVE_LMH, ST_CURVE_LMH, ST_CUBIC_OR_SHARP, ST_LINE, ST_LINE
};
constexpr StrokeTypeDefsArray<RUNE_SIZE_DOL2> RuneStrokesDOL2 =
{
    ST_CUBIC_OR_SHARP, ST_CURVE_LMH, ST_CUBIC_OR_CURVE_H, ST_CURVE_MH, ST_LINE_OR_CURVE_L, ST_LINE_OR_CURVE_L
};
constexpr RunePattern Rune_DOL1 = RunePattern(RUNE_DOL1, RuneStrokesDOL1);
constexpr RunePattern Rune_DOL2 = RunePattern(RUNE_DOL2, RuneStrokesDOL2);
//HEL
constexpr uint32 RUNE_SIZE_HEL1 = 6;
constexpr uint32 RUNE_SIZE_HEL2 = 5;
constexpr StrokeTypeDefsArray<RUNE_SIZE_HEL1> RuneStrokesHEL1 =
{
    ST_CURVE_M, ST_CURVE_M, ST_CURVE_M, ST_CURVE_M, ST_CURVE_M, ST_CURVE_M
};
constexpr StrokeTypeDefsArray<RUNE_SIZE_HEL2> RuneStrokesHEL2 =
{
    ST_CURVE_H, ST_CURVE_H, ST_CURVE_H, ST_CURVE_H, ST_CURVE_H
};
constexpr RunePattern Rune_HEL1 = RunePattern(RUNE_HEL1, RuneStrokesHEL1);
constexpr RunePattern Rune_HEL2 = RunePattern(RUNE_HEL2, RuneStrokesHEL2);
//IO
constexpr uint32 RUNE_SIZE_IO1 = 9;
constexpr uint32 RUNE_SIZE_IO2 = 9;
constexpr uint32 RUNE_SIZE_IO3 = 8;
constexpr StrokeTypeDefsArray<RUNE_SIZE_IO1> RuneStrokesIO1 =
{
    ST_CUBIC_OR_CURVE_H, ST_CURVE_LM_R, ST_CUBIC_OR_CURVE_H_R, ST_LINE_REV, ST_CUBIC_OR_CURVE_H_R,
    ST_SHARP_R, ST_LINE_REV, ST_LINE, ST_CUBIC_OR_CURVE_H
};
constexpr StrokeTypeDefsArray<RUNE_SIZE_IO2> RuneStrokesIO2 =
{
    ST_CURVE_MH, ST_CUBIC_OR_SHARP, ST_CURVE_LMH_R, ST_CUBIC_OR_CURVE_H_R, ST_SHARP, ST_LINE_OR_CURVE_L,
    ST_SHARP_R, ST_LINE_OR_CURVE_L, ST_CUBIC_OR_CURVE_H_R
};
constexpr StrokeTypeDefsArray<RUNE_SIZE_IO3> RuneStrokesIO3 =
{
    ST_CURVE_ANY, ST_LINE_OR_CURVE_L, ST_CUBIC_OR_CURVE_H, ST_SHARP, ST_CURVE_LMH,
    ST_LINE_REV, ST_LINE_OR_CURVE_L, ST_CUBIC_OR_CURVE_H
};
constexpr RunePattern Rune_IO1 = RunePattern(RUNE_IO1, RuneStrokesIO1);
constexpr RunePattern Rune_IO2 = RunePattern(RUNE_IO2, RuneStrokesIO2);
constexpr RunePattern Rune_IO3 = RunePattern(RUNE_IO3, RuneStrokesIO3);
//LUM
constexpr uint32 RUNE_SIZE_LUM = 5;
constexpr StrokeTypeDefsArray<RUNE_SIZE_LUM> RuneStrokesLUM =
{
    ST_CURVE_MH, ST_CUBIC, ST_CURVE_LM, ST_CURVE_LMH, ST_CURVE_ANY_R
};
constexpr RunePattern Rune_LUM = RunePattern(RUNE_LUM, RuneStrokesLUM);
//KO
constexpr uint32 RUNE_SIZE_KO = 7;
constexpr StrokeTypeDefsArray<RUNE_SIZE_KO> RuneStrokesKO =
{
    ST_CUBIC_OR_SHARP, ST_CURVE_LMH, ST_LINE_REV, ST_LINE_OR_CURVE_L, ST_CUBIC_OR_SHARP, ST_CURVE_LMH, ST_CURVE_LMH_R
};
constexpr RunePattern Rune_KO = RunePattern(RUNE_KO, RuneStrokesKO);
//FAL
constexpr uint32 RUNE_SIZE_FAL1 = 6;
constexpr uint32 RUNE_SIZE_FAL2 = 6;
constexpr uint32 RUNE_SIZE_FAL3 = 6;
constexpr uint32 RUNE_SIZE_FAL4 = 6;
constexpr uint32 RUNE_SIZE_FAL5 = 6;
constexpr uint32 RUNE_SIZE_FAL6 = 6;
constexpr StrokeTypeDefsArray<RUNE_SIZE_FAL1> RuneStrokesFAL1 =
{
    ST_SHARP, ST_LINE, ST_CURVE_CMH_R, ST_CUBIC_OR_SHARP, ST_CURVE_CMH, ST_LINE
};
constexpr StrokeTypeDefsArray<RUNE_SIZE_FAL2> RuneStrokesFAL2 =
{
    ST_LINE, ST_CURVE_CMH, ST_CUBIC_OR_SHARP, ST_CURVE_CMH, ST_LINE, ST_SHARP_R
};
constexpr StrokeTypeDefsArray<RUNE_SIZE_FAL3> RuneStrokesFAL3 =
{
    ST_CURVE_CMH, ST_CUBIC_OR_SHARP, ST_CURVE_CMH, ST_LINE, ST_SHARP_R, ST_SHARP
};
constexpr StrokeTypeDefsArray<RUNE_SIZE_FAL4> RuneStrokesFAL4 =
{
    ST_CUBIC_OR_SHARP, ST_CURVE_CMH, ST_LINE, ST_SHARP_R, ST_SHARP, ST_LINE
};
constexpr StrokeTypeDefsArray<RUNE_SIZE_FAL5> RuneStrokesFAL5 =
{
    ST_CURVE_CMH, ST_LINE, ST_SHARP_R, ST_SHARP, ST_LINE, ST_CURVE_CMH_R
};
constexpr StrokeTypeDefsArray<RUNE_SIZE_FAL6> RuneStrokesFAL6 =
{
    ST_LINE, ST_SHARP, ST_SHARP, ST_LINE, ST_CURVE_CMH_R, ST_CUBIC_OR_SHARP
};
constexpr RunePattern Rune_FAL1 = RunePattern(RUNE_FAL1, RuneStrokesFAL1);
constexpr RunePattern Rune_FAL2 = RunePattern(RUNE_FAL2, RuneStrokesFAL2);
constexpr RunePattern Rune_FAL3 = RunePattern(RUNE_FAL3, RuneStrokesFAL3);
constexpr RunePattern Rune_FAL4 = RunePattern(RUNE_FAL4, RuneStrokesFAL4);
constexpr RunePattern Rune_FAL5 = RunePattern(RUNE_FAL5, RuneStrokesFAL5);
constexpr RunePattern Rune_FAL6 = RunePattern(RUNE_FAL6, RuneStrokesFAL6);
//LEM
constexpr uint32 RUNE_SIZE_LEM1 = 8;
constexpr uint32 RUNE_SIZE_LEM2 = 7;
constexpr uint32 RUNE_SIZE_LEM3 = 7;
constexpr uint32 RUNE_SIZE_LEM4 = 7;
constexpr StrokeTypeDefsArray<RUNE_SIZE_LEM1> RuneStrokesLEM1 =
{
    ST_CUBIC_OR_CURVE_H, ST_LINE_REV, ST_LINE, ST_LINE_REV, ST_CUBIC_R, ST_CUBIC_R, ST_LINE_REV, ST_LINE
};
constexpr StrokeTypeDefsArray<RUNE_SIZE_LEM2> RuneStrokesLEM2 =
{
    ST_CUBIC_OR_CURVE_H, ST_LINE_REV, ST_LINE_OR_CURVE_L, ST_LINE_REV, ST_LINE_OR_CURVE_L, ST_LINE_REV, ST_LINE_OR_CURVE_L
};
constexpr StrokeTypeDefsArray<RUNE_SIZE_LEM3> RuneStrokesLEM3 =
{
    ST_LINE_OR_CURVE_L, ST_LINE_REV, ST_LINE_OR_CURVE_L, ST_LINE_REV, ST_LINE_OR_CURVE_L, ST_LINE_REV, ST_CUBIC_OR_CURVE_H
};
constexpr StrokeTypeDefsArray<RUNE_SIZE_LEM4> RuneStrokesLEM4 =
{
    ST_LINE_OR_CURVE_L, ST_LINE_REV, ST_LINE_OR_CURVE_L, ST_LINE_REV, ST_LINE_OR_CURVE_L, ST_LINE_REV, ST_CUBIC_OR_CURVE_H_R
};
constexpr RunePattern Rune_LEM1 = RunePattern(RUNE_LEM1, RuneStrokesLEM1);
constexpr RunePattern Rune_LEM2 = RunePattern(RUNE_LEM2, RuneStrokesLEM2);
constexpr RunePattern Rune_LEM3 = RunePattern(RUNE_LEM3, RuneStrokesLEM3);
constexpr RunePattern Rune_LEM4 = RunePattern(RUNE_LEM4, RuneStrokesLEM4);
//PUL
constexpr uint32 RUNE_SIZE_PUL = 7;
constexpr StrokeTypeDefsArray<RUNE_SIZE_PUL> RuneStrokesPUL =
{
    ST_CURVE_LM, ST_CUBIC_OR_SHARP, ST_CURVE_CMH, ST_SHARP, ST_CURVE_CMH, ST_CUBIC_OR_SHARP, ST_CURVE_LM
};
constexpr RunePattern Rune_PUL = RunePattern(RUNE_PUL, RuneStrokesPUL);
//UM
constexpr uint32 RUNE_SIZE_UM1 = 6;
constexpr uint32 RUNE_SIZE_UM2 = 6;
constexpr uint32 RUNE_SIZE_UM3 = 6;
constexpr StrokeTypeDefsArray<RUNE_SIZE_UM1> RuneStrokesUM1 =
{
    ST_CURVE_CMH, ST_SHARP_R, ST_SHARP_R, ST_LINE_REV, ST_CURVE_LM, ST_SHARP
};
constexpr StrokeTypeDefsArray<RUNE_SIZE_UM2> RuneStrokesUM2 =
{
    ST_CURVE_CMH, ST_SHARP_R, ST_SHARP_R, ST_SHARP_R, ST_CURVE_LM_R, ST_SHARP
};
constexpr StrokeTypeDefsArray<RUNE_SIZE_UM3> RuneStrokesUM3 =
{
    ST_CURVE_CMH, ST_SHARP_R, ST_SHARP_R, ST_SHARP_R, ST_CURVE_LM_R, ST_SHARP //REUSE
};
constexpr RunePattern Rune_UM1 = RunePattern(RUNE_UM1, RuneStrokesUM1);
constexpr RunePattern Rune_UM2 = RunePattern(RUNE_UM2, RuneStrokesUM2);
constexpr RunePattern Rune_UM3 = RunePattern(RUNE_UM3, RuneStrokesUM3);
//MAL
constexpr uint32 RUNE_SIZE_MAL1 = 5;
constexpr uint32 RUNE_SIZE_MAL2 = 5;
constexpr StrokeTypeDefsArray<RUNE_SIZE_MAL1> RuneStrokesMAL1 =
{
    ST_SHARP, ST_SHARP_R, ST_LINE_OR_CURVE_L, ST_LINE_REV, ST_CURVE_LM
};
constexpr StrokeTypeDefsArray<RUNE_SIZE_MAL2> RuneStrokesMAL2 =
{
    ST_SHARP, ST_SHARP_R, ST_LINE_OR_CURVE_L, ST_SHARP_R, ST_CURVE_LM_R
};
constexpr RunePattern Rune_MAL1 = RunePattern(RUNE_MAL1, RuneStrokesMAL1);
constexpr RunePattern Rune_MAL2 = RunePattern(RUNE_MAL2, RuneStrokesMAL2);
//IST
constexpr uint32 RUNE_SIZE_IST = 5;
constexpr StrokeTypeDefsArray<RUNE_SIZE_IST> RuneStrokesIST =
{
    ST_CURVE_LM, ST_CURVE_LM, ST_CUBIC_OR_SHARP, ST_CURVE_CMH_R, ST_CURVE_CMH_R
};
constexpr RunePattern Rune_IST = RunePattern(RUNE_IST, RuneStrokesIST);
//GUL
constexpr uint32 RUNE_SIZE_GUL1 = 5;
constexpr uint32 RUNE_SIZE_GUL2 = 5;
constexpr uint32 RUNE_SIZE_GUL3 = 5;
constexpr StrokeTypeDefsArray<RUNE_SIZE_GUL1> RuneStrokesGUL1 =
{
    ST_CURVE_M, ST_CURVE_MH_R, ST_LINE_REV, ST_CURVE_LMH, ST_LINE_OR_NOTHING
};
constexpr StrokeTypeDefsArray<RUNE_SIZE_GUL2> RuneStrokesGUL2 =
{
    ST_CURVE_M, ST_CURVE_MH_R, ST_LINE_REV, ST_CURVE_LMH, ST_LINE_OR_NOTHING //REUSE
//    ST_CURVE_M, ST_CURVE_MH_R, ST_SHARP, ST_CURVE_LMH, ST_LINE_OR_NOTHING
};
constexpr StrokeTypeDefsArray<RUNE_SIZE_GUL3> RuneStrokesGUL3 =
{
    ST_CURVE_M, ST_CURVE_MH_R, ST_LINE_REV, ST_CURVE_LMH, ST_LINE_OR_NOTHING //REUSE
//    ST_CURVE_M, ST_CURVE_MH_R, ST_SHARP_R, ST_CURVE_LMH_R, ST_LINE_OR_NOTHING
};
constexpr RunePattern Rune_GUL1 = RunePattern(RUNE_GUL1, RuneStrokesGUL1);
constexpr RunePattern Rune_GUL2 = RunePattern(RUNE_GUL2, RuneStrokesGUL2);
constexpr RunePattern Rune_GUL3 = RunePattern(RUNE_GUL3, RuneStrokesGUL3);
//VEX
constexpr uint32 RUNE_SIZE_VEX1 = 7;
constexpr uint32 RUNE_SIZE_VEX2 = 7;
constexpr uint32 RUNE_SIZE_VEX3 = 7;
constexpr uint32 RUNE_SIZE_VEX4 = 5;
constexpr uint32 RUNE_SIZE_VEX5 = 5;
constexpr uint32 RUNE_SIZE_VEX6 = 5;
constexpr StrokeTypeDefsArray<RUNE_SIZE_VEX1> RuneStrokesVEX1 =
{
    ST_CUBIC_OR_CURVE_H, ST_LINE, ST_CUBIC_OR_CURVE_H, ST_LINE_REV, ST_CUBIC_OR_CURVE_H_R, ST_CUBIC_OR_CURVE_H, ST_SHARP
};
constexpr StrokeTypeDefsArray<RUNE_SIZE_VEX2> RuneStrokesVEX2 =
{
    ST_CUBIC_OR_CURVE_H, ST_LINE, ST_CUBIC_OR_CURVE_H, ST_LINE_REV, ST_CUBIC_OR_CURVE_H_R, ST_CUBIC_OR_CURVE_H, ST_SHARP_R
};
constexpr StrokeTypeDefsArray<RUNE_SIZE_VEX3> RuneStrokesVEX3 =
{
    ST_CUBIC_OR_CURVE_H, ST_LINE, ST_CUBIC_OR_CURVE_H, ST_LINE_REV, ST_CUBIC_OR_CURVE_H_R, ST_CUBIC_OR_CURVE_H, ST_LINE_REV
};
constexpr StrokeTypeDefsArray<RUNE_SIZE_VEX4> RuneStrokesVEX4 =
{
    ST_CUBIC_OR_CURVE_H, ST_CUBIC_OR_CURVE_H, ST_SHARP, ST_CUBIC_OR_CURVE_H, ST_CUBIC_OR_CURVE_H
};
constexpr StrokeTypeDefsArray<RUNE_SIZE_VEX5> RuneStrokesVEX5 =
{
    ST_CUBIC_OR_CURVE_H, ST_CUBIC_OR_CURVE_H, ST_SHARP_R, ST_CUBIC_OR_CURVE_H_R, ST_CUBIC_OR_CURVE_H
};
constexpr StrokeTypeDefsArray<RUNE_SIZE_VEX6> RuneStrokesVEX6 =
{
    ST_CUBIC_OR_CURVE_H, ST_CUBIC_OR_CURVE_H, ST_LINE_REV, ST_CUBIC_OR_CURVE_H, ST_CUBIC_OR_CURVE_H
};
constexpr RunePattern Rune_VEX1 = RunePattern(RUNE_VEX1, RuneStrokesVEX1);
constexpr RunePattern Rune_VEX2 = RunePattern(RUNE_VEX2, RuneStrokesVEX2);
constexpr RunePattern Rune_VEX3 = RunePattern(RUNE_VEX3, RuneStrokesVEX3);
constexpr RunePattern Rune_VEX4 = RunePattern(RUNE_VEX4, RuneStrokesVEX4);
constexpr RunePattern Rune_VEX5 = RunePattern(RUNE_VEX5, RuneStrokesVEX5);
constexpr RunePattern Rune_VEX6 = RunePattern(RUNE_VEX6, RuneStrokesVEX6);
//OHM
constexpr uint32 RUNE_SIZE_OHM = 5;
constexpr StrokeTypeDefsArray<RUNE_SIZE_OHM> RuneStrokesOHM =
{
    ST_SHARP, ST_CUBIC_OR_SHARP_R, ST_CUBIC_OR_SHARP_R, ST_CURVE_LM, ST_CURVE_MH
};
constexpr RunePattern Rune_OHM = RunePattern(RUNE_OHM, RuneStrokesOHM);
//LO
constexpr uint32 RUNE_SIZE_LO1 = 7;
constexpr uint32 RUNE_SIZE_LO2 = 7;
constexpr uint32 RUNE_SIZE_LO3 = 9;
constexpr uint32 RUNE_SIZE_LO4 = 9;
constexpr StrokeTypeDefsArray<RUNE_SIZE_LO1> RuneStrokesLO1 =
{
    ST_CUBIC_OR_SHARP, ST_LINE_REV, ST_CUBIC_OR_SHARP, ST_LINE_REV, ST_CUBIC_OR_SHARP, ST_LINE_REV, ST_CUBIC_OR_SHARP
};
constexpr StrokeTypeDefsArray<RUNE_SIZE_LO2> RuneStrokesLO2 =
{
    ST_CUBIC_OR_SHARP, ST_SHARP_R, ST_CUBIC_OR_SHARP_R, ST_SHARP_R, ST_CUBIC_OR_SHARP_R, ST_SHARP_R, ST_CUBIC_OR_SHARP_R
};
constexpr StrokeTypeDefsArray<RUNE_SIZE_LO3> RuneStrokesLO3 =
{
    ST_SHARP, ST_LINE, ST_SHARP, ST_LINE, ST_SHARP, ST_LINE, ST_SHARP, ST_LINE, ST_SHARP
};
constexpr StrokeTypeDefsArray<RUNE_SIZE_LO4> RuneStrokesLO4 =
{
    ST_LINE, ST_SHARP, ST_LINE, ST_SHARP, ST_LINE, ST_SHARP, ST_LINE, ST_SHARP, ST_LINE
};
constexpr RunePattern Rune_LO1 = RunePattern(RUNE_LO1, RuneStrokesLO1);
constexpr RunePattern Rune_LO2 = RunePattern(RUNE_LO2, RuneStrokesLO2);
constexpr RunePattern Rune_LO3 = RunePattern(RUNE_LO3, RuneStrokesLO3);
constexpr RunePattern Rune_LO4 = RunePattern(RUNE_LO4, RuneStrokesLO4);
//SUR
constexpr uint32 RUNE_SIZE_SUR1 = 6;
constexpr uint32 RUNE_SIZE_SUR2 = 6;
constexpr StrokeTypeDefsArray<RUNE_SIZE_SUR1> RuneStrokesSUR1 =
{
    ST_CUBIC_OR_CURVE_H, ST_SHARP_R, ST_LINE_REV, ST_CURVE_LM, ST_SHARP_R, ST_CUBIC_OR_SHARP_R
};
constexpr StrokeTypeDefsArray<RUNE_SIZE_SUR2> RuneStrokesSUR2 =
{
    ST_CUBIC_OR_CURVE_H, ST_SHARP_R, ST_SHARP_R, ST_CURVE_LM_R, ST_SHARP_R, ST_CUBIC_OR_SHARP_R
};
constexpr RunePattern Rune_SUR1 = RunePattern(RUNE_SUR1, RuneStrokesSUR1);
constexpr RunePattern Rune_SUR2 = RunePattern(RUNE_SUR2, RuneStrokesSUR2);
//BER
constexpr uint32 RUNE_SIZE_BER = 8;
constexpr StrokeTypeDefsArray<RUNE_SIZE_BER> RuneStrokesBER =
{
    ST_CUBIC_OR_CURVE_H, ST_CUBIC, ST_LINE_REV, ST_LINE_OR_NOTHING,
    ST_LINE_OR_CURVE_L, ST_LINE_REV, ST_CUBIC, ST_CUBIC_OR_CURVE_H
};
constexpr RunePattern Rune_BER = RunePattern(RUNE_BER, RuneStrokesBER);
//JAH
constexpr uint32 RUNE_SIZE_JAH1 = 7;
constexpr uint32 RUNE_SIZE_JAH2 = 7;
constexpr uint32 RUNE_SIZE_JAH3 = 6;
constexpr uint32 RUNE_SIZE_JAH4 = 6;
constexpr uint32 RUNE_SIZE_JAH5 = 6;
constexpr StrokeTypeDefsArray<RUNE_SIZE_JAH1> RuneStrokesJAH1 =
{
    ST_SHARP, ST_LINE, ST_SHARP_R, ST_LINE, ST_LINE_REV, ST_CURVE_M, ST_LINE
};
constexpr StrokeTypeDefsArray<RUNE_SIZE_JAH2> RuneStrokesJAH2 =
{
    ST_SHARP, ST_LINE, ST_CURVE_M, ST_LINE_REV, ST_LINE, ST_SHARP_R, ST_LINE
};
constexpr StrokeTypeDefsArray<RUNE_SIZE_JAH3> RuneStrokesJAH3 =
{
    ST_LINE, ST_SHARP, ST_SHARP, ST_LINE, ST_SHARP_R, ST_LINE
};
constexpr StrokeTypeDefsArray<RUNE_SIZE_JAH4> RuneStrokesJAH4 =
{
    ST_LINE, ST_SHARP, ST_LINE, ST_SHARP_R, ST_SHARP, ST_LINE
};
constexpr StrokeTypeDefsArray<RUNE_SIZE_JAH5> RuneStrokesJAH5 =
{
    ST_SHARP, ST_LINE, ST_SHARP_R, ST_SHARP, ST_LINE, ST_CURVE_M
};
constexpr RunePattern Rune_JAH1 = RunePattern(RUNE_JAH1, RuneStrokesJAH1);
constexpr RunePattern Rune_JAH2 = RunePattern(RUNE_JAH2, RuneStrokesJAH2);
constexpr RunePattern Rune_JAH3 = RunePattern(RUNE_JAH3, RuneStrokesJAH3);
constexpr RunePattern Rune_JAH4 = RunePattern(RUNE_JAH4, RuneStrokesJAH4);
constexpr RunePattern Rune_JAH5 = RunePattern(RUNE_JAH5, RuneStrokesJAH5);
//CHAM
constexpr uint32 RUNE_SIZE_CHAM1 = 8;
constexpr uint32 RUNE_SIZE_CHAM2 = 8;
constexpr uint32 RUNE_SIZE_CHAM3 = 8;
constexpr uint32 RUNE_SIZE_CHAM4 = 8;
constexpr uint32 RUNE_SIZE_CHAM5 = 8;
constexpr uint32 RUNE_SIZE_CHAM6 = 8;
constexpr uint32 RUNE_SIZE_CHAM7 = 8;
constexpr uint32 RUNE_SIZE_CHAM8 = 8;
constexpr uint32 RUNE_SIZE_CHAM9 = 8;
constexpr StrokeTypeDefsArray<RUNE_SIZE_CHAM1> RuneStrokesCHAM1 =
{
    ST_CUBIC_OR_SHARP, ST_LINE, ST_LINE_OR_NOTHING, ST_CUBIC_OR_SHARP, ST_SHARP, ST_SHARP_R, ST_SHARP_R, ST_SHARP_R
};
constexpr StrokeTypeDefsArray<RUNE_SIZE_CHAM2> RuneStrokesCHAM2 =
{
    ST_LINE, ST_LINE_OR_NOTHING, ST_CUBIC_OR_SHARP, ST_SHARP, ST_SHARP_R, ST_SHARP_R, ST_SHARP_R, ST_SHARP_R
};
constexpr StrokeTypeDefsArray<RUNE_SIZE_CHAM3> RuneStrokesCHAM3 =
{
    ST_LINE, ST_LINE_OR_NOTHING, ST_CUBIC_OR_SHARP, ST_SHARP, ST_SHARP_R, ST_SHARP_R, ST_SHARP_R, ST_SHARP_R //REUSE
};
constexpr StrokeTypeDefsArray<RUNE_SIZE_CHAM4> RuneStrokesCHAM4 =
{
    ST_CUBIC_OR_SHARP, ST_SHARP, ST_SHARP_R, ST_SHARP_R, ST_SHARP_R, ST_SHARP_R, ST_CUBIC_OR_SHARP, ST_LINE
};
constexpr StrokeTypeDefsArray<RUNE_SIZE_CHAM5> RuneStrokesCHAM5 =
{
    ST_SHARP, ST_SHARP_R, ST_SHARP_R, ST_SHARP_R, ST_SHARP_R, ST_CUBIC_OR_SHARP, ST_LINE, ST_LINE_OR_NOTHING
};
constexpr StrokeTypeDefsArray<RUNE_SIZE_CHAM6> RuneStrokesCHAM6 =
{
    ST_SHARP, ST_SHARP_R, ST_SHARP_R, ST_SHARP_R, ST_CUBIC_OR_SHARP, ST_LINE, ST_LINE_OR_NOTHING, ST_CUBIC_OR_SHARP
};
constexpr StrokeTypeDefsArray<RUNE_SIZE_CHAM7> RuneStrokesCHAM7 =
{
    ST_SHARP, ST_SHARP_R, ST_SHARP_R, ST_CUBIC_OR_SHARP, ST_LINE, ST_LINE_OR_NOTHING, ST_CUBIC_OR_SHARP, ST_SHARP
};
constexpr StrokeTypeDefsArray<RUNE_SIZE_CHAM8> RuneStrokesCHAM8 =
{
    ST_SHARP, ST_SHARP_R, ST_CUBIC_OR_SHARP, ST_LINE, ST_LINE_OR_NOTHING, ST_CUBIC_OR_SHARP, ST_SHARP, ST_SHARP_R
};
constexpr StrokeTypeDefsArray<RUNE_SIZE_CHAM9> RuneStrokesCHAM9 =
{
    ST_SHARP, ST_CUBIC_OR_SHARP, ST_LINE, ST_LINE_OR_NOTHING, ST_CUBIC_OR_SHARP, ST_SHARP, ST_SHARP_R, ST_SHARP_R
};
constexpr RunePattern Rune_CHAM1 = RunePattern(RUNE_CHAM1, RuneStrokesCHAM1);
constexpr RunePattern Rune_CHAM2 = RunePattern(RUNE_CHAM2, RuneStrokesCHAM2);
constexpr RunePattern Rune_CHAM3 = RunePattern(RUNE_CHAM3, RuneStrokesCHAM3);
constexpr RunePattern Rune_CHAM4 = RunePattern(RUNE_CHAM4, RuneStrokesCHAM4);
constexpr RunePattern Rune_CHAM5 = RunePattern(RUNE_CHAM5, RuneStrokesCHAM5);
constexpr RunePattern Rune_CHAM6 = RunePattern(RUNE_CHAM6, RuneStrokesCHAM6);
constexpr RunePattern Rune_CHAM7 = RunePattern(RUNE_CHAM7, RuneStrokesCHAM7);
constexpr RunePattern Rune_CHAM8 = RunePattern(RUNE_CHAM8, RuneStrokesCHAM8);
constexpr RunePattern Rune_CHAM9 = RunePattern(RUNE_CHAM9, RuneStrokesCHAM9);
//ZOD
constexpr uint32 RUNE_SIZE_ZOD1 = 7;
constexpr uint32 RUNE_SIZE_ZOD2 = 7;
constexpr StrokeTypeDefsArray<RUNE_SIZE_ZOD1> RuneStrokesZOD1 =
{
    ST_SHARP, ST_SHARP_R, ST_SHARP_R, ST_CUBIC_OR_CURVE_H_R, ST_SHARP_R, ST_CURVE_LMH, ST_CURVE_LMH_R
};
constexpr StrokeTypeDefsArray<RUNE_SIZE_ZOD2> RuneStrokesZOD2 =
{
    ST_SHARP, ST_SHARP_R, ST_SHARP_R, ST_CUBIC_OR_SHARP_R, ST_SHARP_R, ST_CURVE_LMH, ST_CURVE_LMH_R
};
constexpr RunePattern Rune_ZOD1 = RunePattern(RUNE_ZOD1, RuneStrokesZOD1);
constexpr RunePattern Rune_ZOD2 = RunePattern(RUNE_ZOD2, RuneStrokesZOD2);

//Runewords
constexpr uint32 RUNEWORD_SIZE_STEEL = 2;
constexpr RuneSpellsArray<RUNEWORD_SIZE_STEEL> RunesSTEEL = { SPELL_TIR_SELF, SPELL_EL_SELF };
constexpr RunewordPattern Runeword_STEEL = RunewordPattern(RUNEWORD_STEEL, RunesSTEEL);
constexpr uint32 RUNEWORD_SIZE_NADIR = 2;
constexpr RuneSpellsArray<RUNEWORD_SIZE_NADIR> RunesNADIR = { SPELL_NEF_SELF, SPELL_TIR_SELF };
constexpr RunewordPattern Runeword_NADIR = RunewordPattern(RUNEWORD_NADIR, RunesNADIR);
constexpr uint32 RUNEWORD_SIZE_MALICE = 3;
constexpr RuneSpellsArray<RUNEWORD_SIZE_MALICE> RunesMALICE = { SPELL_ITH_SELF, SPELL_EL_SELF, SPELL_ETH_SELF };
constexpr RunewordPattern Runeword_MALICE = RunewordPattern(RUNEWORD_MALICE, RunesMALICE);
constexpr uint32 RUNEWORD_SIZE_STEALTH = 2;
constexpr RuneSpellsArray<RUNEWORD_SIZE_STEALTH> RunesSTEALTH = { SPELL_TAL_SELF, SPELL_ETH_SELF };
constexpr RunewordPattern Runeword_STEALTH = RunewordPattern(RUNEWORD_STEALTH, RunesSTEALTH);
constexpr uint32 RUNEWORD_SIZE_LEAF = 2;
constexpr RuneSpellsArray<RUNEWORD_SIZE_LEAF> RunesLEAF = { SPELL_TIR_SELF, SPELL_RAL_SELF };
constexpr RunewordPattern Runeword_LEAF = RunewordPattern(RUNEWORD_LEAF, RunesLEAF);
constexpr uint32 RUNEWORD_SIZE_ZEPHYR = 2;
constexpr RuneSpellsArray<RUNEWORD_SIZE_ZEPHYR> RunesZEPHYR = { SPELL_ORT_SELF, SPELL_ETH_SELF };
constexpr RunewordPattern Runeword_ZEPHYR = RunewordPattern(RUNEWORD_ZEPHYR, RunesZEPHYR);
constexpr uint32 RUNEWORD_SIZE_ANCIENTS_PLEDGE = 3;
constexpr RuneSpellsArray<RUNEWORD_SIZE_ANCIENTS_PLEDGE> RunesANCIENTS_PLEDGE = { SPELL_RAL_SELF, SPELL_ORT_SELF, SPELL_TAL_SELF };
constexpr RunewordPattern Runeword_ANCIENTS_PLEDGE = RunewordPattern(RUNEWORD_ANCIENTS_PLEDGE, RunesANCIENTS_PLEDGE);
constexpr uint32 RUNEWORD_SIZE_STRENGTH = 2;
constexpr RuneSpellsArray<RUNEWORD_SIZE_STRENGTH> RunesSTRENGTH = { SPELL_AMN_SELF, SPELL_TIR_SELF };
constexpr RunewordPattern Runeword_STRENGTH = RunewordPattern(RUNEWORD_STRENGTH, RunesSTRENGTH);
constexpr uint32 RUNEWORD_SIZE_EDGE = 3;
constexpr RuneSpellsArray<RUNEWORD_SIZE_EDGE> RunesEDGE = { SPELL_TIR_SELF, SPELL_TAL_SELF, SPELL_AMN_SELF };
constexpr RunewordPattern Runeword_EDGE = RunewordPattern(RUNEWORD_EDGE, RunesEDGE);
constexpr uint32 RUNEWORD_SIZE_KINGS_GRACE = 3;
constexpr RuneSpellsArray<RUNEWORD_SIZE_KINGS_GRACE> RunesKINGS_GRACE = { SPELL_AMN_SELF, SPELL_RAL_SELF, SPELL_THUL_SELF };
constexpr RunewordPattern Runeword_KINGS_GRACE = RunewordPattern(RUNEWORD_KINGS_GRACE, RunesKINGS_GRACE);
constexpr uint32 RUNEWORD_SIZE_RADIANCE = 3;
constexpr RuneSpellsArray<RUNEWORD_SIZE_RADIANCE> RunesRADIANCE = { SPELL_NEF_SELF, SPELL_SOL_SELF, SPELL_ITH_SELF };
constexpr RunewordPattern Runeword_RADIANCE = RunewordPattern(RUNEWORD_RADIANCE, RunesRADIANCE);
constexpr uint32 RUNEWORD_SIZE_LORE = 2;
constexpr RuneSpellsArray<RUNEWORD_SIZE_LORE> RunesLORE = { SPELL_ORT_SELF, SPELL_SOL_SELF };
constexpr RunewordPattern Runeword_LORE = RunewordPattern(RUNEWORD_LORE, RunesLORE);
constexpr uint32 RUNEWORD_SIZE_RHYME = 2;
constexpr RuneSpellsArray<RUNEWORD_SIZE_RHYME> RunesRHYME = { SPELL_SHAEL_SELF, SPELL_ETH_SELF };
constexpr RunewordPattern Runeword_RHYME = RunewordPattern(RUNEWORD_RHYME, RunesRHYME);
constexpr uint32 RUNEWORD_SIZE_PEACE = 3;
constexpr RuneSpellsArray<RUNEWORD_SIZE_PEACE> RunesPEACE = { SPELL_SHAEL_SELF, SPELL_THUL_SELF, SPELL_AMN_SELF };
constexpr RunewordPattern Runeword_PEACE = RunewordPattern(RUNEWORD_PEACE, RunesPEACE);
constexpr uint32 RUNEWORD_SIZE_MYTH = 3;
constexpr RuneSpellsArray<RUNEWORD_SIZE_MYTH> RunesMYTH = { SPELL_HEL_SELF, SPELL_AMN_SELF, SPELL_NEF_SELF };
constexpr RunewordPattern Runeword_MYTH = RunewordPattern(RUNEWORD_MYTH, RunesMYTH);
constexpr uint32 RUNEWORD_SIZE_BLACK = 3;
constexpr RuneSpellsArray<RUNEWORD_SIZE_BLACK> RunesBLACK = { SPELL_THUL_SELF, SPELL_IO_SELF, SPELL_NEF_SELF };
constexpr RunewordPattern Runeword_BLACK = RunewordPattern(RUNEWORD_BLACK, RunesBLACK);
constexpr uint32 RUNEWORD_SIZE_WHITE = 2;
constexpr RuneSpellsArray<RUNEWORD_SIZE_WHITE> RunesWHITE = { SPELL_DOL_SELF, SPELL_IO_SELF };
constexpr RunewordPattern Runeword_WHITE = RunewordPattern(RUNEWORD_WHITE, RunesWHITE);
constexpr uint32 RUNEWORD_SIZE_SMOKE = 2;
constexpr RuneSpellsArray<RUNEWORD_SIZE_SMOKE> RunesSMOKE = { SPELL_NEF_SELF, SPELL_LUM_SELF };
constexpr RunewordPattern Runeword_SMOKE = RunewordPattern(RUNEWORD_SMOKE, RunesSMOKE);
constexpr uint32 RUNEWORD_SIZE_SPLENDOR = 2;
constexpr RuneSpellsArray<RUNEWORD_SIZE_SPLENDOR> RunesSPLENDOR = { SPELL_ETH_SELF, SPELL_LUM_SELF };
constexpr RunewordPattern Runeword_SPLENDOR = RunewordPattern(RUNEWORD_SPLENDOR, RunesSPLENDOR);
constexpr uint32 RUNEWORD_SIZE_MELODY = 3;
constexpr RuneSpellsArray<RUNEWORD_SIZE_MELODY> RunesMELODY = { SPELL_SHAEL_SELF, SPELL_KO_SELF, SPELL_NEF_SELF };
constexpr RunewordPattern Runeword_MELODY = RunewordPattern(RUNEWORD_MELODY, RunesMELODY);
constexpr uint32 RUNEWORD_SIZE_LIONHEART = 3;
constexpr RuneSpellsArray<RUNEWORD_SIZE_LIONHEART> RunesLIONHEART = { SPELL_HEL_SELF, SPELL_LUM_SELF, SPELL_FAL_SELF };
constexpr RunewordPattern Runeword_LIONHEART = RunewordPattern(RUNEWORD_LIONHEART, RunesLIONHEART);
constexpr uint32 RUNEWORD_SIZE_TREACHERY = 3;
constexpr RuneSpellsArray<RUNEWORD_SIZE_TREACHERY> RunesTREACHERY = { SPELL_SHAEL_SELF, SPELL_THUL_SELF, SPELL_LEM_SELF };
constexpr RunewordPattern Runeword_TREACHERY = RunewordPattern(RUNEWORD_TREACHERY, RunesTREACHERY);
constexpr uint32 RUNEWORD_SIZE_WEALTH = 3;
constexpr RuneSpellsArray<RUNEWORD_SIZE_WEALTH> RunesWEALTH = { SPELL_LEM_SELF, SPELL_KO_SELF, SPELL_TIR_SELF };
constexpr RunewordPattern Runeword_WEALTH = RunewordPattern(RUNEWORD_WEALTH, RunesWEALTH);
constexpr uint32 RUNEWORD_SIZE_LAWBRINGER = 3;
constexpr RuneSpellsArray<RUNEWORD_SIZE_LAWBRINGER> RunesLAWBRINGER = { SPELL_AMN_SELF, SPELL_LEM_SELF, SPELL_KO_SELF };
constexpr RunewordPattern Runeword_LAWBRINGER = RunewordPattern(RUNEWORD_LAWBRINGER, RunesLAWBRINGER);
constexpr uint32 RUNEWORD_SIZE_ENLIGHTENMENT = 3;
constexpr RuneSpellsArray<RUNEWORD_SIZE_ENLIGHTENMENT> RunesENLIGHTENMENT = { SPELL_PUL_SELF, SPELL_RAL_SELF, SPELL_SOL_SELF };
constexpr RunewordPattern Runeword_ENLIGHTENMENT = RunewordPattern(RUNEWORD_ENLIGHTENMENT, RunesENLIGHTENMENT);
constexpr uint32 RUNEWORD_SIZE_CRESCENT_MOON = 3;
constexpr RuneSpellsArray<RUNEWORD_SIZE_CRESCENT_MOON> RunesCRESCENT_MOON = { SPELL_SHAEL_SELF, SPELL_UM_SELF, SPELL_TIR_SELF };
constexpr RunewordPattern Runeword_CRESCENT_MOON = RunewordPattern(RUNEWORD_CRESCENT_MOON, RunesCRESCENT_MOON);
constexpr uint32 RUNEWORD_SIZE_DURESS = 3;
constexpr RuneSpellsArray<RUNEWORD_SIZE_DURESS> RunesDURESS = { SPELL_SHAEL_SELF, SPELL_LUM_SELF, SPELL_THUL_SELF };
constexpr RunewordPattern Runeword_DURESS = RunewordPattern(RUNEWORD_DURESS, RunesDURESS);
constexpr uint32 RUNEWORD_SIZE_GLOOM = 3;
constexpr RuneSpellsArray<RUNEWORD_SIZE_GLOOM> RunesGLOOM = { SPELL_FAL_SELF, SPELL_UM_SELF, SPELL_PUL_SELF };
constexpr RunewordPattern Runeword_GLOOM = RunewordPattern(RUNEWORD_GLOOM, RunesGLOOM);
constexpr uint32 RUNEWORD_SIZE_PRUDENCE = 2;
constexpr RuneSpellsArray<RUNEWORD_SIZE_PRUDENCE> RunesPRUDENCE = { SPELL_MAL_SELF, SPELL_TIR_SELF };
constexpr RunewordPattern Runeword_PRUDENCE = RunewordPattern(RUNEWORD_PRUDENCE, RunesPRUDENCE);
constexpr uint32 RUNEWORD_SIZE_RAIN = 3;
constexpr RuneSpellsArray<RUNEWORD_SIZE_RAIN> RunesRAIN = { SPELL_ORT_SELF, SPELL_MAL_SELF, SPELL_ITH_SELF };
constexpr RunewordPattern Runeword_RAIN = RunewordPattern(RUNEWORD_RAIN, RunesRAIN);
constexpr uint32 RUNEWORD_SIZE_VENOM = 3;
constexpr RuneSpellsArray<RUNEWORD_SIZE_VENOM> RunesVENOM = { SPELL_TAL_SELF, SPELL_DOL_SELF, SPELL_MAL_SELF };
constexpr RunewordPattern Runeword_VENOM = RunewordPattern(RUNEWORD_VENOM, RunesVENOM);
//constexpr uint32 RUNEWORD_SIZE_SANCTUARY = 3;
//constexpr RuneSpellsArray<RUNEWORD_SIZE_SANCTUARY> RunesSANCTUARY = { SPELL_KO_SELF, SPELL_KO_SELF, SPELL_MAL_SELF };
//constexpr RunewordPattern Runeword_SANCTUARY = RunewordPattern(RUNEWORD_SANCTUARY, RunesSANCTUARY);
constexpr uint32 RUNEWORD_SIZE_DELIRIUM = 3;
constexpr RuneSpellsArray<RUNEWORD_SIZE_DELIRIUM> RunesDELIRIUM = { SPELL_LEM_SELF, SPELL_IST_SELF, SPELL_IO_SELF };
constexpr RunewordPattern Runeword_DELIRIUM = RunewordPattern(RUNEWORD_DELIRIUM, RunesDELIRIUM);
constexpr uint32 RUNEWORD_SIZE_PRINCIPLE = 3;
constexpr RuneSpellsArray<RUNEWORD_SIZE_PRINCIPLE> RunesPRINCIPLE = { SPELL_RAL_SELF, SPELL_GUL_SELF, SPELL_ELD_SELF };
constexpr RunewordPattern Runeword_PRINCIPLE = RunewordPattern(RUNEWORD_PRINCIPLE, RunesPRINCIPLE);
constexpr uint32 RUNEWORD_SIZE_CHAOS = 3;
constexpr RuneSpellsArray<RUNEWORD_SIZE_CHAOS> RunesCHAOS = { SPELL_FAL_SELF, SPELL_OHM_SELF, SPELL_UM_SELF };
constexpr RunewordPattern Runeword_CHAOS = RunewordPattern(RUNEWORD_CHAOS, RunesCHAOS);
constexpr uint32 RUNEWORD_SIZE_WIND = 2;
constexpr RuneSpellsArray<RUNEWORD_SIZE_WIND> RunesWIND = { SPELL_SUR_SELF, SPELL_EL_SELF };
constexpr RunewordPattern Runeword_WIND = RunewordPattern(RUNEWORD_WIND, RunesWIND);
constexpr uint32 RUNEWORD_SIZE_DRAGON = 3;
constexpr RuneSpellsArray<RUNEWORD_SIZE_DRAGON> RunesDRAGON = { SPELL_SUR_SELF, SPELL_LO_SELF, SPELL_SOL_SELF };
constexpr RunewordPattern Runeword_DRAGON = RunewordPattern(RUNEWORD_DRAGON, RunesDRAGON);
constexpr uint32 RUNEWORD_SIZE_DREAM = 3;
constexpr RuneSpellsArray<RUNEWORD_SIZE_DREAM> RunesDREAM = { SPELL_IO_SELF, SPELL_JAH_SELF, SPELL_PUL_SELF };
constexpr RunewordPattern Runeword_DREAM = RunewordPattern(RUNEWORD_DREAM, RunesDREAM);
constexpr uint32 RUNEWORD_SIZE_FURY = 3;
constexpr RuneSpellsArray<RUNEWORD_SIZE_FURY> RunesFURY = { SPELL_JAH_SELF, SPELL_GUL_SELF, SPELL_ETH_SELF };
constexpr RunewordPattern Runeword_FURY = RunewordPattern(RUNEWORD_FURY, RunesFURY);
constexpr uint32 RUNEWORD_SIZE_ENIGMA = 3;
constexpr RuneSpellsArray<RUNEWORD_SIZE_ENIGMA> RunesENIGMA = { SPELL_JAH_SELF, SPELL_ITH_SELF, SPELL_BER_SELF };
constexpr RunewordPattern Runeword_ENIGMA = RunewordPattern(RUNEWORD_ENIGMA, RunesENIGMA);

//List
constexpr RunePattern RunePatterns[MAX_RUNE_TYPES] =
{
    Rune_EL1,
    Rune_EL2,
    Rune_ELD1,
    Rune_ELD2,
    Rune_TIR1,
    Rune_TIR2,
    Rune_NEF,
    Rune_ETH1,
    Rune_ETH2,
    Rune_ITH1,
    Rune_ITH2,
    Rune_TAL1,
    Rune_TAL2,
    Rune_TAL3,
    Rune_RAL,
    Rune_ORT1,
    Rune_ORT2,
    Rune_ORT3,
    Rune_THUL,
    Rune_AMN,
    Rune_SOL,
    Rune_SHAEL1,
    Rune_SHAEL2,
    Rune_DOL1,
    Rune_DOL2,
    Rune_HEL1,
    Rune_HEL2,
    Rune_IO1,
    Rune_IO2,
    Rune_IO3,
    Rune_LUM,
    Rune_KO,
    Rune_FAL1,
    Rune_FAL2,
    Rune_FAL3,
    Rune_FAL4,
    Rune_FAL5,
    Rune_FAL6,
    Rune_LEM1,
    Rune_LEM2,
    Rune_LEM3,
    Rune_LEM4,
    Rune_PUL,
    Rune_UM1,
    Rune_UM2,
    Rune_UM3,
    Rune_MAL1,
    Rune_MAL2,
    Rune_IST,
    Rune_GUL1,
    Rune_GUL2,
    Rune_GUL3,
    Rune_VEX1,
    Rune_VEX2,
    Rune_VEX3,
    Rune_VEX4,
    Rune_VEX5,
    Rune_VEX6,
    Rune_OHM,
    Rune_LO1,
    Rune_LO2,
    Rune_LO3,
    Rune_LO4,
    Rune_SUR1,
    Rune_SUR2,
    Rune_BER,
    Rune_JAH1,
    Rune_JAH2,
    Rune_JAH3,
    Rune_JAH4,
    Rune_JAH5,
    Rune_CHAM1,
    Rune_CHAM2,
    Rune_CHAM3,
    Rune_CHAM4,
    Rune_CHAM5,
    Rune_CHAM6,
    Rune_CHAM7,
    Rune_CHAM8,
    Rune_CHAM9,
    Rune_ZOD1,
    Rune_ZOD2
};

constexpr RunewordPattern RunewordPatterns[MAX_RUNEWORD_TYPES] =
{
    Runeword_STEEL,
    Runeword_NADIR,
    Runeword_MALICE,
    Runeword_STEALTH,
    Runeword_LEAF,
    Runeword_ZEPHYR,
    Runeword_ANCIENTS_PLEDGE,
    Runeword_STRENGTH,
    Runeword_EDGE,
    Runeword_KINGS_GRACE,
    Runeword_RADIANCE,
    Runeword_LORE,
    Runeword_RHYME,
    Runeword_PEACE,
    Runeword_MYTH,
    Runeword_BLACK,
    Runeword_WHITE,
    Runeword_SMOKE,
    Runeword_SPLENDOR,
    Runeword_MELODY,
    Runeword_LIONHEART,
    Runeword_TREACHERY,
    Runeword_WEALTH,
    Runeword_LAWBRINGER,
    Runeword_ENLIGHTENMENT,
    Runeword_CRESCENT_MOON,
    Runeword_DURESS,
    Runeword_GLOOM,
    Runeword_PRUDENCE,
    Runeword_RAIN,
    Runeword_VENOM,
    //Runeword_SANCTUARY,
    Runeword_DELIRIUM,
    Runeword_PRINCIPLE,
    Runeword_CHAOS,
    Runeword_WIND,
    Runeword_DRAGON,
    Runeword_DREAM,
    Runeword_FURY,
    Runeword_ENIGMA
};

//-180 to 180
int32 GetDegrees(Position const* pos1, Position const* posMid, Position const* pos3)
{
    float ang = posMid->GetAbsoluteAngle(pos1) - posMid->GetAbsoluteAngle(pos3);
    ang = (ang > M_PI) ? ang - M_PI * 2 : (ang < -M_PI) ? ang + M_PI * 2 : ang;

    return int32(ang * 180.f / M_PI);
}

template<size_t N>
constexpr bool RunePattern::Matches(std::array<Stroke, N> const& compSeq, RunePattern const& pattern)
{
    using StrokeArr = std::array<Stroke, N>;

    uint8 minSize = pattern.minSize;
    uint8 size = pattern.size;
    StrokeTypeDefs const* strokeSequence = pattern.strokeSequence;

    if (minSize > compSeq.size())
        return false; //impossible

    StrokeTypeDefs jStroke = StrokeTypeDefs(0), jStrokeP = StrokeTypeDefs(0);
    bool found = false;
    bool fullmatch = true;
    uint32 unmatchCount = 0;
    int32 seqsize = compSeq.size(); // 7, minSize = 7, ST_SHARP, ST_SHARP_R, ST_SHARP_R, ST_CUBIC_OR_SHARP_R, ST_SHARP_R, ST_CURVE_LMH, ST_CURVE_LMH_R

    uint32 thisMask = 0;
    uint32 seqMask = 0;

    for (int32 i = 0; i < seqsize; ++i)
    {
        if (minSize > seqsize - i)
        {
            //no way
            fullmatch = false;
            unmatchCount = UNMATCH_THRESHOLD + 1;
            break;
        }

        fullmatch = true;
        if ((1 << compSeq[i].type) & strokeSequence[0])
        {
            StrokeArr seq = compSeq;
            //remove first reverse flag in the sequence to normalize
            if (seq[i].reverse)
                seq[i].reverse = false;
            found = true;
            unmatchCount = 0;

            for (int32 j = i + 1, k = 1; j < seqsize && k < size; ++j, ++k)
            {
                jStroke = StrokeTypeDefs((seq[j].reverse) ? ((1 << seq[j].type) | STDEF_REV) : (1 << seq[j].type));
                jStrokeP = StrokeTypeDefs((seq[j - 1].reverse) ? ((1 << seq[j - 1].type) | STDEF_REV) : (1 << seq[j - 1].type));
                seqMask = jStroke & ~STDEF_REV;
                thisMask = strokeSequence[k] & ~STDEF_REV;
                //we may want to skip current node in own sequence
                if ((thisMask & STDEF_CAN_BE_EMPTY) && k < size - 1 && seqsize < size &&
                    (strokeSequence[k + 1] & seqMask))
                {
                    --j;
                    continue;
                }
                //reversing alterations
                bool revEqCur = (strokeSequence[k] & STDEF_REV) == (jStroke & STDEF_REV);
                if (revEqCur == false || !(thisMask & seqMask))
                {
                    //can skip point in own sequence
                    if ((revEqCur == true || (thisMask & ST_LINETYPES)) &&
                        (thisMask & STDEF_CAN_BE_EMPTY))
                    {
                        --j;
                        continue;
                    }

                    fullmatch = false;
                    if (seq[j].type == ST_LINE || seq[j].type == ST_LINE_REV)
                    {
                        unmatchCount = UNMATCH_THRESHOLD + 1;
                        break;
                    }
                    if (++unmatchCount > UNMATCH_THRESHOLD)
                        break;
                }
            }

            if (fullmatch || unmatchCount <= UNMATCH_THRESHOLD)
                break;
        }
    }

    if (!found)
        return false;

    if (fullmatch)
        return true;

    if (unmatchCount <= UNMATCH_THRESHOLD)
        return true;

    //reverse order
    for (int32 i = seqsize - 1; i >= 0; --i)
    {
        if (minSize > i + 1)
        {
            //no way
            fullmatch = false;
            unmatchCount = UNMATCH_THRESHOLD + 1;
            break;
        }

        fullmatch = true;
        unmatchCount = 0;
        if ((1 << compSeq[i].type) & strokeSequence[0])
        {
            StrokeArr seq = compSeq;
            //remove first reverse flag in the sequence to normalize
            if (seq[i].reverse)
                seq[i].reverse = false;
            for (int32 j = i - 1, k = 1; j >= 0 && k < size; --j, ++k)
            {
                jStroke = StrokeTypeDefs((seq[j].reverse) ? ((1 << seq[j].type) | STDEF_REV) : (1 << seq[j].type));
                jStrokeP = StrokeTypeDefs((seq[j + 1].reverse) ? ((1 << seq[j + 1].type) | STDEF_REV) : (1 << seq[j + 1].type));
                seqMask = jStroke & ~STDEF_REV;
                thisMask = strokeSequence[k] & ~STDEF_REV;
                //we may want to skip current node in own sequence
                if ((thisMask & STDEF_CAN_BE_EMPTY) && k < size - 1 && seqsize < size &&
                    (strokeSequence[k + 1] & seqMask))
                {
                    ++j;
                    continue;
                }
                //reversing alterations
                bool revEqCur = (strokeSequence[k] & STDEF_REV) == (jStroke & STDEF_REV);
                if (revEqCur == false || !(thisMask & seqMask))
                {
                    //can skip point in own sequence
                    if ((revEqCur == true || (thisMask & ST_LINETYPES)) &&
                        (thisMask & STDEF_CAN_BE_EMPTY))
                    {
                        ++j;
                        continue;
                    }

                    fullmatch = false;
                    if (seq[j].type == ST_LINE || seq[j].type == ST_LINE_REV)
                    {
                        unmatchCount = UNMATCH_THRESHOLD + 1;
                        break;
                    }
                    if (++unmatchCount > UNMATCH_THRESHOLD)
                        break;
                }
            }

            if (fullmatch || unmatchCount <= UNMATCH_THRESHOLD)
                break;
        }
    }

    return fullmatch || unmatchCount <= UNMATCH_THRESHOLD;
}

bool RunePattern::Matches(Strokes const& compSeq) const
{
    size_t compSize = compSeq.size();
    if (this->minSize > compSize)
        return false; //impossible

    //just pass it to a constexpr function (in an ugly way, yes)
    if (compSize == 3) //MIN_RUNE_PATTERN_LENGTH - 2
        return Matches(std::array{ compSeq[0], compSeq[1], compSeq[2] }, *this);
    else if (compSize == 4) //MIN_RUNE_PATTERN_LENGTH -1
        return Matches(std::array{ compSeq[0], compSeq[1], compSeq[2], compSeq[3] }, *this);
    else if (compSize == 5) //MIN_RUNE_PATTERN_LENGTH
        return Matches(std::array{ compSeq[0], compSeq[1], compSeq[2], compSeq[3], compSeq[4] }, *this);
    else if (compSize == 6) //MIN_RUNE_PATTERN_LENGTH + 1
        return Matches(std::array{ compSeq[0], compSeq[1], compSeq[2], compSeq[3], compSeq[4], compSeq[5] }, *this);
    else if (compSize == 7) //MIN_RUNE_PATTERN_LENGTH + 2
        return Matches(std::array{ compSeq[0], compSeq[1], compSeq[2], compSeq[3], compSeq[4], compSeq[5], compSeq[6] }, *this);
    else if (compSize == 8) //MIN_RUNE_PATTERN_LENGTH + 3
        return Matches(std::array{ compSeq[0], compSeq[1], compSeq[2], compSeq[3], compSeq[4], compSeq[5], compSeq[6], compSeq[7] }, *this);
    else if (compSize == 9) //MAX_RUNE_PATTERN_LENGTH
        return Matches(std::array{ compSeq[0], compSeq[1], compSeq[2], compSeq[3], compSeq[4], compSeq[5], compSeq[6], compSeq[7], compSeq[8] }, *this);
    else if (compSize == 10) //MAX_RUNE_PATTERN_LENGTH + 1
        return Matches(std::array{ compSeq[0], compSeq[1], compSeq[2], compSeq[3], compSeq[4], compSeq[5], compSeq[6], compSeq[7], compSeq[8], compSeq[9] }, *this);
    else if (compSize == 11) //MAX_RUNE_PATTERN_LENGTH + 2
        return Matches(std::array{ compSeq[0], compSeq[1], compSeq[2], compSeq[3], compSeq[4], compSeq[5], compSeq[6], compSeq[7], compSeq[8], compSeq[9], compSeq[10] }, *this);
    else if (compSize == 12) //MAX_RUNE_POINTS
        return Matches(std::array{ compSeq[0], compSeq[1], compSeq[2], compSeq[3], compSeq[4], compSeq[5], compSeq[6], compSeq[7], compSeq[8], compSeq[9], compSeq[10], compSeq[11] }, *this);
    else
        return false;
}

bool RunewordPattern::Contains(RuneSpellVec const& compSeq) const
{
    size_t compSize = compSeq.size();
    if (this->size > compSize)
        return false; //impossible

    //copy our array
    using SpArr = std::vector<std::pair<uint32, bool>>;
    SpArr s(this->size);
    for (size_t i = 0; i < s.size(); ++i)
    {
        s[i].first = runeSpellList[i];
        s[i].second = false;
    }

    for (size_t i = 0; i < compSize; ++i)
    {
        uint32 curRune = compSeq[i];
        for (size_t j = 0; j < s.size(); ++j)
            if (s[j].second == false && s[j].first == curRune)
                s[j].second = true;
    }

    for (size_t i = 0; i < s.size(); ++i)
        if (s[i].second == false)
            return false;

    return true;
}

template<size_t M, size_t N>
constexpr bool RunewordPattern::Contains(std::array<RuneworderSpells, N> const& compSeq, RunewordPattern const& pattern)
{
    if (M > compSeq.size())
        return false; //impossible

    //copy our array
    using SpArr = std::array<std::pair<uint32, bool>, M>;
    SpArr s;
    for (size_t i = 0; i < M; ++i)
    {
        s[i].first = pattern.runeSpellList[i];
        s[i].second = false;
    }

    for (size_t i = 0; i < N; ++i)
    {
        uint32 curRune = compSeq[i];
        for (size_t j = 0; j < M; ++j)
            if (s[j].second == false && s[j].first == curRune)
                s[j].second = true;
    }

    for (size_t i = 0; i < M; ++i)
        if (s[i].second == false)
            return false;

    return true;
}

typedef std::vector<Creature*> Points;

//stationary rune point marker
class npc_rune_bunny : public CreatureScript
{
    public:
        npc_rune_bunny() : CreatureScript("npc_rune_bunny") { }

        struct npc_rune_bunnyAI : public ScriptedAI
        {
            npc_rune_bunnyAI(Creature* creature) : ScriptedAI(creature) { }

            void Reset() override
            {
                //LOG("scripts", "npc_rune_bunnyAI: _Reset from Reset");
                _Reset();
            }

            void IsSummonedBy(WorldObject* summoner) override
            {
                //LOG("scripts", "npc_rune_bunnyAI: IsSummonedBy");
                if (summoner->GetEntry() != NPC_BOSS_RUNEWORDER)
                {
                    //LOG("scripts", "npc_rune_bunnyAI: Summoned by non-runeworder!");
                    //ASSERT(false);
                    return;
                }
            }

            void UpdateAI(uint32 /*diff*/) override
            {
            }

        private:

            void _Reset()
            {
                DoCast(me, SPELL_COSMETIC_FLAMES);
                me->SetUInt32Value(UNIT_FIELD_FLAGS, UNIT_FLAG_UNINTERACTIBLE | UNIT_FLAG_IMMUNE_TO_PC | UNIT_FLAG_IMMUNE_TO_NPC);
                //LOG("scripts", "npc_rune_bunnyAI: _Reset");
            }
        };

        CreatureAI* GetAI(Creature* creature) const
        {
            return new npc_rune_bunnyAI(creature);
        }
};

//moving trigger
class npc_rune_carver : public CreatureScript
{
    public:
        npc_rune_carver() : CreatureScript("npc_rune_carver") { }

        struct npc_rune_carverAI : public ScriptedAI
        {
            npc_rune_carverAI(Creature* creature) : ScriptedAI(creature)
            {
                _runeworder = nullptr;
            }

            void Reset() override
            {
                //LOG("scripts", "npc_rune_carverAI: _Reset from Reset");
                _Reset();
                DoCast(me, SPELL_COSMETIC_FLAMES);
            }

            void JustDied(Unit* /*killer*/) override
            {
                //LOG("scripts", "npc_rune_carverAI: _Reset from JustDied");
                _Reset();
            }

            void IsSummonedBy(WorldObject* summoner) override
            {
                //LOG("scripts", "npc_rune_carverAI: IsSummonedBy");
                if (summoner->GetEntry() != NPC_BOSS_RUNEWORDER)
                {
                    TC_LOG_ERROR("scripts", "npc_rune_bunnyAI: Summoned by non-runeworder!");
                    //ASSERT(false);
                    return;
                }

                _runeworder = summoner->ToUnit();

                _events.ScheduleEvent(EVENT_FLAMES, Milliseconds(2000));
            }

            void UpdateAI(uint32 diff) override
            {
                if (_runeworder == nullptr)
                    return;

                _events.Update(diff);

                if (!_chaseGUID)
                {
                    //LOG("scripts", "npc_rune_carverAI: _chaseGUID reset...");
                    Unit* target = _runeworder->ToCreature()->AI()->SelectTarget(SelectTargetMethod::MaxDistance, 0, 40.f, true);
                    if (!target)
                        target = _runeworder->GetVictim();
                    if (!target)
                    {
                        if (me->GetVictim())
                            me->AttackStop();
                        if (me->isMoving())
                            me->StopMoving();
                        return;
                    }

                    //LOG("scripts", "npc_rune_carverAI: found target...");
                    _chaseGUID = target->GetGUID();
                }

                Unit* victim = ObjectAccessor::GetUnit(*me, _chaseGUID);
                if (!victim || !victim->IsAlive())
                {
                    //LOG("scripts", "npc_rune_carverAI: target is gone or dead");
                    //find next target
                    _chaseGUID = ObjectGuid::Empty;
                    return;
                }

                if (me->GetVictim() != victim)
                {
                    ModifyThreatByPercent(me->GetVictim(), -100);
                    AddThreat(victim, 999999);
                    me->Attack(victim, false);
                    //me->GetMotionMaster()->MoveChase(victim);
                    //LOG("scripts", "npc_rune_carverAI: moving after %s, Scheduling EVENT_POINT_PUT", victim->GetName().c_str());
                }

                while (uint32 eventId = _events.ExecuteEvent())
                {
                    switch (eventId)
                    {
                        case EVENT_FLAMES:
                            //LOG("scripts", "npc_rune_carverAI: ExecuteEvent EVENT_FLAMES");
                            if (!victim->isMoving() && me->IsWithinMeleeRange(victim))
                            {
#ifdef AC_PLATFORM
                                me->CastSpell(me, SPELL_FLAMES, TRIGGERED_NONE, NULL, NULL, _runeworder->GetGUID());
#else
                                CastSpellExtraArgs args;
                                args.OriginalCaster = _runeworder->GetGUID();
                                me->CastSpell(me, SPELL_FLAMES, args);
#endif
                            }
                            _events.ScheduleEvent(EVENT_FLAMES, Milliseconds(1000));
                            break;
                        default:
                            TC_LOG_ERROR("scripts", "npc_rune_carverAI: unhandled event %u!", eventId);
                            //ASSERT(false);
                            break;
                    }
                }

                me->GetMotionMaster()->MovePoint(me->GetMapId(), *victim, false);
          }

        private:
            EventMap _events;

            ObjectGuid _chaseGUID;
            Unit* _runeworder;

            void _Reset()
            {
                _events.Reset();
                _chaseGUID = ObjectGuid::Empty;
                me->SetUInt32Value(UNIT_FIELD_FLAGS, UNIT_FLAG_UNINTERACTIBLE | UNIT_FLAG_IMMUNE_TO_PC | UNIT_FLAG_IMMUNE_TO_NPC);
                //LOG("scripts", "npc_rune_carverAI: _Reset");
            }
        };

        CreatureAI* GetAI(Creature* creature) const
        {
            return new npc_rune_carverAI(creature);
        }
};

class boss_runeworder : public CreatureScript
{
    public:
        boss_runeworder() : CreatureScript("boss_runeworder") { }

        struct boss_runeworderAI : public ScriptedAI
        {
            boss_runeworderAI(Creature* creature) : ScriptedAI(creature)
            {
                _carver = nullptr;
            }

            void Reset() override
            {
                //LOG("scripts", "runeworderAI: _Reset from Reset");
                _Reset();
            }

#ifdef AC_PLATFORM
            void EnterEvadeMode()
            {
                //LOG("scripts", "runeworderAI: EnterEvadeMode");
                CreatureAI::EnterEvadeMode();
            }
#else
            void JustAppeared() override
            {
                //LOG("scripts", "runeworderAI: _Reset from JustRespawned");
                _Reset();
            }

            void EnterEvadeMode(EvadeReason why) override
            {
                //LOG("scripts", "runeworderAI: EnterEvadeMode");
                CreatureAI::EnterEvadeMode(why);
            }
#endif

#ifdef AC_PLATFORM
            void EnterCombat(Unit* /*who*/)
#else
            void JustEngagedWith(Unit* /*who*/) override

#endif
            {
                //LOG("scripts", "runeworderAI: EnterCombat");
                //me->MonsterYell("Your blood will boil!", LANG_UNIVERSAL, 0);
                Talk(SAY_AGGRO);

                _events.Reset();
                _events.ScheduleEvent(EVENT_CARVER, Milliseconds(urand(3000, 5000)));
                _events.ScheduleEvent(EVENT_RAIN_OF_FIRE, Milliseconds(urand(30000, 40000)));
                _events.ScheduleEvent(EVENT_FRENZY, Milliseconds(FRENZY_TIMER));
            }

            void KilledUnit(Unit* /*victim*/) override
            {
                if (!me->IsAlive())
                    return;
                //if (victim->GetTypeId() != TYPEID_PLAYER)
                //    return;

                if (!(rand() % 3))
                {
                    //me->MonsterYell("Your efforts were futile anyway.", LANG_UNIVERSAL, 0);
                    Talk(SAY_KILL);
                }
            }

            void JustDied(Unit* /*killer*/) override
            {
                //LOG("scripts", "runeworderAI: _Reset from JustDied");
                _Reset();
                //me->MonsterYell("I DID NOT... finish...", LANG_UNIVERSAL, 0);
                Talk(SAY_DEATH);
            }

            void JustSummoned(Creature* /*summon*/) override
            {
                //LOG("scripts", "runeworderAI: JustSummoned %s", summon->GetName().c_str());
            }

            void SummonedCreatureDespawn(Creature* /*summon*/) override
            {
                //LOG("scripts", "runeworderAI: SummonedCreatureDespawn %s", summon->GetName().c_str());
            }

            void UpdateAI(uint32 diff) override
            {
                if (!UpdateVictim())
                    return;

                _events.Update(diff);

                if (me->HasUnitState(UNIT_STATE_CASTING))
                    return;

                while (uint32 eventId = _events.ExecuteEvent())
                {
                    switch (eventId)
                    {
                        case EVENT_CARVER:
                            if (_myphase == PHASE_FRENZY)
                                break;
                            //LOG("scripts", "runeworderAI: ExecuteEvent EVENT_CARVER");
                            if (_carver != nullptr || !_carvePoints.empty())
                            {
                                //LOG("scripts", "runeworderAI: EVENT_CARVER: already in progress, rescheduling");
                                _events.ScheduleEvent(EVENT_CARVER, Milliseconds(2000));
                                break;
                            }
                            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 50.f/*, true*/))
                            {
                                DoCast(target, SPELL_LAUNCH_RUNE_CARVER);
                                //initial point
                                _events.ScheduleEvent(EVENT_POINT_PUT, Milliseconds(CAST_TIME_SUMMON_CARVER + 500));
                                break;
                            }
                            //next attempt
                            //LOG("scripts", "runeworderAI: EVENT_CARVER: no target, rescheduling");
                            _events.ScheduleEvent(EVENT_CARVER, Milliseconds(2000));
                            break;
                        case EVENT_POINT_PUT:
                            //LOG("scripts", "runeworderAI: EVENT_POINT_PUT");
                            if (_carver == nullptr)
                            {
                                //LOG("scripts", "runeworderAI: EVENT_POINT_PUT: no carver, rescheduling");
                                _events.ScheduleEvent(EVENT_POINT_PUT, Milliseconds(500));
                                break;
                            }
                            if (Creature* point = me->SummonCreature(NPC_RUNE_POINT_BUNNY, *_carver))
                            {
                                //LOG("scripts", "runeworderAI: EVENT_POINT_PUT at %.2f %.2f", point->GetPositionX(), point->GetPositionY());
                                _carvePoints.push_back(point);
                                if (_carvePoints.size() > 1)
                                {
                                    //ray
                                    uint32 pos = _carvePoints.size() - 2;
                                    point->AI()->DoCast(_carvePoints[pos], SPELL_VISUAL_RUNE_CHANNEL);
                                }
                            }
                            if (_carvePoints.size() >= MAX_RUNE_POINTS)
                            {
                                _carver->DespawnOrUnsummon();
                                _carver = nullptr;
                                _events.ScheduleEvent(EVENT_RUNE_ASSEMBLE, Milliseconds(1000));
                                break;
                            }
                            _events.ScheduleEvent(EVENT_POINT_PUT, Milliseconds(POINT_PUT_DELAY));
                            break;
                        case EVENT_RUNE_ASSEMBLE:
                            //LOG("scripts", "runeworderAI: ExecuteEvent EVENT_RUNE_ASSEMBLE");
                            _ComputateRuneType();
                            DoCast(me, SPELL_ACTIVATE_RUNE);
                            _events.ScheduleEvent(EVENT_POINTS_UNSUMMON, Milliseconds(CAST_TIME_ACTIVATE_RUNE + 1000));
                            break;
                        case EVENT_POINTS_UNSUMMON:
                            //LOG("scripts", "runeworderAI: ExecuteEvent EVENT_POINTS_UNSUMMON");
                            _UnsummonPoints();
                            _ComputateRunewordType();
                            if (_runewordType != RUNEWORD_INVALID)
                                _events.ScheduleEvent(EVENT_RUNEWORD_ASSEMBLE, Milliseconds(2000));
                            else
                                _events.ScheduleEvent(EVENT_CARVER, Milliseconds(2000));
                            break;
                        case EVENT_RUNEWORD_ASSEMBLE:
                            //LOG("scripts", "runeworderAI: ExecuteEvent EVENT_RUNEWORD_ASSEMBLE");
                            //me->Say("This will do nicely...", LANG_UNIVERSAL, 0);
                            Talk(SAY_RUNEWORD);
                            DoCast(me, SPELL_RUNEWORD);
                            _events.ScheduleEvent(EVENT_CARVER, Milliseconds(CAST_TIME_ACTIVATE_RUNEWORD + 2000));
                            break;
                        case EVENT_FRENZY:
                            //LOG("scripts", "runeworderAI: ExecuteEvent EVENT_FRENZY");
                            if (me->GetHealthPct() > FRENZY_HP_THRESHOLD)
                            {
                                //not yet
                                _events.ScheduleEvent(EVENT_FRENZY, Milliseconds(3000));
                                break;
                            }
                            _myphase = PHASE_FRENZY;
                            //me->RemoveAurasDueToSpell(SPELL_RUNIC_WITHDRAWAL);
                            DoCast(me, SPELL_FRENZY, true);
                            //me->MonsterYell("Leaving already? Your heart is still beating.", LANG_UNIVERSAL, 0);
                            Talk(SAY_FRENZY);
                            // do not do runes if already in frenzy, fire will do the rest
                            _events.Reset();
                            if (_carver)
                            {
                                _carver->DespawnOrUnsummon();
                                _carver = nullptr;
                            }
                            if (!_carvePoints.empty())
                                _UnsummonPoints();
                            // prevent cheating
                            //_events.ScheduleEvent(EVENT_FRENZY, Milliseconds(FRENZY_DURATION));
                            // spells active in frenzy
                            _events.ScheduleEvent(EVENT_RAIN_OF_FIRE, Milliseconds(1000));
                            _events.ScheduleEvent(EVENT_INCINERATE, Milliseconds(5000));
                            break;
                        case EVENT_RAIN_OF_FIRE:
                            //LOG("scripts", "runeworderAI: ExecuteEvent EVENT_RAIN_OF_FIRE");
                            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 50.f/*, true*/))
                            {
                                DoCast(target, SPELL_RAIN_OF_FIRE);
                                _events.ScheduleEvent(EVENT_RAIN_OF_FIRE, Milliseconds(FRENZY_PRED(20000, 6000)));
                                break;
                            }
                            _events.ScheduleEvent(EVENT_RAIN_OF_FIRE, Milliseconds(2000)); //delay next attempt
                            break;
                        case EVENT_INCINERATE:
                            //LOG("scripts", "runeworderAI: ExecuteEvent EVENT_INCINERATE");
                            if (_myphase != PHASE_FRENZY)
                            {
                                TC_LOG_ERROR("scripts", "runeworderAI: EVENT_INCINERATE triggered not in frenzy phase");
                                break;
                            }
                            DoCast(me, SPELL_INCINERATE);
                            _events.ScheduleEvent(EVENT_INCINERATE, Milliseconds(10000));
                            break;
                        case EVENT_ENIGMA_TELEPORT:
                            //LOG("scripts", "runeworderAI: ExecuteEvent EVENT_ENIGMA_TELEPORT");
                            me->CastSpell(me, SPELL_TELEPORT);
                            me->CastSpell(me, SPELL_TELEPORT_ROOT);
                            ResetThreatList();
                            break;
                        default:
                            TC_LOG_ERROR("scripts", "runeworderAI: unhandled event %u", eventId);
                            //ASSERT(false);
                            break;
                    }
                }

                //lovely additions
                bool inMelee = me->IsWithinMeleeRange(me->GetVictim());
                if (inMelee || _speedUpdateTimer >= SPEED_UPDATE_TIMER)
                {
                    if (_speedUpdateTimer)
                        _speedUpdateTimer = 0;
                    if (_isWalking != inMelee)
                    {
                        //TC_LOG_ERROR("scripts", "runeworderAI: speedUpdate update to %u", uint32(inMelee));
                        me->SetSpeedRate(MOVE_RUN, (inMelee && _myphase != PHASE_FRENZY) ? me->GetCreatureTemplate()->speed_walk : me->GetCreatureTemplate()->speed_run);
                        _isWalking = inMelee;
                    }
                } else _speedUpdateTimer += diff;

                //melee attacks or AoE if no nearby target
                if (inMelee)
                {
                    DoMeleeAttackIfReady();
                    if (_incinerateTimer)
                        _incinerateTimer = 0;
                }
                else if (_incinerateFirstDelay > 10000)
                {
                    if (_incinerateTimer >= INCINERATE_CHECK_TIMER)
                    {
                        _incinerateTimer = 0;
                        bool toofar = me->GetDistance(me->GetVictim()) > 15;
                        Unit* target = toofar ? me->SelectNearestTargetInAttackDistance(14) : nullptr;
                        if (target)
                        {
                            float threat = GetThreat(me->GetVictim());
                            //LOG("scripts", "runeworderAI: %i threat from %s to %s",
                            //    int32(threat), me->GetVictim()->GetName().c_str(), target->GetName().c_str());
                            ModifyThreatByPercent(me->GetVictim(), -100);
                            AddThreat(target, threat);
                            return;
                        }
                        else if (toofar)
                        {
                            //LOG("scripts", "runeworderAI: incinerate after %u", _incinerateTimer);
                            _incinerateTimer = 0;
                            DoCastVictim(SPELL_INCINERATE);
                        }
                    } else _incinerateTimer += diff;
                } else _incinerateFirstDelay += diff;
            }

#ifdef AC_PLATFORM
            void SpellHitTarget(Unit* wtarget, SpellInfo const* spellInfo) override
#else
            void SpellHitTarget(WorldObject* wtarget, SpellInfo const* spellInfo) override
#endif
            {
                Unit* target = wtarget ? wtarget->ToUnit() : nullptr;
                if (!target)
                    return;

                uint32 spellId = spellInfo->Id;

                if (spellId == SPELL_LAUNCH_RUNE_CARVER)
                {
                    //LOG("scripts", "runeworderAI: SpellHitTarget SPELL_LAUNCH_RUNE_CARVER");
                    //ASSERT(_carver == nullptr);
                    _carver = me->SummonCreature(NPC_RUNE_CARVER_STALKER, *target);
                    if (_carver == nullptr)
                        return;
                }
                else if (spellId == SPELL_ACTIVATE_RUNE)
                {
                    _ProcessRune();
                }
                else if (spellId == SPELL_RUNEWORD)
                {
                    _ProcessRuneword();
                }
                else if (spellId == SPELL_PERIODIC_TELEPORT_DUMMY)
                {
                    //do not interrupt current spell
                    _events.ScheduleEvent(EVENT_ENIGMA_TELEPORT, Milliseconds(0));
                }
            }

#ifdef AC_PLATFORM
            void DamageTaken(Unit* /*attacker*/, uint32 &damage, DamageEffectType /*damagetype*/, SpellSchoolMask /*damageSchoolMask*/) override
#else
            void DamageTaken(Unit* /*attacker*/, uint32 &damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo*/) override
#endif
            {
                //unkillable state
                if (damage >= me->GetHealth())
                    if (me->HasAura(SPELL_ZOD_SELF, me->GetGUID()))
                        damage = me->GetHealth() - 1;
            }

            void DamageDealt(Unit* /*victim*/, uint32& damage, DamageEffectType damageType) override
            {
                //Life Steal implementation TEMP
                if (damageType == DIRECT_DAMAGE)
                {
                    //does not exist in default dbc
                    int32 totalAmount = me->GetTotalAuraModifierByMiscValue(SPELL_AURA_MOD_SKILL, SKILL_LIFE_STEAL);
                    if (totalAmount > 0)
                    {
                        int32 bp = damage * totalAmount / 100;
                        //LOG("scripts", "runeworderAI: Lifesteal %i pct (%i)", totalAmount, bp);

#ifdef AC_PLATFORM
                        me->CastCustomSpell(me, SPELL_LIFESTEAL, &bp, NULL, NULL, true);
#else
                        CastSpellExtraArgs args(true);
                        args.AddSpellBP0(bp);
                        me->CastSpell(me, SPELL_LIFESTEAL, args);
#endif
                    }
                }
            }

            static RuneTypes _forcedRuneType;

        private:
            EventMap _events;

            uint32 _myphase;
            RuneTypes _runeType;
            RunewordTypes _runewordType;

            uint32 _incinerateTimer;
            uint32 _incinerateFirstDelay;

            uint32 _speedUpdateTimer;
            bool _isWalking;

            Creature* _carver;
            Points _carvePoints;

            void _Reset()
            {
                _events.Reset();
                _myphase = PHASE_NONE;
                _runeType = RUNE_INVALID;
                _runewordType = RUNEWORD_INVALID;

                _forcedRuneType = RUNE_INVALID;

                _incinerateTimer = 0;
                _incinerateFirstDelay = 0;

                _speedUpdateTimer = 0;
                _isWalking = false;

                if (_carver)
                {
                    _carver->DespawnOrUnsummon();
                    _carver = nullptr;
                }

                _UnsummonPoints();

                me->SetSpeedRate(MOVE_RUN, me->GetCreatureTemplate()->speed_run);

                //LOG("scripts", "runeworderAI: _Reset");
            }

            void _UnsummonPoints()
            {
                for (Points::const_iterator cit = _carvePoints.begin(); cit != _carvePoints.end(); ++cit)
                    (*cit)->DespawnOrUnsummon();
                _carvePoints.clear();
            }

            void _ComputateRuneType()
            {
                //LOG("scripts", "runeworderAI: _ComputateRuneType");
                //ASSERT(_carvePoints.size() >= MAX_RUNE_POINTS);

                uint8 dsize = uint8(_carvePoints.size() - 1); //1
                float* dists = new float[dsize]; //1
                memset(dists, 0, dsize * sizeof(float)); //1
                uint8 asize = uint8(_carvePoints.size() - 2); //1
                int32* angles = new int32[asize]; //1
                memset(angles, 0, asize * sizeof(int32)); //1

                std::ostringstream distmsg;
                distmsg.setf(std::ios_base::fixed);
                distmsg.precision(2);
                std::ostringstream anglemsg;
                anglemsg.setf(std::ios_base::fixed);
                anglemsg.precision(1);

                distmsg << "point distances:";
                for (uint32 i = 1; i < _carvePoints.size(); ++i)
                {
                    uint32 j = i - 1;
                    float dist = _carvePoints[i]->GetExactDist2d(_carvePoints[j]);
                    dists[j] = dist; //2
                    distmsg << "\nbetween " << j << " and " << i << ": " << dist;
                }
                //LOG("scripts", distmsg.str().c_str());

                anglemsg << "point angles:";
                for (uint32 i = 1; i < _carvePoints.size() - 1; ++i)
                {
                    uint32 j = i - 1;
                    uint32 k = i + 1;
                    //can be negative
                    float angle = _carvePoints[i]->GetAbsoluteAngle(_carvePoints[j]) - _carvePoints[i]->GetAbsoluteAngle(_carvePoints[k]);
                    int32 degrees = GetDegrees(_carvePoints[j], _carvePoints[i], _carvePoints[k]);
                    bool sign = degrees < 0;
                    angles[j] = degrees; //2
                    anglemsg << "\nbetween " << j << ", " << i << " and " << k << ": " << angle << " (" <<
                        (sign ? "-" : "") << (sign ? uint32(-degrees) : uint32(degrees)) << ")";
                }
                //LOG("scripts", anglemsg.str().c_str());

                //*---*---*---*---*---*---*---*---* // 9 points MAX_RUNE_POINTS
                // --- --- --- --- --- --- --- ---  // 8 dists  MAX_RUNE_POINTS - 1
                //    ^   ^   ^   ^   ^   ^   ^     // 7 angles MAX_RUNE_POINTS - 2
                //LINE                        = 1, //171-180
                //LINE_REV                    = 2, //0-15
                //CURVE_L                     = 3, //141-170
                //CURVE_M /*sharper*/         = 4, //121-140
                //CURVE_H /*even sharper*/    = 5, //101-120
                //TURN_CUBIC                  = 6, //81-100
                //TURN_SHARP                  = 7, //16-80
                CreatureTemplate const* stalkerBase = sObjectMgr->GetCreatureTemplate(NPC_RUNE_CARVER_STALKER);
                float step = baseMoveSpeed[MOVE_RUN]*(POINT_PUT_DELAY*0.001f)*(stalkerBase ? stalkerBase->speed_run : 1.f);
                //Points points = _carvePoints; //copy
                Strokes strokes;
                int32 ang; //in degrees
                int32 absang;
                for (uint8 i = 0; i < asize; ++i)
                {
                    if (dists[i] < step * DIST_THRESHOLD)
                    {
                        //count as single point skipping current, adjust dist and angle
                        //dists is unused
                        dists[i+1] = _carvePoints[i]->GetExactDist2d(_carvePoints[i+2]); //checked no oob
                        //cur angle i is at point i+1
                        if (i < asize - 1) //not last angle
                            angles[i+1] = GetDegrees(_carvePoints[i], _carvePoints[i+2], _carvePoints[i+3]);
                        //LOG("scripts", "!error at %u dist %.2f, step %.2f, next dist %.2f, angle %i!",
                        //    uint32(i), dists[i], step, dists[i+1], (i < asize - 1) ? angles[i+1] : 0);
                        continue;
                    }

                    bool rev = false;
                    ang = angles[i];
                    absang = abs(ang);
                    for (int8 j = int8(strokes.size()-1); j >= 0; --j)
                    {
                        if (strokes[j].type != LINE && strokes[j].type != LINE_REV)
                        {
                            rev = (angles[j] < 0) != (angles[i] < 0);
                            break;
                        }
                    }
                    //line
                    if (absang >= 0 && absang <= 15)
                    {
                        //cannot be rev
                        rev = false;
                        strokes.push_back(Stroke(LINE_REV, rev));
                        continue;
                    }
                    //line rev
                    if (absang >= 171 && absang <= 180)
                    {
                        //cannot be rev
                        rev = false;
                        strokes.push_back(Stroke(LINE, rev));
                        continue;
                    }
                    //curve low
                    if (absang >= 141 && absang <= 170)
                    {
                        strokes.push_back(Stroke(CURVE_L, rev));
                        continue;
                    }
                    //curve medium
                    if (absang >= 121 && absang <= 140)
                    {
                        strokes.push_back(Stroke(CURVE_M, rev));
                        continue;
                    }
                    //curve high
                    if (absang >= 101 && absang <= 120)
                    {
                        strokes.push_back(Stroke(CURVE_H, rev));
                        continue;
                    }
                    //cubic
                    if (absang >= 81 && absang <= 100)
                    {
                        strokes.push_back(Stroke(TURN_CUBIC, rev));
                        continue;
                    }
                    //sharp
                    if (absang >= 16 && absang <= 80)
                    {
                        strokes.push_back(Stroke(TURN_SHARP, rev));
                        continue;
                    }
                }
                delete[] dists;
                delete[] angles;

                std::ostringstream strokesmsg;
                strokesmsg << "Strokes:";
                for (Strokes::const_iterator cit = strokes.begin(); cit != strokes.end(); ++cit)
                {
                    strokesmsg << " " << uint32((*cit).type) << "(" << ((*cit).reverse ? "r" : "") << ")";
                }
                //LOG("scripts", strokesmsg.str().c_str());

                std::vector<RuneTypes> matches;
                if (strokes.size() >= MIN_RUNE_PATTERN_LENGTH)
                {
                    //check rune patterns
                    for (uint8 i = RUNE_EL1; i < MAX_RUNE_TYPES; ++i)
                    {
                        if (RunePatterns[i].Matches(strokes))
                            matches.push_back(RuneTypes(i));
                    }
                }

                std::ostringstream matchesStr;
                matchesStr << "Matches found:";
                for (std::vector<RuneTypes>::const_iterator cit = matches.begin(); cit != matches.end(); ++cit)
                    matchesStr << " " << uint32(*cit);
                //LOG("scripts", matchesStr.str().c_str());

                _runeType = RUNE_INVALID;

                //debug
                if (_forcedRuneType != RUNE_INVALID)
                {
                    _runeType = _forcedRuneType;
                    _forcedRuneType = RUNE_INVALID;
                    return;
                }

                if (matches.empty())
                    return;

                if (matches.size() <= 1)
                {
                    _runeType = matches.front();
                    //LOG("scripts", "single match %u", uint32(_runeType));
                    return;
                }

                //weighted list to shift roll result towards higher runes
                typedef std::unordered_map<RuneTypes /*runetype*/, uint32 /*total weight*/> RunePrioMap;
                RunePrioMap prioMap;
                int32 roll_min = 1, roll_max = 0;
                uint32 weight;
                for (std::vector<RuneTypes>::const_iterator cit = matches.begin(); cit != matches.end(); ++cit)
                {
                    weight = *cit <= RUNE_THUL ? WEIGHT_RUNE_LOW : *cit <= RUNE_LEM4 ? WEIGHT_RUNE_MID : WEIGHT_RUNE_HI;
                    prioMap[*cit] += weight;
                    roll_max += weight;
                }

                //do roll
                int32 roll = irand(roll_min, roll_max);
                //LOG("scripts", "rolled %i (%i-%i among %u matches)", roll, roll_min, roll_max, uint32(matches.size()));
                for (RunePrioMap::const_iterator cit = prioMap.begin(); cit != prioMap.end(); ++cit)
                {
                    roll -= cit->second;
                    //LOG("scripts", "roll reduced to %i", roll);
                    if (roll <= 0)
                    {
                        _runeType = cit->first;
                        //LOG("scripts", "chosen rune %u!", uint32(_runeType));
                        break;
                    }
                }

                //ASSERT(_runeType != RUNE_INVALID);
            }

            void _ProcessRune()
            {
                uint32 spellId1;
                uint32 spellId2 = 0;
                std::string runeName;
                switch (_runeType)
                {
                    case RUNE_EL1: case RUNE_EL2:
                        runeName = "EL!";   spellId1 = SPELL_EL_SELF;   spellId2 = SPELL_EL_TARGETS;   break;
                    case RUNE_ELD1: case RUNE_ELD2:
                        runeName = "ELD!";  spellId1 = SPELL_ELD_SELF;  spellId2 = SPELL_ELD_TARGETS;  break;
                    case RUNE_TIR1: case RUNE_TIR2:
                        runeName = "TIR!";  spellId1 = SPELL_TIR_SELF;  spellId2 = SPELL_TIR_TARGETS;  break;
                    case RUNE_NEF:
                        runeName = "NEF!";  spellId1 = SPELL_NEF_SELF;                                 break;
                    case RUNE_ETH1: case RUNE_ETH2:
                        runeName = "ETH!";  spellId1 = SPELL_ETH_SELF;  spellId2 = SPELL_ETH_TARGETS;  break;
                    case RUNE_ITH1: case RUNE_ITH2:
                        runeName = "ITH!";  spellId1 = SPELL_ITH_SELF;  spellId2 = SPELL_ITH_TARGETS;  break;
                    case RUNE_TAL1: case RUNE_TAL2: case RUNE_TAL3:
                        runeName = "TAL!";  spellId1 = SPELL_TAL_SELF;  spellId2 = SPELL_TAL_TARGETS;  break;
                    case RUNE_RAL:
                        runeName = "RAL!";  spellId1 = SPELL_RAL_SELF;  spellId2 = SPELL_RAL_TARGETS;  break;
                    case RUNE_ORT1: case RUNE_ORT2: case RUNE_ORT3:
                        runeName = "ORT!";  spellId1 = SPELL_ORT_SELF;  spellId2 = SPELL_ORT_TARGETS;  break;
                    case RUNE_THUL:
                        runeName = "THUL!"; spellId1 = SPELL_THUL_SELF; spellId2 = SPELL_THUL_TARGETS; break;
                    case RUNE_AMN:
                        runeName = "AMN!";  spellId1 = SPELL_AMN_SELF;                                 break;
                    case RUNE_SOL:
                        runeName = "SOL!";  spellId1 = SPELL_SOL_SELF;  spellId2 = SPELL_SOL_TARGETS;  break;
                    case RUNE_SHAEL1: case RUNE_SHAEL2:
                        runeName = "SHAEL!";spellId1 = SPELL_SHAEL_SELF;spellId2 = SPELL_SHAEL_TARGETS;break;
                    case RUNE_DOL1: case RUNE_DOL2:
                        runeName = "DOL!";  spellId1 = SPELL_DOL_SELF;                                 break;
                    case RUNE_HEL1: case RUNE_HEL2:
                        runeName = "HEL!";  spellId1 = SPELL_HEL_SELF;  spellId2 = SPELL_HEL_TARGETS;  break;
                    case RUNE_IO1: case RUNE_IO2: case RUNE_IO3:
                        runeName = "IO!";   spellId1 = SPELL_IO_SELF;   spellId2 = SPELL_IO_TARGETS;   break;
                    case RUNE_LUM:
                        runeName = "LUM!";  spellId1 = SPELL_LUM_SELF;  spellId2 = SPELL_LUM_TARGETS;  break;
                    case RUNE_KO:
                        runeName = "KO!";   spellId1 = SPELL_KO_SELF;   spellId2 = SPELL_KO_TARGETS;   break;
                    case RUNE_FAL1: case RUNE_FAL2: case RUNE_FAL3: case RUNE_FAL4: case RUNE_FAL5: case RUNE_FAL6:
                        runeName = "FAL!";  spellId1 = SPELL_FAL_SELF;  spellId2 = SPELL_FAL_TARGETS;  break;
                    case RUNE_LEM1: case RUNE_LEM2: case RUNE_LEM3: case RUNE_LEM4:
                        runeName = "LEM!";  spellId1 = SPELL_LEM_SELF;  spellId2 = SPELL_LEM_TARGETS;  break;
                    case RUNE_PUL:
                        runeName = "PUL!";  spellId1 = SPELL_PUL_SELF;  spellId2 = SPELL_PUL_TARGETS;  break;
                    case RUNE_UM1: case RUNE_UM2: case RUNE_UM3:
                        runeName = "UM!";   spellId1 = SPELL_UM_SELF;   spellId2 = SPELL_UM_TARGETS;   break;
                    case RUNE_MAL1: case RUNE_MAL2:
                        runeName = "MAL!";  spellId1 = SPELL_MAL_SELF;  spellId2 = SPELL_MAL_TARGETS;  break;
                    case RUNE_IST:
                        runeName = "IST!";  spellId1 = SPELL_IST_SELF;                                 break;
                    case RUNE_GUL1: case RUNE_GUL2: case RUNE_GUL3:
                        runeName = "GUL!";  spellId1 = SPELL_GUL_SELF;  spellId2 = SPELL_GUL_TARGETS;  break;
                    case RUNE_VEX1: case RUNE_VEX2: case RUNE_VEX3: case RUNE_VEX4: case RUNE_VEX5: case RUNE_VEX6:
                        runeName = "VEX!";  spellId1 = SPELL_VEX_SELF;  spellId2 = SPELL_VEX_TARGETS;  break;
                    case RUNE_OHM:
                        runeName = "OHM!";  spellId1 = SPELL_OHM_SELF;  spellId2 = SPELL_OHM_TARGETS;  break;
                    case RUNE_LO1: case RUNE_LO2: case RUNE_LO3: case RUNE_LO4:
                        runeName = "LO!";   spellId1 = SPELL_LO_SELF;   spellId2 = SPELL_LO_TARGETS;   break;
                    case RUNE_SUR1: case RUNE_SUR2:
                        runeName = "SUR!";  spellId1 = SPELL_SUR_SELF;  spellId2 = SPELL_SUR_TARGETS;  break;
                    case RUNE_BER:
                        runeName = "BER!";  spellId1 = SPELL_BER_SELF;                                 break;
                    case RUNE_JAH1: case RUNE_JAH2: case RUNE_JAH3: case RUNE_JAH4: case RUNE_JAH5:
                        runeName = "JAH!";  spellId1 = SPELL_JAH_SELF;  spellId2 = SPELL_JAH_TARGETS;  break;
                    case RUNE_CHAM1: case RUNE_CHAM2: case RUNE_CHAM3: case RUNE_CHAM4: case RUNE_CHAM5:
                    case RUNE_CHAM6: case RUNE_CHAM7: case RUNE_CHAM8: case RUNE_CHAM9:
                        runeName = "CHAM!"; spellId1 = SPELL_CHAM_SELF;                                break;
                    case RUNE_ZOD1: case RUNE_ZOD2:
                        runeName = "ZOD!";  spellId1 = SPELL_ZOD_SELF;  spellId2 = SPELL_ZOD_TARGETS;  break;
                    case RUNE_INVALID:
                        spellId1 = SPELL_RUNIC_WITHDRAWAL;                                             break;
                    default:
                        TC_LOG_ERROR("scripts", "runeworderAI: _ProcessRune: unknown _runeType %u!", uint32(_runeType));
                        spellId1 = SPELL_RUNIC_WITHDRAWAL;                                             break;
                }
                //LOG("scripts", "runeworderAI: _ProcessRune rune %u (%s) spellId1 %u, spellId2 %u",
                //    uint32(_runeType), runeName.c_str(), spellId1, spellId2);

                if (_runeType == RUNE_INVALID)
                {
                    //enrage and cast random rune anyway
                    DoCast(me, spellId1, true);
                    Talk(SAY_RUNE_FAIL);
                    spellId1 = urand(SPELL_EL_SELF, SPELL_ZOD_SELF);
                    switch (spellId1)
                    {
                        //these runes only have self effect
                        case SPELL_NEF_SELF: case SPELL_AMN_SELF: case SPELL_DOL_SELF: case SPELL_IST_SELF: case SPELL_BER_SELF: case SPELL_CHAM_SELF:
                            break;
                        default:
                            //hacky
                            spellId2 = spellId1 + 200; // ex. SPELL_EL_SELF -> SPELL_EL_TARGETS
                            break;
                    }
                }
                else
                    me->Say(runeName.c_str(), LANG_UNIVERSAL, 0);

                DoCast(me, spellId1, true);
                if (spellId2)
                    DoCast(me, spellId2, true);

                //Do visuals
                //LOG("scripts", "runeworderAI: _ProcessRune SPELL_VISUAL_RUNE_ACTIVATION");
                for (Points::const_iterator cit = _carvePoints.begin(); cit != _carvePoints.end(); ++cit)
                    (*cit)->AI()->DoCast(*cit, SPELL_VISUAL_RUNE_ACTIVATION, true);
            }

            void _ComputateRunewordType()
            {
                //find what runes and runewords we have
                RuneSpellVec myRunes;
                std::vector<uint32> myRunewords;
                Unit::AuraMap const& runeAuras = me->GetOwnedAuras(); //normally only runes and runewords in here
                for (Unit::AuraMap::const_iterator itr = runeAuras.begin(); itr != runeAuras.end(); ++itr)
                {
                    switch (itr->second->GetSpellInfo()->Id)
                    {
                        //runeword spells
                        case SPELL_STEEL:           myRunewords.push_back(RUNEWORD_STEEL);          break;
                        case SPELL_NADIR:           myRunewords.push_back(RUNEWORD_NADIR);          break;
                        case SPELL_MALICE:          myRunewords.push_back(RUNEWORD_MALICE);         break;
                        case SPELL_STEALTH:         myRunewords.push_back(RUNEWORD_STEALTH);        break;
                        case SPELL_LEAF:            myRunewords.push_back(RUNEWORD_LEAF);           break;
                        case SPELL_ZEPHYR:          myRunewords.push_back(RUNEWORD_ZEPHYR);         break;
                        case SPELL_ANCIENTS_PLEDGE: myRunewords.push_back(RUNEWORD_ANCIENTS_PLEDGE);break;
                        case SPELL_STRENGTH:        myRunewords.push_back(RUNEWORD_STRENGTH);       break;
                        case SPELL_EDGE:            myRunewords.push_back(RUNEWORD_EDGE);           break;
                        case SPELL_KINGS_GRACE:     myRunewords.push_back(RUNEWORD_KINGS_GRACE);    break;
                        case SPELL_RADIANCE:        myRunewords.push_back(RUNEWORD_RADIANCE);       break;
                        case SPELL_LORE:            myRunewords.push_back(RUNEWORD_LORE);           break;
                        case SPELL_RHYME:           myRunewords.push_back(RUNEWORD_RHYME);          break;
                        case SPELL_PEACE:           myRunewords.push_back(RUNEWORD_PEACE);          break;
                        case SPELL_MYTH:            myRunewords.push_back(RUNEWORD_MYTH);           break;
                        case SPELL_BLACK:           myRunewords.push_back(RUNEWORD_BLACK);          break;
                        case SPELL_WHITE:           myRunewords.push_back(RUNEWORD_WHITE);          break;
                        case SPELL_SMOKE:           myRunewords.push_back(RUNEWORD_SMOKE);          break;
                        case SPELL_SPLENDOR:        myRunewords.push_back(RUNEWORD_SPLENDOR);       break;
                        case SPELL_MELODY:          myRunewords.push_back(RUNEWORD_MELODY);         break;
                        case SPELL_LIONHEART:       myRunewords.push_back(RUNEWORD_LIONHEART);      break;
                        case SPELL_TREACHERY:       myRunewords.push_back(RUNEWORD_TREACHERY);      break;
                        case SPELL_WEALTH:          myRunewords.push_back(RUNEWORD_WEALTH);         break;
                        case SPELL_LAWBRINGER:      myRunewords.push_back(RUNEWORD_LAWBRINGER);     break;
                        case SPELL_ENLIGHTENMENT:   myRunewords.push_back(RUNEWORD_ENLIGHTENMENT);  break;
                        case SPELL_CRESCENT_MOON:   myRunewords.push_back(RUNEWORD_CRESCENT_MOON);  break;
                        case SPELL_DURESS:          myRunewords.push_back(RUNEWORD_DURESS);         break;
                        case SPELL_GLOOM:           myRunewords.push_back(RUNEWORD_GLOOM);          break;
                        case SPELL_PRUDENCE:        myRunewords.push_back(RUNEWORD_PRUDENCE);       break;
                        case SPELL_RAIN:            myRunewords.push_back(RUNEWORD_RAIN);           break;
                        case SPELL_VENOM:           myRunewords.push_back(RUNEWORD_VENOM);          break;
                        //case SPELL_SANCTUARY:       myRunewords.push_back(RUNEWORD_SANCTUARY);      break;
                        case SPELL_DELIRIUM:        myRunewords.push_back(RUNEWORD_DELIRIUM);       break;
                        case SPELL_PRINCIPLE:       myRunewords.push_back(RUNEWORD_PRINCIPLE);      break;
                        case SPELL_CHAOS:           myRunewords.push_back(RUNEWORD_CHAOS);          break;
                        case SPELL_WIND:            myRunewords.push_back(RUNEWORD_WIND);           break;
                        case SPELL_DRAGON:          myRunewords.push_back(RUNEWORD_DRAGON);         break;
                        case SPELL_DREAM:           myRunewords.push_back(RUNEWORD_DREAM);          break;
                        case SPELL_FURY:            myRunewords.push_back(RUNEWORD_FURY);           break;
                        case SPELL_ENIGMA:          myRunewords.push_back(RUNEWORD_ENIGMA);         break;

                        //rune spells
                        case SPELL_EL_SELF:    case SPELL_ELD_SELF:  case SPELL_TIR_SELF: case SPELL_NEF_SELF:
                        case SPELL_ETH_SELF:   case SPELL_ITH_SELF:  case SPELL_TAL_SELF: case SPELL_RAL_SELF:
                        case SPELL_ORT_SELF:   case SPELL_THUL_SELF: case SPELL_AMN_SELF: case SPELL_SOL_SELF:
                        case SPELL_SHAEL_SELF: case SPELL_DOL_SELF:  case SPELL_HEL_SELF: case SPELL_IO_SELF:
                        case SPELL_LUM_SELF:   case SPELL_KO_SELF:   case SPELL_FAL_SELF: case SPELL_LEM_SELF:
                        case SPELL_PUL_SELF:   case SPELL_UM_SELF:   case SPELL_MAL_SELF: case SPELL_IST_SELF:
                        case SPELL_GUL_SELF:   case SPELL_VEX_SELF:  case SPELL_OHM_SELF: case SPELL_LO_SELF:
                        case SPELL_SUR_SELF:   case SPELL_BER_SELF:  case SPELL_JAH_SELF: case SPELL_CHAM_SELF:
                        case SPELL_ZOD_SELF:
                            myRunes.push_back(RuneworderSpells(itr->second->GetSpellInfo()->Id));
                            break;

                        default:
                            break;
                    }
                }

                //LOG("scripts", "found %u rune spells", uint32(myRunes.size()));

                if (myRunes.size() < MIN_RUNEWORD_LENGTH)
                    return;

                std::vector<RunewordTypes> RWmatches;
                for (uint8 i = RUNEWORD_STEEL; i < MAX_RUNEWORD_TYPES; ++i)
                {
                    if (RunewordPatterns[i].Contains(myRunes))
                        RWmatches.push_back(RunewordTypes(i));
                }

                std::ostringstream RWmatchesStr;
                RWmatchesStr << "RWMatches found:";
                for (std::vector<RunewordTypes>::const_iterator cit = RWmatches.begin(); cit != RWmatches.end(); ++cit)
                    RWmatchesStr << " " << uint32(*cit);
                //LOG("scripts", RWmatchesStr.str().c_str());

                for (std::vector<uint32>::const_iterator cir = myRunewords.begin(); cir  != myRunewords.end(); ++cir)
                {
                    for (std::vector<RunewordTypes>::const_iterator cit = RWmatches.begin(); cit != RWmatches.end(); ++cit)
                    {
                        if (*cit == *cir)
                        {
                            RWmatches.erase(cit);
                            //LOG("scripts", "removing %u from matches...", uint32(*cir));
                            break;
                        }
                    }
                }

                _runewordType = RUNEWORD_INVALID;
                if (RWmatches.empty())
                    return;

                if (RWmatches.size() < 2)
                {
                    _runewordType = RWmatches.front();
                    //LOG("scripts", "single match %u", uint32(_runewordType));
                    return;
                }

                //roll a runeword
                int32 roll = irand(1,100 * RWmatches.size());
                for (std::vector<RunewordTypes>::const_iterator cit = RWmatches.begin(); cit != RWmatches.end(); ++cit)
                {
                    roll -= 100;
                    //LOG("scripts", "roll reduced to %i", roll);
                    if (roll <= 0)
                    {
                        _runewordType = *cit;
                        //LOG("scripts", "chosen runeword %u!", uint32(_runewordType));
                        break;
                    }
                }
            }

            void _ProcessRuneword()
            {
                uint32 spellId1 = 0, spellId2 = 0;
                std::string runewordName;
                switch (_runewordType)
                {
                    case RUNEWORD_STEEL:
                        runewordName = "STEEL!"; spellId1 = SPELL_STEEL; break;
                    case RUNEWORD_NADIR:
                        runewordName = "NADIR!"; spellId1 = SPELL_NADIR; break;
                    case RUNEWORD_MALICE:
                        runewordName = "MALICE!"; spellId1 = SPELL_MALICE; spellId2 = SPELL_MALICE_TRIGGERED; break;
                    case RUNEWORD_STEALTH:
                        runewordName = "STEALTH!"; spellId1 = SPELL_STEALTH; break;
                    case RUNEWORD_LEAF:
                        runewordName = "LEAF!"; spellId1 = SPELL_LEAF; break;
                    case RUNEWORD_ZEPHYR:
                        runewordName = "ZEPHYR!"; spellId1 = SPELL_ZEPHYR; break;
                    case RUNEWORD_ANCIENTS_PLEDGE:
                        runewordName = "ANCIENT'S PLEDGE!"; spellId1 = SPELL_ANCIENTS_PLEDGE; break;
                    case RUNEWORD_STRENGTH:
                        runewordName = "STRENGTH!"; spellId1 = SPELL_STRENGTH; break;
                    case RUNEWORD_EDGE:
                        runewordName = "EDGE!"; spellId1 = SPELL_EDGE; spellId2 = SPELL_THORNS_AURA; break;
                    case RUNEWORD_KINGS_GRACE:
                        runewordName = "KING's GRACE!"; spellId1 = SPELL_KINGS_GRACE; break;
                    case RUNEWORD_RADIANCE:
                        runewordName = "RADIANCE!"; spellId1 = SPELL_RADIANCE; break;
                    case RUNEWORD_LORE:
                        runewordName = "LORE!"; spellId1 = SPELL_LORE; break;
                    case RUNEWORD_RHYME:
                        runewordName = "RHYME!"; spellId1 = SPELL_RHYME; break;
                    case RUNEWORD_PEACE:
                        runewordName = "PEACE!"; spellId1 = SPELL_PEACE; break;
                    case RUNEWORD_MYTH:
                        runewordName = "MYTH!"; spellId1 = SPELL_MYTH; break;
                    case RUNEWORD_BLACK:
                        runewordName = "BLACK!"; spellId1 = SPELL_BLACK; break;
                    case RUNEWORD_WHITE:
                        runewordName = "WHITE!"; spellId1 = SPELL_WHITE; break;
                    case RUNEWORD_SMOKE:
                        runewordName = "SMOKE!"; spellId1 = SPELL_SMOKE; break;
                    case RUNEWORD_SPLENDOR:
                        runewordName = "SPLENDOR!"; spellId1 = SPELL_SPLENDOR; break;
                    case RUNEWORD_MELODY:
                        runewordName = "MELODY!"; spellId1 = SPELL_MELODY; break;
                    case RUNEWORD_LIONHEART:
                        runewordName = "LIONHEART!"; spellId1 = SPELL_LIONHEART; break;
                    case RUNEWORD_TREACHERY:
                        runewordName = "TREACHERY!"; spellId1 = SPELL_TREACHERY; break;
                    case RUNEWORD_WEALTH:
                        runewordName = "WEALTH!"; spellId1 = SPELL_WEALTH; break;
                    case RUNEWORD_LAWBRINGER:
                        runewordName = "LAWBRINGER!"; spellId1 = SPELL_LAWBRINGER; break;
                    case RUNEWORD_ENLIGHTENMENT:
                        runewordName = "ENLIGHTENMENT!"; spellId1 = SPELL_ENLIGHTENMENT; break;
                    case RUNEWORD_CRESCENT_MOON:
                        runewordName = "CRESCENT MOON!"; spellId1 = SPELL_CRESCENT_MOON; break;
                    case RUNEWORD_DURESS:
                        runewordName = "DURESS!"; spellId1 = SPELL_DURESS; break;
                    case RUNEWORD_GLOOM:
                        runewordName = "GLOOM!"; spellId1 = SPELL_GLOOM; break;
                    case RUNEWORD_PRUDENCE:
                        runewordName = "PRUDENCE!"; spellId1 = SPELL_PRUDENCE; break;
                    case RUNEWORD_RAIN:
                        runewordName = "RAIN!"; spellId1 = SPELL_RAIN; break;
                    case RUNEWORD_VENOM:
                        runewordName = "VENOM!"; spellId1 = SPELL_VENOM; break;
                    //case RUNEWORD_SANCTUARY:
                    //    runewordName = "SANCTUARY!"; spellId1 = SPELL_SANCTUARY; break;
                    case RUNEWORD_DELIRIUM:
                        runewordName = "DELIRIUM!"; spellId1 = SPELL_DELIRIUM; break;
                    case RUNEWORD_PRINCIPLE:
                        runewordName = "PRINCIPLE!"; spellId1 = SPELL_PRINCIPLE; break;
                    case RUNEWORD_CHAOS:
                        runewordName = "CHAOS!"; spellId1 = SPELL_CHAOS; break;
                    case RUNEWORD_WIND:
                        runewordName = "WIND!"; spellId1 = SPELL_WIND; break;
                    case RUNEWORD_DRAGON:
                        runewordName = "DRAGON!"; spellId1 = SPELL_DRAGON; break;
                    case RUNEWORD_DREAM:
                        runewordName = "DREAM!"; spellId1 = SPELL_DREAM; break;
                    case RUNEWORD_FURY:
                        runewordName = "FURY!"; spellId1 = SPELL_FURY; break;
                    case RUNEWORD_ENIGMA:
                        runewordName = "ENIGMA!"; spellId1 = SPELL_ENIGMA; break;
                    case RUNEWORD_INVALID:
                    default:
                        TC_LOG_ERROR("scripts", "runeworderAI: _ProcessRuneword: failed to complete a runeword %u", uint32(_runewordType));
                        return;
                }
                //LOG("scripts", "runeworderAI: _ProcessRuneword runeword %u (%s) spellId1 %u, spellId2 %u",
                //    uint32(_runewordType), runewordName.c_str(), spellId1, spellId2);

                if (!spellId1)
                {
                    TC_LOG_ERROR("scripts", "runeworderAI: _ProcessRuneword: no spellid1 for runeword %u", uint32(_runewordType));
                    return;
                }

                DoCast(me, SPELL_COSMETIC_SCALE, true);
                DoCast(me, spellId1, true);
                if (spellId2)
                    DoCast(me, spellId2, true);

                me->Say(runewordName.c_str(), LANG_UNIVERSAL, 0);

                //refresh runeword's runes duration
                if (Aura const* runewordAura = me->GetAura(spellId1, me->GetGUID()))
                {
                    RunewordPattern const& myPattern = RunewordPatterns[_runewordType];
                    RuneworderSpells const* runeSpells = myPattern.GetRuneSpellList();
                    for (uint8 i = 0; i < myPattern.GetSize(); ++i)
                    {
                        Aura* runeAura = me->GetAura(runeSpells[i], me->GetGUID());
                        if (!runeAura)
                            continue;

                        runeAura->SetDuration(runewordAura->GetDuration());
                        runeAura->SetMaxDuration(runewordAura->GetMaxDuration());
                        //LOG("scripts", "runeworderAI: _ProcessRuneword: refreshing %u", runeAura->GetId());
                    }
                }
            }
        };

        CreatureAI* GetAI(Creature* creature) const
        {
            return new boss_runeworderAI(creature);
        }
};

RuneTypes boss_runeworder::boss_runeworderAI::_forcedRuneType = RUNE_INVALID;
/*
using namespace Trinity::ChatCommands;
#define GM_COMMANDS rbac::RBACPermissions(197)
class runeworder_commandscript : public CommandScript
{
public:
    runeworder_commandscript() : CommandScript("brm_commandscript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable runeworderCommandTable =
        {
            { "spellvis",   HandleSpellVisCommand,      GM_COMMANDS,    Console::No  },
            { "forcerune",  HandleForceRuneCommand,     GM_COMMANDS,    Console::Yes },
        };
        static ChatCommandTable commandTable =
        {
            { "runeworder", runeworderCommandTable                                   },
        };
        return commandTable;
    }

    static bool HandleForceRuneCommand(ChatHandler* handler, const char* args)
    {
        uint32 newRune = (uint32)((*args) ? atoi((char*)args) : RUNE_INVALID);

        if (newRune >= MAX_RUNE_TYPES)
            TC_LOG_ERROR("scripts", "invalid rune %u", newRune);

        boss_runeworder::boss_runeworderAI::_forcedRuneType = RuneTypes(newRune);

        TC_LOG_INFO("scripts", "Next rune set to %u", newRune);
        return true;
    }

    static bool HandleSpellVisCommand(ChatHandler* handler, const char* args)
    {
        Unit* target = handler->GetSession()->GetPlayer()->GetSelectedUnit();
        if (!target)
        {
            handler->SendSysMessage("No target selected");
            return true;
        }

        static uint32 kit = 0;
        if (*args)
            kit = (uint32)atoi((char*)args);
        else
            ++kit;

        if (!(kit % 10))
            handler->PSendSysMessage("%u...", kit);

        target->SendPlaySpellVisual(kit);
        return true;
    }
};
*/
//500054 - Crushing Blow (triggered)
//500067 - Static Field (triggered)
class spell_reduce_health : public SpellScriptLoader
{
    public:
        spell_reduce_health() : SpellScriptLoader("spell_reduce_health") { }

        class spell_reduce_health_SpellScript : public SpellScript
        {
            PrepareSpellScript(spell_reduce_health_SpellScript);

            bool Validate(SpellInfo const* /*spell*/) override
            {
                if (!sSpellMgr->GetSpellInfo(SPELL_STATIC_FIELD_TRIGGERED))
                    return false;
                SpellInfo* spellInfo = const_cast<SpellInfo*>(sSpellMgr->GetSpellInfo(SPELL_CRUSHING_BLOW_TRIGGERED));
                if (!spellInfo)
                    return false;

                //could not find a better way of fixing this (non-bleed physical ability ignoring armor)
                spellInfo->AttributesCu |= SPELL_ATTR0_CU_IGNORE_ARMOR;
                return true;
            }

            bool Load() override
            {
                return true;
            }

            void HandleDamageCalc(SpellEffIndex /*effIndex*/)
            {
                PreventHitDamage();
                //int32 pct = GetSpellInfo()->Effects[effIndex].CalcValue();
                //pct of current health
                Unit const* target = GetHitUnit();
                if (target && target->IsAlive())
                    SetHitDamage(CalculatePct(target->GetHealth(), GetEffectValue()));

                //Crushing Blow damage is 25%
                //if (target && target->IsAlive())
                //    SetHitDamage(target->GetHealth() / 4);
            }

            void Register() override
            {
                OnEffectHitTarget += SpellEffectFn(spell_reduce_health_SpellScript::HandleDamageCalc, EFFECT_0, SPELL_EFFECT_SCHOOL_DAMAGE);
            }
        };

        SpellScript* GetSpellScript() const override
        {
            return new spell_reduce_health_SpellScript();
        }
};

//500059 - Thorns Aura (linked to 500108 - Edge)
class spell_thorns_aura : public SpellScriptLoader
{
    public:
        spell_thorns_aura() : SpellScriptLoader("spell_thorns_aura") { }

        class spell_thorns_aura_AuraScript : public AuraScript
        {
            PrepareAuraScript(spell_thorns_aura_AuraScript);

            bool Validate(SpellInfo const* /*spellInfo*/) override
            {
                if (!sSpellMgr->GetSpellInfo(SPELL_EDGE) ||
                    !sSpellMgr->GetSpellInfo(SPELL_THORNS_AURA) ||
                    !sSpellMgr->GetSpellInfo(SPELL_THORNS_AURA_DAMAGE))
                    return false;
                return true;
            }

            bool Load() override
            {
                return true;
            }

            void HandleProc(AuraEffect const* /*aurEff*/, ProcEventInfo& eventInfo)
            {
                PreventDefaultAction();
                Unit* target = eventInfo.GetActor();
                if (!target)
                    return;

                SpellInfo const* reflectInfo = sSpellMgr->GetSpellInfo(SPELL_EDGE);
#ifdef AC_PLATFORM
                int32 pct = reflectInfo->Effects[EFFECT_2].CalcValue();
#else
                int32 pct = reflectInfo->GetEffect(EFFECT_2).CalcValue();
#endif

                int32 damage = int32(CalculatePct(eventInfo.GetDamageInfo()->GetDamage(), pct));

#ifdef AC_PLATFORM
                GetTarget()->CastCustomSpell(target, SPELL_THORNS_AURA_DAMAGE, &damage, NULL, NULL, true);
#else
                CastSpellExtraArgs args(true);
                args.AddSpellBP0(damage);
                GetTarget()->CastSpell(target, SPELL_THORNS_AURA_DAMAGE, args);
#endif
            }

            void Register() override
            {
                OnEffectProc += AuraEffectProcFn(spell_thorns_aura_AuraScript::HandleProc, EFFECT_0, SPELL_AURA_PROC_TRIGGER_SPELL);
            }
        };

        AuraScript* GetAuraScript() const override
        {
            return new spell_thorns_aura_AuraScript();
        }
};

constexpr void RUNE_PATTERN_TESTS()
{
#define TEST_RUNE_PATTERN(p, ...) \
    constexpr std::array arr_##p { __VA_ARGS__ }; \
    constexpr bool val_##p = RunePattern::Matches(arr_##p, p); \
    static_assert(val_##p)

    //ST_CURVE_MH, ST_LINE_OR_NOTHING, ST_CUBIC_OR_SHARP, ST_CURVE_MH_R, ST_CUBIC_OR_SHARP_R, ST_LINE_OR_NOTHING, ST_CURVE_MH
    TEST_RUNE_PATTERN(Rune_ITH2, Stroke(CURVE_M, false), Stroke(TURN_SHARP, false), Stroke(CURVE_M, true), Stroke(TURN_SHARP, true), Stroke(CURVE_M, false));
    //ST_CURVE_MH, ST_CURVE_LM, ST_CUBIC_OR_CURVE_H_R, ST_SHARP_R, ST_LINE, ST_CURVE_LM, ST_CURVE_LM
    TEST_RUNE_PATTERN(Rune_EL1, Stroke(CURVE_H, false), Stroke(CURVE_L, false), Stroke(TURN_CUBIC, true), Stroke(TURN_SHARP, true), Stroke(LINE, false), Stroke(CURVE_M, false), Stroke(TURN_SHARP, true));
#undef TEST_RUNE_PATTERN
}

constexpr void RUNEWORD_PATTERN_TESTS()
{
#define TEST_RUNEWORD_PATTERN(p, ...) \
    constexpr std::array arr_##p { __VA_ARGS__ }; \
    constexpr bool val_##p = RunewordPattern::Contains<p.size>(arr_##p, p); \
    static_assert(val_##p)

    TEST_RUNEWORD_PATTERN(Runeword_RADIANCE, SPELL_NEF_SELF, SPELL_SOL_SELF, SPELL_ITH_SELF);

#undef TEST_RUNEWORD_PATTERN
}

void AddSC_boss_runeworder()
{
    new boss_runeworder();
    new npc_rune_carver();
    new npc_rune_bunny();
    new spell_reduce_health();
    new spell_thorns_aura();
    //new runeworder_commandscript();
}
