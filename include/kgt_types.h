/* Generated from the Ghidra database (bootstrap); hand-maintained from here on.
 *
 * 64-bit port (win64 branch): the offsets in the member comments are those of the original 32-bit
 * layout, which is also the layout of the data files.  Structures that are only in memory keep their
 * pointer members as real pointers, so in an x86-64 build every member after a pointer moves by 4
 * bytes per pointer before it.  The records that are read from the files with pointer-sized slots in
 * them (kgtImageHeader, kgtSound) have separate file layouts (kgtImageHeaderFile, kgtSoundFile) that
 * the loader converts; all other file reads go member by member into pointer-free parts, so they are
 * the same in both builds.  The size checks at the end state each structure's size as its 32-bit
 * size plus 4 bytes per pointer (KGT_PTR_GROWTH), i.e. they hold for the i686 and the x86-64 build.
 */
#ifndef KGT_TYPES_H
#define KGT_TYPES_H

#include <stddef.h>     /* offsetof */
#include <stdint.h>     /* intptr_t, uint32_t */

/* bytes a pointer (or pointer-sized integer) is larger than in the original 32-bit program */
#define KGT_PTR_GROWTH (sizeof(void *) - 4)

#pragma pack(push, 1)

typedef struct kgtPallette kgtPallette;
typedef struct kgt_core kgt_core;
typedef struct kgtSkillHeader kgtSkillHeader;
typedef struct kgtSkill kgtSkill;
typedef struct kgtImageHeader kgtImageHeader;
typedef struct kgtSound kgtSound;
typedef struct kgtCharacterCPUCommandSkillFull kgtCharacterCPUCommandSkillFull;
typedef struct kgtCharacterCPUCommandSkillShort kgtCharacterCPUCommandSkillShort;
typedef struct kgtCpuCommand kgtCpuCommand;
typedef struct kgtCharacterCommand kgtCharacterCommand;
typedef struct kgtCharacterHitJunction kgtCharacterHitJunction;
typedef struct kgtCommonImage kgtCommonImage;
typedef struct kgtStoryEntryCpu kgtStoryEntryCpu;
typedef struct kgtStoryEntry kgtStoryEntry;
typedef enum kgtJumptableEndpoints kgtJumptableEndpoints;
typedef enum kgtEngineObjectTypes kgtEngineObjectTypes;
typedef struct kgtEngineObject kgtEngineObject;
typedef struct kgt_character_struct kgt_character_struct;
typedef struct kgtSystemHitJunction kgtSystemHitJunction;
typedef struct kgtSystem kgtSystem;
typedef enum GAME_MODES GAME_MODES;
typedef struct kgtGameState kgtGameState;
typedef struct kgt_demo_file kgt_demo_file;
typedef struct kgt_stage kgt_stage;
typedef struct online_tcp_struct online_tcp_struct;
typedef struct kgtWav kgtWav;
typedef struct wavSoundFile wavSoundFile;
typedef struct kgtBMPINFO kgtBMPINFO;
typedef struct UNK_STRUCT_A UNK_STRUCT_A;
typedef struct UNK_STRUCT_B UNK_STRUCT_B;
typedef struct UNK_STRUCT_F UNK_STRUCT_F;
typedef struct UNK_0x48_struct UNK_0x48_struct;
typedef struct kgt_debug_a kgt_debug_a;
typedef struct unk_0x650_struct unk_0x650_struct;
typedef struct POSS_VTABLE_GAME_STATE POSS_VTABLE_GAME_STATE;

struct kgtPallette {   /* size 0x4: one palette colour as stored in the KGT files (B, G, R, 1) */
    char cBlue;                                                    /* 0x0000 */ /* blue, 0-255 */
    char cGreen;                                                   /* 0x0001 */ /* green, 0-255 */
    char cRed;                                                     /* 0x0002 */ /* red, 0-255 */
    char cUnk03;                                                   /* 0x0003 */ /* always 1 in the files */
};

struct kgt_core {   /* size 0x2234 (+ pointer growth): the part common to all KGT files (system, character, demo, stage) as loaded: name, skills, script steps, images, palettes, sounds */
    BYTE cSignature[0x10];                                         /* 0x0000 */ /* the 16-byte file signature ("2DKGT2G" / "2DKGT2K", gszFileSignatures), read by the iOpen*File loaders before bReadKgtCore and not checked */
    char szName[256];                                              /* 0x0010 */ /* name: game title (system file), character name (shown in netplay), demo/stage name */
    kgtSkillHeader * pSkillsAlloc;                                 /* 0x0110 */ /* skills, iActionsCount entries */
    kgtSkill * pSkillScriptsAlloc;                                 /* 0x0114 */ /* script steps of all skills (kgtSkillHeader.shStartingStepIdx indexes it) */
    kgtImageHeader * pImageHeaders;                                /* 0x0118 */ /* images, iImagesCount entries */
    kgtPallette kgtPalettes[256];                                  /* 0x011c */ /* 8 colour palettes of 0x108 entries (0x2100 bytes read as one block, so the next 8 members are its continuation); colour n starts at kgtPalettes + n * 0x108 */
    kgtPallette kgtUnk051C[256];                                   /* 0x051c */ /* part of kgtPalettes */
    kgtPallette kgtUnk091C[256];                                   /* 0x091c */ /* part of kgtPalettes */
    kgtPallette kgtUnk0D1C[256];                                   /* 0x0d1c */ /* part of kgtPalettes */
    kgtPallette kgtUnk111C[256];                                   /* 0x111c */ /* part of kgtPalettes */
    kgtPallette kgtUnk151C[256];                                   /* 0x151c */ /* part of kgtPalettes */
    kgtPallette kgtUnk191C[256];                                   /* 0x191c */ /* part of kgtPalettes */
    kgtPallette kgtUnk1D1C[256];                                   /* 0x1d1c */ /* part of kgtPalettes */
    kgtPallette kgtUnk211C[64];                                    /* 0x211c */ /* end of kgtPalettes */
    kgtSound * pkgtSounds;                                         /* 0x221c */ /* sounds, iSoundsCount entries */
    int iWavBank;                                                  /* 0x2220 */ /* row of gpWavs for these sounds: 0 system, 1 demo, 2 stage, 3 + player */
    int bLoaded;                                                   /* 0x2224 */ /* 1 once the file has been read; vFreeKgtCore frees only then */
    int iActionsCount;                                             /* 0x2228 */ /* number of skills */
    int iImagesCount;                                              /* 0x222c */ /* number of images */
    int iSoundsCount;                                              /* 0x2230 */ /* number of sounds */
};

struct kgtSkillHeader {   /* size 0x27: one skill (named sequence of script steps) of a KGT file */
    char szName[32];                                               /* 0x0000 */ /* skill name (empty = unused slot) */
    short shStartingStepIdx;                                       /* 0x0020 */ /* index of the first step in kgt_core.pSkillScriptsAlloc (read as unsigned) */
    BYTE pad_0022[0x1];                                            /* 0x0022 */ /* unknown */
    int iDefaultScriptGroup;                                       /* 0x0023 */ /* flags, read as an int: 3 = system images (vSpawnStageScripts); 0x20 = system object deleted at the end of the script */
};

struct kgtSkill {   /* size 0x10: one 16-byte script step; battle.c's kgtScriptStep gives the per-command layouts */
    char cSkillType;                                               /* 0x0000 */ /* command: 00 = start of a new skill, 0B = SC, 19 = FD, 0C = I (image), ... */
    short shParam1;                                                /* 0x0001 */ /* image step: wait frames (unsigned); cursor positions: x */
    short shParam2;                                                /* 0x0003 */ /* cursor positions: y */
    char cParam3;                                                  /* 0x0005 */ /* ui positions: x step between repeated marks */
    char cParam4;                                                  /* 0x0006 */ /* ui positions: y step between repeated marks */
    BYTE pad_0007[0x9];                                            /* 0x0007 */ /* rest of the step, depends on the command */
};

struct kgtImageHeader {   /* size 0x14 (+ pointer growth): one image of a KGT file as loaded (the file record is kgtImageHeaderFile) */
    int * pAlloc;                                                  /* 0x0000 */ /* pixel data (8-bit, possibly compressed) */
    int iWidth;                                                    /* 0x0004 */ /* width in pixels: the row length (stride) of the 8-bit pixels */
    int iHeight;                                                   /* 0x0008 */ /* height in pixels: the number of rows */
    int iFlags;                                                    /* 0x000c */ /* bit 0: the image carries its own 256-colour palette (0x400 bytes, 256 x B, G, R, x) in front of the pixels, which then start at pAlloc + 0x400 */
    int iSize;                                                     /* 0x0010 */ /* size of the pixel data in bytes */
};

/* the file record of an image header (0x14 bytes): dwAlloc is the pointer slot of the original,
   meaningless in the file; bReadKgtCore converts it to a kgtImageHeader */
typedef struct kgtImageHeaderFile {
    uint32_t dwAlloc;                                              /* 0x0000 */ /* kgtImageHeader.pAlloc */
    int iWidth;                                                    /* 0x0004 */
    int iHeight;                                                   /* 0x0008 */
    int iFlags;                                                    /* 0x000c */
    int iSize;                                                     /* 0x0010 */
} kgtImageHeaderFile;

struct kgtSound {   /* size 0x2a (+ pointer growth): one sound of a KGT file (wave, MIDI or CD track) as loaded (the file record is kgtSoundFile) */
    void * pAlloc;                                                 /* 0x0000 */ /* sound data read from the file */
    char szName[32];                                               /* 0x0004 */ /* sound name */
    union {
        DWORD iSize;                                               /* 0x0024 */ /* size of the sound data in the file */
        kgtWav * pWav;                                             /* 0x0024 */ /* replaced by the loaded wave object */
    };
    BYTE cFlags;                                                   /* 0x0028 */ /* low nibble: 0 stop all sounds, 1 wave, 2 MIDI, 3 CD audio; 0x10 loop */
    BYTE cCdTrack;                                                 /* 0x0029 */ /* CD audio track */
};

/* the file record of a sound header (0x2a bytes); dwAlloc is the original's pointer slot (kept as it
   is read, as the original does), dwSize the slot that becomes kgtSound.pWav */
typedef struct kgtSoundFile {
    uint32_t dwAlloc;                                              /* 0x0000 */ /* kgtSound.pAlloc */
    char szName[32];                                               /* 0x0004 */
    DWORD iSize;                                                   /* 0x0024 */ /* kgtSound.iSize / pWav */
    BYTE cFlags;                                                   /* 0x0028 */
    BYTE cCdTrack;                                                 /* 0x0029 */
} kgtSoundFile;

struct kgtCharacterCPUCommandSkillFull {   /* size 0x7: one step of a CPU command (kgtCpuCommand.kgtSteps) */
    BYTE pad_0000[0x1];                                            /* 0x0000 */ /* unknown */
    BYTE cDirection;                                               /* 0x0001 */ /* direction: 1 = none, then right, ... up-right; read as a WORD with the next byte (bit 0x2000 = step in use: a command ends at the first step without it) */
    BYTE cContinueFlag;                                            /* 0x0002 */ /* 0x30 = go to next step, 0x20 = end, 0x10 = inactive? */
    WORD wCommandIdx;                                              /* 0x0003 */ /* 1-based index into kgt_character_struct.kgtCommands, 0 = only a direction */
    WORD wTiming;                                                  /* 0x0005 */ /* minimum frames before the step (plus a random delay by CPU level) */
};

struct kgtCharacterCPUCommandSkillShort {   /* size 0x6: the one-byte-shorter last step of a CPU command (not used by the code) */
    char cDirection;                                               /* 0x0000 */ /* as kgtCharacterCPUCommandSkillFull.cDirection */
    char cContinueFlag;                                            /* 0x0001 */ /* as kgtCharacterCPUCommandSkillFull.cContinueFlag */
    short shCommandIdx;                                            /* 0x0002 */ /* as kgtCharacterCPUCommandSkillFull.wCommandIdx */
    short shTiming;                                                /* 0x0004 */ /* as kgtCharacterCPUCommandSkillFull.wTiming */
};

struct kgtCpuCommand {   /* size 0x6f: a CPU (COM) command of a character: conditions and the commands the CPU enters */
    char szName[30];                                               /* 0x0000 */ /* command name */
    BYTE pad_001e[0x2];                                            /* 0x001e */ /* unknown */
    char cIsFighterAirborneBitmask;                                /* 0x0020 */ /* 1 = character in the air, 2 = enemy in the air, 3 = both? */
    BYTE cProbability;                                             /* 0x0021 */ /* chance to use the command, percent */
    WORD wIntervalMin;                                             /* 0x0022 */ /* minimum distance to the enemy */
    WORD wIntervalMax;                                             /* 0x0024 */ /* maximum distance to the enemy */
    BYTE pad_0026[0x3];                                            /* 0x0026 */ /* unknown */
    kgtCharacterCPUCommandSkillFull kgtSteps[10];                  /* 0x0029 */ /* the steps entered in turn (kgt_character_struct.iCpuStepIdx) */
};

struct kgtCharacterCommand {   /* size 0x52: a command (input sequence) of a character and the skills it starts */
    char szName[30];                                               /* 0x0000 */ /* command name */
    BYTE pad_001e[0x2];                                            /* 0x001e */ /* unknown */
    short shCommandTime;                                           /* 0x0020 */ /* frames allowed for the whole input (unsigned); above 29 the input buffer is cleared once it is entered */
    short shAirCommandSet;                                         /* 0x0022 */ /* skill started in the air (0 = none) */
    short shStandNearCommandSet;                                   /* 0x0024 */ /* skill started standing within kgt_character_struct.shNearDistance of the enemy */
    short shStandFarCommandSet;                                    /* 0x0026 */ /* skill started standing farther away */
    short shCrouchedCommandSet;                                    /* 0x0028 */ /* skill started crouching */
    short shCommandInputs[10];                                     /* 0x002a */ /* Byte 1: Right digit: Arrow - 0 for 'free', 1 for dot, 09 for up right, 0A-0D for multi-arrows starting at left.   Leftt-digit: Buttons A through D are stored in the left digit, bitmask-style. Byte 2: Left-digit -  Continue to next input = +0x30 End at this input = +0x20 No repeat/hold/black button = +0x00 Repeat = +0x40 Hold = +0x80 Black = +0xC0  Right-digit: Buttons E (binary 1) and F (binary 2) */
    short shCommandInputTimings[10];                               /* 0x003e */ /* per input: frames it must be held/entered within */
};

struct kgtCharacterHitJunction {   /* size 0x4: one entry of a character's hit junction table */
    short shAllotmentIdx;                                          /* 0x0000 */ /* skill the hit junction jumps to */
    short shSparkIdx;                                              /* 0x0002 */ /* hit spark skill (system file) */
};

struct kgtCommonImage {   /* size 0x6: one common image entry of a character */
    short shENumber;                                               /* 0x0000 */ /* image number */
    short shXMovement;                                             /* 0x0002 */ /* x offset */
    short shYMovement;                                             /* 0x0004 */ /* y offset */
};

struct kgtStoryEntryCpu {   /* size 0x1a: one CPU opponent of a story mode fight entry */
    unsigned int uBitmask;                                         /* 0x0000 */ /* read as a dword: +0x1 show life, +0x2 time, +0x4 life; bits 5-6 win pause (0x20 leave, 0x40 appear); bits 7-8 effect (0x80 player); 0x200 on the player's side (its win points count for the player, and it shares the player's "you win"/"you lose") */
    char cCharacterIdx;                                            /* 0x0004 */ /* character file (1-based, 0 = none) */
    char cCpuLevel;                                                /* 0x0005 */ /* CPU level 0-100 */
    char cEnemyBitmask;                                            /* 0x0006 */ /* players it fights (bit n = player n; player = 0x01) */
    short shStartPos;                                              /* 0x0007 */ /* start x position (pixels, unsigned) */
    BYTE pad_0009[0x2];                                            /* 0x0009 */ /* unknown */
    char cTimeMethodNumber;                                        /* 0x000b */ /* appearance time (frames/seconds) */
    char cTimeMethodNumberRandom;                                  /* 0x000c */ /* random part of the appearance time */
    char cLifeMethodTarget;                                        /* 0x000d */ /* appears when this player's life ... (0 = player) */
    char cLifeMethodAmount;                                        /* 0x000e */ /* ... drops below this */
    char cVictoryPointsAmount;                                     /* 0x000f */ /* win points given when it is defeated */
    char cEffectLifeIncrease;                                      /* 0x0010 */ /* life given by the defeat effect */
    char cEffectSpecialIncrease;                                   /* 0x0011 */ /* special gauge given by the defeat effect */
    char cVictoryPointsTarget;                                     /* 0x0012 */ /* who gets the points: 0 = last attacker, n = player n - 1 */
    char cWhenTimeTarget;                                          /* 0x0013 */ /* time over: compared against this player (0 = player) */
    char cWhenTimeAmount;                                          /* 0x0014 */ /* points given to cWhenTimeTarget at time over (vjmpHandleBattleState) */
    char cWhenTimeNumber;                                          /* 0x0015 */ /* not used by the code */
    BYTE pad_0016[0x3];                                            /* 0x0016 */ /* unknown */
    char cUnk19;                                                   /* 0x0019 */ /* last byte of the entry, not used by the code */
};

struct kgtStoryEntry {   /* size 0xce: one story mode entry (fight, demo, jump or end) of the story character */
    BYTE cStoryType;                                               /* 0x0000 */ /* 1 = fight, 2 = demo, 3 = jump/divergence, 4 = end */
    char cStageIdx;                                                /* 0x0001 */ /* fight: stage (1-based); demo: demo; divergence: 0 unconditional, 1 front stage, 2 life gauge, 3 won all fights */
    char cRoundsAmount;                                            /* 0x0002 */ /* fight: rounds; divergence type 2: life threshold */
    char bFirstLifeCarryOver;                                      /* 0x0003 */ /* keep the life from the previous fight */
    char cLifeRecovery;                                            /* 0x0004 */ /* life recovered before the fight, percent */
    char cWhenDefeatAndFirstRoundBitmask;                          /* 0x0005 */ /* divergence: entries to jump (appointment); fight: +0x01 game over, +0x02 carry over */
    short shTime;                                                  /* 0x0006 */ /* round time */
    short shPlayerStartXPos;                                       /* 0x0008 */ /* player start x (pixels) */
    BYTE pad_000a[0x2];                                            /* 0x000a */ /* unknown */
    char cOptionsBitmask;                                          /* 0x000c */ /* 0x01 show round number, 0x02 fighting spirit indication, 0x04 wall */
    BYTE pad_000d[0x3];                                            /* 0x000d */ /* unknown */
    char cWhenTimeOver;                                            /* 0x0010 */ /* time over: player compared against (0 = cpu1 ...) */
    char cWhenTimeOverNumber;                                      /* 0x0011 */ /* time over: points */
    char cCpuWins;                                                 /* 0x0012 */ /* points on defeat go to: 0 character who hit last, 1 player, 2 cpu1 ... */
    char cCpuWinsNumber;                                           /* 0x0013 */ /* points on defeat */
    BYTE pad_0014[0x4];                                            /* 0x0014 */ /* unknown */
    kgtStoryEntryCpu kgtStoryEntryCPUs[7];                         /* 0x0018 */ /* the CPU opponents (players 1-7) */
};

typedef struct kgtGridCoordinates {   /* size 0x8: a cursor on the character-select grid */
    int iCol;                                                      /* 0x0000 */ /* column */
    int iRow;                                                      /* 0x0004 */ /* row */
} kgtGridCoordinates;

enum kgtJumptableEndpoints {   /* kgtEngineObject.iJumpIdx: the object's handler in gpfnGamestateJumptable (engine.c) */
    EMPTY = 0,                              /* free slot (vEmptyFunction) */
    RESET_IDX = 1,                          /* being deleted (vjmpResetIdx) */
    START_GAME = 2,                         /* vjmpStartGame */
    SCREEN_CONTROL = 3,                     /* vjmpScreenControl */
    READ_SCRIPT = 4,                        /* runs its skill script (vjmpReadScript) */
    FADE_DRIFT_EFFECT = 5,                  /* vjmpFadeDriftEffect */
    UPDATE_TIMER = 6,                       /* battle timer display (vjmpUpdateTimerAndUi) */
    HIT_COMBO_COUNTER = 7,                  /* vjmpHitComboCounter */
    ROUND_START = 8,                        /* vjmpRoundStart */
    IDLE_A = 9,                             /* vjmpIdleA */
    CHARACTER_SELECT_SCREEN = 10,           /* vjmpCharacterSelectScreen */
    IDLE_B = 11,                            /* vjmpIdleB */
    MENU_TRAVERSAL = 12,                    /* title menu (vjmpMenuTraversal) */
    GAME_OVER_SCREEN = 13,                  /* vjmpGameOverScreen */
    BATTLE_STATE = 14,                      /* round flow (vjmpHandleBattleState) */
    BATTLE_UI = 15,                         /* gauges, faces and marks (vjmpHandleBattleInterface) */
    STORY_MODE = 16,                        /* story progress (vjmpInitiateStoryMode) */
    DISPLAY_TITLE_SCREEN = 17,              /* vjmpDisplayTitleScreens */
    /* (not a value of the game: a negative enumerator gives the enum the type int, as MSVC does;
       GNU C compilers otherwise make an enum without negative values unsigned int, which would turn
       comparisons such as iJumpIdx > RESET_IDX into unsigned ones) */
    JUMPTABLE_ENDPOINTS_SIGNED = -1
};

enum kgtEngineObjectTypes {   /* kgtEngineObject.iObjectType: which file's skills the object runs */
    PLAYER_ENGINE_OBJECT = 0,               /* a player's own object (character skills) */
    STORY_ENGINE_OBJECT = 1,                /* every object a character's script creates (O command, hit sparks) and the select/story pictures (character skills) */
    SYSTEM_ENGINE_OBJECT = 2,               /* system file skills (UI, effects) */
    DEMO_ENGINE_OBJECT = 3,                 /* demo file skills */
    STAGE_ENGINE_OBJECT = 4,                /* stage file skills */
    CHARACTER_ENGINE_OBJECT = 5,            /* only the R1 companion: skill R1 run as an extra object alongside the player (character skills) */
    ENGINE_OBJECT_TYPES_SIGNED = -1         /* (see kgtJumptableEndpoints) */
};

struct kgtEngineObject {   /* size 0x17e (+ pointer growth; only in memory): one engine object (gkgtEngineObjects[1024]): players, effects, UI parts and the game-state controllers; iJumpIdx selects its handler */
    kgtJumptableEndpoints iJumpIdx;                                /* 0x0000 */ /* handler (gpfnGamestateJumptable index); EMPTY = free slot */
    int iDepth;                                                    /* 0x0004 */ /* draw layer (gkgtDrawLayers index, 0-127; players 0x50/0x46 by line) */
    int iPosX;                                                     /* 0x0008 */ /* x position, 16.16 fixed point (UI objects: screen x) */
    int iPosY;                                                     /* 0x000c */ /* y position, 16.16 fixed point */
    int iDrawFlag;                                                 /* 0x0010 */ /* what to draw: -1 script image, 0 nothing, n > 0 external bitmap gkgtBitmaps[n] */
    int iLine;                                                     /* 0x0014 */ /* bit 0: which of the two lines (planes) it is on (depth from giObjectLayers); bit 1: line switch in progress */
    int iXMomentum;                                                /* 0x0018 */ /* x speed, 16.16 per frame */
    int iYMomentum;                                                /* 0x001c */ /* y speed, 16.16 per frame */
    int iXGravity;                                                 /* 0x0020 */ /* x acceleration, 16.16 per frame */
    int iYGravity;                                                 /* 0x0024 */ /* y acceleration, 16.16 per frame */
    int iFlags;                                                    /* 0x0028 */ /* 0x20000000 follows pParent at shParentOffsetX/Y; 0x40000000 not at absolute screen coordinates (passed to the drawing code); 0x80000000 O command flag 8 / player without an R1 skill */
    int iSkillScriptIdx;                                           /* 0x002c */ /* current script step (index into pSkillScriptsAlloc) */
    int iSkillIdx;                                                 /* 0x0030 */ /* current skill */
    int iStartSkillIdx;                                            /* 0x0034 */ /* skill it was created with; UI and story pictures restart it when their script ends */
    int iHitJunctionIdx;                                           /* 0x0038 */ /* pending jump set by hits and DS branches: (step << 16) | skill, 0 = none */
    int iImageWaitFrames;                                          /* 0x003c */ /* frames left on the current image step */
    int iOpponentDowntimeInFrames;                                 /* 0x0040 */ /* hit stop: frames the object is frozen after a hit or guard */
    int iColorRed;                                                 /* 0x0044 */ /* colour (COL OBJ command) */
    int iColorGreen;                                               /* 0x0048 */ /* colour */
    int iColorBlue;                                                /* 0x004c */ /* colour */
    int iColorAlpha;                                               /* 0x0050 */ /* 0..32 */
    int iColorBlendtype;                                           /* 0x0054 */ /* 0 normal, 1 half, 2 add, 3 subtract, 4 alpha */
    int iGroundY;                                                  /* 0x0058 */ /* ground level for iPosY (16.16): at it the object stands, below it (smaller y) it is in the air */
    int iPlayerLookingRight;                                       /* 0x005c */ /* facing / image flip (1 when the nearest enemy is at a smaller x); was pos_player_ignore_flag in Ghidra */
    int iOwnerIdx;                                                 /* 0x0060 */ /* player that owns it: objects of the same owner never hit each other */
    int iDsLandingSkillIdx;                                        /* 0x0064 */ /* DS branch 1 (landing): (step << 16) | skill; stage script objects: skill index */
    int iDsGuardedSkillIdx;                                          /* 0x0068 */ /* DS branch 3 (the attack is guarded; also taken by non-players in the powerless FA-0x80 clash); stage script objects: scroll step index */
    int iDsAttackHitsSkillIdx;                                     /* 0x006c */ /* DS branch 2 (attack hits) */
    int iDsWallHitSkillIdx;                                        /* 0x0070 */ /* DS branch 4 (hits the wall) */
    int iDsAttackClashSkillIdx;                                       /* 0x0074 */ /* DS branch 5 (attack against attack: two attack boxes clash) */
    int iDsThrowSkillIdx;                                          /* 0x0078 */ /* DS branch 6 (throw connected) */
    BYTE cLoopCount;                                               /* 0x007c */ /* script SF: loops left */
    int iLoopTarget;                                               /* 0x007d */ /* script SF: (step << 16) | skill looped to */
    int iLoopReturn;                                               /* 0x0081 */ /* script SF: (step << 16) | skill to continue with */
    int iReturnSkillIdx;                                           /* 0x0085 */ /* (step << 16) | skill to return to after a called skill */
    kgtSkill * pAttackBoxes[20];                                   /* 0x0089 */ /* active attack-box script steps (FA) */
    kgtSkill * pGuardBoxes[20];                                    /* 0x00d9 */ /* active body/guard-box script steps (FD) */
    kgtSkill * pReactionStep;                                      /* 0x0129 */ /* active reaction step (hit junctions by situation, battle.c kgtReactionStep) */
    short shParentOffsetX;                                         /* 0x012d */ /* position relative to pParent (iFlags 0x20000000), pixels */
    short shParentOffsetY;                                         /* 0x012f */ /* pixels */
    short shVarA;                                                  /* 0x0131 */ /* object variable A (script command V) */
    short shVarB;                                                  /* 0x0133 */ /* object variable B */
    short shVarC;                                                  /* 0x0135 */ /* object variable C */
    short shVarD;                                                  /* 0x0137 */ /* object variable D */
    short shVarE;                                                  /* 0x0139 */ /* object variable E */
    short shVarF;                                                  /* 0x013b */ /* object variable F */
    short shVarG;                                                  /* 0x013d */ /* object variable G */
    short shVarH;                                                  /* 0x013f */ /* object variable H */
    short shVarI;                                                  /* 0x0141 */ /* object variable I */
    short shVarJ;                                                  /* 0x0143 */ /* object variable J */
    short shVarK;                                                  /* 0x0145 */ /* object variable K */
    short shVarL;                                                  /* 0x0147 */ /* object variable L */
    short shVarM;                                                  /* 0x0149 */ /* object variable M */
    short shVarN;                                                  /* 0x014b */ /* object variable N */
    short shVarO;                                                  /* 0x014d */ /* object variable O */
    short shVarP;                                                  /* 0x014f */ /* object variable P */
    BYTE cAfterImageIdx;                                           /* 0x0151 */ /* after-image trail in use: gAfterImageTrails[cAfterImageIdx - 1], 0 = none */
    int iProcessStep;                                              /* 0x0152 */ /* state of the handler (game-state objects use 0, 1, 100, 200, ...) */
    /* The next members are also work slots of the game-state handlers, which keep either numbers or
       object pointers in them (an int of the 32-bit original); they are pointer-sized integers
       (intptr_t) so that both fit, and read as int where they hold numbers. */
    intptr_t iPlayerIdx;                                           /* 0x0156 */ /* player slot (gkgtLoadedCharacter index) whose file it uses; game-state objects use it as a counter or value, or an object pointer (vjmpHandleBattleInterface) */
    intptr_t iObjectType;                                          /* 0x015a */ /* which file's skills it runs (kgtEngineObjectTypes); UI objects also keep an object pointer in it (vjmpHandleBattleInterface) */
    union {
        kgtEngineObject * pWork015E;                               /* 0x015e */ /* handler work slot: an object (portrait, cursor, digit) or the combo count pointer */
        intptr_t iWork015E;                                        /* 0x015e */ /* the same slot holding a number (countdown, selection) */
        int iStateFlags;                                           /* 0x015e */ /* players (battle.c OBJ_FLAGS): bits 0-1 stance (0 stand, 1 crouch, 2 air); bits 2-3 action state (0 free, 4 in an action, 8 in a hit reaction, 0xc guarding); 0x10 this attack has already hit (set on a hit, blocks further hits; cleared by FA flag 2, at the end of the skill and on a cancel) */
    };
    union {
        kgtEngineObject * pWork0162;                               /* 0x0162 */ /* handler work slot: an object (portrait, digit) */
        intptr_t iWork0162;                                        /* 0x0162 */ /* the same slot holding a number (wins shown) */
    };
    intptr_t iWork0166;                                            /* 0x0166 */ /* handler work slot: timer ones digit shown, 1P cursor object, stock digit object */
    intptr_t iWork016A;                                            /* 0x016a */ /* handler work slot: timer tens digit shown, 2P cursor object, 1P stock count shown */
    intptr_t iWork016E;                                            /* 0x016e */ /* handler work slot: timer hundreds digit shown, stock digit object */
    intptr_t iWork0172;                                            /* 0x0172 */ /* handler work slot: 2P stock count shown */
    intptr_t iWork0176;                                            /* 0x0176 */ /* handler work slot: timer digit spacing (16.16), target face object */
    kgtEngineObject * pParent;                                     /* 0x017a */ /* object that created it */
};

struct kgt_character_struct {   /* size 0xe03f (+ pointer growth): a loaded character file followed by the runtime battle state of its player slot (gkgtLoadedCharacter[8]); the file part (up to iWins) is read member by member */
    kgt_core kgtCore;                                              /* 0x0000 */ /* the file's common part */
    DWORD dpidOnline;                                              /* 0x2234 */ /* netplay: DirectPlay id using this slot */
    int iOnlineState;                                              /* 0x2238 */ /* slot in use: 0 free, 1 active, 2 remote peer joined */
    BYTE pad_223c[0xa];                                            /* 0x223c */ /* unknown */
    kgtCpuCommand kgtCpuCommands[100];                             /* 0x2246 */ /* CPU commands (iCpuCommandIdx is 1-based) */
    kgtCharacterCommand kgtCommands[100];                          /* 0x4da2 */ /* commands (input sequences) */
    kgtCharacterHitJunction kgtHitJunctions[200];                  /* 0x6daa */ /* hit junction table */
    kgtCommonImage kgtCommonImages[200];                           /* 0x70ca */ /* common images */
    short shSkillIdxStanding;                                      /* 0x757a */ /* skill: standing */
    short shSkillIdxForward;                                       /* 0x757c */ /* skill: walking forward */
    short shSkillIdxBackward;                                      /* 0x757e */ /* skill: walking backward */
    short shSkillIdxJumpUp;                                        /* 0x7580 */ /* skill: jumping straight up */
    short shSkillIdxFrontJump;                                     /* 0x7582 */ /* skill: jumping forward */
    short shSkillIdxBackJump;                                      /* 0x7584 */ /* skill: jumping backward */
    short shSkillIdxFalling;                                       /* 0x7586 */ /* skill: falling */
    short shSkillIdxMidCrouch;                                     /* 0x7588 */ /* skill: crouching down */
    short shSkillIdxCrouching;                                     /* 0x758a */ /* skill: crouching */
    short shSkillIdxStandFromCrouch;                               /* 0x758c */ /* skill: standing up */
    short shSkillIdxCrouchAdvance;                                 /* 0x758e */ /* skill: crouch walking forward */
    short shSkillIdxCrouchRetreat;                                 /* 0x7590 */ /* skill: crouch walking backward */
    short shSkillIdxTurnStanding;                                  /* 0x7592 */ /* skill: turning around standing */
    short shSkillIdxTurnCrouching;                                 /* 0x7594 */ /* skill: turning around crouching */
    short shSkillIdxButtonGuardStand;                              /* 0x7596 */ /* skill: guard button, standing */
    short shSkillIdxButtonGuardCrouch;                             /* 0x7598 */ /* skill: guard button, crouching */
    short shSkillIdxButtonGuardAir;                                /* 0x759a */ /* skill: guard button, in the air */
    short shSkillIdxStart;                                         /* 0x759c */ /* skill: round start */
    short shSkillIdxVictory;                                       /* 0x759e */ /* skill: victory */
    short shSkillIdxLoss;                                          /* 0x75a0 */ /* skill: defeat (KO) */
    short shSkillIdxDraw;                                          /* 0x75a2 */ /* skill: draw */
    short shSkillIdxCharSelectPic;                                 /* 0x75a4 */ /* skill: character select picture */
    short shSkillIdxStageFacePic;                                  /* 0x75a6 */ /* skill: face shown in the battle UI */
    short shSkillIdxR1;                                            /* 0x75a8 */ /* skill: run as an extra object next to the player (if not empty) */
    BYTE pad_75aa[0x26];                                           /* 0x75aa */ /* more built-in skill numbers, not used by the code */
    short shAge;                                                   /* 0x75d0 */ /* profile: age */
    BYTE pad_75d2[0x2];                                            /* 0x75d2 */ /* unknown */
    char cGender;                                                  /* 0x75d4 */ /* profile: gender */
    BYTE pad_75d5[0x6bf];                                          /* 0x75d5 */ /* profile text etc., not used by the code */
    int iUnk7C94;                                                  /* 0x7c94 */ /* not used by the code */
    char cUnk7C98[9];                                              /* 0x7c98 */ /* not used by the code */
    short shYPosOfSideHp;                                          /* 0x7ca1 */ /* y offset of the small life bar over story mode CPU opponents */
    short shNearDistance;                                          /* 0x7ca3 */ /* distance (pixels) within which the 'stand near' skill of a command is used */
    char cShaveRatio;                                              /* 0x7ca5 */ /* damage taken through guard (shaving), percent of the attack power */
    char cLifeRevThreshold;                                        /* 0x7ca6 */ /* life percent at or below which damage is reduced by cLifeRevCorrection */
    char cLifeRevCorrection;                                       /* 0x7ca7 */ /* percent of the damage taken below cLifeRevThreshold (formerly short story_mode_step_b) */
    char cCharacterRev;                                            /* 0x7ca8 */ /* combo damage reduction per hit already taken, percent */
    char cGuardButton;                                             /* 0x7ca9 */ /* guard button (input bit 4 + n) */
    union {
        struct {
            short shLifeGaugeMax;                                  /* 0x7caa */ /* low word of dwLifeGaugeMax */
            char cUnk7CAC[2];                                      /* 0x7cac */ /* high word of dwLifeGaugeMax */
            short shSpecialGaugeMax;                               /* 0x7cae */ /* low word of iSpecialGaugeMax */
            char cUnk7CB0[2];                                      /* 0x7cb0 */ /* high word of iSpecialGaugeMax */
        };
        struct {
            DWORD dwLifeGaugeMax;                                  /* 0x7caa */ /* maximum life (file value, copied to iLifeMax) */
            int iSpecialGaugeMax;                                  /* 0x7cae */ /* size of one special gauge stock (copied to iSpecialMax) */
        };
    };
    int iSpecialStockMax;                                          /* 0x7cb2 */ /* maximum number of stocks (copied to iStockMax) */
    int iOptionFlags;                                              /* 0x7cb6 */ /* character options: 1 auto guard when no direction is held, 2 guard in the air, 8 guard button mode (no automatic turning) */
    BYTE pad_7cba[0x4];                                            /* 0x7cba */ /* unknown */
    short shSpecialGaugeIncreaseOnAttack;                          /* 0x7cbe */ /* special gauge gained when hitting */
    short shSpecialGaugeIncreaseOnHit;                             /* 0x7cc0 */ /* special gauge gained when being hit */
    char cStartingStock;                                           /* 0x7cc2 */ /* stocks at the start of a game */
    BYTE pad_7cc3[0x2];                                            /* 0x7cc3 */ /* unknown */
    char cStorySectionStart[4];                                    /* 0x7cc5 */ /* start of the 0x507c-byte story section read by iOpenCharacterFile (zeros) */
    kgtStoryEntry kgtStoryEntries[100];                            /* 0x7cc9 */ /* story mode entries */
    char cSectionI;                                                /* 0xcd41 */ /* start of the next 0x11ac-byte section read by iOpenCharacterFile (zeros by default) */
    BYTE pad_cd42[0x1191];                                         /* 0xcd42 */ /* rest of that section */
    BYTE cUnkDED3;                                                 /* 0xded3 */ /* not used by the code */
    BYTE pad_ded4[0x3];                                            /* 0xded4 */ /* unknown */
    int iUnkDED7;                                                  /* 0xded7 */ /* not used by the code */
    BYTE pad_dedb[0xb];                                            /* 0xdedb */ /* unknown */
    int iUnkDEE6;                                                  /* 0xdee6 */ /* not used by the code (Ghidra guessed a player/CPU flag) */
    BYTE pad_deea[0x3];                                            /* 0xdeea */ /* unknown */
    int iWins;                                                     /* 0xdeed */ /* rounds won (W: in the hit-judge display) */
    int iLosses;                                                   /* 0xdef1 */ /* set to 1 when knocked out (L: in the hit-judge display) */
    kgtEngineObject * pkgtoSelf;                                   /* 0xdef5 */ /* the player's engine object */
    kgtEngineObject * pLastOpponent;                               /* 0xdef9 */ /* object last in hit/guard contact with it: the last attacker, used for story win points */
    kgtEngineObject * pLastAttacker;                               /* 0xdefd */ /* object that last hit it (an int in the original); used to push it back at the screen edge */
    int iComboCount;                                               /* 0xdf01 */ /* hits taken in the current combo (damage reduction, combo counter) */
    int iHealth;                                                   /* 0xdf05 */ /* life */
    int iLifePermille;                                             /* 0xdf09 */ /* life in 1/1000 of iLifeMax, computed at time over to find the winner */
    union {
        struct {
            short shShowLife;                                      /* 0xdf0d */ /* low word of iShowLife */
            short shUnkDF0F;                                       /* 0xdf0f */ /* high word of iShowLife */
        };
        int iShowLife;                                             /* 0xdf0d */ /* draw the small life bar (story mode CPUs); tested as an int by vHandleDrawing */
    };
    int iLifeMax;                                                  /* 0xdf11 */ /* maximum life in battle (copy of dwLifeGaugeMax, at least 1) */
    int iSpecialGaugeTokens;                                       /* 0xdf15 */ /* special stocks filled */
    int iStockMax;                                                 /* 0xdf19 */ /* maximum stocks in battle (copy of iSpecialStockMax); 0 = no special gauge */
    int iSpecialGauge;                                             /* 0xdf1d */ /* fill of the current stock, 0..iSpecialMax */
    int iSpecialMax;                                               /* 0xdf21 */ /* size of one stock in battle (copy of iSpecialGaugeMax, at least 1) */
    int iDamageBar;                                                /* 0xdf25 */ /* recent damage drawn in red after the life bar; shrinks after iDamageBarDelay */
    int iDamageBarDelay;                                           /* 0xdf29 */ /* frames before iDamageBar starts shrinking (20 after a hit) */
    int iHitFlags;                                                 /* 0xdf2d */ /* this frame: 1 an attack of the player connected, 2 it was a throw */
    int iTimesHit;                                                 /* 0xdf31 */ /* this frame: hits received */
    int iUnkDF35;                                                  /* 0xdf35 */ /* set to 20 when the player spawns, otherwise unused */
    BYTE pad_df39[0x1];                                            /* 0xdf39 */ /* unknown */
    int iUnkDF3A;                                                  /* 0xdf3a */ /* not used by the code */
    BYTE pad_df3e[0x1];                                            /* 0xdf3e */ /* unknown */
    short shUnkDF3F;                                               /* 0xdf3f */ /* not used by the code */
    int iCurrentXPos;                                              /* 0xdf41 */ /* copy of the player object's x (16.16) */
    int iCurrentYPos;                                              /* 0xdf45 */ /* copy of the player object's y (16.16) */
    int iPushBackX;                                                /* 0xdf49 */ /* x push-back (16.16) added to the player object's position on the next move (screen-edge push) */
    int iFacing;                                                   /* 0xdf4d */ /* facing written when the player turns (1 at spawn for players 1+) */
    int iTargetFacing;                                             /* 0xdf51 */ /* facing towards the nearest enemy: 1 when it is at a smaller x */
    int iLastCommandIdx;                                           /* 0xdf55 */ /* 1-based index of the last command entered */
    BYTE pad_df59[0x4];                                            /* 0xdf59 */ /* unknown */
    int bCpuControlled;                                            /* 0xdf5d */ /* 1 = controlled by the CPU */
    int iCpuLevel;                                                 /* 0xdf61 */ /* CPU level 0-100 */
    int iCpuMode;                                                  /* 0xdf65 */ /* CPU behaviour (test-play CPU setting; 1 = normal CPU) */
    kgtEngineObject * pNearestEnemy;                               /* 0xdf69 */ /* nearest enemy player object on the same line (vFindNearestEnemyPlayer) */
    int iUnkDF6D;                                                  /* 0xdf6d */ /* cleared when the player spawns, otherwise unused */
    int iUnkDF71;                                                  /* 0xdf71 */ /* cleared when the player spawns, otherwise unused */
    int iCpuCommandIdx;                                            /* 0xdf75 */ /* CPU command being entered, 1-based (0 = none) */
    int iCpuStepDelay;                                             /* 0xdf79 */ /* frames before the next CPU command step */
    int iCpuCommandTimer;                                          /* 0xdf7d */ /* frames before the CPU picks a new command */
    int iCpuStepIdx;                                               /* 0xdf81 */ /* current step of the CPU command (kgtSteps index) */
    int iCurrentActionCancellableFlag;                             /* 0xdf85 */ /* cancel state of the current skill (2 after a clash) */
    int iOnlineLag;                                                /* 0xdf89 */ /* netplay: inputs still owed by a remote peer */
    int iInputBufferPos;                                           /* 0xdf8d */ /* netplay: position in giInputBuffer this slot has reached */
    BYTE cCancelType;                                              /* 0xdf91 */ /* cancel block copied from script command C (6 bytes): command byte */
    BYTE cCancelFlags;                                             /* 0xdf92 */ /* bits 0-2 cancel type, bits 3-5 second condition */
    BYTE cCancelLevelMin;                                          /* 0xdf93 */ /* lowest skill level that may cancel */
    WORD wCancelSkillIdx;                                          /* 0xdf94 */ /* the one skill a type-1 cancel allows */
    BYTE cCancelLevelMax;                                          /* 0xdf96 */ /* highest skill level that may cancel */
    short shVarA;                                                  /* 0xdf97 */ /* player variable A (script command V) */
    short shVarB;                                                  /* 0xdf99 */ /* player variable B */
    short shVarC;                                                  /* 0xdf9b */ /* player variable C */
    short shVarD;                                                  /* 0xdf9d */ /* player variable D */
    short shVarE;                                                  /* 0xdf9f */ /* player variable E */
    short shVarF;                                                  /* 0xdfa1 */ /* player variable F */
    short shVarG;                                                  /* 0xdfa3 */ /* player variable G */
    short shVarH;                                                  /* 0xdfa5 */ /* player variable H */
    short shVarI;                                                  /* 0xdfa7 */ /* player variable I */
    short shVarJ;                                                  /* 0xdfa9 */ /* player variable J */
    short shVarK;                                                  /* 0xdfab */ /* player variable K */
    short shVarL;                                                  /* 0xdfad */ /* player variable L */
    short shVarM;                                                  /* 0xdfaf */ /* player variable M */
    short shVarN;                                                  /* 0xdfb1 */ /* player variable N */
    short shVarO;                                                  /* 0xdfb3 */ /* player variable O */
    short shVarP;                                                  /* 0xdfb5 */ /* player variable P */
    union {
        char cEnemyBitmask;                                        /* 0xdfb7 */ /* low byte of iEnemyBitmask */
        int iEnemyBitmask;                                         /* 0xdfb7 */ /* bit n set = player n is an enemy */
    };
    int iWinPoints;                                                /* 0xdfbb */ /* story mode win points (WinPoint / WP: in the status displays) */
    kgtEngineObject * pMNumberObjs[10];                            /* 0xdfbf */ /* objects created with M numbers 0-9 (O command) */
    int iHasCrouchAdvance;                                         /* 0xdfe7 */ /* the crouch advance skill is not empty */
    int iHasCrouchRetreat;                                         /* 0xdfeb */ /* the crouch retreat skill is not empty */
    int iThrowFlags;                                               /* 0xdfef */ /* throw (RC command) flags | 0x20; 0x10 tested for the edge push */
    BYTE pad_dff3[0x4];                                            /* 0xdff3 */ /* unknown */
    int iThrowOffsetX;                                             /* 0xdff7 */ /* thrown opponent's offset (16.16), set by script command RC */
    int iThrowOffsetY;                                             /* 0xdffb */ /* 16.16 */
    int bUseStoredInput;                                           /* 0xdfff */ /* 1 = hit checks use dwStoredInput (set by script command PS) */
    union {
        BYTE cStoredInput;                                         /* 0xe003 */ /* low byte of dwStoredInput */
        DWORD dwStoredInput;                                       /* 0xe003 */ /* input at the moment of a PS stop */
    };
    int iRoundResult;                                              /* 0xe007 */ /* round result: 1 won, 2 lost by KO, 3 lost at time over / draw; 0 = undecided */
    int iColor;                                                    /* 0xe00b */ /* colour (palette) chosen, -1 = none */
    int bImageShown;                                               /* 0xe00f */ /* 1 once the player's script has shown an image (it is drawn/targetable from then on) */
    union {
        struct {
            BYTE cFlashType;                                       /* 0xe013 */ /* low byte of flash.iType */
            BYTE pad_e014[0x3];                                    /* 0xe014 */ /* unknown */
            BYTE cFlashRed;                                        /* 0xe017 */ /* low byte of flash.iRed */
            BYTE pad_e018[0x3];                                    /* 0xe018 */ /* unknown */
            BYTE cFlashGreen;                                      /* 0xe01b */ /* low byte of flash.iGreen */
            BYTE pad_e01c[0x3];                                    /* 0xe01c */ /* unknown */
            BYTE cFlashBlue;                                       /* 0xe01f */ /* low byte of flash.iBlue */
            BYTE pad_e020[0x3];                                    /* 0xe020 */ /* unknown */
            BYTE cFlashAlpha;                                      /* 0xe023 */ /* low byte of flash.iAlpha */
            BYTE pad_e024[0x3];                                    /* 0xe024 */ /* unknown */
            int iFlashTimeLeft;                                    /* 0xe027 */ /* flash.iTimeLeft: frames left; counted down by vProcessEngineObjects */
            BYTE cFlashBaseRed;                                    /* 0xe02b */ /* low byte of flash.iBaseRed */
            BYTE pad_e02c[0x3];                                    /* 0xe02c */ /* unknown */
            BYTE cFlashBaseGreen;                                  /* 0xe02f */ /* low byte of flash.iBaseGreen */
            BYTE pad_e030[0x3];                                    /* 0xe030 */ /* unknown */
            BYTE cFlashBaseBlue;                                   /* 0xe033 */ /* low byte of flash.iBaseBlue */
            BYTE pad_e034[0x3];                                    /* 0xe034 */ /* unknown */
            BYTE cFlashBaseAlpha;                                  /* 0xe037 */ /* low byte of flash.iBaseAlpha */
            BYTE pad_e038[0x3];                                    /* 0xe038 */ /* unknown */
            BYTE cFlashDuration;                                   /* 0xe03b */ /* low byte of flash.iDuration */
            BYTE pad_e03c[0x3];                                    /* 0xe03c */ /* unknown */
        };
        struct {                                                   /* 0xe013: palette flash set by script command EB */
            int iType;                                             /* 0xe013 */ /* 1 smooth, 2 blinking, 3 random */
            int iRed, iGreen, iBlue, iAlpha;                       /* 0xe017 */ /* colour to flash to (0..255) */
            int iTimeLeft;                                         /* 0xe027 */ /* frames left */
            int iBaseRed, iBaseGreen, iBaseBlue, iBaseAlpha;       /* 0xe02b */ /* object colour to return to */
            int iDuration;                                         /* 0xe03b */ /* total frames */
        } flash;                                                   /* palette flash of the player's objects, set by script command EB (battle.c kgtFlash) */
    };
};


struct kgtSystemHitJunction {   /* size 0x24: one hit junction (hit reaction type) of the system file */
    char szName[30];                                               /* 0x0000 */ /* hit junction name */
    BYTE pad_001e[0x2];                                            /* 0x001e */ /* unknown */
    char cDoing;                                                   /* 0x0020 */ /* flags: bit 0 = the reaction is guarded */
    BYTE pad_0021[0x3];                                            /* 0x0021 */ /* unknown */
};

struct kgtSystem {   /* size 0x124bc (+ pointer growth of the kgt_core): the loaded system file (gkgtKgtSystem): game settings, name lists and the skills of the system images */
    kgt_core kgtCore;                                              /* 0x0000 */ /* the file's common part */
    char szCharacterNames[50][256];                                /* 0x2234 */ /* character file names (without extension) */
    kgtSystemHitJunction kgtHitJunctions[200];                     /* 0x5434 */ /* hit junction (reaction) types */
    char cUnk7054;                                                 /* 0x7054 */ /* not used by the code */
    BYTE pad_7055[0x4];                                            /* 0x7055 */ /* unknown */
    char cStiffTimeHit;                                            /* 0x7059 */ /* hit stop frames on hit (unsigned) */
    char cStiffTimeGuard;                                          /* 0x705a */ /* hit stop frames on guard */
    char cStiffTimeOffset;                                         /* 0x705b */ /* hit stop frames when two attacks cancel out */
    char szStageNames[50][256];                                    /* 0x705c */ /* stage file names */
    char szDemoNames[100][256];                                    /* 0xa25c */ /* demo file names */
    char cTitleDemoIdx;                                            /* 0x1065c */ /* demo of the title screen */
    char cStoryModeDemoIdx;                                        /* 0x1065d */ /* demo before story mode */
    char cVsSingleDemoIdx;                                         /* 0x1065e */ /* demo before a single versus game */
    char cVsTeamDemoIdx;                                           /* 0x1065f */ /* demo before a team versus game */
    char cGameOverDemoIdx;                                         /* 0x10660 */ /* demo of the game over screen */
    char cOpeningDemoIdx;                                          /* 0x10661 */ /* opening demo */
    char cUnk10662;                                                /* 0x10662 */ /* not used by the code */
    char cUnk10663;                                                /* 0x10663 */ /* not used by the code */
    char cSystemBitmask;                                           /* 0x10664 */ /* +0x1 editor won't read file, +0x02 offset, +0x04 story mode, +0x08 vs mode, +0x10 vs team mode, +0x20 numbers shown on life, +0x40 cursor stays */
    BYTE pad_10665[0x3];                                           /* 0x10665 */ /* unknown */
    char szCommonImageNames[200][32];                              /* 0x10668 */ /* common image names */
    short shSkillIdxNone;                                          /* 0x11f68 */ /* system skill: unused */
    short shSkillIdxHitLetterHit;                                  /* 0x11f6a */ /* system skill: 'hit' letters of the combo counter */
    short shSkillIdxHitNumber0;                                    /* 0x11f6c */ /* system skill: combo counter digit 0 */
    short shSkillIdxHitNumber1;                                    /* 0x11f6e */ /* system skill: combo counter digit 1 */
    short shSkillIdxHitNumber2;                                    /* 0x11f70 */ /* system skill: combo counter digit 2 */
    short shSkillIdxHitNumber3;                                    /* 0x11f72 */ /* system skill: combo counter digit 3 */
    short shSkillIdxHitNumber4;                                    /* 0x11f74 */ /* system skill: combo counter digit 4 */
    short shSkillIdxHitNumber5;                                    /* 0x11f76 */ /* system skill: combo counter digit 5 */
    short shSkillIdxHitNumber6;                                    /* 0x11f78 */ /* system skill: combo counter digit 6 */
    short shSkillIdxHitNumber7;                                    /* 0x11f7a */ /* system skill: combo counter digit 7 */
    short shSkillIdxHitNumber8;                                    /* 0x11f7c */ /* system skill: combo counter digit 8 */
    short shSkillIdxHitNumber9;                                    /* 0x11f7e */ /* system skill: combo counter digit 9 */
    short shSkillIdxOffsetHitMark;                                 /* 0x11f80 */ /* system skill: mark shown when two attacks cancel out */
    short shSkillIdxRoundAniStartTime;                             /* 0x11f82 */ /* system skill: round animation start */
    short shSkillIdxRoundAniEndTime;                               /* 0x11f84 */ /* system skill: round animation end */
    short shSkillIdxRound1;                                        /* 0x11f86 */ /* system skill: 'round 1' */
    short shSkillIdxRound2;                                        /* 0x11f88 */ /* system skill: 'round 2' */
    short shSkillIdxRound3;                                        /* 0x11f8a */ /* system skill: 'round 3' */
    short shSkillIdxRound4;                                        /* 0x11f8c */ /* system skill: 'round 4' */
    short shSkillIdxRound5;                                        /* 0x11f8e */ /* system skill: 'round 5' */
    short shSkillIdxRound6;                                        /* 0x11f90 */ /* system skill: 'round 6' */
    short shSkillIdxRound7;                                        /* 0x11f92 */ /* system skill: 'round 7' */
    short shSkillIdxRound8;                                        /* 0x11f94 */ /* system skill: 'round 8' */
    short shSkillIdxRound9;                                        /* 0x11f96 */ /* system skill: 'round 9' */
    short shSkillIdxRoundFinal;                                    /* 0x11f98 */ /* system skill: 'final round' */
    short shSkillIdxSpirits;                                       /* 0x11f9a */ /* system skill: fighting spirit ('fight') */
    short shSkillIdxKO;                                            /* 0x11f9c */ /* system skill: 'KO' */
    short shSkillIdxPerfect;                                       /* 0x11f9e */ /* system skill: 'perfect' */
    short shSkillIdxYouWin;                                        /* 0x11fa0 */ /* system skill: 'you win' */
    short shSkillIdxYouLose;                                       /* 0x11fa2 */ /* system skill: 'you lose' */
    short shSkillIdx1pWins;                                        /* 0x11fa4 */ /* system skill: '1P wins' */
    short shSkillIdx2pWins;                                        /* 0x11fa6 */ /* system skill: '2P wins' */
    short shSkillIdxDraw;                                          /* 0x11fa8 */ /* system skill: 'draw' */
    short shSkillIdxDoubleKO;                                      /* 0x11faa */ /* system skill: 'double KO' */
    short shSkillIdxUnlimitedSign;                                 /* 0x11fac */ /* system skill: infinite time sign */
    short shSkillIdxTimeNumber0;                                   /* 0x11fae */ /* system skill: timer digit 0 (indexed from shSkillIdxTimeNumber0) */
    short shSkillIdxTimeNumber1;                                   /* 0x11fb0 */ /* system skill: timer digit 1 */
    short shSkillIdxTimeNumber2;                                   /* 0x11fb2 */ /* system skill: timer digit 2 */
    short shSkillIdxTimeNumber3;                                   /* 0x11fb4 */ /* system skill: timer digit 3 */
    short shSkillIdxTimeNumber4;                                   /* 0x11fb6 */ /* system skill: timer digit 4 */
    short shSkillIdxTimeNumber5;                                   /* 0x11fb8 */ /* system skill: timer digit 5 */
    short shSkillIdxTimeNumber6;                                   /* 0x11fba */ /* system skill: timer digit 6 */
    short shSkillIdxTimeNumber7;                                   /* 0x11fbc */ /* system skill: timer digit 7 */
    short shSkillIdxTimeNumber8;                                   /* 0x11fbe */ /* system skill: timer digit 8 */
    short shSkillIdxTimeNumber9;                                   /* 0x11fc0 */ /* system skill: timer digit 9 */
    short shSkillIdxSpecialStockNumber0;                           /* 0x11fc2 */ /* system skill: stock count digit 0 (indexed from here) */
    short shSkillIdxSpecialStockNumber1;                           /* 0x11fc4 */ /* system skill: stock count digit 1 */
    short shSkillIdxSpecialStockNumber2;                           /* 0x11fc6 */ /* system skill: stock count digit 2 */
    short shSkillIdxSpecialStockNumber3;                           /* 0x11fc8 */ /* system skill: stock count digit 3 */
    short shSkillIdxSpecialStockNumber4;                           /* 0x11fca */ /* system skill: stock count digit 4 */
    short shSkillIdxSpecialStockNumber5;                           /* 0x11fcc */ /* system skill: stock count digit 5 */
    short shSkillIdxSpecialStockNumber6;                           /* 0x11fce */ /* system skill: stock count digit 6 */
    short shSkillIdxSpecialStockNumber7;                           /* 0x11fd0 */ /* system skill: stock count digit 7 */
    short shSkillIdxSpecialStockNumber8;                           /* 0x11fd2 */ /* system skill: stock count digit 8 */
    short shSkillIdxSpecialStockNumber9;                           /* 0x11fd4 */ /* system skill: stock count digit 9 */
    short shSkillIdxVictoryMarkOn;                                 /* 0x11fd6 */ /* system skill: victory mark, won */
    short shSkillIdxVictoryMarkOff;                                /* 0x11fd8 */ /* system skill: victory mark, not won */
    short shSkillIdxStageLayout1;                                  /* 0x11fda */ /* system skill: battle UI layout 1 */
    short shSkillIdxStageLayout2;                                  /* 0x11fdc */ /* system skill: battle UI layout 2 */
    short shSkillIdxStageLayout3;                                  /* 0x11fde */ /* system skill: battle UI layout 3 */
    short shSkillIdxStageLayout4;                                  /* 0x11fe0 */ /* system skill: battle UI layout 4 */
    short shSkillIdxStageLayout5;                                  /* 0x11fe2 */ /* system skill: battle UI layout 5 */
    short shSkillIdxStageLayout6;                                  /* 0x11fe4 */ /* system skill: battle UI layout 6 */
    short shSkillIdxStageLayout7;                                  /* 0x11fe6 */ /* system skill: battle UI layout 7 */
    short shSkillIdxStageLayout8;                                  /* 0x11fe8 */ /* system skill: battle UI layout 8 */
    short shSkillIdxStageLayout9;                                  /* 0x11fea */ /* system skill: battle UI layout 9 */
    short shSkillIdxStageLayout10;                                 /* 0x11fec */ /* system skill: battle UI layout 10 */
    short shSkillIdx1pLifeGauge;                                   /* 0x11fee */ /* system skill: 1P life gauge */
    short shSkillIdx2pLifeGauge;                                   /* 0x11ff0 */ /* system skill: 2P life gauge */
    short shSkillIdx1pSpecialGauge;                                /* 0x11ff2 */ /* system skill: 1P special gauge */
    short shSkillIdx2pSpecialGauge;                                /* 0x11ff4 */ /* system skill: 2P special gauge */
    short shSkillIdxPositionTimer;                                 /* 0x11ff6 */ /* system skill: timer position */
    short shSkillIdxPos1pFace;                                     /* 0x11ff8 */ /* system skill: 1P face position */
    short shSkillIdxPos2pFace;                                     /* 0x11ffa */ /* system skill: 2P face position */
    short shSkillIdxPosSpecialStock1p;                             /* 0x11ffc */ /* system skill: 1P stock count position */
    short shSkillIdxPosSpecialStock2p;                             /* 0x11ffe */ /* system skill: 2P stock count position */
    short shSkillIdxPosVictoryMark1p;                              /* 0x12000 */ /* system skill: 1P victory marks position */
    short shSkillIdxPosVictoryMark2p;                              /* 0x12002 */ /* system skill: 2P victory marks position */
    short shSkillIdxTitleCursor;                                   /* 0x12004 */ /* system skill: title menu cursor */
    short shSkillIdxPositionForStoryMode;                          /* 0x12006 */ /* system skill: menu position of 'story mode' */
    short shSkillIdxPositionForVsMode;                             /* 0x12008 */ /* system skill: menu position of 'vs mode' */
    short shSkillIdxContinueCursor;                                /* 0x1200a */ /* system skill: continue screen cursor */
    short shSkillIdxPositionCursorItDoes;                          /* 0x1200c */ /* system skill: continue: 'yes' position */
    short shSkillIdxPositionCursorItDoesNot;                       /* 0x1200e */ /* system skill: continue: 'no' position */
    short shSkillIdx1pVsScreenCursor;                              /* 0x12010 */ /* system skill: 1P character select cursor */
    short shSkillIdx2pVsScreenCursor;                              /* 0x12012 */ /* system skill: 2P character select cursor */
    short shSkillIdx1pVsCursorAfterInput;                          /* 0x12014 */ /* system skill: 1P cursor after choosing */
    short shSkillIdx2pVsCursorAfterInput;                          /* 0x12016 */ /* system skill: 2P cursor after choosing */
    short shSkillIdxPosCursorForTeamBattle;                        /* 0x12018 */ /* system skill: team battle cursor */
    short shSkillIdxPause;                                         /* 0x1201a */ /* system skill: pause */
    short shSkillIdxSpare6;                                        /* 0x1201c */ /* not a built-in skill; not used by the code */
    short shSkillIdxSpare7;                                        /* 0x1201e */ /* not used by the code */
    short shSkillIdxSpare8;                                        /* 0x12020 */ /* not used by the code */
    short shSkillIdxSpare9;                                        /* 0x12022 */ /* not used by the code */
    short shSkillIdxSpare10;                                       /* 0x12024 */ /* not used by the code */
    short shSkillIdxSpare11;                                       /* 0x12026 */ /* not used by the code */
    short shSkillIdxSpare12;                                       /* 0x12028 */ /* not used by the code */
    short shSkillIdxSpare13;                                       /* 0x1202a */ /* not used by the code */
    short shSkillIdxSpare14;                                       /* 0x1202c */ /* not used by the code */
    short shSkillIdxSpare15;                                       /* 0x1202e */ /* not used by the code */
    short shSkillIdxSpare16;                                       /* 0x12030 */ /* not used by the code */
    short shSkillIdxSpare17;                                       /* 0x12032 */ /* not used by the code */
    short shSkillIdxSpare18;                                       /* 0x12034 */ /* not used by the code */
    short shSkillIdxSpare19;                                       /* 0x12036 */ /* not used by the code */
    BYTE pad_12038[0x38];                                          /* 0x12038 */ /* unknown */
    short shCharacterSelectStartX;                                 /* 0x12070 */ /* character select grid: x of the first cell */
    short shCharacterSelectStartY;                                 /* 0x12072 */ /* y of the first cell */
    short shDistanceBetweenCharactersX;                            /* 0x12074 */ /* cell width */
    short shDistanceBetweenCharactersY;                            /* 0x12076 */ /* cell height */
    short shColumnsInSelectScreen;                                 /* 0x12078 */ /* columns */
    short shRowsInSelectScreen;                                    /* 0x1207a */ /* rows */
    short shPlayerOneCursorX;                                      /* 0x1207c */ /* 1P start cursor column */
    short shPlayerOneCursorY;                                      /* 0x1207e */ /* 1P start cursor row */
    short shPlayerOneSelectionWidth;                               /* 0x12080 */ /* 1P selection area width */
    short shPlayerOneSelectionHeight;                              /* 0x12082 */ /* 1P selection area height */
    short shPlayerTwoCursorX;                                      /* 0x12084 */ /* 2P start cursor column */
    short shPlayerTwoCursorY;                                      /* 0x12086 */ /* 2P start cursor row */
    short shPlayerTwoSelectionWidth;                               /* 0x12088 */ /* 2P selection area width */
    short shPlayerTwoSelectionHeight;                              /* 0x1208a */ /* 2P selection area height */
    char cCharacterModeFlags[50];                                  /* 0x1208c */ /* per character file: +0x1 has story mode, +0x2 usable in vs mode */
    BYTE pad_120be[0x3b0];                                         /* 0x120be */ /* unknown */
    BYTE pad_1246e[0x4e];                                          /* 0x1246e */ /* unknown */
};

enum GAME_MODES {   /* kgtGameState.kgtGameMode */
    GAME_MODE_STORY = 0,                    /* 1 player story mode */
    GAME_MODE_VS_SINGLE = 1,                /* versus, one character per side */
    GAME_MODE_VS_TEAM = 2,                  /* versus, teams of up to 4 */
    GAME_MODE_UNK_3 = 3,                    /* not used by the code */
    GAME_MODES_SIGNED = -1                  /* (see kgtJumptableEndpoints) */
};

struct kgtGameState {   /* size 0x1ac: state of the current game (gkgtGameState): mode, round, timer, character select */
    int iCharSelect[8];                                            /* 0x0000 */ /* character file chosen per player */
    int iConfigTestplayStageNb;                                    /* 0x0020 */ /* stage in use (set from giConfigTestplayStageNb, then by the modes) */
    int iCurrentRound;                                             /* 0x0024 */ /* round number */
    int iRoundsAmount;                                             /* 0x0028 */ /* rounds (wins) needed; signed compares in vjmpHandleBattleState */
    DWORD dwRoundPhase;                                            /* 0x002c */ /* 0 before the fight, 1 fighting, 2 round over */
    int iGameTimerInFrames;                                        /* 0x0030 */ /* round time left in frames */
    int iGameStateNumber;                                          /* 0x0034 */ /* 1000 title/menu, 2000 character select, 3000 battle, 4000 story mode progress */
    GAME_MODES kgtGameMode;                                        /* 0x0038 */ /* story / vs single / vs team */
    BYTE pad_003c[0x4];                                            /* 0x003c */ /* unknown */
    int iConfigTestPlayVsMode;                                     /* 0x0040 */ /* test play versus mode (copy of giConfigTestplayVsMode) */
    int iConfigNumberOfRoundsTeamVs;                               /* 0x0044 */ /* rounds of a team game */
    int iConfigNumberOfRounds;                                     /* 0x0048 */ /* rounds of a single game */
    union {
        struct {
            DWORD dwTeamRoster0;                                   /* 0x004c */ /* aiTeamRoster[0] */
            DWORD dwTeamRoster1;                                   /* 0x0050 */ /* aiTeamRoster[1] */
        };
        int aiTeamRoster[8];                                       /* 0x004c */ /* team battle: character file of each member [side * 4 + member] */
    };
    BYTE pad_006c[0x60];                                           /* 0x006c */ /* unknown */
    union {
        struct {
            int iTeamMember0;                                      /* 0x00cc */ /* aiTeamMember[0] */
            int iTeamMember1;                                      /* 0x00d0 */ /* aiTeamMember[1] */
        };
        int aiTeamMember[2];                                       /* 0x00cc */ /* team battle: members chosen / current member per side */
    };
    BYTE pad_00d4[0x18];                                           /* 0x00d4 */ /* unknown */
    int iCarryOverPlayer;                                          /* 0x00ec */ /* player whose life and gauge carry over to the next fight (the winner), -1 = none */
    DWORD dwTempHealth;                                            /* 0x00f0 */ /* saved life of iCarryOverPlayer */
    DWORD dwTempSpecialGaugeTokens;                                /* 0x00f4 */ /* saved stocks of iCarryOverPlayer */
    int iTempSpecialGauge;                                         /* 0x00f8 */ /* saved gauge of iCarryOverPlayer */
    union {
        struct {
            int iTeamColor0;                                       /* 0x00fc */ /* aiTeamColor[0] */
            BYTE pad_0100[0xc];                                    /* 0x0100 */ /* unknown */
            int iTeamColor4;                                       /* 0x010c */ /* aiTeamColor[4] */
        };
        int aiTeamColor[8];                                        /* 0x00fc */ /* colour (button) chosen for each member [side * 4 + member] */
    };
    BYTE pad_011c[0x60];                                           /* 0x011c */ /* unknown */
    union {
        struct {
            int bPlayerChose0;                                     /* 0x017c */ /* abPlayerChose[0] */
            int bPlayerChose1;                                     /* 0x0180 */ /* abPlayerChose[1] */
        };
        int abPlayerChose[2];                                      /* 0x017c */ /* character select: side has confirmed its choice */
    };
    BYTE pad_0184[0x18];                                           /* 0x0184 */ /* unknown */
    int bPaused;                                                   /* 0x019c */ /* game paused */
    UINT uStatusDisplay;                                           /* 0x01a0 */ /* show the FPS/status line (toggled by a key, saved in the ini) */
    int iTargetPlayer;                                             /* 0x01a4 */ /* opponent player 1 last hit (or the last one standing), whose face and life the UI shows; -1 = none */
    DWORD dwTargetPlayerTimer;                                     /* 0x01a8 */ /* frames until iTargetPlayer is reset (1000 after a hit) */
};

struct kgt_demo_file {   /* size 0x2669 (+ pointer growth of the kgt_core): a loaded demo file (gkgtLoadedDemo) */
    kgt_core kgtCore;                                              /* 0x0000 */ /* the file's common part */
    union {
        char cBgmSelection;                                        /* 0x2234 */ /* low byte of wBgmSelection */
        unsigned short wBgmSelection;                              /* 0x2234 */ /* background music: index into kgtCore.pkgtSounds */
    };
    char cSkipWithInput;                                           /* 0x2236 */ /* the demo can be skipped with a button */
    BYTE pad_2237[0x2];                                            /* 0x2237 */ /* unknown */
    int iTime;                                                     /* 0x2239 */ /* demo length in frames */
    BYTE pad_223d[0x400];                                          /* 0x223d */ /* unknown */
    BYTE pad_263d[0x2c];                                           /* 0x263d */ /* size per vMemzero(&..., sizeof) */
};

struct kgt_stage {   /* size 0x2691 (+ pointer growth of the kgt_core): a loaded stage file (gkgtLoadedStage) */
    kgt_core kgtCore;                                              /* 0x0000 */ /* the file's common part */
    union {
        char cBgmSelection;                                        /* 0x2234 */ /* low byte of wBgmSelection */
        unsigned short wBgmSelection;                              /* 0x2234 */ /* background music: index into kgtCore.pkgtSounds */
    };
    BYTE pad_2236[0x407];                                          /* 0x2236 */ /* unknown */
    BYTE pad_263d[0x54];                                           /* 0x263d */ /* runtime colour/shake effects of the stage (giStageFlash*, giShake*), then size per vMemzero(&..., sizeof) */
};

struct online_tcp_struct {   /* size 0x140: unused Ghidra structure, probably a netplay message */
    DWORD dwUnk0000;                                               /* 0x0000 */ /* unknown */
    DWORD dwUnk0004;                                               /* 0x0004 */ /* unknown */
    BYTE pad_0008[0x138];                                          /* 0x0008 */ /* unknown */
};

/* SNDOBJ from the DirectX SDK sample dsutil.c: a wave with iAlloc duplicated buffers */
struct kgtWav {   /* size 0x10 + 4 * iAlloc (+ pointer growth; allocated with offsetof(kgtWav, pBuffers)): a loaded wave (dsutil.c) */
    BYTE * pbWaveData;                                             /* 0x0000 */ /* wave data (SDK name) */
    DWORD cbWaveSize;                                              /* 0x0004 */ /* wave data size in bytes (SDK name) */
    int iAlloc;                                                    /* 0x0008 */ /* number of buffers */
    int iCurrent;                                                  /* 0x000c */ /* buffer to play next */
    LPDIRECTSOUNDBUFFER pBuffers[2];                               /* 0x0010 */ /* the buffers, really [iAlloc] */
};

struct wavSoundFile {   /* size 0x30: RIFF WAVE file header (unused Ghidra structure) */
    int iFileTypeBlocId;                                           /* 0x0000 */ /* 'RIFF' */
    int iFileSize;                                                 /* 0x0004 */ /* file size - 8 */
    int iFileFormatId;                                             /* 0x0008 */ /* 'WAVE' */
    int iFormatBlocId;                                             /* 0x000c */ /* 'fmt ' */
    unsigned int uBlocSize;                                        /* 0x0010 */ /* size of the format block */
    short shAudioFormat;                                           /* 0x0014 */ /* 1 = PCM */
    short shNbrChannels;                                           /* 0x0016 */ /* channels */
    int iFrequency;                                                /* 0x0018 */ /* samples per second */
    int iBytePerSec;                                               /* 0x001c */ /* bytes per second */
    short shBytePerBloc;                                           /* 0x0020 */ /* bytes per sample frame */
    short shBitsPerSample;                                         /* 0x0022 */ /* bits per sample */
    int iDataBlocId;                                               /* 0x0024 */ /* 'data' */
    int iDataSize;                                                 /* 0x0028 */ /* size of the samples */
    BYTE * pSampledData;                                           /* 0x002c */ /* samples */
};

struct kgtBMPINFO {   /* size 0x14 (+ pointer growth): an external bitmap (text.bmp, 1.bmp.., stage bg_*.bmp) converted for drawing (gkgtBitmaps) */
    void * pData;                                                  /* 0x0000 */ /* GlobalAlloc'ed data: RGB555 palette of iColorsUsed + 1 entries, then the 8-bit pixels (16-bit images: pixels only) */
    int iWidth;                                                    /* 0x0004 */ /* width in pixels (rounded up to a multiple of 4 or 8) */
    int iHeight;                                                   /* 0x0008 */ /* height in pixels */
    int iColorsUsed;                                               /* 0x000c */ /* palette entries - 1 */
    int iCompressedSize;                                           /* 0x0010 */ /* 0 = raw; cleared by iLoadExternalImage */
};

struct UNK_STRUCT_A {   /* size 0x9fa: unused Ghidra structure */
    DWORD dwUnk0000;                                               /* 0x0000 */ /* unknown */
    BYTE pad_0004[0x10];                                           /* 0x0004 */ /* unknown */
    kgtBMPINFO kgtBmpInfo;                                         /* 0x0014 */ /* a bitmap */
    BYTE pad_0028[0x9d2];                                          /* 0x0028 */ /* unknown */
};

struct UNK_STRUCT_B {   /* size 0x701f8: unused Ghidra overlay of gkgtLoadedCharacter[8] (8 * 0xe03f bytes); names from its Ghidra labels */
    BYTE cUnk0000[8];                                              /* 0x0000 */ /* = gkgtLoadedCharacter[0].kgtCore */
    BYTE pad_0008[0x8];                                            /* 0x0008 */ /* unknown */
    BYTE cUnk0010[12];                                             /* 0x0010 */ /* = gkgtLoadedCharacter[0].kgtCore + 0x10 */
    BYTE pad_001c[0xf4];                                           /* 0x001c */ /* unknown */
    DWORD dwUnk0110;                                               /* 0x0110 */ /* = gkgtLoadedCharacter[0].kgtCore + 0x110 */
    BYTE pad_0114[0x2120];                                         /* 0x0114 */ /* unknown */
    DWORD dwUnk2234;                                               /* 0x2234 */ /* = gkgtLoadedCharacter[0].dpidOnline */
    DWORD dwUnk2238;                                               /* 0x2238 */ /* = gkgtLoadedCharacter[0].iOnlineState */
    BYTE pad_223c[0xa];                                            /* 0x223c */ /* unknown */
    BYTE cUnk2246[7];                                              /* 0x2246 */ /* = gkgtLoadedCharacter[0].kgtCpuCommands */
    BYTE pad_224d[0x2b55];                                         /* 0x224d */ /* unknown */
    BYTE cUnk4DA2[10];                                             /* 0x4da2 */ /* = gkgtLoadedCharacter[0].kgtCommands */
    BYTE pad_4dac[0x2000];                                         /* 0x4dac */ /* unknown */
    void * pUnk6DAC;                                               /* 0x6dac */ /* = gkgtLoadedCharacter[0].kgtHitJunctions + 0x2 */
    void * pUnk6DB0;                                               /* 0x6db0 */ /* = gkgtLoadedCharacter[0].kgtHitJunctions + 0x6 */
    void * pUnk6DB4;                                               /* 0x6db4 */ /* = gkgtLoadedCharacter[0].kgtHitJunctions + 0xa */
    void * pUnk6DB8;                                               /* 0x6db8 */ /* = gkgtLoadedCharacter[0].kgtHitJunctions + 0xe */
    void * pUnk6DBC;                                               /* 0x6dbc */ /* = gkgtLoadedCharacter[0].kgtHitJunctions + 0x12 */
    void * pUnk6DC0;                                               /* 0x6dc0 */ /* = gkgtLoadedCharacter[0].kgtHitJunctions + 0x16 */
    void * pUnk6DC4;                                               /* 0x6dc4 */ /* = gkgtLoadedCharacter[0].kgtHitJunctions + 0x1a */
    void * pUnk6DC8;                                               /* 0x6dc8 */ /* = gkgtLoadedCharacter[0].kgtHitJunctions + 0x1e */
    void * pUnk6DCC;                                               /* 0x6dcc */ /* = gkgtLoadedCharacter[0].kgtHitJunctions + 0x22 */
    void * pUnk6DD0;                                               /* 0x6dd0 */ /* = gkgtLoadedCharacter[0].kgtHitJunctions + 0x26 */
    BYTE pad_6dd4[0x7d0];                                          /* 0x6dd4 */ /* unknown */
    DWORD dwUnk75A4;                                               /* 0x75a4 */ /* = gkgtLoadedCharacter[0].shSkillIdxCharSelectPic */
    DWORD dwUnk75A8;                                               /* 0x75a8 */ /* = gkgtLoadedCharacter[0].shSkillIdxR1 */
    BYTE pad_75ac[0x6941];                                         /* 0x75ac */ /* unknown */
    DWORD dwUnkDEED;                                               /* 0xdeed */ /* = gkgtLoadedCharacter[0].iWins */
    DWORD dwUnkDEF1;                                               /* 0xdef1 */ /* = gkgtLoadedCharacter[0].iLosses */
    DWORD dwUnkDEF5;                                               /* 0xdef5 */ /* = gkgtLoadedCharacter[0].pkgtoSelf */
    DWORD dwUnkDEF9;                                               /* 0xdef9 */ /* = gkgtLoadedCharacter[0].pLastOpponent */
    BYTE pad_defd[0x8];                                            /* 0xdefd */ /* unknown */
    int iP1Health;                                                 /* 0xdf05 */ /* = gkgtLoadedCharacter[0].iHealth */
    BYTE pad_df09[0x8];                                            /* 0xdf09 */ /* unknown */
    DWORD dwUnkDF11;                                               /* 0xdf11 */ /* = gkgtLoadedCharacter[0].iLifeMax */
    DWORD dwP1Stocks;                                              /* 0xdf15 */ /* = gkgtLoadedCharacter[0].iSpecialGaugeTokens */
    BYTE pad_df19[0x4];                                            /* 0xdf19 */ /* unknown */
    DWORD dwUnkDF1D;                                               /* 0xdf1d */ /* = gkgtLoadedCharacter[0].iSpecialGauge */
    DWORD dwUnkDF21;                                               /* 0xdf21 */ /* = gkgtLoadedCharacter[0].iSpecialMax */
    DWORD dwUnkDF25;                                               /* 0xdf25 */ /* = gkgtLoadedCharacter[0].iDamageBar */
    BYTE pad_df29[0x4];                                            /* 0xdf29 */ /* unknown */
    DWORD dwUnkDF2D;                                               /* 0xdf2d */ /* = gkgtLoadedCharacter[0].iHitFlags */
    DWORD dwUnkDF31;                                               /* 0xdf31 */ /* = gkgtLoadedCharacter[0].iTimesHit */
    BYTE pad_df35[0xc];                                            /* 0xdf35 */ /* unknown */
    DWORD dwUnkDF41;                                               /* 0xdf41 */ /* = gkgtLoadedCharacter[0].iCurrentXPos */
    DWORD dwUnkDF45;                                               /* 0xdf45 */ /* = gkgtLoadedCharacter[0].iCurrentYPos */
    BYTE pad_df49[0x3c];                                           /* 0xdf49 */ /* unknown */
    DWORD dwUnkDF85;                                               /* 0xdf85 */ /* = gkgtLoadedCharacter[0].iCurrentActionCancellableFlag */
    DWORD dwUnkDF89;                                               /* 0xdf89 */ /* = gkgtLoadedCharacter[0].iOnlineLag */
    DWORD dwUnkDF8D;                                               /* 0xdf8d */ /* = gkgtLoadedCharacter[0].iInputBufferPos */
    BYTE pad_df91[0x1e];                                           /* 0xdf91 */ /* unknown */
    int iUnkDFAF;                                                  /* 0xdfaf */ /* = gkgtLoadedCharacter[0].shVarM */
    BYTE pad_dfb3[0x8];                                            /* 0xdfb3 */ /* unknown */
    DWORD dwUnkDFBB;                                               /* 0xdfbb */ /* = gkgtLoadedCharacter[0].iWinPoints */
    DWORD dwUnkDFBF;                                               /* 0xdfbf */ /* = gkgtLoadedCharacter[0].pMNumberObjs */
    DWORD dwUnkDFC3;                                               /* 0xdfc3 */ /* = gkgtLoadedCharacter[0].pMNumberObjs + 0x4 */
    BYTE pad_dfc7[0x38];                                           /* 0xdfc7 */ /* unknown */
    DWORD dwUnkDFFF;                                               /* 0xdfff */ /* = gkgtLoadedCharacter[0].bUseStoredInput */
    DWORD dwUnkE003;                                               /* 0xe003 */ /* = gkgtLoadedCharacter[0].cStoredInput */
    BYTE pad_e007[0x4];                                            /* 0xe007 */ /* unknown */
    DWORD dwUnkE00B;                                               /* 0xe00b */ /* = gkgtLoadedCharacter[0].iColor */
    BYTE pad_e00f[0x18];                                           /* 0xe00f */ /* unknown */
    int iUnkE027;                                                  /* 0xe027 */ /* = gkgtLoadedCharacter[0].iFlashTimeLeft */
    BYTE pad_e02b[0x14];                                           /* 0xe02b */ /* unknown */
    BYTE cUnkE03F[8];                                              /* 0xe03f */ /* = gkgtLoadedCharacter[1].kgtCore */
    BYTE pad_e047[0x4];                                            /* 0xe047 */ /* unknown */
    BYTE cUnkE04B;                                                 /* 0xe04b */ /* = gkgtLoadedCharacter[1].kgtCore + 0xc */
    BYTE pad_e04c[0x3];                                            /* 0xe04c */ /* unknown */
    BYTE cUnkE04F[1];                                              /* 0xe04f */ /* = gkgtLoadedCharacter[1].kgtCore + 0x10 */
    BYTE pad_e050[0xff];                                           /* 0xe050 */ /* unknown */
    DWORD dwUnkE14F;                                               /* 0xe14f */ /* = gkgtLoadedCharacter[1].kgtCore + 0x110 */
    BYTE pad_e153[0x1da7];                                         /* 0xe153 */ /* unknown */
    void * pUnkFEFA;                                               /* 0xfefa */ /* = gkgtLoadedCharacter[1].kgtCore + 0x1ebb */
    void * pUnkFEFE;                                               /* 0xfefe */ /* = gkgtLoadedCharacter[1].kgtCore + 0x1ebf */
    void * pUnkFF02;                                               /* 0xff02 */ /* = gkgtLoadedCharacter[1].kgtCore + 0x1ec3 */
    void * pUnkFF06;                                               /* 0xff06 */ /* = gkgtLoadedCharacter[1].kgtCore + 0x1ec7 */
    void * pUnkFF0A;                                               /* 0xff0a */ /* = gkgtLoadedCharacter[1].kgtCore + 0x1ecb */
    void * pUnkFF0E;                                               /* 0xff0e */ /* = gkgtLoadedCharacter[1].kgtCore + 0x1ecf */
    void * pUnkFF12;                                               /* 0xff12 */ /* = gkgtLoadedCharacter[1].kgtCore + 0x1ed3 */
    void * pUnkFF16;                                               /* 0xff16 */ /* = gkgtLoadedCharacter[1].kgtCore + 0x1ed7 */
    void * pUnkFF1A;                                               /* 0xff1a */ /* = gkgtLoadedCharacter[1].kgtCore + 0x1edb */
    void * pUnkFF1E;                                               /* 0xff1e */ /* = gkgtLoadedCharacter[1].kgtCore + 0x1edf */
    void * pUnkFF22;                                               /* 0xff22 */ /* = gkgtLoadedCharacter[1].kgtCore + 0x1ee3 */
    void * pUnkFF26;                                               /* 0xff26 */ /* = gkgtLoadedCharacter[1].kgtCore + 0x1ee7 */
    void * pUnkFF2A;                                               /* 0xff2a */ /* = gkgtLoadedCharacter[1].kgtCore + 0x1eeb */
    void * pUnkFF2E;                                               /* 0xff2e */ /* = gkgtLoadedCharacter[1].kgtCore + 0x1eef */
    void * pUnkFF32;                                               /* 0xff32 */ /* = gkgtLoadedCharacter[1].kgtCore + 0x1ef3 */
    void * pUnkFF36;                                               /* 0xff36 */ /* = gkgtLoadedCharacter[1].kgtCore + 0x1ef7 */
    void * pUnkFF3A;                                               /* 0xff3a */ /* = gkgtLoadedCharacter[1].kgtCore + 0x1efb */
    void * pUnkFF3E;                                               /* 0xff3e */ /* = gkgtLoadedCharacter[1].kgtCore + 0x1eff */
    void * pUnkFF42;                                               /* 0xff42 */ /* = gkgtLoadedCharacter[1].kgtCore + 0x1f03 */
    void * pUnkFF46;                                               /* 0xff46 */ /* = gkgtLoadedCharacter[1].kgtCore + 0x1f07 */
    void * pUnkFF4A;                                               /* 0xff4a */ /* = gkgtLoadedCharacter[1].kgtCore + 0x1f0b */
    void * pUnkFF4E;                                               /* 0xff4e */ /* = gkgtLoadedCharacter[1].kgtCore + 0x1f0f */
    void * pUnkFF52;                                               /* 0xff52 */ /* = gkgtLoadedCharacter[1].kgtCore + 0x1f13 */
    void * pUnkFF56;                                               /* 0xff56 */ /* = gkgtLoadedCharacter[1].kgtCore + 0x1f17 */
    void * pUnkFF5A;                                               /* 0xff5a */ /* = gkgtLoadedCharacter[1].kgtCore + 0x1f1b */
    void * pUnkFF5E;                                               /* 0xff5e */ /* = gkgtLoadedCharacter[1].kgtCore + 0x1f1f */
    void * pUnkFF62;                                               /* 0xff62 */ /* = gkgtLoadedCharacter[1].kgtCore + 0x1f23 */
    void * pUnkFF66;                                               /* 0xff66 */ /* = gkgtLoadedCharacter[1].kgtCore + 0x1f27 */
    void * pUnkFF6A;                                               /* 0xff6a */ /* = gkgtLoadedCharacter[1].kgtCore + 0x1f2b */
    void * pUnkFF6E;                                               /* 0xff6e */ /* = gkgtLoadedCharacter[1].kgtCore + 0x1f2f */
    void * pUnkFF72;                                               /* 0xff72 */ /* = gkgtLoadedCharacter[1].kgtCore + 0x1f33 */
    void * pUnkFF76;                                               /* 0xff76 */ /* = gkgtLoadedCharacter[1].kgtCore + 0x1f37 */
    void * pUnkFF7A;                                               /* 0xff7a */ /* = gkgtLoadedCharacter[1].kgtCore + 0x1f3b */
    void * pUnkFF7E;                                               /* 0xff7e */ /* = gkgtLoadedCharacter[1].kgtCore + 0x1f3f */
    BYTE pad_ff82[0x2f1];                                          /* 0xff82 */ /* unknown */
    DWORD dwUnk10273;                                              /* 0x10273 */ /* = gkgtLoadedCharacter[1].dpidOnline */
    DWORD dwUnk10277;                                              /* 0x10277 */ /* = gkgtLoadedCharacter[1].iOnlineState */
    BYTE pad_1027b[0xa];                                           /* 0x1027b */ /* unknown */
    BYTE cUnk10285[1];                                             /* 0x10285 */ /* = gkgtLoadedCharacter[1].kgtCpuCommands */
    BYTE pad_10286[0x5333];                                        /* 0x10286 */ /* unknown */
    WORD wUnk155B9;                                                /* 0x155b9 */ /* = gkgtLoadedCharacter[1].shSkillIdxStanding */
    WORD wUnk155BB;                                                /* 0x155bb */ /* = gkgtLoadedCharacter[1].shSkillIdxForward */
    WORD wUnk155BD;                                                /* 0x155bd */ /* = gkgtLoadedCharacter[1].shSkillIdxBackward */
    WORD wUnk155BF;                                                /* 0x155bf */ /* = gkgtLoadedCharacter[1].shSkillIdxJumpUp */
    WORD wUnk155C1;                                                /* 0x155c1 */ /* = gkgtLoadedCharacter[1].shSkillIdxFrontJump */
    WORD wUnk155C3;                                                /* 0x155c3 */ /* = gkgtLoadedCharacter[1].shSkillIdxBackJump */
    WORD wUnk155C5;                                                /* 0x155c5 */ /* = gkgtLoadedCharacter[1].shSkillIdxFalling */
    WORD wUnk155C7;                                                /* 0x155c7 */ /* = gkgtLoadedCharacter[1].shSkillIdxMidCrouch */
    WORD wUnk155C9;                                                /* 0x155c9 */ /* = gkgtLoadedCharacter[1].shSkillIdxCrouching */
    WORD wUnk155CB;                                                /* 0x155cb */ /* = gkgtLoadedCharacter[1].shSkillIdxStandFromCrouch */
    WORD wUnk155CD;                                                /* 0x155cd */ /* = gkgtLoadedCharacter[1].shSkillIdxCrouchAdvance */
    WORD wUnk155CF;                                                /* 0x155cf */ /* = gkgtLoadedCharacter[1].shSkillIdxCrouchRetreat */
    WORD wUnk155D1;                                                /* 0x155d1 */ /* = gkgtLoadedCharacter[1].shSkillIdxTurnStanding */
    WORD wUnk155D3;                                                /* 0x155d3 */ /* = gkgtLoadedCharacter[1].shSkillIdxTurnCrouching */
    WORD wUnk155D5;                                                /* 0x155d5 */ /* = gkgtLoadedCharacter[1].shSkillIdxButtonGuardStand */
    WORD wUnk155D7;                                                /* 0x155d7 */ /* = gkgtLoadedCharacter[1].shSkillIdxButtonGuardCrouch */
    WORD wUnk155D9;                                                /* 0x155d9 */ /* = gkgtLoadedCharacter[1].shSkillIdxButtonGuardAir */
    WORD wUnk155DB;                                                /* 0x155db */ /* = gkgtLoadedCharacter[1].shSkillIdxStart */
    WORD * pUnk155DD;                                              /* 0x155dd */ /* = gkgtLoadedCharacter[1].shSkillIdxVictory */
    WORD wUnk155E1;                                                /* 0x155e1 */ /* = gkgtLoadedCharacter[1].shSkillIdxDraw */
    WORD wUnk155E3;                                                /* 0x155e3 */ /* = gkgtLoadedCharacter[1].shSkillIdxCharSelectPic */
    unsigned long * * * * * pUnk155E5;                             /* 0x155e5 */ /* = gkgtLoadedCharacter[1].shSkillIdxStageFacePic */
    BYTE pad_155e9[0x6f7];                                         /* 0x155e9 */ /* unknown */
    WORD wUnk15CE0;                                                /* 0x15ce0 */ /* = gkgtLoadedCharacter[1].shYPosOfSideHp */
    BYTE pad_15ce2[0x624a];                                        /* 0x15ce2 */ /* unknown */
    int iP2Wins;                                                   /* 0x1bf2c */ /* = gkgtLoadedCharacter[1].iWins */
    BYTE pad_1bf30[0x4];                                           /* 0x1bf30 */ /* unknown */
    DWORD dwUnk1BF34;                                              /* 0x1bf34 */ /* = gkgtLoadedCharacter[1].pkgtoSelf */
    DWORD dwUnk1BF38;                                              /* 0x1bf38 */ /* = gkgtLoadedCharacter[1].pLastOpponent */
    BYTE pad_1bf3c[0x8];                                           /* 0x1bf3c */ /* unknown */
    int iP2Health;                                                 /* 0x1bf44 */ /* = gkgtLoadedCharacter[1].iHealth */
    BYTE pad_1bf48[0x4];                                           /* 0x1bf48 */ /* unknown */
    DWORD dwUnk1BF4C;                                              /* 0x1bf4c */ /* = gkgtLoadedCharacter[1].shShowLife */
    DWORD dwUnk1BF50;                                              /* 0x1bf50 */ /* = gkgtLoadedCharacter[1].iLifeMax */
    int iP2Stocks;                                                 /* 0x1bf54 */ /* = gkgtLoadedCharacter[1].iSpecialGaugeTokens */
    BYTE pad_1bf58[0x4];                                           /* 0x1bf58 */ /* unknown */
    DWORD dwUnk1BF5C;                                              /* 0x1bf5c */ /* = gkgtLoadedCharacter[1].iSpecialGauge */
    DWORD dwUnk1BF60;                                              /* 0x1bf60 */ /* = gkgtLoadedCharacter[1].iSpecialMax */
    DWORD dwUnk1BF64;                                              /* 0x1bf64 */ /* = gkgtLoadedCharacter[1].iDamageBar */
    BYTE pad_1bf68[0x4];                                           /* 0x1bf68 */ /* unknown */
    DWORD dwUnk1BF6C;                                              /* 0x1bf6c */ /* = gkgtLoadedCharacter[1].iHitFlags */
    DWORD dwUnk1BF70;                                              /* 0x1bf70 */ /* = gkgtLoadedCharacter[1].iTimesHit */
    BYTE pad_1bf74[0xc];                                           /* 0x1bf74 */ /* unknown */
    DWORD dwUnk1BF80;                                              /* 0x1bf80 */ /* = gkgtLoadedCharacter[1].iCurrentXPos */
    DWORD dwUnk1BF84;                                              /* 0x1bf84 */ /* = gkgtLoadedCharacter[1].iCurrentYPos */
    BYTE pad_1bf88[0x14];                                          /* 0x1bf88 */ /* unknown */
    DWORD dwUnk1BF9C;                                              /* 0x1bf9c */ /* = gkgtLoadedCharacter[1].bCpuControlled */
    DWORD dwUnk1BFA0;                                              /* 0x1bfa0 */ /* = gkgtLoadedCharacter[1].iCpuLevel */
    DWORD dwUnk1BFA4;                                              /* 0x1bfa4 */ /* = gkgtLoadedCharacter[1].iCpuMode */
    BYTE pad_1bfa8[0x20];                                          /* 0x1bfa8 */ /* unknown */
    DWORD dwUnk1BFC8;                                              /* 0x1bfc8 */ /* = gkgtLoadedCharacter[1].iOnlineLag */
    DWORD dwUnk1BFCC;                                              /* 0x1bfcc */ /* = gkgtLoadedCharacter[1].iInputBufferPos */
    BYTE pad_1bfd0[0x1e];                                          /* 0x1bfd0 */ /* unknown */
    int iUnk1BFEE;                                                 /* 0x1bfee */ /* = gkgtLoadedCharacter[1].shVarM */
    BYTE pad_1bff2[0x8];                                           /* 0x1bff2 */ /* unknown */
    DWORD dwUnk1BFFA;                                              /* 0x1bffa */ /* = gkgtLoadedCharacter[1].iWinPoints */
    BYTE pad_1bffe[0x4c];                                          /* 0x1bffe */ /* unknown */
    DWORD dwUnk1C04A;                                              /* 0x1c04a */ /* = gkgtLoadedCharacter[1].iColor */
    DWORD dwUnk1C04E;                                              /* 0x1c04e */ /* = gkgtLoadedCharacter[1].bImageShown */
    BYTE pad_1c052[0x14];                                          /* 0x1c052 */ /* unknown */
    DWORD dwUnk1C066;                                              /* 0x1c066 */ /* = gkgtLoadedCharacter[1].iFlashTimeLeft */
    BYTE pad_1c06a[0x224c];                                        /* 0x1c06a */ /* unknown */
    DWORD dwUnk1E2B6;                                              /* 0x1e2b6 */ /* = gkgtLoadedCharacter[2].iOnlineState */
    BYTE pad_1e2ba[0xbcc9];                                        /* 0x1e2ba */ /* unknown */
    DWORD dwUnk29F83;                                              /* 0x29f83 */ /* = gkgtLoadedCharacter[2].iHealth */
    BYTE pad_29f87[0x4];                                           /* 0x29f87 */ /* unknown */
    DWORD dwUnk29F8B;                                              /* 0x29f8b */ /* = gkgtLoadedCharacter[2].shShowLife */
    BYTE pad_29f8f[0xfe];                                          /* 0x29f8f */ /* unknown */
    DWORD dwUnk2A08D;                                              /* 0x2a08d */ /* = gkgtLoadedCharacter[2].bImageShown */
    BYTE pad_2a091[0x46167];                                       /* 0x2a091 */ /* unknown */
};

struct UNK_STRUCT_F {   /* size 0x870: unused Ghidra structure with the size of kgt_debug_a */
    DWORD dwUnk0000;                                               /* 0x0000 */ /* unknown */
    char cUnk0004;                                                 /* 0x0004 */ /* unknown */
    BYTE pad_0005[0x35];                                           /* 0x0005 */ /* unknown */
    char cUnk003A;                                                 /* 0x003a */ /* unknown */
    BYTE pad_003b[0x9];                                            /* 0x003b */ /* unknown */
    DWORD dwUnk0044;                                               /* 0x0044 */ /* unknown */
    BYTE pad_0048[0x44];                                           /* 0x0048 */ /* unknown */
    DWORD dwUnk008C;                                               /* 0x008c */ /* unknown */
    BYTE pad_0090[0x74e];                                          /* 0x0090 */ /* unknown */
    char cUnk07DE;                                                 /* 0x07de */ /* unknown */
    BYTE pad_07df[0x1];                                            /* 0x07df */ /* unknown */
    DWORD dwUnk07E0;                                               /* 0x07e0 */ /* unknown */
    char cUnk07E4;                                                 /* 0x07e4 */ /* unknown */
    BYTE pad_07e5[0x3f];                                           /* 0x07e5 */ /* unknown */
    DWORD dwUnk0824;                                               /* 0x0824 */ /* unknown */
    DWORD dwUnk0828;                                               /* 0x0828 */ /* unknown */
    char cUnk082C;                                                 /* 0x082c */ /* unknown */
    BYTE pad_082d[0x3f];                                           /* 0x082d */ /* unknown */
    DWORD dwUnk086C;                                               /* 0x086c */ /* unknown */
};

struct UNK_0x48_struct {   /* size 0x48: one on-screen debug message line (debug.c) */
    COLORREF dwColor;                                              /* 0x0000 */ /* text colour (COLORREF) */
    char szText[64];                                               /* 0x0004 */ /* message */
    int iTimer;                                                    /* 0x0044 */ /* frames left to show (600 when added) */
};

struct kgt_debug_a {   /* size 0x870: the on-screen debug message log (gkgtDebugLog) */
    UNK_0x48_struct kgtLines[30];                                  /* 0x0000 */ /* [0] = newest */
};

/* one captured frame of an after-image trail (16 bytes in the original) */
typedef struct kgtTrailFrame {
    int iX;                                                        /* 0x0000 */ /* x (16.16) */
    int iY;                                                        /* 0x0004 */ /* y (16.16) */
    int iFlags;                                                    /* 0x0008 */ /* bit 0 image flip (step flag 0x4000), bit 2 mirrored (iPlayerLookingRight * 4) */
    kgtSkill * pImage;                                             /* 0x000c */ /* the image step shown (kgtSkillImageStep) */
} kgtTrailFrame;

struct unk_0x650_struct {   /* size 0x650 (+ pointer growth): an after-image trail (script command AI): header + 100 captured frames (the original's aiData[404]: frame n at aiData[(n + 1) * 4]) */
    int bInUse;                                                    /* 0x0000 */ /* trail in use */
    int iPos;                                                      /* 0x0004 */ /* next frame slot (0..99) */
    kgtSkill * pStep;                                              /* 0x0008 */ /* the AI script step: [3] frames shown, [4] capture interval, [5] blend type, [6] colour mode, [7..10] r g b alpha */
    int iTimer;                                                    /* 0x000c */ /* frames until the next capture */
    kgtTrailFrame kgtFrames[100];                                  /* 0x0010 */ /* the captured frames (ring buffer) */
};

/* draw list rebuilt every frame by vProcessEngineObjects (main.c): one list per layer (iDepth) */
typedef struct kgtDrawNode {   /* size 0x8 (+ pointer growth) */
    kgtEngineObject * pObj;                                        /* 0x0000 */ /* the object */
    struct kgtDrawNode * pNext;                                    /* 0x0004 */ /* next object of the same layer, NULL at the end */
} kgtDrawNode;

typedef struct kgtDrawLayer {   /* size 0x8 (+ pointer growth) */
    kgtDrawNode * pHead;                                           /* 0x0000 */ /* first node, valid only when pTail is not NULL */
    kgtDrawNode * pTail;                                           /* 0x0004 */ /* last node; NULL = empty layer */
} kgtDrawLayer;

struct POSS_VTABLE_GAME_STATE {   /* size 0x48: Ghidra view of gpfnGamestateJumptable (engine.c): one handler per kgtJumptableEndpoints value */
    BYTE * pfnEmpty;                                               /* 0x0000 */ /* handler of iJumpIdx 0 */
    BYTE * pfnResetIdx;                                            /* 0x0004 */ /* handler of iJumpIdx 1 */
    BYTE * pfnStartGame;                                           /* 0x0008 */ /* handler of iJumpIdx 2 */
    BYTE * pfnScreenControl;                                       /* 0x000c */ /* handler of iJumpIdx 3 */
    BYTE * pfnReadScript;                                          /* 0x0010 */ /* handler of iJumpIdx 4 */
    BYTE * pfnFadeDriftEffect;                                     /* 0x0014 */ /* handler of iJumpIdx 5 */
    BYTE * pfnUpdateTimer;                                         /* 0x0018 */ /* handler of iJumpIdx 6 */
    BYTE * pfnHitComboCounter;                                     /* 0x001c */ /* handler of iJumpIdx 7 */
    BYTE * pfnRoundStart;                                          /* 0x0020 */ /* handler of iJumpIdx 8 */
    BYTE * pfnIdleA;                                               /* 0x0024 */ /* handler of iJumpIdx 9 */
    BYTE * pfnCharacterSelectScreen;                               /* 0x0028 */ /* handler of iJumpIdx 10 */
    BYTE * pfnIdleB;                                               /* 0x002c */ /* handler of iJumpIdx 11 */
    BYTE * pfnMenuTraversal;                                       /* 0x0030 */ /* handler of iJumpIdx 12 */
    BYTE * pfnGameOverScreen;                                      /* 0x0034 */ /* handler of iJumpIdx 13 */
    BYTE * pfnBattleState;                                         /* 0x0038 */ /* handler of iJumpIdx 14 */
    BYTE * pfnBattleUi;                                            /* 0x003c */ /* handler of iJumpIdx 15 */
    BYTE * pfnStoryMode;                                           /* 0x0040 */ /* handler of iJumpIdx 16 */
    BYTE * pfnDisplayTitleScreen;                                  /* 0x0044 */ /* handler of iJumpIdx 17 */
};

#pragma pack(pop)

/* size checks: the 32-bit size of the original plus KGT_PTR_GROWTH per pointer (or pointer-sized
   member); the file records (no pointers) have their file size in both builds */
#define KGT_ASSERT_SIZE(T, size32, nptr) \
    _Static_assert(sizeof(T) == (size32) + (nptr) * KGT_PTR_GROWTH, "size of " #T)
KGT_ASSERT_SIZE(kgtPallette, 0x4, 0);
KGT_ASSERT_SIZE(kgt_core, 0x2234, 4);
KGT_ASSERT_SIZE(kgtSkillHeader, 0x27, 0);
KGT_ASSERT_SIZE(kgtSkill, 0x10, 0);
KGT_ASSERT_SIZE(kgtImageHeader, 0x14, 1);
KGT_ASSERT_SIZE(kgtImageHeaderFile, 0x14, 0);
KGT_ASSERT_SIZE(kgtSound, 0x2a, 2);
KGT_ASSERT_SIZE(kgtSoundFile, 0x2a, 0);
KGT_ASSERT_SIZE(kgtCharacterCPUCommandSkillFull, 0x7, 0);
KGT_ASSERT_SIZE(kgtCharacterCPUCommandSkillShort, 0x6, 0);
KGT_ASSERT_SIZE(kgtCpuCommand, 0x6f, 0);
KGT_ASSERT_SIZE(kgtCharacterCommand, 0x52, 0);
KGT_ASSERT_SIZE(kgtCharacterHitJunction, 0x4, 0);
KGT_ASSERT_SIZE(kgtCommonImage, 0x6, 0);
KGT_ASSERT_SIZE(kgtStoryEntryCpu, 0x1a, 0);
KGT_ASSERT_SIZE(kgtStoryEntry, 0xce, 0);
KGT_ASSERT_SIZE(kgtEngineObject, 0x17e, 51);
KGT_ASSERT_SIZE(kgt_character_struct, 0xe03f, 4 + 14);
KGT_ASSERT_SIZE(kgtSystemHitJunction, 0x24, 0);
KGT_ASSERT_SIZE(kgtSystem, 0x124bc, 4);
KGT_ASSERT_SIZE(kgtGameState, 0x1ac, 0);
KGT_ASSERT_SIZE(kgt_demo_file, 0x2669, 4);
KGT_ASSERT_SIZE(kgt_stage, 0x2691, 4);
KGT_ASSERT_SIZE(online_tcp_struct, 0x140, 0);
KGT_ASSERT_SIZE(kgtWav, 0x18, 3);
KGT_ASSERT_SIZE(wavSoundFile, 0x30, 1);
KGT_ASSERT_SIZE(kgtBMPINFO, 0x14, 1);
KGT_ASSERT_SIZE(UNK_STRUCT_A, 0x9fa, 1);
KGT_ASSERT_SIZE(UNK_STRUCT_B, 0x701f8, 46);
KGT_ASSERT_SIZE(UNK_STRUCT_F, 0x870, 0);
KGT_ASSERT_SIZE(UNK_0x48_struct, 0x48, 0);
KGT_ASSERT_SIZE(kgt_debug_a, 0x870, 0);
KGT_ASSERT_SIZE(kgtTrailFrame, 0x10, 1);
KGT_ASSERT_SIZE(unk_0x650_struct, 0x650, 101);
KGT_ASSERT_SIZE(kgtDrawNode, 0x8, 2);
KGT_ASSERT_SIZE(kgtDrawLayer, 0x8, 2);
KGT_ASSERT_SIZE(POSS_VTABLE_GAME_STATE, 0x48, 18);

/* the file parts that are read straight into the structures, member by member, have no pointers:
   their offsets from the end of kgt_core are the same in both builds */
#define KGT_CORE_GROWTH (4 * KGT_PTR_GROWTH)
_Static_assert(offsetof(kgt_character_struct, iWins) == 0xdeed + KGT_CORE_GROWTH, "character file part");
_Static_assert(offsetof(kgtSystem, szCharacterNames) == 0x2234 + KGT_CORE_GROWTH, "system file part");
_Static_assert(offsetof(kgt_demo_file, cBgmSelection) == 0x2234 + KGT_CORE_GROWTH, "demo file part");
_Static_assert(offsetof(kgt_stage, cBgmSelection) == 0x2234 + KGT_CORE_GROWTH, "stage file part");

#endif