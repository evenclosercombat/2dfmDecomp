/*
 * engine.c - the engine-object system and everything that the per-frame object loop dispatches to.
 *
 * Everything on screen and every game-state controller is a kgtEngineObject in gkgtEngineObjects[1024].
 * Each frame the main loop runs, for every object, the handler gpfnGamestateJumptable[iJumpIdx]
 * (vJumptableJump) and then draws it (vDrawCurrentEngineObject).  This file contains:
 *
 *   - object management: creating (kgtoNewEngineObject and the skill/script spawners), resetting and
 *     clearing objects, and the game-speed scalars that are derived when objects are reset;
 *   - story mode: stepping through the story script of the story character (vProgressStoryMode) and
 *     playing its demos (vjmpInitiateStoryMode);
 *   - the game-flow screens: title/menu (vjmpMenuTraversal), character select (normal, team and the
 *     unused story variant), game over, the battle state machine (vjmpHandleBattleState: round start,
 *     KO, winner, continue), and the start-up/title screens (vjmpStartGame, vjmpDisplayTitleScreens);
 *   - the battle interface: life/power gauges, timer, win marks, combo counter, fades and screen
 *     control (part B: vjmpHandleBattleInterface ... vjmpScreenControl);
 *   - drawing into the 640x480 16-bit frame buffer gpFrameBits (RGB555 in a window, RGB565 full screen,
 *     giScreenMode): external bitmaps (vBlitImageRect16), filled rectangles, digits and text from
 *     text.bmp, KGT script images with the six blend modes (vDrawKgtImage16), hit boxes, screen shake,
 *     colour transitions, and the per-object draw routine vDrawCurrentEngineObject (layers, after-image
 *     trails, flashes).
 *
 * Four functions here (vjmpHandleBattleInterface, vBlitImageRect16, vDrawKgtImage16,
 * vDrawCurrentEngineObject) do not compile to the original bytes yet; they are emitted as byte-exact
 * stand-ins (see docs/MATCHING.md, "Functions that do not match yet").
 */
#include "kgt.h"

/* ------------------------------------------------------------------------------------------ */
/* declarations not (yet) in globals.h / protos.h                                              */
/* ------------------------------------------------------------------------------------------ */

extern kgtEngineObject gkgtEngineObjects[1024];    /* 0x4701e0: all engine objects */
extern kgtEngineObject *gpkgtCurrentEngineObject;  /* 0x4cfa00: object whose handler is running */
extern kgtGameState gkgtGameState;                 /* 0x470020: state of the current game */
extern int giConfigTestplayGamespeed;              /* 0x430104: test play: game speed */
extern int giGamespeedFrames;                      /* 0x445704: game speed setting in effect */
extern int giPlayerMomentumScalar;                 /* 0x541f78: multiplier of script momentum values (game speed) */
extern int giGravityScalar;                        /* 0x445700: multiplier of script gravity values (game speed) */
extern int giDemoTime;                             /* 0x424f08: frames left of the demo */
extern char gcDemoSkipWithInput;                   /* 0x424f04: copy of the demo's cSkipWithInput */
extern int giStoryModeSide;                        /* 0x424f24: copy of giStoryModePlayerIdx taken when the story character is chosen; indexes giCurrentStoryStep */
extern int giCurrentStoryStep[2];                  /* 0x424f28: story entry per side */
/* vProgressStoryMode accesses giStoryModeSide and giCurrentStoryStep as one struct: the store to
   iStep[iPlayer] makes the compiler reload iPlayer, which the original does */
typedef struct { int iPlayer; int iStep[2]; } kgtStoryVars;
#define STORY (*(kgtStoryVars *)&giStoryModeSide)
extern int giStoryFrontStageFlag;           /* 0x424f0c: story mode divergence type 1 (front stage) jumps when it is 1; only ever cleared */
extern int giRoundsNotWon;                  /* 0x424e64: story mode: fights not won (divergence type 3 needs 0) */
extern DWORD giAnyInputXor;                 /* 0x4280d8: giLastInputXor of all players ORed */
extern DWORD giLastInputCleaned[8];         /* 0x447f40: newly pressed or auto-repeated inputs (menus) */
extern DWORD giLastInputXor[8];             /* 0x447f60: inputs newly pressed this frame */
extern int giStoryModePlayerIdx;            /* 0x424f20: player (0/1) who started story mode */
extern int giConfigTestplayVsMode;          /* 0x430120: test play: versus mode */
extern int giConfigNumberOfRounds;          /* 0x430124: rounds of a single game */
extern int giConfigNumberOfRoundsTeamVs;    /* 0x430128: rounds of a team game */
extern int giBattlePrestartTimer;           /* 0x424f00: frames before the fight starts */
extern int giCameraX;                       /* 0x447f2c: camera x (pixels) */
extern int giCameraY;                       /* 0x447f30: camera y (pixels) */
extern DWORD giAnyInput;                    /* 0x4cfa04: giUserKeydowns of all players ORed */
extern int giGameModes[3];                  /* 0x424e40: game modes offered by the title menu (GAME_MODES) */
extern int giAmountOfGameModes;             /* 0x424e60: number of menu entries - 1 */
extern int giMenuSelectionIdx;              /* 0x424780: title menu entry selected */
extern int gbStoryMode;                     /* 0x424714: 1 = story mode chosen in the title menu */
extern int giStoryModeCurrentRound;         /* 0x424f34: round in the current story fight */
extern int giStoryWinsP1;                   /* 0x424f38: story mode: saved rounds won of player 1 (carry over) */
extern int giStoryWinsP2;                   /* 0x424f3c: story mode: saved rounds won of player 2 */
extern int giConfigTestplayStageNb;         /* 0x43010c: test play: stage */
extern int giConfigTestplayTime;            /* 0x430114: round time setting */
extern int giShakeXMode;                    /* 0x447da9: screen shake x (script command EB): mode; vCalculateShake takes the 5 ints */
extern int giShakeXOffset;                  /* 0x447dad: current x offset (pixels) */
extern int giShakeXAmplitude;               /* 0x447db1: amplitude */
extern int giShakeXTimeLeft;                /* 0x447db5: frames left */
extern int giShakeXDuration;                /* 0x447db9: total frames */
extern int giShakeYMode;                    /* 0x447dbd: screen shake y: mode */
extern int giShakeYOffset;                  /* 0x447dc1: current y offset (pixels) */
extern int giShakeYAmplitude;               /* 0x447dc5: amplitude */
extern int giShakeYTimeLeft;                /* 0x447dc9: frames left */
extern int giShakeYDuration;                /* 0x447dcd: total frames */
/* 0x424718: set to end the round at once (Ghidra DAT_00424718); main.c declares it as giForceRoundEnd, engine.c reaches it through gbStoryMode */
#define giForceRoundEnd ((&gbStoryMode)[1])
extern kgtGridCoordinates gkgtSelectCursor1;      /* 0x424e50: character select: 1P cursor */
extern kgtGridCoordinates gkgtSelectCursor2;      /* 0x424e58: character select: 2P cursor */
extern kgtGridCoordinates gkgtStorySelectCursor;  /* 0x424e68: character select cursor in story mode */
/* 0x424e80: team battle portrait objects [side * 4 + member]; this variable has no symbol of its own in
   asm/game_bss.txt, so it is addressed relative to gkgtStorySelectCursor */
#define gpTeamPortraits ((kgtEngineObject **)((char *)&gkgtStorySelectCursor + 0x18))

/* engine.c part B (0x409a60-0x40e4a0) */
void vjmpStartGame(void);
void vjmpHandleBattleInterface(void);
void vjmpUpdateTimerAndUi(void);
void vjmpHitComboCounter(void);
void vjmpFadeDriftEffect(void);
void vjmpDisplayTitleScreens(void);
void vjmpScreenControl(void);
/* script.c */
void vjmpReadScript(void);

/* this file */
void vResetObjectsAndSpeed(void);
kgtEngineObject *kgtoNewEngineObject(int iJumpIdx, int iDepth, int iPosX, int iPosY);
void vEmptyFunction(void);
void vjmpResetIdx(void);
void vProgressStoryMode(void);
void vjmpInitiateStoryMode(void);
void vjmpRoundStart(void);
void vjmpIdleA(void);
void vjmpIdleB(void);
void vjmpCharacterSelectScreen(void);
void vjmpMenuTraversal(void);
void vjmpGameOverScreen(void);
void vjmpHandleBattleState(void);

/* ------------------------------------------------------------------------------------------ */
/* initialized data                                                                            */
/* ------------------------------------------------------------------------------------------ */

/* indexed by kgtEngineObject.iJumpIdx (kgtJumptableEndpoints) */
void (*gpfnGamestateJumptable[18])(void) = {  /* handler per kgtJumptableEndpoints value */
    vEmptyFunction,                 /* EMPTY */
    vjmpResetIdx,                   /* RESET_IDX */
    vjmpStartGame,                  /* START_GAME */
    vjmpScreenControl,              /* SCREEN_CONTROL */
    vjmpReadScript,                 /* READ_SCRIPT */
    vjmpFadeDriftEffect,            /* FADE_DRIFT_EFFECT */
    vjmpUpdateTimerAndUi,           /* UPDATE_TIMER */
    vjmpHitComboCounter,            /* HIT_COMBO_COUNTER */
    vjmpRoundStart,                 /* ROUND_START */
    vjmpIdleA,                      /* IDLE_A */
    vjmpCharacterSelectScreen,      /* CHARACTER_SELECT_SCREEN */
    vjmpIdleB,                      /* IDLE_B */
    vjmpMenuTraversal,              /* MENU_TRAVERSAL */
    vjmpGameOverScreen,             /* GAME_OVER_SCREEN */
    vjmpHandleBattleState,          /* BATTLE_STATE */
    vjmpHandleBattleInterface,      /* BATTLE_UI */
    vjmpInitiateStoryMode,          /* STORY_MODE */
    vjmpDisplayTitleScreens,        /* DISPLAY_TITLE_SCREEN */
};

/* hitbox colours (RGB555) used by vDrawCurrentEngineObject: [0] attack boxes, [1] guard boxes */
unsigned int guHitboxColors[4] = { 0x7c21, 0x04bf, 0x04df, 0x04ff };  /* 0x41eda0: hit box colours (RGB555): [0] attack boxes, [1] guard boxes */

/* ------------------------------------------------------------------------------------------ */
/* engine objects                                                                              */
/* ------------------------------------------------------------------------------------------ */

/*
 * vResetObjectsAndSpeed - end every active object except the one running now, and recompute the
 * game-speed dependent scalars.
 *
 * Objects with a handler (iJumpIdx > RESET_IDX) get RESET_IDX, whose handler frees them next frame.
 * Globals read: gkgtEngineObjects, gpkgtCurrentEngineObject, giConfigTestplayGamespeed.
 * Globals changed: gkgtEngineObjects[].iJumpIdx, giGamespeedFrames, giPlayerMomentumScalar,
 * giGravityScalar.
 */
void vResetObjectsAndSpeed(void)
{
    int i;
    kgtEngineObject *pObj;

    /* send every running object except the caller to RESET_IDX */
    pObj = gkgtEngineObjects;
    for (i = 0; i < 1024; i++, pObj++) {
        if (pObj->iJumpIdx > RESET_IDX && pObj != gpkgtCurrentEngineObject)
            pObj->iJumpIdx = RESET_IDX;
    }
    /* game speed setting -> speed value: settings above 10 are x10, lower ones 50 + 5 per step
       (setting 10 = 100) */
    if (giConfigTestplayGamespeed > 10)
        giGamespeedFrames = giConfigTestplayGamespeed * 10;
    else
        giGamespeedFrames = giConfigTestplayGamespeed * 5 + 50;
    /* 16.16 scalars applied to script speeds (1/speed) and accelerations (60/speed^2) */
    giPlayerMomentumScalar = 0x10000 / giGamespeedFrames;
    giGravityScalar = 0x3c0000 / (giGamespeedFrames * giGamespeedFrames);
}

/*
 * vClearJumpIdxOnAllObjects - end (RESET_IDX) every object whose handler is iJumpIdx.
 * iJumpIdx: a kgtJumptableEndpoints value.  Globals changed: gkgtEngineObjects[].iJumpIdx.
 */
void vClearJumpIdxOnAllObjects(int iJumpIdx)
{
    int i;
    kgtEngineObject *pObj;

    pObj = gkgtEngineObjects;
    for (i = 0; i < 1024; i++, pObj++) {
        if (pObj->iJumpIdx == iJumpIdx)
            pObj->iJumpIdx = RESET_IDX;
    }
}

/*
 * vResetAllObjectsWithSkillIdx - end (RESET_IDX) every script object currently running skill iSkillIdx.
 * iSkillIdx: skill index (of whichever file the object uses).
 * Globals changed: gkgtEngineObjects[].iJumpIdx.
 */
void vResetAllObjectsWithSkillIdx(int iSkillIdx)
{
    int i;
    kgtEngineObject *pObj;

    pObj = gkgtEngineObjects;
    for (i = 0; i < 1024; i++, pObj++) {
        if (pObj->iJumpIdx == READ_SCRIPT && pObj->iSkillIdx == iSkillIdx)
            pObj->iJumpIdx = RESET_IDX;
    }
}

/*
 * vResetObjectsForPlayerIdx - end the script objects of one player slot and forget its number objects.
 * iPlayerIdx: gkgtLoadedCharacter index.  Only character objects (iObjectType below
 * SYSTEM_ENGINE_OBJECT) of that slot are ended.
 * Globals changed: gkgtEngineObjects[].iJumpIdx, gkgtLoadedCharacter[iPlayerIdx].pMNumberObjs[].
 */
void vResetObjectsForPlayerIdx(int iPlayerIdx)
{
    int i;
    kgtEngineObject *pObj;
    kgt_character_struct *pChar;

    /* end the slot's character script objects */
    pObj = gkgtEngineObjects;
    for (i = 0; i < 1024; i++, pObj++) {
        if (pObj->iJumpIdx == READ_SCRIPT && pObj->iObjectType < SYSTEM_ENGINE_OBJECT && pObj->iPlayerIdx == iPlayerIdx)
            pObj->iJumpIdx = RESET_IDX;
    }
    /* the objects created with M numbers 0-9 (O command) are gone now */
    pChar = &gkgtLoadedCharacter[iPlayerIdx];
    for (i = 0; i < 10; i++)
        pChar->pMNumberObjs[i] = NULL;
}

/*
 * kgtoNewEngineObject - create an engine object.
 *
 * Takes the first free (EMPTY) object of gkgtEngineObjects[0..1022], clears it and sets its handler,
 * draw layer and position; its parent is the object whose handler is running.  When all are in use the
 * last object (index 1023) is overwritten and "OBJ OVER" is logged.
 * iJumpIdx: handler (kgtJumptableEndpoints); iDepth: draw layer (0-127); iPosX/iPosY: position
 * (16.16 fixed point for script objects, plain values for some UI/state objects).
 * Returns the object.  Globals read: gpkgtCurrentEngineObject.  Globals changed: gkgtEngineObjects.
 */
kgtEngineObject *kgtoNewEngineObject(int iJumpIdx, int iDepth, int iPosX, int iPosY)
{
    int i;
    kgtEngineObject *pObj;

    /* first free slot */
    for (i = 0, pObj = gkgtEngineObjects; i < 1023; i++, pObj++) {
        if (pObj->iJumpIdx == EMPTY) {
            memset(pObj, 0, sizeof(*pObj));
            pObj->iJumpIdx = iJumpIdx;
            pObj->iDepth = iDepth;
            pObj->iPosX = iPosX;
            pObj->iPosY = iPosY;
            pObj->pParent = gpkgtCurrentEngineObject;
            return pObj;
        }
    }
    /* none free: reuse the last object (pObj = &gkgtEngineObjects[1023]) */
    memset(pObj, 0, sizeof(*pObj));
    pObj->iJumpIdx = iJumpIdx;
    pObj->iDepth = iDepth;
    pObj->iPosX = iPosX;
    pObj->iPosY = iPosY;
    pObj->pParent = gpkgtCurrentEngineObject;
    iSetDebugInfo("OBJ OVER", 0x1fff1f);
    return pObj;
}

/*
 * vSpawnStageScripts - start the scripts of a battle's background: one script object for every named
 * stage skill that contains an image step (command 0x0C), then one for each of the system's ten battle
 * UI layout skills that does, and start the stage BGM.
 * Globals read: gkgtLoadedStage, gkgtKgtSystem.  Globals changed: gkgtEngineObjects (new objects).
 */
void vSpawnStageScripts(void)
{
    int i, iSteps;      /* i: skill index (stage loop) / layout number (system loop) */
    int iDepth;         /* draw layer of the stage objects */
    int bHasImage;
    kgtSkill *pStep;
    kgtEngineObject *pObj;
    int iSkill;

    /* stage skills 1 .. count-2 (the last entry only marks the end of the step list) */
    iDepth = 12;
    for (i = 1; i < gkgtLoadedStage.kgtCore.iActionsCount - 1; i++) {
        if (gkgtLoadedStage.kgtCore.pSkillsAlloc[i].szName[0] != '\0') {
            bHasImage = 0;
            /* from the first "system images" skill (flags 3) on, stage objects go to layer 100 (in
               front); iDepth is never set back */
            if (gkgtLoadedStage.kgtCore.pSkillsAlloc[i].iDefaultScriptGroup == 3)
                iDepth = 100;
            /* the skill's steps run up to the next skill's first step: iSteps is the first step's
               index, then the number of steps */
            iSteps = (unsigned short)gkgtLoadedStage.kgtCore.pSkillsAlloc[i].shStartingStepIdx;
            if (iSteps < (unsigned short)gkgtLoadedStage.kgtCore.pSkillsAlloc[i + 1].shStartingStepIdx) {
                pStep = &gkgtLoadedStage.kgtCore.pSkillScriptsAlloc[iSteps];
                iSteps = (unsigned short)gkgtLoadedStage.kgtCore.pSkillsAlloc[i + 1].shStartingStepIdx - iSteps;
                /* any image step (command 0x0C)? */
                do {
                    if (pStep->cSkillType == 12)
                        bHasImage = 1;
                    pStep++;
                } while (--iSteps);
                if (bHasImage) {
                    pObj = kgtoNewEngineObject(READ_SCRIPT, iDepth, 0, 0);
                    pObj->iObjectType = STAGE_ENGINE_OBJECT;
                    pObj->iSkillIdx = i;
                    pObj->iSkillScriptIdx = (unsigned short)gkgtLoadedStage.kgtCore.pSkillsAlloc[i].shStartingStepIdx;
                }
            }
        }
    }
    /* the system's battle UI layouts 1-10 (shSkillIdxStageLayout1..10 are consecutive shorts), layer 101 */
    for (i = 0; i < 10; i++) {
        iSkill = (unsigned short)(&gkgtKgtSystem.shSkillIdxStageLayout1)[i];
        bHasImage = 0;
        iSteps = (unsigned short)gkgtKgtSystem.kgtCore.pSkillsAlloc[iSkill].shStartingStepIdx;
        if (iSteps < (unsigned short)gkgtKgtSystem.kgtCore.pSkillsAlloc[iSkill + 1].shStartingStepIdx) {
            pStep = &gkgtKgtSystem.kgtCore.pSkillScriptsAlloc[iSteps];
            iSteps = (unsigned short)gkgtKgtSystem.kgtCore.pSkillsAlloc[iSkill + 1].shStartingStepIdx - iSteps;
            do {
                if (pStep->cSkillType == 12)
                    bHasImage = 1;
                pStep++;
            } while (--iSteps);
            if (bHasImage) {
                pObj = kgtoNewEngineObject(READ_SCRIPT, 101, 0, 0);
                pObj->iObjectType = SYSTEM_ENGINE_OBJECT;
                pObj->iSkillIdx = iSkill;
                pObj->iSkillScriptIdx = (unsigned short)gkgtKgtSystem.kgtCore.pSkillsAlloc[iSkill].shStartingStepIdx;
            }
        }
    }
    /* stage BGM */
    vHandleLoadingSound(&gkgtLoadedStage.kgtCore.pkgtSounds[gkgtLoadedStage.wBgmSelection]);
}

/*
 * vSpawnEngineObjectForDemoSkills - start a demo (story picture/ending) file: one script object for every
 * named demo skill that contains an image step, as in vSpawnStageScripts; then start the demo BGM and
 * copy the demo's play time and skip setting.
 * Globals read: gkgtLoadedDemo.  Globals changed: gkgtEngineObjects (new objects), giDemoTime,
 * gcDemoSkipWithInput.
 */
void vSpawnEngineObjectForDemoSkills(void)
{
    int i, iSteps;      /* i: skill index; iSteps: first step index, then number of steps */
    int iDepth;         /* draw layer: 12, 100 from the first "system images" skill (flags 3) on */
    int bHasImage;
    kgtSkill *pStep;
    kgtEngineObject *pObj;

    iDepth = 12;
    for (i = 1; i < gkgtLoadedDemo.kgtCore.iActionsCount - 1; i++) {
        if (gkgtLoadedDemo.kgtCore.pSkillsAlloc[i].szName[0] != '\0') {
            bHasImage = 0;
            if (gkgtLoadedDemo.kgtCore.pSkillsAlloc[i].iDefaultScriptGroup == 3)
                iDepth = 100;
            iSteps = (unsigned short)gkgtLoadedDemo.kgtCore.pSkillsAlloc[i].shStartingStepIdx;
            if (iSteps < (unsigned short)gkgtLoadedDemo.kgtCore.pSkillsAlloc[i + 1].shStartingStepIdx) {
                pStep = &gkgtLoadedDemo.kgtCore.pSkillScriptsAlloc[iSteps];
                iSteps = (unsigned short)gkgtLoadedDemo.kgtCore.pSkillsAlloc[i + 1].shStartingStepIdx - iSteps;
                /* any image step (command 0x0C)? */
                do {
                    if (pStep->cSkillType == 12)
                        bHasImage = 1;
                    pStep++;
                } while (--iSteps);
                if (bHasImage) {
                    pObj = kgtoNewEngineObject(READ_SCRIPT, iDepth, 0, 0);
                    pObj->iObjectType = DEMO_ENGINE_OBJECT;
                    pObj->iSkillIdx = i;
                    pObj->iSkillScriptIdx = (unsigned short)gkgtLoadedDemo.kgtCore.pSkillsAlloc[i].shStartingStepIdx;
                }
            }
        }
    }
    /* demo BGM, play time (frames, 0 = no limit) and skip-with-input flag */
    vHandleLoadingSound(&gkgtLoadedDemo.kgtCore.pkgtSounds[gkgtLoadedDemo.wBgmSelection]);
    giDemoTime = gkgtLoadedDemo.iTime;
    gcDemoSkipWithInput = gkgtLoadedDemo.cSkipWithInput;
}

/*
 * kgtoNewObjectForSkillIdx - create a script object running system skill iSkillIdx from its first step.
 * iDepth, iPosX, iPosY: as for kgtoNewEngineObject.  Returns the object.
 * Globals read: gkgtKgtSystem.
 */
kgtEngineObject *kgtoNewObjectForSkillIdx(int iSkillIdx, int iDepth, int iPosX, int iPosY)
{
    kgtEngineObject *pObj;

    pObj = kgtoNewEngineObject(READ_SCRIPT, iDepth, iPosX, iPosY);
    pObj->iSkillIdx = iSkillIdx;
    pObj->iObjectType = SYSTEM_ENGINE_OBJECT;
    pObj->iSkillScriptIdx = (unsigned short)gkgtKgtSystem.kgtCore.pSkillsAlloc[iSkillIdx].shStartingStepIdx;
    return pObj;
}

/*
 * kgtSpawnNewEngineObjectReturnSkillField0x1 - create a script object running system skill iSkillIdx
 * (like kgtoNewObjectForSkillIdx) and return the parameter 1 of its first step, which the title/demo
 * screens use as the time (frames) to show it; the value is also logged ("demo:NAME wait:N").
 * iDepth, iPosX, iPosY: as for kgtoNewEngineObject.  Globals read: gkgtKgtSystem.
 */
int kgtSpawnNewEngineObjectReturnSkillField0x1(int iSkillIdx, int iDepth, int iPosX, int iPosY)
{
    kgtEngineObject *pObj;
    int iWait;
    char szMsg[256];

    pObj = kgtoNewEngineObject(READ_SCRIPT, iDepth, iPosX, iPosY);
    pObj->iObjectType = SYSTEM_ENGINE_OBJECT;
    pObj->iSkillIdx = iSkillIdx;
    pObj->iSkillScriptIdx = (unsigned short)gkgtKgtSystem.kgtCore.pSkillsAlloc[iSkillIdx].shStartingStepIdx;
    iWait = (unsigned short)gkgtKgtSystem.kgtCore.pSkillScriptsAlloc[pObj->iSkillScriptIdx].shParam1;
    sprintf(szMsg, "demo:%s wait:%d", gkgtKgtSystem.kgtCore.pSkillsAlloc[iSkillIdx].szName, iWait);
    iSetDebugInfo(szMsg, 0x1fff1f);
    return iWait;
}

/*
 * vEmptyEngineObjects - clear all engine objects and create the START_GAME object that starts the game
 * flow again.  Globals changed: gkgtEngineObjects.
 */
void vEmptyEngineObjects(void)
{
    memset(gkgtEngineObjects, 0, sizeof(gkgtEngineObjects));
    kgtoNewEngineObject(START_GAME, 0, 0, 0);
}

/* vEmptyFunction - handler of EMPTY (free) objects: does nothing. */
void vEmptyFunction(void)
{
}

/*
 * vjmpResetIdx - handler of RESET_IDX: frees the running object (EMPTY), one frame after it was ended.
 * Globals changed: gpkgtCurrentEngineObject->iJumpIdx.
 */
void vjmpResetIdx(void)
{
    gpkgtCurrentEngineObject->iJumpIdx = EMPTY;
}

/* ------------------------------------------------------------------------------------------ */
/* story mode                                                                                  */
/* ------------------------------------------------------------------------------------------ */

/*
 * vProgressStoryMode - advance story mode: run the story script of the story character (the file loaded
 * in player slot 0) from the step after the current one until a step starts a fight (BATTLE_STATE
 * object) or a demo (STORY_MODE object).  Jump steps are followed; the end of the script, the "story
 * over" step and any error create the MENU_TRAVERSAL object instead (back to the title menu).
 *
 * Story steps (kgtStoryEntry, cStoryType & 0xf): 0 end of script, 1 fight (CPU characters 1-7 and the
 * stage cStageIdx), 2 demo number cStageIdx, 3 jump by cWhenDefeatAndFirstRoundBitmask - 1 steps if
 * the condition cStageIdx holds (0 always, 1 front stage flag, 2 life below cRoundsAmount, 3 all fights
 * won), 4 story over.
 * Globals read: gkgtKgtSystem.cCharacterModeFlags, giStoryModeSide, giStoryFrontStageFlag,
 * giRoundsNotWon, gkgtLoadedCharacter[0].  Globals changed: giCurrentStoryStep[], gkgtGameState,
 * gkgtLoadedCharacter[].iOnlineState, gkgtEngineObjects.
 */
void vProgressStoryMode(void)
{
    kgt_character_struct *pChar;
    int i;
    int iLoop;          /* non-zero while stepping; also counts the steps run (limit 200) */
    int iStep;
    kgtStoryEntry *pEntry;
    char *pCpuChar;     /* cCharacterIdx of the next kgtStoryEntryCpu (1-based file index, 0 = none) */
    char szMsg[224];

    iLoop = 1;
    /* the character has no story (cCharacterModeFlags bit 0) */
    if (!(gkgtKgtSystem.cCharacterModeFlags[giStoryModeSide] & 1)) {
        iSetDebugInfo("\203X\203g\201[\203\212\201[\202\252\220\335\222\350\202\263\202\352\202\304\202\242\202\334\202\271\202\361", 0xdfffff);  /* story not set */
        goto back_to_menu;
    }
    /* stop everything else, take all slots offline, game state 4000 (story mode) */
    vResetObjectsAndSpeed();
    for (i = 0; i < 8; i++) {
        pChar = &gkgtLoadedCharacter[i];
        if (pChar)
            pChar->iOnlineState = 0;
    }
    gkgtGameState.iGameStateNumber = 4000;
    while (iLoop) {
        /* next step of the story side (STORY: see the kgtStoryVars comment at the top) */
        STORY.iStep[STORY.iPlayer]++;
        iStep = STORY.iStep[STORY.iPlayer];
        if (iStep < 0) {
            iSetDebugInfo("\203X\203g\201[\203\212\201[\203X\203e\203b\203v\220\224\203G\203\211\201[\201@\203}\203C\203i\203X\202\311\202\310\202\301\202\304\202\242\202\334\202\267", 0xdfffff);  /* story step count error: negative */
            goto back_to_menu;
        }
        if (iStep >= 100) {
            iSetDebugInfo("\203X\203g\201[\203\212\201[\203X\203e\203b\203v\220\224\203G\203\211\201[", 0xdfffff);  /* story step count error */
            goto back_to_menu;
        }
        pEntry = &gkgtLoadedCharacter[0].kgtStoryEntries[iStep];
        /* guard against jump loops */
        if (iLoop++ > 200) {
            iSetDebugInfo("\203X\203g\201[\203\212\201[\203X\203N\203\212\203v\203g\202\314\226\263\214\300\203\213\201[\203v\213^\230f\201B\213\255\220\247\217I\227\271", 0xdfffff);  /* story script infinite loop suspected, aborting */
            goto back_to_menu;
        }
        sprintf(szMsg, "\203X\203e\203b\203v %d - %d - %d", iStep, pEntry->cStoryType & 0xf, (BYTE)pEntry->cStageIdx);  /* step %d - %d - %d */
        iSetDebugInfo(szMsg, 0xffafaf);
        switch (pEntry->cStoryType & 0xf) {
        case 0:
            iSetDebugInfo("\203X\203g\201[\203\212\201[\203X\203N\203\212\203v\203g\202\314\226\226\222[", 0xdfffff);  /* end of the story script */
            goto back_to_menu;
        case 1:     /* fight: load the CPU characters into slots 1-7, then start the battle */
            for (i = 0, pCpuChar = &pEntry->kgtStoryEntryCPUs[0].cCharacterIdx; i < 7; i++, pCpuChar += sizeof(kgtStoryEntryCpu)) {
                if (*pCpuChar)
                    iOpenCharacterFile(i + 1, (BYTE)*pCpuChar - 1);
            }
            if (pEntry->cStageIdx) {
                gkgtGameState.bPaused = 0;
                kgtoNewEngineObject(BATTLE_STATE, 0x7f, 0, 0);
                iLoop = 0;
            }
            break;
        case 2:     /* demo: play demo file cStageIdx (vjmpInitiateStoryMode, number in iPosX) */
            if (pEntry->cStageIdx) {
                kgtoNewEngineObject(STORY_MODE, 0x7f, (BYTE)pEntry->cStageIdx, 0);
                iLoop = 0;
            }
            break;
        case 3:     /* jump / divergence: cStageIdx is the condition */
            switch ((BYTE)pEntry->cStageIdx) {
            case 1:     /* front stage */
                if (giStoryFrontStageFlag != 1)
                    break;
                goto divergence_jump;
            case 2:     /* life gauge */
                if (gkgtLoadedCharacter[0].iHealth >= (BYTE)pEntry->cRoundsAmount)
                    break;
                goto divergence_jump;
            case 3:     /* won all the fights */
                if (giRoundsNotWon != 0)
                    break;
            case 0:     /* unconditional */
            divergence_jump:
                /* relative jump; the loop's ++ adds the missing 1 */
                STORY.iStep[STORY.iPlayer] += pEntry->cWhenDefeatAndFirstRoundBitmask - 1;
                break;
            }
            break;
        case 4:
            iSetDebugInfo("\203X\203g\201[\203\212\201[\217I\227\271", 0xdfffff);  /* story over */
            goto back_to_menu;
        default:
            iSetDebugInfo("\203X\203g\201[\203\212\201[\203X\203N\203\212\203v\203g\203G\203\211\201[\201F\216\355\227\336\202\252\225s\220\263\202\305\202\267", 0xdfffff);  /* story script error: bad step type */
            goto back_to_menu;
        }
    }
    return;

back_to_menu:
    kgtoNewEngineObject(MENU_TRAVERSAL, 0x7f, 0, 0);
}

/*
 * vjmpInitiateStoryMode - handler of STORY_MODE objects: plays one story demo (created by
 * vProgressStoryMode with the demo number in iPosX), then continues the story.
 * Steps (iProcessStep): 0 load and start, 1 play until the time runs out or it is skipped, 2 finish.
 * Globals read: giAnyInputXor, gcDemoSkipWithInput.  Globals changed: gkgtGameState.iGameStateNumber,
 * giDemoTime, the running object.
 */
void vjmpInitiateStoryMode(void)
{
    kgtEngineObject *pObj;

    switch (gpkgtCurrentEngineObject->iProcessStep) {
    case 0:
        /* load demo file number iPosX and start its scripts; if it can't be opened go on with the
           next story step at once */
        gpkgtCurrentEngineObject->iProcessStep = 1;
        gkgtGameState.iGameStateNumber = 4000;
        vResetObjectsAndSpeed();
        if (iOpenDemoFile(gpkgtCurrentEngineObject->iPosX) != 0) {
            vResetObjectsAndSpeed();
            gpkgtCurrentEngineObject->iJumpIdx = RESET_IDX;
            vProgressStoryMode();
            return;
        }
        vSpawnEngineObjectForDemoSkills();
        /* fall through */
    case 1:
        /* playing.  iPlayerIdx counts the first 10 frames, in which input is ignored; after that a
           button A-F (input bits 4-9, 0x3f0) skips the demo if it allows skipping, but only once a
           frame without a new press was seen (iObjectType is used as that flag: PLAYER_ENGINE_OBJECT
           (0, from the clear) until then, STORY_ENGINE_OBJECT after) */
        pObj = gpkgtCurrentEngineObject;
        if (pObj->iPlayerIdx < 10) {
            pObj->iPlayerIdx++;
        } else if (gcDemoSkipWithInput & 1) {
            if (giAnyInputXor & 0x3f0) {
                if (pObj->iObjectType != PLAYER_ENGINE_OBJECT) {
                    pObj->iProcessStep++;
                    return;
                }
            } else {
                pObj->iObjectType = STORY_ENGINE_OBJECT;
            }
        }
        /* the demo's play time ran out */
        if (giDemoTime != 0 && --giDemoTime == 0)
            pObj->iProcessStep++;
        break;
    case 2:
        /* demo over: end its objects and continue the story */
        vResetObjectsAndSpeed();
        gpkgtCurrentEngineObject->iJumpIdx = RESET_IDX;
        vProgressStoryMode();
        break;
    }
}

/* ------------------------------------------------------------------------------------------ */
/* small state handlers                                                                        */
/* ------------------------------------------------------------------------------------------ */

/*
 * vjmpRoundStart - handler of ROUND_START objects: shows the "round N" and fighting spirit ("fight")
 * system skills on layer 0x65 and ends them (and itself) once the parent battle-state object reaches
 * step 200 or later.  iObjectType (cast) and pWork015E hold the "round N" and "fight" objects.
 * Globals read: gkgtKgtSystem, gkgtGameState.iCurrentRound.
 */
void vjmpRoundStart(void)
{
    kgtEngineObject *pObj;

    switch (gpkgtCurrentEngineObject->iProcessStep) {
    case 0:
        /* shSkillIdxRound1.. are consecutive shorts, one per round */
        gpkgtCurrentEngineObject->iProcessStep = 1;
        gpkgtCurrentEngineObject->iObjectType = (kgtEngineObjectTypes)kgtoNewObjectForSkillIdx(
            (unsigned short)(&gkgtKgtSystem.shSkillIdxRound1)[gkgtGameState.iCurrentRound], 0x65, 0, 0);
        gpkgtCurrentEngineObject->pWork015E = kgtoNewObjectForSkillIdx((unsigned short)gkgtKgtSystem.shSkillIdxSpirits, 0x65, 0, 0);
        /* fall through */
    case 1:
        /* wait for the battle state to leave the round intro */
        pObj = gpkgtCurrentEngineObject;
        if (pObj->pParent->iProcessStep >= 200) {
            ((kgtEngineObject *)pObj->iObjectType)->iJumpIdx = RESET_IDX;
            pObj->pWork015E->iJumpIdx = RESET_IDX;
            pObj->iJumpIdx = RESET_IDX;
        }
        break;
    }
}

/* vjmpIdleA - handler of IDLE_A objects: only moves from step 0 to 1 (a marker object that other
   handlers watch or end). */
void vjmpIdleA(void)
{
    if (gpkgtCurrentEngineObject->iProcessStep == 0)
        gpkgtCurrentEngineObject->iProcessStep = 1;
}

/*
 * vSelectPrevRegisteredCharacter - move *piChar (character file index 0-49) to the previous registered
 * character (one with a name in gkgtKgtSystem.szCharacterNames), wrapping around.  Logs an error when
 * none is registered.  Globals read: gkgtKgtSystem.szCharacterNames.
 */
void vSelectPrevRegisteredCharacter(int *piChar)
{
    int iTries = 49;

    /* at most 50 tries: all 50 entries */
    do {
        if (--*piChar < 0)
            *piChar = 49;
        if (gkgtKgtSystem.szCharacterNames[*piChar][0] != '\0')
            return;
    } while (iTries--);
    iSetDebugInfo("Error:\203v\203\214\203C\203\204\201[\202\252\223o\230^\202\263\202\352\202\304\202\242\202\334\202\271\202\361", 0xdfffff);  /* Error: no player registered */
}

/*
 * vSelectNextRegisteredCharacter - move *piChar (character file index 0-49) to the next registered
 * character (one with a name in gkgtKgtSystem.szCharacterNames), wrapping around.  Logs an error when
 * none is registered.  Globals read: gkgtKgtSystem.szCharacterNames.
 */
void vSelectNextRegisteredCharacter(int *piChar)
{
    int iTries = 49;

    /* at most 50 tries: all 50 entries */
    do {
        if (++*piChar == 50)
            *piChar = 0;
        if (gkgtKgtSystem.szCharacterNames[*piChar][0] != '\0')
            return;
    } while (iTries--);
    iSetDebugInfo("Error:\203v\203\214\203C\203\204\201[\202\252\223o\230^\202\263\202\352\202\304\202\242\202\334\202\271\202\361", 0xdfffff);  /* Error: no player registered */
}

/* vjmpIdleB - handler of IDLE_B objects: same as vjmpIdleA. */
void vjmpIdleB(void)
{
    if (gpkgtCurrentEngineObject->iProcessStep == 0)
        gpkgtCurrentEngineObject->iProcessStep = 1;
}

/* ------------------------------------------------------------------------------------------ */
/* character select                                                                            */
/* ------------------------------------------------------------------------------------------ */

/*
 * vCharacterSelectChangeSelection - move a character select cursor by (iDCol, iDRow) cells, wrapping
 * around the select grid (gkgtKgtSystem.shColumnsInSelectScreen x shRowsInSelectScreen).
 * pGrid: the cursor.  All callers also pass &gpkgtCurrentEngineObject->iPlayerIdx and 0 (piUnused,
 * iUnused), which are not used.  Globals read: gkgtKgtSystem.
 */
void vCharacterSelectChangeSelection(kgtGridCoordinates *pGrid, int iDCol, int iDRow, int *piUnused, int iUnused)
{
    pGrid->iCol += iDCol;
    pGrid->iRow += iDRow;
    if (pGrid->iCol < 0)
        pGrid->iCol = gkgtKgtSystem.shColumnsInSelectScreen - 1;
    if (pGrid->iRow < 0)
        pGrid->iRow = gkgtKgtSystem.shRowsInSelectScreen - 1;
    if (pGrid->iCol >= gkgtKgtSystem.shColumnsInSelectScreen)
        pGrid->iCol = 0;
    if (pGrid->iRow >= gkgtKgtSystem.shRowsInSelectScreen)
        pGrid->iRow = 0;
}

/*
 * iGetPressedActionButton - which action button is in dwInput (an input bit mask): buttons A-F (input
 * bits 4-9) -> 0-5; the highest one pressed wins, none gives 0.
 */
int iGetPressedActionButton(DWORD dwInput)
{
    int iButton = 0;

    if (dwInput & 0x10)
        iButton = 0;
    if (dwInput & 0x20)
        iButton = 1;
    if (dwInput & 0x40)
        iButton = 2;
    if (dwInput & 0x80)
        iButton = 3;
    if (dwInput & 0x100)
        iButton = 4;
    if (dwInput & 0x200)
        iButton = 5;
    return iButton;
}

/*
 * vPickPlayerColor - set the colour (palette 0-7) of player slot iPlayer from the button that chose the
 * character: button A-F (dwInput bits 4-9) = colour 0-5; while another slot has that colour, the next
 * one (mod 8) is tried.  If all 8 are taken the colour is left unchanged.
 * Globals changed: gkgtLoadedCharacter[iPlayer].iColor.
 */
void vPickPlayerColor(int iPlayer, DWORD dwInput)
{
    int iColor = 0;
    int iTry, iOther;
    int bTaken;

    /* same mapping as iGetPressedActionButton (inlined) */
    if (dwInput & 0x10)
        iColor = 0;
    if (dwInput & 0x20)
        iColor = 1;
    if (dwInput & 0x40)
        iColor = 2;
    if (dwInput & 0x80)
        iColor = 3;
    if (dwInput & 0x100)
        iColor = 4;
    if (dwInput & 0x200)
        iColor = 5;
    /* first colour from there on that no other slot uses */
    for (iTry = 0; iTry < 8; iTry++) {
        bTaken = 0;
        for (iOther = 0; iOther < 8; iOther++) {
            if (iPlayer != iOther && iColor == gkgtLoadedCharacter[iOther].iColor)
                bTaken = 1;
        }
        if (!bTaken) {
            gkgtLoadedCharacter[iPlayer].iColor = iColor;
            return;
        }
        iColor = (iColor + 1) % 8;
    }
}

/*
 * vjmpCharacterSelectScreen - handler of CHARACTER_SELECT_SCREEN objects: the character select screen
 * of story mode (one player), versus single and versus team.
 *
 * Steps (iProcessStep): 0 set up (reset the selection state, start the mode's select demo from the
 * system file, create the cursor objects), 1 run the selection, 4 back to the title menu (start button
 * or an error).  When both sides have chosen, the battle (BATTLE_STATE) starts after 100 more frames
 * (giBattlePrestartTimer); in story mode vProgressStoryMode takes over instead.
 * The handler's work slots: iWork0166 / iWork016A the cursor objects of 1P / 2P (int-cast pointers),
 * pWork015E / pWork0162 the portrait of the character under the 1P / 2P cursor.
 * Input bits (giLastInputCleaned / giLastInputXor): 1 left, 2 right, 4 up, 8 down, 0x10-0x200 buttons
 * A-F (0x3f0 = any of them), 0x400 start.
 * Globals read: gkgtKgtSystem (grid layout, cursor/portrait positions, skills), giAnyInputXor,
 * giLastInputCleaned, giLastInputXor, giStoryModePlayerIdx, giConfigTestplayVsMode,
 * giConfigNumberOfRounds, giConfigNumberOfRoundsTeamVs.
 * Globals changed: gkgtGameState (selection, team roster/colours, game state 2000), gkgtLoadedCharacter
 * (files loaded, iWins, iColor), gkgtSelectCursor1/2, gkgtStorySelectCursor, gpTeamPortraits,
 * giBattlePrestartTimer, giCameraX/Y, giStoryModeSide.
 */
void vjmpCharacterSelectScreen(void)
{
    int i;
    int iHovered;       /* character file index under the cursor (row * columns + column) */
    int iSkill;
    kgtEngineObject *pObj;
    kgtEngineObject *pCursor;
    /* VS */
    int iPlayer;
    kgtEngineObject *pVsCursor;
    kgtEngineObject **ppVsPortrait;
    kgtGridCoordinates *pVsGrid;
    /* team */
    int iSide;
    int iSideBase;      /* iSide * 4: first character slot / team entry of the side */
    int iMemberSlot;    /* character slot of the member being chosen (iSideBase + member) */
    int iPreviewSlot;   /* character slot of the portrait under the cursor (iSideBase + 3) */
    int iMembersLeft;   /* members still to choose after this one; places the member's portrait */
    kgtEngineObject *pTeamCursor;
    kgtEngineObject **ppTeamPortrait;
    kgtGridCoordinates *pTeamGrid;

    switch (gpkgtCurrentEngineObject->iProcessStep) {
    case 0:
        /* set up: nobody chosen yet, round 0, settings copied from the configuration */
        gpkgtCurrentEngineObject->iProcessStep = 1;
        vResetObjectsAndSpeed();
        gkgtGameState.iConfigTestPlayVsMode = giConfigTestplayVsMode;
        gkgtGameState.iCharSelect[0] = -1;
        gkgtGameState.iCharSelect[1] = -1;
        gkgtGameState.aiTeamMember[0] = 0;
        gkgtGameState.aiTeamMember[1] = 0;
        gkgtGameState.abPlayerChose[0] = 0;
        gkgtGameState.abPlayerChose[1] = 0;
        gkgtGameState.iCurrentRound = 0;
        gkgtGameState.iConfigNumberOfRoundsTeamVs = giConfigNumberOfRoundsTeamVs;
        gkgtGameState.iConfigNumberOfRounds = giConfigNumberOfRounds;
        gkgtLoadedCharacter[0].iWins = 0;
        gkgtLoadedCharacter[1].iWins = 0;
        giBattlePrestartTimer = 0;
        /* the camera looks at y 480..959: objects here are placed at their screen y + 480 */
        giCameraX = 0;
        giCameraY = 480;
        for (i = 0; i < 8; i++) {
            gkgtGameState.iCharSelect[i] = -1;
            gkgtLoadedCharacter[i].iColor = -1;
        }
        /* the select screen's background demo of the mode (demo number from the system file) */
        switch (gkgtGameState.kgtGameMode) {
        case GAME_MODE_STORY:
            if (iOpenDemoFile((BYTE)gkgtKgtSystem.cStoryModeDemoIdx) != 0) {
                iSetDebugInfo("\203L\203\203\203\211\203N\203^\201[\203Z\203\214\203N\203g\202P\202o\203f\203\202\203X\203N\203\212\203v\203g\202\252\214\251\202\302\202\251\202\350\202\334\202\271\202\361", 0x4040ff);  /* character select 1P demo script not found */
                gpkgtCurrentEngineObject->iProcessStep = 4;
                return;
            }
            break;
        case GAME_MODE_VS_SINGLE:
            if (iOpenDemoFile((BYTE)gkgtKgtSystem.cVsSingleDemoIdx) != 0) {
                iSetDebugInfo("\203L\203\203\203\211\203N\203^\201[\203Z\203\214\203N\203g\202u\202r\203V\203\223\203O\203\213\203f\203\202\203X\203N\203\212\203v\203g\202\252\214\251\202\302\202\251\202\350\202\334\202\271\202\361", 0x4040ff);  /* character select VS single demo script not found */
                gpkgtCurrentEngineObject->iProcessStep = 4;
                return;
            }
            break;
        case GAME_MODE_VS_TEAM:
            if (iOpenDemoFile((BYTE)gkgtKgtSystem.cVsTeamDemoIdx) != 0) {
                iSetDebugInfo("\203L\203\203\203\211\203N\203^\201[\203Z\203\214\203N\203g\202u\202r\203`\201[\203\200\203f\203\202\203X\203N\203\212\203v\203g\202\252\214\251\202\302\202\251\202\350\202\334\202\271\202\361", 0x4040ff);  /* character select VS team demo script not found */
                gpkgtCurrentEngineObject->iProcessStep = 4;
                return;
            }
            break;
        }
        /* start it and create the cursors (1P only in story mode) */
        vSpawnEngineObjectForDemoSkills();
        switch (gkgtGameState.kgtGameMode) {
        case GAME_MODE_STORY:
            gpkgtCurrentEngineObject->iWork0166 = (int)kgtoNewObjectForSkillIdx((unsigned short)gkgtKgtSystem.shSkillIdx1pVsScreenCursor, 0x65, 0, 0);
            break;
        case GAME_MODE_VS_SINGLE:
        case GAME_MODE_VS_TEAM:
            gpkgtCurrentEngineObject->iWork0166 = (int)kgtoNewObjectForSkillIdx((unsigned short)gkgtKgtSystem.shSkillIdx1pVsScreenCursor, 0x65, 0, 0);
            gpkgtCurrentEngineObject->iWork016A = (int)kgtoNewObjectForSkillIdx((unsigned short)gkgtKgtSystem.shSkillIdx2pVsScreenCursor, 0x65, 0, 0);
            for (i = 0; i < 4; i++) {
                gpTeamPortraits[i] = NULL;
                gpTeamPortraits[i + 4] = NULL;
            }
            break;
        }
        /* game state 2000 = character select; a grid without cells is an error */
        gkgtGameState.iGameStateNumber = 2000;
        if (gkgtKgtSystem.shColumnsInSelectScreen * gkgtKgtSystem.shRowsInSelectScreen == 0) {
            iSetDebugInfo("\203L\203\203\203\211\203Z\203\214\203N\203g\220\335\222\350\203G\203\211\201[\201i\217c\220\224\202\251\211\241\220\224\202\2520\202\305\202\267\201j", 0x4040ff);  /* character select setting error (rows or columns is 0) */
            goto error_exit;
        }
        /* fall through */
    case 1:
        if (giAnyInputXor & 0x400) {         /* start: back to the menu */
            gpkgtCurrentEngineObject->iProcessStep = 4;
            return;
        }
        /* forget portraits that were ended (vResetObjectsForPlayerIdx) */
        if (gpkgtCurrentEngineObject->pWork015E != NULL && gpkgtCurrentEngineObject->pWork015E->iJumpIdx == RESET_IDX)
            gpkgtCurrentEngineObject->pWork015E = NULL;
        if (gpkgtCurrentEngineObject->pWork0162 != NULL && gpkgtCurrentEngineObject->pWork0162->iJumpIdx == RESET_IDX)
            gpkgtCurrentEngineObject->pWork0162 = NULL;
        switch (gkgtGameState.kgtGameMode) {
        case GAME_MODE_STORY:
            /* story: only the player who started story mode selects, with gkgtStorySelectCursor; the
               cursor object follows the cursor cell (16.16 position) */
            pCursor = (kgtEngineObject *)gpkgtCurrentEngineObject->iWork0166;
            pCursor->iPosX = (gkgtKgtSystem.shDistanceBetweenCharactersX * gkgtStorySelectCursor.iCol + gkgtKgtSystem.shCharacterSelectStartX) << 16;
            pCursor->iPosY = (gkgtKgtSystem.shDistanceBetweenCharactersY * gkgtStorySelectCursor.iRow + gkgtKgtSystem.shCharacterSelectStartY) << 16;
            /* chosen: after 100 frames go on with the story */
            if (gkgtGameState.abPlayerChose[0]) {
                if (giBattlePrestartTimer++ > 100) {
                    vResetObjectsAndSpeed();
                    gpkgtCurrentEngineObject->iJumpIdx = RESET_IDX;
                    vProgressStoryMode();
                    return;
                }
                break;
            }
            /* move the cursor (up, down, left, right) */
            if (giLastInputCleaned[giStoryModePlayerIdx] & 4)
                vCharacterSelectChangeSelection(&gkgtStorySelectCursor, 0, -1, &gpkgtCurrentEngineObject->iPlayerIdx, 0);
            if (giLastInputCleaned[giStoryModePlayerIdx] & 8)
                vCharacterSelectChangeSelection(&gkgtStorySelectCursor, 0, 1, &gpkgtCurrentEngineObject->iPlayerIdx, 0);
            if (giLastInputCleaned[giStoryModePlayerIdx] & 1)
                vCharacterSelectChangeSelection(&gkgtStorySelectCursor, -1, 0, &gpkgtCurrentEngineObject->iPlayerIdx, 0);
            if (giLastInputCleaned[giStoryModePlayerIdx] & 2)
                vCharacterSelectChangeSelection(&gkgtStorySelectCursor, 1, 0, &gpkgtCurrentEngineObject->iPlayerIdx, 0);
            /* the cursor moved to another cell: first frame drop the old character, next frame load
               the new one (if it is registered and has a story) into slot 0 and show its select
               picture as the portrait (layer 0x50, at the 1P portrait position) */
            iHovered = gkgtKgtSystem.shColumnsInSelectScreen * gkgtStorySelectCursor.iRow + gkgtStorySelectCursor.iCol;
            if (gkgtGameState.iCharSelect[0] != iHovered) {
                if (gkgtGameState.iCharSelect[0] != -1) {
                    vResetObjectsForPlayerIdx(0);
                    gkgtGameState.iCharSelect[0] = -1;
                    return;
                }
                if (iHovered >= 50 || gkgtKgtSystem.szCharacterNames[iHovered][0] == '\0'
                    || !(gkgtKgtSystem.cCharacterModeFlags[iHovered] & 1))
                    return;
                gkgtGameState.iCharSelect[0] = iHovered;
                iOpenCharacterFile(0, iHovered);
                pObj = kgtoNewEngineObject(READ_SCRIPT, 0x50, gkgtKgtSystem.shPlayerOneCursorX << 16, (gkgtKgtSystem.shPlayerOneCursorY + 480) << 16);
                gpkgtCurrentEngineObject->pWork015E = pObj;
                gpkgtCurrentEngineObject->pWork015E->iObjectType = STORY_ENGINE_OBJECT;
                gpkgtCurrentEngineObject->pWork015E->iPlayerIdx = 0;
                gpkgtCurrentEngineObject->pWork015E->iSkillIdx = (unsigned short)gkgtLoadedCharacter[0].shSkillIdxCharSelectPic;
                gpkgtCurrentEngineObject->pWork015E->iSkillScriptIdx = (unsigned short)gkgtLoadedCharacter[0].kgtCore.pSkillsAlloc[gpkgtCurrentEngineObject->pWork015E->iSkillIdx].shStartingStepIdx;
                pObj->iFlags |= 0x40000000;     /* camera-relative */
            }
            /* a button chooses the character: the cursor changes to its "after input" skill, the button
               gives the colour */
            if (gkgtGameState.iCharSelect[0] >= 0 && (giLastInputXor[giStoryModePlayerIdx] & 0x3f0)) {
                gkgtGameState.abPlayerChose[0] = 1;
                ((kgtEngineObject *)gpkgtCurrentEngineObject->iWork0166)->iJumpIdx = RESET_IDX;
                gpkgtCurrentEngineObject->iWork0166 = (int)kgtoNewObjectForSkillIdx((unsigned short)gkgtKgtSystem.shSkillIdx1pVsCursorAfterInput, 0x65, 0, 0);
                gkgtGameState.iTeamColor0 = iGetPressedActionButton(giLastInputXor[giStoryModePlayerIdx]);
                giStoryModeSide = giStoryModePlayerIdx;
                vPickPlayerColor(0, giLastInputXor[giStoryModePlayerIdx]);
            }
            break;

        case GAME_MODE_VS_SINGLE:
            /* versus: both players select at the same time, each into its own slot (0 / 1) */
            for (iPlayer = 0; iPlayer < 2; iPlayer++) {
                switch (iPlayer) {
                case 0:
                    ppVsPortrait = &gpkgtCurrentEngineObject->pWork015E;
                    pVsGrid = &gkgtSelectCursor1;
                    pVsCursor = (kgtEngineObject *)gpkgtCurrentEngineObject->iWork0166;
                    break;
                case 1:
                    ppVsPortrait = &gpkgtCurrentEngineObject->pWork0162;
                    pVsGrid = &gkgtSelectCursor2;
                    pVsCursor = (kgtEngineObject *)gpkgtCurrentEngineObject->iWork016A;
                    break;
                }
                pVsCursor->iPosX = (gkgtKgtSystem.shDistanceBetweenCharactersX * pVsGrid->iCol + gkgtKgtSystem.shCharacterSelectStartX) << 16;
                pVsCursor->iPosY = (gkgtKgtSystem.shDistanceBetweenCharactersY * pVsGrid->iRow + gkgtKgtSystem.shCharacterSelectStartY) << 16;
                if (gkgtGameState.abPlayerChose[iPlayer])
                    continue;
                /* a button chooses: "after input" cursor, colour from the button */
                if (gkgtGameState.iCharSelect[iPlayer] >= 0 && (giLastInputXor[iPlayer] & 0x3f0)) {
                    pVsCursor->iJumpIdx = RESET_IDX;
                    if (iPlayer == 0)
                        gpkgtCurrentEngineObject->iWork0166 = (int)kgtoNewObjectForSkillIdx((unsigned short)gkgtKgtSystem.shSkillIdx1pVsCursorAfterInput, 0x65, 0, 0);
                    else
                        gpkgtCurrentEngineObject->iWork016A = (int)kgtoNewObjectForSkillIdx((unsigned short)gkgtKgtSystem.shSkillIdx2pVsCursorAfterInput, 0x65, 0, 0);
                    gkgtGameState.abPlayerChose[iPlayer] = 1;
                    gkgtGameState.aiTeamColor[iPlayer * 4] = iGetPressedActionButton(giLastInputXor[iPlayer]);
                    vPickPlayerColor(iPlayer, giLastInputXor[iPlayer]);
                } else {
                    /* move the cursor; on a new cell drop the old character, then (next frame) load
                       the new one if it is registered and allowed in versus mode (flag 2) and show its
                       portrait (2P's mirrored) */
                    if (giLastInputCleaned[iPlayer] & 4)
                        vCharacterSelectChangeSelection(pVsGrid, 0, -1, &gpkgtCurrentEngineObject->iPlayerIdx, 0);
                    if (giLastInputCleaned[iPlayer] & 8)
                        vCharacterSelectChangeSelection(pVsGrid, 0, 1, &gpkgtCurrentEngineObject->iPlayerIdx, 0);
                    if (giLastInputCleaned[iPlayer] & 1)
                        vCharacterSelectChangeSelection(pVsGrid, -1, 0, &gpkgtCurrentEngineObject->iPlayerIdx, 0);
                    if (giLastInputCleaned[iPlayer] & 2)
                        vCharacterSelectChangeSelection(pVsGrid, 1, 0, &gpkgtCurrentEngineObject->iPlayerIdx, 0);
                    iHovered = gkgtKgtSystem.shColumnsInSelectScreen * pVsGrid->iRow + pVsGrid->iCol;
                    if (gkgtGameState.iCharSelect[iPlayer] != iHovered) {
                        if (gkgtGameState.iCharSelect[iPlayer] != -1) {
                            vResetObjectsForPlayerIdx(iPlayer);
                            gkgtGameState.iCharSelect[iPlayer] = -1;
                        } else if (iHovered < 50 && gkgtKgtSystem.szCharacterNames[iHovered][0] != '\0'
                                   && (gkgtKgtSystem.cCharacterModeFlags[iHovered] & 2)) {
                            gkgtGameState.iCharSelect[iPlayer] = iHovered;
                            iOpenCharacterFile(iPlayer, iHovered);
                            if (iPlayer == 0) {
                                pObj = kgtoNewEngineObject(READ_SCRIPT, 0x50, gkgtKgtSystem.shPlayerOneCursorX << 16, (gkgtKgtSystem.shPlayerOneCursorY + 480) << 16);
                            } else {
                                pObj = kgtoNewEngineObject(READ_SCRIPT, 0x50, gkgtKgtSystem.shPlayerTwoCursorX << 16, (gkgtKgtSystem.shPlayerTwoCursorY + 480) << 16);
                                pObj->iPlayerLookingRight = 1;
                            }
                            *ppVsPortrait = pObj;
                            iSkill = (unsigned short)gkgtLoadedCharacter[iPlayer].shSkillIdxCharSelectPic;
                            pObj->iSkillIdx = iSkill;
                            pObj->iObjectType = STORY_ENGINE_OBJECT;
                            pObj->iPlayerIdx = iPlayer;
                            pObj->iSkillScriptIdx = (unsigned short)gkgtLoadedCharacter[iPlayer].kgtCore.pSkillsAlloc[iSkill].shStartingStepIdx;
                            pObj->iFlags |= 0x40000000;     /* camera-relative */
                        }
                    }
                }
            }
            /* both chose: start the battle 100 frames later */
            if (gkgtGameState.abPlayerChose[0] && gkgtGameState.abPlayerChose[1] && giBattlePrestartTimer++ > 100) {
                vResetObjectsAndSpeed();
                gpkgtCurrentEngineObject->iJumpIdx = RESET_IDX;
                gkgtGameState.bPaused = 0;
                kgtoNewEngineObject(BATTLE_STATE, 0x7f, 0, 0);
                return;
            }
            break;

        case GAME_MODE_VS_TEAM:
            /* each side picks iConfigNumberOfRoundsTeamVs members, one after the other; member n of a
               side is loaded into character slot side * 4 + n, the character under the cursor into
               slot side * 4 + 3 (its portrait) */
            for (iSide = 0; iSide < 2; iSide++) {
                iSideBase = iSide * 4;
                iMemberSlot = gkgtGameState.aiTeamMember[iSide] + iSideBase;
                switch (iSide) {
                case 0:
                    ppTeamPortrait = &gpkgtCurrentEngineObject->pWork015E;
                    pTeamGrid = &gkgtSelectCursor1;
                    pTeamCursor = (kgtEngineObject *)gpkgtCurrentEngineObject->iWork0166;
                    break;
                case 1:
                    ppTeamPortrait = &gpkgtCurrentEngineObject->pWork0162;
                    pTeamGrid = &gkgtSelectCursor2;
                    pTeamCursor = (kgtEngineObject *)gpkgtCurrentEngineObject->iWork016A;
                    break;
                }
                pTeamCursor->iPosX = (gkgtKgtSystem.shDistanceBetweenCharactersX * pTeamGrid->iCol + gkgtKgtSystem.shCharacterSelectStartX) << 16;
                pTeamCursor->iPosY = (gkgtKgtSystem.shDistanceBetweenCharactersY * pTeamGrid->iRow + gkgtKgtSystem.shCharacterSelectStartY) << 16;
                if (gkgtGameState.abPlayerChose[iSide])
                    continue;
                /* move the cursor; on a new cell drop the old preview, then (next frame) load the new
                   character into the preview slot and show its portrait */
                if (giLastInputCleaned[iSide] & 4)
                    vCharacterSelectChangeSelection(pTeamGrid, 0, -1, &gpkgtCurrentEngineObject->iPlayerIdx, 0);
                if (giLastInputCleaned[iSide] & 8)
                    vCharacterSelectChangeSelection(pTeamGrid, 0, 1, &gpkgtCurrentEngineObject->iPlayerIdx, 0);
                if (giLastInputCleaned[iSide] & 1)
                    vCharacterSelectChangeSelection(pTeamGrid, -1, 0, &gpkgtCurrentEngineObject->iPlayerIdx, 0);
                if (giLastInputCleaned[iSide] & 2)
                    vCharacterSelectChangeSelection(pTeamGrid, 1, 0, &gpkgtCurrentEngineObject->iPlayerIdx, 0);
                iHovered = gkgtKgtSystem.shColumnsInSelectScreen * pTeamGrid->iRow + pTeamGrid->iCol;
                if (gkgtGameState.iCharSelect[iSide] != iHovered) {
                    iPreviewSlot = iSideBase + 3;
                    if (gkgtGameState.iCharSelect[iSide] != -1) {
                        vResetObjectsForPlayerIdx(iPreviewSlot);
                        gkgtGameState.iCharSelect[iSide] = -1;
                        continue;
                    }
                    if (iHovered >= 50 || gkgtKgtSystem.szCharacterNames[iHovered][0] == '\0'
                        || !(gkgtKgtSystem.cCharacterModeFlags[iHovered] & 2))
                        continue;
                    gkgtGameState.iCharSelect[iSide] = iHovered;
                    iOpenCharacterFile(iPreviewSlot, iHovered);
                    if (iSide == 0) {
                        pObj = kgtoNewEngineObject(READ_SCRIPT, 0x50, gkgtKgtSystem.shPlayerOneCursorX << 16, (gkgtKgtSystem.shPlayerOneCursorY + 480) << 16);
                    } else {
                        pObj = kgtoNewEngineObject(READ_SCRIPT, 0x50, gkgtKgtSystem.shPlayerTwoCursorX << 16, (gkgtKgtSystem.shPlayerTwoCursorY + 480) << 16);
                        pObj->iPlayerLookingRight = 1;
                    }
                    pObj->iPlayerIdx = iPreviewSlot;
                    pObj->iObjectType = STORY_ENGINE_OBJECT;
                    *ppTeamPortrait = pObj;
                    iSkill = (unsigned short)gkgtLoadedCharacter[iPreviewSlot].shSkillIdxCharSelectPic;
                    pObj->iSkillIdx = iSkill;
                    pObj->iSkillScriptIdx = (unsigned short)gkgtLoadedCharacter[iPreviewSlot].kgtCore.pSkillsAlloc[iSkill].shStartingStepIdx;
                    pObj->iFlags |= 0x40000000;     /* camera-relative */
                }
                /* the member being chosen is whoever is under the cursor; a button fixes it */
                gkgtGameState.aiTeamRoster[gkgtGameState.aiTeamMember[iSide] + iSideBase] = iHovered;
                if (gkgtGameState.iCharSelect[iSide] >= 0 && (giLastInputXor[iSide] & 0x3f0)) {
                    vPickPlayerColor(iMemberSlot, giLastInputXor[iSide]);
                    gkgtGameState.aiTeamColor[gkgtGameState.aiTeamMember[iSide] + iSideBase] = gkgtLoadedCharacter[iMemberSlot].iColor;
                    if (gkgtGameState.aiTeamMember[iSide] + 1 >= gkgtGameState.iConfigNumberOfRoundsTeamVs) {
                        /* the whole team is chosen */
                        pTeamCursor->iJumpIdx = RESET_IDX;
                        if (iSide == 0)
                            gpkgtCurrentEngineObject->iWork0166 = (int)kgtoNewObjectForSkillIdx((unsigned short)gkgtKgtSystem.shSkillIdx1pVsCursorAfterInput, 0x65, 0, 0);
                        else
                            gpkgtCurrentEngineObject->iWork016A = (int)kgtoNewObjectForSkillIdx((unsigned short)gkgtKgtSystem.shSkillIdx2pVsCursorAfterInput, 0x65, 0, 0);
                        gkgtGameState.abPlayerChose[iSide] = 1;
                    } else {
                        /* the member is chosen: load it into its own slot, show its portrait in the
                           side's row (one layer and one selection width/height further per member
                           still to come) and go on with the next one */
                        iMembersLeft = gkgtGameState.iConfigNumberOfRoundsTeamVs - gkgtGameState.aiTeamMember[iSide] - 1;
                        iOpenCharacterFile(iMemberSlot, iHovered);
                        if (iSide == 0) {
                            pObj = kgtoNewEngineObject(READ_SCRIPT, iMembersLeft + 0x50,
                                (gkgtKgtSystem.shPlayerOneSelectionWidth * iMembersLeft + gkgtKgtSystem.shPlayerOneCursorX) << 16,
                                (gkgtKgtSystem.shPlayerOneSelectionHeight * iMembersLeft + gkgtKgtSystem.shPlayerOneCursorY + 480) << 16);
                        } else {
                            pObj = kgtoNewEngineObject(READ_SCRIPT, iMembersLeft + 0x50,
                                (gkgtKgtSystem.shPlayerTwoSelectionWidth * iMembersLeft + gkgtKgtSystem.shPlayerTwoCursorX) << 16,
                                (gkgtKgtSystem.shPlayerTwoSelectionHeight * iMembersLeft + gkgtKgtSystem.shPlayerTwoCursorY + 480) << 16);
                            pObj->iPlayerLookingRight = 1;
                        }
                        pObj->iObjectType = STORY_ENGINE_OBJECT;
                        gpTeamPortraits[gkgtGameState.aiTeamMember[iSide] + iSideBase] = pObj;
                        pObj->iPlayerIdx = iMemberSlot;
                        iSkill = (unsigned short)gkgtLoadedCharacter[iMemberSlot].shSkillIdxCharSelectPic;
                        pObj->iSkillIdx = iSkill;
                        pObj->iSkillScriptIdx = (unsigned short)gkgtLoadedCharacter[iMemberSlot].kgtCore.pSkillsAlloc[iSkill].shStartingStepIdx;
                        pObj->iFlags |= 0x40000000;     /* camera-relative */
                        gkgtGameState.aiTeamMember[iSide]++;
                    }
                }
            }
            /* both teams chosen: start the battle 100 frames later */
            if (gkgtGameState.abPlayerChose[0] && gkgtGameState.abPlayerChose[1] && giBattlePrestartTimer++ > 100) {
                vResetObjectsAndSpeed();
                gpkgtCurrentEngineObject->iJumpIdx = RESET_IDX;
                kgtoNewEngineObject(BATTLE_STATE, 0x7f, 0, 0);
                return;
            }
            break;
        }
        break;

    case 4:
        /* back to the title menu, at its step 2 (title demo; the opening demo is skipped) */
        vResetObjectsAndSpeed();
        gpkgtCurrentEngineObject->iJumpIdx = RESET_IDX;
        kgtoNewEngineObject(MENU_TRAVERSAL, 0x7f, 0, 0)->iProcessStep = 2;
        break;
    }
    return;
error_exit:
    gpkgtCurrentEngineObject->iProcessStep = 4;
}

/*
 * vjmpStoryCharacterSelect_Unused - an older story/VS character select that pages through the
 * registered characters with the direction keys (no longer in the jump table, never called).
 * Steps: 0 set up (story select demo, game state 2000), 1 select; a button starts the story
 * (vProgressStoryMode) or the battle (BATTLE_STATE).
 * iPlayerIdx / iObjectType hold the character shown for each side (-1 = none yet), pWork015E /
 * pWork0162 their face objects.
 * Globals read: gkgtKgtSystem, giLastInputCleaned, giLastInputXor, giAnyInputXor, giStoryModePlayerIdx.
 * Globals changed: gkgtGameState.iCharSelect[], iGameStateNumber, giCameraX/Y, giStoryModeSide,
 * gkgtLoadedCharacter (files loaded).
 */
void vjmpStoryCharacterSelect_Unused(void)
{
    int i;              /* side */
    int *piShown;       /* character shown for side i */
    kgtEngineObject **ppFace;
    kgtEngineObject *pFace;
    int iSkill;         /* unused */

    switch (gpkgtCurrentEngineObject->iProcessStep) {
    case 0:
        gpkgtCurrentEngineObject->iProcessStep = 1;
        if (iOpenDemoFile((BYTE)gkgtKgtSystem.cStoryModeDemoIdx) == 0)
            vSpawnEngineObjectForDemoSkills();
        gkgtGameState.iCharSelect[0] = 0;
        gkgtGameState.iCharSelect[1] = 0;
        gpkgtCurrentEngineObject->iPlayerIdx = -1;
        gpkgtCurrentEngineObject->iObjectType = -1;
        gpkgtCurrentEngineObject->pWork015E = (kgtEngineObject *)-1;
        gpkgtCurrentEngineObject->pWork0162 = (kgtEngineObject *)-1;
        giCameraX = 0;
        giCameraY = 480;
        gkgtGameState.iGameStateNumber = 2000;
        /* fall through */
    case 1:
        switch (gkgtGameState.kgtGameMode) {
        case GAME_MODE_STORY:
            /* left/up (5) = previous, right/down (0xa) = next registered character */
            if (giLastInputCleaned[giStoryModePlayerIdx] & 5)
                vSelectPrevRegisteredCharacter(&gkgtGameState.iCharSelect[0]);
            if (giLastInputCleaned[giStoryModePlayerIdx] & 0xa)
                vSelectNextRegisteredCharacter(&gkgtGameState.iCharSelect[0]);
            /* another character: replace the face object (x 220 + 200 per side, y 440 + 480 for the
               camera, 16.16) and load the file into slot 0 */
            if (gkgtGameState.iCharSelect[0] != gpkgtCurrentEngineObject->iPlayerIdx) {
                if (gpkgtCurrentEngineObject->iPlayerIdx != -1)
                    gpkgtCurrentEngineObject->pWork015E->iJumpIdx = RESET_IDX;
                gpkgtCurrentEngineObject->pWork015E = kgtoNewEngineObject(READ_SCRIPT, 0x50, giStoryModePlayerIdx * 0xc80000 + 0xdc0000, 0x3980000);
                gpkgtCurrentEngineObject->iPlayerIdx = gkgtGameState.iCharSelect[0];
                iOpenCharacterFile(0, gkgtGameState.iCharSelect[0]);
                gpkgtCurrentEngineObject->pWork015E->iObjectType = STORY_ENGINE_OBJECT;
                gpkgtCurrentEngineObject->pWork015E->iPlayerIdx = 0;
                gpkgtCurrentEngineObject->pWork015E->iSkillIdx = (unsigned short)gkgtLoadedCharacter[0].shSkillIdxStageFacePic;
                gpkgtCurrentEngineObject->pWork015E->iSkillScriptIdx = (unsigned short)gkgtLoadedCharacter[0].kgtCore.pSkillsAlloc[gpkgtCurrentEngineObject->pWork015E->iSkillIdx].shStartingStepIdx;
                gpkgtCurrentEngineObject->pWork015E->iFlags |= 0x40000000;
            }
            /* a button (A-F) starts the story */
            if (giLastInputXor[giStoryModePlayerIdx] & 0x3f0) {
                vResetObjectsAndSpeed();
                gpkgtCurrentEngineObject->iJumpIdx = RESET_IDX;
                giStoryModeSide = giStoryModePlayerIdx;
                vProgressStoryMode();
            }
            break;
        case GAME_MODE_VS_SINGLE:
        case GAME_MODE_VS_TEAM:
            /* both sides page through the characters, each into its own slot */
            for (i = 0; i < 2; i++) {
                switch (i) {
                case 0:
                    piShown = &gpkgtCurrentEngineObject->iPlayerIdx;
                    pFace = gpkgtCurrentEngineObject->pWork015E;
                    ppFace = &gpkgtCurrentEngineObject->pWork015E;
                    break;
                case 1:
                    piShown = (int *)&gpkgtCurrentEngineObject->iObjectType;
                    pFace = gpkgtCurrentEngineObject->pWork0162;
                    ppFace = &gpkgtCurrentEngineObject->pWork0162;
                    break;
                }
                if (giLastInputCleaned[i] & 5)
                    vSelectPrevRegisteredCharacter(&gkgtGameState.iCharSelect[i]);
                if (giLastInputCleaned[i] & 0xa)
                    vSelectNextRegisteredCharacter(&gkgtGameState.iCharSelect[i]);
                if (gkgtGameState.iCharSelect[i] != *piShown) {
                    if (*piShown != -1)
                        pFace->iJumpIdx = RESET_IDX;
                    pFace = kgtoNewEngineObject(READ_SCRIPT, 0x50, i * 0xc80000 + 0xdc0000, 0x3980000);
                    *ppFace = pFace;
                    *piShown = gkgtGameState.iCharSelect[i];
                    iOpenCharacterFile(i, gkgtGameState.iCharSelect[i]);
                    pFace->iObjectType = STORY_ENGINE_OBJECT;
                    pFace->iPlayerIdx = i;
                    pFace->iSkillIdx = (unsigned short)gkgtLoadedCharacter[i].shSkillIdxStageFacePic;
                    pFace->iSkillScriptIdx = (unsigned short)gkgtLoadedCharacter[i].kgtCore.pSkillsAlloc[pFace->iSkillIdx].shStartingStepIdx;
                    pFace->iFlags |= 0x40000000;
                }
            }
            /* any player's button starts the battle */
            if (giAnyInputXor & 0x3f0) {
                vResetObjectsAndSpeed();
                gpkgtCurrentEngineObject->iJumpIdx = RESET_IDX;
                kgtoNewEngineObject(BATTLE_STATE, 0x7f, 0, 0);
                return;
            }
            break;
        }
        break;
    }
}

/*
 * vjmpMenuTraversal - handler of MENU_TRAVERSAL objects: the title screen.  Plays the opening demo,
 * then the title demo with the game-mode menu (optionally behind a "press start" step); returns to
 * the opening demo when nobody touches the controls for the title demo's play time.  Choosing a mode
 * creates the CHARACTER_SELECT_SCREEN object.
 * Steps (iProcessStep): 0 start the opening demo, 1 opening demo, 2 start the title demo, 3 "press
 * start", 4 build the mode menu, 5 mode menu.
 * iObjectType holds the menu cursor object, pWork015E the idle countdown (frames, as an int), iPosX
 * blocks key repeat (1 until all keys are released).
 * Globals read: gkgtKgtSystem (demo numbers, cSystemBitmask, cursor skills), giAnyInput,
 * giAnyInputXor, giLastInputXor, giDemoTime, gcDemoSkipWithInput.
 * Globals changed: gkgtGameState (iGameStateNumber 1000, iCurrentRound, kgtGameMode), giGameModes,
 * giAmountOfGameModes, giMenuSelectionIdx, giStoryModePlayerIdx, gbStoryMode, giCurrentStoryStep[],
 * gkgtLoadedCharacter[0/1].iWins.
 */
void vjmpMenuTraversal(void)
{
    kgtEngineObject *pMenuCursor;
    kgtSkill *pCursorPos[3];    /* first step of each mode's cursor position skill (x = param 1, y = param 2) */
    int iModes;                 /* number of game modes offered */

    switch (gpkgtCurrentEngineObject->iProcessStep) {
    case 0:
        gpkgtCurrentEngineObject->iProcessStep = 1;
        gkgtGameState.iGameStateNumber = 1000;
        gkgtGameState.iCurrentRound = 0;
        vResetObjectsAndSpeed();
        /* no opening demo: straight to the title demo */
        if (gkgtKgtSystem.cOpeningDemoIdx == 0) {
            gpkgtCurrentEngineObject->iProcessStep = 2;
            return;
        }
        if (iOpenDemoFile((BYTE)gkgtKgtSystem.cOpeningDemoIdx) == 0)
            vSpawnEngineObjectForDemoSkills();
        break;

    case 1:     /* opening demo: as in vjmpInitiateStoryMode, input is ignored for 10 frames (iPlayerIdx)
                   and a skip needs a frame without a new press first (iObjectType flag); without the
                   skip option the demo's play time ends it */
        if (gpkgtCurrentEngineObject->iPlayerIdx < 10) {
            gpkgtCurrentEngineObject->iPlayerIdx++;
            return;
        }
        if (gcDemoSkipWithInput & 1) {
            if (giAnyInputXor & 0x3f0) {
                if (gpkgtCurrentEngineObject->iObjectType != PLAYER_ENGINE_OBJECT)
                    gpkgtCurrentEngineObject->iProcessStep = 2;
            } else {
                gpkgtCurrentEngineObject->iObjectType = STORY_ENGINE_OBJECT;
            }
        } else if (giDemoTime != 0 && --giDemoTime == 0) {
            gpkgtCurrentEngineObject->iProcessStep = 2;
        }
        break;

    case 2:     /* title demo */
        vResetObjectsAndSpeed();
        if (iOpenDemoFile((BYTE)gkgtKgtSystem.cTitleDemoIdx) == 0)
            vSpawnEngineObjectForDemoSkills();
        /* idle countdown = the title demo's play time; cSystemBitmask 0x40 = "press start" step */
        gpkgtCurrentEngineObject->pWork015E = (kgtEngineObject *)giDemoTime;
        if (gkgtKgtSystem.cSystemBitmask & 0x40)
            gpkgtCurrentEngineObject->iProcessStep++;
        else
            gpkgtCurrentEngineObject->iProcessStep = 4;
        break;

    case 3:     /* "press start": any key; when the countdown runs out, back to the opening demo */
        if (giAnyInputXor != 0) {
            gpkgtCurrentEngineObject->iProcessStep = 4;
            return;
        }
        if (gkgtKgtSystem.cOpeningDemoIdx != 0 && gpkgtCurrentEngineObject->pWork015E != NULL
            && (gpkgtCurrentEngineObject->pWork015E = (kgtEngineObject *)((int)gpkgtCurrentEngineObject->pWork015E - 1)) == NULL) {
            vResetObjectsAndSpeed();
            gpkgtCurrentEngineObject->iProcessStep = 0;
            return;
        }
        break;

    case 4:     /* build the mode menu: the modes the system file enables (cSystemBitmask 4 story,
                   8 versus single, 0x10 versus team); none = the title stays without a menu */
        giCurrentStoryStep[0] = -1;
        giCurrentStoryStep[1] = -1;
        gpkgtCurrentEngineObject->iProcessStep = 5;
        iModes = 0;
        if (gkgtKgtSystem.cSystemBitmask & 4)
            giGameModes[iModes++] = GAME_MODE_STORY;
        if (gkgtKgtSystem.cSystemBitmask & 8)
            giGameModes[iModes++] = GAME_MODE_VS_SINGLE;
        if (gkgtKgtSystem.cSystemBitmask & 0x10)
            giGameModes[iModes++] = GAME_MODE_VS_TEAM;
        if (iModes == 0) {
            gpkgtCurrentEngineObject->iJumpIdx = RESET_IDX;
            return;
        }
        giAmountOfGameModes = iModes - 1;
        pMenuCursor = kgtoNewObjectForSkillIdx((unsigned short)gkgtKgtSystem.shSkillIdxTitleCursor, 0x65, 0, 0);
        gkgtLoadedCharacter[0].iWins = 0;
        gkgtLoadedCharacter[1].iWins = 0;
        gpkgtCurrentEngineObject->iObjectType = (kgtEngineObjectTypes)pMenuCursor;
        gkgtGameState.iGameStateNumber = 1000;
        /* fall through */
    case 5:     /* mode menu: up/left (5) and down/right (0xa) move, a button A-F chooses; one move
                   per key press (iPosX) */
        if (giAnyInput == 0) {
            gpkgtCurrentEngineObject->iPosX = 0;
        } else if ((giAnyInputXor & 5) && gpkgtCurrentEngineObject->iPosX == 0) {
            if (--giMenuSelectionIdx < 0)
                giMenuSelectionIdx = giAmountOfGameModes;
            gpkgtCurrentEngineObject->iPosX = 1;
        } else if ((giAnyInputXor & 0xa) && gpkgtCurrentEngineObject->iPosX == 0) {
            if (++giMenuSelectionIdx > giAmountOfGameModes)
                giMenuSelectionIdx = 0;
            gpkgtCurrentEngineObject->iPosX = 1;
        } else if ((giAnyInputXor & 0x3f0) && gpkgtCurrentEngineObject->iPosX == 0) {
            /* the player who pressed the button plays story mode (2P wins a tie) */
            if (giLastInputXor[0] & 0x3f0)
                giStoryModePlayerIdx = 0;
            if (giLastInputXor[1] & 0x3f0)
                giStoryModePlayerIdx = 1;
            gkgtGameState.kgtGameMode = giGameModes[giMenuSelectionIdx];
            switch (gkgtGameState.kgtGameMode) {
            case GAME_MODE_STORY:
                gbStoryMode = 1;
                break;
            case GAME_MODE_VS_SINGLE:
                gbStoryMode = 0;
                break;
            case GAME_MODE_VS_TEAM:
                gbStoryMode = 0;
                break;
            }
            kgtoNewEngineObject(CHARACTER_SELECT_SCREEN, 0x7f, 0, 0);
                gpkgtCurrentEngineObject->iJumpIdx = RESET_IDX;
        }
        /* place the cursor at the selected mode's position (first step of the mode's position skill) */
        pCursorPos[0] = &gkgtKgtSystem.kgtCore.pSkillScriptsAlloc[(unsigned short)gkgtKgtSystem.kgtCore.pSkillsAlloc[(unsigned short)gkgtKgtSystem.shSkillIdxPositionForStoryMode].shStartingStepIdx];
        pCursorPos[1] = &gkgtKgtSystem.kgtCore.pSkillScriptsAlloc[(unsigned short)gkgtKgtSystem.kgtCore.pSkillsAlloc[(unsigned short)gkgtKgtSystem.shSkillIdxPositionForVsMode].shStartingStepIdx];
        pCursorPos[2] = &gkgtKgtSystem.kgtCore.pSkillScriptsAlloc[(unsigned short)gkgtKgtSystem.kgtCore.pSkillsAlloc[(unsigned short)gkgtKgtSystem.shSkillIdxPosCursorForTeamBattle].shStartingStepIdx];
        ((kgtEngineObject *)gpkgtCurrentEngineObject->iObjectType)->iPosX = pCursorPos[giGameModes[giMenuSelectionIdx]]->shParam1 << 16;
        ((kgtEngineObject *)gpkgtCurrentEngineObject->iObjectType)->iPosY = pCursorPos[giGameModes[giMenuSelectionIdx]]->shParam2 << 16;
        /* idle countdown (restarted by any key): back to the opening demo */
        if ((gkgtKgtSystem.cOpeningDemoIdx != 0 || (gkgtKgtSystem.cSystemBitmask & 0x40)) && gpkgtCurrentEngineObject->pWork015E != NULL
            && (gpkgtCurrentEngineObject->pWork015E = (kgtEngineObject *)((int)gpkgtCurrentEngineObject->pWork015E - 1)) == NULL) {
            vResetObjectsAndSpeed();
            gpkgtCurrentEngineObject->iProcessStep = 0;
            return;
        }
        if (giAnyInput != 0)
            gpkgtCurrentEngineObject->pWork015E = (kgtEngineObject *)giDemoTime;
        break;
    }
}

/*
 * vjmpGameOverScreen - handler of GAME_OVER_SCREEN objects: the story mode "continue?" screen.
 * Steps: 0 start the game over demo and the cursor, 1 choose.  Any direction toggles the selection,
 * a button A-F confirms: continue replays the lost story step with the saved round and wins, give up
 * returns to the title menu.
 * pWork015E is the selection (0 continue, 1 give up), iObjectType the cursor object, iPosX blocks key
 * repeat.
 * Globals read: gkgtKgtSystem, giAnyInput, giAnyInputXor, giStoryModeCurrentRound, giStoryWinsP1/P2.
 * Globals changed: gkgtGameState (iGameStateNumber, iCurrentRound), gkgtLoadedCharacter[0/1].iWins,
 * giCurrentStoryStep[].
 */
void vjmpGameOverScreen(void)
{
    kgtEngineObject *pCursor;
    kgtSkill *pCursorPos[2];

    switch (gpkgtCurrentEngineObject->iProcessStep) {
    case 0:
        gpkgtCurrentEngineObject->iProcessStep = 1;
        vResetObjectsAndSpeed();
        if (iOpenDemoFile((BYTE)gkgtKgtSystem.cGameOverDemoIdx) == 0)
            vSpawnEngineObjectForDemoSkills();
        pCursor = kgtoNewObjectForSkillIdx((unsigned short)gkgtKgtSystem.shSkillIdxContinueCursor, 0x65, 0, 0);
        gkgtGameState.iGameStateNumber = 4000;
        gpkgtCurrentEngineObject->iObjectType = (kgtEngineObjectTypes)pCursor;
        break;
    case 1:
        if (giAnyInput == 0) {
            gpkgtCurrentEngineObject->iPosX = 0;
        } else if ((giAnyInputXor & 0xf) && gpkgtCurrentEngineObject->iPosX == 0) {
            gpkgtCurrentEngineObject->pWork015E = (kgtEngineObject *)((int)gpkgtCurrentEngineObject->pWork015E ^ 1);
            gpkgtCurrentEngineObject->iPosX = 1;
        } else if ((giAnyInputXor & 0x3f0) && gpkgtCurrentEngineObject->iPosX == 0) {
            vResetObjectsAndSpeed();
            switch ((int)gpkgtCurrentEngineObject->pWork015E) {
            case 0:     /* continue: replay the story step (vProgressStoryMode advances it again) */
                gkgtGameState.iCurrentRound = giStoryModeCurrentRound;
                gkgtLoadedCharacter[0].iWins = giStoryWinsP1;
                gkgtLoadedCharacter[1].iWins = giStoryWinsP2;
                giCurrentStoryStep[giStoryModeSide]--;
                vProgressStoryMode();
                break;
            case 1:     /* give up: title menu */
                kgtoNewEngineObject(MENU_TRAVERSAL, 0x7f, 0, 0);
                break;
            }
            gpkgtCurrentEngineObject->iJumpIdx = RESET_IDX;
        }
        /* place the cursor (x = param 1, y = param 2 of the first step of the yes / no position skills) */
        pCursorPos[0] = &gkgtKgtSystem.kgtCore.pSkillScriptsAlloc[(unsigned short)gkgtKgtSystem.kgtCore.pSkillsAlloc[(unsigned short)gkgtKgtSystem.shSkillIdxPositionCursorItDoes].shStartingStepIdx];
        pCursorPos[1] = &gkgtKgtSystem.kgtCore.pSkillScriptsAlloc[(unsigned short)gkgtKgtSystem.kgtCore.pSkillsAlloc[(unsigned short)gkgtKgtSystem.shSkillIdxPositionCursorItDoesNot].shStartingStepIdx];
        ((kgtEngineObject *)gpkgtCurrentEngineObject->iObjectType)->iPosX = pCursorPos[(int)gpkgtCurrentEngineObject->pWork015E]->shParam1 << 16;
        ((kgtEngineObject *)gpkgtCurrentEngineObject->iObjectType)->iPosY = pCursorPos[(int)gpkgtCurrentEngineObject->pWork015E]->shParam2 << 16;
        break;
    }
}

/*
 * vjmpHandleBattleState - handler of BATTLE_STATE objects: the flow of a match (story fight, versus
 * single or versus team), round by round.
 *
 * Steps (iProcessStep); iPlayerIdx is the wait counter (frames) of the announcement animations, set
 * from the first step of each announcement skill (kgtSpawnNewEngineObjectReturnSkillField0x1):
 *     0/1          load the stage and the characters, rounds needed, game state 3000
 *     100          round start: spawn the fighters, the stage and UI objects; 110-113 "round N",
 *                  "fight" (story fights can switch either off)
 *     200          fighting: timer, KO / 100 win points, forced round end
 *     300          round end: decide the result
 *     410/420/430  story win / lose / draw; 510 draw, 520/530 1P/2P win ("K.O.", "xP wins",
 *                  "perfect"), 540 double KO; the following steps wait for the animations
 *     900-902      round over: next round, next team member, next story step, game over screen or
 *                  back to character select
 * While gkgtGameState.bPaused is set only the pause keys are handled (start resumes, A+B+C quits).
 * Round results (kgt_character_struct.iRoundResult): 1 won, 2 lost by KO, 3 lost at time over / draw.
 * Globals read: gkgtKgtSystem (announcement skills), gkgtLoadedCharacter[0].kgtStoryEntries,
 * giCurrentStoryStep[], giStoryModeSide, giConfig* settings, giAppmode, giAnyInput, giAnyInputXor,
 * giForceRoundEnd.
 * Globals changed: gkgtGameState (rounds, timer, round phase, team members, carry-over), gkgtLoadedCharacter
 * (health, win points, wins, round results, CPU settings, colours), giCameraX/Y, the screen shake
 * globals, giStoryFrontStageFlag, giRoundsNotWon, giStoryModeCurrentRound, giStoryWinsP1/P2,
 * giForceRoundEnd, gkgtEngineObjects.
 */
void vjmpHandleBattleState(void)
{
    int i, iCount;      /* iCount: fighters (story start x average), round-end reason, fighters standing */
    int iStartXSum;     /* sum of the story fighters' start x (pixels) */
    WORD wSkill;        /* matching: a WORD local (not an int with a cast) gives the original's
                           load/store order (vc6-matching-notes/matching-techniques.md) */
    int iBestOther, iBestOwnSide;   /* best win points of the opponents / of the player's side */
    kgtStoryEntry *pEntry;          /* the current story step */
    kgtEngineObject *pObj;          /* unused */
    kgtEngineObject *pScan;

    if (gkgtGameState.bPaused == 0) {
        /* start pauses the game and shows the pause skill (layer 0x78) */
        if (giAnyInputXor & 0x400) {
            gkgtGameState.bPaused = 1;
            kgtoNewObjectForSkillIdx((unsigned short)gkgtKgtSystem.shSkillIdxPause, 0x78, 0, 0);
            return;
        }
        switch (gpkgtCurrentEngineObject->iProcessStep) {
        case 0:
            gpkgtCurrentEngineObject->iProcessStep = 1;
            /* fall through */
        case 1:
            /* load the match: stage, characters and the number of rounds (wins) needed */
            gpkgtCurrentEngineObject->iPlayerIdx = 0;
            gpkgtCurrentEngineObject->iProcessStep = 100;
            vResetObjectsAndSpeed();
            giStoryFrontStageFlag = 0;
            giRoundsNotWon = 0;
            gkgtGameState.iConfigTestplayStageNb = giConfigTestplayStageNb;
            switch (gkgtGameState.kgtGameMode) {
            case GAME_MODE_STORY:
                /* the current story step's stage (1-based); the characters are already loaded */
                iOpenStageFile((BYTE)gkgtLoadedCharacter[0].kgtStoryEntries[giCurrentStoryStep[giStoryModeSide]].cStageIdx - 1);
                pEntry = &gkgtLoadedCharacter[0].kgtStoryEntries[giCurrentStoryStep[giStoryModeSide]];
                if (pEntry->cWhenDefeatAndFirstRoundBitmask & 2) {
                    /* carry over (fight flag 0x02): keep round and wins, remember them for a continue */
                    giStoryWinsP1 = gkgtLoadedCharacter[0].iWins;
                    giStoryWinsP2 = gkgtLoadedCharacter[1].iWins;
                } else {
                    gkgtGameState.iCurrentRound = 0;
                    gkgtLoadedCharacter[0].iWins = 0;
                    gkgtLoadedCharacter[1].iWins = 0;
                }
                gkgtGameState.iRoundsAmount = (BYTE)pEntry->cRoundsAmount;
                giStoryModeCurrentRound = gkgtGameState.iCurrentRound;
                break;
            case GAME_MODE_VS_SINGLE:
                gkgtGameState.iCurrentRound = 0;
                gkgtGameState.iRoundsAmount = giConfigNumberOfRounds;
                iOpenStageFile(giConfigTestplayStageNb);
                iOpenCharacterFile(0, gkgtGameState.iCharSelect[0]);
                iOpenCharacterFile(1, gkgtGameState.iCharSelect[1]);
                break;
            case GAME_MODE_VS_TEAM:
                gkgtGameState.iCurrentRound = 0;
                iOpenStageFile(giConfigTestplayStageNb);
                gkgtGameState.iRoundsAmount = gkgtGameState.iConfigNumberOfRoundsTeamVs;
                gkgtGameState.aiTeamMember[0] = 0;
                gkgtGameState.aiTeamMember[1] = 0;
                gkgtGameState.iCarryOverPlayer = -1;
                iOpenCharacterFile(0, gkgtGameState.iCharSelect[0]);
                iOpenCharacterFile(1, gkgtGameState.iCharSelect[1]);
                break;
            }
            /* sanity checks of the round count */
            switch (gkgtGameState.kgtGameMode) {
            case GAME_MODE_STORY:
                if (gkgtGameState.iRoundsAmount <= gkgtGameState.iCurrentRound) {
                    iSetDebugInfo("\212\371\202\311\203\211\203E\203\223\203h\220\224\202\252\217\237\202\277\224\262\202\253\226{\220\224\202\311\222B\202\265\202\304\202\242\202\334\202\267\201B", 0x4040ff);  /* the round count has already reached the number of wins */
                    vProgressStoryMode();
                    gpkgtCurrentEngineObject->iJumpIdx = RESET_IDX;
                    gkgtGameState.iGameStateNumber = 3000;
                    return;
                }
                /* fall through */
            case GAME_MODE_VS_SINGLE:
                if (gkgtGameState.iRoundsAmount == 0) {
                    iSetDebugInfo("Error \217\237\202\277\224\262\202\253\226{\220\224\202\252\202O", 0x4040ff);  /* Error: number of wins is 0 */
                    switch (gkgtGameState.kgtGameMode) {
                    case GAME_MODE_STORY:
                        vProgressStoryMode();
                        gpkgtCurrentEngineObject->iJumpIdx = RESET_IDX;
                        break;
                    case GAME_MODE_VS_SINGLE:
                    case GAME_MODE_VS_TEAM:
                        kgtoNewEngineObject(MENU_TRAVERSAL, 0x7f, 0, 0);
                        gkgtGameState.iGameStateNumber = 3000;
                        return;
                    }
                }
                break;
            }
            gkgtGameState.iGameStateNumber = 3000;
            return;

        case 100:
            /* round start: next round, reset every slot's win points and life (iLifeMax 1 avoids a
               division by zero before the fighters set it) */
            gpkgtCurrentEngineObject->iProcessStep = 101;
            gkgtGameState.iCurrentRound++;
            giCameraY = 0;
            gkgtGameState.dwRoundPhase = 0;
            for (i = 0; i < 8; i++) {
                gkgtLoadedCharacter[i].iWinPoints = 0;
                gkgtLoadedCharacter[i].iHealth = 0;
                gkgtLoadedCharacter[i].iLifeMax = 1;
            }
            switch (gkgtGameState.kgtGameMode) {
            case GAME_MODE_STORY:
                /* the player (slot 0) and the step's CPU opponents (slots 1-7), at their start x and
                   y 920 (16.16; ground level with the camera at y 480); CPU level clamped to 0-100.
                   The camera starts centred on the average start x. */
                iCount = 1;
                pEntry = &gkgtLoadedCharacter[0].kgtStoryEntries[giCurrentStoryStep[giStoryModeSide]];
                kgtoNewEngineObject(READ_SCRIPT, 0x50, (unsigned short)pEntry->shPlayerStartXPos << 16, 0x3980000)->iPlayerIdx = 0;
                iStartXSum = (unsigned short)pEntry->shPlayerStartXPos;
                for (i = 0; i < 7; i++) {
                    if (pEntry->kgtStoryEntryCPUs[i].cCharacterIdx) {
                        kgtoNewEngineObject(READ_SCRIPT, 0x50, (unsigned short)pEntry->kgtStoryEntryCPUs[i].shStartPos << 16, 0x3980000)->iPlayerIdx = i + 1;
                        iStartXSum += (unsigned short)pEntry->kgtStoryEntryCPUs[i].shStartPos;
                        iCount++;
                        gkgtLoadedCharacter[i + 1].bCpuControlled = 1;
                        gkgtLoadedCharacter[i + 1].iCpuLevel = (BYTE)pEntry->kgtStoryEntryCPUs[i].cCpuLevel;
                        if (gkgtLoadedCharacter[i + 1].iCpuLevel < 0)
                            gkgtLoadedCharacter[i + 1].iCpuLevel = 0;
                        if (gkgtLoadedCharacter[i + 1].iCpuLevel > 100)
                            gkgtLoadedCharacter[i + 1].iCpuLevel = 100;
                        gkgtLoadedCharacter[i + 1].iCpuMode = 1;
                    }
                }
                gkgtGameState.iTargetPlayer = -1;
                giCameraX = iStartXSum / iCount - 320;
                /* round time: time setting * 100 frames */
                gkgtGameState.iGameTimerInFrames = (unsigned short)pEntry->shTime * 100 - 1;
                break;
            case GAME_MODE_VS_SINGLE:
                /* both players human unless giAppmode is 3 (test play started with -F, which keeps
                   the CPU settings); 1P at x 390, 2P at x 890, y 920 (16.16) */
                if (giAppmode != 3) {
                    gkgtLoadedCharacter[0].bCpuControlled = 0;
                    gkgtLoadedCharacter[1].bCpuControlled = 0;
                }
                kgtoNewEngineObject(READ_SCRIPT, 0x50, 0x1860000, 0x3980000)->iPlayerIdx = 0;
                kgtoNewEngineObject(READ_SCRIPT, 0x50, 0x37a0000, 0x3980000)->iPlayerIdx = 1;
                gkgtGameState.iGameTimerInFrames = giConfigTestplayTime * 100 - 1;
                break;
            case GAME_MODE_VS_TEAM:
                if (giAppmode != 3) {
                    gkgtLoadedCharacter[0].bCpuControlled = 0;
                    gkgtLoadedCharacter[1].bCpuControlled = 0;
                }
                /* load the current member of each team into slots 0 / 1 with its chosen colour */
                gkgtGameState.iCarryOverPlayer = -1;
                for (i = 0; i < 8; i++)
                    gkgtLoadedCharacter[i].iColor = -1;
                iOpenCharacterFile(0, gkgtGameState.aiTeamRoster[gkgtGameState.aiTeamMember[0]]);
                iOpenCharacterFile(1, gkgtGameState.aiTeamRoster[gkgtGameState.aiTeamMember[1] + 4]);
                kgtoNewEngineObject(READ_SCRIPT, 0x50, 0x1860000, 0x3980000)->iPlayerIdx = 0;
                kgtoNewEngineObject(READ_SCRIPT, 0x50, 0x37a0000, 0x3980000)->iPlayerIdx = 1;
                giCameraX = 320;
                gkgtGameState.iGameTimerInFrames = giConfigTestplayTime * 100 - 1;
                gkgtLoadedCharacter[0].iColor = gkgtGameState.aiTeamColor[gkgtGameState.aiTeamMember[0]];
                gkgtLoadedCharacter[1].iColor = gkgtGameState.aiTeamColor[gkgtGameState.aiTeamMember[1] + 4];
                break;
            }
            /* background, battle interface and the two screen-control objects (layers 1 and 13) */
            vSpawnStageScripts();
            kgtoNewEngineObject(BATTLE_UI, 0xf, 0, 0);
            kgtoNewEngineObject(SCREEN_CONTROL, 1, 0, 0)->iProcessStep = 12;
            kgtoNewEngineObject(SCREEN_CONTROL, 13, 0, 0)->iProcessStep = 14;
            gpkgtCurrentEngineObject->iPosX = 320;
            gpkgtCurrentEngineObject->iPosY = 150;
            gpkgtCurrentEngineObject->iPlayerIdx = 100;
            /* no screen shake */
            giShakeXMode = 0;
            giShakeXOffset = 0;
            giShakeXAmplitude = 0;
            giShakeXTimeLeft = 0;
            giShakeXDuration = 0;
            giShakeYMode = 0;
            giShakeYOffset = 0;
            giShakeYAmplitude = 0;
            giShakeYTimeLeft = 0;
            giShakeYDuration = 0;
            /* round animation start; its first step's parameter is the wait */
            gpkgtCurrentEngineObject->iProcessStep = 110;
            gpkgtCurrentEngineObject->iPlayerIdx = kgtSpawnNewEngineObjectReturnSkillField0x1((unsigned short)gkgtKgtSystem.shSkillIdxRoundAniStartTime, 0x65, 0, 0);
            return;

        case 110:   /* "round N" (story fights: only with option 0x01) */
            if (gpkgtCurrentEngineObject->iPlayerIdx-- > 0)
                return;
            gpkgtCurrentEngineObject->iProcessStep++;
            if (gkgtGameState.kgtGameMode == GAME_MODE_STORY
                && !(gkgtLoadedCharacter[0].kgtStoryEntries[giCurrentStoryStep[giStoryModeSide]].cOptionsBitmask & 1)) {
                gpkgtCurrentEngineObject->iPlayerIdx = 0;
                return;
            }
            /* (&shSkillIdxRoundAniEndTime)[n] = shSkillIdxRound1 + n - 1 (consecutive shorts); rounds
               from 10 on show nothing */
            if (gkgtGameState.iCurrentRound < 10)
                gpkgtCurrentEngineObject->iPlayerIdx = kgtSpawnNewEngineObjectReturnSkillField0x1((unsigned short)(&gkgtKgtSystem.shSkillIdxRoundAniEndTime)[gkgtGameState.iCurrentRound], 0x65, 0, 0);
            else
                gpkgtCurrentEngineObject->iPlayerIdx = 0;
            /* fall through */
        case 111:   /* "fight" (story fights: only with option 0x02) */
            if (gpkgtCurrentEngineObject->iPlayerIdx-- > 0)
                return;
            gpkgtCurrentEngineObject->iProcessStep++;
            if (gkgtGameState.kgtGameMode == GAME_MODE_STORY
                && !(gkgtLoadedCharacter[0].kgtStoryEntries[giCurrentStoryStep[giStoryModeSide]].cOptionsBitmask & 2)) {
                gpkgtCurrentEngineObject->iPlayerIdx = 0;
                return;
            }
            gpkgtCurrentEngineObject->iPlayerIdx = kgtSpawnNewEngineObjectReturnSkillField0x1((unsigned short)gkgtKgtSystem.shSkillIdxSpirits, 0x65, 0, 0);
            /* fall through */
        case 112:
            if (gpkgtCurrentEngineObject->iPlayerIdx-- > 0)
                return;
            gpkgtCurrentEngineObject->iProcessStep++;
            /* fall through */
        case 113:
            /* the fight begins */
            gpkgtCurrentEngineObject->iProcessStep = 200;
            gkgtGameState.dwRoundPhase = 1;
            return;

        case 200:   /* fighting: count down the time, look for the end of the round */
            if (gkgtGameState.iGameTimerInFrames >= 0 && --gkgtGameState.iGameTimerInFrames < 0) {
                gpkgtCurrentEngineObject->iPlayerIdx = 0;
                gkgtGameState.iGameTimerInFrames = 0;
                gpkgtCurrentEngineObject->iProcessStep = 300;
            }
            switch (gkgtGameState.kgtGameMode) {
            case GAME_MODE_STORY:
                /* 1: the player is down, i + 2: fighter i reached 100 win points */
                iCount = 0;
                if (gkgtLoadedCharacter[0].iHealth == 0)
                    iCount = 1;
                for (i = 0; i < 8; i++) {
                    if (gkgtLoadedCharacter[i].iWinPoints >= 100)
                        iCount = i + 2;
                }
                if (iCount != 0) {
                    gpkgtCurrentEngineObject->iPlayerIdx = 0;
                    gpkgtCurrentEngineObject->iProcessStep = 300;
                }
                break;
            case GAME_MODE_VS_SINGLE:
            case GAME_MODE_VS_TEAM:
                /* the round ends when fewer than two fighters are standing */
                iCount = 0;
                pScan = gkgtEngineObjects;
                for (i = 0; i < 1024; i++, pScan++) {
                    if (pScan->iJumpIdx == READ_SCRIPT && pScan->iObjectType == PLAYER_ENGINE_OBJECT && gkgtLoadedCharacter[pScan->iPlayerIdx].iHealth != 0)
                        iCount++;
                }
                if (iCount < 2) {
                    gpkgtCurrentEngineObject->iPlayerIdx = 0;
                    gpkgtCurrentEngineObject->iProcessStep = 300;
                }
                break;
            }
            /* round ended from outside (giForceRoundEnd, e.g. a script) */
            if (giForceRoundEnd == 0)
                return;
            gpkgtCurrentEngineObject->iProcessStep = 300;
            giForceRoundEnd = 0;
            gkgtGameState.dwRoundPhase = 2;
            return;

        case 300:   /* round end: life in 1/1000 of the maximum for every slot */
            for (i = 0; i < 8; i++) {
                if (gkgtLoadedCharacter[i].iLifeMax != 0)
                    gkgtLoadedCharacter[i].iLifePermille = gkgtLoadedCharacter[i].iHealth * 1000 / gkgtLoadedCharacter[i].iLifeMax;
                else
                    gkgtLoadedCharacter[i].iLifePermille = 0;
            }
            switch (gkgtGameState.kgtGameMode) {
            case GAME_MODE_STORY:
                pEntry = &gkgtLoadedCharacter[0].kgtStoryEntries[giCurrentStoryStep[giStoryModeSide]];
                if (gkgtGameState.iGameTimerInFrames == 0) {
                    /* time over: the entry's time-over points go to the CPU it names (cWhenTimeOver + 1)
                       if it has more life than the player, and each CPU's cWhenTimeAmount points go
                       to its cWhenTimeTarget if that one (the player or a CPU) has more life */
                    if (gkgtLoadedCharacter[0].iLifePermille < gkgtLoadedCharacter[(BYTE)pEntry->cWhenTimeOver + 1].iLifePermille
                        && gkgtLoadedCharacter[(BYTE)pEntry->cWhenTimeOver + 1].iLifeMax != 0
                        && gkgtLoadedCharacter[(BYTE)pEntry->cWhenTimeOver + 1].bCpuControlled != 0)
                        gkgtLoadedCharacter[(BYTE)pEntry->cWhenTimeOver + 1].iWinPoints += (BYTE)pEntry->cWhenTimeOverNumber;
                    for (i = 1; i < 8; i++) {
                        if (gkgtLoadedCharacter[i].bCpuControlled != 0) {
                            if (gkgtLoadedCharacter[(BYTE)pEntry->kgtStoryEntryCPUs[i - 1].cWhenTimeTarget].iLifeMax != 0
                                && (gkgtLoadedCharacter[(BYTE)pEntry->kgtStoryEntryCPUs[i - 1].cWhenTimeTarget].bCpuControlled != 0 || pEntry->kgtStoryEntryCPUs[i - 1].cWhenTimeTarget == 0)
                                && gkgtLoadedCharacter[i].iLifeMax != 0
                                && gkgtLoadedCharacter[i].iLifePermille < gkgtLoadedCharacter[(BYTE)pEntry->kgtStoryEntryCPUs[i - 1].cWhenTimeTarget].iLifePermille)
                                gkgtLoadedCharacter[(BYTE)pEntry->kgtStoryEntryCPUs[i - 1].cWhenTimeTarget].iWinPoints += (BYTE)pEntry->kgtStoryEntryCPUs[i - 1].cWhenTimeAmount;
                        }
                    }
                }
                /* compare the best score on the player's side (the player and the CPUs with uBitmask
                   0x200) with the best of the others; only scores of 100 or more count for the CPUs */
                iBestOwnSide = gkgtLoadedCharacter[0].iWinPoints;
                iBestOther = 0;
                for (i = 1; i < 8; i++) {
                    if (gkgtLoadedCharacter[i].iWinPoints >= 100) {
                        if (pEntry->kgtStoryEntryCPUs[i - 1].uBitmask & 0x200) {
                            if (gkgtLoadedCharacter[i].iWinPoints > iBestOwnSide)
                                iBestOwnSide = gkgtLoadedCharacter[i].iWinPoints;
                        } else {
                            if (gkgtLoadedCharacter[i].iWinPoints > iBestOther)
                                iBestOther = gkgtLoadedCharacter[i].iWinPoints;
                        }
                    }
                }
                if (iBestOwnSide == iBestOther) {
                    iSetDebugInfo("\203X\203g\201[\203\212\201[\203h\203\215\201[", 0xffff1f);  /* story draw */
                    gpkgtCurrentEngineObject->iProcessStep = 430;
                    giRoundsNotWon++;
                    return;
                }
                if (iBestOwnSide > iBestOther) {
                    iSetDebugInfo("\203X\203g\201[\203\212\201[\220\254\214\367", 0xffff1f);  /* story success */
                    gpkgtCurrentEngineObject->iProcessStep = 410;
                    return;
                }
                iSetDebugInfo("\203X\203g\201[\203\212\201[\216\270\224s", 0xffff1f);  /* story failure */
                giRoundsNotWon++;
                gpkgtCurrentEngineObject->iProcessStep = 420;
                return;
                iSetDebugInfo("\203X\203g\201[\203\212\201[\216\270\224s", 0xffff1f);    /* unreachable; matching: keeps the original's
                                                                                   second copy of the string literal in .data */
            case GAME_MODE_VS_SINGLE:
            case GAME_MODE_VS_TEAM:
                /* versus: compare the two players' remaining life */
                if (gkgtLoadedCharacter[0].iLifePermille == 0 && gkgtLoadedCharacter[1].iLifePermille == 0) {
                    gpkgtCurrentEngineObject->iProcessStep = 540;
                    iSetDebugInfo("w ko", 0xffff1f);
                } else if (gkgtLoadedCharacter[0].iLifePermille == gkgtLoadedCharacter[1].iLifePermille) {
                    gpkgtCurrentEngineObject->iProcessStep = 510;
                    iSetDebugInfo("draw", 0xffff1f);
                } else if (gkgtLoadedCharacter[0].iLifePermille > gkgtLoadedCharacter[1].iLifePermille) {
                    gpkgtCurrentEngineObject->iProcessStep = 520;
                    iSetDebugInfo("1p win", 0xffff1f);
                } else {
                    gpkgtCurrentEngineObject->iProcessStep = 530;
                    iSetDebugInfo("2p win", 0xffff1f);
                }
                return;
            }
            return;

        case 410:   /* story: "you win"; the CPUs that did not win lose (by KO, or time over when the
                       timer ran out); CPUs on the player's side (0x200) win with the player */
            gpkgtCurrentEngineObject->iProcessStep++;
            gpkgtCurrentEngineObject->iPlayerIdx = kgtSpawnNewEngineObjectReturnSkillField0x1((unsigned short)gkgtKgtSystem.shSkillIdxYouWin, 0x65, 0, 0);
            gkgtLoadedCharacter[0].iRoundResult = 1;
            gkgtLoadedCharacter[0].iWins++;
            pEntry = &gkgtLoadedCharacter[0].kgtStoryEntries[giCurrentStoryStep[giStoryModeSide]];
            for (i = 1; i < 8; i++) {
                if (gkgtLoadedCharacter[i].iRoundResult == 0) {
                    gkgtLoadedCharacter[i].iRoundResult = gkgtGameState.iGameTimerInFrames ? 2 : 3;
                    if (pEntry->kgtStoryEntryCPUs[i - 1].uBitmask & 0x200)
                        gkgtLoadedCharacter[i].iRoundResult = 1;
                }
            }
            goto wait_then_round_over;

        case 420:   /* story: "you lose"; the CPUs win, except those on the player's side (0x200) */
            gpkgtCurrentEngineObject->iProcessStep++;
            gpkgtCurrentEngineObject->iPlayerIdx = kgtSpawnNewEngineObjectReturnSkillField0x1((unsigned short)gkgtKgtSystem.shSkillIdxYouLose, 0x65, 0, 0);
            gkgtLoadedCharacter[0].iRoundResult = gkgtGameState.iGameTimerInFrames ? 2 : 3;
            gkgtLoadedCharacter[1].iWins++;
            pEntry = &gkgtLoadedCharacter[0].kgtStoryEntries[giCurrentStoryStep[giStoryModeSide]];
            for (i = 1; i < 8; i++) {
                if (gkgtLoadedCharacter[i].iRoundResult == 0) {
                    gkgtLoadedCharacter[i].iRoundResult = 1;
                    if (pEntry->kgtStoryEntryCPUs[i - 1].uBitmask & 0x200)
                        gkgtLoadedCharacter[i].iRoundResult = 3;
                }
            }
            goto wait_then_round_over;

        case 510:   /* draw: both get a win */
            wSkill = gkgtKgtSystem.shSkillIdxDraw;
            gpkgtCurrentEngineObject->iProcessStep++;
            gpkgtCurrentEngineObject->iPlayerIdx = kgtSpawnNewEngineObjectReturnSkillField0x1(wSkill, 0x65, 0, 0);
            gkgtLoadedCharacter[0].iRoundResult = 3;
            gkgtLoadedCharacter[1].iRoundResult = 3;
            gkgtLoadedCharacter[0].iWins++;
            gkgtLoadedCharacter[1].iWins++;
            goto wait_then_round_over;


        case 430:   /* story: draw; both slots 0 and 1 get a win */
            gpkgtCurrentEngineObject->iProcessStep++;
            gpkgtCurrentEngineObject->iPlayerIdx = kgtSpawnNewEngineObjectReturnSkillField0x1((unsigned short)gkgtKgtSystem.shSkillIdxDraw, 0x65, 0, 0);
            gkgtLoadedCharacter[0].iRoundResult = 3;
            gkgtLoadedCharacter[1].iRoundResult = 3;
            gkgtLoadedCharacter[2].iRoundResult = 3;
            gkgtLoadedCharacter[3].iRoundResult = 3;
            gkgtLoadedCharacter[4].iRoundResult = 3;
            gkgtLoadedCharacter[5].iRoundResult = 3;
            gkgtLoadedCharacter[6].iRoundResult = 3;
            gkgtLoadedCharacter[7].iRoundResult = 3;
            gkgtLoadedCharacter[0].iWins++;
            gkgtLoadedCharacter[1].iWins++;
            goto wait_then_round_over;

        case 411:
        case 421:
        case 431:
        case 511:
        case 523:
        wait_then_round_over:
            /* wait for the announcement, then the round is over */
            if (gpkgtCurrentEngineObject->iPlayerIdx-- > 0)
                return;
            gpkgtCurrentEngineObject->iProcessStep = 900;
            return;

        case 520:   /* 1P wins: "K.O.", "1P wins", "perfect" */
            gpkgtCurrentEngineObject->iProcessStep++;
            gpkgtCurrentEngineObject->iPlayerIdx = kgtSpawnNewEngineObjectReturnSkillField0x1((unsigned short)gkgtKgtSystem.shSkillIdxKO, 0x65, 0, 0);
            gkgtLoadedCharacter[0].iRoundResult = 1;
            gkgtLoadedCharacter[1].iRoundResult = gkgtGameState.iGameTimerInFrames ? 2 : 3;
            gkgtLoadedCharacter[0].iWins++;
            /* fall through */
        case 521:
            if (gpkgtCurrentEngineObject->iPlayerIdx-- > 0)
                return;
            gpkgtCurrentEngineObject->iProcessStep++;
            gpkgtCurrentEngineObject->iPlayerIdx = kgtSpawnNewEngineObjectReturnSkillField0x1((unsigned short)gkgtKgtSystem.shSkillIdx1pWins, 0x65, 0, 0);
            /* fall through */
        case 522:
            if (gpkgtCurrentEngineObject->iPlayerIdx-- > 0)
                return;
            /* "perfect" if the winner has full life */
            if (gkgtLoadedCharacter[0].iHealth != gkgtLoadedCharacter[0].iLifeMax) {
                gpkgtCurrentEngineObject->iProcessStep = 900;
                return;
            }
            goto perfect2;      /* matching: jumps to a second copy of the "perfect" code so that VC6
                                   merges the copies instead of the exits (vc6-matching-notes/matching-techniques.md, tail merging) */

        case 530:   /* 2P wins */
            gpkgtCurrentEngineObject->iProcessStep++;
            gpkgtCurrentEngineObject->iPlayerIdx = kgtSpawnNewEngineObjectReturnSkillField0x1((unsigned short)gkgtKgtSystem.shSkillIdxKO, 0x65, 0, 0);
            gkgtLoadedCharacter[1].iRoundResult = 1;
            gkgtLoadedCharacter[0].iRoundResult = gkgtGameState.iGameTimerInFrames ? 2 : 3;
            gkgtLoadedCharacter[1].iWins++;
            /* fall through */
        case 531:
            if (gpkgtCurrentEngineObject->iPlayerIdx-- > 0)
                return;
            gpkgtCurrentEngineObject->iProcessStep++;
            gpkgtCurrentEngineObject->iPlayerIdx = kgtSpawnNewEngineObjectReturnSkillField0x1((unsigned short)gkgtKgtSystem.shSkillIdx2pWins, 0x65, 0, 0);
            /* fall through */
        case 532:
            if (gpkgtCurrentEngineObject->iPlayerIdx-- > 0)
                return;
            if (gkgtLoadedCharacter[1].iHealth != gkgtLoadedCharacter[1].iLifeMax)
                gpkgtCurrentEngineObject->iProcessStep = 900;
            else {
        perfect:       /* not jumped to; the first copy of the "perfect" code */
                gpkgtCurrentEngineObject->iProcessStep++;
                gpkgtCurrentEngineObject->iPlayerIdx = kgtSpawnNewEngineObjectReturnSkillField0x1((unsigned short)gkgtKgtSystem.shSkillIdxPerfect, 0x65, 0, 0);
            }
            return;

        perfect2:      /* matching: second copy of the "perfect" code, for 1P (step 522 -> 523) */
            gpkgtCurrentEngineObject->iProcessStep++;
            gpkgtCurrentEngineObject->iPlayerIdx = kgtSpawnNewEngineObjectReturnSkillField0x1((unsigned short)gkgtKgtSystem.shSkillIdxPerfect, 0x65, 0, 0);
            return;

        case 533:
            if (gpkgtCurrentEngineObject->iPlayerIdx-- > 0)
                return;
            gpkgtCurrentEngineObject->iProcessStep = 900;
            return;
        case 540:   /* double K.O.: both lose by KO but get a win */
            gpkgtCurrentEngineObject->iProcessStep++;
            gpkgtCurrentEngineObject->iPlayerIdx = kgtSpawnNewEngineObjectReturnSkillField0x1((unsigned short)gkgtKgtSystem.shSkillIdxDoubleKO, 0x65, 0, 0);
            gkgtLoadedCharacter[0].iWins++;
            gkgtLoadedCharacter[0].iRoundResult = 2;
            gkgtLoadedCharacter[1].iRoundResult = 2;
            gkgtLoadedCharacter[1].iWins++;
            /* fall through */
        case 541:
            if (gpkgtCurrentEngineObject->iPlayerIdx-- > 0)
                return;
            gpkgtCurrentEngineObject->iProcessStep = 900;
            return;

        case 900:   /* round over: round animation end, then decide what comes next */
            gpkgtCurrentEngineObject->iProcessStep++;
            gpkgtCurrentEngineObject->iPlayerIdx = kgtSpawnNewEngineObjectReturnSkillField0x1((unsigned short)gkgtKgtSystem.shSkillIdxRoundAniEndTime, 0x65, 0, 0);
            /* fall through */
        case 901:
            if (gpkgtCurrentEngineObject->iPlayerIdx-- > 0)
                return;
            gpkgtCurrentEngineObject->iProcessStep++;
            /* fall through */
        case 902:
            /* nobody has the wins needed yet: next round (team battle: next member) */
            if (gkgtLoadedCharacter[0].iWins < gkgtGameState.iRoundsAmount
                && gkgtLoadedCharacter[1].iWins < gkgtGameState.iRoundsAmount) {
                switch (gkgtGameState.kgtGameMode) {
                case GAME_MODE_VS_TEAM:
                    /* the loser's next member comes in; on a draw (equal life, not 1P alive and
                       2P down) both */
                    if ((gkgtLoadedCharacter[0].iLifePermille != 0 || gkgtLoadedCharacter[1].iLifePermille == 0) && gkgtLoadedCharacter[0].iLifePermille == gkgtLoadedCharacter[1].iLifePermille) {
                        gkgtGameState.aiTeamMember[0]++;
                        gkgtGameState.aiTeamMember[1]++;
                        gkgtGameState.iCarryOverPlayer = -1;
                    } else {
                        if (gkgtLoadedCharacter[0].iLifePermille > gkgtLoadedCharacter[1].iLifePermille) {
                            gkgtGameState.aiTeamMember[1]++;
                            gkgtGameState.iCarryOverPlayer = 0;
                        } else {
                            gkgtGameState.aiTeamMember[0]++;
                            gkgtGameState.iCarryOverPlayer = 1;
                        }
                        /* the winner keeps its life and gauges */
                        gkgtGameState.dwTempHealth = gkgtLoadedCharacter[gkgtGameState.iCarryOverPlayer].iHealth;
                        gkgtGameState.dwTempSpecialGaugeTokens = gkgtLoadedCharacter[gkgtGameState.iCarryOverPlayer].iSpecialGaugeTokens;
                        gkgtGameState.iTempSpecialGauge = gkgtLoadedCharacter[gkgtGameState.iCarryOverPlayer].iSpecialGauge;
                    }
                    /* a team has no members left: back to character select */
                    if (gkgtGameState.aiTeamMember[0] > gkgtGameState.iConfigNumberOfRoundsTeamVs
                        || gkgtGameState.aiTeamMember[1] > gkgtGameState.iConfigNumberOfRoundsTeamVs)
                        goto back_to_select;
                    break;
                }
                gpkgtCurrentEngineObject->iPlayerIdx = 0;
                gpkgtCurrentEngineObject->iProcessStep = 100;
                vResetObjectsAndSpeed();
                return;
            }
            /* the match is over */
            switch (gkgtGameState.kgtGameMode) {
            case GAME_MODE_STORY:
                /* story: lost a fight marked "game over" (0x01): continue screen; otherwise next
                   story step */
                if (gkgtLoadedCharacter[0].iWins <= gkgtLoadedCharacter[1].iWins
                    && (gkgtLoadedCharacter[0].kgtStoryEntries[giCurrentStoryStep[giStoryModeSide]].cWhenDefeatAndFirstRoundBitmask & 1)) {
                    kgtoNewEngineObject(GAME_OVER_SCREEN, 0x7f, 0, 0);
                    return;
                }
                vProgressStoryMode();
                gpkgtCurrentEngineObject->iJumpIdx = RESET_IDX;
                return;
            case GAME_MODE_VS_SINGLE:
            case GAME_MODE_VS_TEAM:
            back_to_select:
                kgtoNewEngineObject(CHARACTER_SELECT_SCREEN, 0x7f, 0, 0);
                return;
            }
            return;
        }
    } else {
        /* paused: A+B+C quits to the menu, start resumes */
        if (giAnyInput == 0x70) {
            gkgtGameState.bPaused = 0;
            vResetObjectsAndSpeed();
            gpkgtCurrentEngineObject->iJumpIdx = RESET_IDX;
            kgtoNewEngineObject(MENU_TRAVERSAL, 0x7f, 0, 0);
            return;
        }
        /* start: end the pause skill objects and resume */
        if (giAnyInputXor & 0x400) {
            vResetAllObjectsWithSkillIdx((unsigned short)gkgtKgtSystem.shSkillIdxPause);
            gkgtGameState.bPaused = 0;
        }
    }
}

/*
 * Part B of engine.c (0x409a60-0x40e4a0; it was a separate engine_b.c while the functions were being
 * matched, and is still laid out as one): the remaining object handlers (game start, battle interface,
 * timer, hit counter, fades, title screens, camera/background control) and the 16-bit software renderer
 * (image blitting, rectangles, digits and text, KGT images, hit boxes, object drawing).
 * Screen coordinates are pixels of the 640x480 frame buffer; object positions are 16.16 fixed point.
 */


/* ------------------------------------------------------------------------------------------ */
/* declarations not (yet) in globals.h / protos.h                                              */
/* ------------------------------------------------------------------------------------------ */

extern kgtEngineObject gkgtEngineObjects[1024];    /* 0x4701e0: all engine objects */
extern kgtEngineObject *gpkgtCurrentEngineObject;  /* 0x4cfa00: object whose handler is running */
extern kgtGameState gkgtGameState;                 /* 0x470020: state of the current game */
extern void (*gpfnGamestateJumptable[18])(void);   /* 0x41ed58: handler per kgtJumptableEndpoints value */
extern unsigned int guHitboxColors[4];             /* 0x41eda0: hit box colours (RGB555): [0] attack boxes, [1] guard boxes */
extern int giStartGameUnusedA;                     /* 0x424708: cleared by vjmpStartGame, otherwise unused */
extern int giStartGameUnusedB;                     /* 0x447f28: cleared by vjmpStartGame, otherwise unused */
extern kgtBMPINFO gkgtBitmaps[128];                /* 0x424f60: external bitmaps: [1] text.bmp (fonts, digits), others by kgtEngineObject.iDrawFlag */
extern DWORD giAnyInputXor;                        /* 0x4280d8: giLastInputXor of all players ORed */
extern int giCameraX;                              /* 0x447f2c: camera x (pixels) */
extern int giCameraY;                              /* 0x447f30: camera y (pixels) */
extern int giStoryModeSide;                        /* 0x424f24: copy of giStoryModePlayerIdx taken when the story character is chosen; indexes giCurrentStoryStep */
extern int giCurrentStoryStep[2];                  /* 0x424f28: story entry per side */
extern char gszConfigReturnedFilename[];           /* 0x43012c: system file name (ini File/Filename) */
extern int giConfigTestplayPlayer0Nb;              /* 0x4300e0: test play: character of player 0 */
extern int giConfigTestplayPlayer0Cpu;             /* 0x4300e4: test play: player 0 CPU mode */
extern int giConfigTestplayPlayer1Nb;              /* 0x4300f0: test play: character of player 1 */
extern int giConfigTestplayPlayer1Cpu;             /* 0x4300f4: test play: player 1 CPU mode */
extern int giConfigTestplayStageNb;                /* 0x43010c: test play: stage */
extern void *gpFrameBits;                          /* 0x4246cc: pixels of the frame buffer: 640x480, 16 bit (RGB555 in a window, RGB565 full screen) */
extern int giScreenMode;                           /* 0x424704: display mode in use: 0 window (RGB555 DIB), 1 full screen (RGB565 surface) */
extern int giHitJudge;                             /* 0x42470c: hit-judge display (hit boxes and player state), copy of giConfigTestplayHitjudge; the next int (0x424710) enables the line switch button */
extern void *gpGlobalMemoryAlloc;                  /* 0x425a44: decompression buffer */
extern WORD gwTintedPalette16[256];                /* 0x4d1a20: palette of the image being drawn, tinted, in the screen pixel format */
extern int giShakeXOffset;                         /* 0x447dad: current x offset (pixels) */
extern int giShakeYOffset;                         /* 0x447dc1: current y offset (pixels) */
extern int giReverseShakeDirection;                /* 0x4456fc: frame counter; its low bit flips the shake direction */
extern int giSystemFlashType;                      /* 0x4456d0: colour effect of system objects (layout of kgt_character_struct.flash): 1 smooth, 2 blinking, 3 random */
extern int giStageFlashType;                       /* 0x447d7d: colour effect of stage objects (layout of kgt_character_struct.flash), kgt_stage + 0x263d */
extern char gszEmptyBgFile[4];                     /* 0x424784: engine.c: iLoadExternalImage(..., "bg_001_0.bmp", "", 0) */

/* A skill-script step (kgtSkill, 16 bytes) viewed as an image step (command 0x0C). */
#pragma pack(push, 1)
typedef struct kgtSkillImageStep {
    char cType;                 /* 0x00: command (0x0C) */
    BYTE pad_01[2];             /* 0x01: frames to show (read through kgtSkill) */
    WORD wImage;                /* 0x03: image index & 0x1fff; 0x4000 flip x, 0x8000 flip y */
    short shX;                  /* 0x05: x offset from the object (pixels) */
    short shY;                  /* 0x07: y offset (pixels) */
    BYTE cFlags;                /* 0x09: bit 0 = do not turn with the object (drawn unflipped when it
                                   looks right) */
    BYTE pad_0a[6];             /* 0x0a: rest of the step */
} kgtSkillImageStep;

/* A skill-script step viewed as stage scroll parameters (stage objects keep its index in
   iDsGuardedSkillIdx). */
typedef struct kgtSkillStageScroll {
    char cType;                 /* 0x00: command */
    BYTE cFlags;                /* 0x01: 2 tile x, 4 tile y, 8 scroll x, 0x10 scroll y */
    short shScrollX;            /* 0x02: percent of the camera movement */
    short shScrollY;            /* 0x04: percent of the camera movement (y) */
    BYTE pad_06[10];            /* 0x06: rest of the step */
} kgtSkillStageScroll;

/* One captured frame of an after-image trail, drawn behind an object (kgtEngineObject.cAfterImageIdx,
   1-based); the same memory as unk_0x650_struct.aiData[(n + 1) * 4 + 0..3]. */
typedef struct kgtStageLayer {
    int iX;                     /* 0x00: x (16.16) */
    int iY;                     /* 0x04: y (16.16) */
    int iFlags;                 /* 0x08: & 3 flip, 4 mirrored */
    kgtSkillImageStep *pStep;   /* 0x0c: the image step shown */
} kgtStageLayer;

/* engine.c's view of an after-image trail (unk_0x650_struct). */
typedef struct kgtStageLayerSet {
    int bInUse;                 /* 0x00: trail in use */
    int iNewest;                /* 0x04: next frame slot (0..99) */
    BYTE *pInfo;                /* 0x08: the AI script step: [3] frames shown, [4] capture interval,
                                   [5] blend type, [6] colour mode, [7..10] r g b alpha */
    int iPhase;                 /* 0x0c: frames until the next capture */
    kgtStageLayer kgtLayers[100];   /* 0x10: the captured frames (ring buffer) */
} kgtStageLayerSet;
#pragma pack(pop)

extern kgtStageLayerSet gAfterImageLayers[];  /* 0x447f80: engine.c's view of gAfterImageTrails (drawing) */

int iLoadExternalImage(kgtBMPINFO *pInfo, LPCSTR szResource, LPCSTR szFile, int iUnused);   /* main_b.c */
int iKgtDecompress(BYTE *pDst, BYTE *pSrc, int iSrcLen);                                       /* compress.c */
kgtEngineObject *kgtoNewEngineObject(int iJumpIdx, int iDepth, int iPosX, int iPosY);        /* engine.c */
kgtEngineObject *kgtoNewObjectForSkillIdx(int iSkillIdx, int iDepth, int iPosX, int iPosY);   /* engine.c */
void vResetObjectsAndSpeed(void);                                                           /* engine.c */

/* the first script step of one of the system file's skills (n = skill number); the UI position skills
   keep x in shParam1 and y in shParam2 (pixels), and a spacing in cParam3/cParam4 */
#define SYS_STEP(n) gkgtKgtSystem.kgtCore.pSkillScriptsAlloc[(WORD)gkgtKgtSystem.kgtCore.pSkillsAlloc[n].shStartingStepIdx]

/* several engine object fields (iWork0166 ..., iPlayerIdx) hold pointers to other objects as ints */
#define OBJ(p) ((kgtEngineObject *)(p))

/* ------------------------------------------------------------------------------------------ */
/* object handlers                                                                             */
/* ------------------------------------------------------------------------------------------ */

/*
 * vjmpStartGame - handler of the START_GAME object (created by vEmptyEngineObjects): opens the game's
 * system file - <exe name>.kgt, or in test play (and when the exe itself is named KGT*) the file the
 * editor wrote to the ini (gszConfigReturnedFilename), from its directory - and creates the first
 * game-flow object: the title screens (normal game), the title menu (giAppmode 1) or directly a versus
 * battle with the ini's test play characters (giAppmode 3).  Runs once (step 0), then ends itself.
 * Quits the program (PostQuitMessage) when the system file can't be opened.
 * Globals read: giAppmode, gszConfigReturnedFilename, giConfigTestplay* settings.
 * Globals changed: gkgtGameState, gkgtBitmaps[1] (text.bmp), gkgtLoadedCharacter[0/1] (CPU settings,
 * colours), giStartGameUnusedA/B, the current directory.
 */
void vjmpStartGame(void)
{
    int i, j;           /* character indexes into the names being copied */
    int bFound;
    char *pCmdLine;
    char cChar;
    int iPlayer0, iPlayer1;
    char szName[256];
    char szCmdLine[256];

    if (gpkgtCurrentEngineObject->iProcessStep != 0)
        return;
    gpkgtCurrentEngineObject->iProcessStep = 1;
    for (i = 0; i < 8; i++)
        gkgtGameState.iCharSelect[i] = -1;
    giStartGameUnusedA = 0;
    giStartGameUnusedB = 1;
    /* the font/digit bitmap (resource, or text.bmp next to the game) */
    iLoadExternalImage(&gkgtBitmaps[1], "RC_BMP_text", "text.bmp", 0);
    if (giAppmode == 0) {
        /* "<dir>\NAME.exe ..." -> "NAME": copy up to the first '.', restarting after every '\' */
        i = 0;
        sprintf(szCmdLine, "%s", GetCommandLineA());
        pCmdLine = szCmdLine;
        while (1) {
            cChar = *pCmdLine;
            szName[i] = cChar;
            if (cChar == '.')
                break;
            if (cChar == '\\')
                i = -1;
            i++;
            pCmdLine++;
        }
        szName[i] = '\0';
        if ((szName[0] == 'K' || szName[0] == 'k') && (szName[1] == 'G' || szName[1] == 'g')
            && (szName[2] == 'T' || szName[2] == 't'))
            goto test_play;         /* the KGT player exe: use the editor's file */
        sprintf(szName, "%s.kgt", szName);
        if (iOpenKgtSystemFile(szName) != 0) {
            PostQuitMessage(0);
            return;
        }
    } else {
test_play:
        /* directory of the file handed over by the editor: copy the path backwards and cut it after
           its last '\' */
        bFound = 0;
        for (i = 254; i >= 0; i--) {
            szName[i] = gszConfigReturnedFilename[i];
            if (!bFound && szName[i] == '\\') {
                bFound = 1;
                szName[i + 1] = '\0';
            }
        }
        SetCurrentDirectoryA(szName);
        /* then its file name without the directory */
        j = 0;
        for (i = 0; i < 256; i++, j++) {
            cChar = gszConfigReturnedFilename[i];
            szName[j] = cChar;
            if (cChar == '\0')
                break;
            if (cChar == '\\')
                j = -1;
        }
        if (iOpenKgtSystemFile(szName) != 0) {
            PostQuitMessage(0);
            return;
        }
    }
    /* test play defaults: player 0's character, all other slots player 1's, the ini stage */
    iPlayer0 = giConfigTestplayPlayer0Nb;
    iPlayer1 = giConfigTestplayPlayer1Nb;
    gkgtGameState.iCharSelect[0] = iPlayer0;
    for (i = 1; i < 8; i++)
        gkgtGameState.iCharSelect[i] = iPlayer1;
    gkgtGameState.iConfigTestplayStageNb = giConfigTestplayStageNb;
    /* first game-flow object (giAppmode 2 and 4 create none) */
    switch (giAppmode) {
    case 0:
        kgtoNewEngineObject(DISPLAY_TITLE_SCREEN, 127, 0, 0);
        break;
    case 1:
        kgtoNewEngineObject(MENU_TRAVERSAL, 127, 0, 0);
        break;
    case 3:
        /* test play battle: versus single with the ini's characters; a CPU mode from the ini makes
           that side a level 100 CPU */
        for (i = 0; i < 8; i++) {
            gkgtGameState.iCharSelect[i] = -1;
            gkgtLoadedCharacter[i].iColor = -1;
        }
        gkgtGameState.kgtGameMode = GAME_MODE_VS_SINGLE;
        gkgtGameState.iCharSelect[0] = iPlayer0;
        gkgtGameState.iCharSelect[1] = iPlayer1;
        gkgtLoadedCharacter[0].bCpuControlled = 0;
        gkgtLoadedCharacter[1].bCpuControlled = 0;
        if (giConfigTestplayPlayer0Cpu) {
            gkgtLoadedCharacter[0].bCpuControlled = 1;
            gkgtLoadedCharacter[0].iCpuLevel = 100;
            gkgtLoadedCharacter[0].iCpuMode = giConfigTestplayPlayer0Cpu;
        }
        if (giConfigTestplayPlayer1Cpu) {
            gkgtLoadedCharacter[1].bCpuControlled = 1;
            gkgtLoadedCharacter[1].iCpuLevel = 100;
            gkgtLoadedCharacter[1].iCpuMode = giConfigTestplayPlayer1Cpu;
        }
        gkgtLoadedCharacter[0].iColor = 0;
        gkgtLoadedCharacter[1].iColor = 1;
        gkgtGameState.bPaused = 0;
        kgtoNewEngineObject(BATTLE_STATE, 127, 0, 0);
        break;
    }
    gpkgtCurrentEngineObject->iJumpIdx = RESET_IDX;
}

/*
 * vjmpHandleBattleInterface - handler of the BATTLE_UI object (created at every round start by
 * vjmpHandleBattleState): builds and updates the battle interface.
 *
 * Step 0 creates the UPDATE_TIMER object, the victory marks of both sides (one per round needed,
 * "won" for the wins so far), the face pictures, the life and special gauges (script objects whose
 * iPlayerIdx 10/11 = 1P/2P life, 20/21 = 1P/2P special gauge tells the script code what to show) and
 * the two special stock digits.  Every frame it then:
 *   - story mode: keeps the opponent face on the target player (gkgtGameState.iTargetPlayer, the one
 *     attacked last, or the only CPU still standing; -1 after dwTargetPlayerTimer runs out) and shows
 *     that player's stock count;
 *   - updates the stock digits when a stock count changes;
 *   - switches the first "not won" victory mark of a side to "won" when its wins change.
 * Work slots: iPlayerIdx / iObjectType = 1P's next victory mark object / wins shown, pWork015E /
 * pWork0162 = the same for 2P, iWork0166 / iWork016A = 1P stock digit object / count shown, iWork016E /
 * iWork0172 = 2P (story: target's) stock digit object / count shown, iWork0176 = story opponent face.
 * UI positions come from the first steps of fixed system skills (SYS_STEP): 0x48 / 0x49 1P / 2P face,
 * 0x4a / 0x4b 1P / 2P stock digit, 0x4c / 0x4d 1P / 2P victory marks (x, y and spacing).
 * Globals read: gkgtKgtSystem, gkgtLoadedCharacter (wins, health, stocks, face skills).
 * Globals changed: gkgtGameState.dwTargetPlayerTimer / iTargetPlayer, gkgtLoadedCharacter[].iLifeMax /
 * iSpecialMax (0 -> 1), gkgtEngineObjects.
 *
 * Built from the original's machine code: VC6 does not yet compile the C to exactly these bytes.
 * The dead `if (0)` copy of the C body keeps the file's string literals and imported symbols in
 * the original order; the plain C version is on the `nonmatching` branch.
 * Still different: 2304 vs 2336 bytes. The face-picture calls pass iX/iY locals (the original evaluates x before
 * y).  Left: the constant-1 register (orig edi, ours ebp) - which then leaves the original a free ebp for
 * a constant-0 register in the VS case (cmp eax,ebp / mov [..],ebp), so that case's 2P stock block is
 * not cross-jumped into the story one as ours is (-> the goto set_script); wIdx is kept in di by the
 * original but spilled by us; the -1 block reloads iWork016E in the original. Case order, variable
 * choice, declaration order and the form of the first if have no effect.
 */
/* the original machine code; the C in the dead block keeps the literals and imports */
__declspec(naked) void vjmpHandleBattleInterface(void)
{
    if (0) {
        kgtEngineObject *pObj;
        int i;
        int iX, iY;
        int iDx, iDy;
        int iDx2, iDy2;
        int bMarked;
        int iAlive, iLastAlive;
        WORD wIdx;
    
        if (gkgtGameState.dwTargetPlayerTimer != 0 && --gkgtGameState.dwTargetPlayerTimer == 0)
            gkgtGameState.iTargetPlayer = -1;
    
        switch (gpkgtCurrentEngineObject->iProcessStep) {
        case 0:
            gpkgtCurrentEngineObject->iProcessStep = 1;
            for (i = 0; i < 8; i++) {
                if (gkgtLoadedCharacter[i].iLifeMax == 0)
                    gkgtLoadedCharacter[i].iLifeMax = 1;
                if (gkgtLoadedCharacter[i].iSpecialMax == 0)
                    gkgtLoadedCharacter[i].iSpecialMax = 1;
            }
            kgtoNewEngineObject(UPDATE_TIMER, 1, 0, 0);
    
            /* victory marks 1P */
            bMarked = 0;
            iX = SYS_STEP(0x4c).shParam1 << 16;
            iY = SYS_STEP(0x4c).shParam2 << 16;
            iDx = SYS_STEP(0x4c).cParam3 << 16;
            iDy = SYS_STEP(0x4c).cParam4 << 16;
            for (i = 0; i < (int)gkgtGameState.iRoundsAmount; i++) {
                pObj = kgtoNewEngineObject(READ_SCRIPT, 0x65, iX, iY);
                pObj->iObjectType = SYSTEM_ENGINE_OBJECT;
                if (gkgtLoadedCharacter[0].iWins > i) {
                    pObj->iSkillIdx = pObj->iStartSkillIdx = (WORD)gkgtKgtSystem.shSkillIdxVictoryMarkOn;
                    { int *pSkillIdx = &pObj->iSkillIdx; pObj->iSkillScriptIdx = (WORD)gkgtKgtSystem.kgtCore.pSkillsAlloc[*pSkillIdx].shStartingStepIdx; }
                } else {
                    pObj->iSkillIdx = pObj->iStartSkillIdx = (WORD)gkgtKgtSystem.shSkillIdxVictoryMarkOff;
                    if (!bMarked) {
                        bMarked = 1;
                        gpkgtCurrentEngineObject->iPlayerIdx = (int)pObj;
                        gpkgtCurrentEngineObject->iObjectType = gkgtLoadedCharacter[0].iWins;
                    }
                    { int *pSkillIdx = &pObj->iSkillIdx; pObj->iSkillScriptIdx = (WORD)gkgtKgtSystem.kgtCore.pSkillsAlloc[*pSkillIdx].shStartingStepIdx; }
                }
                iX += iDx;
                iY += iDy;
            }
    
            /* victory marks 2P */
            bMarked = 0;
            iX = SYS_STEP(0x4d).shParam1 << 16;
            iY = SYS_STEP(0x4d).shParam2 << 16;
            iDx2 = SYS_STEP(0x4d).cParam3 << 16;
            iDy2 = SYS_STEP(0x4d).cParam4 << 16;
            for (i = 0; i < (int)gkgtGameState.iRoundsAmount; i++) {
                pObj = kgtoNewEngineObject(READ_SCRIPT, 0x65, iX, iY);
                pObj->iObjectType = SYSTEM_ENGINE_OBJECT;
                if (gkgtLoadedCharacter[1].iWins > i) {
                    pObj->iSkillIdx = pObj->iStartSkillIdx = (WORD)gkgtKgtSystem.shSkillIdxVictoryMarkOn;
                } else {
                    pObj->iSkillIdx = pObj->iStartSkillIdx = (WORD)gkgtKgtSystem.shSkillIdxVictoryMarkOff;
                    if (!bMarked) {
                        bMarked = 1;
                        gpkgtCurrentEngineObject->pWork015E = pObj;
                        gpkgtCurrentEngineObject->pWork0162 = (kgtEngineObject *)gkgtLoadedCharacter[1].iWins;
                    }
                }
                pObj->iSkillScriptIdx = (WORD)gkgtKgtSystem.kgtCore.pSkillsAlloc[pObj->iSkillIdx].shStartingStepIdx;
                iX += iDx2;
                iY += iDy2;
            }
    
            /* face pictures */
            switch (gkgtGameState.kgtGameMode) {
            case GAME_MODE_STORY:
                if ((WORD)gkgtLoadedCharacter[0].kgtCore.pSkillsAlloc[(WORD)gkgtLoadedCharacter[0].shSkillIdxStageFacePic].shStartingStepIdx + 1
                    < (WORD)gkgtLoadedCharacter[0].kgtCore.pSkillsAlloc[(WORD)gkgtLoadedCharacter[0].shSkillIdxR1].shStartingStepIdx) {
                    iX = SYS_STEP(0x48).shParam1 << 16;
                    iY = SYS_STEP(0x48).shParam2 << 16;
                    pObj = kgtoNewEngineObject(READ_SCRIPT, 0x65, iX, iY);
                    pObj->iSkillIdx = pObj->iStartSkillIdx = (WORD)gkgtLoadedCharacter[0].shSkillIdxStageFacePic;
                    pObj->iPlayerIdx = 0;
                    pObj->iObjectType = STORY_ENGINE_OBJECT;
                    pObj->iSkillScriptIdx = (WORD)gkgtLoadedCharacter[0].kgtCore.pSkillsAlloc[pObj->iSkillIdx].shStartingStepIdx;
                }
                break;
            case GAME_MODE_VS_SINGLE:
            case GAME_MODE_VS_TEAM:
                iX = SYS_STEP(0x48).shParam1 << 16;
                iY = SYS_STEP(0x48).shParam2 << 16;
                pObj = kgtoNewEngineObject(READ_SCRIPT, 0x65, iX, iY);
                pObj->iSkillIdx = pObj->iStartSkillIdx = (WORD)gkgtLoadedCharacter[0].shSkillIdxStageFacePic;
                pObj->iPlayerIdx = 0;
                pObj->iObjectType = STORY_ENGINE_OBJECT;
                pObj->iSkillScriptIdx = (WORD)gkgtLoadedCharacter[0].kgtCore.pSkillsAlloc[pObj->iSkillIdx].shStartingStepIdx;
    
                iX = SYS_STEP(0x49).shParam1 << 16;
                iY = SYS_STEP(0x49).shParam2 << 16;
                pObj = kgtoNewEngineObject(READ_SCRIPT, 0x65, iX, iY);
                pObj->iSkillIdx = pObj->iStartSkillIdx = (WORD)gkgtLoadedCharacter[1].shSkillIdxStageFacePic;
                pObj->iPlayerIdx = 1;
                pObj->iObjectType = STORY_ENGINE_OBJECT;
                pObj->iPlayerLookingRight = 1;
                pObj->iSkillScriptIdx = (WORD)gkgtLoadedCharacter[1].kgtCore.pSkillsAlloc[pObj->iSkillIdx].shStartingStepIdx;
                break;
            }
    
            /* life and special gauges */
            pObj = kgtoNewEngineObject(READ_SCRIPT, 0x65, 0, 0);
            pObj->iSkillIdx = pObj->iStartSkillIdx = (WORD)gkgtKgtSystem.shSkillIdx1pLifeGauge;
            pObj->iObjectType = SYSTEM_ENGINE_OBJECT;
            pObj->iSkillScriptIdx = (WORD)gkgtKgtSystem.kgtCore.pSkillsAlloc[pObj->iSkillIdx].shStartingStepIdx;
            pObj->iPlayerIdx = 10;
            pObj = kgtoNewEngineObject(READ_SCRIPT, 0x65, 0, 0);
            pObj->iSkillIdx = pObj->iStartSkillIdx = (WORD)gkgtKgtSystem.shSkillIdx2pLifeGauge;
            pObj->iObjectType = SYSTEM_ENGINE_OBJECT;
            pObj->iSkillScriptIdx = (WORD)gkgtKgtSystem.kgtCore.pSkillsAlloc[pObj->iSkillIdx].shStartingStepIdx;
            pObj->iPlayerIdx = 11;
            pObj = kgtoNewEngineObject(READ_SCRIPT, 0x65, 0, 0);
            pObj->iSkillIdx = pObj->iStartSkillIdx = (WORD)gkgtKgtSystem.shSkillIdx1pSpecialGauge;
            pObj->iObjectType = SYSTEM_ENGINE_OBJECT;
            pObj->iSkillScriptIdx = (WORD)gkgtKgtSystem.kgtCore.pSkillsAlloc[pObj->iSkillIdx].shStartingStepIdx;
            pObj->iPlayerIdx = 20;
            pObj = kgtoNewEngineObject(READ_SCRIPT, 0x65, 0, 0);
            pObj->iObjectType = SYSTEM_ENGINE_OBJECT;
            pObj->iSkillIdx = pObj->iStartSkillIdx = (WORD)gkgtKgtSystem.shSkillIdx2pSpecialGauge;
            pObj->iPlayerIdx = 21;
            pObj->iSkillScriptIdx = (WORD)gkgtKgtSystem.kgtCore.pSkillsAlloc[pObj->iSkillIdx].shStartingStepIdx;
    
            /* special stock numbers */
            pObj = kgtoNewEngineObject(READ_SCRIPT, 0x65, SYS_STEP(0x4a).shParam1 << 16, SYS_STEP(0x4a).shParam2 << 16);
            pObj->iObjectType = SYSTEM_ENGINE_OBJECT;
            gpkgtCurrentEngineObject->iWork0166 = (int)pObj;
            pObj = kgtoNewEngineObject(READ_SCRIPT, 0x65, SYS_STEP(0x4b).shParam1 << 16, SYS_STEP(0x4b).shParam2 << 16);
            pObj->iObjectType = SYSTEM_ENGINE_OBJECT;
            gpkgtCurrentEngineObject->iWork016E = (int)pObj;
            gpkgtCurrentEngineObject->iWork0172 = -1;
            gpkgtCurrentEngineObject->iWork016A = -1;
            break;
        case 1:
            break;
        default:
            ;
        }
    
        switch (gkgtGameState.kgtGameMode) {
        case GAME_MODE_STORY:
            /* the opponent face follows whoever was attacked last (or the only one left) */
            iAlive = 0;
            for (i = 1; i < 8; i++) {
                if (gkgtLoadedCharacter[i].iHealth != 0) {
                    iAlive++;
                    iLastAlive = i;
                }
            }
            if (iAlive == 1)
                gkgtGameState.iTargetPlayer = iLastAlive;
            if (gpkgtCurrentEngineObject->iWork0176 != 0 && *(int *)gpkgtCurrentEngineObject->iWork0176 == 1)
                gpkgtCurrentEngineObject->iWork0176 = 0;
            if (gkgtGameState.iTargetPlayer == -1) {
                if (gpkgtCurrentEngineObject->iWork0176 != 0) {
                    *(int *)gpkgtCurrentEngineObject->iWork0176 = 1;
                    gpkgtCurrentEngineObject->iWork0176 = 0;
                }
            } else {
                pObj = (kgtEngineObject *)gpkgtCurrentEngineObject->iWork0176;
                if (pObj != NULL) {
                    if (pObj->iPlayerIdx != gkgtGameState.iTargetPlayer) {
                        pObj->iPlayerIdx = gkgtGameState.iTargetPlayer;
                        pObj->iSkillIdx = pObj->iStartSkillIdx = (WORD)gkgtLoadedCharacter[gkgtGameState.iTargetPlayer].shSkillIdxStageFacePic;
                        pObj->iSkillScriptIdx = (WORD)gkgtLoadedCharacter[gkgtGameState.iTargetPlayer].kgtCore.pSkillsAlloc[pObj->iSkillIdx].shStartingStepIdx;
                        pObj->iDrawFlag = 0;
                    }
                } else {
                    iX = SYS_STEP(0x49).shParam1 << 16;
                    pObj = kgtoNewEngineObject(READ_SCRIPT, 0x65, iX, SYS_STEP(0x49).shParam2 << 16);
                                gpkgtCurrentEngineObject->iWork0176 = (int)pObj;
                    pObj->iObjectType = STORY_ENGINE_OBJECT;
                    pObj->iSkillIdx = pObj->iStartSkillIdx = (WORD)gkgtLoadedCharacter[gkgtGameState.iTargetPlayer].shSkillIdxStageFacePic;
                    pObj->iPlayerIdx = gkgtGameState.iTargetPlayer;
                    pObj->iPlayerLookingRight = 1;
                    pObj->iSkillScriptIdx = (WORD)gkgtLoadedCharacter[gkgtGameState.iTargetPlayer].kgtCore.pSkillsAlloc[pObj->iSkillIdx].shStartingStepIdx;
                    gpkgtCurrentEngineObject->iPosX = 1;
                }
            }
            i = 1;
            if (gpkgtCurrentEngineObject->iWork016A != gkgtLoadedCharacter[0].iSpecialGaugeTokens) {
                gpkgtCurrentEngineObject->iWork016A = gkgtLoadedCharacter[0].iSpecialGaugeTokens;
                OBJ(gpkgtCurrentEngineObject->iWork0166)->iSkillIdx = OBJ(gpkgtCurrentEngineObject->iWork0166)->iStartSkillIdx = (WORD)(&gkgtKgtSystem.shSkillIdxSpecialStockNumber0)[gkgtLoadedCharacter[0].iSpecialGaugeTokens];
                OBJ(gpkgtCurrentEngineObject->iWork0166)->iImageWaitFrames = 0;
                OBJ(gpkgtCurrentEngineObject->iWork0166)->iSkillScriptIdx = (WORD)gkgtKgtSystem.kgtCore.pSkillsAlloc[OBJ(gpkgtCurrentEngineObject->iWork0166)->iSkillIdx].shStartingStepIdx;
            }
            if (gkgtGameState.kgtGameMode == GAME_MODE_STORY) {
                if (gkgtGameState.iTargetPlayer == -1) {
                    ((kgtEngineObject *)gpkgtCurrentEngineObject->iWork016E)->iDrawFlag = 0;
                    gpkgtCurrentEngineObject->iWork0172 = -1;
                    ((kgtEngineObject *)gpkgtCurrentEngineObject->iWork016E)->iImageWaitFrames = -1;
                    goto victory_marks;
                }
                i = gkgtGameState.iTargetPlayer;
            }
            if (gpkgtCurrentEngineObject->iWork0172 != gkgtLoadedCharacter[i].iSpecialGaugeTokens) {
                gpkgtCurrentEngineObject->iWork0172 = gkgtLoadedCharacter[i].iSpecialGaugeTokens;
                OBJ(gpkgtCurrentEngineObject->iWork016E)->iSkillIdx = OBJ(gpkgtCurrentEngineObject->iWork016E)->iStartSkillIdx = (WORD)(&gkgtKgtSystem.shSkillIdxSpecialStockNumber0)[gkgtLoadedCharacter[i].iSpecialGaugeTokens];
                OBJ(gpkgtCurrentEngineObject->iWork016E)->iImageWaitFrames = 0;
    set_script:
                OBJ(gpkgtCurrentEngineObject->iWork016E)->iSkillScriptIdx = (WORD)gkgtKgtSystem.kgtCore.pSkillsAlloc[OBJ(gpkgtCurrentEngineObject->iWork016E)->iSkillIdx].shStartingStepIdx;
            }
            break;
        case GAME_MODE_VS_SINGLE:
        case GAME_MODE_VS_TEAM:
            if (gpkgtCurrentEngineObject->iWork016A != gkgtLoadedCharacter[0].iSpecialGaugeTokens) {
                gpkgtCurrentEngineObject->iWork016A = gkgtLoadedCharacter[0].iSpecialGaugeTokens;
                OBJ(gpkgtCurrentEngineObject->iWork0166)->iSkillIdx = OBJ(gpkgtCurrentEngineObject->iWork0166)->iStartSkillIdx = (WORD)(&gkgtKgtSystem.shSkillIdxSpecialStockNumber0)[gkgtLoadedCharacter[0].iSpecialGaugeTokens];
                OBJ(gpkgtCurrentEngineObject->iWork0166)->iImageWaitFrames = 0;
                OBJ(gpkgtCurrentEngineObject->iWork0166)->iSkillScriptIdx = (WORD)gkgtKgtSystem.kgtCore.pSkillsAlloc[OBJ(gpkgtCurrentEngineObject->iWork0166)->iSkillIdx].shStartingStepIdx;
            }
            if (gpkgtCurrentEngineObject->iWork0172 != gkgtLoadedCharacter[1].iSpecialGaugeTokens) {
                gpkgtCurrentEngineObject->iWork0172 = gkgtLoadedCharacter[1].iSpecialGaugeTokens;
                OBJ(gpkgtCurrentEngineObject->iWork016E)->iSkillIdx = OBJ(gpkgtCurrentEngineObject->iWork016E)->iStartSkillIdx = (WORD)(&gkgtKgtSystem.shSkillIdxSpecialStockNumber0)[gkgtLoadedCharacter[1].iSpecialGaugeTokens];
                OBJ(gpkgtCurrentEngineObject->iWork016E)->iImageWaitFrames = 0;
                goto set_script;
            }
            break;
        }
    
    victory_marks:
        if (gpkgtCurrentEngineObject->iObjectType != gkgtLoadedCharacter[0].iWins) {
            gpkgtCurrentEngineObject->iObjectType = gkgtLoadedCharacter[0].iWins;
            OBJ(gpkgtCurrentEngineObject->iPlayerIdx)->iSkillIdx = OBJ(gpkgtCurrentEngineObject->iPlayerIdx)->iStartSkillIdx = (WORD)gkgtKgtSystem.shSkillIdxVictoryMarkOn;
            OBJ(gpkgtCurrentEngineObject->iPlayerIdx)->iSkillScriptIdx = (WORD)gkgtKgtSystem.kgtCore.pSkillsAlloc[(WORD)gkgtKgtSystem.shSkillIdxVictoryMarkOn].shStartingStepIdx;
            OBJ(gpkgtCurrentEngineObject->iPlayerIdx)->iImageWaitFrames = 0;
        }
        if (gpkgtCurrentEngineObject->pWork0162 != (kgtEngineObject *)gkgtLoadedCharacter[1].iWins) {
            gpkgtCurrentEngineObject->pWork0162 = (kgtEngineObject *)gkgtLoadedCharacter[1].iWins;
            gpkgtCurrentEngineObject->pWork015E->iSkillIdx = gpkgtCurrentEngineObject->pWork015E->iStartSkillIdx = (WORD)gkgtKgtSystem.shSkillIdxVictoryMarkOn;
            gpkgtCurrentEngineObject->pWork015E->iSkillScriptIdx = (WORD)gkgtKgtSystem.kgtCore.pSkillsAlloc[(WORD)gkgtKgtSystem.shSkillIdxVictoryMarkOn].shStartingStepIdx;
            gpkgtCurrentEngineObject->pWork015E->iImageWaitFrames = 0;
        }
    }
    __asm {
        _emit 0xA1        ; 00409D00  mov eax, dword ptr [0x4701c8]
        _emit 0xC8
        _emit 0x01
        _emit 0x47
        _emit 0x00
        _emit 0x83        ; 00409D05  sub esp, 0xc
        _emit 0xEC
        _emit 0x0C
        _emit 0x85        ; 00409D08  test eax, eax
        _emit 0xC0
        _emit 0x53        ; 00409D0A  push ebx
        _emit 0x55        ; 00409D0B  push ebp
        _emit 0x56        ; 00409D0C  push esi
        _emit 0x57        ; 00409D0D  push edi
        _emit 0x74        ; 00409D0E  je 0x409d23
        _emit 0x13
        _emit 0x48        ; 00409D10  dec eax
        _emit 0xA3        ; 00409D11  mov dword ptr [0x4701c8], eax
        _emit 0xC8
        _emit 0x01
        _emit 0x47
        _emit 0x00
        _emit 0x75        ; 00409D16  jne 0x409d23
        _emit 0x0B
        _emit 0x83        ; 00409D18  or ebx, 0xffffffff
        _emit 0xCB
        _emit 0xFF
        _emit 0x89        ; 00409D1B  mov dword ptr [0x4701c4], ebx
        _emit 0x1D
        _emit 0xC4
        _emit 0x01
        _emit 0x47
        _emit 0x00
        _emit 0xEB        ; 00409D21  jmp 0x409d29
        _emit 0x06
        _emit 0x8B        ; 00409D23  mov ebx, dword ptr [0x4701c4]
        _emit 0x1D
        _emit 0xC4
        _emit 0x01
        _emit 0x47
        _emit 0x00
        _emit 0x8B        ; 00409D29  mov ecx, dword ptr [0x4cfa00]
        _emit 0x0D
        _emit 0x00
        _emit 0xFA
        _emit 0x4C
        _emit 0x00
        _emit 0x8B        ; 00409D2F  mov eax, dword ptr [ecx + 0x152]
        _emit 0x81
        _emit 0x52
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x83        ; 00409D35  sub eax, 0
        _emit 0xE8
        _emit 0x00
        _emit 0x74        ; 00409D38  je 0x409d49
        _emit 0x0F
        _emit 0x48        ; 00409D3A  dec eax
        _emit 0x0F        ; 00409D3B  je 0x40a2c7
        _emit 0x84
        _emit 0x86
        _emit 0x05
        _emit 0x00
        _emit 0x00
        _emit 0x5F        ; 00409D41  pop edi
        _emit 0x5E        ; 00409D42  pop esi
        _emit 0x5D        ; 00409D43  pop ebp
        _emit 0x5B        ; 00409D44  pop ebx
        _emit 0x83        ; 00409D45  add esp, 0xc
        _emit 0xC4
        _emit 0x0C
        _emit 0xC3        ; 00409D48  ret
        _emit 0xBA        ; 00409D49  mov edx, 1
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0xB8        ; 00409D4E  mov eax, 0x4dfca1
        _emit 0xA1
        _emit 0xFC
        _emit 0x4D
        _emit 0x00
        _emit 0x89        ; 00409D53  mov dword ptr [ecx + 0x152], edx
        _emit 0x91
        _emit 0x52
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 00409D59  mov ecx, dword ptr [eax - 0x10]
        _emit 0x48
        _emit 0xF0
        _emit 0x85        ; 00409D5C  test ecx, ecx
        _emit 0xC9
        _emit 0x75        ; 00409D5E  jne 0x409d63
        _emit 0x03
        _emit 0x89        ; 00409D60  mov dword ptr [eax - 0x10], edx
        _emit 0x50
        _emit 0xF0
        _emit 0x83        ; 00409D63  cmp dword ptr [eax], 0
        _emit 0x38
        _emit 0x00
        _emit 0x75        ; 00409D66  jne 0x409d6a
        _emit 0x02
        _emit 0x89        ; 00409D68  mov dword ptr [eax], edx
        _emit 0x10
        _emit 0x05        ; 00409D6A  add eax, 0xe03f
        _emit 0x3F
        _emit 0xE0
        _emit 0x00
        _emit 0x00
        _emit 0x3D        ; 00409D6F  cmp eax, 0x54fe99
        _emit 0x99
        _emit 0xFE
        _emit 0x54
        _emit 0x00
        _emit 0x7C        ; 00409D74  jl 0x409d59
        _emit 0xE3
        _emit 0x6A        ; 00409D76  push 0
        _emit 0x00
        _emit 0x6A        ; 00409D78  push 0
        _emit 0x00
        _emit 0x52        ; 00409D7A  push edx
        _emit 0x6A        ; 00409D7B  push 6
        _emit 0x06
        _emit 0xE8        ; 00409D7D  call 0x406570
        _emit 0xEE
        _emit 0xC7
        _emit 0xFF
        _emit 0xFF
        _emit 0x8B        ; 00409D82  mov ecx, dword ptr [0x433350]
        _emit 0x0D
        _emit 0x50
        _emit 0x33
        _emit 0x43
        _emit 0x00
        _emit 0x8B        ; 00409D88  mov edx, dword ptr [0x433354]
        _emit 0x15
        _emit 0x54
        _emit 0x33
        _emit 0x43
        _emit 0x00
        _emit 0x33        ; 00409D8E  xor eax, eax
        _emit 0xC0
        _emit 0x83        ; 00409D90  add esp, 0x10
        _emit 0xC4
        _emit 0x10
        _emit 0x66        ; 00409D93  mov ax, word ptr [ecx + 0xbb4]
        _emit 0x8B
        _emit 0x81
        _emit 0xB4
        _emit 0x0B
        _emit 0x00
        _emit 0x00
        _emit 0x33        ; 00409D9A  xor ebx, ebx
        _emit 0xDB
        _emit 0xC1        ; 00409D9C  shl eax, 4
        _emit 0xE0
        _emit 0x04
        _emit 0x03        ; 00409D9F  add eax, edx
        _emit 0xC2
        _emit 0xC7        ; 00409DA1  mov dword ptr [esp + 0x10], 0
        _emit 0x44
        _emit 0x24
        _emit 0x10
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x0F        ; 00409DA9  movsx ebp, byte ptr [eax + 5]
        _emit 0xBE
        _emit 0x68
        _emit 0x05
        _emit 0x0F        ; 00409DAD  movsx esi, word ptr [eax + 1]
        _emit 0xBF
        _emit 0x70
        _emit 0x01
        _emit 0x0F        ; 00409DB1  movsx edi, word ptr [eax + 3]
        _emit 0xBF
        _emit 0x78
        _emit 0x03
        _emit 0x0F        ; 00409DB5  movsx eax, byte ptr [eax + 6]
        _emit 0xBE
        _emit 0x40
        _emit 0x06
        _emit 0xC1        ; 00409DB9  shl ebp, 0x10
        _emit 0xE5
        _emit 0x10
        _emit 0x89        ; 00409DBC  mov dword ptr [esp + 0x14], ebp
        _emit 0x6C
        _emit 0x24
        _emit 0x14
        _emit 0x8B        ; 00409DC0  mov ebp, dword ptr [0x470048]
        _emit 0x2D
        _emit 0x48
        _emit 0x00
        _emit 0x47
        _emit 0x00
        _emit 0xC1        ; 00409DC6  shl esi, 0x10
        _emit 0xE6
        _emit 0x10
        _emit 0xC1        ; 00409DC9  shl edi, 0x10
        _emit 0xE7
        _emit 0x10
        _emit 0xC1        ; 00409DCC  shl eax, 0x10
        _emit 0xE0
        _emit 0x10
        _emit 0x85        ; 00409DCF  test ebp, ebp
        _emit 0xED
        _emit 0x89        ; 00409DD1  mov dword ptr [esp + 0x18], eax
        _emit 0x44
        _emit 0x24
        _emit 0x18
        _emit 0x0F        ; 00409DD5  jle 0x409e7e
        _emit 0x8E
        _emit 0xA3
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x57        ; 00409DDB  push edi
        _emit 0x56        ; 00409DDC  push esi
        _emit 0x6A        ; 00409DDD  push 0x65
        _emit 0x65
        _emit 0x6A        ; 00409DDF  push 4
        _emit 0x04
        _emit 0xE8        ; 00409DE1  call 0x406570
        _emit 0x8A
        _emit 0xC7
        _emit 0xFF
        _emit 0xFF
        _emit 0x8B        ; 00409DE6  mov edx, dword ptr [0x4dfc6d]
        _emit 0x15
        _emit 0x6D
        _emit 0xFC
        _emit 0x4D
        _emit 0x00
        _emit 0x83        ; 00409DEC  add esp, 0x10
        _emit 0xC4
        _emit 0x10
        _emit 0x3B        ; 00409DEF  cmp edx, ebx
        _emit 0xD3
        _emit 0xC7        ; 00409DF1  mov dword ptr [eax + 0x15a], 2
        _emit 0x80
        _emit 0x5A
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x02
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x7E        ; 00409DFB  jle 0x409e0e
        _emit 0x11
        _emit 0x33        ; 00409DFD  xor ecx, ecx
        _emit 0xC9
        _emit 0x66        ; 00409DFF  mov cx, word ptr [0x445216]
        _emit 0x8B
        _emit 0x0D
        _emit 0x16
        _emit 0x52
        _emit 0x44
        _emit 0x00
        _emit 0x89        ; 00409E06  mov dword ptr [eax + 0x34], ecx
        _emit 0x48
        _emit 0x34
        _emit 0x89        ; 00409E09  mov dword ptr [eax + 0x30], ecx
        _emit 0x48
        _emit 0x30
        _emit 0xEB        ; 00409E0C  jmp 0x409e42
        _emit 0x34
        _emit 0x8B        ; 00409E0E  mov ecx, dword ptr [0x445218]
        _emit 0x0D
        _emit 0x18
        _emit 0x52
        _emit 0x44
        _emit 0x00
        _emit 0x81        ; 00409E14  and ecx, 0xffff
        _emit 0xE1
        _emit 0xFF
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x89        ; 00409E1A  mov dword ptr [eax + 0x34], ecx
        _emit 0x48
        _emit 0x34
        _emit 0x89        ; 00409E1D  mov dword ptr [eax + 0x30], ecx
        _emit 0x48
        _emit 0x30
        _emit 0x8B        ; 00409E20  mov ecx, dword ptr [esp + 0x10]
        _emit 0x4C
        _emit 0x24
        _emit 0x10
        _emit 0x85        ; 00409E24  test ecx, ecx
        _emit 0xC9
        _emit 0x75        ; 00409E26  jne 0x409e42
        _emit 0x1A
        _emit 0x8B        ; 00409E28  mov ecx, dword ptr [0x4cfa00]
        _emit 0x0D
        _emit 0x00
        _emit 0xFA
        _emit 0x4C
        _emit 0x00
        _emit 0xC7        ; 00409E2E  mov dword ptr [esp + 0x10], 1
        _emit 0x44
        _emit 0x24
        _emit 0x10
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x89        ; 00409E36  mov dword ptr [ecx + 0x156], eax
        _emit 0x81
        _emit 0x56
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x89        ; 00409E3C  mov dword ptr [ecx + 0x15a], edx
        _emit 0x91
        _emit 0x5A
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 00409E42  mov ecx, dword ptr [eax + 0x30]
        _emit 0x48
        _emit 0x30
        _emit 0x33        ; 00409E45  xor ebp, ebp
        _emit 0xED
        _emit 0x8D        ; 00409E47  lea edx, [ecx + ecx*4]
        _emit 0x14
        _emit 0x89
        _emit 0xC1        ; 00409E4A  shl edx, 3
        _emit 0xE2
        _emit 0x03
        _emit 0x2B        ; 00409E4D  sub edx, ecx
        _emit 0xD1
        _emit 0x8B        ; 00409E4F  mov ecx, dword ptr [0x433350]
        _emit 0x0D
        _emit 0x50
        _emit 0x33
        _emit 0x43
        _emit 0x00
        _emit 0x66        ; 00409E55  mov bp, word ptr [edx + ecx + 0x20]
        _emit 0x8B
        _emit 0x6C
        _emit 0x0A
        _emit 0x20
        _emit 0x89        ; 00409E5A  mov dword ptr [eax + 0x2c], ebp
        _emit 0x68
        _emit 0x2C
        _emit 0x8B        ; 00409E5D  mov eax, dword ptr [esp + 0x14]
        _emit 0x44
        _emit 0x24
        _emit 0x14
        _emit 0x8B        ; 00409E61  mov ebp, dword ptr [esp + 0x18]
        _emit 0x6C
        _emit 0x24
        _emit 0x18
        _emit 0x03        ; 00409E65  add esi, eax
        _emit 0xF0
        _emit 0x03        ; 00409E67  add edi, ebp
        _emit 0xFD
        _emit 0x8B        ; 00409E69  mov ebp, dword ptr [0x470048]
        _emit 0x2D
        _emit 0x48
        _emit 0x00
        _emit 0x47
        _emit 0x00
        _emit 0x43        ; 00409E6F  inc ebx
        _emit 0x3B        ; 00409E70  cmp ebx, ebp
        _emit 0xDD
        _emit 0x0F        ; 00409E72  jl 0x409ddb
        _emit 0x8C
        _emit 0x63
        _emit 0xFF
        _emit 0xFF
        _emit 0xFF
        _emit 0x8B        ; 00409E78  mov edx, dword ptr [0x433354]
        _emit 0x15
        _emit 0x54
        _emit 0x33
        _emit 0x43
        _emit 0x00
        _emit 0x33        ; 00409E7E  xor eax, eax
        _emit 0xC0
        _emit 0xC7        ; 00409E80  mov dword ptr [esp + 0x10], 0
        _emit 0x44
        _emit 0x24
        _emit 0x10
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x66        ; 00409E88  mov ax, word ptr [ecx + 0xbdb]
        _emit 0x8B
        _emit 0x81
        _emit 0xDB
        _emit 0x0B
        _emit 0x00
        _emit 0x00
        _emit 0xC1        ; 00409E8F  shl eax, 4
        _emit 0xE0
        _emit 0x04
        _emit 0x03        ; 00409E92  add eax, edx
        _emit 0xC2
        _emit 0x0F        ; 00409E94  movsx ebx, byte ptr [eax + 5]
        _emit 0xBE
        _emit 0x58
        _emit 0x05
        _emit 0x0F        ; 00409E98  movsx esi, word ptr [eax + 1]
        _emit 0xBF
        _emit 0x70
        _emit 0x01
        _emit 0x0F        ; 00409E9C  movsx edi, word ptr [eax + 3]
        _emit 0xBF
        _emit 0x78
        _emit 0x03
        _emit 0x0F        ; 00409EA0  movsx eax, byte ptr [eax + 6]
        _emit 0xBE
        _emit 0x40
        _emit 0x06
        _emit 0xC1        ; 00409EA4  shl ebx, 0x10
        _emit 0xE3
        _emit 0x10
        _emit 0x89        ; 00409EA7  mov dword ptr [esp + 0x18], ebx
        _emit 0x5C
        _emit 0x24
        _emit 0x18
        _emit 0x33        ; 00409EAB  xor ebx, ebx
        _emit 0xDB
        _emit 0xC1        ; 00409EAD  shl esi, 0x10
        _emit 0xE6
        _emit 0x10
        _emit 0xC1        ; 00409EB0  shl edi, 0x10
        _emit 0xE7
        _emit 0x10
        _emit 0xC1        ; 00409EB3  shl eax, 0x10
        _emit 0xE0
        _emit 0x10
        _emit 0x85        ; 00409EB6  test ebp, ebp
        _emit 0xED
        _emit 0x89        ; 00409EB8  mov dword ptr [esp + 0x14], eax
        _emit 0x44
        _emit 0x24
        _emit 0x14
        _emit 0x0F        ; 00409EBC  jle 0x409f64
        _emit 0x8E
        _emit 0xA2
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x57        ; 00409EC2  push edi
        _emit 0x56        ; 00409EC3  push esi
        _emit 0x6A        ; 00409EC4  push 0x65
        _emit 0x65
        _emit 0x6A        ; 00409EC6  push 4
        _emit 0x04
        _emit 0xE8        ; 00409EC8  call 0x406570
        _emit 0xA3
        _emit 0xC6
        _emit 0xFF
        _emit 0xFF
        _emit 0x8B        ; 00409ECD  mov edx, dword ptr [0x4edcac]
        _emit 0x15
        _emit 0xAC
        _emit 0xDC
        _emit 0x4E
        _emit 0x00
        _emit 0x83        ; 00409ED3  add esp, 0x10
        _emit 0xC4
        _emit 0x10
        _emit 0x3B        ; 00409ED6  cmp edx, ebx
        _emit 0xD3
        _emit 0xC7        ; 00409ED8  mov dword ptr [eax + 0x15a], 2
        _emit 0x80
        _emit 0x5A
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x02
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x7E        ; 00409EE2  jle 0x409ef5
        _emit 0x11
        _emit 0x33        ; 00409EE4  xor ecx, ecx
        _emit 0xC9
        _emit 0x66        ; 00409EE6  mov cx, word ptr [0x445216]
        _emit 0x8B
        _emit 0x0D
        _emit 0x16
        _emit 0x52
        _emit 0x44
        _emit 0x00
        _emit 0x89        ; 00409EED  mov dword ptr [eax + 0x34], ecx
        _emit 0x48
        _emit 0x34
        _emit 0x89        ; 00409EF0  mov dword ptr [eax + 0x30], ecx
        _emit 0x48
        _emit 0x30
        _emit 0xEB        ; 00409EF3  jmp 0x409f29
        _emit 0x34
        _emit 0x8B        ; 00409EF5  mov ecx, dword ptr [0x445218]
        _emit 0x0D
        _emit 0x18
        _emit 0x52
        _emit 0x44
        _emit 0x00
        _emit 0x81        ; 00409EFB  and ecx, 0xffff
        _emit 0xE1
        _emit 0xFF
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x89        ; 00409F01  mov dword ptr [eax + 0x34], ecx
        _emit 0x48
        _emit 0x34
        _emit 0x89        ; 00409F04  mov dword ptr [eax + 0x30], ecx
        _emit 0x48
        _emit 0x30
        _emit 0x8B        ; 00409F07  mov ecx, dword ptr [esp + 0x10]
        _emit 0x4C
        _emit 0x24
        _emit 0x10
        _emit 0x85        ; 00409F0B  test ecx, ecx
        _emit 0xC9
        _emit 0x75        ; 00409F0D  jne 0x409f29
        _emit 0x1A
        _emit 0x8B        ; 00409F0F  mov ecx, dword ptr [0x4cfa00]
        _emit 0x0D
        _emit 0x00
        _emit 0xFA
        _emit 0x4C
        _emit 0x00
        _emit 0xC7        ; 00409F15  mov dword ptr [esp + 0x10], 1
        _emit 0x44
        _emit 0x24
        _emit 0x10
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x89        ; 00409F1D  mov dword ptr [ecx + 0x15e], eax
        _emit 0x81
        _emit 0x5E
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x89        ; 00409F23  mov dword ptr [ecx + 0x162], edx
        _emit 0x91
        _emit 0x62
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 00409F29  mov ecx, dword ptr [eax + 0x30]
        _emit 0x48
        _emit 0x30
        _emit 0x33        ; 00409F2C  xor ebp, ebp
        _emit 0xED
        _emit 0x8D        ; 00409F2E  lea edx, [ecx + ecx*4]
        _emit 0x14
        _emit 0x89
        _emit 0xC1        ; 00409F31  shl edx, 3
        _emit 0xE2
        _emit 0x03
        _emit 0x2B        ; 00409F34  sub edx, ecx
        _emit 0xD1
        _emit 0x8B        ; 00409F36  mov ecx, dword ptr [0x433350]
        _emit 0x0D
        _emit 0x50
        _emit 0x33
        _emit 0x43
        _emit 0x00
        _emit 0x66        ; 00409F3C  mov bp, word ptr [edx + ecx + 0x20]
        _emit 0x8B
        _emit 0x6C
        _emit 0x0A
        _emit 0x20
        _emit 0x89        ; 00409F41  mov dword ptr [eax + 0x2c], ebp
        _emit 0x68
        _emit 0x2C
        _emit 0x8B        ; 00409F44  mov eax, dword ptr [esp + 0x18]
        _emit 0x44
        _emit 0x24
        _emit 0x18
        _emit 0x8B        ; 00409F48  mov ebp, dword ptr [esp + 0x14]
        _emit 0x6C
        _emit 0x24
        _emit 0x14
        _emit 0x03        ; 00409F4C  add esi, eax
        _emit 0xF0
        _emit 0xA1        ; 00409F4E  mov eax, dword ptr [0x470048]
        _emit 0x48
        _emit 0x00
        _emit 0x47
        _emit 0x00
        _emit 0x03        ; 00409F53  add edi, ebp
        _emit 0xFD
        _emit 0x43        ; 00409F55  inc ebx
        _emit 0x3B        ; 00409F56  cmp ebx, eax
        _emit 0xD8
        _emit 0x0F        ; 00409F58  jl 0x409ec2
        _emit 0x8C
        _emit 0x64
        _emit 0xFF
        _emit 0xFF
        _emit 0xFF
        _emit 0x8B        ; 00409F5E  mov edx, dword ptr [0x433354]
        _emit 0x15
        _emit 0x54
        _emit 0x33
        _emit 0x43
        _emit 0x00
        _emit 0xA1        ; 00409F64  mov eax, dword ptr [0x470058]
        _emit 0x58
        _emit 0x00
        _emit 0x47
        _emit 0x00
        _emit 0x85        ; 00409F69  test eax, eax
        _emit 0xC0
        _emit 0x0F        ; 00409F6B  je 0x40a059
        _emit 0x84
        _emit 0xE8
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x0F        ; 00409F71  jle 0x40a0fb
        _emit 0x8E
        _emit 0x84
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0xBE        ; 00409F77  mov esi, 2
        _emit 0x02
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x3B        ; 00409F7C  cmp eax, esi
        _emit 0xC6
        _emit 0x0F        ; 00409F7E  jg 0x40a2bd
        _emit 0x8F
        _emit 0x39
        _emit 0x03
        _emit 0x00
        _emit 0x00
        _emit 0x33        ; 00409F84  xor eax, eax
        _emit 0xC0
        _emit 0x66        ; 00409F86  mov ax, word ptr [ecx + 0xb18]
        _emit 0x8B
        _emit 0x81
        _emit 0x18
        _emit 0x0B
        _emit 0x00
        _emit 0x00
        _emit 0xC1        ; 00409F8D  shl eax, 4
        _emit 0xE0
        _emit 0x04
        _emit 0x8D        ; 00409F90  lea ecx, [eax + edx]
        _emit 0x0C
        _emit 0x10
        _emit 0x0F        ; 00409F93  movsx eax, word ptr [eax + edx + 1]
        _emit 0xBF
        _emit 0x44
        _emit 0x10
        _emit 0x01
        _emit 0x0F        ; 00409F98  movsx ecx, word ptr [ecx + 3]
        _emit 0xBF
        _emit 0x49
        _emit 0x03
        _emit 0xC1        ; 00409F9C  shl ecx, 0x10
        _emit 0xE1
        _emit 0x10
        _emit 0xC1        ; 00409F9F  shl eax, 0x10
        _emit 0xE0
        _emit 0x10
        _emit 0x51        ; 00409FA2  push ecx
        _emit 0x50        ; 00409FA3  push eax
        _emit 0x6A        ; 00409FA4  push 0x65
        _emit 0x65
        _emit 0x6A        ; 00409FA6  push 4
        _emit 0x04
        _emit 0xE8        ; 00409FA8  call 0x406570
        _emit 0xC3
        _emit 0xC5
        _emit 0xFF
        _emit 0xFF
        _emit 0x33        ; 00409FAD  xor ecx, ecx
        _emit 0xC9
        _emit 0x8B        ; 00409FAF  mov ebx, dword ptr [0x4d1e90]
        _emit 0x1D
        _emit 0x90
        _emit 0x1E
        _emit 0x4D
        _emit 0x00
        _emit 0x66        ; 00409FB5  mov cx, word ptr [0x4d9326]
        _emit 0x8B
        _emit 0x0D
        _emit 0x26
        _emit 0x93
        _emit 0x4D
        _emit 0x00
        _emit 0xBF        ; 00409FBC  mov edi, 1
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x89        ; 00409FC1  mov dword ptr [eax + 0x34], ecx
        _emit 0x48
        _emit 0x34
        _emit 0x89        ; 00409FC4  mov dword ptr [eax + 0x30], ecx
        _emit 0x48
        _emit 0x30
        _emit 0x8D        ; 00409FC7  lea edx, [ecx + ecx*4]
        _emit 0x14
        _emit 0x89
        _emit 0xC7        ; 00409FCA  mov dword ptr [eax + 0x156], 0
        _emit 0x80
        _emit 0x56
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0xC1        ; 00409FD4  shl edx, 3
        _emit 0xE2
        _emit 0x03
        _emit 0x2B        ; 00409FD7  sub edx, ecx
        _emit 0xD1
        _emit 0x33        ; 00409FD9  xor ecx, ecx
        _emit 0xC9
        _emit 0x89        ; 00409FDB  mov dword ptr [eax + 0x15a], edi
        _emit 0xB8
        _emit 0x5A
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x66        ; 00409FE1  mov cx, word ptr [edx + ebx + 0x20]
        _emit 0x8B
        _emit 0x4C
        _emit 0x1A
        _emit 0x20
        _emit 0x33        ; 00409FE6  xor edx, edx
        _emit 0xD2
        _emit 0x89        ; 00409FE8  mov dword ptr [eax + 0x2c], ecx
        _emit 0x48
        _emit 0x2C
        _emit 0xA1        ; 00409FEB  mov eax, dword ptr [0x433350]
        _emit 0x50
        _emit 0x33
        _emit 0x43
        _emit 0x00
        _emit 0x8B        ; 00409FF0  mov ecx, dword ptr [0x433354]
        _emit 0x0D
        _emit 0x54
        _emit 0x33
        _emit 0x43
        _emit 0x00
        _emit 0x66        ; 00409FF6  mov dx, word ptr [eax + 0xb3f]
        _emit 0x8B
        _emit 0x90
        _emit 0x3F
        _emit 0x0B
        _emit 0x00
        _emit 0x00
        _emit 0xC1        ; 00409FFD  shl edx, 4
        _emit 0xE2
        _emit 0x04
        _emit 0x03        ; 0040A000  add ecx, edx
        _emit 0xCA
        _emit 0x0F        ; 0040A002  movsx eax, word ptr [ecx + 1]
        _emit 0xBF
        _emit 0x41
        _emit 0x01
        _emit 0x0F        ; 0040A006  movsx ecx, word ptr [ecx + 3]
        _emit 0xBF
        _emit 0x49
        _emit 0x03
        _emit 0xC1        ; 0040A00A  shl ecx, 0x10
        _emit 0xE1
        _emit 0x10
        _emit 0xC1        ; 0040A00D  shl eax, 0x10
        _emit 0xE0
        _emit 0x10
        _emit 0x51        ; 0040A010  push ecx
        _emit 0x50        ; 0040A011  push eax
        _emit 0x6A        ; 0040A012  push 0x65
        _emit 0x65
        _emit 0x6A        ; 0040A014  push 4
        _emit 0x04
        _emit 0xE8        ; 0040A016  call 0x406570
        _emit 0x55
        _emit 0xC5
        _emit 0xFF
        _emit 0xFF
        _emit 0x33        ; 0040A01B  xor ecx, ecx
        _emit 0xC9
        _emit 0x8B        ; 0040A01D  mov ebx, dword ptr [0x4dfecf]
        _emit 0x1D
        _emit 0xCF
        _emit 0xFE
        _emit 0x4D
        _emit 0x00
        _emit 0x66        ; 0040A023  mov cx, word ptr [0x4e7365]
        _emit 0x8B
        _emit 0x0D
        _emit 0x65
        _emit 0x73
        _emit 0x4E
        _emit 0x00
        _emit 0x83        ; 0040A02A  add esp, 0x20
        _emit 0xC4
        _emit 0x20
        _emit 0x89        ; 0040A02D  mov dword ptr [eax + 0x34], ecx
        _emit 0x48
        _emit 0x34
        _emit 0x89        ; 0040A030  mov dword ptr [eax + 0x30], ecx
        _emit 0x48
        _emit 0x30
        _emit 0x8D        ; 0040A033  lea edx, [ecx + ecx*4]
        _emit 0x14
        _emit 0x89
        _emit 0x89        ; 0040A036  mov dword ptr [eax + 0x156], edi
        _emit 0xB8
        _emit 0x56
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0xC1        ; 0040A03C  shl edx, 3
        _emit 0xE2
        _emit 0x03
        _emit 0x2B        ; 0040A03F  sub edx, ecx
        _emit 0xD1
        _emit 0x33        ; 0040A041  xor ecx, ecx
        _emit 0xC9
        _emit 0x89        ; 0040A043  mov dword ptr [eax + 0x15a], edi
        _emit 0xB8
        _emit 0x5A
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x89        ; 0040A049  mov dword ptr [eax + 0x5c], edi
        _emit 0x78
        _emit 0x5C
        _emit 0x66        ; 0040A04C  mov cx, word ptr [edx + ebx + 0x20]
        _emit 0x8B
        _emit 0x4C
        _emit 0x1A
        _emit 0x20
        _emit 0x89        ; 0040A051  mov dword ptr [eax + 0x2c], ecx
        _emit 0x48
        _emit 0x2C
        _emit 0xE9        ; 0040A054  jmp 0x40a105
        _emit 0xAC
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x33        ; 0040A059  xor eax, eax
        _emit 0xC0
        _emit 0x8B        ; 0040A05B  mov esi, dword ptr [0x4d1e90]
        _emit 0x35
        _emit 0x90
        _emit 0x1E
        _emit 0x4D
        _emit 0x00
        _emit 0x66        ; 0040A061  mov ax, word ptr [0x4d9326]
        _emit 0xA1
        _emit 0x26
        _emit 0x93
        _emit 0x4D
        _emit 0x00
        _emit 0x8D        ; 0040A067  lea edi, [eax + eax*4]
        _emit 0x3C
        _emit 0x80
        _emit 0xC1        ; 0040A06A  shl edi, 3
        _emit 0xE7
        _emit 0x03
        _emit 0x2B        ; 0040A06D  sub edi, eax
        _emit 0xF8
        _emit 0x33        ; 0040A06F  xor eax, eax
        _emit 0xC0
        _emit 0x66        ; 0040A071  mov ax, word ptr [edi + esi + 0x20]
        _emit 0x8B
        _emit 0x44
        _emit 0x37
        _emit 0x20
        _emit 0x8B        ; 0040A076  mov edi, eax
        _emit 0xF8
        _emit 0xA1        ; 0040A078  mov eax, dword ptr [0x4d9328]
        _emit 0x28
        _emit 0x93
        _emit 0x4D
        _emit 0x00
        _emit 0x25        ; 0040A07D  and eax, 0xffff
        _emit 0xFF
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x47        ; 0040A082  inc edi
        _emit 0x8D        ; 0040A083  lea ebx, [eax + eax*4]
        _emit 0x1C
        _emit 0x80
        _emit 0xC1        ; 0040A086  shl ebx, 3
        _emit 0xE3
        _emit 0x03
        _emit 0x2B        ; 0040A089  sub ebx, eax
        _emit 0xD8
        _emit 0x33        ; 0040A08B  xor eax, eax
        _emit 0xC0
        _emit 0x66        ; 0040A08D  mov ax, word ptr [ebx + esi + 0x20]
        _emit 0x8B
        _emit 0x44
        _emit 0x33
        _emit 0x20
        _emit 0x3B        ; 0040A092  cmp edi, eax
        _emit 0xF8
        _emit 0x7D        ; 0040A094  jge 0x40a0fb
        _emit 0x65
        _emit 0x33        ; 0040A096  xor eax, eax
        _emit 0xC0
        _emit 0x66        ; 0040A098  mov ax, word ptr [ecx + 0xb18]
        _emit 0x8B
        _emit 0x81
        _emit 0x18
        _emit 0x0B
        _emit 0x00
        _emit 0x00
        _emit 0xC1        ; 0040A09F  shl eax, 4
        _emit 0xE0
        _emit 0x04
        _emit 0x03        ; 0040A0A2  add edx, eax
        _emit 0xD0
        _emit 0x0F        ; 0040A0A4  movsx ecx, word ptr [edx + 3]
        _emit 0xBF
        _emit 0x4A
        _emit 0x03
        _emit 0x0F        ; 0040A0A8  movsx eax, word ptr [edx + 1]
        _emit 0xBF
        _emit 0x42
        _emit 0x01
        _emit 0xC1        ; 0040A0AC  shl ecx, 0x10
        _emit 0xE1
        _emit 0x10
        _emit 0xC1        ; 0040A0AF  shl eax, 0x10
        _emit 0xE0
        _emit 0x10
        _emit 0x51        ; 0040A0B2  push ecx
        _emit 0x50        ; 0040A0B3  push eax
        _emit 0x6A        ; 0040A0B4  push 0x65
        _emit 0x65
        _emit 0x6A        ; 0040A0B6  push 4
        _emit 0x04
        _emit 0xE8        ; 0040A0B8  call 0x406570
        _emit 0xB3
        _emit 0xC4
        _emit 0xFF
        _emit 0xFF
        _emit 0x33        ; 0040A0BD  xor ecx, ecx
        _emit 0xC9
        _emit 0x8B        ; 0040A0BF  mov esi, dword ptr [0x4d1e90]
        _emit 0x35
        _emit 0x90
        _emit 0x1E
        _emit 0x4D
        _emit 0x00
        _emit 0x66        ; 0040A0C5  mov cx, word ptr [0x4d9326]
        _emit 0x8B
        _emit 0x0D
        _emit 0x26
        _emit 0x93
        _emit 0x4D
        _emit 0x00
        _emit 0x83        ; 0040A0CC  add esp, 0x10
        _emit 0xC4
        _emit 0x10
        _emit 0x89        ; 0040A0CF  mov dword ptr [eax + 0x34], ecx
        _emit 0x48
        _emit 0x34
        _emit 0x89        ; 0040A0D2  mov dword ptr [eax + 0x30], ecx
        _emit 0x48
        _emit 0x30
        _emit 0x8D        ; 0040A0D5  lea edx, [ecx + ecx*4]
        _emit 0x14
        _emit 0x89
        _emit 0xC7        ; 0040A0D8  mov dword ptr [eax + 0x156], 0
        _emit 0x80
        _emit 0x56
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0xC1        ; 0040A0E2  shl edx, 3
        _emit 0xE2
        _emit 0x03
        _emit 0x2B        ; 0040A0E5  sub edx, ecx
        _emit 0xD1
        _emit 0x33        ; 0040A0E7  xor ecx, ecx
        _emit 0xC9
        _emit 0xC7        ; 0040A0E9  mov dword ptr [eax + 0x15a], 1
        _emit 0x80
        _emit 0x5A
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x66        ; 0040A0F3  mov cx, word ptr [edx + esi + 0x20]
        _emit 0x8B
        _emit 0x4C
        _emit 0x32
        _emit 0x20
        _emit 0x89        ; 0040A0F8  mov dword ptr [eax + 0x2c], ecx
        _emit 0x48
        _emit 0x2C
        _emit 0xBF        ; 0040A0FB  mov edi, 1
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0xBE        ; 0040A100  mov esi, 2
        _emit 0x02
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x6A        ; 0040A105  push 0
        _emit 0x00
        _emit 0x6A        ; 0040A107  push 0
        _emit 0x00
        _emit 0x6A        ; 0040A109  push 0x65
        _emit 0x65
        _emit 0x6A        ; 0040A10B  push 4
        _emit 0x04
        _emit 0xE8        ; 0040A10D  call 0x406570
        _emit 0x5E
        _emit 0xC4
        _emit 0xFF
        _emit 0xFF
        _emit 0x33        ; 0040A112  xor ecx, ecx
        _emit 0xC9
        _emit 0x8B        ; 0040A114  mov ebx, dword ptr [0x433350]
        _emit 0x1D
        _emit 0x50
        _emit 0x33
        _emit 0x43
        _emit 0x00
        _emit 0x66        ; 0040A11A  mov cx, word ptr [0x44522e]
        _emit 0x8B
        _emit 0x0D
        _emit 0x2E
        _emit 0x52
        _emit 0x44
        _emit 0x00
        _emit 0x6A        ; 0040A121  push 0
        _emit 0x00
        _emit 0x89        ; 0040A123  mov dword ptr [eax + 0x34], ecx
        _emit 0x48
        _emit 0x34
        _emit 0x89        ; 0040A126  mov dword ptr [eax + 0x30], ecx
        _emit 0x48
        _emit 0x30
        _emit 0x8D        ; 0040A129  lea edx, [ecx + ecx*4]
        _emit 0x14
        _emit 0x89
        _emit 0x6A        ; 0040A12C  push 0
        _emit 0x00
        _emit 0xC1        ; 0040A12E  shl edx, 3
        _emit 0xE2
        _emit 0x03
        _emit 0x2B        ; 0040A131  sub edx, ecx
        _emit 0xD1
        _emit 0x33        ; 0040A133  xor ecx, ecx
        _emit 0xC9
        _emit 0x6A        ; 0040A135  push 0x65
        _emit 0x65
        _emit 0x6A        ; 0040A137  push 4
        _emit 0x04
        _emit 0x66        ; 0040A139  mov cx, word ptr [edx + ebx + 0x20]
        _emit 0x8B
        _emit 0x4C
        _emit 0x1A
        _emit 0x20
        _emit 0x89        ; 0040A13E  mov dword ptr [eax + 0x15a], esi
        _emit 0xB0
        _emit 0x5A
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x89        ; 0040A144  mov dword ptr [eax + 0x2c], ecx
        _emit 0x48
        _emit 0x2C
        _emit 0xC7        ; 0040A147  mov dword ptr [eax + 0x156], 0xa
        _emit 0x80
        _emit 0x56
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x0A
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0xE8        ; 0040A151  call 0x406570
        _emit 0x1A
        _emit 0xC4
        _emit 0xFF
        _emit 0xFF
        _emit 0x8B        ; 0040A156  mov ecx, dword ptr [0x445230]
        _emit 0x0D
        _emit 0x30
        _emit 0x52
        _emit 0x44
        _emit 0x00
        _emit 0x8B        ; 0040A15C  mov ebx, dword ptr [0x433350]
        _emit 0x1D
        _emit 0x50
        _emit 0x33
        _emit 0x43
        _emit 0x00
        _emit 0x81        ; 0040A162  and ecx, 0xffff
        _emit 0xE1
        _emit 0xFF
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x6A        ; 0040A168  push 0
        _emit 0x00
        _emit 0x89        ; 0040A16A  mov dword ptr [eax + 0x34], ecx
        _emit 0x48
        _emit 0x34
        _emit 0x89        ; 0040A16D  mov dword ptr [eax + 0x30], ecx
        _emit 0x48
        _emit 0x30
        _emit 0x8D        ; 0040A170  lea edx, [ecx + ecx*4]
        _emit 0x14
        _emit 0x89
        _emit 0x6A        ; 0040A173  push 0
        _emit 0x00
        _emit 0xC1        ; 0040A175  shl edx, 3
        _emit 0xE2
        _emit 0x03
        _emit 0x2B        ; 0040A178  sub edx, ecx
        _emit 0xD1
        _emit 0x33        ; 0040A17A  xor ecx, ecx
        _emit 0xC9
        _emit 0x6A        ; 0040A17C  push 0x65
        _emit 0x65
        _emit 0x6A        ; 0040A17E  push 4
        _emit 0x04
        _emit 0x66        ; 0040A180  mov cx, word ptr [edx + ebx + 0x20]
        _emit 0x8B
        _emit 0x4C
        _emit 0x1A
        _emit 0x20
        _emit 0x89        ; 0040A185  mov dword ptr [eax + 0x15a], esi
        _emit 0xB0
        _emit 0x5A
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x89        ; 0040A18B  mov dword ptr [eax + 0x2c], ecx
        _emit 0x48
        _emit 0x2C
        _emit 0xC7        ; 0040A18E  mov dword ptr [eax + 0x156], 0xb
        _emit 0x80
        _emit 0x56
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x0B
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0xE8        ; 0040A198  call 0x406570
        _emit 0xD3
        _emit 0xC3
        _emit 0xFF
        _emit 0xFF
        _emit 0x33        ; 0040A19D  xor ecx, ecx
        _emit 0xC9
        _emit 0x8B        ; 0040A19F  mov ebx, dword ptr [0x433350]
        _emit 0x1D
        _emit 0x50
        _emit 0x33
        _emit 0x43
        _emit 0x00
        _emit 0x66        ; 0040A1A5  mov cx, word ptr [0x445232]
        _emit 0x8B
        _emit 0x0D
        _emit 0x32
        _emit 0x52
        _emit 0x44
        _emit 0x00
        _emit 0x6A        ; 0040A1AC  push 0
        _emit 0x00
        _emit 0x89        ; 0040A1AE  mov dword ptr [eax + 0x34], ecx
        _emit 0x48
        _emit 0x34
        _emit 0x89        ; 0040A1B1  mov dword ptr [eax + 0x30], ecx
        _emit 0x48
        _emit 0x30
        _emit 0x8D        ; 0040A1B4  lea edx, [ecx + ecx*4]
        _emit 0x14
        _emit 0x89
        _emit 0x6A        ; 0040A1B7  push 0
        _emit 0x00
        _emit 0xC1        ; 0040A1B9  shl edx, 3
        _emit 0xE2
        _emit 0x03
        _emit 0x2B        ; 0040A1BC  sub edx, ecx
        _emit 0xD1
        _emit 0x33        ; 0040A1BE  xor ecx, ecx
        _emit 0xC9
        _emit 0x6A        ; 0040A1C0  push 0x65
        _emit 0x65
        _emit 0x6A        ; 0040A1C2  push 4
        _emit 0x04
        _emit 0x66        ; 0040A1C4  mov cx, word ptr [edx + ebx + 0x20]
        _emit 0x8B
        _emit 0x4C
        _emit 0x1A
        _emit 0x20
        _emit 0x89        ; 0040A1C9  mov dword ptr [eax + 0x15a], esi
        _emit 0xB0
        _emit 0x5A
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x89        ; 0040A1CF  mov dword ptr [eax + 0x2c], ecx
        _emit 0x48
        _emit 0x2C
        _emit 0xC7        ; 0040A1D2  mov dword ptr [eax + 0x156], 0x14
        _emit 0x80
        _emit 0x56
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x14
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0xE8        ; 0040A1DC  call 0x406570
        _emit 0x8F
        _emit 0xC3
        _emit 0xFF
        _emit 0xFF
        _emit 0x8B        ; 0040A1E1  mov ecx, dword ptr [0x445234]
        _emit 0x0D
        _emit 0x34
        _emit 0x52
        _emit 0x44
        _emit 0x00
        _emit 0x33        ; 0040A1E7  xor ebx, ebx
        _emit 0xDB
        _emit 0x81        ; 0040A1E9  and ecx, 0xffff
        _emit 0xE1
        _emit 0xFF
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x89        ; 0040A1EF  mov dword ptr [eax + 0x15a], esi
        _emit 0xB0
        _emit 0x5A
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x89        ; 0040A1F5  mov dword ptr [eax + 0x34], ecx
        _emit 0x48
        _emit 0x34
        _emit 0x89        ; 0040A1F8  mov dword ptr [eax + 0x30], ecx
        _emit 0x48
        _emit 0x30
        _emit 0x8D        ; 0040A1FB  lea edx, [ecx + ecx*4]
        _emit 0x14
        _emit 0x89
        _emit 0xC7        ; 0040A1FE  mov dword ptr [eax + 0x156], 0x15
        _emit 0x80
        _emit 0x56
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x15
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0xC1        ; 0040A208  shl edx, 3
        _emit 0xE2
        _emit 0x03
        _emit 0x2B        ; 0040A20B  sub edx, ecx
        _emit 0xD1
        _emit 0x8B        ; 0040A20D  mov ecx, dword ptr [0x433350]
        _emit 0x0D
        _emit 0x50
        _emit 0x33
        _emit 0x43
        _emit 0x00
        _emit 0x83        ; 0040A213  add esp, 0x40
        _emit 0xC4
        _emit 0x40
        _emit 0x66        ; 0040A216  mov bx, word ptr [edx + ecx + 0x20]
        _emit 0x8B
        _emit 0x5C
        _emit 0x0A
        _emit 0x20
        _emit 0x89        ; 0040A21B  mov dword ptr [eax + 0x2c], ebx
        _emit 0x58
        _emit 0x2C
        _emit 0x33        ; 0040A21E  xor eax, eax
        _emit 0xC0
        _emit 0x66        ; 0040A220  mov ax, word ptr [ecx + 0xb66]
        _emit 0x8B
        _emit 0x81
        _emit 0x66
        _emit 0x0B
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040A227  mov ecx, dword ptr [0x433354]
        _emit 0x0D
        _emit 0x54
        _emit 0x33
        _emit 0x43
        _emit 0x00
        _emit 0xC1        ; 0040A22D  shl eax, 4
        _emit 0xE0
        _emit 0x04
        _emit 0x03        ; 0040A230  add eax, ecx
        _emit 0xC1
        _emit 0x0F        ; 0040A232  movsx edx, word ptr [eax + 3]
        _emit 0xBF
        _emit 0x50
        _emit 0x03
        _emit 0xC1        ; 0040A236  shl edx, 0x10
        _emit 0xE2
        _emit 0x10
        _emit 0x52        ; 0040A239  push edx
        _emit 0x0F        ; 0040A23A  movsx eax, word ptr [eax + 1]
        _emit 0xBF
        _emit 0x40
        _emit 0x01
        _emit 0xC1        ; 0040A23E  shl eax, 0x10
        _emit 0xE0
        _emit 0x10
        _emit 0x50        ; 0040A241  push eax
        _emit 0x6A        ; 0040A242  push 0x65
        _emit 0x65
        _emit 0x6A        ; 0040A244  push 4
        _emit 0x04
        _emit 0xE8        ; 0040A246  call 0x406570
        _emit 0x25
        _emit 0xC3
        _emit 0xFF
        _emit 0xFF
        _emit 0x8B        ; 0040A24B  mov ecx, dword ptr [0x4cfa00]
        _emit 0x0D
        _emit 0x00
        _emit 0xFA
        _emit 0x4C
        _emit 0x00
        _emit 0x89        ; 0040A251  mov dword ptr [eax + 0x15a], esi
        _emit 0xB0
        _emit 0x5A
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x33        ; 0040A257  xor edx, edx
        _emit 0xD2
        _emit 0x89        ; 0040A259  mov dword ptr [ecx + 0x166], eax
        _emit 0x81
        _emit 0x66
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0xA1        ; 0040A25F  mov eax, dword ptr [0x433350]
        _emit 0x50
        _emit 0x33
        _emit 0x43
        _emit 0x00
        _emit 0x8B        ; 0040A264  mov ecx, dword ptr [0x433354]
        _emit 0x0D
        _emit 0x54
        _emit 0x33
        _emit 0x43
        _emit 0x00
        _emit 0x66        ; 0040A26A  mov dx, word ptr [eax + 0xb8d]
        _emit 0x8B
        _emit 0x90
        _emit 0x8D
        _emit 0x0B
        _emit 0x00
        _emit 0x00
        _emit 0xC1        ; 0040A271  shl edx, 4
        _emit 0xE2
        _emit 0x04
        _emit 0x8D        ; 0040A274  lea eax, [edx + ecx]
        _emit 0x04
        _emit 0x0A
        _emit 0x0F        ; 0040A277  movsx edx, word ptr [edx + ecx + 3]
        _emit 0xBF
        _emit 0x54
        _emit 0x0A
        _emit 0x03
        _emit 0x0F        ; 0040A27C  movsx eax, word ptr [eax + 1]
        _emit 0xBF
        _emit 0x40
        _emit 0x01
        _emit 0xC1        ; 0040A280  shl edx, 0x10
        _emit 0xE2
        _emit 0x10
        _emit 0xC1        ; 0040A283  shl eax, 0x10
        _emit 0xE0
        _emit 0x10
        _emit 0x52        ; 0040A286  push edx
        _emit 0x50        ; 0040A287  push eax
        _emit 0x6A        ; 0040A288  push 0x65
        _emit 0x65
        _emit 0x6A        ; 0040A28A  push 4
        _emit 0x04
        _emit 0xE8        ; 0040A28C  call 0x406570
        _emit 0xDF
        _emit 0xC2
        _emit 0xFF
        _emit 0xFF
        _emit 0x8B        ; 0040A291  mov ecx, dword ptr [0x4cfa00]
        _emit 0x0D
        _emit 0x00
        _emit 0xFA
        _emit 0x4C
        _emit 0x00
        _emit 0x8B        ; 0040A297  mov ebx, dword ptr [0x4701c4]
        _emit 0x1D
        _emit 0xC4
        _emit 0x01
        _emit 0x47
        _emit 0x00
        _emit 0x83        ; 0040A29D  add esp, 0x20
        _emit 0xC4
        _emit 0x20
        _emit 0x89        ; 0040A2A0  mov dword ptr [eax + 0x15a], esi
        _emit 0xB0
        _emit 0x5A
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x89        ; 0040A2A6  mov dword ptr [ecx + 0x16e], eax
        _emit 0x81
        _emit 0x6E
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x83        ; 0040A2AC  or eax, 0xffffffff
        _emit 0xC8
        _emit 0xFF
        _emit 0x89        ; 0040A2AF  mov dword ptr [ecx + 0x172], eax
        _emit 0x81
        _emit 0x72
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x89        ; 0040A2B5  mov dword ptr [ecx + 0x16a], eax
        _emit 0x81
        _emit 0x6A
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0xEB        ; 0040A2BB  jmp 0x40a2d1
        _emit 0x14
        _emit 0xBF        ; 0040A2BD  mov edi, 1
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0xE9        ; 0040A2C2  jmp 0x40a105
        _emit 0x3E
        _emit 0xFE
        _emit 0xFF
        _emit 0xFF
        _emit 0xBF        ; 0040A2C7  mov edi, 1
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0xBE        ; 0040A2CC  mov esi, 2
        _emit 0x02
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0xA1        ; 0040A2D1  mov eax, dword ptr [0x470058]
        _emit 0x58
        _emit 0x00
        _emit 0x47
        _emit 0x00
        _emit 0x33        ; 0040A2D6  xor ebp, ebp
        _emit 0xED
        _emit 0x3B        ; 0040A2D8  cmp eax, ebp
        _emit 0xC5
        _emit 0x0F        ; 0040A2DA  je 0x40a36e
        _emit 0x84
        _emit 0x8E
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x0F        ; 0040A2E0  jle 0x40a57e
        _emit 0x8E
        _emit 0x98
        _emit 0x02
        _emit 0x00
        _emit 0x00
        _emit 0x3B        ; 0040A2E6  cmp eax, esi
        _emit 0xC6
        _emit 0x0F        ; 0040A2E8  jg 0x40a57e
        _emit 0x8F
        _emit 0x90
        _emit 0x02
        _emit 0x00
        _emit 0x00
        _emit 0xA1        ; 0040A2EE  mov eax, dword ptr [0x4dfc95]
        _emit 0x95
        _emit 0xFC
        _emit 0x4D
        _emit 0x00
        _emit 0x8B        ; 0040A2F3  mov edx, dword ptr [ecx + 0x16a]
        _emit 0x91
        _emit 0x6A
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040A2F9  mov esi, dword ptr [0x433350]
        _emit 0x35
        _emit 0x50
        _emit 0x33
        _emit 0x43
        _emit 0x00
        _emit 0x3B        ; 0040A2FF  cmp edx, eax
        _emit 0xD0
        _emit 0x74        ; 0040A301  je 0x40a337
        _emit 0x34
        _emit 0x33        ; 0040A303  xor edx, edx
        _emit 0xD2
        _emit 0x89        ; 0040A305  mov dword ptr [ecx + 0x16a], eax
        _emit 0x81
        _emit 0x6A
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x66        ; 0040A30B  mov dx, word ptr [eax*2 + 0x445202]
        _emit 0x8B
        _emit 0x14
        _emit 0x45
        _emit 0x02
        _emit 0x52
        _emit 0x44
        _emit 0x00
        _emit 0x8B        ; 0040A313  mov eax, dword ptr [ecx + 0x166]
        _emit 0x81
        _emit 0x66
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x89        ; 0040A319  mov dword ptr [eax + 0x34], edx
        _emit 0x50
        _emit 0x34
        _emit 0x89        ; 0040A31C  mov dword ptr [eax + 0x30], edx
        _emit 0x50
        _emit 0x30
        _emit 0x89        ; 0040A31F  mov dword ptr [eax + 0x3c], ebp
        _emit 0x68
        _emit 0x3C
        _emit 0x8B        ; 0040A322  mov edx, dword ptr [eax + 0x30]
        _emit 0x50
        _emit 0x30
        _emit 0x8D        ; 0040A325  lea edi, [edx + edx*4]
        _emit 0x3C
        _emit 0x92
        _emit 0xC1        ; 0040A328  shl edi, 3
        _emit 0xE7
        _emit 0x03
        _emit 0x2B        ; 0040A32B  sub edi, edx
        _emit 0xFA
        _emit 0x33        ; 0040A32D  xor edx, edx
        _emit 0xD2
        _emit 0x66        ; 0040A32F  mov dx, word ptr [edi + esi + 0x20]
        _emit 0x8B
        _emit 0x54
        _emit 0x37
        _emit 0x20
        _emit 0x89        ; 0040A334  mov dword ptr [eax + 0x2c], edx
        _emit 0x50
        _emit 0x2C
        _emit 0xA1        ; 0040A337  mov eax, dword ptr [0x4edcd4]
        _emit 0xD4
        _emit 0xDC
        _emit 0x4E
        _emit 0x00
        _emit 0x8B        ; 0040A33C  mov edx, dword ptr [ecx + 0x172]
        _emit 0x91
        _emit 0x72
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x3B        ; 0040A342  cmp edx, eax
        _emit 0xD0
        _emit 0x0F        ; 0040A344  je 0x40a584
        _emit 0x84
        _emit 0x3A
        _emit 0x02
        _emit 0x00
        _emit 0x00
        _emit 0x33        ; 0040A34A  xor edx, edx
        _emit 0xD2
        _emit 0x89        ; 0040A34C  mov dword ptr [ecx + 0x172], eax
        _emit 0x81
        _emit 0x72
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x66        ; 0040A352  mov dx, word ptr [eax*2 + 0x445202]
        _emit 0x8B
        _emit 0x14
        _emit 0x45
        _emit 0x02
        _emit 0x52
        _emit 0x44
        _emit 0x00
        _emit 0x8B        ; 0040A35A  mov eax, dword ptr [ecx + 0x16e]
        _emit 0x81
        _emit 0x6E
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x89        ; 0040A360  mov dword ptr [eax + 0x34], edx
        _emit 0x50
        _emit 0x34
        _emit 0x89        ; 0040A363  mov dword ptr [eax + 0x30], edx
        _emit 0x50
        _emit 0x30
        _emit 0x89        ; 0040A366  mov dword ptr [eax + 0x3c], ebp
        _emit 0x68
        _emit 0x3C
        _emit 0xE9        ; 0040A369  jmp 0x40a567
        _emit 0xF9
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040A36E  mov ebp, dword ptr [esp + 0x18]
        _emit 0x6C
        _emit 0x24
        _emit 0x18
        _emit 0x33        ; 0040A372  xor esi, esi
        _emit 0xF6
        _emit 0x8B        ; 0040A374  mov edx, edi
        _emit 0xD7
        _emit 0xB8        ; 0040A376  mov eax, 0x4edcc4
        _emit 0xC4
        _emit 0xDC
        _emit 0x4E
        _emit 0x00
        _emit 0x83        ; 0040A37B  cmp dword ptr [eax], 0
        _emit 0x38
        _emit 0x00
        _emit 0x74        ; 0040A37E  je 0x40a383
        _emit 0x03
        _emit 0x46        ; 0040A380  inc esi
        _emit 0x8B        ; 0040A381  mov ebp, edx
        _emit 0xEA
        _emit 0x05        ; 0040A383  add eax, 0xe03f
        _emit 0x3F
        _emit 0xE0
        _emit 0x00
        _emit 0x00
        _emit 0x42        ; 0040A388  inc edx
        _emit 0x3D        ; 0040A389  cmp eax, 0x54fe7d
        _emit 0x7D
        _emit 0xFE
        _emit 0x54
        _emit 0x00
        _emit 0x7C        ; 0040A38E  jl 0x40a37b
        _emit 0xEB
        _emit 0x3B        ; 0040A390  cmp esi, edi
        _emit 0xF7
        _emit 0x75        ; 0040A392  jne 0x40a39c
        _emit 0x08
        _emit 0x8B        ; 0040A394  mov ebx, ebp
        _emit 0xDD
        _emit 0x89        ; 0040A396  mov dword ptr [0x4701c4], ebx
        _emit 0x1D
        _emit 0xC4
        _emit 0x01
        _emit 0x47
        _emit 0x00
        _emit 0x8B        ; 0040A39C  mov eax, dword ptr [ecx + 0x176]
        _emit 0x81
        _emit 0x76
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x33        ; 0040A3A2  xor esi, esi
        _emit 0xF6
        _emit 0x3B        ; 0040A3A4  cmp eax, esi
        _emit 0xC6
        _emit 0x74        ; 0040A3A6  je 0x40a3b2
        _emit 0x0A
        _emit 0x39        ; 0040A3A8  cmp dword ptr [eax], edi
        _emit 0x38
        _emit 0x75        ; 0040A3AA  jne 0x40a3b2
        _emit 0x06
        _emit 0x89        ; 0040A3AC  mov dword ptr [ecx + 0x176], esi
        _emit 0xB1
        _emit 0x76
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x83        ; 0040A3B2  cmp ebx, -1
        _emit 0xFB
        _emit 0xFF
        _emit 0x75        ; 0040A3B5  jne 0x40a3d2
        _emit 0x1B
        _emit 0x8B        ; 0040A3B7  mov eax, dword ptr [ecx + 0x176]
        _emit 0x81
        _emit 0x76
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x3B        ; 0040A3BD  cmp eax, esi
        _emit 0xC6
        _emit 0x0F        ; 0040A3BF  je 0x40a4b1
        _emit 0x84
        _emit 0xEC
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x89        ; 0040A3C5  mov dword ptr [eax], edi
        _emit 0x38
        _emit 0x89        ; 0040A3C7  mov dword ptr [ecx + 0x176], esi
        _emit 0xB1
        _emit 0x76
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0xE9        ; 0040A3CD  jmp 0x40a4b1
        _emit 0xDF
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040A3D2  mov edx, dword ptr [ecx + 0x176]
        _emit 0x91
        _emit 0x76
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x3B        ; 0040A3D8  cmp edx, esi
        _emit 0xD6
        _emit 0x74        ; 0040A3DA  je 0x40a429
        _emit 0x4D
        _emit 0x39        ; 0040A3DC  cmp dword ptr [edx + 0x156], ebx
        _emit 0x9A
        _emit 0x56
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x0F        ; 0040A3E2  je 0x40a4b1
        _emit 0x84
        _emit 0xC9
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040A3E8  mov esi, ebx
        _emit 0xF3
        _emit 0x33        ; 0040A3EA  xor eax, eax
        _emit 0xC0
        _emit 0x69        ; 0040A3EC  imul esi, esi, 0xe03f
        _emit 0xF6
        _emit 0x3F
        _emit 0xE0
        _emit 0x00
        _emit 0x00
        _emit 0x89        ; 0040A3F2  mov dword ptr [edx + 0x156], ebx
        _emit 0x9A
        _emit 0x56
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x66        ; 0040A3F8  mov ax, word ptr [esi + 0x4d9326]
        _emit 0x8B
        _emit 0x86
        _emit 0x26
        _emit 0x93
        _emit 0x4D
        _emit 0x00
        _emit 0x89        ; 0040A3FF  mov dword ptr [edx + 0x34], eax
        _emit 0x42
        _emit 0x34
        _emit 0x89        ; 0040A402  mov dword ptr [edx + 0x30], eax
        _emit 0x42
        _emit 0x30
        _emit 0x8D        ; 0040A405  lea ebp, [eax + eax*4]
        _emit 0x2C
        _emit 0x80
        _emit 0xC1        ; 0040A408  shl ebp, 3
        _emit 0xE5
        _emit 0x03
        _emit 0x2B        ; 0040A40B  sub ebp, eax
        _emit 0xE8
        _emit 0x8B        ; 0040A40D  mov eax, dword ptr [esi + 0x4d1e90]
        _emit 0x86
        _emit 0x90
        _emit 0x1E
        _emit 0x4D
        _emit 0x00
        _emit 0x33        ; 0040A413  xor esi, esi
        _emit 0xF6
        _emit 0x66        ; 0040A415  mov si, word ptr [eax + ebp + 0x20]
        _emit 0x8B
        _emit 0x74
        _emit 0x28
        _emit 0x20
        _emit 0xC7        ; 0040A41A  mov dword ptr [edx + 0x10], 0
        _emit 0x42
        _emit 0x10
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x89        ; 0040A421  mov dword ptr [edx + 0x2c], esi
        _emit 0x72
        _emit 0x2C
        _emit 0xE9        ; 0040A424  jmp 0x40a4b1
        _emit 0x88
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040A429  mov edx, dword ptr [0x433350]
        _emit 0x15
        _emit 0x50
        _emit 0x33
        _emit 0x43
        _emit 0x00
        _emit 0xA1        ; 0040A42F  mov eax, dword ptr [0x433354]
        _emit 0x54
        _emit 0x33
        _emit 0x43
        _emit 0x00
        _emit 0x33        ; 0040A434  xor ecx, ecx
        _emit 0xC9
        _emit 0x66        ; 0040A436  mov cx, word ptr [edx + 0xb3f]
        _emit 0x8B
        _emit 0x8A
        _emit 0x3F
        _emit 0x0B
        _emit 0x00
        _emit 0x00
        _emit 0xC1        ; 0040A43D  shl ecx, 4
        _emit 0xE1
        _emit 0x04
        _emit 0x03        ; 0040A440  add ecx, eax
        _emit 0xC8
        _emit 0x0F        ; 0040A442  movsx eax, word ptr [ecx + 1]
        _emit 0xBF
        _emit 0x41
        _emit 0x01
        _emit 0x0F        ; 0040A446  movsx ecx, word ptr [ecx + 3]
        _emit 0xBF
        _emit 0x49
        _emit 0x03
        _emit 0xC1        ; 0040A44A  shl ecx, 0x10
        _emit 0xE1
        _emit 0x10
        _emit 0xC1        ; 0040A44D  shl eax, 0x10
        _emit 0xE0
        _emit 0x10
        _emit 0x51        ; 0040A450  push ecx
        _emit 0x50        ; 0040A451  push eax
        _emit 0x6A        ; 0040A452  push 0x65
        _emit 0x65
        _emit 0x6A        ; 0040A454  push 4
        _emit 0x04
        _emit 0xE8        ; 0040A456  call 0x406570
        _emit 0x15
        _emit 0xC1
        _emit 0xFF
        _emit 0xFF
        _emit 0x8B        ; 0040A45B  mov ebx, dword ptr [0x4701c4]
        _emit 0x1D
        _emit 0xC4
        _emit 0x01
        _emit 0x47
        _emit 0x00
        _emit 0x33        ; 0040A461  xor edx, edx
        _emit 0xD2
        _emit 0x8B        ; 0040A463  mov esi, ebx
        _emit 0xF3
        _emit 0x8B        ; 0040A465  mov ecx, dword ptr [0x4cfa00]
        _emit 0x0D
        _emit 0x00
        _emit 0xFA
        _emit 0x4C
        _emit 0x00
        _emit 0x69        ; 0040A46B  imul esi, esi, 0xe03f
        _emit 0xF6
        _emit 0x3F
        _emit 0xE0
        _emit 0x00
        _emit 0x00
        _emit 0x83        ; 0040A471  add esp, 0x10
        _emit 0xC4
        _emit 0x10
        _emit 0x89        ; 0040A474  mov dword ptr [ecx + 0x176], eax
        _emit 0x81
        _emit 0x76
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x66        ; 0040A47A  mov dx, word ptr [esi + 0x4d9326]
        _emit 0x8B
        _emit 0x96
        _emit 0x26
        _emit 0x93
        _emit 0x4D
        _emit 0x00
        _emit 0x89        ; 0040A481  mov dword ptr [eax + 0x15a], edi
        _emit 0xB8
        _emit 0x5A
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x89        ; 0040A487  mov dword ptr [eax + 0x34], edx
        _emit 0x50
        _emit 0x34
        _emit 0x89        ; 0040A48A  mov dword ptr [eax + 0x30], edx
        _emit 0x50
        _emit 0x30
        _emit 0x8D        ; 0040A48D  lea ebp, [edx + edx*4]
        _emit 0x2C
        _emit 0x92
        _emit 0x89        ; 0040A490  mov dword ptr [eax + 0x156], ebx
        _emit 0x98
        _emit 0x56
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0xC1        ; 0040A496  shl ebp, 3
        _emit 0xE5
        _emit 0x03
        _emit 0x2B        ; 0040A499  sub ebp, edx
        _emit 0xEA
        _emit 0x8B        ; 0040A49B  mov edx, dword ptr [esi + 0x4d1e90]
        _emit 0x96
        _emit 0x90
        _emit 0x1E
        _emit 0x4D
        _emit 0x00
        _emit 0x33        ; 0040A4A1  xor esi, esi
        _emit 0xF6
        _emit 0x89        ; 0040A4A3  mov dword ptr [eax + 0x5c], edi
        _emit 0x78
        _emit 0x5C
        _emit 0x66        ; 0040A4A6  mov si, word ptr [edx + ebp + 0x20]
        _emit 0x8B
        _emit 0x74
        _emit 0x2A
        _emit 0x20
        _emit 0x89        ; 0040A4AB  mov dword ptr [ecx + 8], edi
        _emit 0x79
        _emit 0x08
        _emit 0x89        ; 0040A4AE  mov dword ptr [eax + 0x2c], esi
        _emit 0x70
        _emit 0x2C
        _emit 0xA1        ; 0040A4B1  mov eax, dword ptr [0x4dfc95]
        _emit 0x95
        _emit 0xFC
        _emit 0x4D
        _emit 0x00
        _emit 0x8B        ; 0040A4B6  mov edx, dword ptr [ecx + 0x16a]
        _emit 0x91
        _emit 0x6A
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040A4BC  mov esi, dword ptr [0x433350]
        _emit 0x35
        _emit 0x50
        _emit 0x33
        _emit 0x43
        _emit 0x00
        _emit 0x3B        ; 0040A4C2  cmp edx, eax
        _emit 0xD0
        _emit 0x74        ; 0040A4C4  je 0x40a4fe
        _emit 0x38
        _emit 0x33        ; 0040A4C6  xor edx, edx
        _emit 0xD2
        _emit 0x89        ; 0040A4C8  mov dword ptr [ecx + 0x16a], eax
        _emit 0x81
        _emit 0x6A
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x66        ; 0040A4CE  mov dx, word ptr [eax*2 + 0x445202]
        _emit 0x8B
        _emit 0x14
        _emit 0x45
        _emit 0x02
        _emit 0x52
        _emit 0x44
        _emit 0x00
        _emit 0x8B        ; 0040A4D6  mov eax, dword ptr [ecx + 0x166]
        _emit 0x81
        _emit 0x66
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x89        ; 0040A4DC  mov dword ptr [eax + 0x34], edx
        _emit 0x50
        _emit 0x34
        _emit 0x89        ; 0040A4DF  mov dword ptr [eax + 0x30], edx
        _emit 0x50
        _emit 0x30
        _emit 0xC7        ; 0040A4E2  mov dword ptr [eax + 0x3c], 0
        _emit 0x40
        _emit 0x3C
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040A4E9  mov edx, dword ptr [eax + 0x30]
        _emit 0x50
        _emit 0x30
        _emit 0x8D        ; 0040A4EC  lea ebp, [edx + edx*4]
        _emit 0x2C
        _emit 0x92
        _emit 0xC1        ; 0040A4EF  shl ebp, 3
        _emit 0xE5
        _emit 0x03
        _emit 0x2B        ; 0040A4F2  sub ebp, edx
        _emit 0xEA
        _emit 0x33        ; 0040A4F4  xor edx, edx
        _emit 0xD2
        _emit 0x66        ; 0040A4F6  mov dx, word ptr [esi + ebp + 0x20]
        _emit 0x8B
        _emit 0x54
        _emit 0x2E
        _emit 0x20
        _emit 0x89        ; 0040A4FB  mov dword ptr [eax + 0x2c], edx
        _emit 0x50
        _emit 0x2C
        _emit 0xA1        ; 0040A4FE  mov eax, dword ptr [0x470058]
        _emit 0x58
        _emit 0x00
        _emit 0x47
        _emit 0x00
        _emit 0x85        ; 0040A503  test eax, eax
        _emit 0xC0
        _emit 0x75        ; 0040A505  jne 0x40a52e
        _emit 0x27
        _emit 0x83        ; 0040A507  or eax, 0xffffffff
        _emit 0xC8
        _emit 0xFF
        _emit 0x3B        ; 0040A50A  cmp ebx, eax
        _emit 0xD8
        _emit 0x75        ; 0040A50C  jne 0x40a52c
        _emit 0x1E
        _emit 0x8B        ; 0040A50E  mov edx, dword ptr [ecx + 0x16e]
        _emit 0x91
        _emit 0x6E
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x89        ; 0040A514  mov dword ptr [ecx + 0x172], eax
        _emit 0x81
        _emit 0x72
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0xC7        ; 0040A51A  mov dword ptr [edx + 0x10], 0
        _emit 0x42
        _emit 0x10
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040A521  mov edx, dword ptr [ecx + 0x16e]
        _emit 0x91
        _emit 0x6E
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x89        ; 0040A527  mov dword ptr [edx + 0x3c], eax
        _emit 0x42
        _emit 0x3C
        _emit 0xEB        ; 0040A52A  jmp 0x40a584
        _emit 0x58
        _emit 0x8B        ; 0040A52C  mov edi, ebx
        _emit 0xFB
        _emit 0x69        ; 0040A52E  imul edi, edi, 0xe03f
        _emit 0xFF
        _emit 0x3F
        _emit 0xE0
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040A534  mov edx, dword ptr [ecx + 0x172]
        _emit 0x91
        _emit 0x72
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040A53A  mov eax, dword ptr [edi + 0x4dfc95]
        _emit 0x87
        _emit 0x95
        _emit 0xFC
        _emit 0x4D
        _emit 0x00
        _emit 0x3B        ; 0040A540  cmp edx, eax
        _emit 0xD0
        _emit 0x74        ; 0040A542  je 0x40a584
        _emit 0x40
        _emit 0x33        ; 0040A544  xor edx, edx
        _emit 0xD2
        _emit 0x89        ; 0040A546  mov dword ptr [ecx + 0x172], eax
        _emit 0x81
        _emit 0x72
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x66        ; 0040A54C  mov dx, word ptr [eax*2 + 0x445202]
        _emit 0x8B
        _emit 0x14
        _emit 0x45
        _emit 0x02
        _emit 0x52
        _emit 0x44
        _emit 0x00
        _emit 0x8B        ; 0040A554  mov eax, dword ptr [ecx + 0x16e]
        _emit 0x81
        _emit 0x6E
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x89        ; 0040A55A  mov dword ptr [eax + 0x34], edx
        _emit 0x50
        _emit 0x34
        _emit 0x89        ; 0040A55D  mov dword ptr [eax + 0x30], edx
        _emit 0x50
        _emit 0x30
        _emit 0xC7        ; 0040A560  mov dword ptr [eax + 0x3c], 0
        _emit 0x40
        _emit 0x3C
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040A567  mov edx, dword ptr [eax + 0x30]
        _emit 0x50
        _emit 0x30
        _emit 0x8D        ; 0040A56A  lea edi, [edx + edx*4]
        _emit 0x3C
        _emit 0x92
        _emit 0xC1        ; 0040A56D  shl edi, 3
        _emit 0xE7
        _emit 0x03
        _emit 0x2B        ; 0040A570  sub edi, edx
        _emit 0xFA
        _emit 0x33        ; 0040A572  xor edx, edx
        _emit 0xD2
        _emit 0x66        ; 0040A574  mov dx, word ptr [edi + esi + 0x20]
        _emit 0x8B
        _emit 0x54
        _emit 0x37
        _emit 0x20
        _emit 0x89        ; 0040A579  mov dword ptr [eax + 0x2c], edx
        _emit 0x50
        _emit 0x2C
        _emit 0xEB        ; 0040A57C  jmp 0x40a584
        _emit 0x06
        _emit 0x8B        ; 0040A57E  mov esi, dword ptr [0x433350]
        _emit 0x35
        _emit 0x50
        _emit 0x33
        _emit 0x43
        _emit 0x00
        _emit 0xA1        ; 0040A584  mov eax, dword ptr [0x4dfc6d]
        _emit 0x6D
        _emit 0xFC
        _emit 0x4D
        _emit 0x00
        _emit 0x8B        ; 0040A589  mov edx, dword ptr [ecx + 0x15a]
        _emit 0x91
        _emit 0x5A
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x66        ; 0040A58F  mov di, word ptr [0x445216]
        _emit 0x8B
        _emit 0x3D
        _emit 0x16
        _emit 0x52
        _emit 0x44
        _emit 0x00
        _emit 0x3B        ; 0040A596  cmp edx, eax
        _emit 0xD0
        _emit 0x74        ; 0040A598  je 0x40a5ca
        _emit 0x30
        _emit 0x89        ; 0040A59A  mov dword ptr [ecx + 0x15a], eax
        _emit 0x81
        _emit 0x5A
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040A5A0  mov edx, dword ptr [ecx + 0x156]
        _emit 0x91
        _emit 0x56
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x33        ; 0040A5A6  xor eax, eax
        _emit 0xC0
        _emit 0x66        ; 0040A5A8  mov ax, di
        _emit 0x8B
        _emit 0xC7
        _emit 0x89        ; 0040A5AB  mov dword ptr [edx + 0x34], eax
        _emit 0x42
        _emit 0x34
        _emit 0x89        ; 0040A5AE  mov dword ptr [edx + 0x30], eax
        _emit 0x42
        _emit 0x30
        _emit 0x8D        ; 0040A5B1  lea ebx, [eax + eax*4]
        _emit 0x1C
        _emit 0x80
        _emit 0xC1        ; 0040A5B4  shl ebx, 3
        _emit 0xE3
        _emit 0x03
        _emit 0x2B        ; 0040A5B7  sub ebx, eax
        _emit 0xD8
        _emit 0x33        ; 0040A5B9  xor eax, eax
        _emit 0xC0
        _emit 0x66        ; 0040A5BB  mov ax, word ptr [ebx + esi + 0x20]
        _emit 0x8B
        _emit 0x44
        _emit 0x33
        _emit 0x20
        _emit 0x89        ; 0040A5C0  mov dword ptr [edx + 0x2c], eax
        _emit 0x42
        _emit 0x2C
        _emit 0xC7        ; 0040A5C3  mov dword ptr [edx + 0x3c], 0
        _emit 0x42
        _emit 0x3C
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0xA1        ; 0040A5CA  mov eax, dword ptr [0x4edcac]
        _emit 0xAC
        _emit 0xDC
        _emit 0x4E
        _emit 0x00
        _emit 0x8B        ; 0040A5CF  mov edx, dword ptr [ecx + 0x162]
        _emit 0x91
        _emit 0x62
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x3B        ; 0040A5D5  cmp edx, eax
        _emit 0xD0
        _emit 0x74        ; 0040A5D7  je 0x40a609
        _emit 0x30
        _emit 0x89        ; 0040A5D9  mov dword ptr [ecx + 0x162], eax
        _emit 0x81
        _emit 0x62
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040A5DF  mov ecx, dword ptr [ecx + 0x15e]
        _emit 0x89
        _emit 0x5E
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x33        ; 0040A5E5  xor eax, eax
        _emit 0xC0
        _emit 0x66        ; 0040A5E7  mov ax, di
        _emit 0x8B
        _emit 0xC7
        _emit 0x89        ; 0040A5EA  mov dword ptr [ecx + 0x34], eax
        _emit 0x41
        _emit 0x34
        _emit 0x89        ; 0040A5ED  mov dword ptr [ecx + 0x30], eax
        _emit 0x41
        _emit 0x30
        _emit 0x8D        ; 0040A5F0  lea edx, [eax + eax*4]
        _emit 0x14
        _emit 0x80
        _emit 0xC1        ; 0040A5F3  shl edx, 3
        _emit 0xE2
        _emit 0x03
        _emit 0x2B        ; 0040A5F6  sub edx, eax
        _emit 0xD0
        _emit 0x33        ; 0040A5F8  xor eax, eax
        _emit 0xC0
        _emit 0x66        ; 0040A5FA  mov ax, word ptr [edx + esi + 0x20]
        _emit 0x8B
        _emit 0x44
        _emit 0x32
        _emit 0x20
        _emit 0x89        ; 0040A5FF  mov dword ptr [ecx + 0x2c], eax
        _emit 0x41
        _emit 0x2C
        _emit 0xC7        ; 0040A602  mov dword ptr [ecx + 0x3c], 0
        _emit 0x41
        _emit 0x3C
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x5F        ; 0040A609  pop edi
        _emit 0x5E        ; 0040A60A  pop esi
        _emit 0x5D        ; 0040A60B  pop ebp
        _emit 0x5B        ; 0040A60C  pop ebx
        _emit 0x83        ; 0040A60D  add esp, 0xc
        _emit 0xC4
        _emit 0x0C
        _emit 0xC3        ; 0040A610  ret
        _emit 0x90        ; 0040A611  nop
        _emit 0x90        ; 0040A612  nop
        _emit 0x90        ; 0040A613  nop
        _emit 0x90        ; 0040A614  nop
        _emit 0x90        ; 0040A615  nop
        _emit 0x90        ; 0040A616  nop
        _emit 0x90        ; 0040A617  nop
        _emit 0x90        ; 0040A618  nop
        _emit 0x90        ; 0040A619  nop
        _emit 0x90        ; 0040A61A  nop
        _emit 0x90        ; 0040A61B  nop
        _emit 0x90        ; 0040A61C  nop
        _emit 0x90        ; 0040A61D  nop
        _emit 0x90        ; 0040A61E  nop
        _emit 0x90        ; 0040A61F  nop
    }
}

/*
 * vjmpUpdateTimerAndUi - handler of the UPDATE_TIMER object (created by vjmpHandleBattleInterface): shows
 * the round timer as up to three digit objects (system skills (&shSkillIdxTimeNumber0)[digit]), centred
 * on the timer position (first step of system skill 0x47: x, y and the digit spacing in cParam3), or
 * the "unlimited" sign when the round has no time limit (iGameTimerInFrames < 0).
 * The displayed value is iGameTimerInFrames / 100.  Unused digits are parked at x = 10000 (0x27100000,
 * 16.16), off screen.
 * Work slots: iObjectType / pWork015E / pWork0162 = ones / tens / hundreds digit objects, iWork0166 /
 * iWork016A / iWork016E = the digits shown (-1 = none), iWork0176 = digit spacing (16.16), iPosX/iPosY =
 * timer position (16.16), iPlayerIdx = value shown.
 * Globals read: gkgtGameState.iGameTimerInFrames, gkgtKgtSystem.
 */
void vjmpUpdateTimerAndUi(void)
{
    switch (gpkgtCurrentEngineObject->iProcessStep) {
    case 0:
        gpkgtCurrentEngineObject->iProcessStep = 1;
        if (gkgtGameState.iGameTimerInFrames < 0) {
            gpkgtCurrentEngineObject->iProcessStep = 2;
            return;
        }
        /* three digits, drawn by script objects; parked off screen, with a very long image wait so
           that they don't run on until a digit skill is set */
        gpkgtCurrentEngineObject->pWork0162 = kgtoNewEngineObject(READ_SCRIPT, 0x65, 0, 0);
        gpkgtCurrentEngineObject->pWork0162->iObjectType = SYSTEM_ENGINE_OBJECT;
        gpkgtCurrentEngineObject->pWork0162->iPosX = 0x27100000;
        gpkgtCurrentEngineObject->pWork015E = kgtoNewEngineObject(READ_SCRIPT, 0x65, 0, 0);
        gpkgtCurrentEngineObject->pWork015E->iObjectType = SYSTEM_ENGINE_OBJECT;
        gpkgtCurrentEngineObject->pWork015E->iPosX = 0x27100000;
        gpkgtCurrentEngineObject->iObjectType = (int)kgtoNewEngineObject(READ_SCRIPT, 0x65, 0, 0);
        OBJ(gpkgtCurrentEngineObject->iObjectType)->iObjectType = SYSTEM_ENGINE_OBJECT;
        OBJ(gpkgtCurrentEngineObject->iObjectType)->iPosX = 0x27100000;
        OBJ(gpkgtCurrentEngineObject->iObjectType)->iImageWaitFrames = 1000000;
        gpkgtCurrentEngineObject->pWork015E->iImageWaitFrames = 1000000;
        gpkgtCurrentEngineObject->pWork0162->iImageWaitFrames = 1000000;
        gpkgtCurrentEngineObject->iWork016E = -1;
        gpkgtCurrentEngineObject->iWork016A = -1;
        gpkgtCurrentEngineObject->iWork0166 = -1;
        /* timer position and digit spacing */
        gpkgtCurrentEngineObject->iPosX = SYS_STEP(0x47).shParam1 << 16;
        gpkgtCurrentEngineObject->iPosY = SYS_STEP(0x47).shParam2 << 16;
        gpkgtCurrentEngineObject->iWork0176 = (BYTE)SYS_STEP(0x47).cParam3 << 16;
        OBJ(gpkgtCurrentEngineObject->iObjectType)->iPosY = gpkgtCurrentEngineObject->iPosY;
        gpkgtCurrentEngineObject->pWork015E->iPosY = gpkgtCurrentEngineObject->iPosY;
        gpkgtCurrentEngineObject->pWork0162->iPosY = gpkgtCurrentEngineObject->iPosY;
        break;
    case 1:
        break;
    case 2:
        /* no time limit: show the "unlimited" sign */
        gpkgtCurrentEngineObject->iProcessStep = 3;
        gpkgtCurrentEngineObject->pWork015E = kgtoNewEngineObject(READ_SCRIPT, 0x65, 0, 0);
        gpkgtCurrentEngineObject->pWork015E->iObjectType = SYSTEM_ENGINE_OBJECT;
        gpkgtCurrentEngineObject->pWork015E->iPosX = SYS_STEP(0x47).shParam1 << 16;
        gpkgtCurrentEngineObject->pWork015E->iPosY = SYS_STEP(0x47).shParam2 << 16;
        gpkgtCurrentEngineObject->pWork015E->iSkillIdx = gpkgtCurrentEngineObject->pWork015E->iStartSkillIdx = (WORD)gkgtKgtSystem.shSkillIdxUnlimitedSign;
        gpkgtCurrentEngineObject->pWork015E->iSkillScriptIdx = (WORD)gkgtKgtSystem.kgtCore.pSkillsAlloc[gpkgtCurrentEngineObject->pWork015E->iSkillIdx].shStartingStepIdx;
        return;
    default:
        return;
    }

    /* value to show */
    if (gkgtGameState.iGameTimerInFrames < 0)
        gpkgtCurrentEngineObject->iPlayerIdx = 0;
    else
        gpkgtCurrentEngineObject->iPlayerIdx = gkgtGameState.iGameTimerInFrames / 100;

    /* one digit: centred; two: half a spacing left and right of the centre; three: -1, 0, +1
       spacing.  A digit object is restarted only when its digit changes. */
    if (gpkgtCurrentEngineObject->iPlayerIdx < 10) {
        if (gpkgtCurrentEngineObject->iPlayerIdx % 10 != gpkgtCurrentEngineObject->iWork0166) {
            gpkgtCurrentEngineObject->iWork0166 = gpkgtCurrentEngineObject->iPlayerIdx % 10;
            OBJ(gpkgtCurrentEngineObject->iObjectType)->iPosX = gpkgtCurrentEngineObject->iPosX;
            OBJ(gpkgtCurrentEngineObject->iObjectType)->iSkillIdx = OBJ(gpkgtCurrentEngineObject->iObjectType)->iStartSkillIdx = (WORD)(&gkgtKgtSystem.shSkillIdxTimeNumber0)[gpkgtCurrentEngineObject->iPlayerIdx % 10];
            OBJ(gpkgtCurrentEngineObject->iObjectType)->iImageWaitFrames = 0;
            OBJ(gpkgtCurrentEngineObject->iObjectType)->iSkillScriptIdx = (WORD)gkgtKgtSystem.kgtCore.pSkillsAlloc[OBJ(gpkgtCurrentEngineObject->iObjectType)->iSkillIdx].shStartingStepIdx;
        }
        gpkgtCurrentEngineObject->pWork015E->iPosX = 0x27100000;
        gpkgtCurrentEngineObject->pWork0162->iPosX = 0x27100000;
    } else if (gpkgtCurrentEngineObject->iPlayerIdx < 100) {
        if (gpkgtCurrentEngineObject->iPlayerIdx % 10 != gpkgtCurrentEngineObject->iWork0166) {
            gpkgtCurrentEngineObject->iWork0166 = gpkgtCurrentEngineObject->iPlayerIdx % 10;
            OBJ(gpkgtCurrentEngineObject->iObjectType)->iPosX = gpkgtCurrentEngineObject->iWork0176 / 2 + gpkgtCurrentEngineObject->iPosX;
            OBJ(gpkgtCurrentEngineObject->iObjectType)->iSkillIdx = OBJ(gpkgtCurrentEngineObject->iObjectType)->iStartSkillIdx = (WORD)(&gkgtKgtSystem.shSkillIdxTimeNumber0)[gpkgtCurrentEngineObject->iPlayerIdx % 10];
            OBJ(gpkgtCurrentEngineObject->iObjectType)->iImageWaitFrames = 0;
            OBJ(gpkgtCurrentEngineObject->iObjectType)->iSkillScriptIdx = (WORD)gkgtKgtSystem.kgtCore.pSkillsAlloc[OBJ(gpkgtCurrentEngineObject->iObjectType)->iSkillIdx].shStartingStepIdx;
        }
        if (gpkgtCurrentEngineObject->iPlayerIdx / 10 % 10 != gpkgtCurrentEngineObject->iWork016A) {
            gpkgtCurrentEngineObject->iWork016A = gpkgtCurrentEngineObject->iPlayerIdx / 10 % 10;
            gpkgtCurrentEngineObject->pWork015E->iPosX = gpkgtCurrentEngineObject->iPosX - gpkgtCurrentEngineObject->iWork0176 / 2;
            gpkgtCurrentEngineObject->pWork015E->iSkillIdx = gpkgtCurrentEngineObject->pWork015E->iStartSkillIdx = (WORD)(&gkgtKgtSystem.shSkillIdxTimeNumber0)[gpkgtCurrentEngineObject->iPlayerIdx / 10 % 10];
            gpkgtCurrentEngineObject->pWork015E->iImageWaitFrames = 0;
            gpkgtCurrentEngineObject->pWork015E->iSkillScriptIdx = (WORD)gkgtKgtSystem.kgtCore.pSkillsAlloc[gpkgtCurrentEngineObject->pWork015E->iSkillIdx].shStartingStepIdx;
        }
        gpkgtCurrentEngineObject->pWork0162->iPosX = 0x27100000;
    } else {
        if (gpkgtCurrentEngineObject->iPlayerIdx % 10 != gpkgtCurrentEngineObject->iWork0166) {
            gpkgtCurrentEngineObject->iWork0166 = gpkgtCurrentEngineObject->iPlayerIdx % 10;
            OBJ(gpkgtCurrentEngineObject->iObjectType)->iPosX = gpkgtCurrentEngineObject->iWork0176 + gpkgtCurrentEngineObject->iPosX;
            OBJ(gpkgtCurrentEngineObject->iObjectType)->iSkillIdx = OBJ(gpkgtCurrentEngineObject->iObjectType)->iStartSkillIdx = (WORD)(&gkgtKgtSystem.shSkillIdxTimeNumber0)[gpkgtCurrentEngineObject->iPlayerIdx % 10];
            OBJ(gpkgtCurrentEngineObject->iObjectType)->iImageWaitFrames = 0;
            OBJ(gpkgtCurrentEngineObject->iObjectType)->iSkillScriptIdx = (WORD)gkgtKgtSystem.kgtCore.pSkillsAlloc[OBJ(gpkgtCurrentEngineObject->iObjectType)->iSkillIdx].shStartingStepIdx;
        }
        if (gpkgtCurrentEngineObject->iPlayerIdx / 10 % 10 != gpkgtCurrentEngineObject->iWork016A) {
            gpkgtCurrentEngineObject->iWork016A = gpkgtCurrentEngineObject->iPlayerIdx / 10 % 10;
            gpkgtCurrentEngineObject->pWork015E->iPosX = gpkgtCurrentEngineObject->iPosX;
            gpkgtCurrentEngineObject->pWork015E->iSkillIdx = gpkgtCurrentEngineObject->pWork015E->iStartSkillIdx = (WORD)(&gkgtKgtSystem.shSkillIdxTimeNumber0)[gpkgtCurrentEngineObject->iPlayerIdx / 10 % 10];
            gpkgtCurrentEngineObject->pWork015E->iImageWaitFrames = 0;
            gpkgtCurrentEngineObject->pWork015E->iSkillScriptIdx = (WORD)gkgtKgtSystem.kgtCore.pSkillsAlloc[gpkgtCurrentEngineObject->pWork015E->iSkillIdx].shStartingStepIdx;
        }
        if (gpkgtCurrentEngineObject->iPlayerIdx / 100 % 10 != gpkgtCurrentEngineObject->iWork016E) {
            gpkgtCurrentEngineObject->iWork016E = gpkgtCurrentEngineObject->iPlayerIdx / 100 % 10;
            gpkgtCurrentEngineObject->pWork0162->iPosX = gpkgtCurrentEngineObject->iPosX - gpkgtCurrentEngineObject->iWork0176;
            gpkgtCurrentEngineObject->pWork0162->iSkillIdx = gpkgtCurrentEngineObject->pWork0162->iStartSkillIdx = (WORD)(&gkgtKgtSystem.shSkillIdxTimeNumber0)[gpkgtCurrentEngineObject->iPlayerIdx / 100 % 10];
            gpkgtCurrentEngineObject->pWork0162->iImageWaitFrames = 0;
            gpkgtCurrentEngineObject->pWork0162->iSkillScriptIdx = (WORD)gkgtKgtSystem.kgtCore.pSkillsAlloc[gpkgtCurrentEngineObject->pWork0162->iSkillIdx].shStartingStepIdx;
        }
    }
}

/*
 * vjmpHitComboCounter - handler of HIT_COMBO_COUNTER objects (created by the hit code with the combo
 * position in iPosX/iPosY): shows "N hits" as the "hit" letters (system skill shSkillIdxHitLetterHit)
 * and one digit object per decimal digit ((&shSkillIdxHitNumber0)[digit]), all on layer 101 and
 * camera-relative.  The digit layout comes from the first step of system skill 1: byte 1 bit 0 =
 * digits extend to the right of the position (else to the left), byte 2 = digit width in pixels.
 * The counter ends itself 200 frames after it was created, or at once (count 1000) when the live hit
 * count has grown past the value it shows (a newer counter takes over).
 * iPlayerIdx = number of hits shown, pWork015E -> the live hit count (an int), pWork0162 = the "hit"
 * letters, iObjectType = frames shown.
 * matching: iPlayerIdx is read twice, into uHits and uRest, and the statement order below decides the
 * register allocation.
 * Globals read: gkgtKgtSystem.
 */
void vjmpHitComboCounter(void)
{
    kgtEngineObject *pObj;
    unsigned int uHits, uRest;  /* the count / the digits still to create */
    int iDigits;                /* number of digits - 1, then digits left to create */
    int iLayoutFlags;           /* byte 1 of the layout step */
    int iDigitWidth;            /* digit spacing (16.16) */

    switch (gpkgtCurrentEngineObject->iProcessStep) {
    case 0:
        gpkgtCurrentEngineObject->iProcessStep = 1;
        pObj = kgtoNewObjectForSkillIdx((unsigned short)gkgtKgtSystem.shSkillIdxHitLetterHit, 101,
                                        gpkgtCurrentEngineObject->iPosX, gpkgtCurrentEngineObject->iPosY);
        gpkgtCurrentEngineObject->pWork0162 = pObj;
        pObj->iFlags |= 0x40000000;
        /* layout of the digits from the first step of system skill 1 (see above) */
        uHits = gpkgtCurrentEngineObject->iPlayerIdx;
        iLayoutFlags = ((BYTE *)&SYS_STEP(1))[1];
        uRest = gpkgtCurrentEngineObject->iPlayerIdx;
        iDigitWidth = ((BYTE *)&SYS_STEP(1))[2] << 16;
        iDigits = 0;
        while (uRest > 9) {
            uRest /= 10;
            iDigits++;
        }
        /* digits are created right to left (lowest first), so start at the rightmost one */
        if (iLayoutFlags & 1)
            gpkgtCurrentEngineObject->iPosX += iDigits * iDigitWidth;
        uRest = uHits;
        for (iDigits++; iDigits != 0; iDigits--) {
            pObj = kgtoNewObjectForSkillIdx((unsigned short)(&gkgtKgtSystem.shSkillIdxHitNumber0)[uRest % 10], 101,
                                            gpkgtCurrentEngineObject->iPosX, gpkgtCurrentEngineObject->iPosY);
            pObj->iFlags |= 0x40000000;
            uRest /= 10;
            gpkgtCurrentEngineObject->iPosX -= iDigitWidth;
        }
        break;
    case 1:
        break;
    default:
        return;
    }
    /* the combo went on: this counter is out of date */
    if (gpkgtCurrentEngineObject->iPlayerIdx < *(int *)gpkgtCurrentEngineObject->pWork015E)
        gpkgtCurrentEngineObject->iObjectType = 1000;
    if (++gpkgtCurrentEngineObject->iObjectType > 200)
        gpkgtCurrentEngineObject->iJumpIdx = RESET_IDX;
}

/*
 * vjmpFadeDriftEffect - handler of FADE_DRIFT_EFFECT objects: a 31-frame animated effect from text.bmp
 * (iDrawFlag -3: 128x128 frame (iPlayerIdx / 8) of row iObjectType, additive blend, at screen pixel
 * iPosX/iPosY minus the camera) that drifts sideways by iXMomentum pixels per frame and fades: type 1
 * loses blue and green (turns red), type 2 red and green (turns blue).
 * iObjectType: effect type (1, 2); iPlayerLookingRight mirrors it (type 1 then drifts the other way).
 */
void vjmpFadeDriftEffect(void)
{
    switch (gpkgtCurrentEngineObject->iProcessStep) {
    case 0:
        gpkgtCurrentEngineObject->iProcessStep = 1;
        gpkgtCurrentEngineObject->iDrawFlag = -3;
        switch (gpkgtCurrentEngineObject->iObjectType) {
        case 1:
            gpkgtCurrentEngineObject->iXMomentum = 3;
            if (gpkgtCurrentEngineObject->iPlayerLookingRight)
                gpkgtCurrentEngineObject->iXMomentum = -3;
            break;
        case 2:
            gpkgtCurrentEngineObject->iXMomentum = -1;
            if (gpkgtCurrentEngineObject->iPlayerLookingRight)
                gpkgtCurrentEngineObject->iXMomentum = -1;
            break;
        }
        break;
    case 1:
        break;
    default:
        return;
    }
    /* fade, count the frames (iPlayerIdx) and drift */
    switch (gpkgtCurrentEngineObject->iObjectType) {
    case 1:
        gpkgtCurrentEngineObject->iColorBlue--;
        gpkgtCurrentEngineObject->iColorGreen--;
        break;
    case 2:
        gpkgtCurrentEngineObject->iColorRed--;
        gpkgtCurrentEngineObject->iColorGreen--;
        break;
    }
    if (++gpkgtCurrentEngineObject->iPlayerIdx >= 31)
        gpkgtCurrentEngineObject->iJumpIdx = RESET_IDX;
    gpkgtCurrentEngineObject->iPosX += gpkgtCurrentEngineObject->iXMomentum;
}

/*
 * vjmpDisplayTitleScreens - handler of the DISPLAY_TITLE_SCREEN object (normal game start): shows the
 * three start-up pictures 1.bmp, 2.bmp and 3.bmp (resources RC_BMP_01..03, or files) as bitmap 3 for 300
 * frames each (any key skips), fading each out over 10 frames by lowering the tint (red and green 2,
 * blue 1 per frame, from -20), then creates the title menu (MENU_TRAVERSAL).
 * Steps: 0 first picture, odd steps show, even steps fade and load the next one.  iPlayerIdx is the
 * frame counter.
 * Globals read: giAnyInputXor.  Globals changed: gkgtGameState.iGameStateNumber (1000), gkgtBitmaps[3].
 */
void vjmpDisplayTitleScreens(void)
{
    switch (gpkgtCurrentEngineObject->iProcessStep) {
    case 0:
        gkgtGameState.iGameStateNumber = 1000;
        vResetObjectsAndSpeed();
        gpkgtCurrentEngineObject->iProcessStep++;
        gpkgtCurrentEngineObject->iDrawFlag = 3;
        gpkgtCurrentEngineObject->iColorRed = gpkgtCurrentEngineObject->iColorGreen = gpkgtCurrentEngineObject->iColorBlue = 0;
        iLoadExternalImage(&gkgtBitmaps[gpkgtCurrentEngineObject->iDrawFlag], "RC_BMP_01", "1.bmp", 0);
        gpkgtCurrentEngineObject->iPlayerIdx = 300;
        gpkgtCurrentEngineObject->iColorBlendtype = 5;     /* blend mode 5: opaque copy */
        break;
    case 1:
        if (giAnyInputXor)
            gpkgtCurrentEngineObject->iPlayerIdx = 0;
        if (--gpkgtCurrentEngineObject->iPlayerIdx <= 0) {
            gpkgtCurrentEngineObject->iProcessStep++;
            gpkgtCurrentEngineObject->iPlayerIdx = 10;
            gpkgtCurrentEngineObject->iColorRed = gpkgtCurrentEngineObject->iColorGreen = gpkgtCurrentEngineObject->iColorBlue = -20;
        }
        break;
    case 2:
        gpkgtCurrentEngineObject->iColorRed -= 2;
        gpkgtCurrentEngineObject->iColorGreen -= 2;
        gpkgtCurrentEngineObject->iColorBlue--;
        if (--gpkgtCurrentEngineObject->iPlayerIdx <= 0) {
            gpkgtCurrentEngineObject->iProcessStep++;
            gpkgtCurrentEngineObject->iDrawFlag = 3;
            gpkgtCurrentEngineObject->iColorRed = gpkgtCurrentEngineObject->iColorGreen = gpkgtCurrentEngineObject->iColorBlue = 0;
            iLoadExternalImage(&gkgtBitmaps[gpkgtCurrentEngineObject->iDrawFlag], "RC_BMP_02", "2.bmp", 0);
            gpkgtCurrentEngineObject->iPlayerIdx = 300;
        }
        break;
    case 3:
        if (giAnyInputXor)
            gpkgtCurrentEngineObject->iPlayerIdx = 0;
        if (--gpkgtCurrentEngineObject->iPlayerIdx <= 0) {
            gpkgtCurrentEngineObject->iProcessStep++;
            gpkgtCurrentEngineObject->iPlayerIdx = 10;
            gpkgtCurrentEngineObject->iColorRed = gpkgtCurrentEngineObject->iColorGreen = gpkgtCurrentEngineObject->iColorBlue = -20;
        }
        break;
    case 4:
        gpkgtCurrentEngineObject->iColorRed -= 2;
        gpkgtCurrentEngineObject->iColorGreen -= 2;
        gpkgtCurrentEngineObject->iColorBlue--;
        if (--gpkgtCurrentEngineObject->iPlayerIdx <= 0) {
            gpkgtCurrentEngineObject->iProcessStep++;
            gpkgtCurrentEngineObject->iDrawFlag = 3;
            gpkgtCurrentEngineObject->iColorRed = gpkgtCurrentEngineObject->iColorGreen = gpkgtCurrentEngineObject->iColorBlue = 0;
            iLoadExternalImage(&gkgtBitmaps[gpkgtCurrentEngineObject->iDrawFlag], "RC_BMP_03", "3.bmp", 0);
            gpkgtCurrentEngineObject->iPlayerIdx = 300;
        }
        break;
    case 5:
        if (giAnyInputXor)
            gpkgtCurrentEngineObject->iPlayerIdx = 0;
        if (--gpkgtCurrentEngineObject->iPlayerIdx <= 0) {
            gpkgtCurrentEngineObject->iProcessStep++;
            gpkgtCurrentEngineObject->iPlayerIdx = 10;
            gpkgtCurrentEngineObject->iColorRed = gpkgtCurrentEngineObject->iColorGreen = gpkgtCurrentEngineObject->iColorBlue = -20;
        }
        break;
    case 6:
        gpkgtCurrentEngineObject->iColorRed -= 2;
        gpkgtCurrentEngineObject->iColorGreen -= 2;
        gpkgtCurrentEngineObject->iColorBlue--;
        if (--gpkgtCurrentEngineObject->iPlayerIdx <= 0) {
            gpkgtCurrentEngineObject->iProcessStep++;
            kgtoNewEngineObject(MENU_TRAVERSAL, 127, 0, 0);
        }
        break;
    }
}

/*
 * vjmpScreenControl - handler of SCREEN_CONTROL objects: background, camera and full-screen effects.
 * The object's start step (set by its creator) selects what it is:
 *    0        nothing
 *    10/11    stage background bitmap bg_001_0.bmp as bitmap iDepth (drawn by vDrawCurrentEngineObject)
 *    12/13    the camera: follows the fighters (story: the player, or everybody with the WALL option;
 *             versus: the midpoint of the two players), smoothed, clamped to 0..640 x 0..480; the
 *             object's position is the negative camera (16.16).  The oscillation it runs in
 *             iPosX/iPlayerIdx first has no effect (iPosX is overwritten).
 *    14/15    the shadows of the script objects (iDrawFlag -2)
 *    16/17    a pulsing overlay: text.bmp's top left 512x512 in blend mode 4 (iDrawFlag -4) with a weight of 0..30 changing every 4 frames
 *    20/21    bitmap iDepth with random-walk values in the work slots (reset to 32 each frame)
 *    30/31    screen blur (iDrawFlag -10) of strength iPlayerIdx going up and down between 1 and 99
 * Globals read: gkgtGameState.kgtGameMode, gkgtLoadedCharacter (positions, health), gkgtEngineObjects,
 * giCurrentStoryStep[], giStoryModeSide.  Globals changed: giCameraX, giCameraY, gkgtBitmaps[iDepth].
 */
void vjmpScreenControl(void)
{
    int i, iCount;      /* iCount: fighters averaged */
    int iSumX, iSumY;   /* sums of their positions (pixels); iSumY is not used */
    int iP1X, iCenterX; /* story: player x / camera centre x (pixels) */
    kgtEngineObject *pObj;
    kgt_character_struct *pChar;

    switch (gpkgtCurrentEngineObject->iProcessStep) {
    case 0:
        gpkgtCurrentEngineObject->iProcessStep = 1;
        return;
    case 10:    /* stage background */
        gpkgtCurrentEngineObject->iProcessStep = 11;
        gpkgtCurrentEngineObject->iDrawFlag = gpkgtCurrentEngineObject->iDepth;
        iLoadExternalImage(&gkgtBitmaps[gpkgtCurrentEngineObject->iDrawFlag], "bg_001_0.bmp", gszEmptyBgFile, 0);
        gpkgtCurrentEngineObject->iPosX = 0;
        gpkgtCurrentEngineObject->iPosY = 0;
        return;
    case 12:    /* camera */
        gpkgtCurrentEngineObject->iPosX = 320;
        gpkgtCurrentEngineObject->iPosY = 0;
        if (++gpkgtCurrentEngineObject->iPlayerIdx <= 0)
            break;
        gpkgtCurrentEngineObject->iPlayerIdx = 0;
        gpkgtCurrentEngineObject->iProcessStep++;
        /* fall through */
    case 13:
        /* oscillation around x = -320 (16.16); no effect, iPosX is overwritten below */
        if (gpkgtCurrentEngineObject->iPosX < -0x1400000)
            gpkgtCurrentEngineObject->iPlayerIdx += 1000;
        else
            gpkgtCurrentEngineObject->iPlayerIdx -= 1000;
        gpkgtCurrentEngineObject->iPosX += gpkgtCurrentEngineObject->iPlayerIdx;
        switch (gkgtGameState.kgtGameMode) {
        case 0:     /* story: y follows the player (halfway per frame) */
            giCameraY = (gkgtLoadedCharacter[0].iCurrentYPos / 0x10000 - 320 + giCameraY) / 2;
            if (gkgtLoadedCharacter[0].kgtStoryEntries[giCurrentStoryStep[giStoryModeSide]].cOptionsBitmask & 4) {
                /* WALL: centre on everybody still alive */
                iSumX = 0;
                iSumY = 0;
                iCount = 0;
                for (i = 0; i < 8; i++) {
                    pChar = &gkgtLoadedCharacter[i];
                    if (pChar->iHealth) {
                        iSumX += pChar->iCurrentXPos / 0x10000;
                        iSumY += pChar->iCurrentYPos / 0x10000;
                        iCount++;
                    }
                }
                if (iCount)
                    giCameraX = (iSumX / iCount - 320 + giCameraX) / 2;
            } else if (gkgtLoadedCharacter[0].iHealth) {
                /* player alive: halfway between the player and the average of the fighters that are
                   online, moving a quarter of the way per frame */
                iCount = 0;
                iSumX = 0;
                iSumY = 0;
                pObj = gkgtEngineObjects;
                for (i = 0; i < 1024; i++, pObj++) {
                    if (pObj->iJumpIdx == READ_SCRIPT && pObj->iObjectType == PLAYER_ENGINE_OBJECT
                        && gkgtLoadedCharacter[pObj->iPlayerIdx].iHealth
                        && gkgtLoadedCharacter[pObj->iPlayerIdx].iOnlineState) {
                        iSumX += pObj->iPosX / 0x10000;
                        iSumY += pObj->iPosY / 0x10000;
                        iCount++;
                    }
                }
                if (iCount) {
                    iP1X = gkgtLoadedCharacter[0].iCurrentXPos / 0x10000;
                    iCenterX = (iSumX / iCount + iP1X) / 2;
                    /* keep player 1 on screen */
                    while (iP1X > iCenterX + 280)
                        iCenterX++;
                    while (iP1X < iCenterX - 280)
                        iCenterX--;
                    giCameraX += (iCenterX - giCameraX - 320) / 4;
                }
            } else {
                /* player down: slowly (1/16 per frame) to the average of the fighters still alive */
                iCount = 0;
                iSumX = 0;
                iSumY = 0;
                pObj = gkgtEngineObjects;
                for (i = 0; i < 1024; i++, pObj++) {
                    if (pObj->iJumpIdx == READ_SCRIPT && pObj->iObjectType == PLAYER_ENGINE_OBJECT
                        && gkgtLoadedCharacter[pObj->iPlayerIdx].iHealth) {
                        iSumX += pObj->iPosX / 0x10000;
                        iSumY += pObj->iPosY / 0x10000;
                        iCount++;
                    }
                }
                if (iCount)
                    giCameraX += (iSumX / iCount - giCameraX - 320) / 16;
            }
            break;
        case 1:     /* versus: halfway to the midpoint of the two players per frame (positions 16.16;
                       / 0x20000 = average in pixels) */
        case 2:
            giCameraX = ((gkgtLoadedCharacter[0].iCurrentXPos + gkgtLoadedCharacter[1].iCurrentXPos) / 0x20000 - 320 + giCameraX) / 2;
            giCameraY = ((gkgtLoadedCharacter[0].iCurrentYPos + gkgtLoadedCharacter[1].iCurrentYPos) / 0x20000 - 320 + giCameraY) / 2;
            break;
        }
        /* the stage is 1280x960: the camera's top left stays within 0..640 x 0..480 */
        if (giCameraX < 0)
            giCameraX = 0;
        else if (giCameraX > 640)
            giCameraX = 640;
        if (giCameraY < 0)
            giCameraY = 0;
        else if (giCameraY > 480)
            giCameraY = 480;
        gpkgtCurrentEngineObject->iPosX = -giCameraX * 0x10000;
        gpkgtCurrentEngineObject->iPosY = -giCameraY * 0x10000;
        return;
    case 14:    /* shadows */
        gpkgtCurrentEngineObject->iProcessStep = 15;
        gpkgtCurrentEngineObject->iDrawFlag = -2;
        return;
    case 16:    /* flashing */
        gpkgtCurrentEngineObject->iProcessStep = 17;
        gpkgtCurrentEngineObject->iDrawFlag = -4;
        gpkgtCurrentEngineObject->iPosX = 0;
        gpkgtCurrentEngineObject->iPosY = 0;
        gpkgtCurrentEngineObject->iObjectType = 1;
        /* fall through */
    case 17:
        if ((++gpkgtCurrentEngineObject->iPlayerIdx & 3) == 0) {
            gpkgtCurrentEngineObject->iColorRed += gpkgtCurrentEngineObject->iObjectType;
            if (gpkgtCurrentEngineObject->iColorRed > 30 || gpkgtCurrentEngineObject->iColorRed < 0)
                gpkgtCurrentEngineObject->iObjectType = -gpkgtCurrentEngineObject->iObjectType;
        }
        gpkgtCurrentEngineObject->iColorGreen = gpkgtCurrentEngineObject->iColorBlue = gpkgtCurrentEngineObject->iColorRed;
        return;
    case 20:    /* bitmap iDepth; the random walk is overwritten at once (leftover code) */
        gpkgtCurrentEngineObject->iProcessStep = 21;
        gpkgtCurrentEngineObject->iDrawFlag = gpkgtCurrentEngineObject->iDepth;
        /* fall through */
    case 21:
        gpkgtCurrentEngineObject->pWork015E = (kgtEngineObject *)(((int)gpkgtCurrentEngineObject->pWork015E + (rand() & 1)) & 0x3f);
        gpkgtCurrentEngineObject->pWork0162 = (kgtEngineObject *)(((int)gpkgtCurrentEngineObject->pWork0162 + (rand() & 1)) & 0x3f);
        gpkgtCurrentEngineObject->iWork0166 = (gpkgtCurrentEngineObject->iWork0166 + (rand() & 1)) & 0x3f;
        gpkgtCurrentEngineObject->pWork015E = (kgtEngineObject *)32;
        gpkgtCurrentEngineObject->pWork0162 = (kgtEngineObject *)32;
        gpkgtCurrentEngineObject->iWork0166 = 32;
        return;
    case 30:    /* blur, strength iPlayerIdx 1..99 back and forth (iObjectType = +1 / -1) */
        gpkgtCurrentEngineObject->iProcessStep = 31;
        gpkgtCurrentEngineObject->iDrawFlag = -10;
        gpkgtCurrentEngineObject->iObjectType = 1;
        /* fall through */
    case 31:
        gpkgtCurrentEngineObject->iPlayerIdx += gpkgtCurrentEngineObject->iObjectType;
        if (gpkgtCurrentEngineObject->iPlayerIdx < 1 || gpkgtCurrentEngineObject->iPlayerIdx > 99)
            gpkgtCurrentEngineObject->iObjectType = -gpkgtCurrentEngineObject->iObjectType;
        break;
    }
}

/* ------------------------------------------------------------------------------------------ */
/* 16-bit software renderer                                                                    */
/* ------------------------------------------------------------------------------------------ */

/*
 * vBlitImageRect16 - draw the iRectW x iRectH rectangle at (iSrcX, iSrcY) of an external 8-bit bitmap
 * (kgtBMPINFO: pData = palette of iColorsUsed + 1 RGB555 entries followed by the pixels, rows stored
 * bottom-up as in a BMP) to (iX, iY) of the frame buffer gpFrameBits (640x480, 16 bit), clipped to the
 * screen.  Palette colour 0 is transparent (except in mode 5).
 * The palette is first tinted - iTintR/G/B (-31..31) added to each 5-bit channel, clamped - and
 * converted to the screen format (RGB565 full screen, RGB555 in a window) into gwTintedPalette16.
 * uDrawFlags: bit 31 = draw the rows top-down (upside down), bit 30 = mirror horizontally, bits 0-2 =
 * blend mode: 0 copy, 1 50% mix, 2 add (saturated), 3 subtract (saturated), 4 alpha (the tint values
 * are then the destination weights 0..32 instead: iTintR for the low channel (bits 0-4), iTintG the
 * middle and iTintB the top channel of the pixel), 5 opaque copy.
 * Globals read: gpFrameBits, giScreenMode.  Globals changed: the frame buffer, gwTintedPalette16.
 *
 * Built from the original's machine code: VC6 does not yet compile the C to exactly these bytes.
 * The dead `if (0)` copy of the C body keeps the file's string literals and imported symbols in
 * the original order; the plain C version is on the `nonmatching` branch.
 * Still different: (case 4's else arm and the iInvG/iInvB stack slots, a few register choices in case 2).  Older
 * note: same structure/case layout, but register allocation differs from the start: the original keeps
 * pImage/iX/iRectW/pPalette/iRectH in esi/edx/ecx/ebp/ebx and spills iColors at once (we keep iColors in
 * eax), which shifts every case body.
 *
 * matching scaffolding in the body: the iStackPad locals and the dead "= 0" stores keep VC6's candidate
 * numbering and reload order as in the original; "if (0) vMemzero(&v, 0);" keeps iSkipX, iSkipY and
 * iTintB in memory; "*(volatile int *)&giScreenMode" re-reads the mode at every test as the original
 * does (vc6-matching-notes/matching-techniques.md).
 */
/* the original machine code; the C in the dead block keeps the literals and imports */
__declspec(naked) void vBlitImageRect16(kgtBMPINFO *pImage, int iX, int iY, int iRectW, int iRectH, int iSrcX, int iSrcY,
                      UINT uDrawFlags, int iTintR, int iTintG, int iTintB)
{
    if (0) {
        BYTE * pSrc;
        int iColors;
        int iRed, iGreen, iBlue;
        int iSkipX, iSkipY;
        WORD * pDst;
        int iInvG, iInvB;
        register int iStep;
        WORD * pPalEntry;
        WORD * pTint;
        int iRow, iCol;
        int iDstSkip;
        WORD wColor, wDest;
        WORD wPalColor, wDestColor;
        WORD wColor1, wDest1;
        WORD wColor2, wDest2;
        WORD * pPalette;
        int iStackPad1 = 0;
        int iStackPad2 = 0;
        int iStackPad3 = 0;
        int iStackPad4 = 0;
        int iStackPad5 = 0;
        int iStackPad6 = 0;
        int iStackPad7 = 0;
        pDst = 0;
        iStep = 0;
        if (0) vMemzero(& iSkipX, 0);
        iDstSkip = 0;
        pDst = 0;
        iDstSkip = 0;
        iDstSkip = 0;
        pPalette = 0;
        iColors = 0;
        pTint = 0;
        if (0) vMemzero(& iSkipY, 0);
        pSrc = 0;
        iDstSkip = 0;
        if (0) vMemzero(& iTintB, 0);
        pPalette =(WORD *) pImage->pData;
        iColors = pImage->iColorsUsed + 1;
        pSrc =(BYTE *)(pPalette + pImage->iColorsUsed + 1);
        iSkipX = 0;
        iSkipY = 0;
        pPalEntry = pPalette;
        if (iRectW + iX >= 640) iRectW = 640 - iX;
        if (iY + iRectH >= 480) iRectH = 480 - iY;
        if (iX < 0) {
            iRectW += iX;
            iSkipX = - iX;
            iX = 0;
        }
        if (iY < 0) {
            iRectH += iY;
            iSkipY = -(pImage->iWidth * iY);
            iY = 0;
        }
        pDst =(WORD *) gpFrameBits + iY * 640 + iX;
        iDstSkip = 640 - iRectW;
        if (iRectW <= 0 || iRectH <= 0) ;
        for (pTint = gwTintedPalette16; iColors--; pPalEntry++) {
            wColor = * pPalEntry;
            if (wColor != 0) {
                iRed =(wColor >> 10) & 0x1f;
                iGreen =(wColor >> 5) & 0x1f;
                iBlue = wColor & 0x1f;
                iRed += iTintR;
                if (iRed > 0x1f) {
                    iRed = 0x1f;
                } else {
                    if (iRed < 0) iRed = 0;
                }
                iGreen += iTintG;
                if (iGreen > 0x1f) {
                    iGreen = 0x1f;
                    iBlue += iTintB;
                } else {
                    if (iGreen < 0) iGreen = 0;
                    iBlue += iTintB;
                }
                if (iBlue > 0x1f) iBlue = 0x1f; else if (iBlue < 0) iBlue = 0;
                if (*(volatile int *) & giScreenMode) * pTint++ =(((((iRed << 5))) + iGreen) << 6) + iBlue; else * pTint++ =(((iRed << 5) + iGreen) << 5) + iBlue;
            } else {
                * pTint++ = 0;
            }
        }
        if (uDrawFlags & 0x40000000) {
            if (!(uDrawFlags & 0x80000000)) {
                pSrc +=(pImage->iHeight - iSrcY) * pImage->iWidth - iSkipY - iSkipX - iSrcX - 1;
                iSrcY = iRectW - pImage->iWidth;
            } else {
                pSrc +=(iSrcY + 1) * pImage->iWidth - iSkipX + iSkipY + iSrcX - 1;
                iSrcY = pImage->iWidth + iRectW;
            }
            iStep = - 1;
        } else {
            if (!(uDrawFlags & 0x80000000)) {
                pSrc +=(pImage->iHeight - iSrcY - 1) * pImage->iWidth - iSkipY + iSkipX + iSrcX;
                iSrcY = -(pImage->iWidth + iRectW);
            } else {
                pSrc += pImage->iWidth * iSrcY + iSkipY + iSkipX + iSrcX;
                iSrcY = pImage->iWidth - iRectW;
            }
            iStep = 1;
        }
        switch (uDrawFlags & 7) {
            case 0 :
            for (iRow = iRectH; iRow--; pDst += iDstSkip) {
                iCol = iRectW;
                do {
                    wColor = gwTintedPalette16[* pSrc];
                    if (wColor != 0) {
                        * pDst = wColor;
                        pDst++;
                    } else {
                        pDst++;
                    }
                    pSrc += iStep;
                } while (-- iCol);
                pSrc += iSrcY;
            }
            break;
            case 1 :
            if (*(volatile int *) & giScreenMode) {
                iRow = iRectH;
                while (iRow--) {
                    iCol = iRectW;
                    while (iCol--) {
                        wColor1 = gwTintedPalette16[* pSrc];
                        if (wColor1) * pDst =((* pDst >> 1) & 0x7bef) +((wColor1 >> 1) & 0x7bef);
                        pDst += 1;
                        pSrc += iStep;
                    }
                    pSrc += iSrcY;
                    pDst += iDstSkip;
                }
                break;
            } else {
                iRow = iRectH;
                while (iRow--) {
                    iCol = iRectW;
                    while (iCol--) {
                        wColor1 = gwTintedPalette16[* pSrc];
                        if (wColor1) * pDst =((* pDst >> 1) & 0x3def) +((wColor1 >> 1) & 0x3def);
                        pDst++;
                        pSrc += iStep;
                    }
                    pSrc += iSrcY;
                    pDst += iDstSkip;
                }
                break;
            }
            case 2 :
            if (*(volatile int *) & giScreenMode) {
                iRow = iRectH;
                while (iRow--) {
                    iCol = iRectW;
                    do {
                        wColor2 = gwTintedPalette16[* pSrc];
                        if (wColor2) {
                            short shBlue, shGreen;
                            wDest2 = * pDst;
                            shBlue =(wColor2 & 0x1f) +(wDest2 & 0x1f);
                            if (shBlue > 0x1f) shBlue = 0x1f;
                            shGreen =(wColor2 & 0x7e0) +(wDest2 & 0x7e0);
                            if (shGreen > 0x7e0) shGreen = 0x7e0;
                            iRed =(wColor2 & 0xf800) +(wDest2 & 0xf800);
                            if (iRed > 0xf800) iRed = 0xf800;
                            * pDst = iRed + shGreen + shBlue;
                        }
                        pDst++;
                        pSrc += iStep;
                    } while (-- iCol);
                    pSrc += iSrcY;
                    pDst += iDstSkip;
                }
            } else {
                iRow = iRectH;
                while (iRow--) {
                    for (iCol = iRectW; iCol--; pSrc += iStep) {
                        wColor2 = gwTintedPalette16[* pSrc];
                        if (wColor2) {
                            WORD wSum, wCarry;
                            wSum =(* pDst & 0x7bdf) +(wColor2 & 0x7bdf);
                            wCarry = wSum & 0x8420;
                            * pDst =(wCarry -(wCarry >> 5)) | wSum;
                        }
                        pDst++;
                    }
                    pSrc += iSrcY;
                    pDst += iDstSkip;
                }
            }
            break;
            case 3 :
            if (*(volatile int *) & giScreenMode) {
                iRow = iRectH;
                while (iRow--) {
                    iCol = iRectW;
                    while (iCol--) {
                        wColor = gwTintedPalette16[* pSrc];
                        if (wColor) {
                            short shBlue, shGreen;
                            wDest = * pDst;
                            shBlue =(wDest & 0x1f) -(wColor & 0x1f);
                            if (shBlue < 0) shBlue = 0;
                            shGreen =(wDest & 0x7e0) -(wColor & 0x7e0);
                            if (shGreen < 0x40) shGreen = 0;
                            iRed =(wDest & 0xf800) -(wColor & 0xf800);
                            if (iRed < 0x800) iRed = 0;
                            * pDst = iRed + shGreen + shBlue;
                        }
                        pDst++;
                        pSrc += iStep;
                    }
                    pSrc += iSrcY;
                    pDst += iDstSkip;
                }
            } else {
                for (iRow = iRectH; iRow--; pDst += iDstSkip) {
                    iCol = iRectW;
                    while (iCol--) {
                        wColor = gwTintedPalette16[* pSrc];
                        if (wColor) {
                            short shBlue, shGreen, shRed;
                            wDest = * pDst;
                            shBlue =(wDest & 0x1f) -(wColor & 0x1f);
                            if (shBlue < 0) shBlue = 0;
                            shGreen =(wDest & 0x3e0) -(wColor & 0x3e0);
                            if (shGreen < 0x20) shGreen = 0;
                            shRed =(wDest & 0x7c00) -(wColor & 0x7c00);
                            if (shRed < 0x400) shRed = 0;
                            * pDst = shRed + shGreen + shBlue;
                        }
                        pDst++;
                        pSrc += iStep;
                    }
                    pSrc += iSrcY;
                }
            }
            break;
            case 4 :
            iY = 32 - iTintR;
            iInvB = 32 - iTintB;
            iInvG = 32 - iTintG;
            if (*(volatile int *) & giScreenMode) {
                iInvB *= 2;
                iInvG *= 2;
                iRow = iRectH;
                while (iRow--) {
                    iCol = iRectW;
                    while (iCol--) {
                        if (gwTintedPalette16[* pSrc]) {
                            wPalColor = pPalette[* pSrc];
                            wDestColor = * pDst;
                            * pDst =((((wPalColor & 0x7c00) *(WORD) iInvB +(wDestColor & 0xf800) *(WORD) iTintB) >> 5) & 0xf800) +((((wDestColor & 0x7e0) * iTintG +(wPalColor & 0x3e0) * iInvG) >> 5) & 0x7e0) +((((wPalColor & 0x1f) * iY +(wDestColor & 0x1f) * iTintR) >> 5) & 0x1f);
                        }
                        pDst++;
                        pSrc += iStep;
                    }
                    pSrc += iSrcY;
                    pDst += iDstSkip;
                }
            } else {
                for (iRow = iRectH; iRow--; pDst += iDstSkip) {
                    iCol = iRectW;
                    do {
                        if (gwTintedPalette16[* pSrc]) {
                            wPalColor = pPalette[* pSrc];
                            wDestColor = * pDst;
                            * pDst =((((wPalColor & 0x3e0) * iInvG +(wDestColor & 0x3e0) * iTintG) >> 5) & 0x3e0) +((((wPalColor & 0x1f) * iY +(wDestColor & 0x1f) * iTintR) >> 5) & 0x1f) +((((wPalColor & 0x7c00) *(WORD) iInvB +(wDestColor & 0x7c00) *(WORD) iTintB) >> 5) & 0x7c00);
                        }
                        pDst++;
                        pSrc += iStep;
                    } while (-- iCol);
                    pSrc += iSrcY;
                }
            }
            break;
            case 5 :
            for (iRow = iRectH; iRow--; pDst += iDstSkip) {
                iCol = iRectW;
                do {
                    * pDst++ = gwTintedPalette16[* pSrc];
                    pSrc += iStep;
                } while (-- iCol);
                pSrc += iSrcY;
            }
            break;
        }
    }
    __asm {
        _emit 0x83        ; 0040B4C0  sub esp, 0x14
        _emit 0xEC
        _emit 0x14
        _emit 0x53        ; 0040B4C3  push ebx
        _emit 0x55        ; 0040B4C4  push ebp
        _emit 0x56        ; 0040B4C5  push esi
        _emit 0x8B        ; 0040B4C6  mov esi, dword ptr [esp + 0x24]
        _emit 0x74
        _emit 0x24
        _emit 0x24
        _emit 0x8B        ; 0040B4CA  mov edx, dword ptr [esp + 0x28]
        _emit 0x54
        _emit 0x24
        _emit 0x28
        _emit 0x8B        ; 0040B4CE  mov ecx, dword ptr [esp + 0x30]
        _emit 0x4C
        _emit 0x24
        _emit 0x30
        _emit 0x8B        ; 0040B4D2  mov eax, dword ptr [esi + 0xc]
        _emit 0x46
        _emit 0x0C
        _emit 0x8B        ; 0040B4D5  mov ebp, dword ptr [esi]
        _emit 0x2E
        _emit 0x40        ; 0040B4D7  inc eax
        _emit 0x57        ; 0040B4D8  push edi
        _emit 0x89        ; 0040B4D9  mov dword ptr [esp + 0x1c], eax
        _emit 0x44
        _emit 0x24
        _emit 0x1C
        _emit 0x89        ; 0040B4DD  mov dword ptr [esp + 0x20], ebp
        _emit 0x6C
        _emit 0x24
        _emit 0x20
        _emit 0x8D        ; 0040B4E1  lea edi, [ebp + eax*2]
        _emit 0x7C
        _emit 0x45
        _emit 0x00
        _emit 0x33        ; 0040B4E5  xor eax, eax
        _emit 0xC0
        _emit 0x89        ; 0040B4E7  mov dword ptr [esp + 0x14], eax
        _emit 0x44
        _emit 0x24
        _emit 0x14
        _emit 0x89        ; 0040B4EB  mov dword ptr [esp + 0x10], eax
        _emit 0x44
        _emit 0x24
        _emit 0x10
        _emit 0x8D        ; 0040B4EF  lea eax, [edx + ecx]
        _emit 0x04
        _emit 0x0A
        _emit 0x89        ; 0040B4F2  mov dword ptr [esp + 0x18], ebp
        _emit 0x6C
        _emit 0x24
        _emit 0x18
        _emit 0x3D        ; 0040B4F6  cmp eax, 0x280
        _emit 0x80
        _emit 0x02
        _emit 0x00
        _emit 0x00
        _emit 0x7C        ; 0040B4FB  jl 0x40b508
        _emit 0x0B
        _emit 0xB9        ; 0040B4FD  mov ecx, 0x280
        _emit 0x80
        _emit 0x02
        _emit 0x00
        _emit 0x00
        _emit 0x2B        ; 0040B502  sub ecx, edx
        _emit 0xCA
        _emit 0x89        ; 0040B504  mov dword ptr [esp + 0x34], ecx
        _emit 0x4C
        _emit 0x24
        _emit 0x34
        _emit 0x8B        ; 0040B508  mov eax, dword ptr [esp + 0x30]
        _emit 0x44
        _emit 0x24
        _emit 0x30
        _emit 0x8B        ; 0040B50C  mov ebx, dword ptr [esp + 0x38]
        _emit 0x5C
        _emit 0x24
        _emit 0x38
        _emit 0x03        ; 0040B510  add ebx, eax
        _emit 0xD8
        _emit 0x81        ; 0040B512  cmp ebx, 0x1e0
        _emit 0xFB
        _emit 0xE0
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x7C        ; 0040B518  jl 0x40b527
        _emit 0x0D
        _emit 0xBB        ; 0040B51A  mov ebx, 0x1e0
        _emit 0xE0
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x2B        ; 0040B51F  sub ebx, eax
        _emit 0xD8
        _emit 0x89        ; 0040B521  mov dword ptr [esp + 0x38], ebx
        _emit 0x5C
        _emit 0x24
        _emit 0x38
        _emit 0xEB        ; 0040B525  jmp 0x40b52b
        _emit 0x04
        _emit 0x8B        ; 0040B527  mov ebx, dword ptr [esp + 0x38]
        _emit 0x5C
        _emit 0x24
        _emit 0x38
        _emit 0x85        ; 0040B52B  test edx, edx
        _emit 0xD2
        _emit 0x7D        ; 0040B52D  jge 0x40b53d
        _emit 0x0E
        _emit 0x03        ; 0040B52F  add ecx, edx
        _emit 0xCA
        _emit 0xF7        ; 0040B531  neg edx
        _emit 0xDA
        _emit 0x89        ; 0040B533  mov dword ptr [esp + 0x14], edx
        _emit 0x54
        _emit 0x24
        _emit 0x14
        _emit 0x89        ; 0040B537  mov dword ptr [esp + 0x34], ecx
        _emit 0x4C
        _emit 0x24
        _emit 0x34
        _emit 0x33        ; 0040B53B  xor edx, edx
        _emit 0xD2
        _emit 0x85        ; 0040B53D  test eax, eax
        _emit 0xC0
        _emit 0x7D        ; 0040B53F  jge 0x40b559
        _emit 0x18
        _emit 0x03        ; 0040B541  add ebx, eax
        _emit 0xD8
        _emit 0x89        ; 0040B543  mov dword ptr [esp + 0x38], ebx
        _emit 0x5C
        _emit 0x24
        _emit 0x38
        _emit 0x8B        ; 0040B547  mov ebx, dword ptr [esi + 4]
        _emit 0x5E
        _emit 0x04
        _emit 0x0F        ; 0040B54A  imul ebx, eax
        _emit 0xAF
        _emit 0xD8
        _emit 0xF7        ; 0040B54D  neg ebx
        _emit 0xDB
        _emit 0x89        ; 0040B54F  mov dword ptr [esp + 0x10], ebx
        _emit 0x5C
        _emit 0x24
        _emit 0x10
        _emit 0x8B        ; 0040B553  mov ebx, dword ptr [esp + 0x38]
        _emit 0x5C
        _emit 0x24
        _emit 0x38
        _emit 0x33        ; 0040B557  xor eax, eax
        _emit 0xC0
        _emit 0x8D        ; 0040B559  lea eax, [eax + eax*4]
        _emit 0x04
        _emit 0x80
        _emit 0xC1        ; 0040B55C  shl eax, 7
        _emit 0xE0
        _emit 0x07
        _emit 0x03        ; 0040B55F  add eax, edx
        _emit 0xC2
        _emit 0x8B        ; 0040B561  mov edx, dword ptr [0x4246cc]
        _emit 0x15
        _emit 0xCC
        _emit 0x46
        _emit 0x42
        _emit 0x00
        _emit 0x8D        ; 0040B567  lea edx, [edx + eax*2]
        _emit 0x14
        _emit 0x42
        _emit 0xB8        ; 0040B56A  mov eax, 0x280
        _emit 0x80
        _emit 0x02
        _emit 0x00
        _emit 0x00
        _emit 0x2B        ; 0040B56F  sub eax, ecx
        _emit 0xC1
        _emit 0x85        ; 0040B571  test ecx, ecx
        _emit 0xC9
        _emit 0x89        ; 0040B573  mov dword ptr [esp + 0x2c], eax
        _emit 0x44
        _emit 0x24
        _emit 0x2C
        _emit 0x0F        ; 0040B577  jle 0x40bd5e
        _emit 0x8E
        _emit 0xE1
        _emit 0x07
        _emit 0x00
        _emit 0x00
        _emit 0x85        ; 0040B57D  test ebx, ebx
        _emit 0xDB
        _emit 0x0F        ; 0040B57F  jle 0x40bd5e
        _emit 0x8E
        _emit 0xD9
        _emit 0x07
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040B585  mov eax, dword ptr [esp + 0x1c]
        _emit 0x44
        _emit 0x24
        _emit 0x1C
        _emit 0xB9        ; 0040B589  mov ecx, 0x4d1a20
        _emit 0x20
        _emit 0x1A
        _emit 0x4D
        _emit 0x00
        _emit 0x8B        ; 0040B58E  mov ebx, eax
        _emit 0xD8
        _emit 0x48        ; 0040B590  dec eax
        _emit 0x85        ; 0040B591  test ebx, ebx
        _emit 0xDB
        _emit 0x89        ; 0040B593  mov dword ptr [esp + 0x30], ecx
        _emit 0x4C
        _emit 0x24
        _emit 0x30
        _emit 0x0F        ; 0040B597  je 0x40b688
        _emit 0x84
        _emit 0xEB
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040B59D  mov ebx, dword ptr [esp + 0x38]
        _emit 0x5C
        _emit 0x24
        _emit 0x38
        _emit 0x40        ; 0040B5A1  inc eax
        _emit 0x89        ; 0040B5A2  mov dword ptr [esp + 0x1c], eax
        _emit 0x44
        _emit 0x24
        _emit 0x1C
        _emit 0x8B        ; 0040B5A6  mov eax, dword ptr [esp + 0x18]
        _emit 0x44
        _emit 0x24
        _emit 0x18
        _emit 0x66        ; 0040B5AA  mov ax, word ptr [eax]
        _emit 0x8B
        _emit 0x00
        _emit 0x66        ; 0040B5AD  test ax, ax
        _emit 0x85
        _emit 0xC0
        _emit 0x0F        ; 0040B5B0  je 0x40b660
        _emit 0x84
        _emit 0xAA
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040B5B6  mov ebx, dword ptr [esp + 0x48]
        _emit 0x5C
        _emit 0x24
        _emit 0x48
        _emit 0x25        ; 0040B5BA  and eax, 0xffff
        _emit 0xFF
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040B5BF  mov ecx, eax
        _emit 0xC8
        _emit 0x8B        ; 0040B5C1  mov esi, eax
        _emit 0xF0
        _emit 0xC1        ; 0040B5C3  shr eax, 0xa
        _emit 0xE8
        _emit 0x0A
        _emit 0x83        ; 0040B5C6  and eax, 0x1f
        _emit 0xE0
        _emit 0x1F
        _emit 0x83        ; 0040B5C9  and esi, 0x1f
        _emit 0xE6
        _emit 0x1F
        _emit 0xC1        ; 0040B5CC  shr ecx, 5
        _emit 0xE9
        _emit 0x05
        _emit 0x03        ; 0040B5CF  add eax, ebx
        _emit 0xC3
        _emit 0x83        ; 0040B5D1  and ecx, 0x1f
        _emit 0xE1
        _emit 0x1F
        _emit 0x83        ; 0040B5D4  cmp eax, 0x1f
        _emit 0xF8
        _emit 0x1F
        _emit 0x7E        ; 0040B5D7  jle 0x40b5e0
        _emit 0x07
        _emit 0xB8        ; 0040B5D9  mov eax, 0x1f
        _emit 0x1F
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0xEB        ; 0040B5DE  jmp 0x40b5e6
        _emit 0x06
        _emit 0x85        ; 0040B5E0  test eax, eax
        _emit 0xC0
        _emit 0x7D        ; 0040B5E2  jge 0x40b5e6
        _emit 0x02
        _emit 0x33        ; 0040B5E4  xor eax, eax
        _emit 0xC0
        _emit 0x03        ; 0040B5E6  add ecx, dword ptr [esp + 0x4c]
        _emit 0x4C
        _emit 0x24
        _emit 0x4C
        _emit 0x83        ; 0040B5EA  cmp ecx, 0x1f
        _emit 0xF9
        _emit 0x1F
        _emit 0x7E        ; 0040B5ED  jle 0x40b5f6
        _emit 0x07
        _emit 0xB9        ; 0040B5EF  mov ecx, 0x1f
        _emit 0x1F
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0xEB        ; 0040B5F4  jmp 0x40b5fc
        _emit 0x06
        _emit 0x85        ; 0040B5F6  test ecx, ecx
        _emit 0xC9
        _emit 0x7D        ; 0040B5F8  jge 0x40b5fc
        _emit 0x02
        _emit 0x33        ; 0040B5FA  xor ecx, ecx
        _emit 0xC9
        _emit 0x03        ; 0040B5FC  add esi, dword ptr [esp + 0x50]
        _emit 0x74
        _emit 0x24
        _emit 0x50
        _emit 0x83        ; 0040B600  cmp esi, 0x1f
        _emit 0xFE
        _emit 0x1F
        _emit 0x7E        ; 0040B603  jle 0x40b60c
        _emit 0x07
        _emit 0xBE        ; 0040B605  mov esi, 0x1f
        _emit 0x1F
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0xEB        ; 0040B60A  jmp 0x40b612
        _emit 0x06
        _emit 0x85        ; 0040B60C  test esi, esi
        _emit 0xF6
        _emit 0x7D        ; 0040B60E  jge 0x40b612
        _emit 0x02
        _emit 0x33        ; 0040B610  xor esi, esi
        _emit 0xF6
        _emit 0x8B        ; 0040B612  mov ebx, dword ptr [0x424704]
        _emit 0x1D
        _emit 0x04
        _emit 0x47
        _emit 0x42
        _emit 0x00
        _emit 0x85        ; 0040B618  test ebx, ebx
        _emit 0xDB
        _emit 0x74        ; 0040B61A  je 0x40b63e
        _emit 0x22
        _emit 0xC1        ; 0040B61C  shl eax, 5
        _emit 0xE0
        _emit 0x05
        _emit 0x03        ; 0040B61F  add eax, ecx
        _emit 0xC1
        _emit 0x8B        ; 0040B621  mov ecx, dword ptr [esp + 0x30]
        _emit 0x4C
        _emit 0x24
        _emit 0x30
        _emit 0x8B        ; 0040B625  mov ebx, dword ptr [esp + 0x38]
        _emit 0x5C
        _emit 0x24
        _emit 0x38
        _emit 0xC1        ; 0040B629  shl eax, 6
        _emit 0xE0
        _emit 0x06
        _emit 0x03        ; 0040B62C  add eax, esi
        _emit 0xC6
        _emit 0x8B        ; 0040B62E  mov esi, dword ptr [esp + 0x28]
        _emit 0x74
        _emit 0x24
        _emit 0x28
        _emit 0x66        ; 0040B632  mov word ptr [ecx], ax
        _emit 0x89
        _emit 0x01
        _emit 0x83        ; 0040B635  add ecx, 2
        _emit 0xC1
        _emit 0x02
        _emit 0x89        ; 0040B638  mov dword ptr [esp + 0x30], ecx
        _emit 0x4C
        _emit 0x24
        _emit 0x30
        _emit 0xEB        ; 0040B63C  jmp 0x40b66c
        _emit 0x2E
        _emit 0xC1        ; 0040B63E  shl eax, 5
        _emit 0xE0
        _emit 0x05
        _emit 0x03        ; 0040B641  add eax, ecx
        _emit 0xC1
        _emit 0x8B        ; 0040B643  mov ecx, dword ptr [esp + 0x30]
        _emit 0x4C
        _emit 0x24
        _emit 0x30
        _emit 0x8B        ; 0040B647  mov ebx, dword ptr [esp + 0x38]
        _emit 0x5C
        _emit 0x24
        _emit 0x38
        _emit 0xC1        ; 0040B64B  shl eax, 5
        _emit 0xE0
        _emit 0x05
        _emit 0x03        ; 0040B64E  add eax, esi
        _emit 0xC6
        _emit 0x8B        ; 0040B650  mov esi, dword ptr [esp + 0x28]
        _emit 0x74
        _emit 0x24
        _emit 0x28
        _emit 0x66        ; 0040B654  mov word ptr [ecx], ax
        _emit 0x89
        _emit 0x01
        _emit 0x83        ; 0040B657  add ecx, 2
        _emit 0xC1
        _emit 0x02
        _emit 0x89        ; 0040B65A  mov dword ptr [esp + 0x30], ecx
        _emit 0x4C
        _emit 0x24
        _emit 0x30
        _emit 0xEB        ; 0040B65E  jmp 0x40b66c
        _emit 0x0C
        _emit 0x66        ; 0040B660  mov word ptr [ecx], 0
        _emit 0xC7
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x83        ; 0040B665  add ecx, 2
        _emit 0xC1
        _emit 0x02
        _emit 0x89        ; 0040B668  mov dword ptr [esp + 0x30], ecx
        _emit 0x4C
        _emit 0x24
        _emit 0x30
        _emit 0x8B        ; 0040B66C  mov eax, dword ptr [esp + 0x18]
        _emit 0x44
        _emit 0x24
        _emit 0x18
        _emit 0x83        ; 0040B670  add eax, 2
        _emit 0xC0
        _emit 0x02
        _emit 0x89        ; 0040B673  mov dword ptr [esp + 0x18], eax
        _emit 0x44
        _emit 0x24
        _emit 0x18
        _emit 0x8B        ; 0040B677  mov eax, dword ptr [esp + 0x1c]
        _emit 0x44
        _emit 0x24
        _emit 0x1C
        _emit 0x48        ; 0040B67B  dec eax
        _emit 0x89        ; 0040B67C  mov dword ptr [esp + 0x1c], eax
        _emit 0x44
        _emit 0x24
        _emit 0x1C
        _emit 0x0F        ; 0040B680  jne 0x40b5a6
        _emit 0x85
        _emit 0x20
        _emit 0xFF
        _emit 0xFF
        _emit 0xFF
        _emit 0xEB        ; 0040B686  jmp 0x40b68c
        _emit 0x04
        _emit 0x8B        ; 0040B688  mov ebx, dword ptr [esp + 0x38]
        _emit 0x5C
        _emit 0x24
        _emit 0x38
        _emit 0x8B        ; 0040B68C  mov ecx, dword ptr [esp + 0x44]
        _emit 0x4C
        _emit 0x24
        _emit 0x44
        _emit 0xF7        ; 0040B690  test ecx, 0x40000000
        _emit 0xC1
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x40
        _emit 0x74        ; 0040B696  je 0x40b701
        _emit 0x69
        _emit 0xF7        ; 0040B698  test ecx, 0x80000000
        _emit 0xC1
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x80
        _emit 0x75        ; 0040B69E  jne 0x40b6d3
        _emit 0x33
        _emit 0x8B        ; 0040B6A0  mov eax, dword ptr [esi + 4]
        _emit 0x46
        _emit 0x04
        _emit 0x8B        ; 0040B6A3  mov esi, dword ptr [esi + 8]
        _emit 0x76
        _emit 0x08
        _emit 0x2B        ; 0040B6A6  sub esi, dword ptr [esp + 0x40]
        _emit 0x74
        _emit 0x24
        _emit 0x40
        _emit 0x0F        ; 0040B6AA  imul esi, eax
        _emit 0xAF
        _emit 0xF0
        _emit 0x2B        ; 0040B6AD  sub esi, dword ptr [esp + 0x10]
        _emit 0x74
        _emit 0x24
        _emit 0x10
        _emit 0x2B        ; 0040B6B1  sub esi, dword ptr [esp + 0x14]
        _emit 0x74
        _emit 0x24
        _emit 0x14
        _emit 0x2B        ; 0040B6B5  sub esi, dword ptr [esp + 0x3c]
        _emit 0x74
        _emit 0x24
        _emit 0x3C
        _emit 0x8D        ; 0040B6B9  lea edi, [edi + esi - 1]
        _emit 0x7C
        _emit 0x37
        _emit 0xFF
        _emit 0x8B        ; 0040B6BD  mov esi, dword ptr [esp + 0x34]
        _emit 0x74
        _emit 0x24
        _emit 0x34
        _emit 0x2B        ; 0040B6C1  sub esi, eax
        _emit 0xF0
        _emit 0x89        ; 0040B6C3  mov dword ptr [esp + 0x3c], edi
        _emit 0x7C
        _emit 0x24
        _emit 0x3C
        _emit 0x89        ; 0040B6C7  mov dword ptr [esp + 0x40], esi
        _emit 0x74
        _emit 0x24
        _emit 0x40
        _emit 0x83        ; 0040B6CB  or esi, 0xffffffff
        _emit 0xCE
        _emit 0xFF
        _emit 0xE9        ; 0040B6CE  jmp 0x40b762
        _emit 0x8F
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040B6D3  mov eax, dword ptr [esp + 0x40]
        _emit 0x44
        _emit 0x24
        _emit 0x40
        _emit 0x8B        ; 0040B6D7  mov esi, dword ptr [esi + 4]
        _emit 0x76
        _emit 0x04
        _emit 0x40        ; 0040B6DA  inc eax
        _emit 0x0F        ; 0040B6DB  imul eax, esi
        _emit 0xAF
        _emit 0xC6
        _emit 0x2B        ; 0040B6DE  sub eax, dword ptr [esp + 0x14]
        _emit 0x44
        _emit 0x24
        _emit 0x14
        _emit 0x03        ; 0040B6E2  add eax, dword ptr [esp + 0x10]
        _emit 0x44
        _emit 0x24
        _emit 0x10
        _emit 0x03        ; 0040B6E6  add eax, dword ptr [esp + 0x3c]
        _emit 0x44
        _emit 0x24
        _emit 0x3C
        _emit 0x8D        ; 0040B6EA  lea edi, [edi + eax - 1]
        _emit 0x7C
        _emit 0x07
        _emit 0xFF
        _emit 0x8B        ; 0040B6EE  mov eax, dword ptr [esp + 0x34]
        _emit 0x44
        _emit 0x24
        _emit 0x34
        _emit 0x03        ; 0040B6F2  add esi, eax
        _emit 0xF0
        _emit 0x89        ; 0040B6F4  mov dword ptr [esp + 0x3c], edi
        _emit 0x7C
        _emit 0x24
        _emit 0x3C
        _emit 0x89        ; 0040B6F8  mov dword ptr [esp + 0x40], esi
        _emit 0x74
        _emit 0x24
        _emit 0x40
        _emit 0x83        ; 0040B6FC  or esi, 0xffffffff
        _emit 0xCE
        _emit 0xFF
        _emit 0xEB        ; 0040B6FF  jmp 0x40b762
        _emit 0x61
        _emit 0xF7        ; 0040B701  test ecx, 0x80000000
        _emit 0xC1
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x80
        _emit 0x75        ; 0040B707  jne 0x40b737
        _emit 0x2E
        _emit 0x8B        ; 0040B709  mov eax, dword ptr [esi + 4]
        _emit 0x46
        _emit 0x04
        _emit 0x8B        ; 0040B70C  mov esi, dword ptr [esi + 8]
        _emit 0x76
        _emit 0x08
        _emit 0x2B        ; 0040B70F  sub esi, dword ptr [esp + 0x40]
        _emit 0x74
        _emit 0x24
        _emit 0x40
        _emit 0x4E        ; 0040B713  dec esi
        _emit 0x0F        ; 0040B714  imul esi, eax
        _emit 0xAF
        _emit 0xF0
        _emit 0x2B        ; 0040B717  sub esi, dword ptr [esp + 0x10]
        _emit 0x74
        _emit 0x24
        _emit 0x10
        _emit 0x03        ; 0040B71B  add esi, dword ptr [esp + 0x14]
        _emit 0x74
        _emit 0x24
        _emit 0x14
        _emit 0x03        ; 0040B71F  add esi, dword ptr [esp + 0x3c]
        _emit 0x74
        _emit 0x24
        _emit 0x3C
        _emit 0x03        ; 0040B723  add edi, esi
        _emit 0xFE
        _emit 0x8B        ; 0040B725  mov esi, dword ptr [esp + 0x34]
        _emit 0x74
        _emit 0x24
        _emit 0x34
        _emit 0x03        ; 0040B729  add eax, esi
        _emit 0xC6
        _emit 0x89        ; 0040B72B  mov dword ptr [esp + 0x3c], edi
        _emit 0x7C
        _emit 0x24
        _emit 0x3C
        _emit 0xF7        ; 0040B72F  neg eax
        _emit 0xD8
        _emit 0x89        ; 0040B731  mov dword ptr [esp + 0x40], eax
        _emit 0x44
        _emit 0x24
        _emit 0x40
        _emit 0xEB        ; 0040B735  jmp 0x40b75d
        _emit 0x26
        _emit 0x8B        ; 0040B737  mov esi, dword ptr [esi + 4]
        _emit 0x76
        _emit 0x04
        _emit 0x8B        ; 0040B73A  mov eax, esi
        _emit 0xC6
        _emit 0x0F        ; 0040B73C  imul eax, dword ptr [esp + 0x40]
        _emit 0xAF
        _emit 0x44
        _emit 0x24
        _emit 0x40
        _emit 0x03        ; 0040B741  add eax, dword ptr [esp + 0x10]
        _emit 0x44
        _emit 0x24
        _emit 0x10
        _emit 0x03        ; 0040B745  add eax, dword ptr [esp + 0x14]
        _emit 0x44
        _emit 0x24
        _emit 0x14
        _emit 0x03        ; 0040B749  add eax, dword ptr [esp + 0x3c]
        _emit 0x44
        _emit 0x24
        _emit 0x3C
        _emit 0x03        ; 0040B74D  add edi, eax
        _emit 0xF8
        _emit 0x8B        ; 0040B74F  mov eax, dword ptr [esp + 0x34]
        _emit 0x44
        _emit 0x24
        _emit 0x34
        _emit 0x2B        ; 0040B753  sub esi, eax
        _emit 0xF0
        _emit 0x89        ; 0040B755  mov dword ptr [esp + 0x3c], edi
        _emit 0x7C
        _emit 0x24
        _emit 0x3C
        _emit 0x89        ; 0040B759  mov dword ptr [esp + 0x40], esi
        _emit 0x74
        _emit 0x24
        _emit 0x40
        _emit 0xBE        ; 0040B75D  mov esi, 1
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040B762  mov eax, ecx
        _emit 0xC1
        _emit 0x89        ; 0040B764  mov dword ptr [esp + 0x28], esi
        _emit 0x74
        _emit 0x24
        _emit 0x28
        _emit 0x83        ; 0040B768  and eax, 7
        _emit 0xE0
        _emit 0x07
        _emit 0x83        ; 0040B76B  cmp eax, 5
        _emit 0xF8
        _emit 0x05
        _emit 0x0F        ; 0040B76E  ja 0x40bd5e
        _emit 0x87
        _emit 0xEA
        _emit 0x05
        _emit 0x00
        _emit 0x00
        _emit 0xFF        ; 0040B774  jmp dword ptr [eax*4 + 0x40bd68]
        _emit 0x24
        _emit 0x85
        _emit 0x68
        _emit 0xBD
        _emit 0x40
        _emit 0x00
        _emit 0x8D        ; 0040B77B  lea ebp, [ebx]
        _emit 0x2B
        _emit 0x8B        ; 0040B77D  mov ebx, dword ptr [esp + 0x40]
        _emit 0x5C
        _emit 0x24
        _emit 0x40
        _emit 0x8B        ; 0040B781  mov ecx, dword ptr [esp + 0x34]
        _emit 0x4C
        _emit 0x24
        _emit 0x34
        _emit 0x33        ; 0040B785  xor eax, eax
        _emit 0xC0
        _emit 0x8A        ; 0040B787  mov al, byte ptr [edi]
        _emit 0x07
        _emit 0x66        ; 0040B789  mov ax, word ptr [eax*2 + 0x4d1a20]
        _emit 0x8B
        _emit 0x04
        _emit 0x45
        _emit 0x20
        _emit 0x1A
        _emit 0x4D
        _emit 0x00
        _emit 0x66        ; 0040B791  test ax, ax
        _emit 0x85
        _emit 0xC0
        _emit 0x74        ; 0040B794  je 0x40b799
        _emit 0x03
        _emit 0x66        ; 0040B796  mov word ptr [edx], ax
        _emit 0x89
        _emit 0x02
        _emit 0x83        ; 0040B799  add edx, 2
        _emit 0xC2
        _emit 0x02
        _emit 0x03        ; 0040B79C  add edi, esi
        _emit 0xFE
        _emit 0x49        ; 0040B79E  dec ecx
        _emit 0x75        ; 0040B79F  jne 0x40b785
        _emit 0xE4
        _emit 0x8B        ; 0040B7A1  mov ecx, dword ptr [esp + 0x2c]
        _emit 0x4C
        _emit 0x24
        _emit 0x2C
        _emit 0x03        ; 0040B7A5  add edi, ebx
        _emit 0xFB
        _emit 0x4D        ; 0040B7A7  dec ebp
        _emit 0x8D        ; 0040B7A8  lea edx, [edx + ecx*2]
        _emit 0x14
        _emit 0x4A
        _emit 0x75        ; 0040B7AB  jne 0x40b781
        _emit 0xD4
        _emit 0x5F        ; 0040B7AD  pop edi
        _emit 0x5E        ; 0040B7AE  pop esi
        _emit 0x5D        ; 0040B7AF  pop ebp
        _emit 0x5B        ; 0040B7B0  pop ebx
        _emit 0x83        ; 0040B7B1  add esp, 0x14
        _emit 0xC4
        _emit 0x14
        _emit 0xC3        ; 0040B7B4  ret
        _emit 0xA1        ; 0040B7B5  mov eax, dword ptr [0x424704]
        _emit 0x04
        _emit 0x47
        _emit 0x42
        _emit 0x00
        _emit 0x85        ; 0040B7BA  test eax, eax
        _emit 0xC0
        _emit 0x74        ; 0040B7BC  je 0x40b818
        _emit 0x5A
        _emit 0x8B        ; 0040B7BE  mov ebp, dword ptr [esp + 0x40]
        _emit 0x6C
        _emit 0x24
        _emit 0x40
        _emit 0x89        ; 0040B7C2  mov dword ptr [esp + 0x38], ebx
        _emit 0x5C
        _emit 0x24
        _emit 0x38
        _emit 0x8B        ; 0040B7C6  mov eax, dword ptr [esp + 0x34]
        _emit 0x44
        _emit 0x24
        _emit 0x34
        _emit 0x8D        ; 0040B7CA  lea ecx, [eax]
        _emit 0x08
        _emit 0x33        ; 0040B7CC  xor eax, eax
        _emit 0xC0
        _emit 0x8A        ; 0040B7CE  mov al, byte ptr [edi]
        _emit 0x07
        _emit 0x66        ; 0040B7D0  mov ax, word ptr [eax*2 + 0x4d1a20]
        _emit 0x8B
        _emit 0x04
        _emit 0x45
        _emit 0x20
        _emit 0x1A
        _emit 0x4D
        _emit 0x00
        _emit 0x66        ; 0040B7D8  test ax, ax
        _emit 0x85
        _emit 0xC0
        _emit 0x74        ; 0040B7DB  je 0x40b7f4
        _emit 0x17
        _emit 0x66        ; 0040B7DD  mov bx, word ptr [edx]
        _emit 0x8B
        _emit 0x1A
        _emit 0xD1        ; 0040B7E0  shr ebx, 1
        _emit 0xEB
        _emit 0xD1        ; 0040B7E2  shr eax, 1
        _emit 0xE8
        _emit 0x81        ; 0040B7E4  and ebx, 0x7bef
        _emit 0xE3
        _emit 0xEF
        _emit 0x7B
        _emit 0x00
        _emit 0x00
        _emit 0x25        ; 0040B7EA  and eax, 0x7bef
        _emit 0xEF
        _emit 0x7B
        _emit 0x00
        _emit 0x00
        _emit 0x03        ; 0040B7EF  add ebx, eax
        _emit 0xD8
        _emit 0x66        ; 0040B7F1  mov word ptr [edx], bx
        _emit 0x89
        _emit 0x1A
        _emit 0x83        ; 0040B7F4  add edx, 2
        _emit 0xC2
        _emit 0x02
        _emit 0x03        ; 0040B7F7  add edi, esi
        _emit 0xFE
        _emit 0x49        ; 0040B7F9  dec ecx
        _emit 0x75        ; 0040B7FA  jne 0x40b7cc
        _emit 0xD0
        _emit 0x8B        ; 0040B7FC  mov eax, dword ptr [esp + 0x38]
        _emit 0x44
        _emit 0x24
        _emit 0x38
        _emit 0x8B        ; 0040B800  mov ecx, dword ptr [esp + 0x2c]
        _emit 0x4C
        _emit 0x24
        _emit 0x2C
        _emit 0x03        ; 0040B804  add edi, ebp
        _emit 0xFD
        _emit 0x48        ; 0040B806  dec eax
        _emit 0x8D        ; 0040B807  lea edx, [edx + ecx*2]
        _emit 0x14
        _emit 0x4A
        _emit 0x89        ; 0040B80A  mov dword ptr [esp + 0x38], eax
        _emit 0x44
        _emit 0x24
        _emit 0x38
        _emit 0x75        ; 0040B80E  jne 0x40b7c6
        _emit 0xB6
        _emit 0x5F        ; 0040B810  pop edi
        _emit 0x5E        ; 0040B811  pop esi
        _emit 0x5D        ; 0040B812  pop ebp
        _emit 0x5B        ; 0040B813  pop ebx
        _emit 0x83        ; 0040B814  add esp, 0x14
        _emit 0xC4
        _emit 0x14
        _emit 0xC3        ; 0040B817  ret
        _emit 0x8B        ; 0040B818  mov ebp, dword ptr [esp + 0x40]
        _emit 0x6C
        _emit 0x24
        _emit 0x40
        _emit 0x89        ; 0040B81C  mov dword ptr [esp + 0x38], ebx
        _emit 0x5C
        _emit 0x24
        _emit 0x38
        _emit 0x8B        ; 0040B820  mov eax, dword ptr [esp + 0x34]
        _emit 0x44
        _emit 0x24
        _emit 0x34
        _emit 0x8D        ; 0040B824  lea ecx, [eax]
        _emit 0x08
        _emit 0x33        ; 0040B826  xor eax, eax
        _emit 0xC0
        _emit 0x8A        ; 0040B828  mov al, byte ptr [edi]
        _emit 0x07
        _emit 0x66        ; 0040B82A  mov ax, word ptr [eax*2 + 0x4d1a20]
        _emit 0x8B
        _emit 0x04
        _emit 0x45
        _emit 0x20
        _emit 0x1A
        _emit 0x4D
        _emit 0x00
        _emit 0x66        ; 0040B832  test ax, ax
        _emit 0x85
        _emit 0xC0
        _emit 0x74        ; 0040B835  je 0x40b84e
        _emit 0x17
        _emit 0x66        ; 0040B837  mov bx, word ptr [edx]
        _emit 0x8B
        _emit 0x1A
        _emit 0xD1        ; 0040B83A  shr ebx, 1
        _emit 0xEB
        _emit 0xD1        ; 0040B83C  shr eax, 1
        _emit 0xE8
        _emit 0x81        ; 0040B83E  and ebx, 0x3def
        _emit 0xE3
        _emit 0xEF
        _emit 0x3D
        _emit 0x00
        _emit 0x00
        _emit 0x25        ; 0040B844  and eax, 0x3def
        _emit 0xEF
        _emit 0x3D
        _emit 0x00
        _emit 0x00
        _emit 0x03        ; 0040B849  add ebx, eax
        _emit 0xD8
        _emit 0x66        ; 0040B84B  mov word ptr [edx], bx
        _emit 0x89
        _emit 0x1A
        _emit 0x83        ; 0040B84E  add edx, 2
        _emit 0xC2
        _emit 0x02
        _emit 0x03        ; 0040B851  add edi, esi
        _emit 0xFE
        _emit 0x49        ; 0040B853  dec ecx
        _emit 0x75        ; 0040B854  jne 0x40b826
        _emit 0xD0
        _emit 0x8B        ; 0040B856  mov eax, dword ptr [esp + 0x38]
        _emit 0x44
        _emit 0x24
        _emit 0x38
        _emit 0x8B        ; 0040B85A  mov ecx, dword ptr [esp + 0x2c]
        _emit 0x4C
        _emit 0x24
        _emit 0x2C
        _emit 0x03        ; 0040B85E  add edi, ebp
        _emit 0xFD
        _emit 0x48        ; 0040B860  dec eax
        _emit 0x8D        ; 0040B861  lea edx, [edx + ecx*2]
        _emit 0x14
        _emit 0x4A
        _emit 0x89        ; 0040B864  mov dword ptr [esp + 0x38], eax
        _emit 0x44
        _emit 0x24
        _emit 0x38
        _emit 0x75        ; 0040B868  jne 0x40b820
        _emit 0xB6
        _emit 0x5F        ; 0040B86A  pop edi
        _emit 0x5E        ; 0040B86B  pop esi
        _emit 0x5D        ; 0040B86C  pop ebp
        _emit 0x5B        ; 0040B86D  pop ebx
        _emit 0x83        ; 0040B86E  add esp, 0x14
        _emit 0xC4
        _emit 0x14
        _emit 0xC3        ; 0040B871  ret
        _emit 0xA1        ; 0040B872  mov eax, dword ptr [0x424704]
        _emit 0x04
        _emit 0x47
        _emit 0x42
        _emit 0x00
        _emit 0x85        ; 0040B877  test eax, eax
        _emit 0xC0
        _emit 0x0F        ; 0040B879  je 0x40b936
        _emit 0x84
        _emit 0xB7
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x89        ; 0040B87F  mov dword ptr [esp + 0x3c], ebx
        _emit 0x5C
        _emit 0x24
        _emit 0x3C
        _emit 0x8B        ; 0040B883  mov eax, dword ptr [esp + 0x34]
        _emit 0x44
        _emit 0x24
        _emit 0x34
        _emit 0x89        ; 0040B887  mov dword ptr [esp + 0x38], eax
        _emit 0x44
        _emit 0x24
        _emit 0x38
        _emit 0x33        ; 0040B88B  xor ecx, ecx
        _emit 0xC9
        _emit 0x8A        ; 0040B88D  mov cl, byte ptr [edi]
        _emit 0x0F
        _emit 0x66        ; 0040B88F  mov ax, word ptr [ecx*2 + 0x4d1a20]
        _emit 0x8B
        _emit 0x04
        _emit 0x4D
        _emit 0x20
        _emit 0x1A
        _emit 0x4D
        _emit 0x00
        _emit 0x66        ; 0040B897  test ax, ax
        _emit 0x85
        _emit 0xC0
        _emit 0x74        ; 0040B89A  je 0x40b8fe
        _emit 0x62
        _emit 0x66        ; 0040B89C  mov cx, word ptr [edx]
        _emit 0x8B
        _emit 0x0A
        _emit 0x8A        ; 0040B89F  mov bl, al
        _emit 0xD8
        _emit 0x89        ; 0040B8A1  mov dword ptr [esp + 0x4c], ecx
        _emit 0x4C
        _emit 0x24
        _emit 0x4C
        _emit 0x83        ; 0040B8A5  and ebx, 0x1f
        _emit 0xE3
        _emit 0x1F
        _emit 0x83        ; 0040B8A8  and ecx, 0x1f
        _emit 0xE1
        _emit 0x1F
        _emit 0x03        ; 0040B8AB  add ebx, ecx
        _emit 0xD9
        _emit 0x66        ; 0040B8AD  cmp bx, 0x1f
        _emit 0x83
        _emit 0xFB
        _emit 0x1F
        _emit 0x7E        ; 0040B8B1  jle 0x40b8b8
        _emit 0x05
        _emit 0xBB        ; 0040B8B3  mov ebx, 0x1f
        _emit 0x1F
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040B8B8  mov esi, dword ptr [esp + 0x4c]
        _emit 0x74
        _emit 0x24
        _emit 0x4C
        _emit 0x8B        ; 0040B8BC  mov ecx, eax
        _emit 0xC8
        _emit 0x8B        ; 0040B8BE  mov ebp, esi
        _emit 0xEE
        _emit 0x81        ; 0040B8C0  and ecx, 0x7e0
        _emit 0xE1
        _emit 0xE0
        _emit 0x07
        _emit 0x00
        _emit 0x00
        _emit 0x81        ; 0040B8C6  and ebp, 0x7e0
        _emit 0xE5
        _emit 0xE0
        _emit 0x07
        _emit 0x00
        _emit 0x00
        _emit 0x03        ; 0040B8CC  add ecx, ebp
        _emit 0xCD
        _emit 0x66        ; 0040B8CE  cmp cx, 0x7e0
        _emit 0x81
        _emit 0xF9
        _emit 0xE0
        _emit 0x07
        _emit 0x7E        ; 0040B8D3  jle 0x40b8da
        _emit 0x05
        _emit 0xB9        ; 0040B8D5  mov ecx, 0x7e0
        _emit 0xE0
        _emit 0x07
        _emit 0x00
        _emit 0x00
        _emit 0x25        ; 0040B8DA  and eax, 0xf800
        _emit 0x00
        _emit 0xF8
        _emit 0x00
        _emit 0x00
        _emit 0x81        ; 0040B8DF  and esi, 0xf800
        _emit 0xE6
        _emit 0x00
        _emit 0xF8
        _emit 0x00
        _emit 0x00
        _emit 0x03        ; 0040B8E5  add eax, esi
        _emit 0xC6
        _emit 0x3D        ; 0040B8E7  cmp eax, 0xf800
        _emit 0x00
        _emit 0xF8
        _emit 0x00
        _emit 0x00
        _emit 0x7E        ; 0040B8EC  jle 0x40b8f3
        _emit 0x05
        _emit 0xB8        ; 0040B8EE  mov eax, 0xf800
        _emit 0x00
        _emit 0xF8
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040B8F3  mov esi, dword ptr [esp + 0x28]
        _emit 0x74
        _emit 0x24
        _emit 0x28
        _emit 0x03        ; 0040B8F7  add eax, ecx
        _emit 0xC1
        _emit 0x03        ; 0040B8F9  add eax, ebx
        _emit 0xC3
        _emit 0x66        ; 0040B8FB  mov word ptr [edx], ax
        _emit 0x89
        _emit 0x02
        _emit 0x8B        ; 0040B8FE  mov eax, dword ptr [esp + 0x38]
        _emit 0x44
        _emit 0x24
        _emit 0x38
        _emit 0x83        ; 0040B902  add edx, 2
        _emit 0xC2
        _emit 0x02
        _emit 0x03        ; 0040B905  add edi, esi
        _emit 0xFE
        _emit 0x48        ; 0040B907  dec eax
        _emit 0x89        ; 0040B908  mov dword ptr [esp + 0x38], eax
        _emit 0x44
        _emit 0x24
        _emit 0x38
        _emit 0x0F        ; 0040B90C  jne 0x40b88b
        _emit 0x85
        _emit 0x79
        _emit 0xFF
        _emit 0xFF
        _emit 0xFF
        _emit 0x8B        ; 0040B912  mov eax, dword ptr [esp + 0x2c]
        _emit 0x44
        _emit 0x24
        _emit 0x2C
        _emit 0x8B        ; 0040B916  mov ecx, dword ptr [esp + 0x40]
        _emit 0x4C
        _emit 0x24
        _emit 0x40
        _emit 0x03        ; 0040B91A  add edi, ecx
        _emit 0xF9
        _emit 0x8D        ; 0040B91C  lea edx, [edx + eax*2]
        _emit 0x14
        _emit 0x42
        _emit 0x8B        ; 0040B91F  mov eax, dword ptr [esp + 0x3c]
        _emit 0x44
        _emit 0x24
        _emit 0x3C
        _emit 0x48        ; 0040B923  dec eax
        _emit 0x89        ; 0040B924  mov dword ptr [esp + 0x3c], eax
        _emit 0x44
        _emit 0x24
        _emit 0x3C
        _emit 0x0F        ; 0040B928  jne 0x40b883
        _emit 0x85
        _emit 0x55
        _emit 0xFF
        _emit 0xFF
        _emit 0xFF
        _emit 0x5F        ; 0040B92E  pop edi
        _emit 0x5E        ; 0040B92F  pop esi
        _emit 0x5D        ; 0040B930  pop ebp
        _emit 0x5B        ; 0040B931  pop ebx
        _emit 0x83        ; 0040B932  add esp, 0x14
        _emit 0xC4
        _emit 0x14
        _emit 0xC3        ; 0040B935  ret
        _emit 0x89        ; 0040B936  mov dword ptr [esp + 0x38], ebx
        _emit 0x5C
        _emit 0x24
        _emit 0x38
        _emit 0x8B        ; 0040B93A  mov ecx, dword ptr [esp + 0x34]
        _emit 0x4C
        _emit 0x24
        _emit 0x34
        _emit 0x8D        ; 0040B93E  lea ebp, [ecx]
        _emit 0x29
        _emit 0x33        ; 0040B940  xor eax, eax
        _emit 0xC0
        _emit 0x8A        ; 0040B942  mov al, byte ptr [edi]
        _emit 0x07
        _emit 0x66        ; 0040B944  mov ax, word ptr [eax*2 + 0x4d1a20]
        _emit 0x8B
        _emit 0x04
        _emit 0x45
        _emit 0x20
        _emit 0x1A
        _emit 0x4D
        _emit 0x00
        _emit 0x66        ; 0040B94C  test ax, ax
        _emit 0x85
        _emit 0xC0
        _emit 0x74        ; 0040B94F  je 0x40b976
        _emit 0x25
        _emit 0x66        ; 0040B951  mov cx, word ptr [edx]
        _emit 0x8B
        _emit 0x0A
        _emit 0x25        ; 0040B954  and eax, 0x7bdf
        _emit 0xDF
        _emit 0x7B
        _emit 0x00
        _emit 0x00
        _emit 0x81        ; 0040B959  and ecx, 0x7bdf
        _emit 0xE1
        _emit 0xDF
        _emit 0x7B
        _emit 0x00
        _emit 0x00
        _emit 0x03        ; 0040B95F  add ecx, eax
        _emit 0xC8
        _emit 0x8B        ; 0040B961  mov eax, ecx
        _emit 0xC1
        _emit 0x25        ; 0040B963  and eax, 0x8420
        _emit 0x20
        _emit 0x84
        _emit 0x00
        _emit 0x00
        _emit 0x66        ; 0040B968  mov bx, ax
        _emit 0x8B
        _emit 0xD8
        _emit 0x66        ; 0040B96B  shr bx, 5
        _emit 0xC1
        _emit 0xEB
        _emit 0x05
        _emit 0x2B        ; 0040B96F  sub eax, ebx
        _emit 0xC3
        _emit 0x0B        ; 0040B971  or eax, ecx
        _emit 0xC1
        _emit 0x66        ; 0040B973  mov word ptr [edx], ax
        _emit 0x89
        _emit 0x02
        _emit 0x83        ; 0040B976  add edx, 2
        _emit 0xC2
        _emit 0x02
        _emit 0x03        ; 0040B979  add edi, esi
        _emit 0xFE
        _emit 0x4D        ; 0040B97B  dec ebp
        _emit 0x75        ; 0040B97C  jne 0x40b940
        _emit 0xC2
        _emit 0x8B        ; 0040B97E  mov ecx, dword ptr [esp + 0x40]
        _emit 0x4C
        _emit 0x24
        _emit 0x40
        _emit 0x8B        ; 0040B982  mov eax, dword ptr [esp + 0x38]
        _emit 0x44
        _emit 0x24
        _emit 0x38
        _emit 0x03        ; 0040B986  add edi, ecx
        _emit 0xF9
        _emit 0x8B        ; 0040B988  mov ecx, dword ptr [esp + 0x2c]
        _emit 0x4C
        _emit 0x24
        _emit 0x2C
        _emit 0x48        ; 0040B98C  dec eax
        _emit 0x8D        ; 0040B98D  lea edx, [edx + ecx*2]
        _emit 0x14
        _emit 0x4A
        _emit 0x89        ; 0040B990  mov dword ptr [esp + 0x38], eax
        _emit 0x44
        _emit 0x24
        _emit 0x38
        _emit 0x75        ; 0040B994  jne 0x40b93a
        _emit 0xA4
        _emit 0x5F        ; 0040B996  pop edi
        _emit 0x5E        ; 0040B997  pop esi
        _emit 0x5D        ; 0040B998  pop ebp
        _emit 0x5B        ; 0040B999  pop ebx
        _emit 0x83        ; 0040B99A  add esp, 0x14
        _emit 0xC4
        _emit 0x14
        _emit 0xC3        ; 0040B99D  ret
        _emit 0xA1        ; 0040B99E  mov eax, dword ptr [0x424704]
        _emit 0x04
        _emit 0x47
        _emit 0x42
        _emit 0x00
        _emit 0x85        ; 0040B9A3  test eax, eax
        _emit 0xC0
        _emit 0x0F        ; 0040B9A5  je 0x40ba53
        _emit 0x84
        _emit 0xA8
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x89        ; 0040B9AB  mov dword ptr [esp + 0x4c], ebx
        _emit 0x5C
        _emit 0x24
        _emit 0x4C
        _emit 0x8B        ; 0040B9AF  mov eax, dword ptr [esp + 0x34]
        _emit 0x44
        _emit 0x24
        _emit 0x34
        _emit 0x89        ; 0040B9B3  mov dword ptr [esp + 0x3c], eax
        _emit 0x44
        _emit 0x24
        _emit 0x3C
        _emit 0x33        ; 0040B9B7  xor ecx, ecx
        _emit 0xC9
        _emit 0x8A        ; 0040B9B9  mov cl, byte ptr [edi]
        _emit 0x0F
        _emit 0x66        ; 0040B9BB  mov cx, word ptr [ecx*2 + 0x4d1a20]
        _emit 0x8B
        _emit 0x0C
        _emit 0x4D
        _emit 0x20
        _emit 0x1A
        _emit 0x4D
        _emit 0x00
        _emit 0x66        ; 0040B9C3  test cx, cx
        _emit 0x85
        _emit 0xC9
        _emit 0x89        ; 0040B9C6  mov dword ptr [esp + 0x38], ecx
        _emit 0x4C
        _emit 0x24
        _emit 0x38
        _emit 0x74        ; 0040B9CA  je 0x40ba1f
        _emit 0x53
        _emit 0x66        ; 0040B9CC  mov ax, word ptr [edx]
        _emit 0x8B
        _emit 0x02
        _emit 0x83        ; 0040B9CF  and ecx, 0x1f
        _emit 0xE1
        _emit 0x1F
        _emit 0x8A        ; 0040B9D2  mov bl, al
        _emit 0xD8
        _emit 0x83        ; 0040B9D4  and ebx, 0x1f
        _emit 0xE3
        _emit 0x1F
        _emit 0x2B        ; 0040B9D7  sub ebx, ecx
        _emit 0xD9
        _emit 0x66        ; 0040B9D9  test bx, bx
        _emit 0x85
        _emit 0xDB
        _emit 0x7D        ; 0040B9DC  jge 0x40b9e0
        _emit 0x02
        _emit 0x33        ; 0040B9DE  xor ebx, ebx
        _emit 0xDB
        _emit 0x8B        ; 0040B9E0  mov esi, dword ptr [esp + 0x38]
        _emit 0x74
        _emit 0x24
        _emit 0x38
        _emit 0x8B        ; 0040B9E4  mov ecx, eax
        _emit 0xC8
        _emit 0x8B        ; 0040B9E6  mov ebp, esi
        _emit 0xEE
        _emit 0x81        ; 0040B9E8  and ecx, 0x7e0
        _emit 0xE1
        _emit 0xE0
        _emit 0x07
        _emit 0x00
        _emit 0x00
        _emit 0x81        ; 0040B9EE  and ebp, 0x7e0
        _emit 0xE5
        _emit 0xE0
        _emit 0x07
        _emit 0x00
        _emit 0x00
        _emit 0x2B        ; 0040B9F4  sub ecx, ebp
        _emit 0xCD
        _emit 0x66        ; 0040B9F6  cmp cx, 0x40
        _emit 0x83
        _emit 0xF9
        _emit 0x40
        _emit 0x7D        ; 0040B9FA  jge 0x40b9fe
        _emit 0x02
        _emit 0x33        ; 0040B9FC  xor ecx, ecx
        _emit 0xC9
        _emit 0x81        ; 0040B9FE  and esi, 0xf800
        _emit 0xE6
        _emit 0x00
        _emit 0xF8
        _emit 0x00
        _emit 0x00
        _emit 0x25        ; 0040BA04  and eax, 0xf800
        _emit 0x00
        _emit 0xF8
        _emit 0x00
        _emit 0x00
        _emit 0x2B        ; 0040BA09  sub eax, esi
        _emit 0xC6
        _emit 0x3D        ; 0040BA0B  cmp eax, 0x800
        _emit 0x00
        _emit 0x08
        _emit 0x00
        _emit 0x00
        _emit 0x7D        ; 0040BA10  jge 0x40ba14
        _emit 0x02
        _emit 0x33        ; 0040BA12  xor eax, eax
        _emit 0xC0
        _emit 0x8B        ; 0040BA14  mov esi, dword ptr [esp + 0x28]
        _emit 0x74
        _emit 0x24
        _emit 0x28
        _emit 0x03        ; 0040BA18  add eax, ecx
        _emit 0xC1
        _emit 0x03        ; 0040BA1A  add eax, ebx
        _emit 0xC3
        _emit 0x66        ; 0040BA1C  mov word ptr [edx], ax
        _emit 0x89
        _emit 0x02
        _emit 0x8B        ; 0040BA1F  mov eax, dword ptr [esp + 0x3c]
        _emit 0x44
        _emit 0x24
        _emit 0x3C
        _emit 0x83        ; 0040BA23  add edx, 2
        _emit 0xC2
        _emit 0x02
        _emit 0x03        ; 0040BA26  add edi, esi
        _emit 0xFE
        _emit 0x48        ; 0040BA28  dec eax
        _emit 0x89        ; 0040BA29  mov dword ptr [esp + 0x3c], eax
        _emit 0x44
        _emit 0x24
        _emit 0x3C
        _emit 0x75        ; 0040BA2D  jne 0x40b9b7
        _emit 0x88
        _emit 0x8B        ; 0040BA2F  mov eax, dword ptr [esp + 0x2c]
        _emit 0x44
        _emit 0x24
        _emit 0x2C
        _emit 0x8B        ; 0040BA33  mov ecx, dword ptr [esp + 0x40]
        _emit 0x4C
        _emit 0x24
        _emit 0x40
        _emit 0x03        ; 0040BA37  add edi, ecx
        _emit 0xF9
        _emit 0x8D        ; 0040BA39  lea edx, [edx + eax*2]
        _emit 0x14
        _emit 0x42
        _emit 0x8B        ; 0040BA3C  mov eax, dword ptr [esp + 0x4c]
        _emit 0x44
        _emit 0x24
        _emit 0x4C
        _emit 0x48        ; 0040BA40  dec eax
        _emit 0x89        ; 0040BA41  mov dword ptr [esp + 0x4c], eax
        _emit 0x44
        _emit 0x24
        _emit 0x4C
        _emit 0x0F        ; 0040BA45  jne 0x40b9af
        _emit 0x85
        _emit 0x64
        _emit 0xFF
        _emit 0xFF
        _emit 0xFF
        _emit 0x5F        ; 0040BA4B  pop edi
        _emit 0x5E        ; 0040BA4C  pop esi
        _emit 0x5D        ; 0040BA4D  pop ebp
        _emit 0x5B        ; 0040BA4E  pop ebx
        _emit 0x83        ; 0040BA4F  add esp, 0x14
        _emit 0xC4
        _emit 0x14
        _emit 0xC3        ; 0040BA52  ret
        _emit 0x89        ; 0040BA53  mov dword ptr [esp + 0x4c], ebx
        _emit 0x5C
        _emit 0x24
        _emit 0x4C
        _emit 0x8B        ; 0040BA57  mov ecx, dword ptr [esp + 0x34]
        _emit 0x4C
        _emit 0x24
        _emit 0x34
        _emit 0x89        ; 0040BA5B  mov dword ptr [esp + 0x3c], ecx
        _emit 0x4C
        _emit 0x24
        _emit 0x3C
        _emit 0x33        ; 0040BA5F  xor eax, eax
        _emit 0xC0
        _emit 0x8A        ; 0040BA61  mov al, byte ptr [edi]
        _emit 0x07
        _emit 0x66        ; 0040BA63  mov cx, word ptr [eax*2 + 0x4d1a20]
        _emit 0x8B
        _emit 0x0C
        _emit 0x45
        _emit 0x20
        _emit 0x1A
        _emit 0x4D
        _emit 0x00
        _emit 0x66        ; 0040BA6B  test cx, cx
        _emit 0x85
        _emit 0xC9
        _emit 0x89        ; 0040BA6E  mov dword ptr [esp + 0x38], ecx
        _emit 0x4C
        _emit 0x24
        _emit 0x38
        _emit 0x74        ; 0040BA72  je 0x40bac6
        _emit 0x52
        _emit 0x66        ; 0040BA74  mov ax, word ptr [edx]
        _emit 0x8B
        _emit 0x02
        _emit 0x83        ; 0040BA77  and ecx, 0x1f
        _emit 0xE1
        _emit 0x1F
        _emit 0x8A        ; 0040BA7A  mov bl, al
        _emit 0xD8
        _emit 0x83        ; 0040BA7C  and ebx, 0x1f
        _emit 0xE3
        _emit 0x1F
        _emit 0x2B        ; 0040BA7F  sub ebx, ecx
        _emit 0xD9
        _emit 0x66        ; 0040BA81  test bx, bx
        _emit 0x85
        _emit 0xDB
        _emit 0x7D        ; 0040BA84  jge 0x40ba88
        _emit 0x02
        _emit 0x33        ; 0040BA86  xor ebx, ebx
        _emit 0xDB
        _emit 0x8B        ; 0040BA88  mov esi, dword ptr [esp + 0x38]
        _emit 0x74
        _emit 0x24
        _emit 0x38
        _emit 0x8B        ; 0040BA8C  mov ecx, eax
        _emit 0xC8
        _emit 0x8B        ; 0040BA8E  mov ebp, esi
        _emit 0xEE
        _emit 0x81        ; 0040BA90  and ecx, 0x3e0
        _emit 0xE1
        _emit 0xE0
        _emit 0x03
        _emit 0x00
        _emit 0x00
        _emit 0x81        ; 0040BA96  and ebp, 0x3e0
        _emit 0xE5
        _emit 0xE0
        _emit 0x03
        _emit 0x00
        _emit 0x00
        _emit 0x2B        ; 0040BA9C  sub ecx, ebp
        _emit 0xCD
        _emit 0x66        ; 0040BA9E  cmp cx, 0x20
        _emit 0x83
        _emit 0xF9
        _emit 0x20
        _emit 0x7D        ; 0040BAA2  jge 0x40baa6
        _emit 0x02
        _emit 0x33        ; 0040BAA4  xor ecx, ecx
        _emit 0xC9
        _emit 0x81        ; 0040BAA6  and esi, 0x7c00
        _emit 0xE6
        _emit 0x00
        _emit 0x7C
        _emit 0x00
        _emit 0x00
        _emit 0x25        ; 0040BAAC  and eax, 0x7c00
        _emit 0x00
        _emit 0x7C
        _emit 0x00
        _emit 0x00
        _emit 0x2B        ; 0040BAB1  sub eax, esi
        _emit 0xC6
        _emit 0x66        ; 0040BAB3  cmp ax, 0x400
        _emit 0x3D
        _emit 0x00
        _emit 0x04
        _emit 0x7D        ; 0040BAB7  jge 0x40babb
        _emit 0x02
        _emit 0x33        ; 0040BAB9  xor eax, eax
        _emit 0xC0
        _emit 0x8B        ; 0040BABB  mov esi, dword ptr [esp + 0x28]
        _emit 0x74
        _emit 0x24
        _emit 0x28
        _emit 0x03        ; 0040BABF  add eax, ecx
        _emit 0xC1
        _emit 0x03        ; 0040BAC1  add eax, ebx
        _emit 0xC3
        _emit 0x66        ; 0040BAC3  mov word ptr [edx], ax
        _emit 0x89
        _emit 0x02
        _emit 0x8B        ; 0040BAC6  mov eax, dword ptr [esp + 0x3c]
        _emit 0x44
        _emit 0x24
        _emit 0x3C
        _emit 0x83        ; 0040BACA  add edx, 2
        _emit 0xC2
        _emit 0x02
        _emit 0x03        ; 0040BACD  add edi, esi
        _emit 0xFE
        _emit 0x48        ; 0040BACF  dec eax
        _emit 0x89        ; 0040BAD0  mov dword ptr [esp + 0x3c], eax
        _emit 0x44
        _emit 0x24
        _emit 0x3C
        _emit 0x75        ; 0040BAD4  jne 0x40ba5f
        _emit 0x89
        _emit 0x8B        ; 0040BAD6  mov ecx, dword ptr [esp + 0x40]
        _emit 0x4C
        _emit 0x24
        _emit 0x40
        _emit 0x8B        ; 0040BADA  mov eax, dword ptr [esp + 0x4c]
        _emit 0x44
        _emit 0x24
        _emit 0x4C
        _emit 0x03        ; 0040BADE  add edi, ecx
        _emit 0xF9
        _emit 0x8B        ; 0040BAE0  mov ecx, dword ptr [esp + 0x2c]
        _emit 0x4C
        _emit 0x24
        _emit 0x2C
        _emit 0x48        ; 0040BAE4  dec eax
        _emit 0x8D        ; 0040BAE5  lea edx, [edx + ecx*2]
        _emit 0x14
        _emit 0x4A
        _emit 0x89        ; 0040BAE8  mov dword ptr [esp + 0x4c], eax
        _emit 0x44
        _emit 0x24
        _emit 0x4C
        _emit 0x0F        ; 0040BAEC  jne 0x40ba57
        _emit 0x85
        _emit 0x65
        _emit 0xFF
        _emit 0xFF
        _emit 0xFF
        _emit 0x5F        ; 0040BAF2  pop edi
        _emit 0x5E        ; 0040BAF3  pop esi
        _emit 0x5D        ; 0040BAF4  pop ebp
        _emit 0x5B        ; 0040BAF5  pop ebx
        _emit 0x83        ; 0040BAF6  add esp, 0x14
        _emit 0xC4
        _emit 0x14
        _emit 0xC3        ; 0040BAF9  ret
        _emit 0x8B        ; 0040BAFA  mov ecx, dword ptr [esp + 0x48]
        _emit 0x4C
        _emit 0x24
        _emit 0x48
        _emit 0x8B        ; 0040BAFE  mov eax, dword ptr [esp + 0x4c]
        _emit 0x44
        _emit 0x24
        _emit 0x4C
        _emit 0xBE        ; 0040BB02  mov esi, 0x20
        _emit 0x20
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0xBB        ; 0040BB07  mov ebx, 0x20
        _emit 0x20
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x2B        ; 0040BB0C  sub esi, ecx
        _emit 0xF1
        _emit 0x8B        ; 0040BB0E  mov ecx, dword ptr [esp + 0x50]
        _emit 0x4C
        _emit 0x24
        _emit 0x50
        _emit 0x2B        ; 0040BB12  sub ebx, eax
        _emit 0xD8
        _emit 0xB8        ; 0040BB14  mov eax, 0x20
        _emit 0x20
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x2B        ; 0040BB19  sub eax, ecx
        _emit 0xC1
        _emit 0x8B        ; 0040BB1B  mov ecx, dword ptr [0x424704]
        _emit 0x0D
        _emit 0x04
        _emit 0x47
        _emit 0x42
        _emit 0x00
        _emit 0x85        ; 0040BB21  test ecx, ecx
        _emit 0xC9
        _emit 0x89        ; 0040BB23  mov dword ptr [esp + 0x44], ebx
        _emit 0x5C
        _emit 0x24
        _emit 0x44
        _emit 0x89        ; 0040BB27  mov dword ptr [esp + 0x30], eax
        _emit 0x44
        _emit 0x24
        _emit 0x30
        _emit 0x0F        ; 0040BB2B  je 0x40bc37
        _emit 0x84
        _emit 0x06
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x8D        ; 0040BB31  lea ecx, [ebx + ebx]
        _emit 0x0C
        _emit 0x1B
        _emit 0x03        ; 0040BB34  add eax, eax
        _emit 0xC0
        _emit 0x89        ; 0040BB36  mov dword ptr [esp + 0x44], ecx
        _emit 0x4C
        _emit 0x24
        _emit 0x44
        _emit 0x8B        ; 0040BB3A  mov ecx, dword ptr [esp + 0x38]
        _emit 0x4C
        _emit 0x24
        _emit 0x38
        _emit 0x89        ; 0040BB3E  mov dword ptr [esp + 0x30], eax
        _emit 0x44
        _emit 0x24
        _emit 0x30
        _emit 0x89        ; 0040BB42  mov dword ptr [esp + 0x1c], ecx
        _emit 0x4C
        _emit 0x24
        _emit 0x1C
        _emit 0x8B        ; 0040BB46  mov eax, dword ptr [esp + 0x34]
        _emit 0x44
        _emit 0x24
        _emit 0x34
        _emit 0x89        ; 0040BB4A  mov dword ptr [esp + 0x38], eax
        _emit 0x44
        _emit 0x24
        _emit 0x38
        _emit 0x33        ; 0040BB4E  xor eax, eax
        _emit 0xC0
        _emit 0x8A        ; 0040BB50  mov al, byte ptr [edi]
        _emit 0x07
        _emit 0xD1        ; 0040BB52  shl eax, 1
        _emit 0xE0
        _emit 0x66        ; 0040BB54  cmp word ptr [eax + 0x4d1a20], 0
        _emit 0x83
        _emit 0xB8
        _emit 0x20
        _emit 0x1A
        _emit 0x4D
        _emit 0x00
        _emit 0x00
        _emit 0x0F        ; 0040BB5C  je 0x40bbf3
        _emit 0x84
        _emit 0x91
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x66        ; 0040BB62  mov ax, word ptr [eax + ebp]
        _emit 0x8B
        _emit 0x04
        _emit 0x28
        _emit 0x8B        ; 0040BB66  mov ebx, dword ptr [esp + 0x30]
        _emit 0x5C
        _emit 0x24
        _emit 0x30
        _emit 0x66        ; 0040BB6A  mov cx, word ptr [edx]
        _emit 0x8B
        _emit 0x0A
        _emit 0x8B        ; 0040BB6D  mov ebp, dword ptr [esp + 0x50]
        _emit 0x6C
        _emit 0x24
        _emit 0x50
        _emit 0x25        ; 0040BB71  and eax, 0xffff
        _emit 0xFF
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x81        ; 0040BB76  and ebx, 0xffff
        _emit 0xE3
        _emit 0xFF
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040BB7C  mov edi, eax
        _emit 0xF8
        _emit 0x81        ; 0040BB7E  and ecx, 0xffff
        _emit 0xE1
        _emit 0xFF
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x81        ; 0040BB84  and edi, 0x7c00
        _emit 0xE7
        _emit 0x00
        _emit 0x7C
        _emit 0x00
        _emit 0x00
        _emit 0x81        ; 0040BB8A  and ebp, 0xffff
        _emit 0xE5
        _emit 0xFF
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x0F        ; 0040BB90  imul edi, ebx
        _emit 0xAF
        _emit 0xFB
        _emit 0x8B        ; 0040BB93  mov ebx, ecx
        _emit 0xD9
        _emit 0x81        ; 0040BB95  and ebx, 0xf800
        _emit 0xE3
        _emit 0x00
        _emit 0xF8
        _emit 0x00
        _emit 0x00
        _emit 0x0F        ; 0040BB9B  imul ebx, ebp
        _emit 0xAF
        _emit 0xDD
        _emit 0x03        ; 0040BB9E  add edi, ebx
        _emit 0xFB
        _emit 0x8B        ; 0040BBA0  mov ebx, ecx
        _emit 0xD9
        _emit 0x8B        ; 0040BBA2  mov ebp, eax
        _emit 0xE8
        _emit 0x81        ; 0040BBA4  and ebx, 0x7e0
        _emit 0xE3
        _emit 0xE0
        _emit 0x07
        _emit 0x00
        _emit 0x00
        _emit 0x0F        ; 0040BBAA  imul ebx, dword ptr [esp + 0x4c]
        _emit 0xAF
        _emit 0x5C
        _emit 0x24
        _emit 0x4C
        _emit 0x81        ; 0040BBAF  and ebp, 0x3e0
        _emit 0xE5
        _emit 0xE0
        _emit 0x03
        _emit 0x00
        _emit 0x00
        _emit 0x83        ; 0040BBB5  and eax, 0x1f
        _emit 0xE0
        _emit 0x1F
        _emit 0x0F        ; 0040BBB8  imul ebp, dword ptr [esp + 0x44]
        _emit 0xAF
        _emit 0x6C
        _emit 0x24
        _emit 0x44
        _emit 0x0F        ; 0040BBBD  imul eax, esi
        _emit 0xAF
        _emit 0xC6
        _emit 0x83        ; 0040BBC0  and ecx, 0x1f
        _emit 0xE1
        _emit 0x1F
        _emit 0x03        ; 0040BBC3  add ebx, ebp
        _emit 0xDD
        _emit 0x0F        ; 0040BBC5  imul ecx, dword ptr [esp + 0x48]
        _emit 0xAF
        _emit 0x4C
        _emit 0x24
        _emit 0x48
        _emit 0xC1        ; 0040BBCA  sar edi, 5
        _emit 0xFF
        _emit 0x05
        _emit 0xC1        ; 0040BBCD  sar ebx, 5
        _emit 0xFB
        _emit 0x05
        _emit 0x03        ; 0040BBD0  add eax, ecx
        _emit 0xC1
        _emit 0x8B        ; 0040BBD2  mov ebp, dword ptr [esp + 0x20]
        _emit 0x6C
        _emit 0x24
        _emit 0x20
        _emit 0x81        ; 0040BBD6  and edi, 0xf800
        _emit 0xE7
        _emit 0x00
        _emit 0xF8
        _emit 0x00
        _emit 0x00
        _emit 0x81        ; 0040BBDC  and ebx, 0x7e0
        _emit 0xE3
        _emit 0xE0
        _emit 0x07
        _emit 0x00
        _emit 0x00
        _emit 0xC1        ; 0040BBE2  sar eax, 5
        _emit 0xF8
        _emit 0x05
        _emit 0x03        ; 0040BBE5  add edi, ebx
        _emit 0xFB
        _emit 0x83        ; 0040BBE7  and eax, 0x1f
        _emit 0xE0
        _emit 0x1F
        _emit 0x03        ; 0040BBEA  add edi, eax
        _emit 0xF8
        _emit 0x66        ; 0040BBEC  mov word ptr [edx], di
        _emit 0x89
        _emit 0x3A
        _emit 0x8B        ; 0040BBEF  mov edi, dword ptr [esp + 0x3c]
        _emit 0x7C
        _emit 0x24
        _emit 0x3C
        _emit 0x8B        ; 0040BBF3  mov ecx, dword ptr [esp + 0x28]
        _emit 0x4C
        _emit 0x24
        _emit 0x28
        _emit 0x8B        ; 0040BBF7  mov eax, dword ptr [esp + 0x38]
        _emit 0x44
        _emit 0x24
        _emit 0x38
        _emit 0x83        ; 0040BBFB  add edx, 2
        _emit 0xC2
        _emit 0x02
        _emit 0x03        ; 0040BBFE  add edi, ecx
        _emit 0xF9
        _emit 0x48        ; 0040BC00  dec eax
        _emit 0x89        ; 0040BC01  mov dword ptr [esp + 0x3c], edi
        _emit 0x7C
        _emit 0x24
        _emit 0x3C
        _emit 0x89        ; 0040BC05  mov dword ptr [esp + 0x38], eax
        _emit 0x44
        _emit 0x24
        _emit 0x38
        _emit 0x0F        ; 0040BC09  jne 0x40bb4e
        _emit 0x85
        _emit 0x3F
        _emit 0xFF
        _emit 0xFF
        _emit 0xFF
        _emit 0x8B        ; 0040BC0F  mov ecx, dword ptr [esp + 0x40]
        _emit 0x4C
        _emit 0x24
        _emit 0x40
        _emit 0x8B        ; 0040BC13  mov eax, dword ptr [esp + 0x1c]
        _emit 0x44
        _emit 0x24
        _emit 0x1C
        _emit 0x03        ; 0040BC17  add edi, ecx
        _emit 0xF9
        _emit 0x8B        ; 0040BC19  mov ecx, dword ptr [esp + 0x2c]
        _emit 0x4C
        _emit 0x24
        _emit 0x2C
        _emit 0x48        ; 0040BC1D  dec eax
        _emit 0x89        ; 0040BC1E  mov dword ptr [esp + 0x3c], edi
        _emit 0x7C
        _emit 0x24
        _emit 0x3C
        _emit 0x8D        ; 0040BC22  lea edx, [edx + ecx*2]
        _emit 0x14
        _emit 0x4A
        _emit 0x89        ; 0040BC25  mov dword ptr [esp + 0x1c], eax
        _emit 0x44
        _emit 0x24
        _emit 0x1C
        _emit 0x0F        ; 0040BC29  jne 0x40bb46
        _emit 0x85
        _emit 0x17
        _emit 0xFF
        _emit 0xFF
        _emit 0xFF
        _emit 0x5F        ; 0040BC2F  pop edi
        _emit 0x5E        ; 0040BC30  pop esi
        _emit 0x5D        ; 0040BC31  pop ebp
        _emit 0x5B        ; 0040BC32  pop ebx
        _emit 0x83        ; 0040BC33  add esp, 0x14
        _emit 0xC4
        _emit 0x14
        _emit 0xC3        ; 0040BC36  ret
        _emit 0x8B        ; 0040BC37  mov eax, dword ptr [esp + 0x38]
        _emit 0x44
        _emit 0x24
        _emit 0x38
        _emit 0x89        ; 0040BC3B  mov dword ptr [esp + 0x1c], eax
        _emit 0x44
        _emit 0x24
        _emit 0x1C
        _emit 0x8B        ; 0040BC3F  mov ecx, dword ptr [esp + 0x34]
        _emit 0x4C
        _emit 0x24
        _emit 0x34
        _emit 0x89        ; 0040BC43  mov dword ptr [esp + 0x38], ecx
        _emit 0x4C
        _emit 0x24
        _emit 0x38
        _emit 0x33        ; 0040BC47  xor eax, eax
        _emit 0xC0
        _emit 0x8A        ; 0040BC49  mov al, byte ptr [edi]
        _emit 0x07
        _emit 0xD1        ; 0040BC4B  shl eax, 1
        _emit 0xE0
        _emit 0x66        ; 0040BC4D  cmp word ptr [eax + 0x4d1a20], 0
        _emit 0x83
        _emit 0xB8
        _emit 0x20
        _emit 0x1A
        _emit 0x4D
        _emit 0x00
        _emit 0x00
        _emit 0x0F        ; 0040BC55  je 0x40bcec
        _emit 0x84
        _emit 0x91
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x66        ; 0040BC5B  mov ax, word ptr [eax + ebp]
        _emit 0x8B
        _emit 0x04
        _emit 0x28
        _emit 0x66        ; 0040BC5F  mov cx, word ptr [edx]
        _emit 0x8B
        _emit 0x0A
        _emit 0x25        ; 0040BC62  and eax, 0xffff
        _emit 0xFF
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x81        ; 0040BC67  and ecx, 0xffff
        _emit 0xE1
        _emit 0xFF
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040BC6D  mov edi, eax
        _emit 0xF8
        _emit 0x8B        ; 0040BC6F  mov ebp, ecx
        _emit 0xE9
        _emit 0x81        ; 0040BC71  and edi, 0x3e0
        _emit 0xE7
        _emit 0xE0
        _emit 0x03
        _emit 0x00
        _emit 0x00
        _emit 0x83        ; 0040BC77  and ebp, 0x1f
        _emit 0xE5
        _emit 0x1F
        _emit 0x0F        ; 0040BC7A  imul edi, ebx
        _emit 0xAF
        _emit 0xFB
        _emit 0x0F        ; 0040BC7D  imul ebp, dword ptr [esp + 0x48]
        _emit 0xAF
        _emit 0x6C
        _emit 0x24
        _emit 0x48
        _emit 0x8B        ; 0040BC82  mov ebx, ecx
        _emit 0xD9
        _emit 0x81        ; 0040BC84  and ecx, 0x7c00
        _emit 0xE1
        _emit 0x00
        _emit 0x7C
        _emit 0x00
        _emit 0x00
        _emit 0x81        ; 0040BC8A  and ebx, 0x3e0
        _emit 0xE3
        _emit 0xE0
        _emit 0x03
        _emit 0x00
        _emit 0x00
        _emit 0x0F        ; 0040BC90  imul ebx, dword ptr [esp + 0x4c]
        _emit 0xAF
        _emit 0x5C
        _emit 0x24
        _emit 0x4C
        _emit 0x03        ; 0040BC95  add edi, ebx
        _emit 0xFB
        _emit 0x8B        ; 0040BC97  mov ebx, eax
        _emit 0xD8
        _emit 0x83        ; 0040BC99  and ebx, 0x1f
        _emit 0xE3
        _emit 0x1F
        _emit 0x25        ; 0040BC9C  and eax, 0x7c00
        _emit 0x00
        _emit 0x7C
        _emit 0x00
        _emit 0x00
        _emit 0x0F        ; 0040BCA1  imul ebx, esi
        _emit 0xAF
        _emit 0xDE
        _emit 0x03        ; 0040BCA4  add ebx, ebp
        _emit 0xDD
        _emit 0x8B        ; 0040BCA6  mov ebp, dword ptr [esp + 0x20]
        _emit 0x6C
        _emit 0x24
        _emit 0x20
        _emit 0xC1        ; 0040BCAA  sar edi, 5
        _emit 0xFF
        _emit 0x05
        _emit 0xC1        ; 0040BCAD  sar ebx, 5
        _emit 0xFB
        _emit 0x05
        _emit 0x81        ; 0040BCB0  and edi, 0x3e0
        _emit 0xE7
        _emit 0xE0
        _emit 0x03
        _emit 0x00
        _emit 0x00
        _emit 0x83        ; 0040BCB6  and ebx, 0x1f
        _emit 0xE3
        _emit 0x1F
        _emit 0x03        ; 0040BCB9  add edi, ebx
        _emit 0xFB
        _emit 0x8B        ; 0040BCBB  mov ebx, dword ptr [esp + 0x30]
        _emit 0x5C
        _emit 0x24
        _emit 0x30
        _emit 0x81        ; 0040BCBF  and ebx, 0xffff
        _emit 0xE3
        _emit 0xFF
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x0F        ; 0040BCC5  imul eax, ebx
        _emit 0xAF
        _emit 0xC3
        _emit 0x8B        ; 0040BCC8  mov ebx, dword ptr [esp + 0x50]
        _emit 0x5C
        _emit 0x24
        _emit 0x50
        _emit 0x81        ; 0040BCCC  and ebx, 0xffff
        _emit 0xE3
        _emit 0xFF
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x0F        ; 0040BCD2  imul ecx, ebx
        _emit 0xAF
        _emit 0xCB
        _emit 0x03        ; 0040BCD5  add eax, ecx
        _emit 0xC1
        _emit 0x8B        ; 0040BCD7  mov ebx, dword ptr [esp + 0x44]
        _emit 0x5C
        _emit 0x24
        _emit 0x44
        _emit 0xC1        ; 0040BCDB  sar eax, 5
        _emit 0xF8
        _emit 0x05
        _emit 0x25        ; 0040BCDE  and eax, 0x7c00
        _emit 0x00
        _emit 0x7C
        _emit 0x00
        _emit 0x00
        _emit 0x03        ; 0040BCE3  add edi, eax
        _emit 0xF8
        _emit 0x66        ; 0040BCE5  mov word ptr [edx], di
        _emit 0x89
        _emit 0x3A
        _emit 0x8B        ; 0040BCE8  mov edi, dword ptr [esp + 0x3c]
        _emit 0x7C
        _emit 0x24
        _emit 0x3C
        _emit 0x8B        ; 0040BCEC  mov ecx, dword ptr [esp + 0x28]
        _emit 0x4C
        _emit 0x24
        _emit 0x28
        _emit 0x8B        ; 0040BCF0  mov eax, dword ptr [esp + 0x38]
        _emit 0x44
        _emit 0x24
        _emit 0x38
        _emit 0x83        ; 0040BCF4  add edx, 2
        _emit 0xC2
        _emit 0x02
        _emit 0x03        ; 0040BCF7  add edi, ecx
        _emit 0xF9
        _emit 0x48        ; 0040BCF9  dec eax
        _emit 0x89        ; 0040BCFA  mov dword ptr [esp + 0x3c], edi
        _emit 0x7C
        _emit 0x24
        _emit 0x3C
        _emit 0x89        ; 0040BCFE  mov dword ptr [esp + 0x38], eax
        _emit 0x44
        _emit 0x24
        _emit 0x38
        _emit 0x0F        ; 0040BD02  jne 0x40bc47
        _emit 0x85
        _emit 0x3F
        _emit 0xFF
        _emit 0xFF
        _emit 0xFF
        _emit 0x8B        ; 0040BD08  mov eax, dword ptr [esp + 0x2c]
        _emit 0x44
        _emit 0x24
        _emit 0x2C
        _emit 0x8B        ; 0040BD0C  mov ecx, dword ptr [esp + 0x40]
        _emit 0x4C
        _emit 0x24
        _emit 0x40
        _emit 0x03        ; 0040BD10  add edi, ecx
        _emit 0xF9
        _emit 0x8D        ; 0040BD12  lea edx, [edx + eax*2]
        _emit 0x14
        _emit 0x42
        _emit 0x8B        ; 0040BD15  mov eax, dword ptr [esp + 0x1c]
        _emit 0x44
        _emit 0x24
        _emit 0x1C
        _emit 0x48        ; 0040BD19  dec eax
        _emit 0x89        ; 0040BD1A  mov dword ptr [esp + 0x3c], edi
        _emit 0x7C
        _emit 0x24
        _emit 0x3C
        _emit 0x89        ; 0040BD1E  mov dword ptr [esp + 0x1c], eax
        _emit 0x44
        _emit 0x24
        _emit 0x1C
        _emit 0x0F        ; 0040BD22  jne 0x40bc3f
        _emit 0x85
        _emit 0x17
        _emit 0xFF
        _emit 0xFF
        _emit 0xFF
        _emit 0x5F        ; 0040BD28  pop edi
        _emit 0x5E        ; 0040BD29  pop esi
        _emit 0x5D        ; 0040BD2A  pop ebp
        _emit 0x5B        ; 0040BD2B  pop ebx
        _emit 0x83        ; 0040BD2C  add esp, 0x14
        _emit 0xC4
        _emit 0x14
        _emit 0xC3        ; 0040BD2F  ret
        _emit 0x8B        ; 0040BD30  mov ecx, dword ptr [esp + 0x2c]
        _emit 0x4C
        _emit 0x24
        _emit 0x2C
        _emit 0x8D        ; 0040BD34  lea ebp, [ebx]
        _emit 0x2B
        _emit 0x8B        ; 0040BD36  mov eax, dword ptr [esp + 0x34]
        _emit 0x44
        _emit 0x24
        _emit 0x34
        _emit 0x33        ; 0040BD3A  xor ebx, ebx
        _emit 0xDB
        _emit 0x83        ; 0040BD3C  add edx, 2
        _emit 0xC2
        _emit 0x02
        _emit 0x8A        ; 0040BD3F  mov bl, byte ptr [edi]
        _emit 0x1F
        _emit 0x03        ; 0040BD41  add edi, esi
        _emit 0xFE
        _emit 0x48        ; 0040BD43  dec eax
        _emit 0x66        ; 0040BD44  mov bx, word ptr [ebx*2 + 0x4d1a20]
        _emit 0x8B
        _emit 0x1C
        _emit 0x5D
        _emit 0x20
        _emit 0x1A
        _emit 0x4D
        _emit 0x00
        _emit 0x66        ; 0040BD4C  mov word ptr [edx - 2], bx
        _emit 0x89
        _emit 0x5A
        _emit 0xFE
        _emit 0x75        ; 0040BD50  jne 0x40bd3a
        _emit 0xE8
        _emit 0x8B        ; 0040BD52  mov ebx, dword ptr [esp + 0x40]
        _emit 0x5C
        _emit 0x24
        _emit 0x40
        _emit 0x8D        ; 0040BD56  lea edx, [edx + ecx*2]
        _emit 0x14
        _emit 0x4A
        _emit 0x03        ; 0040BD59  add edi, ebx
        _emit 0xFB
        _emit 0x4D        ; 0040BD5B  dec ebp
        _emit 0x75        ; 0040BD5C  jne 0x40bd36
        _emit 0xD8
        _emit 0x5F        ; 0040BD5E  pop edi
        _emit 0x5E        ; 0040BD5F  pop esi
        _emit 0x5D        ; 0040BD60  pop ebp
        _emit 0x5B        ; 0040BD61  pop ebx
        _emit 0x83        ; 0040BD62  add esp, 0x14
        _emit 0xC4
        _emit 0x14
        _emit 0xC3        ; 0040BD65  ret
        _emit 0x8B        ; 0040BD66  mov edi, edi
        _emit 0xFF
        _emit 0x7B        ; 0040BD68  jnp 0x40bd21
        _emit 0xB7
        _emit 0x40        ; 0040BD6A  inc eax
        _emit 0x00        ; 0040BD6B  add byte ptr [ebp + 0x720040b7], dh
        _emit 0xB5
        _emit 0xB7
        _emit 0x40
        _emit 0x00
        _emit 0x72
        _emit 0xB8        ; 0040BD71  mov eax, 0xb99e0040
        _emit 0x40
        _emit 0x00
        _emit 0x9E
        _emit 0xB9
        _emit 0x40        ; 0040BD76  inc eax
        _emit 0x00        ; 0040BD77  add dl, bh
        _emit 0xFA
        _emit 0xBA        ; 0040BD79  mov edx, 0xbd300040
        _emit 0x40
        _emit 0x00
        _emit 0x30
        _emit 0xBD
        _emit 0x40        ; 0040BD7E  inc eax
        _emit 0x00        ; 0040BD7F  .byte 0x00
    }
}

/*
 * vFillRect16 - fill a rectangle of the frame buffer with one colour, clipped to the 640x480 screen.
 * iX, iY, iWidth, iHeight: the rectangle (pixels); uColor: RGB555 colour; iBlendMode: 0 solid, 1 50%
 * mix with the screen (other values draw nothing).
 * Globals read: gpFrameBits, giScreenMode.  Globals changed: the frame buffer.
 */
void vFillRect16(int iX, int iY, int iWidth, int iHeight, int iBlendMode, UINT uColor)
{
    WORD *pDst;
    int iDstSkip;       /* 640 - iWidth: from the end of a row to the start of the next */
    int iCol;
    WORD wHalf;         /* mode 1: the colour halved */

    /* clip */
    if (iX < 0) {
        iWidth += iX;
        iX = 0;
    }
    if (iY < 0) {
        iHeight += iY;
        iY = 0;
    }
    if (iX + iWidth > 640)
        iWidth = 640 - iX;
    if (iY + iHeight > 480)
        iHeight = 480 - iY;
    if (iHeight > 0 && iWidth > 0) {
        /* full screen: to RGB565 by moving bits 4 and up one bit (the original's formula: blue's top
           bit moves into green) */
        if (giScreenMode)
            uColor = (uColor & 0xf) + (uColor & 0xfffffff0) * 2;
        iDstSkip = 640 - iWidth;
        pDst = (WORD *)gpFrameBits + iY * 640 + iX;
        switch (iBlendMode) {
        case 0:
            while (iHeight--) {
                iCol = iWidth;
                while (iCol--)
                    *pDst++ = uColor;
                pDst += iDstSkip;
            }
            break;
        case 1:
            /* 50% mix: halve both with the channel masks 0x7bef (RGB565) / 0x3def (RGB555), add */
            if (giScreenMode) {
                wHalf = ((int)uColor >> 1) & 0x7bef;
                while (iHeight--) {
                    iCol = iWidth;
                    while (iCol--) {
                        *pDst = ((*pDst >> 1) & 0x7bef) + wHalf;
                        pDst++;
                    }
                    pDst += iDstSkip;
                }
            } else {
                wHalf = ((int)uColor >> 1) & 0x3def;
                while (iHeight--) {
                    iCol = iWidth;
                    while (iCol--) {
                        *pDst = ((*pDst >> 1) & 0x3def) + wHalf;
                        pDst++;
                    }
                    pDst += iDstSkip;
                }
            }
            break;
        }
    }
}

/*
 * vDrawNumberSmall - draw iValue with the small digits of text.bmp (10x16 cells, 16 apart, row 0),
 * right to left: iX is the left edge of the last digit, each further digit 10 pixels to the left.
 * Nothing is drawn for values <= 0.  uDrawFlags / iTint*: as for vBlitImageRect16.
 * Globals read: gkgtBitmaps[1].
 */
void vDrawNumberSmall(int iValue, int iX, int iY, UINT uDrawFlags, int iTintR, int iTintG, int iTintB)
{
    while (iValue > 0) {
        vBlitImageRect16(&gkgtBitmaps[1], iX, iY, 10, 16, iValue % 10 * 16, 0, uDrawFlags, iTintR, iTintG, iTintB);
        iX -= 10;
        iValue /= 10;
    }
}

/*
 * vDrawNumberMedium - as vDrawNumberSmall with the 16x16 digits (text.bmp y 16), 16 pixels apart.
 */
void vDrawNumberMedium(int iValue, int iX, int iY, UINT uDrawFlags, int iTintR, int iTintG, int iTintB)
{
    while (iValue > 0) {
        vBlitImageRect16(&gkgtBitmaps[1], iX, iY, 16, 16, iValue % 10 * 16, 16, uDrawFlags, iTintR, iTintG, iTintB);
        iX -= 16;
        iValue /= 10;
    }
}

/*
 * vDrawNumberLarge - as vDrawNumberSmall with the 32x32 digits (text.bmp y 32), 32 pixels apart.
 */
void vDrawNumberLarge(int iValue, int iX, int iY, UINT uDrawFlags, int iTintR, int iTintG, int iTintB)
{
    while (iValue > 0) {
        vBlitImageRect16(&gkgtBitmaps[1], iX, iY, 32, 32, iValue % 10 * 32, 32, uDrawFlags, iTintR, iTintG, iTintB);
        iX -= 32;
        iValue /= 10;
    }
}

/*
 * vDrawTextSmall - draw szText at (iX, iY) with the small font of text.bmp: 8x8 glyphs starting at
 * x 0xb0, 16 per row, advancing 6 pixels.  text.bmp holds the ASCII characters 0x20..0x5f in rows of
 * 16; lower case is drawn as upper case.  Stops at the first control character (or NUL).
 * uDrawFlags / iTint*: as for vBlitImageRect16.  Globals read: gkgtBitmaps[1].
 */
void vDrawTextSmall(char *szText, int iX, int iY, UINT uDrawFlags, int iTintR, int iTintG, int iTintB)
{
    int iGlyph;         /* glyph number: character - 0x20 */

    while (*szText > 0x1f) {
        iGlyph = *szText - 0x20;
        /* 0x60..0x7f (lower case) -> 0x40..0x5f */
        if (iGlyph > 0x3f)
            iGlyph -= ((UINT)(iGlyph - 0x20) >> 5) * 32;
        vBlitImageRect16(&gkgtBitmaps[1], iX, iY, 8, 8, (iGlyph & 0xf) * 8 + 0xb0, iGlyph / 16 * 8, uDrawFlags, iTintR, iTintG, iTintB);
        iX += 6;
        szText++;
    }
}

/*
 * vDrawTextMedium - as vDrawTextSmall with the medium font: 12x16 glyphs starting at x 0x140,
 * advancing 10 pixels.
 */
void vDrawTextMedium(char *szText, int iX, int iY, UINT uDrawFlags, int iTintR, int iTintG, int iTintB)
{
    int iGlyph;         /* glyph number: character - 0x20 (lower case folded to upper case) */

    while (*szText > 0x1f) {
        iGlyph = *szText - 0x20;
        if (iGlyph > 0x3f)
            iGlyph -= ((UINT)(iGlyph - 0x20) >> 5) * 32;
        vBlitImageRect16(&gkgtBitmaps[1], iX, iY, 12, 16, (iGlyph & 0xf) * 12 + 0x140, iGlyph / 16 * 16, uDrawFlags, iTintR, iTintG, iTintB);
        iX += 10;
        szText++;
    }
}

/*
 * vJumptableJump - run the handler of the current object: gpfnGamestateJumptable[iJumpIdx].
 * Globals read: gpkgtCurrentEngineObject.
 */
void vJumptableJump(void)
{
    gpfnGamestateJumptable[gpkgtCurrentEngineObject->iJumpIdx]();
}

/*
 * vDrawKgtImage16 - draw an 8-bit KGT image (decompressed pixels, rows stored top-down) through a
 * 16-bit palette in the screen format into the frame buffer, clipped to the 640x480 screen, in the
 * blend mode of the current object (gpkgtCurrentEngineObject->iColorBlendtype).  Palette entry value 0
 * is transparent.
 * pHeader: the image (its row length is pHeader->iWidth and its number of rows pHeader->iHeight);
 * pSrc: its pixels; pPalette:
 * 256 colours in the screen format (gwTintedPalette16); iX, iY: top left on the screen (pixels);
 * iDrawW, iDrawH: size drawn; iFlags: 1 = mirror horizontally, 0x8000 = flip vertically.
 * Blend modes: 0 copy, 1 50% mix, 2 add (saturated), 3 subtract (saturated), 4 alpha with the object's
 * iColorAlpha (0..32, the destination weight).
 * Globals read: gpkgtCurrentEngineObject, gpFrameBits, giScreenMode.  Globals changed: the frame buffer.
 *
 * Built from the original's machine code: VC6 does not yet compile the C to exactly these bytes.
 * The dead `if (0)` copy of the C body keeps the file's string literals and imported symbols in
 * the original order; the plain C version is on the `nonmatching` branch.
 * Still different: (mostly case 4: the order of the G/B/R terms and the spilling of iAlpha, plus small register
 * choices in cases 0, 1 and 3).  Older note: same block layout/jump table, but register allocation
 * differs throughout (original keeps the step/row-skip in memory and dst in eax, the counters in dead
 * parameter slots; ours enregisters the step in ebx) - 1760 vs 1824 bytes. Tried shared/per-case
 * counters, params reused as step/row-skip, __inline helpers per blend mode, c/d/a types, for-loops,
 * declaration order.
 *
 * matching scaffolding in the body: the iStackPad locals keep VC6's candidate numbering as in the
 * original; "if (0) vMemzero(&iX, 0);" keeps iX in memory (the original never enregisters it); iX and
 * iY are reused as the source row skip and step, as in the original.
 */
/* the original machine code; the C in the dead block keeps the literals and imports */
__declspec(naked) void vDrawKgtImage16(kgtImageHeader *pHeader, BYTE *pSrc, WORD *pPalette, int iX, int iY, int iDrawW, int iDrawH, int iFlags)
{
    if (0) {
        int iSkipX, iSkipY;
        WORD *pDst;
        int iDstSkip;
        int iStackPad1 = 0;
        int iStackPad2 = 0;
        int iStackPad3 = 0;
        int iStackPad4 = 0;
        int iStackPad5 = 0;
        int iStackPad6 = 0;
        int iStackPad7 = 0;
        int iStackPad8 = 0;
        int iStackPad9 = 0;
    
        if (0) vMemzero(&iX, 0);
        iSkipX = 0;
        iSkipY = 0;
        if (iX + iDrawW >= 640)
            iDrawW = 640 - iX;
        if (iY + iDrawH >= 480)
            iDrawH = 480 - iY;
        if (iX < 0) {
            iDrawW += iX;
            iSkipX = -iX;
            iX = 0;
        }
        if (iY < 0) {
            iSkipY = -(pHeader->iWidth * iY);
            iDrawH += iY;
            iY = 0;
        }
        if (iDrawW <= 0 || iDrawH <= 0)
            ;
        pDst = (WORD *)gpFrameBits + iY * 640 + iX;
        iDstSkip = 640 - iDrawW;
        if (iFlags & 1) {
            if (iFlags & 0x8000) {
                pSrc += pHeader->iHeight * pHeader->iWidth - iSkipY - iSkipX - 1;
                iY = -1;
                iX = iDrawW - pHeader->iWidth;
            } else {
                iY = -1;
                pSrc += pHeader->iWidth - iSkipX + iSkipY - 1;
                iX = pHeader->iWidth + iDrawW;
            }
        } else {
            if (iFlags & 0x8000) {
                pSrc += (pHeader->iHeight - 1) * pHeader->iWidth - iSkipY + iSkipX;
                iX = -(pHeader->iWidth + iDrawW);
            } else {
                pSrc += iSkipY + iSkipX;
                iX = pHeader->iWidth - iDrawW;
            }
            iY = 1;
        }
    
    
        switch (gpkgtCurrentEngineObject->iColorBlendtype) {
        case 0: {
            int iRow = iDrawH;
            while (iRow--) {
                int iCol = iDrawW;
                do {
                                    if (pPalette[*pSrc])
                        *pDst = pPalette[*pSrc];
                    pDst++;
                    pSrc += iY;
                } while (--iCol);
                pDst += iDstSkip;
                pSrc += iX;
            }
            ;
        }
        case 1:
            if (giScreenMode) {
                int iRow = iDrawH;
                do {
                    int iCol = iDrawW;
                    while (iCol--) {
                                            if (pPalette[*pSrc])
                            *pDst = ((*pDst >> 1) & 0x7bef) + ((pPalette[*pSrc] >> 1) & 0x7bef);
                        pDst++;
                        pSrc += iY;
                    }
                    pSrc += iX;
                    pDst += iDstSkip;
                } while (--iRow);
            } else {
                int iRow = iDrawH;
                do {
                    int iCol = iDrawW;
                    do {
                        WORD wColor = pPalette[*pSrc];
                        if (wColor)
                            *pDst = ((*pDst >> 1) & 0x3def) + ((wColor >> 1) & 0x3def);
                        pDst++;
                        pSrc += iY;
                    } while (--iCol);
                    pSrc += iX;
                    pDst += iDstSkip;
                } while (--iRow);
            }
            ;
        case 2:
            if (giScreenMode) {
                int iRow = iDrawH;
                while (iRow--) {
                    int iCol = iDrawW;
                    while (iCol--) {
                                            if (pPalette[*pSrc]) {
                            WORD wDest = *pDst;
                            short shBlue, shGreen;
                            int iRed;
                            shBlue = (pPalette[*pSrc] & 0x1f) + (wDest & 0x1f);
                            if (shBlue > 0x1f)
                                shBlue = 0x1f;
                            shGreen = (pPalette[*pSrc] & 0x7e0) + (wDest & 0x7e0);
                            if (shGreen > 0x7e0)
                                shGreen = 0x7e0;
                            iRed = (pPalette[*pSrc] & 0xf800) + (wDest & 0xf800);
                            if (iRed > 0xf800)
                                iRed = 0xf800;
                            *pDst = iRed + shGreen + shBlue;
                            pDst++;
                        } else {
                            pDst++;
                        }
                        pSrc += iY;
                    }
                    pSrc += iX;
                    pDst += iDstSkip;
                }
            } else {
                int iRow = iDrawH;
                while (iRow--) {
                    int iCol = iDrawW;
                    while (iCol--) {
                                            if (pPalette[*pSrc]) {
                            WORD wSum, wCarry;
                            wSum = (*pDst & 0x7bdf) + (pPalette[*pSrc] & 0x7bdf);
                            wCarry = wSum & 0x8420;
                            *pDst = (wCarry - (wCarry >> 5)) | wSum;
                            pDst++;
                        } else {
                            pDst++;
                        }
                        pSrc += iY;
                    }
                    pDst += iDstSkip;
                    pSrc += iX;
                }
            }
            ;
        case 3:
            if (giScreenMode) {
                int iRow = iDrawH;
                while (iRow--) {
                    int iCol = iDrawW;
                    while (iCol--) {
                                            if (pPalette[*pSrc]) {
                            WORD wDest = *pDst;
                            short shBlue, shGreen;
                            int iRed;
                            shBlue = (wDest & 0x1f) - (pPalette[*pSrc] & 0x1f);
                            if (shBlue < 0) {
                                shBlue = 0;
                                shGreen = (wDest & 0x7e0) - (pPalette[*pSrc] & 0x7e0);
                            } else
                                shGreen = (wDest & 0x7e0) - (pPalette[*pSrc] & 0x7e0);
                            if (shGreen < 0x40) {
                                shGreen = 0;
                                iRed = (wDest & 0xf800) - (pPalette[*pSrc] & 0xf800);
                            } else
                                iRed = (wDest & 0xf800) - (pPalette[*pSrc] & 0xf800);
                            if (iRed < 0x800)
                                iRed = 0;
                            *pDst = iRed + shGreen + shBlue;
                            pDst++;
                        } else {
                            pDst++;
                        }
                        pSrc += iY;
                    }
                    pDst += iDstSkip;
                    pSrc += iX;
                }
            } else {
                int iRow = iDrawH;
                do {
                    int iCol = iDrawW;
                    while (iCol--) {
                                            if (pPalette[*pSrc]) {
                            WORD wDest = *pDst;
                            short shRed, shGreen, shBlue;
                            shBlue = (wDest & 0x1f) - (pPalette[*pSrc] & 0x1f);
                            if (shBlue < 0) {
                                shBlue = 0;
                                shGreen = (wDest & 0x3e0) - (pPalette[*pSrc] & 0x3e0);
                            } else
                                shGreen = (wDest & 0x3e0) - (pPalette[*pSrc] & 0x3e0);
                            if (shGreen < 0x1f) {
                                shGreen = 0;
                                shRed = (wDest & 0x7c00) - (pPalette[*pSrc] & 0x7c00);
                            } else
                                shRed = (wDest & 0x7c00) - (pPalette[*pSrc] & 0x7c00);
                            if (shRed < 0x3ff)
                                shRed = 0;
                            *pDst = shRed + shGreen + shBlue;
                            pDst++;
                        } else {
                            pDst++;
                        }
                        pSrc += iY;
                    }
                    pDst += iDstSkip;
                    pSrc += iX;
                } while (--iRow);
            }
            ;
        case 4: {
            int iAlpha, iInvAlpha;
            iAlpha = (WORD)gpkgtCurrentEngineObject->iColorAlpha;
            iInvAlpha = 0x20 - iAlpha;
            if (giScreenMode) {
                int iRow = iDrawH;
                do {
                    int iCol = iDrawW;
                    while (iCol--) {
                                            if (pPalette[*pSrc]) {
                            WORD wDest = *pDst;
                            /* (short)/(WORD) casts on a/iInvAlpha/d below are value-neutral for the bits each term keeps; they only
                               steer VC6's conversion narrowing and term evaluation order */
                            *pDst = ((((wDest & 0x7e0) * (WORD)iAlpha + (pPalette[*pSrc] & 0x7e0) * (short)iInvAlpha) >> 5) & 0x7e0)
                                  + (((((WORD)(wDest & 0x1f)) * iAlpha + (pPalette[*pSrc] & 0x1f) * (short)iInvAlpha) >> 5) & 0x1f)
                                  + (((((int)wDest & 0xf800) * (short)iAlpha + (pPalette[*pSrc] & 0xf800) * (short)iInvAlpha) >> 5) & 0xf800);
                            pDst++;
                        } else {
                            pDst++;
                        }
                        pSrc += iY;
                    }
                    pSrc += iX;
                    pDst += iDstSkip;
                } while (--iRow);
            } else {
                int iRow = iDrawH;
                do {
                    int iCol = iDrawW;
                    while (iCol--) {
                        WORD wColor = pPalette[*pSrc];
                        if (wColor) {
                            WORD wDest = *pDst;
                            *pDst = (((((WORD)(wColor & 0x3e0)) * (int)(short)iInvAlpha + ((int)wDest & 0x3e0) * (short)iAlpha) >> 5) & 0x3e0)
                                  + (((((int)wColor & 0x1f) * (WORD)iInvAlpha + ((int)wDest & 0x1f) * (int)(short)iAlpha) >> 5) & 0x1f)
                                  + (((((int)wColor & 0x7c00) * (iInvAlpha & 0xffff) + ((WORD)(wDest & 0x7c00)) * (short)iAlpha) >> 5) & 0x7c00);
                            pDst++;
                        } else {
                            pDst++;
                        }
                        pSrc += iY;
                    }
                    pSrc += iX;
                    pDst += iDstSkip;
                } while (--iRow);
            }
            ;
        }
        }
    }
    __asm {
        _emit 0x8B        ; 0040C140  mov edx, dword ptr [esp + 0x10]
        _emit 0x54
        _emit 0x24
        _emit 0x10
        _emit 0x83        ; 0040C144  sub esp, 8
        _emit 0xEC
        _emit 0x08
        _emit 0x33        ; 0040C147  xor ecx, ecx
        _emit 0xC9
        _emit 0x53        ; 0040C149  push ebx
        _emit 0x55        ; 0040C14A  push ebp
        _emit 0x8B        ; 0040C14B  mov ebp, dword ptr [esp + 0x28]
        _emit 0x6C
        _emit 0x24
        _emit 0x28
        _emit 0x56        ; 0040C14F  push esi
        _emit 0x57        ; 0040C150  push edi
        _emit 0x33        ; 0040C151  xor edi, edi
        _emit 0xFF
        _emit 0x8D        ; 0040C153  lea eax, [edx + ebp]
        _emit 0x04
        _emit 0x2A
        _emit 0x3D        ; 0040C156  cmp eax, 0x280
        _emit 0x80
        _emit 0x02
        _emit 0x00
        _emit 0x00
        _emit 0x7C        ; 0040C15B  jl 0x40c168
        _emit 0x0B
        _emit 0xBD        ; 0040C15D  mov ebp, 0x280
        _emit 0x80
        _emit 0x02
        _emit 0x00
        _emit 0x00
        _emit 0x2B        ; 0040C162  sub ebp, edx
        _emit 0xEA
        _emit 0x89        ; 0040C164  mov dword ptr [esp + 0x30], ebp
        _emit 0x6C
        _emit 0x24
        _emit 0x30
        _emit 0x8B        ; 0040C168  mov eax, dword ptr [esp + 0x2c]
        _emit 0x44
        _emit 0x24
        _emit 0x2C
        _emit 0x8B        ; 0040C16C  mov ebx, dword ptr [esp + 0x34]
        _emit 0x5C
        _emit 0x24
        _emit 0x34
        _emit 0x8D        ; 0040C170  lea esi, [eax + ebx]
        _emit 0x34
        _emit 0x18
        _emit 0x81        ; 0040C173  cmp esi, 0x1e0
        _emit 0xFE
        _emit 0xE0
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x7C        ; 0040C179  jl 0x40c182
        _emit 0x07
        _emit 0xBB        ; 0040C17B  mov ebx, 0x1e0
        _emit 0xE0
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x2B        ; 0040C180  sub ebx, eax
        _emit 0xD8
        _emit 0x85        ; 0040C182  test edx, edx
        _emit 0xD2
        _emit 0x7D        ; 0040C184  jge 0x40c192
        _emit 0x0C
        _emit 0x8B        ; 0040C186  mov edi, edx
        _emit 0xFA
        _emit 0x03        ; 0040C188  add ebp, edx
        _emit 0xEA
        _emit 0xF7        ; 0040C18A  neg edi
        _emit 0xDF
        _emit 0x89        ; 0040C18C  mov dword ptr [esp + 0x30], ebp
        _emit 0x6C
        _emit 0x24
        _emit 0x30
        _emit 0x33        ; 0040C190  xor edx, edx
        _emit 0xD2
        _emit 0x8B        ; 0040C192  mov esi, dword ptr [esp + 0x1c]
        _emit 0x74
        _emit 0x24
        _emit 0x1C
        _emit 0x85        ; 0040C196  test eax, eax
        _emit 0xC0
        _emit 0x7D        ; 0040C198  jge 0x40c1a6
        _emit 0x0C
        _emit 0x8B        ; 0040C19A  mov ecx, dword ptr [esi + 4]
        _emit 0x4E
        _emit 0x04
        _emit 0x03        ; 0040C19D  add ebx, eax
        _emit 0xD8
        _emit 0x0F        ; 0040C19F  imul ecx, eax
        _emit 0xAF
        _emit 0xC8
        _emit 0xF7        ; 0040C1A2  neg ecx
        _emit 0xD9
        _emit 0x33        ; 0040C1A4  xor eax, eax
        _emit 0xC0
        _emit 0x85        ; 0040C1A6  test ebp, ebp
        _emit 0xED
        _emit 0x0F        ; 0040C1A8  jle 0x40c835
        _emit 0x8E
        _emit 0x87
        _emit 0x06
        _emit 0x00
        _emit 0x00
        _emit 0x85        ; 0040C1AE  test ebx, ebx
        _emit 0xDB
        _emit 0x0F        ; 0040C1B0  jle 0x40c835
        _emit 0x8E
        _emit 0x7F
        _emit 0x06
        _emit 0x00
        _emit 0x00
        _emit 0x8D        ; 0040C1B6  lea eax, [eax + eax*4]
        _emit 0x04
        _emit 0x80
        _emit 0xC1        ; 0040C1B9  shl eax, 7
        _emit 0xE0
        _emit 0x07
        _emit 0x03        ; 0040C1BC  add eax, edx
        _emit 0xC2
        _emit 0x8B        ; 0040C1BE  mov edx, dword ptr [0x4246cc]
        _emit 0x15
        _emit 0xCC
        _emit 0x46
        _emit 0x42
        _emit 0x00
        _emit 0x8D        ; 0040C1C4  lea eax, [edx + eax*2]
        _emit 0x04
        _emit 0x42
        _emit 0xBA        ; 0040C1C7  mov edx, 0x280
        _emit 0x80
        _emit 0x02
        _emit 0x00
        _emit 0x00
        _emit 0x2B        ; 0040C1CC  sub edx, ebp
        _emit 0xD5
        _emit 0x89        ; 0040C1CE  mov dword ptr [esp + 0x34], edx
        _emit 0x54
        _emit 0x24
        _emit 0x34
        _emit 0x8B        ; 0040C1D2  mov edx, dword ptr [esp + 0x38]
        _emit 0x54
        _emit 0x24
        _emit 0x38
        _emit 0xF6        ; 0040C1D6  test dl, 1
        _emit 0xC2
        _emit 0x01
        _emit 0x74        ; 0040C1D9  je 0x40c236
        _emit 0x5B
        _emit 0xF6        ; 0040C1DB  test dh, 0x80
        _emit 0xC6
        _emit 0x80
        _emit 0x74        ; 0040C1DE  je 0x40c20d
        _emit 0x2D
        _emit 0x8B        ; 0040C1E0  mov edx, dword ptr [esi + 4]
        _emit 0x56
        _emit 0x04
        _emit 0x8B        ; 0040C1E3  mov esi, dword ptr [esi + 8]
        _emit 0x76
        _emit 0x08
        _emit 0x0F        ; 0040C1E6  imul esi, edx
        _emit 0xAF
        _emit 0xF2
        _emit 0x2B        ; 0040C1E9  sub esi, ecx
        _emit 0xF1
        _emit 0x8B        ; 0040C1EB  mov ecx, dword ptr [esp + 0x20]
        _emit 0x4C
        _emit 0x24
        _emit 0x20
        _emit 0x2B        ; 0040C1EF  sub esi, edi
        _emit 0xF7
        _emit 0xC7        ; 0040C1F1  mov dword ptr [esp + 0x2c], 0xffffffff
        _emit 0x44
        _emit 0x24
        _emit 0x2C
        _emit 0xFF
        _emit 0xFF
        _emit 0xFF
        _emit 0xFF
        _emit 0x8D        ; 0040C1F9  lea ebp, [ecx + esi - 1]
        _emit 0x6C
        _emit 0x31
        _emit 0xFF
        _emit 0x8B        ; 0040C1FD  mov ecx, dword ptr [esp + 0x30]
        _emit 0x4C
        _emit 0x24
        _emit 0x30
        _emit 0x2B        ; 0040C201  sub ecx, edx
        _emit 0xCA
        _emit 0x89        ; 0040C203  mov dword ptr [esp + 0x20], ebp
        _emit 0x6C
        _emit 0x24
        _emit 0x20
        _emit 0x89        ; 0040C207  mov dword ptr [esp + 0x28], ecx
        _emit 0x4C
        _emit 0x24
        _emit 0x28
        _emit 0xEB        ; 0040C20B  jmp 0x40c282
        _emit 0x75
        _emit 0x8B        ; 0040C20D  mov esi, dword ptr [esi + 4]
        _emit 0x76
        _emit 0x04
        _emit 0xC7        ; 0040C210  mov dword ptr [esp + 0x2c], 0xffffffff
        _emit 0x44
        _emit 0x24
        _emit 0x2C
        _emit 0xFF
        _emit 0xFF
        _emit 0xFF
        _emit 0xFF
        _emit 0x8B        ; 0040C218  mov edx, esi
        _emit 0xD6
        _emit 0x2B        ; 0040C21A  sub edx, edi
        _emit 0xD7
        _emit 0x03        ; 0040C21C  add edx, ecx
        _emit 0xD1
        _emit 0x8B        ; 0040C21E  mov ecx, dword ptr [esp + 0x20]
        _emit 0x4C
        _emit 0x24
        _emit 0x20
        _emit 0x8D        ; 0040C222  lea ebp, [ecx + edx - 1]
        _emit 0x6C
        _emit 0x11
        _emit 0xFF
        _emit 0x8B        ; 0040C226  mov edx, dword ptr [esp + 0x30]
        _emit 0x54
        _emit 0x24
        _emit 0x30
        _emit 0x03        ; 0040C22A  add esi, edx
        _emit 0xF2
        _emit 0x89        ; 0040C22C  mov dword ptr [esp + 0x20], ebp
        _emit 0x6C
        _emit 0x24
        _emit 0x20
        _emit 0x89        ; 0040C230  mov dword ptr [esp + 0x28], esi
        _emit 0x74
        _emit 0x24
        _emit 0x28
        _emit 0xEB        ; 0040C234  jmp 0x40c282
        _emit 0x4C
        _emit 0xF6        ; 0040C236  test dh, 0x80
        _emit 0xC6
        _emit 0x80
        _emit 0x74        ; 0040C239  je 0x40c261
        _emit 0x26
        _emit 0x8B        ; 0040C23B  mov edx, dword ptr [esi + 4]
        _emit 0x56
        _emit 0x04
        _emit 0x8B        ; 0040C23E  mov esi, dword ptr [esi + 8]
        _emit 0x76
        _emit 0x08
        _emit 0x4E        ; 0040C241  dec esi
        _emit 0x8B        ; 0040C242  mov ebp, dword ptr [esp + 0x20]
        _emit 0x6C
        _emit 0x24
        _emit 0x20
        _emit 0x0F        ; 0040C246  imul esi, edx
        _emit 0xAF
        _emit 0xF2
        _emit 0x2B        ; 0040C249  sub esi, ecx
        _emit 0xF1
        _emit 0x8B        ; 0040C24B  mov ecx, dword ptr [esp + 0x30]
        _emit 0x4C
        _emit 0x24
        _emit 0x30
        _emit 0x03        ; 0040C24F  add esi, edi
        _emit 0xF7
        _emit 0x03        ; 0040C251  add edx, ecx
        _emit 0xD1
        _emit 0x03        ; 0040C253  add ebp, esi
        _emit 0xEE
        _emit 0xF7        ; 0040C255  neg edx
        _emit 0xDA
        _emit 0x89        ; 0040C257  mov dword ptr [esp + 0x20], ebp
        _emit 0x6C
        _emit 0x24
        _emit 0x20
        _emit 0x89        ; 0040C25B  mov dword ptr [esp + 0x28], edx
        _emit 0x54
        _emit 0x24
        _emit 0x28
        _emit 0xEB        ; 0040C25F  jmp 0x40c27a
        _emit 0x19
        _emit 0x8B        ; 0040C261  mov ebp, dword ptr [esp + 0x20]
        _emit 0x6C
        _emit 0x24
        _emit 0x20
        _emit 0x8B        ; 0040C265  mov esi, dword ptr [esi + 4]
        _emit 0x76
        _emit 0x04
        _emit 0x03        ; 0040C268  add ecx, edi
        _emit 0xCF
        _emit 0x03        ; 0040C26A  add ebp, ecx
        _emit 0xE9
        _emit 0x8B        ; 0040C26C  mov ecx, dword ptr [esp + 0x30]
        _emit 0x4C
        _emit 0x24
        _emit 0x30
        _emit 0x2B        ; 0040C270  sub esi, ecx
        _emit 0xF1
        _emit 0x89        ; 0040C272  mov dword ptr [esp + 0x20], ebp
        _emit 0x6C
        _emit 0x24
        _emit 0x20
        _emit 0x89        ; 0040C276  mov dword ptr [esp + 0x28], esi
        _emit 0x74
        _emit 0x24
        _emit 0x28
        _emit 0xC7        ; 0040C27A  mov dword ptr [esp + 0x2c], 1
        _emit 0x44
        _emit 0x24
        _emit 0x2C
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040C282  mov ecx, dword ptr [0x4cfa00]
        _emit 0x0D
        _emit 0x00
        _emit 0xFA
        _emit 0x4C
        _emit 0x00
        _emit 0x8B        ; 0040C288  mov edx, dword ptr [ecx + 0x54]
        _emit 0x51
        _emit 0x54
        _emit 0x83        ; 0040C28B  cmp edx, 4
        _emit 0xFA
        _emit 0x04
        _emit 0x0F        ; 0040C28E  ja 0x40c835
        _emit 0x87
        _emit 0xA1
        _emit 0x05
        _emit 0x00
        _emit 0x00
        _emit 0xFF        ; 0040C294  jmp dword ptr [edx*4 + 0x40c840]
        _emit 0x24
        _emit 0x95
        _emit 0x40
        _emit 0xC8
        _emit 0x40
        _emit 0x00
        _emit 0x8B        ; 0040C29B  mov edi, dword ptr [esp + 0x24]
        _emit 0x7C
        _emit 0x24
        _emit 0x24
        _emit 0x8B        ; 0040C29F  mov esi, dword ptr [esp + 0x2c]
        _emit 0x74
        _emit 0x24
        _emit 0x2C
        _emit 0x89        ; 0040C2A3  mov dword ptr [esp + 0x20], ebx
        _emit 0x5C
        _emit 0x24
        _emit 0x20
        _emit 0x8B        ; 0040C2A7  mov ebx, dword ptr [esp + 0x28]
        _emit 0x5C
        _emit 0x24
        _emit 0x28
        _emit 0x8B        ; 0040C2AB  mov edx, dword ptr [esp + 0x30]
        _emit 0x54
        _emit 0x24
        _emit 0x30
        _emit 0x33        ; 0040C2AF  xor ecx, ecx
        _emit 0xC9
        _emit 0x8A        ; 0040C2B1  mov cl, byte ptr [ebp]
        _emit 0x4D
        _emit 0x00
        _emit 0x66        ; 0040C2B4  mov cx, word ptr [edi + ecx*2]
        _emit 0x8B
        _emit 0x0C
        _emit 0x4F
        _emit 0x66        ; 0040C2B8  test cx, cx
        _emit 0x85
        _emit 0xC9
        _emit 0x74        ; 0040C2BB  je 0x40c2c0
        _emit 0x03
        _emit 0x66        ; 0040C2BD  mov word ptr [eax], cx
        _emit 0x89
        _emit 0x08
        _emit 0x83        ; 0040C2C0  add eax, 2
        _emit 0xC0
        _emit 0x02
        _emit 0x03        ; 0040C2C3  add ebp, esi
        _emit 0xEE
        _emit 0x4A        ; 0040C2C5  dec edx
        _emit 0x75        ; 0040C2C6  jne 0x40c2af
        _emit 0xE7
        _emit 0x8B        ; 0040C2C8  mov ecx, dword ptr [esp + 0x20]
        _emit 0x4C
        _emit 0x24
        _emit 0x20
        _emit 0x8B        ; 0040C2CC  mov edx, dword ptr [esp + 0x34]
        _emit 0x54
        _emit 0x24
        _emit 0x34
        _emit 0x03        ; 0040C2D0  add ebp, ebx
        _emit 0xEB
        _emit 0x49        ; 0040C2D2  dec ecx
        _emit 0x8D        ; 0040C2D3  lea eax, [eax + edx*2]
        _emit 0x04
        _emit 0x50
        _emit 0x89        ; 0040C2D6  mov dword ptr [esp + 0x20], ecx
        _emit 0x4C
        _emit 0x24
        _emit 0x20
        _emit 0x75        ; 0040C2DA  jne 0x40c2ab
        _emit 0xCF
        _emit 0x5F        ; 0040C2DC  pop edi
        _emit 0x5E        ; 0040C2DD  pop esi
        _emit 0x5D        ; 0040C2DE  pop ebp
        _emit 0x5B        ; 0040C2DF  pop ebx
        _emit 0x83        ; 0040C2E0  add esp, 8
        _emit 0xC4
        _emit 0x08
        _emit 0xC3        ; 0040C2E3  ret
        _emit 0x8B        ; 0040C2E4  mov ecx, dword ptr [0x424704]
        _emit 0x0D
        _emit 0x04
        _emit 0x47
        _emit 0x42
        _emit 0x00
        _emit 0x85        ; 0040C2EA  test ecx, ecx
        _emit 0xC9
        _emit 0x74        ; 0040C2EC  je 0x40c34e
        _emit 0x60
        _emit 0x8B        ; 0040C2EE  mov edi, dword ptr [esp + 0x24]
        _emit 0x7C
        _emit 0x24
        _emit 0x24
        _emit 0x8B        ; 0040C2F2  mov esi, dword ptr [esp + 0x2c]
        _emit 0x74
        _emit 0x24
        _emit 0x2C
        _emit 0x89        ; 0040C2F6  mov dword ptr [esp + 0x20], ebx
        _emit 0x5C
        _emit 0x24
        _emit 0x20
        _emit 0x8B        ; 0040C2FA  mov ecx, dword ptr [esp + 0x30]
        _emit 0x4C
        _emit 0x24
        _emit 0x30
        _emit 0x8D        ; 0040C2FE  lea edx, [ecx]
        _emit 0x11
        _emit 0x33        ; 0040C300  xor ecx, ecx
        _emit 0xC9
        _emit 0x8A        ; 0040C302  mov cl, byte ptr [ebp]
        _emit 0x4D
        _emit 0x00
        _emit 0x66        ; 0040C305  mov cx, word ptr [edi + ecx*2]
        _emit 0x8B
        _emit 0x0C
        _emit 0x4F
        _emit 0x66        ; 0040C309  test cx, cx
        _emit 0x85
        _emit 0xC9
        _emit 0x74        ; 0040C30C  je 0x40c326
        _emit 0x18
        _emit 0x66        ; 0040C30E  mov bx, word ptr [eax]
        _emit 0x8B
        _emit 0x18
        _emit 0xD1        ; 0040C311  shr ebx, 1
        _emit 0xEB
        _emit 0xD1        ; 0040C313  shr ecx, 1
        _emit 0xE9
        _emit 0x81        ; 0040C315  and ebx, 0x7bef
        _emit 0xE3
        _emit 0xEF
        _emit 0x7B
        _emit 0x00
        _emit 0x00
        _emit 0x81        ; 0040C31B  and ecx, 0x7bef
        _emit 0xE1
        _emit 0xEF
        _emit 0x7B
        _emit 0x00
        _emit 0x00
        _emit 0x03        ; 0040C321  add ebx, ecx
        _emit 0xD9
        _emit 0x66        ; 0040C323  mov word ptr [eax], bx
        _emit 0x89
        _emit 0x18
        _emit 0x83        ; 0040C326  add eax, 2
        _emit 0xC0
        _emit 0x02
        _emit 0x03        ; 0040C329  add ebp, esi
        _emit 0xEE
        _emit 0x4A        ; 0040C32B  dec edx
        _emit 0x75        ; 0040C32C  jne 0x40c300
        _emit 0xD2
        _emit 0x8B        ; 0040C32E  mov edx, dword ptr [esp + 0x28]
        _emit 0x54
        _emit 0x24
        _emit 0x28
        _emit 0x8B        ; 0040C332  mov ecx, dword ptr [esp + 0x20]
        _emit 0x4C
        _emit 0x24
        _emit 0x20
        _emit 0x03        ; 0040C336  add ebp, edx
        _emit 0xEA
        _emit 0x8B        ; 0040C338  mov edx, dword ptr [esp + 0x34]
        _emit 0x54
        _emit 0x24
        _emit 0x34
        _emit 0x49        ; 0040C33C  dec ecx
        _emit 0x8D        ; 0040C33D  lea eax, [eax + edx*2]
        _emit 0x04
        _emit 0x50
        _emit 0x89        ; 0040C340  mov dword ptr [esp + 0x20], ecx
        _emit 0x4C
        _emit 0x24
        _emit 0x20
        _emit 0x75        ; 0040C344  jne 0x40c2fa
        _emit 0xB4
        _emit 0x5F        ; 0040C346  pop edi
        _emit 0x5E        ; 0040C347  pop esi
        _emit 0x5D        ; 0040C348  pop ebp
        _emit 0x5B        ; 0040C349  pop ebx
        _emit 0x83        ; 0040C34A  add esp, 8
        _emit 0xC4
        _emit 0x08
        _emit 0xC3        ; 0040C34D  ret
        _emit 0x8B        ; 0040C34E  mov edi, dword ptr [esp + 0x24]
        _emit 0x7C
        _emit 0x24
        _emit 0x24
        _emit 0x8B        ; 0040C352  mov esi, dword ptr [esp + 0x2c]
        _emit 0x74
        _emit 0x24
        _emit 0x2C
        _emit 0x89        ; 0040C356  mov dword ptr [esp + 0x20], ebx
        _emit 0x5C
        _emit 0x24
        _emit 0x20
        _emit 0x8B        ; 0040C35A  mov ecx, dword ptr [esp + 0x30]
        _emit 0x4C
        _emit 0x24
        _emit 0x30
        _emit 0x8D        ; 0040C35E  lea edx, [ecx]
        _emit 0x11
        _emit 0x33        ; 0040C360  xor ecx, ecx
        _emit 0xC9
        _emit 0x8A        ; 0040C362  mov cl, byte ptr [ebp]
        _emit 0x4D
        _emit 0x00
        _emit 0x66        ; 0040C365  mov cx, word ptr [edi + ecx*2]
        _emit 0x8B
        _emit 0x0C
        _emit 0x4F
        _emit 0x66        ; 0040C369  test cx, cx
        _emit 0x85
        _emit 0xC9
        _emit 0x74        ; 0040C36C  je 0x40c386
        _emit 0x18
        _emit 0x66        ; 0040C36E  mov bx, word ptr [eax]
        _emit 0x8B
        _emit 0x18
        _emit 0xD1        ; 0040C371  shr ebx, 1
        _emit 0xEB
        _emit 0xD1        ; 0040C373  shr ecx, 1
        _emit 0xE9
        _emit 0x81        ; 0040C375  and ebx, 0x3def
        _emit 0xE3
        _emit 0xEF
        _emit 0x3D
        _emit 0x00
        _emit 0x00
        _emit 0x81        ; 0040C37B  and ecx, 0x3def
        _emit 0xE1
        _emit 0xEF
        _emit 0x3D
        _emit 0x00
        _emit 0x00
        _emit 0x03        ; 0040C381  add ebx, ecx
        _emit 0xD9
        _emit 0x66        ; 0040C383  mov word ptr [eax], bx
        _emit 0x89
        _emit 0x18
        _emit 0x83        ; 0040C386  add eax, 2
        _emit 0xC0
        _emit 0x02
        _emit 0x03        ; 0040C389  add ebp, esi
        _emit 0xEE
        _emit 0x4A        ; 0040C38B  dec edx
        _emit 0x75        ; 0040C38C  jne 0x40c360
        _emit 0xD2
        _emit 0x8B        ; 0040C38E  mov edx, dword ptr [esp + 0x28]
        _emit 0x54
        _emit 0x24
        _emit 0x28
        _emit 0x8B        ; 0040C392  mov ecx, dword ptr [esp + 0x20]
        _emit 0x4C
        _emit 0x24
        _emit 0x20
        _emit 0x03        ; 0040C396  add ebp, edx
        _emit 0xEA
        _emit 0x8B        ; 0040C398  mov edx, dword ptr [esp + 0x34]
        _emit 0x54
        _emit 0x24
        _emit 0x34
        _emit 0x49        ; 0040C39C  dec ecx
        _emit 0x8D        ; 0040C39D  lea eax, [eax + edx*2]
        _emit 0x04
        _emit 0x50
        _emit 0x89        ; 0040C3A0  mov dword ptr [esp + 0x20], ecx
        _emit 0x4C
        _emit 0x24
        _emit 0x20
        _emit 0x75        ; 0040C3A4  jne 0x40c35a
        _emit 0xB4
        _emit 0x5F        ; 0040C3A6  pop edi
        _emit 0x5E        ; 0040C3A7  pop esi
        _emit 0x5D        ; 0040C3A8  pop ebp
        _emit 0x5B        ; 0040C3A9  pop ebx
        _emit 0x83        ; 0040C3AA  add esp, 8
        _emit 0xC4
        _emit 0x08
        _emit 0xC3        ; 0040C3AD  ret
        _emit 0x8B        ; 0040C3AE  mov ecx, dword ptr [0x424704]
        _emit 0x0D
        _emit 0x04
        _emit 0x47
        _emit 0x42
        _emit 0x00
        _emit 0x85        ; 0040C3B4  test ecx, ecx
        _emit 0xC9
        _emit 0x0F        ; 0040C3B6  je 0x40c476
        _emit 0x84
        _emit 0xBA
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x89        ; 0040C3BC  mov dword ptr [esp + 0x1c], ebx
        _emit 0x5C
        _emit 0x24
        _emit 0x1C
        _emit 0x8B        ; 0040C3C0  mov ecx, dword ptr [esp + 0x30]
        _emit 0x4C
        _emit 0x24
        _emit 0x30
        _emit 0x89        ; 0040C3C4  mov dword ptr [esp + 0x20], ecx
        _emit 0x4C
        _emit 0x24
        _emit 0x20
        _emit 0x8B        ; 0040C3C8  mov ecx, dword ptr [esp + 0x24]
        _emit 0x4C
        _emit 0x24
        _emit 0x24
        _emit 0x33        ; 0040C3CC  xor edx, edx
        _emit 0xD2
        _emit 0x8A        ; 0040C3CE  mov dl, byte ptr [ebp]
        _emit 0x55
        _emit 0x00
        _emit 0x66        ; 0040C3D1  mov cx, word ptr [ecx + edx*2]
        _emit 0x8B
        _emit 0x0C
        _emit 0x51
        _emit 0x66        ; 0040C3D5  test cx, cx
        _emit 0x85
        _emit 0xC9
        _emit 0x74        ; 0040C3D8  je 0x40c43a
        _emit 0x60
        _emit 0x66        ; 0040C3DA  mov dx, word ptr [eax]
        _emit 0x8B
        _emit 0x10
        _emit 0x8A        ; 0040C3DD  mov bl, cl
        _emit 0xD9
        _emit 0x89        ; 0040C3DF  mov dword ptr [esp + 0x38], edx
        _emit 0x54
        _emit 0x24
        _emit 0x38
        _emit 0x83        ; 0040C3E3  and ebx, 0x1f
        _emit 0xE3
        _emit 0x1F
        _emit 0x83        ; 0040C3E6  and edx, 0x1f
        _emit 0xE2
        _emit 0x1F
        _emit 0x03        ; 0040C3E9  add ebx, edx
        _emit 0xDA
        _emit 0x66        ; 0040C3EB  cmp bx, 0x1f
        _emit 0x83
        _emit 0xFB
        _emit 0x1F
        _emit 0x7E        ; 0040C3EF  jle 0x40c3f6
        _emit 0x05
        _emit 0xBB        ; 0040C3F1  mov ebx, 0x1f
        _emit 0x1F
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040C3F6  mov esi, dword ptr [esp + 0x38]
        _emit 0x74
        _emit 0x24
        _emit 0x38
        _emit 0x8B        ; 0040C3FA  mov edx, ecx
        _emit 0xD1
        _emit 0x8B        ; 0040C3FC  mov edi, esi
        _emit 0xFE
        _emit 0x81        ; 0040C3FE  and edx, 0x7e0
        _emit 0xE2
        _emit 0xE0
        _emit 0x07
        _emit 0x00
        _emit 0x00
        _emit 0x81        ; 0040C404  and edi, 0x7e0
        _emit 0xE7
        _emit 0xE0
        _emit 0x07
        _emit 0x00
        _emit 0x00
        _emit 0x03        ; 0040C40A  add edx, edi
        _emit 0xD7
        _emit 0x66        ; 0040C40C  cmp dx, 0x7e0
        _emit 0x81
        _emit 0xFA
        _emit 0xE0
        _emit 0x07
        _emit 0x7E        ; 0040C411  jle 0x40c418
        _emit 0x05
        _emit 0xBA        ; 0040C413  mov edx, 0x7e0
        _emit 0xE0
        _emit 0x07
        _emit 0x00
        _emit 0x00
        _emit 0x81        ; 0040C418  and ecx, 0xf800
        _emit 0xE1
        _emit 0x00
        _emit 0xF8
        _emit 0x00
        _emit 0x00
        _emit 0x81        ; 0040C41E  and esi, 0xf800
        _emit 0xE6
        _emit 0x00
        _emit 0xF8
        _emit 0x00
        _emit 0x00
        _emit 0x03        ; 0040C424  add ecx, esi
        _emit 0xCE
        _emit 0x81        ; 0040C426  cmp ecx, 0xf800
        _emit 0xF9
        _emit 0x00
        _emit 0xF8
        _emit 0x00
        _emit 0x00
        _emit 0x7E        ; 0040C42C  jle 0x40c433
        _emit 0x05
        _emit 0xB9        ; 0040C42E  mov ecx, 0xf800
        _emit 0x00
        _emit 0xF8
        _emit 0x00
        _emit 0x00
        _emit 0x03        ; 0040C433  add ecx, edx
        _emit 0xCA
        _emit 0x03        ; 0040C435  add ecx, ebx
        _emit 0xCB
        _emit 0x66        ; 0040C437  mov word ptr [eax], cx
        _emit 0x89
        _emit 0x08
        _emit 0x8B        ; 0040C43A  mov edx, dword ptr [esp + 0x2c]
        _emit 0x54
        _emit 0x24
        _emit 0x2C
        _emit 0x8B        ; 0040C43E  mov ecx, dword ptr [esp + 0x20]
        _emit 0x4C
        _emit 0x24
        _emit 0x20
        _emit 0x83        ; 0040C442  add eax, 2
        _emit 0xC0
        _emit 0x02
        _emit 0x03        ; 0040C445  add ebp, edx
        _emit 0xEA
        _emit 0x49        ; 0040C447  dec ecx
        _emit 0x89        ; 0040C448  mov dword ptr [esp + 0x20], ecx
        _emit 0x4C
        _emit 0x24
        _emit 0x20
        _emit 0x0F        ; 0040C44C  jne 0x40c3c8
        _emit 0x85
        _emit 0x76
        _emit 0xFF
        _emit 0xFF
        _emit 0xFF
        _emit 0x8B        ; 0040C452  mov edx, dword ptr [esp + 0x28]
        _emit 0x54
        _emit 0x24
        _emit 0x28
        _emit 0x8B        ; 0040C456  mov ecx, dword ptr [esp + 0x1c]
        _emit 0x4C
        _emit 0x24
        _emit 0x1C
        _emit 0x03        ; 0040C45A  add ebp, edx
        _emit 0xEA
        _emit 0x8B        ; 0040C45C  mov edx, dword ptr [esp + 0x34]
        _emit 0x54
        _emit 0x24
        _emit 0x34
        _emit 0x49        ; 0040C460  dec ecx
        _emit 0x8D        ; 0040C461  lea eax, [eax + edx*2]
        _emit 0x04
        _emit 0x50
        _emit 0x89        ; 0040C464  mov dword ptr [esp + 0x1c], ecx
        _emit 0x4C
        _emit 0x24
        _emit 0x1C
        _emit 0x0F        ; 0040C468  jne 0x40c3c0
        _emit 0x85
        _emit 0x52
        _emit 0xFF
        _emit 0xFF
        _emit 0xFF
        _emit 0x5F        ; 0040C46E  pop edi
        _emit 0x5E        ; 0040C46F  pop esi
        _emit 0x5D        ; 0040C470  pop ebp
        _emit 0x5B        ; 0040C471  pop ebx
        _emit 0x83        ; 0040C472  add esp, 8
        _emit 0xC4
        _emit 0x08
        _emit 0xC3        ; 0040C475  ret
        _emit 0x8B        ; 0040C476  mov edi, dword ptr [esp + 0x24]
        _emit 0x7C
        _emit 0x24
        _emit 0x24
        _emit 0x89        ; 0040C47A  mov dword ptr [esp + 0x20], ebx
        _emit 0x5C
        _emit 0x24
        _emit 0x20
        _emit 0x8B        ; 0040C47E  mov ecx, dword ptr [esp + 0x30]
        _emit 0x4C
        _emit 0x24
        _emit 0x30
        _emit 0x8D        ; 0040C482  lea esi, [ecx]
        _emit 0x31
        _emit 0x33        ; 0040C484  xor edx, edx
        _emit 0xD2
        _emit 0x8A        ; 0040C486  mov dl, byte ptr [ebp]
        _emit 0x55
        _emit 0x00
        _emit 0x66        ; 0040C489  mov cx, word ptr [edi + edx*2]
        _emit 0x8B
        _emit 0x0C
        _emit 0x57
        _emit 0x66        ; 0040C48D  test cx, cx
        _emit 0x85
        _emit 0xC9
        _emit 0x74        ; 0040C490  je 0x40c4b9
        _emit 0x27
        _emit 0x66        ; 0040C492  mov dx, word ptr [eax]
        _emit 0x8B
        _emit 0x10
        _emit 0x81        ; 0040C495  and ecx, 0x7bdf
        _emit 0xE1
        _emit 0xDF
        _emit 0x7B
        _emit 0x00
        _emit 0x00
        _emit 0x81        ; 0040C49B  and edx, 0x7bdf
        _emit 0xE2
        _emit 0xDF
        _emit 0x7B
        _emit 0x00
        _emit 0x00
        _emit 0x03        ; 0040C4A1  add edx, ecx
        _emit 0xD1
        _emit 0x8B        ; 0040C4A3  mov ecx, edx
        _emit 0xCA
        _emit 0x81        ; 0040C4A5  and ecx, 0x8420
        _emit 0xE1
        _emit 0x20
        _emit 0x84
        _emit 0x00
        _emit 0x00
        _emit 0x66        ; 0040C4AB  mov bx, cx
        _emit 0x8B
        _emit 0xD9
        _emit 0x66        ; 0040C4AE  shr bx, 5
        _emit 0xC1
        _emit 0xEB
        _emit 0x05
        _emit 0x2B        ; 0040C4B2  sub ecx, ebx
        _emit 0xCB
        _emit 0x0B        ; 0040C4B4  or ecx, edx
        _emit 0xCA
        _emit 0x66        ; 0040C4B6  mov word ptr [eax], cx
        _emit 0x89
        _emit 0x08
        _emit 0x8B        ; 0040C4B9  mov edx, dword ptr [esp + 0x2c]
        _emit 0x54
        _emit 0x24
        _emit 0x2C
        _emit 0x83        ; 0040C4BD  add eax, 2
        _emit 0xC0
        _emit 0x02
        _emit 0x03        ; 0040C4C0  add ebp, edx
        _emit 0xEA
        _emit 0x4E        ; 0040C4C2  dec esi
        _emit 0x75        ; 0040C4C3  jne 0x40c484
        _emit 0xBF
        _emit 0x8B        ; 0040C4C5  mov ecx, dword ptr [esp + 0x34]
        _emit 0x4C
        _emit 0x24
        _emit 0x34
        _emit 0x8B        ; 0040C4C9  mov edx, dword ptr [esp + 0x28]
        _emit 0x54
        _emit 0x24
        _emit 0x28
        _emit 0x03        ; 0040C4CD  add ebp, edx
        _emit 0xEA
        _emit 0x8D        ; 0040C4CF  lea eax, [eax + ecx*2]
        _emit 0x04
        _emit 0x48
        _emit 0x8B        ; 0040C4D2  mov ecx, dword ptr [esp + 0x20]
        _emit 0x4C
        _emit 0x24
        _emit 0x20
        _emit 0x49        ; 0040C4D6  dec ecx
        _emit 0x89        ; 0040C4D7  mov dword ptr [esp + 0x20], ecx
        _emit 0x4C
        _emit 0x24
        _emit 0x20
        _emit 0x75        ; 0040C4DB  jne 0x40c47e
        _emit 0xA1
        _emit 0x5F        ; 0040C4DD  pop edi
        _emit 0x5E        ; 0040C4DE  pop esi
        _emit 0x5D        ; 0040C4DF  pop ebp
        _emit 0x5B        ; 0040C4E0  pop ebx
        _emit 0x83        ; 0040C4E1  add esp, 8
        _emit 0xC4
        _emit 0x08
        _emit 0xC3        ; 0040C4E4  ret
        _emit 0x8B        ; 0040C4E5  mov ecx, dword ptr [0x424704]
        _emit 0x0D
        _emit 0x04
        _emit 0x47
        _emit 0x42
        _emit 0x00
        _emit 0x85        ; 0040C4EB  test ecx, ecx
        _emit 0xC9
        _emit 0x0F        ; 0040C4ED  je 0x40c59e
        _emit 0x84
        _emit 0xAB
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x89        ; 0040C4F3  mov dword ptr [esp + 0x38], ebx
        _emit 0x5C
        _emit 0x24
        _emit 0x38
        _emit 0x8B        ; 0040C4F7  mov edx, dword ptr [esp + 0x30]
        _emit 0x54
        _emit 0x24
        _emit 0x30
        _emit 0x89        ; 0040C4FB  mov dword ptr [esp + 0x1c], edx
        _emit 0x54
        _emit 0x24
        _emit 0x1C
        _emit 0x8B        ; 0040C4FF  mov edx, dword ptr [esp + 0x24]
        _emit 0x54
        _emit 0x24
        _emit 0x24
        _emit 0x33        ; 0040C503  xor ecx, ecx
        _emit 0xC9
        _emit 0x8A        ; 0040C505  mov cl, byte ptr [ebp]
        _emit 0x4D
        _emit 0x00
        _emit 0x66        ; 0040C508  mov dx, word ptr [edx + ecx*2]
        _emit 0x8B
        _emit 0x14
        _emit 0x4A
        _emit 0x66        ; 0040C50C  test dx, dx
        _emit 0x85
        _emit 0xD2
        _emit 0x89        ; 0040C50F  mov dword ptr [esp + 0x20], edx
        _emit 0x54
        _emit 0x24
        _emit 0x20
        _emit 0x74        ; 0040C513  je 0x40c566
        _emit 0x51
        _emit 0x66        ; 0040C515  mov cx, word ptr [eax]
        _emit 0x8B
        _emit 0x08
        _emit 0x83        ; 0040C518  and edx, 0x1f
        _emit 0xE2
        _emit 0x1F
        _emit 0x8A        ; 0040C51B  mov bl, cl
        _emit 0xD9
        _emit 0x83        ; 0040C51D  and ebx, 0x1f
        _emit 0xE3
        _emit 0x1F
        _emit 0x2B        ; 0040C520  sub ebx, edx
        _emit 0xDA
        _emit 0x66        ; 0040C522  test bx, bx
        _emit 0x85
        _emit 0xDB
        _emit 0x7D        ; 0040C525  jge 0x40c529
        _emit 0x02
        _emit 0x33        ; 0040C527  xor ebx, ebx
        _emit 0xDB
        _emit 0x8B        ; 0040C529  mov esi, dword ptr [esp + 0x20]
        _emit 0x74
        _emit 0x24
        _emit 0x20
        _emit 0x8B        ; 0040C52D  mov edx, ecx
        _emit 0xD1
        _emit 0x8B        ; 0040C52F  mov edi, esi
        _emit 0xFE
        _emit 0x81        ; 0040C531  and edx, 0x7e0
        _emit 0xE2
        _emit 0xE0
        _emit 0x07
        _emit 0x00
        _emit 0x00
        _emit 0x81        ; 0040C537  and edi, 0x7e0
        _emit 0xE7
        _emit 0xE0
        _emit 0x07
        _emit 0x00
        _emit 0x00
        _emit 0x2B        ; 0040C53D  sub edx, edi
        _emit 0xD7
        _emit 0x66        ; 0040C53F  cmp dx, 0x40
        _emit 0x83
        _emit 0xFA
        _emit 0x40
        _emit 0x7D        ; 0040C543  jge 0x40c547
        _emit 0x02
        _emit 0x33        ; 0040C545  xor edx, edx
        _emit 0xD2
        _emit 0x81        ; 0040C547  and esi, 0xf800
        _emit 0xE6
        _emit 0x00
        _emit 0xF8
        _emit 0x00
        _emit 0x00
        _emit 0x81        ; 0040C54D  and ecx, 0xf800
        _emit 0xE1
        _emit 0x00
        _emit 0xF8
        _emit 0x00
        _emit 0x00
        _emit 0x2B        ; 0040C553  sub ecx, esi
        _emit 0xCE
        _emit 0x81        ; 0040C555  cmp ecx, 0x800
        _emit 0xF9
        _emit 0x00
        _emit 0x08
        _emit 0x00
        _emit 0x00
        _emit 0x7D        ; 0040C55B  jge 0x40c55f
        _emit 0x02
        _emit 0x33        ; 0040C55D  xor ecx, ecx
        _emit 0xC9
        _emit 0x03        ; 0040C55F  add ecx, edx
        _emit 0xCA
        _emit 0x03        ; 0040C561  add ecx, ebx
        _emit 0xCB
        _emit 0x66        ; 0040C563  mov word ptr [eax], cx
        _emit 0x89
        _emit 0x08
        _emit 0x8B        ; 0040C566  mov edx, dword ptr [esp + 0x2c]
        _emit 0x54
        _emit 0x24
        _emit 0x2C
        _emit 0x8B        ; 0040C56A  mov ecx, dword ptr [esp + 0x1c]
        _emit 0x4C
        _emit 0x24
        _emit 0x1C
        _emit 0x83        ; 0040C56E  add eax, 2
        _emit 0xC0
        _emit 0x02
        _emit 0x03        ; 0040C571  add ebp, edx
        _emit 0xEA
        _emit 0x49        ; 0040C573  dec ecx
        _emit 0x89        ; 0040C574  mov dword ptr [esp + 0x1c], ecx
        _emit 0x4C
        _emit 0x24
        _emit 0x1C
        _emit 0x75        ; 0040C578  jne 0x40c4ff
        _emit 0x85
        _emit 0x8B        ; 0040C57A  mov ecx, dword ptr [esp + 0x34]
        _emit 0x4C
        _emit 0x24
        _emit 0x34
        _emit 0x8B        ; 0040C57E  mov edx, dword ptr [esp + 0x28]
        _emit 0x54
        _emit 0x24
        _emit 0x28
        _emit 0x03        ; 0040C582  add ebp, edx
        _emit 0xEA
        _emit 0x8D        ; 0040C584  lea eax, [eax + ecx*2]
        _emit 0x04
        _emit 0x48
        _emit 0x8B        ; 0040C587  mov ecx, dword ptr [esp + 0x38]
        _emit 0x4C
        _emit 0x24
        _emit 0x38
        _emit 0x49        ; 0040C58B  dec ecx
        _emit 0x89        ; 0040C58C  mov dword ptr [esp + 0x38], ecx
        _emit 0x4C
        _emit 0x24
        _emit 0x38
        _emit 0x0F        ; 0040C590  jne 0x40c4f7
        _emit 0x85
        _emit 0x61
        _emit 0xFF
        _emit 0xFF
        _emit 0xFF
        _emit 0x5F        ; 0040C596  pop edi
        _emit 0x5E        ; 0040C597  pop esi
        _emit 0x5D        ; 0040C598  pop ebp
        _emit 0x5B        ; 0040C599  pop ebx
        _emit 0x83        ; 0040C59A  add esp, 8
        _emit 0xC4
        _emit 0x08
        _emit 0xC3        ; 0040C59D  ret
        _emit 0x89        ; 0040C59E  mov dword ptr [esp + 0x38], ebx
        _emit 0x5C
        _emit 0x24
        _emit 0x38
        _emit 0x8B        ; 0040C5A2  mov edx, dword ptr [esp + 0x30]
        _emit 0x54
        _emit 0x24
        _emit 0x30
        _emit 0x89        ; 0040C5A6  mov dword ptr [esp + 0x1c], edx
        _emit 0x54
        _emit 0x24
        _emit 0x1C
        _emit 0x8B        ; 0040C5AA  mov edx, dword ptr [esp + 0x24]
        _emit 0x54
        _emit 0x24
        _emit 0x24
        _emit 0x33        ; 0040C5AE  xor ecx, ecx
        _emit 0xC9
        _emit 0x8A        ; 0040C5B0  mov cl, byte ptr [ebp]
        _emit 0x4D
        _emit 0x00
        _emit 0x66        ; 0040C5B3  mov dx, word ptr [edx + ecx*2]
        _emit 0x8B
        _emit 0x14
        _emit 0x4A
        _emit 0x66        ; 0040C5B7  test dx, dx
        _emit 0x85
        _emit 0xD2
        _emit 0x89        ; 0040C5BA  mov dword ptr [esp + 0x20], edx
        _emit 0x54
        _emit 0x24
        _emit 0x20
        _emit 0x74        ; 0040C5BE  je 0x40c610
        _emit 0x50
        _emit 0x66        ; 0040C5C0  mov cx, word ptr [eax]
        _emit 0x8B
        _emit 0x08
        _emit 0x83        ; 0040C5C3  and edx, 0x1f
        _emit 0xE2
        _emit 0x1F
        _emit 0x8A        ; 0040C5C6  mov bl, cl
        _emit 0xD9
        _emit 0x83        ; 0040C5C8  and ebx, 0x1f
        _emit 0xE3
        _emit 0x1F
        _emit 0x2B        ; 0040C5CB  sub ebx, edx
        _emit 0xDA
        _emit 0x66        ; 0040C5CD  test bx, bx
        _emit 0x85
        _emit 0xDB
        _emit 0x7D        ; 0040C5D0  jge 0x40c5d4
        _emit 0x02
        _emit 0x33        ; 0040C5D2  xor ebx, ebx
        _emit 0xDB
        _emit 0x8B        ; 0040C5D4  mov esi, dword ptr [esp + 0x20]
        _emit 0x74
        _emit 0x24
        _emit 0x20
        _emit 0x8B        ; 0040C5D8  mov edx, ecx
        _emit 0xD1
        _emit 0x8B        ; 0040C5DA  mov edi, esi
        _emit 0xFE
        _emit 0x81        ; 0040C5DC  and edx, 0x3e0
        _emit 0xE2
        _emit 0xE0
        _emit 0x03
        _emit 0x00
        _emit 0x00
        _emit 0x81        ; 0040C5E2  and edi, 0x3e0
        _emit 0xE7
        _emit 0xE0
        _emit 0x03
        _emit 0x00
        _emit 0x00
        _emit 0x2B        ; 0040C5E8  sub edx, edi
        _emit 0xD7
        _emit 0x66        ; 0040C5EA  cmp dx, 0x1f
        _emit 0x83
        _emit 0xFA
        _emit 0x1F
        _emit 0x7D        ; 0040C5EE  jge 0x40c5f2
        _emit 0x02
        _emit 0x33        ; 0040C5F0  xor edx, edx
        _emit 0xD2
        _emit 0x81        ; 0040C5F2  and esi, 0x7c00
        _emit 0xE6
        _emit 0x00
        _emit 0x7C
        _emit 0x00
        _emit 0x00
        _emit 0x81        ; 0040C5F8  and ecx, 0x7c00
        _emit 0xE1
        _emit 0x00
        _emit 0x7C
        _emit 0x00
        _emit 0x00
        _emit 0x2B        ; 0040C5FE  sub ecx, esi
        _emit 0xCE
        _emit 0x66        ; 0040C600  cmp cx, 0x3ff
        _emit 0x81
        _emit 0xF9
        _emit 0xFF
        _emit 0x03
        _emit 0x7D        ; 0040C605  jge 0x40c609
        _emit 0x02
        _emit 0x33        ; 0040C607  xor ecx, ecx
        _emit 0xC9
        _emit 0x03        ; 0040C609  add ecx, edx
        _emit 0xCA
        _emit 0x03        ; 0040C60B  add ecx, ebx
        _emit 0xCB
        _emit 0x66        ; 0040C60D  mov word ptr [eax], cx
        _emit 0x89
        _emit 0x08
        _emit 0x8B        ; 0040C610  mov edx, dword ptr [esp + 0x2c]
        _emit 0x54
        _emit 0x24
        _emit 0x2C
        _emit 0x8B        ; 0040C614  mov ecx, dword ptr [esp + 0x1c]
        _emit 0x4C
        _emit 0x24
        _emit 0x1C
        _emit 0x83        ; 0040C618  add eax, 2
        _emit 0xC0
        _emit 0x02
        _emit 0x03        ; 0040C61B  add ebp, edx
        _emit 0xEA
        _emit 0x49        ; 0040C61D  dec ecx
        _emit 0x89        ; 0040C61E  mov dword ptr [esp + 0x1c], ecx
        _emit 0x4C
        _emit 0x24
        _emit 0x1C
        _emit 0x75        ; 0040C622  jne 0x40c5aa
        _emit 0x86
        _emit 0x8B        ; 0040C624  mov ecx, dword ptr [esp + 0x34]
        _emit 0x4C
        _emit 0x24
        _emit 0x34
        _emit 0x8B        ; 0040C628  mov edx, dword ptr [esp + 0x28]
        _emit 0x54
        _emit 0x24
        _emit 0x28
        _emit 0x03        ; 0040C62C  add ebp, edx
        _emit 0xEA
        _emit 0x8D        ; 0040C62E  lea eax, [eax + ecx*2]
        _emit 0x04
        _emit 0x48
        _emit 0x8B        ; 0040C631  mov ecx, dword ptr [esp + 0x38]
        _emit 0x4C
        _emit 0x24
        _emit 0x38
        _emit 0x49        ; 0040C635  dec ecx
        _emit 0x89        ; 0040C636  mov dword ptr [esp + 0x38], ecx
        _emit 0x4C
        _emit 0x24
        _emit 0x38
        _emit 0x0F        ; 0040C63A  jne 0x40c5a2
        _emit 0x85
        _emit 0x62
        _emit 0xFF
        _emit 0xFF
        _emit 0xFF
        _emit 0x5F        ; 0040C640  pop edi
        _emit 0x5E        ; 0040C641  pop esi
        _emit 0x5D        ; 0040C642  pop ebp
        _emit 0x5B        ; 0040C643  pop ebx
        _emit 0x83        ; 0040C644  add esp, 8
        _emit 0xC4
        _emit 0x08
        _emit 0xC3        ; 0040C647  ret
        _emit 0x66        ; 0040C648  mov si, word ptr [ecx + 0x50]
        _emit 0x8B
        _emit 0x71
        _emit 0x50
        _emit 0x8B        ; 0040C64C  mov ecx, dword ptr [0x424704]
        _emit 0x0D
        _emit 0x04
        _emit 0x47
        _emit 0x42
        _emit 0x00
        _emit 0xBF        ; 0040C652  mov edi, 0x20
        _emit 0x20
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x89        ; 0040C657  mov dword ptr [esp + 0x10], esi
        _emit 0x74
        _emit 0x24
        _emit 0x10
        _emit 0x2B        ; 0040C65B  sub edi, esi
        _emit 0xFE
        _emit 0x85        ; 0040C65D  test ecx, ecx
        _emit 0xC9
        _emit 0x89        ; 0040C65F  mov dword ptr [esp + 0x14], edi
        _emit 0x7C
        _emit 0x24
        _emit 0x14
        _emit 0x0F        ; 0040C663  je 0x40c753
        _emit 0x84
        _emit 0xEA
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x89        ; 0040C669  mov dword ptr [esp + 0x38], ebx
        _emit 0x5C
        _emit 0x24
        _emit 0x38
        _emit 0x8B        ; 0040C66D  mov edx, dword ptr [esp + 0x30]
        _emit 0x54
        _emit 0x24
        _emit 0x30
        _emit 0x89        ; 0040C671  mov dword ptr [esp + 0x1c], edx
        _emit 0x54
        _emit 0x24
        _emit 0x1C
        _emit 0x8B        ; 0040C675  mov edx, dword ptr [esp + 0x24]
        _emit 0x54
        _emit 0x24
        _emit 0x24
        _emit 0x33        ; 0040C679  xor ecx, ecx
        _emit 0xC9
        _emit 0x8A        ; 0040C67B  mov cl, byte ptr [ebp]
        _emit 0x4D
        _emit 0x00
        _emit 0x66        ; 0040C67E  mov dx, word ptr [edx + ecx*2]
        _emit 0x8B
        _emit 0x14
        _emit 0x4A
        _emit 0x66        ; 0040C682  test dx, dx
        _emit 0x85
        _emit 0xD2
        _emit 0x0F        ; 0040C685  je 0x40c70f
        _emit 0x84
        _emit 0x84
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x66        ; 0040C68B  mov cx, word ptr [eax]
        _emit 0x8B
        _emit 0x08
        _emit 0x81        ; 0040C68E  and edx, 0xffff
        _emit 0xE2
        _emit 0xFF
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x81        ; 0040C694  and ecx, 0xffff
        _emit 0xE1
        _emit 0xFF
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040C69A  mov ebp, edx
        _emit 0xEA
        _emit 0x8B        ; 0040C69C  mov ebx, ecx
        _emit 0xD9
        _emit 0x81        ; 0040C69E  and ebp, 0x7e0
        _emit 0xE5
        _emit 0xE0
        _emit 0x07
        _emit 0x00
        _emit 0x00
        _emit 0x81        ; 0040C6A4  and ebx, 0x7e0
        _emit 0xE3
        _emit 0xE0
        _emit 0x07
        _emit 0x00
        _emit 0x00
        _emit 0x0F        ; 0040C6AA  imul ebp, edi
        _emit 0xAF
        _emit 0xEF
        _emit 0x0F        ; 0040C6AD  imul ebx, esi
        _emit 0xAF
        _emit 0xDE
        _emit 0x03        ; 0040C6B0  add ebx, ebp
        _emit 0xDD
        _emit 0x8B        ; 0040C6B2  mov ebp, ecx
        _emit 0xE9
        _emit 0x83        ; 0040C6B4  and ebp, 0x1f
        _emit 0xE5
        _emit 0x1F
        _emit 0x81        ; 0040C6B7  and ecx, 0xf800
        _emit 0xE1
        _emit 0x00
        _emit 0xF8
        _emit 0x00
        _emit 0x00
        _emit 0x0F        ; 0040C6BD  imul ebp, esi
        _emit 0xAF
        _emit 0xEE
        _emit 0x8B        ; 0040C6C0  mov esi, edx
        _emit 0xF2
        _emit 0x81        ; 0040C6C2  and edx, 0xf800
        _emit 0xE2
        _emit 0x00
        _emit 0xF8
        _emit 0x00
        _emit 0x00
        _emit 0x83        ; 0040C6C8  and esi, 0x1f
        _emit 0xE6
        _emit 0x1F
        _emit 0x0F        ; 0040C6CB  imul esi, edi
        _emit 0xAF
        _emit 0xF7
        _emit 0x03        ; 0040C6CE  add ebp, esi
        _emit 0xEE
        _emit 0x8B        ; 0040C6D0  mov esi, dword ptr [esp + 0x10]
        _emit 0x74
        _emit 0x24
        _emit 0x10
        _emit 0xC1        ; 0040C6D4  sar ebx, 5
        _emit 0xFB
        _emit 0x05
        _emit 0xC1        ; 0040C6D7  sar ebp, 5
        _emit 0xFD
        _emit 0x05
        _emit 0x81        ; 0040C6DA  and ebx, 0x7e0
        _emit 0xE3
        _emit 0xE0
        _emit 0x07
        _emit 0x00
        _emit 0x00
        _emit 0x83        ; 0040C6E0  and ebp, 0x1f
        _emit 0xE5
        _emit 0x1F
        _emit 0x03        ; 0040C6E3  add ebx, ebp
        _emit 0xDD
        _emit 0x8B        ; 0040C6E5  mov ebp, esi
        _emit 0xEE
        _emit 0x81        ; 0040C6E7  and ebp, 0xffff
        _emit 0xE5
        _emit 0xFF
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x0F        ; 0040C6ED  imul ecx, ebp
        _emit 0xAF
        _emit 0xCD
        _emit 0x8B        ; 0040C6F0  mov ebp, edi
        _emit 0xEF
        _emit 0x81        ; 0040C6F2  and ebp, 0xffff
        _emit 0xE5
        _emit 0xFF
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x0F        ; 0040C6F8  imul edx, ebp
        _emit 0xAF
        _emit 0xD5
        _emit 0x03        ; 0040C6FB  add ecx, edx
        _emit 0xCA
        _emit 0x8B        ; 0040C6FD  mov ebp, dword ptr [esp + 0x20]
        _emit 0x6C
        _emit 0x24
        _emit 0x20
        _emit 0xC1        ; 0040C701  sar ecx, 5
        _emit 0xF9
        _emit 0x05
        _emit 0x81        ; 0040C704  and ecx, 0xf800
        _emit 0xE1
        _emit 0x00
        _emit 0xF8
        _emit 0x00
        _emit 0x00
        _emit 0x03        ; 0040C70A  add ebx, ecx
        _emit 0xD9
        _emit 0x66        ; 0040C70C  mov word ptr [eax], bx
        _emit 0x89
        _emit 0x18
        _emit 0x8B        ; 0040C70F  mov edx, dword ptr [esp + 0x2c]
        _emit 0x54
        _emit 0x24
        _emit 0x2C
        _emit 0x8B        ; 0040C713  mov ecx, dword ptr [esp + 0x1c]
        _emit 0x4C
        _emit 0x24
        _emit 0x1C
        _emit 0x83        ; 0040C717  add eax, 2
        _emit 0xC0
        _emit 0x02
        _emit 0x03        ; 0040C71A  add ebp, edx
        _emit 0xEA
        _emit 0x49        ; 0040C71C  dec ecx
        _emit 0x89        ; 0040C71D  mov dword ptr [esp + 0x20], ebp
        _emit 0x6C
        _emit 0x24
        _emit 0x20
        _emit 0x89        ; 0040C721  mov dword ptr [esp + 0x1c], ecx
        _emit 0x4C
        _emit 0x24
        _emit 0x1C
        _emit 0x0F        ; 0040C725  jne 0x40c675
        _emit 0x85
        _emit 0x4A
        _emit 0xFF
        _emit 0xFF
        _emit 0xFF
        _emit 0x8B        ; 0040C72B  mov ecx, dword ptr [esp + 0x34]
        _emit 0x4C
        _emit 0x24
        _emit 0x34
        _emit 0x8B        ; 0040C72F  mov edx, dword ptr [esp + 0x28]
        _emit 0x54
        _emit 0x24
        _emit 0x28
        _emit 0x03        ; 0040C733  add ebp, edx
        _emit 0xEA
        _emit 0x8D        ; 0040C735  lea eax, [eax + ecx*2]
        _emit 0x04
        _emit 0x48
        _emit 0x8B        ; 0040C738  mov ecx, dword ptr [esp + 0x38]
        _emit 0x4C
        _emit 0x24
        _emit 0x38
        _emit 0x49        ; 0040C73C  dec ecx
        _emit 0x89        ; 0040C73D  mov dword ptr [esp + 0x20], ebp
        _emit 0x6C
        _emit 0x24
        _emit 0x20
        _emit 0x89        ; 0040C741  mov dword ptr [esp + 0x38], ecx
        _emit 0x4C
        _emit 0x24
        _emit 0x38
        _emit 0x0F        ; 0040C745  jne 0x40c66d
        _emit 0x85
        _emit 0x22
        _emit 0xFF
        _emit 0xFF
        _emit 0xFF
        _emit 0x5F        ; 0040C74B  pop edi
        _emit 0x5E        ; 0040C74C  pop esi
        _emit 0x5D        ; 0040C74D  pop ebp
        _emit 0x5B        ; 0040C74E  pop ebx
        _emit 0x83        ; 0040C74F  add esp, 8
        _emit 0xC4
        _emit 0x08
        _emit 0xC3        ; 0040C752  ret
        _emit 0x89        ; 0040C753  mov dword ptr [esp + 0x38], ebx
        _emit 0x5C
        _emit 0x24
        _emit 0x38
        _emit 0x8B        ; 0040C757  mov edx, dword ptr [esp + 0x30]
        _emit 0x54
        _emit 0x24
        _emit 0x30
        _emit 0x89        ; 0040C75B  mov dword ptr [esp + 0x1c], edx
        _emit 0x54
        _emit 0x24
        _emit 0x1C
        _emit 0x8B        ; 0040C75F  mov edx, dword ptr [esp + 0x24]
        _emit 0x54
        _emit 0x24
        _emit 0x24
        _emit 0x33        ; 0040C763  xor ecx, ecx
        _emit 0xC9
        _emit 0x8A        ; 0040C765  mov cl, byte ptr [ebp]
        _emit 0x4D
        _emit 0x00
        _emit 0x66        ; 0040C768  mov cx, word ptr [edx + ecx*2]
        _emit 0x8B
        _emit 0x0C
        _emit 0x4A
        _emit 0x66        ; 0040C76C  test cx, cx
        _emit 0x85
        _emit 0xC9
        _emit 0x0F        ; 0040C76F  je 0x40c7f9
        _emit 0x84
        _emit 0x84
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x66        ; 0040C775  mov dx, word ptr [eax]
        _emit 0x8B
        _emit 0x10
        _emit 0x81        ; 0040C778  and ecx, 0xffff
        _emit 0xE1
        _emit 0xFF
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x81        ; 0040C77E  and edx, 0xffff
        _emit 0xE2
        _emit 0xFF
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040C784  mov ebx, ecx
        _emit 0xD9
        _emit 0x8B        ; 0040C786  mov ebp, edx
        _emit 0xEA
        _emit 0x81        ; 0040C788  and ebx, 0x3e0
        _emit 0xE3
        _emit 0xE0
        _emit 0x03
        _emit 0x00
        _emit 0x00
        _emit 0x81        ; 0040C78E  and ebp, 0x3e0
        _emit 0xE5
        _emit 0xE0
        _emit 0x03
        _emit 0x00
        _emit 0x00
        _emit 0x0F        ; 0040C794  imul ebx, edi
        _emit 0xAF
        _emit 0xDF
        _emit 0x0F        ; 0040C797  imul ebp, esi
        _emit 0xAF
        _emit 0xEE
        _emit 0x03        ; 0040C79A  add ebx, ebp
        _emit 0xDD
        _emit 0x8B        ; 0040C79C  mov ebp, ecx
        _emit 0xE9
        _emit 0x83        ; 0040C79E  and ebp, 0x1f
        _emit 0xE5
        _emit 0x1F
        _emit 0x81        ; 0040C7A1  and ecx, 0x7c00
        _emit 0xE1
        _emit 0x00
        _emit 0x7C
        _emit 0x00
        _emit 0x00
        _emit 0x0F        ; 0040C7A7  imul ebp, edi
        _emit 0xAF
        _emit 0xEF
        _emit 0x8B        ; 0040C7AA  mov edi, edx
        _emit 0xFA
        _emit 0x81        ; 0040C7AC  and edx, 0x7c00
        _emit 0xE2
        _emit 0x00
        _emit 0x7C
        _emit 0x00
        _emit 0x00
        _emit 0x83        ; 0040C7B2  and edi, 0x1f
        _emit 0xE7
        _emit 0x1F
        _emit 0x0F        ; 0040C7B5  imul edi, esi
        _emit 0xAF
        _emit 0xFE
        _emit 0x03        ; 0040C7B8  add ebp, edi
        _emit 0xEF
        _emit 0x8B        ; 0040C7BA  mov edi, dword ptr [esp + 0x14]
        _emit 0x7C
        _emit 0x24
        _emit 0x14
        _emit 0xC1        ; 0040C7BE  sar ebx, 5
        _emit 0xFB
        _emit 0x05
        _emit 0xC1        ; 0040C7C1  sar ebp, 5
        _emit 0xFD
        _emit 0x05
        _emit 0x81        ; 0040C7C4  and ebx, 0x3e0
        _emit 0xE3
        _emit 0xE0
        _emit 0x03
        _emit 0x00
        _emit 0x00
        _emit 0x83        ; 0040C7CA  and ebp, 0x1f
        _emit 0xE5
        _emit 0x1F
        _emit 0x03        ; 0040C7CD  add ebx, ebp
        _emit 0xDD
        _emit 0x8B        ; 0040C7CF  mov ebp, edi
        _emit 0xEF
        _emit 0x81        ; 0040C7D1  and ebp, 0xffff
        _emit 0xE5
        _emit 0xFF
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x0F        ; 0040C7D7  imul ecx, ebp
        _emit 0xAF
        _emit 0xCD
        _emit 0x8B        ; 0040C7DA  mov ebp, esi
        _emit 0xEE
        _emit 0x81        ; 0040C7DC  and ebp, 0xffff
        _emit 0xE5
        _emit 0xFF
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x0F        ; 0040C7E2  imul edx, ebp
        _emit 0xAF
        _emit 0xD5
        _emit 0x03        ; 0040C7E5  add ecx, edx
        _emit 0xCA
        _emit 0x8B        ; 0040C7E7  mov ebp, dword ptr [esp + 0x20]
        _emit 0x6C
        _emit 0x24
        _emit 0x20
        _emit 0xC1        ; 0040C7EB  sar ecx, 5
        _emit 0xF9
        _emit 0x05
        _emit 0x81        ; 0040C7EE  and ecx, 0x7c00
        _emit 0xE1
        _emit 0x00
        _emit 0x7C
        _emit 0x00
        _emit 0x00
        _emit 0x03        ; 0040C7F4  add ebx, ecx
        _emit 0xD9
        _emit 0x66        ; 0040C7F6  mov word ptr [eax], bx
        _emit 0x89
        _emit 0x18
        _emit 0x8B        ; 0040C7F9  mov edx, dword ptr [esp + 0x2c]
        _emit 0x54
        _emit 0x24
        _emit 0x2C
        _emit 0x8B        ; 0040C7FD  mov ecx, dword ptr [esp + 0x1c]
        _emit 0x4C
        _emit 0x24
        _emit 0x1C
        _emit 0x83        ; 0040C801  add eax, 2
        _emit 0xC0
        _emit 0x02
        _emit 0x03        ; 0040C804  add ebp, edx
        _emit 0xEA
        _emit 0x49        ; 0040C806  dec ecx
        _emit 0x89        ; 0040C807  mov dword ptr [esp + 0x20], ebp
        _emit 0x6C
        _emit 0x24
        _emit 0x20
        _emit 0x89        ; 0040C80B  mov dword ptr [esp + 0x1c], ecx
        _emit 0x4C
        _emit 0x24
        _emit 0x1C
        _emit 0x0F        ; 0040C80F  jne 0x40c75f
        _emit 0x85
        _emit 0x4A
        _emit 0xFF
        _emit 0xFF
        _emit 0xFF
        _emit 0x8B        ; 0040C815  mov ecx, dword ptr [esp + 0x34]
        _emit 0x4C
        _emit 0x24
        _emit 0x34
        _emit 0x8B        ; 0040C819  mov edx, dword ptr [esp + 0x28]
        _emit 0x54
        _emit 0x24
        _emit 0x28
        _emit 0x03        ; 0040C81D  add ebp, edx
        _emit 0xEA
        _emit 0x8D        ; 0040C81F  lea eax, [eax + ecx*2]
        _emit 0x04
        _emit 0x48
        _emit 0x8B        ; 0040C822  mov ecx, dword ptr [esp + 0x38]
        _emit 0x4C
        _emit 0x24
        _emit 0x38
        _emit 0x49        ; 0040C826  dec ecx
        _emit 0x89        ; 0040C827  mov dword ptr [esp + 0x20], ebp
        _emit 0x6C
        _emit 0x24
        _emit 0x20
        _emit 0x89        ; 0040C82B  mov dword ptr [esp + 0x38], ecx
        _emit 0x4C
        _emit 0x24
        _emit 0x38
        _emit 0x0F        ; 0040C82F  jne 0x40c757
        _emit 0x85
        _emit 0x22
        _emit 0xFF
        _emit 0xFF
        _emit 0xFF
        _emit 0x5F        ; 0040C835  pop edi
        _emit 0x5E        ; 0040C836  pop esi
        _emit 0x5D        ; 0040C837  pop ebp
        _emit 0x5B        ; 0040C838  pop ebx
        _emit 0x83        ; 0040C839  add esp, 8
        _emit 0xC4
        _emit 0x08
        _emit 0xC3        ; 0040C83C  ret
        _emit 0x8D        ; 0040C83D  lea ecx, [ecx]
        _emit 0x49
        _emit 0x00
        _emit 0x9B        ; 0040C840  wait
        _emit 0xC2        ; 0040C841  ret 0x40
        _emit 0x40
        _emit 0x00
        _emit 0xE4        ; 0040C844  in al, 0xc2
        _emit 0xC2
        _emit 0x40        ; 0040C846  inc eax
        _emit 0x00        ; 0040C847  add byte ptr [esi - 0x1affbf3d], ch
        _emit 0xAE
        _emit 0xC3
        _emit 0x40
        _emit 0x00
        _emit 0xE5
        _emit 0xC4        ; 0040C84D  les eax, ptr [eax]
        _emit 0x40
        _emit 0x00
        _emit 0x48        ; 0040C850  dec eax
        _emit 0xC6        ; 0040C851  mov byte ptr [eax], 0x90
        _emit 0x40
        _emit 0x00
        _emit 0x90
        _emit 0x90        ; 0040C855  nop
        _emit 0x90        ; 0040C856  nop
        _emit 0x90        ; 0040C857  nop
        _emit 0x90        ; 0040C858  nop
        _emit 0x90        ; 0040C859  nop
        _emit 0x90        ; 0040C85A  nop
        _emit 0x90        ; 0040C85B  nop
        _emit 0x90        ; 0040C85C  nop
        _emit 0x90        ; 0040C85D  nop
        _emit 0x90        ; 0040C85E  nop
        _emit 0x90        ; 0040C85F  nop
    }
}

/*
 * vDrawHitboxFilled - hit-judge display: draw a hit box of the current object as a 50% mixed filled
 * rectangle.  pHitbox: the box's script step (16 bytes: +1 x offset, +3 y offset, +5 half width, +7 half
 * height, shorts, pixels, relative to the object's position); NULL draws nothing.  uColor: RGB555
 * (guHitboxColors); bFlipX: the object faces the other way (x offset mirrored).
 * Globals read: gpkgtCurrentEngineObject, giCameraX, giCameraY.
 */
void vDrawHitboxFilled(BYTE *pHitbox, UINT uColor, int bFlipX)
{
    kgtEngineObject *pObj;
    int iX, iY, iBoxW, iBoxH;   /* box centre on the screen and half size (pixels) */

    if (pHitbox) {
        pObj = gpkgtCurrentEngineObject;
        iBoxW = *(short *)(pHitbox + 5);
        iY = pObj->iPosY / 0x10000 + *(short *)(pHitbox + 3) - giCameraY;
        iBoxH = *(short *)(pHitbox + 7);
        if (bFlipX)
            iX = pObj->iPosX / 0x10000 - *(short *)(pHitbox + 1) - giCameraX;
        else
            iX = pObj->iPosX / 0x10000 + *(short *)(pHitbox + 1) - giCameraX;
        vFillRect16(iX - iBoxW, iY - iBoxH, iBoxW * 2, iBoxH * 2, 1, uColor);
    }
}

/*
 * vDrawHitboxOutline - as vDrawHitboxFilled, but draws only the box's 1-pixel outline (four 50% mixed
 * lines).
 */
void vDrawHitboxOutline(BYTE *pHitbox, UINT uColor, int bFlipX)
{
    kgtEngineObject *pObj;
    int iX, iY, iBoxW, iBoxH;   /* centre and half size, then top left and full size (pixels) */

    if (pHitbox) {
        pObj = gpkgtCurrentEngineObject;
        iY = pObj->iPosY / 0x10000 + *(short *)(pHitbox + 3) - giCameraY;
        iBoxW = *(short *)(pHitbox + 5);
        iBoxH = *(short *)(pHitbox + 7);
        if (bFlipX)
            iX = pObj->iPosX / 0x10000 - *(short *)(pHitbox + 1) - giCameraX;
        else
            iX = pObj->iPosX / 0x10000 + *(short *)(pHitbox + 1) - giCameraX;
        /* to top left and full size, then left, top, right and bottom edge */
        iY -= iBoxH;
        iX -= iBoxW;
        iBoxW *= 2;
        iBoxH *= 2;
        vFillRect16(iX, iY, 1, iBoxH, 1, uColor);
        vFillRect16(iX, iY, iBoxW, 1, 1, uColor);
        vFillRect16(iX + iBoxW - 1, iY, 1, iBoxH, 1, uColor);
        vFillRect16(iX, iY + iBoxH - 1, iBoxW, 1, 1, uColor);
    }
}
/*
 * vCalculateShake - advance one screen shake (script command EB) by a frame and compute its offset.
 * pShake: { mode, current offset (pixels), amplitude, frames left, total frames } (giShakeXMode..
 * or giShakeYMode..).  Modes: 1 fade out, 2 fade in, 3 fixed, 4 random 0..amplitude-1; the offset's
 * sign alternates every frame (giReverseShakeDirection bit 0).  The offset becomes 0 when the shake ends.
 * Globals read: giReverseShakeDirection.
 */
void vCalculateShake(int *pShake)
{
    int iPercent;       /* frames left in percent of the total */
    int iAmp;

    /* no shake running / the last frame */
    if (pShake[3] == 0)
        return;
    if (--pShake[3] == 0) {
        pShake[1] = 0;
        return;
    }
    iPercent = pShake[3] * 100 / pShake[4];
    switch (pShake[0]) {
    case 1:     /* fade out */
        pShake[1] = pShake[2] * iPercent / 100;
        break;
    case 2:     /* fade in */
        pShake[1] = (100 - iPercent) * pShake[2] / 100;
        break;
    case 3:     /* fixed */
        pShake[1] = pShake[2];
        break;
    case 4:     /* random */
        iAmp = pShake[2];
        if (iAmp)
            pShake[1] = rand() % iAmp;
        break;
    }
    if (giReverseShakeDirection & 1)
        pShake[1] = -pShake[1];
}

/*
 * vApplyObjectColorTransition - set the current object's colour (iColorRed/Green/Blue/Alpha) from a
 * colour effect (the layout of kgt_character_struct.flash; giSystemFlashType, giStageFlashType).
 * pFx: { mode, r0, g0, b0, a0, frames left, r1, g1, b1, a1, total frames }.  Modes: 1 smooth (from
 * r1.. at the start to r0.. at the end), 2 blinking (r1.. on odd frames of giReverseShakeDirection,
 * else as 1), 3 random mix of the two.  Nothing happens when no frames are left; the countdown itself
 * is run elsewhere.
 * Globals read: giReverseShakeDirection.  Globals changed: gpkgtCurrentEngineObject's colour.
 */
void vApplyObjectColorTransition(int *pFx)
{
    int iPercent, iInv;     /* weight of r1.. / of r0.. (percent) */

    if (pFx[5]) {
        iPercent = pFx[5] * 100 / pFx[10];
        iInv = 100 - iPercent;
        switch (pFx[0]) {
        case 3:
            iPercent = rand() % 100;
            iInv = 100 - iPercent;
            gpkgtCurrentEngineObject->iColorRed = (pFx[1] * iInv + pFx[6] * iPercent) / 100;
            gpkgtCurrentEngineObject->iColorGreen = (pFx[2] * iInv + pFx[7] * iPercent) / 100;
            gpkgtCurrentEngineObject->iColorBlue = (pFx[3] * iInv + pFx[8] * iPercent) / 100;
            gpkgtCurrentEngineObject->iColorAlpha = (pFx[4] * iInv + pFx[9] * iPercent) / 100;
            break;
        case 2:
            if (giReverseShakeDirection & 1) {
                gpkgtCurrentEngineObject->iColorRed = pFx[6];
                gpkgtCurrentEngineObject->iColorGreen = pFx[7];
                gpkgtCurrentEngineObject->iColorBlue = pFx[8];
                gpkgtCurrentEngineObject->iColorAlpha = pFx[9];
                break;
            }
            /* fall through */
        case 1:
            gpkgtCurrentEngineObject->iColorRed = (pFx[1] * iInv + pFx[6] * iPercent) / 100;
            gpkgtCurrentEngineObject->iColorGreen = (pFx[2] * iInv + pFx[7] * iPercent) / 100;
            gpkgtCurrentEngineObject->iColorBlue = (pFx[3] * iInv + pFx[8] * iPercent) / 100;
            gpkgtCurrentEngineObject->iColorAlpha = (pFx[4] * iInv + pFx[9] * iPercent) / 100;
            break;
        }
    }
}

/*
 * vDrawCurrentEngineObject - draw gpkgtCurrentEngineObject into the frame buffer (called for every object
 * after its handler).  What is drawn depends on iDrawFlag:
 *   -1   the image of its current script step (the step before iSkillScriptIdx) from the file its
 *        iObjectType selects (system, demo, stage or a player slot's character), with its palette
 *        (a character's colour palette, or the image's own palette) tinted by the object's colour
 *        (after the file's colour effect, vApplyObjectColorTransition), and first the frames of its
 *        after-image trail (cAfterImageIdx).  Characters: centred on x, standing on y, mirrored when
 *        looking right, with the hit-judge display (hit boxes and state) in test play; stage objects:
 *        parallax scrolling and tiling (kgtSkillStageScroll); system/demo objects: the life and special
 *        gauges (iPlayerIdx 10/11/20/21) are cut to the value they show.
 *   > 0  the external bitmap gkgtBitmaps[iDrawFlag] at iPosX/iPosY (16.16), in the object's blend mode
 *        and tint.
 *   < -1 special effects drawn from text.bmp or on the frame buffer: -2 shadows of the script objects,
 *        -3 effect animation (vjmpFadeDriftEffect), -4 overlay, -5 small number, -6 large number with a
 *        label, -7 "round N", -8 a caption, -10 blur of the whole screen.
 * The tinted palette is built in gwTintedPalette16 in the screen format: RGB565 full screen, RGB555 in
 * a window; KGT palettes are RGBQUAD-like dwords (B, G, R, x), so GetBValue() reads the red byte and
 * GetRValue() the blue one; entries 0 (transparent) stay 0 and a tinted black becomes 1 so it is not
 * transparent.
 * Globals read: gkgtKgtSystem, gkgtLoadedDemo, gkgtLoadedStage, gkgtLoadedCharacter, gAfterImageLayers,
 * gkgtBitmaps, gkgtEngineObjects, gkgtGameState, giCameraX/Y, giShakeX/YOffset, giHitJudge,
 * giScreenMode, giReverseShakeDirection, giSystemFlashType, giStageFlashType, guHitboxColors.
 * Globals changed: the frame buffer gpFrameBits, gwTintedPalette16, gpGlobalMemoryAlloc (decompressed
 * images), the current object's colour (colour effect; restored after the after-image frames).
 *
 * Built from the original's machine code: VC6 does not yet compile the C to exactly these bytes.
 * The dead `if (0)` copy of the C body keeps the file's string literals and imported symbols in
 * the original order; the plain C version is on the `nonmatching` branch.
 * Still different: (the iObjectType/pChar register swap, the pChar/pStep stack slots, the layer reload order, the
 * held flip arm and the blur preheader).  Older note: code shape (block order, switch tables, tail
 * merges) matches, but register and stack slot allocation differ (frame 0x70 instead of 0x6c). The
 * original keeps cAfterImageIdx * 0x650 as a separate term in the layer addressing
 * ([n*16 + ofs + base]) where we get ((idx*101 + n) * 16).
 *
 * matching scaffolding in the body: the many "if (0) goto boundaryN; boundaryN:;" are empty block
 * boundaries that stop VC6 from moving or forward-substituting code across them; "k = iX" and the split
 * x/y computations (iX = ...; boundary; iX -= ...) reproduce the original's evaluation order; the dead
 * "pBlurTL = 0; ..." in case -8 fixes the order of the pointer loads in the blur loop; pAI indexes the
 * trail as ints ((iSlot + 1) * 4 + field) to get the original's addressing (vc6-matching-notes/matching-techniques.md).
 */
/* the original machine code; the C in the dead block keeps the literals and imports */
__declspec(naked) void vDrawCurrentEngineObject(void)
{
    if (0) {
        int iSavedAOrRed;
        int iBlurCol;
        int iBlurPasses;
        WORD * pTintDst;
        int iLayerRed;
        kgtImageHeader *pHeader;
        kgtSkillImageStep *pStep;
        kgt_character_struct *pChar;
        DWORD *pPalette;
        BYTE *pPixels;
        int iImgW, iImgH;
        int bFlip;
        int iX, iY;
        int iRed, iGreen, iBlue;
        int k;
        int iDrawW;
        WORD wImage;
        UINT uFlags;
        int iPlayer;
        int bHitJudge;
        int bHeldFlip;
        BYTE cScrollFlags;
        kgtSkillStageScroll *pScroll;
        DWORD *pLayerPalEntry;
        DWORD *pPalEntry;
        WORD *pLayerTintDst;
        int iSet;
        int *pAI;
        kgtImageHeader *pPrevHeader;
        kgtImageHeader *pLayerHeader;
        kgtSkillImageStep *pLayerStep;
        int iLayerW, iLayerH;
        int bLayerFlip;
        int iLayerImage;
        int iFrame, iSlot;
        int iSavedBlend, iSavedR, iSavedG, iSavedB, iSavedA;
        int iPercent;
        kgtBMPINFO *pBmp;
        kgtEngineObject *pObj;
        WORD *pBlurTL, *pBlurTR, *pBlurBL, *pBlurBR;
        int iRow, iCol;
        int iInv;
    
        pPalette = 0;
        pLayerTintDst = 0;
        if (gpkgtCurrentEngineObject->iDrawFlag == -1) {
            if (0) goto boundary1; boundary1:;
            switch (gpkgtCurrentEngineObject->iObjectType) {
            case SYSTEM_ENGINE_OBJECT:
                if (0) goto boundary2; boundary2:;
                pChar = (kgt_character_struct *)&gkgtKgtSystem;
                break;
            case DEMO_ENGINE_OBJECT:
                pChar = (kgt_character_struct *)&gkgtLoadedDemo;
                break;
            case STAGE_ENGINE_OBJECT:
                pChar = (kgt_character_struct *)&gkgtLoadedStage;
                break;
            case PLAYER_ENGINE_OBJECT:
            case STORY_ENGINE_OBJECT:
            case CHARACTER_ENGINE_OBJECT:
                pChar = &gkgtLoadedCharacter[gpkgtCurrentEngineObject->iPlayerIdx];
                if (0) goto boundary3; boundary3:;
                break;
            }
            pStep = (kgtSkillImageStep *)&pChar->kgtCore.pSkillScriptsAlloc[gpkgtCurrentEngineObject->iSkillScriptIdx - 1];
            pHeader = &pChar->kgtCore.pImageHeaders[pStep->wImage & 0x1fff];
            if (pHeader->pAlloc == NULL)
                ;
            iImgW = pHeader->iWidth;
            iImgH = pHeader->iHeight;
            if (0) goto boundary4; boundary4:;
            if (0) goto boundary5; boundary5:;
            bFlip = (pStep->wImage & 0x4000) >> 14;
            if (gpkgtCurrentEngineObject->iObjectType > STORY_ENGINE_OBJECT)
                pPalette = (DWORD *)pChar->kgtCore.kgtPalettes;
            else if (pChar->iColor < 0)
                pPalette = (DWORD *)pChar->kgtCore.kgtPalettes;
            else
                pPalette = (DWORD *)pChar->kgtCore.kgtPalettes + pChar->iColor * 0x108;
    
            /* extra image layers (after-images) */
            if (gpkgtCurrentEngineObject->cAfterImageIdx != 0) {
                iSet = gpkgtCurrentEngineObject->cAfterImageIdx;
                pAI = (int *)&gAfterImageLayers[iSet - 1];
                if (((BYTE *)pAI[2])[4] != 0) {
                    iSavedBlend = gpkgtCurrentEngineObject->iColorBlendtype;
                    gpkgtCurrentEngineObject->iColorBlendtype = gAfterImageLayers[iSet - 1].pInfo[5];
                    iSavedR = gpkgtCurrentEngineObject->iColorRed;
                    iSavedG = gpkgtCurrentEngineObject->iColorGreen;
                    if (0) goto boundary6; boundary6:;
                    if (0) goto boundary7; boundary7:;
                    if (0) goto boundary8; boundary8:;
                    pPrevHeader = NULL;
                    iSavedB = gpkgtCurrentEngineObject->iColorBlue;
                    iSavedAOrRed = gpkgtCurrentEngineObject->iColorAlpha;
                    iSlot = (pAI[1] - gAfterImageLayers[iSet - 1].pInfo[3] + 100) % 100;
                    if (0) goto boundary9; boundary9:;
                    for (iFrame = 0; iFrame < gAfterImageLayers[iSet - 1].pInfo[3]; iFrame++, iSlot = (iSlot + 1) % 100) {
                        pLayerStep = (kgtSkillImageStep *)pAI[(iSlot + 1) * 4 + 3];
                        if (0) goto boundary10; boundary10:;
                        if (0) goto boundary11; boundary11:;
                        bLayerFlip = pAI[(iSlot + 1) * 4 + 2] & 3;
                        if (0) goto boundary12; boundary12:;
                        if (0) goto boundary13; boundary13:;
                        if (pLayerStep != NULL) {
                            if (0) goto boundary14; boundary14:;
                            iLayerImage = pLayerStep->wImage;
                            pLayerHeader = &pChar->kgtCore.pImageHeaders[iLayerImage & 0x1fff];
                            if (pLayerHeader->pAlloc != NULL) {
                                iLayerW = pLayerHeader->iWidth;
                                iLayerH = pLayerHeader->iHeight;
                                if (pLayerHeader->iSize) {
                                    if (0) goto boundary15; boundary15:;
                                    if (pLayerHeader != pPrevHeader)
                                        iKgtDecompress((BYTE *)gpGlobalMemoryAlloc, (BYTE *)pLayerHeader->pAlloc, pLayerHeader->iSize);
                                    if (0) goto boundary16; boundary16:;
                                    if (pLayerHeader->iFlags & 1) {
                                        pPalette = (DWORD *)gpGlobalMemoryAlloc;
                                        if (0) goto boundary17; boundary17:;
                                        pPixels = (BYTE *)gpGlobalMemoryAlloc + 0x400;
                                    } else {
                                        pPixels = (BYTE *)gpGlobalMemoryAlloc;
                                    }
                                } else {
                                    pPixels = (BYTE *)pLayerHeader->pAlloc;
                                    if (pLayerHeader->iFlags & 1) {
                                        pPalette = (DWORD *)pLayerHeader->pAlloc;
                                        pPixels = (BYTE *)pLayerHeader->pAlloc + 0x400;
                                    }
                                }
                                switch (gAfterImageLayers[iSet - 1].pInfo[6]) {
                                case 0:
                                    gpkgtCurrentEngineObject->iColorRed = iSavedR;
                                    gpkgtCurrentEngineObject->iColorGreen = iSavedG;
                                    gpkgtCurrentEngineObject->iColorBlue = iSavedB;
                                    gpkgtCurrentEngineObject->iColorAlpha = iSavedAOrRed;
                                    break;
                                case 3:
                                    if (!(giReverseShakeDirection & 1))
                                        goto fade;
                                    gpkgtCurrentEngineObject->iColorRed = (char)gAfterImageLayers[iSet - 1].pInfo[7];
                                    gpkgtCurrentEngineObject->iColorGreen = (char)gAfterImageLayers[iSet - 1].pInfo[8];
                                    gpkgtCurrentEngineObject->iColorBlue = (char)gAfterImageLayers[iSet - 1].pInfo[9];
                                    gpkgtCurrentEngineObject->iColorAlpha = (char)gAfterImageLayers[iSet - 1].pInfo[10];
                                    break;
                                case 1:
                                    gpkgtCurrentEngineObject->iColorRed = (char)gAfterImageLayers[iSet - 1].pInfo[7];
                                    gpkgtCurrentEngineObject->iColorGreen = (char)gAfterImageLayers[iSet - 1].pInfo[8];
                                    gpkgtCurrentEngineObject->iColorBlue = (char)gAfterImageLayers[iSet - 1].pInfo[9];
                                    if (0) goto boundary18; boundary18:;
                                    gpkgtCurrentEngineObject->iColorAlpha = (char)gAfterImageLayers[iSet - 1].pInfo[10];
                                    break;
                                case 2:
                                if (0) goto boundary19; boundary19:;
                                if (0) goto boundary20; boundary20:;
                                if (0) goto boundary21; boundary21:;
                                if (0) goto boundary22; boundary22:;
                                fade:
                                    if (0) goto boundary23; boundary23:;
                                    if (0) goto boundary24; boundary24:;
                                    iPercent = ((gAfterImageLayers[iSet - 1].pInfo[4] * iFrame + pAI[3]) * 100) / (gAfterImageLayers[iSet - 1].pInfo[3] * gAfterImageLayers[iSet - 1].pInfo[4]);
                                    iInv = 100 - iPercent;
                                    gpkgtCurrentEngineObject->iColorRed = ((char)gAfterImageLayers[iSet - 1].pInfo[7] * iInv + iPercent * iSavedR) / 100;
                                    gpkgtCurrentEngineObject->iColorGreen = ((char)gAfterImageLayers[iSet - 1].pInfo[8] * iInv + iPercent * iSavedG) / 100;
                                    gpkgtCurrentEngineObject->iColorBlue = ((char)gAfterImageLayers[iSet - 1].pInfo[9] * iInv + iPercent * iSavedB) / 100;
                                    gpkgtCurrentEngineObject->iColorAlpha = ((char)gAfterImageLayers[iSet - 1].pInfo[10] * iInv + iPercent * iSavedAOrRed) / 100;
                                    break;
                                case 4:
                                    iPercent = rand() % 100;
                                    gpkgtCurrentEngineObject->iColorRed = ((char)gAfterImageLayers[iSet - 1].pInfo[7] * iPercent + (100 - iPercent) * iSavedR) / 100;
                                    gpkgtCurrentEngineObject->iColorGreen = ((char)gAfterImageLayers[iSet - 1].pInfo[8] * iPercent + (100 - iPercent) * iSavedG) / 100;
                                    gpkgtCurrentEngineObject->iColorBlue = ((char)gAfterImageLayers[iSet - 1].pInfo[9] * iPercent + (100 - iPercent) * iSavedB) / 100;
                                    gpkgtCurrentEngineObject->iColorAlpha = ((char)gAfterImageLayers[iSet - 1].pInfo[10] * iPercent + (100 - iPercent) * iSavedAOrRed) / 100;
                                    if (0) goto boundary25; boundary25:;
                                    if (0) goto boundary26; boundary26:;
                                    break;
                                }
                                pLayerTintDst = gwTintedPalette16;
                                pLayerPalEntry = pPalette;
                                for (k = 256; k != 0; k--) {
                                    if (*pLayerPalEntry & 0xffffff) {
                                        if (0) goto boundary27; boundary27:;
                                        if (0) goto boundary28; boundary28:;
                                        iLayerRed = (GetBValue(*pLayerPalEntry) >> 3) + gpkgtCurrentEngineObject->iColorRed;
                                        if (iLayerRed > 31)
                                            iLayerRed = 31;
                                        else if (iLayerRed < 0)
                                            iLayerRed = 0;
                                        iGreen = ((BYTE)(*pLayerPalEntry >> 8) >> 3) + gpkgtCurrentEngineObject->iColorGreen;
                                        if (iGreen > 31)
                                            iGreen = 31;
                                        else if (iGreen < 0)
                                            iGreen = 0;
                                        iBlue = (GetRValue(*pLayerPalEntry) >> 3) + gpkgtCurrentEngineObject->iColorBlue;
                                        if (iBlue > 31)
                                            iBlue = 31;
                                        else if (iBlue < 0)
                                            iBlue = 0;
                                        if (iLayerRed + iGreen + iBlue == 0)
                                            iBlue = 1;
                                        if (giScreenMode)
                                            *pLayerTintDst = (WORD)((iLayerRed * 32 + iGreen) * 64 + iBlue);
                                        else
                                            *pLayerTintDst = (WORD)((iLayerRed * 32 + iGreen) * 32 + iBlue);
                                    } else {
                                        *pLayerTintDst = 0;
                                    }
                                    pLayerTintDst++;
                                    if (0) goto boundary29; boundary29:;
                                    if (0) goto boundary30; boundary30:;
                                    pLayerPalEntry++;
                                }
                                switch (gpkgtCurrentEngineObject->iObjectType) {
                                case PLAYER_ENGINE_OBJECT:
                                case STORY_ENGINE_OBJECT:
                                case CHARACTER_ENGINE_OBJECT:
                                    if (pAI[(iSlot + 1) * 4 + 2] & 4) {
                                        iX = ((DWORD)pLayerHeader->iWidth >> 1) - pLayerStep->shX + pAI[(iSlot + 1) * 4 + 0] / 0x10000 - pLayerHeader->iWidth;
                                        if (0) goto boundary31; boundary31:;
                                        bLayerFlip = bLayerFlip ^ 1;
                                    } else {
                                        iX = pLayerStep->shX - ((DWORD)pLayerHeader->iWidth >> 1) + pAI[(iSlot + 1) * 4 + 0] / 0x10000;
                                    }
                                    iY = pLayerStep->shY;
                                    if (0) goto boundary32; boundary32:;
                                    iY += pAI[(iSlot + 1) * 4 + 1] / 0x10000;
                                    if (0) goto boundary33; boundary33:;
                                    iY -= pLayerHeader->iHeight;
                                    if (gpkgtCurrentEngineObject->iFlags & 0x40000000) {
                                        iX -= giCameraX;
                                        iY -= giCameraY;
                                    }
                                    iX += giShakeXOffset;
                                    iY += giShakeYOffset;
                                    break;
                                case STAGE_ENGINE_OBJECT:
                                    if (pAI[(iSlot + 1) * 4 + 2] & 4) {
                                        iX = pAI[(iSlot + 1) * 4 + 0] / 0x10000 - pLayerStep->shX - pLayerHeader->iWidth;
                                        bLayerFlip ^= 1;
                                    } else {
                                        if (0) goto boundary34; boundary34:;
                                        iX = pAI[(iSlot + 1) * 4 + 0] / 0x10000 + pLayerStep->shX;
                                    }
                                    if (0) goto boundary35; boundary35:;
                                    iY = pAI[(iSlot + 1) * 4 + 1] / 0x10000 + pLayerStep->shY;
                                    if (gpkgtCurrentEngineObject->iFlags & 0x40000000) {
                                        iX -= giCameraX;
                                        iY -= giCameraY;
                                    }
                                    if (0) goto boundary36; boundary36:;
                                    iX += giShakeXOffset;
                                    iY = iY + giShakeYOffset;
                                    pScroll = (kgtSkillStageScroll *)&pChar->kgtCore.pSkillScriptsAlloc[gpkgtCurrentEngineObject->iDsGuardedSkillIdx];
                                    if (pScroll->cFlags & 8) {
                                        iX += pScroll->shScrollX * giCameraX / -100;
                                        if (pScroll->shScrollX < 0)
                                            iX += -abs(pScroll->shScrollX) * 64 / 10;
                                    }
                                    if (pScroll->cFlags & 0x10) {
                                        if (0) goto boundary37; boundary37:;
                                        iY += pScroll->shScrollY * giCameraY / -100;
                                        if (pScroll->shScrollY < 0)
                                            iY += -abs(pScroll->shScrollY) * 48 / 10;
                                    }
                                    break;
                                case SYSTEM_ENGINE_OBJECT:
                                case DEMO_ENGINE_OBJECT:
                                    if (pAI[(iSlot + 1) * 4 + 2] & 4) {
                                        iX = pAI[(iSlot + 1) * 4 + 0] / 0x10000 - pLayerStep->shX - pLayerHeader->iWidth;
                                        bLayerFlip = bLayerFlip ^ 1;
                                    } else {
                                        iX = pAI[(iSlot + 1) * 4 + 0] / 0x10000 + pLayerStep->shX;
                                    }
                                    iY = pAI[(iSlot + 1) * 4 + 1] / 0x10000 + pLayerStep->shY;
                                    if (gpkgtCurrentEngineObject->iFlags & 0x40000000) {
                                        iX -= giCameraX;
                                        iY = iY - giCameraY;
                                    }
                                    if (0) goto boundary38; boundary38:;
                                    iX += giShakeXOffset;
                                    iY += giShakeYOffset;
                                    break;
                                }
                                vDrawKgtImage16(pLayerHeader, pPixels, gwTintedPalette16, iX, iY, iLayerW, iLayerH, (iLayerImage & 0xc000) + bLayerFlip);
                            }
                            pPrevHeader = pLayerHeader;
                        }
                    }
                    if (0) goto boundary39; boundary39:;
                    gpkgtCurrentEngineObject->iColorBlendtype = iSavedBlend;
                    gpkgtCurrentEngineObject->iColorRed = iSavedR;
                    gpkgtCurrentEngineObject->iColorGreen = iSavedG;
                    gpkgtCurrentEngineObject->iColorBlue = iSavedB;
                    gpkgtCurrentEngineObject->iColorAlpha = iSavedAOrRed;
                }
            }
    
            /* the object's own image */
            if (pHeader->iSize) {
                iKgtDecompress((BYTE *)gpGlobalMemoryAlloc, (BYTE *)pHeader->pAlloc, pHeader->iSize);
                if (pHeader->iFlags & 1) {
                    pPalette = (DWORD *)gpGlobalMemoryAlloc;
                    pPixels = (BYTE *)gpGlobalMemoryAlloc + 0x400;
                } else {
                    pPixels = (BYTE *)gpGlobalMemoryAlloc;
                }
            } else {
                if (0) goto boundary40; boundary40:;
                pPixels = (BYTE *)pHeader->pAlloc;
                if (pHeader->iFlags & 1) {
                    if (0) goto boundary41; boundary41:;
                    if (0) goto boundary42; boundary42:;
                    pPalette = (DWORD *)pPixels;
                    pPixels = pPixels + 0x400;
                }
            }
            if (0) goto boundary43; boundary43:;
            if (0) goto boundary44; boundary44:;
            switch (gpkgtCurrentEngineObject->iObjectType) {
            case PLAYER_ENGINE_OBJECT:
            case STORY_ENGINE_OBJECT:
            case CHARACTER_ENGINE_OBJECT:
                if (0) goto boundary45; boundary45:;
                if (0) goto boundary46; boundary46:;
                vApplyObjectColorTransition((int *)&gkgtLoadedCharacter[gpkgtCurrentEngineObject->iPlayerIdx].cFlashType);
                break;
            case SYSTEM_ENGINE_OBJECT:
                vApplyObjectColorTransition(&giSystemFlashType);
                break;
            case STAGE_ENGINE_OBJECT:
                vApplyObjectColorTransition(&giStageFlashType);
                if (0) goto boundary47; boundary47:;
                if (0) goto boundary48; boundary48:;
                if (0) goto boundary49; boundary49:;
                if (0) goto boundary50; boundary50:;
                if (0) goto boundary51; boundary51:;
                if (0) goto boundary52; boundary52:;
                break;
            }
            pPalEntry = pPalette;
            if (0) goto boundary53; boundary53:;
            if (0) goto boundary54; boundary54:;
            if (0) goto boundary55; boundary55:;
            if (0) goto boundary56; boundary56:;
            if (0) goto boundary57; boundary57:;
            pTintDst = gwTintedPalette16;
            if (0) goto boundary58; boundary58:;
            for (k = 256; k != 0; k--) {
                if (0) goto boundary59; boundary59:;
                if (*pPalEntry & 0xffffff) {
                    if (0) goto boundary60; boundary60:;
                    iSavedAOrRed = (GetBValue(*pPalEntry) >> 3) + gpkgtCurrentEngineObject->iColorRed;
                    if (iSavedAOrRed > 31) {
                        iSavedAOrRed = 31;
                        iGreen = ((BYTE)(*pPalEntry >> 8) >> 3) + gpkgtCurrentEngineObject->iColorGreen;
                    } else {
                        if (iSavedAOrRed < 0)
                            iSavedAOrRed = 0;
                        iGreen = ((BYTE)(*pPalEntry >> 8) >> 3) + gpkgtCurrentEngineObject->iColorGreen;
                    }
                    if (iGreen > 31)
                        iGreen = 31;
                    else if (iGreen < 0)
                        iGreen = 0;
                    iBlue = (GetRValue(*pPalEntry) >> 3) + gpkgtCurrentEngineObject->iColorBlue;
                    if (iBlue > 31)
                        iBlue = 31;
                    else if (iBlue < 0)
                        iBlue = 0;
                    if (iSavedAOrRed + iGreen + iBlue == 0)
                        iBlue = 1;
                    if (giScreenMode)
                        *pTintDst = (WORD)((iSavedAOrRed * 32 + iGreen) * 64 + iBlue);
                    else
                        *pTintDst = (WORD)((iSavedAOrRed * 32 + iGreen) * 32 + iBlue);
                } else {
                    *pTintDst = 0;
                }
                if (0) goto boundary61; boundary61:;
                if (0) goto boundary62; boundary62:;
                pTintDst++;
                pPalEntry++;
            }
    
            switch (gpkgtCurrentEngineObject->iObjectType) {
            case PLAYER_ENGINE_OBJECT:
            case STORY_ENGINE_OBJECT:
            case CHARACTER_ENGINE_OBJECT:
                if (0) goto boundary63; boundary63:;
                if (gpkgtCurrentEngineObject->iOpponentDowntimeInFrames == -1) {
                    /* held by the opponent: drawn with the opponent's orientation */
                    bHeldFlip = pChar->pLastOpponent->iPlayerLookingRight;
                    if (0) goto boundary64; boundary64:;
                    wImage = pStep->wImage;
                    if (0) goto boundary65; boundary65:;
                    if (wImage & 0x4000)
                        bHeldFlip = bHeldFlip ^ 1;
                    if (bHeldFlip) {
                        iX = pStep->shX;
                        if (0) goto boundary66; boundary66:;
                        k = iX;
                        if (0) goto boundary67; boundary67:;
                        iX = gpkgtCurrentEngineObject->iPosX / 0x10000;
                        if (0) goto boundary68; boundary68:;
                        iX -= k;
                        iX -= pHeader->iWidth;
                    } else
                        iX = gpkgtCurrentEngineObject->iPosX / 0x10000 + pStep->shX;
                    if (wImage & 0x8000)
                    {
                        iY = pStep->shY;
                        if (0) goto boundary69; boundary69:;
                        iY = gpkgtCurrentEngineObject->iPosY / 0x10000 - iY;
                        if (0) goto boundary70; boundary70:;
                        iY -= pHeader->iHeight;
                    }
                    else
                        iY = gpkgtCurrentEngineObject->iPosY / 0x10000 + pStep->shY;
                    if (gpkgtCurrentEngineObject->iFlags & 0x40000000) {
                        iX -= giCameraX;
                        if (0) goto boundary71; boundary71:;
                        iY = iY - giCameraY;
                    }
                    iY += giShakeYOffset;
                    uFlags = (wImage & 0xc000) | bHeldFlip;
                    iX += giShakeXOffset;
                    vDrawKgtImage16(pHeader, pPixels, gwTintedPalette16, iX, iY, iImgW, iImgH, uFlags);
                    if (0) goto boundary72; boundary72:;
                    if (0) goto boundary73; boundary73:;
                    ;
                }
                if (gpkgtCurrentEngineObject->iPlayerLookingRight && !(pStep->cFlags & 1)) {
                    iX = ((DWORD)pHeader->iWidth >> 1) - pStep->shX + gpkgtCurrentEngineObject->iPosX / 0x10000 - pHeader->iWidth;
                    if (0) goto boundary74; boundary74:;
                    bFlip = bFlip ^ 1;
                } else {
                    iX = pStep->shX - ((DWORD)pHeader->iWidth >> 1) + gpkgtCurrentEngineObject->iPosX / 0x10000;
                }
                iY = pStep->shY;
                if (0) goto boundary75; boundary75:;
                iY += gpkgtCurrentEngineObject->iPosY / 0x10000;
                if (0) goto boundary76; boundary76:;
                iY -= pHeader->iHeight;
                if (gpkgtCurrentEngineObject->iFlags & 0x40000000) {
                    if (0) goto boundary77; boundary77:;
                    if (0) goto boundary78; boundary78:;
                    if (0) goto boundary79; boundary79:;
                    iX -= giShakeXOffset + giCameraX;
                    iY -= giShakeYOffset + giCameraY;
                }
                if (0) goto boundary80; boundary80:;
                bHitJudge = giHitJudge;
                if (bHitJudge) {
                    k = 19;
                    if (0) goto boundary81; boundary81:;
                    if (0) goto boundary82; boundary82:;
                    if (0) goto boundary83; boundary83:;
                    do {
                        vDrawHitboxFilled((BYTE *)gpkgtCurrentEngineObject->pGuardBoxes[k], guHitboxColors[1], bFlip);
                    } while (k--);
                    if (0) goto boundary84; boundary84:;
                    if (0) goto boundary85; boundary85:;
                    k = 19;
                    do {
                        if (0) goto boundary86; boundary86:;
                        vDrawHitboxFilled((BYTE *)gpkgtCurrentEngineObject->pAttackBoxes[k], guHitboxColors[0], bFlip);
                    } while (k--);
                }
                if (0) goto boundary87; boundary87:;
                vDrawKgtImage16(pHeader, pPixels, gwTintedPalette16, iX, iY, iImgW, iImgH, (pStep->wImage & 0xc000) + bFlip);
                if (0) goto boundary88; boundary88:;
                if (0) goto boundary89; boundary89:;
                if (0) goto boundary90; boundary90:;
                if (0) goto boundary91; boundary91:;
                if (!bHitJudge)
                    ;
                if (0) goto boundary92; boundary92:;
                k = 19;
                do {
                    vDrawHitboxOutline((BYTE *)gpkgtCurrentEngineObject->pGuardBoxes[k], guHitboxColors[1], bFlip);
                } while (k--);
                k = 19;
                do {
                    if (0) goto boundary93; boundary93:;
                    vDrawHitboxOutline((BYTE *)gpkgtCurrentEngineObject->pAttackBoxes[k], guHitboxColors[0], bFlip);
                } while (k--);
                if (gpkgtCurrentEngineObject->iObjectType == PLAYER_ENGINE_OBJECT) {
                    vBlitImageRect16(&gkgtBitmaps[1],
                                     gpkgtCurrentEngineObject->iPosX / 0x10000 - giCameraX - 0x20,
                                     gpkgtCurrentEngineObject->iPosY / 0x10000 - giCameraY - 0x40,
                                     0x40, 0x20, (((int)gpkgtCurrentEngineObject->pWork015E >> 2 & 3) + 4) * 0x40, 0x60, 0, 0, 0, 0);
                    vBlitImageRect16(&gkgtBitmaps[1],
                                     gpkgtCurrentEngineObject->iPosX / 0x10000 - giCameraX - 0x20,
                                     gpkgtCurrentEngineObject->iPosY / 0x10000 - giCameraY - 0x20,
                                     0x40, 0x20, (((int)gpkgtCurrentEngineObject->pWork015E & 3) + 4) * 0x40, 0x40, 0, 0, 0, 0);
                }
                ;
            case STAGE_ENGINE_OBJECT:
                if (0) goto boundary94; boundary94:;
                if (gpkgtCurrentEngineObject->iPlayerLookingRight) {
                    iX = pStep->shX;
                    if (0) goto boundary95; boundary95:;
                    iX = gpkgtCurrentEngineObject->iPosX / 0x10000 - iX;
                    if (0) goto boundary96; boundary96:;
                    iX -= pHeader->iWidth;
                    if (0) goto boundary97; boundary97:;
                    bFlip = bFlip ^ 1;
                } else {
                    iX = gpkgtCurrentEngineObject->iPosX / 0x10000 + pStep->shX;
                }
                iX += giShakeXOffset;
                if (0) goto boundary98; boundary98:;
                iY = pStep->shY + giShakeYOffset + gpkgtCurrentEngineObject->iPosY / 0x10000;
                pScroll = (kgtSkillStageScroll *)&pChar->kgtCore.pSkillScriptsAlloc[gpkgtCurrentEngineObject->iDsGuardedSkillIdx];
                if (pScroll->cFlags & 8) {
                    iX += pScroll->shScrollX * giCameraX / -100;
                    if (pScroll->shScrollX < 0)
                        iX += -abs(pScroll->shScrollX) * 64 / 10;
                }
                cScrollFlags = pScroll->cFlags;
                if (0) goto boundary99; boundary99:;
                if (cScrollFlags & 0x10) {
                    iY += pScroll->shScrollY * giCameraY / -100;
                    if (pScroll->shScrollY < 0)
                        iY += -abs(pScroll->shScrollY) * 48 / 10;
                }
                if (cScrollFlags & 2) {
                    if (0) goto boundary100; boundary100:;
                    if (cScrollFlags & 4) {
                        /* tiled in both directions */
                        while (iX > 0)
                            iX -= iImgW;
                        if (0) goto boundary101; boundary101:;
                        while (iY > 0)
                            iY -= iImgH;
                        for (iRow = iY; iRow < 480; iRow += iImgH)
                            for (iCol = iX; iCol < 640; iCol += iImgW)
                                vDrawKgtImage16(pHeader, pPixels, gwTintedPalette16, iCol, iRow, iImgW, iImgH, (pStep->wImage & 0xc000) + bFlip);
                        ;
                    }
                    while (iX > 0)
                        iX -= iImgW;
                    for (; iX < 640; iX += iImgW)
                        vDrawKgtImage16(pHeader, pPixels, gwTintedPalette16, iX, iY, iImgW, iImgH, (pStep->wImage & 0xc000) + bFlip);
                    if (0) goto boundary102; boundary102:;
                    ;
                }
                if (cScrollFlags & 4) {
                    if (0) goto boundary103; boundary103:;
                    while (iY > 0)
                        iY = iY - iImgH;
                    if (0) goto boundary104; boundary104:;
                    for (; iY < 480; iY += iImgH)
                        vDrawKgtImage16(pHeader, pPixels, gwTintedPalette16, iX, iY, iImgW, iImgH, (pStep->wImage & 0xc000) + bFlip);
                    ;
                }
                vDrawKgtImage16(pHeader, pPixels, gwTintedPalette16, iX, iY, iImgW, iImgH, (pStep->wImage & 0xc000) + bFlip);
                if (0) goto boundary105; boundary105:;
                ;
            case SYSTEM_ENGINE_OBJECT:
            case DEMO_ENGINE_OBJECT:
                if (gpkgtCurrentEngineObject->iPlayerLookingRight) {
                    iX = pStep->shX;
                    if (0) goto boundary106; boundary106:;
                    iX = gpkgtCurrentEngineObject->iPosX / 0x10000 - iX;
                    if (0) goto boundary107; boundary107:;
                    iX -= pHeader->iWidth;
                    bFlip ^= 1;
                } else {
                    if (0) goto boundary108; boundary108:;
                    iX = gpkgtCurrentEngineObject->iPosX / 0x10000 + pStep->shX;
                }
                iY = gpkgtCurrentEngineObject->iPosY / 0x10000 + pStep->shY;
                if (gpkgtCurrentEngineObject->iFlags & 0x40000000) {
                    if (0) goto boundary109; boundary109:;
                    iX -= giShakeXOffset + giCameraX;
                    iY -= giShakeYOffset + giCameraY;
                }
                if (0) goto boundary110; boundary110:;
                if (0) goto boundary111; boundary111:;
                /* gauges are cut according to the value they show */
                switch (gpkgtCurrentEngineObject->iPlayerIdx) {
                case 10:    /* 1P life */
                    if (gkgtLoadedCharacter[0].iLifeMax) {
                        iDrawW = gkgtLoadedCharacter[0].iHealth * iImgW / gkgtLoadedCharacter[0].iLifeMax;
                        pPixels = pPixels + (iImgW - iDrawW);
                        iX += iImgW - iDrawW;
                        iImgW = iDrawW;
                    }
                    if (0) goto boundary112; boundary112:;
                    break;
                case 11:    /* 2P life */
                    iPlayer = 1;
                    if (0) goto boundary113; boundary113:;
                    if (0) goto boundary114; boundary114:;
                    if (gkgtGameState.kgtGameMode == GAME_MODE_STORY) {
                        iPlayer = gkgtGameState.iTargetPlayer;
                        if (0) goto boundary115; boundary115:;
                        if (0) goto boundary116; boundary116:;
                        if (0) goto boundary117; boundary117:;
                        if (0) goto boundary118; boundary118:;
                        if (iPlayer == -1)
                            ;
                    }
                    if (gkgtLoadedCharacter[iPlayer].iLifeMax)
                        iImgW = gkgtLoadedCharacter[iPlayer].iHealth * iImgW / gkgtLoadedCharacter[iPlayer].iLifeMax;
                    break;
                case 20:    /* 1P special gauge */
                    if (gkgtLoadedCharacter[0].iSpecialMax) {
                        iDrawW = gkgtLoadedCharacter[0].iSpecialGauge * iImgW / gkgtLoadedCharacter[0].iSpecialMax;
                        pPixels = pPixels + (iImgW - iDrawW);
                        if (0) goto boundary119; boundary119:;
                        if (0) goto boundary120; boundary120:;
                        iX += iImgW - iDrawW;
                        iImgW = iDrawW;
                    }
                    break;
                case 21:    /* 2P special gauge */
                    iPlayer = 1;
                    if (0) goto boundary121; boundary121:;
                    if (gkgtGameState.kgtGameMode == GAME_MODE_STORY) {
                        iPlayer = gkgtGameState.iTargetPlayer;
                        if (iPlayer == -1)
                            ;
                    }
                    if (gkgtLoadedCharacter[iPlayer].iSpecialMax)
                        iImgW = gkgtLoadedCharacter[iPlayer].iSpecialGauge * iImgW / gkgtLoadedCharacter[iPlayer].iSpecialMax;
                    break;
                }
                if (0) goto boundary122; boundary122:;
                uFlags = (pStep->wImage & 0xc000) + bFlip;
                break;
            default:
                if (0) goto boundary123; boundary123:;
                if (0) goto boundary124; boundary124:;
                ;
            }
            vDrawKgtImage16(pHeader, pPixels, gwTintedPalette16, iX, iY, iImgW, iImgH, uFlags);
            if (0) goto boundary125; boundary125:;
            ;
        }
    
        if (gpkgtCurrentEngineObject->iDrawFlag > 0) {
            /* one of the built-in bitmaps */
            pBmp = &gkgtBitmaps[gpkgtCurrentEngineObject->iDrawFlag];
            if (pBmp->pData == NULL)
                ;
            if (pBmp->iCompressedSize)
                iKgtDecompress((BYTE *)gpGlobalMemoryAlloc, (BYTE *)pBmp->pData, pBmp->iCompressedSize);
            vBlitImageRect16(pBmp, gpkgtCurrentEngineObject->iPosX / 0x10000, gpkgtCurrentEngineObject->iPosY / 0x10000,
                             pBmp->iWidth, pBmp->iHeight, 0, 0, gpkgtCurrentEngineObject->iColorBlendtype,
                             gpkgtCurrentEngineObject->iColorRed, gpkgtCurrentEngineObject->iColorGreen, gpkgtCurrentEngineObject->iColorBlue);
            ;
        }
    
        switch (gpkgtCurrentEngineObject->iDrawFlag) {
        case -2:    /* shadows of the script objects */
            pObj = gkgtEngineObjects;
            for (k = 1024; k != 0; k--, pObj++) {
                if (0) goto boundary126; boundary126:;
                if (pObj->iJumpIdx == READ_SCRIPT && pObj->iDrawFlag == -1 && (pObj->iFlags & 0x80000000)) {
                    gpkgtCurrentEngineObject->iColorRed = gpkgtCurrentEngineObject->iColorGreen = gpkgtCurrentEngineObject->iColorBlue =
                        (pObj->iPosY - pObj->iGroundY) / 0x100000;
                    vBlitImageRect16(&gkgtBitmaps[1],
                                     pObj->iPosX / 0x10000 - giCameraX - 0x38,
                                     pObj->iGroundY / 0x10000 - giCameraY - 0x10,
                                     0x80, 0x20, 0, 0x60, 3,
                                     gpkgtCurrentEngineObject->iColorRed, gpkgtCurrentEngineObject->iColorGreen, gpkgtCurrentEngineObject->iColorBlue);
                }
            }
            break;
        case -3:
            vBlitImageRect16(&gkgtBitmaps[1],
                             gpkgtCurrentEngineObject->iPosX - giCameraX - 0x40,
                             gpkgtCurrentEngineObject->iPosY - giCameraY - 0x40,
                             0x80, 0x80, gpkgtCurrentEngineObject->iPlayerIdx / 8 * 0x80, gpkgtCurrentEngineObject->iObjectType * 0x80,
                             gpkgtCurrentEngineObject->iPlayerLookingRight * 0x40000000 + 2,
                             gpkgtCurrentEngineObject->iColorRed, gpkgtCurrentEngineObject->iColorGreen, gpkgtCurrentEngineObject->iColorBlue);
            break;
        case -4:
            vBlitImageRect16(&gkgtBitmaps[1],
                             gpkgtCurrentEngineObject->iPosX / 0x10000, gpkgtCurrentEngineObject->iPosY / 0x10000,
                             0x200, 0x200, 0, 0, 4,
                             gpkgtCurrentEngineObject->iColorRed, gpkgtCurrentEngineObject->iColorGreen, gpkgtCurrentEngineObject->iColorBlue);
            break;
        case -5:
            if (0) goto boundary127; boundary127:;
            if (0) goto boundary128; boundary128:;
            vDrawNumberSmall(gpkgtCurrentEngineObject->iPlayerIdx, gpkgtCurrentEngineObject->iPosX, gpkgtCurrentEngineObject->iPosY, 0,
                             gpkgtCurrentEngineObject->iColorRed, gpkgtCurrentEngineObject->iColorGreen, gpkgtCurrentEngineObject->iColorBlue);
            break;
        case -6:
            vDrawNumberLarge(gpkgtCurrentEngineObject->iPlayerIdx, gpkgtCurrentEngineObject->iPosX - giCameraX, gpkgtCurrentEngineObject->iPosY - giCameraY, 4,
                             gpkgtCurrentEngineObject->iColorRed, gpkgtCurrentEngineObject->iColorGreen, gpkgtCurrentEngineObject->iColorBlue);
            vBlitImageRect16(&gkgtBitmaps[1],
                             gpkgtCurrentEngineObject->iPosX - giCameraX + 0x20, gpkgtCurrentEngineObject->iPosY - giCameraY,
                             0x60, 0x20, 0, 0x40, 4,
                             gpkgtCurrentEngineObject->iColorRed, gpkgtCurrentEngineObject->iColorGreen, gpkgtCurrentEngineObject->iColorBlue);
            break;
        case -7:    /* "round N" */
            vBlitImageRect16(&gkgtBitmaps[1],
                             gpkgtCurrentEngineObject->iPosX - 0xc0, gpkgtCurrentEngineObject->iPosY,
                             0x100, 0x40, 0, 0x240, 0,
                             gpkgtCurrentEngineObject->iColorRed, gpkgtCurrentEngineObject->iColorGreen, gpkgtCurrentEngineObject->iColorBlue);
            if ((int)gkgtGameState.iCurrentRound / 10)
                vBlitImageRect16(&gkgtBitmaps[1],
                                 gpkgtCurrentEngineObject->iPosX + 0x40, gpkgtCurrentEngineObject->iPosY,
                                 0x40, 0x40, (int)gkgtGameState.iCurrentRound / 10 * 0x40, 0x200, 0,
                                 gpkgtCurrentEngineObject->iColorRed, gpkgtCurrentEngineObject->iColorGreen, gpkgtCurrentEngineObject->iColorBlue);
            if (0) goto boundary129; boundary129:;
            vBlitImageRect16(&gkgtBitmaps[1],
                             gpkgtCurrentEngineObject->iPosX + 0x80, gpkgtCurrentEngineObject->iPosY,
                             0x40, 0x40, (int)gkgtGameState.iCurrentRound % 10 * 0x40, 0x200, 0,
                             gpkgtCurrentEngineObject->iColorRed, gpkgtCurrentEngineObject->iColorGreen, gpkgtCurrentEngineObject->iColorBlue);
            if (0) goto boundary130; boundary130:;
            if (0) goto boundary131; boundary131:;
            vBlitImageRect16(&gkgtBitmaps[1],
                             gpkgtCurrentEngineObject->iPosX - 0x80, gpkgtCurrentEngineObject->iPosY + 0x40,
                             0x100, 0x5c, 0x100, 0x240, 0,
                             gpkgtCurrentEngineObject->iColorRed, gpkgtCurrentEngineObject->iColorGreen, gpkgtCurrentEngineObject->iColorBlue);
            break;
        case -8:
            vBlitImageRect16(&gkgtBitmaps[1],
                             gpkgtCurrentEngineObject->iPosX - 0x100, gpkgtCurrentEngineObject->iPosY,
                             0xc0, 0x40, 0x200, 0x240, 0,
                             gpkgtCurrentEngineObject->iColorRed, gpkgtCurrentEngineObject->iColorGreen, gpkgtCurrentEngineObject->iColorBlue);
            if (0) goto boundary132; boundary132:;
            pBlurTL = 0; pBlurTR = 0; pBlurBL = 0; pBlurBR = 0;
            break;
        case -10:   /* blur the whole screen iPlayerIdx / 20 times */
            iBlurPasses = gpkgtCurrentEngineObject->iPlayerIdx / 20;
            for (pBlurTL = gpFrameBits; iBlurPasses--; pBlurTL = gpFrameBits) {
                pBlurTR = pBlurTL + 1;
                if (0) goto boundary133; boundary133:;
                pBlurBL = pBlurTL + 640;
                pBlurBR = pBlurTL + 641;
                if (giScreenMode) {
                    for (iRow = 478; iRow != 0; iRow--)
                        for (iBlurCol = 638; iBlurCol != 0; iBlurCol--) {
                            if (0) goto boundary134; boundary134:;
                            *pBlurTL = ((*pBlurBR & 0xe79c) >> 2) + ((*pBlurBL & 0xe79c) >> 2) + ((*pBlurTR & 0xe79c) >> 2) + ((*pBlurTL & 0xe79c) >> 2);
                            if (0) goto boundary135; boundary135:;
                            pBlurTL++;
                            pBlurTR++;
                            pBlurBL++;
                            pBlurBR++;
                        }
                } else {
                    for (iRow = 478; iRow != 0; iRow--)
                        for (iBlurCol = 638; iBlurCol != 0; iBlurCol--) {
                            *pBlurTL = ((*pBlurBR & 0x739c) >> 2) + ((*pBlurBL & 0x739c) >> 2) + ((*pBlurTR & 0x739c) >> 2) + ((*pBlurTL & 0x739c) >> 2);
                            pBlurTL++;
                            pBlurTR++;
                            pBlurBL++;
                            pBlurBR++;
                        }
                }
            }
            break;
        }
    }
    __asm {
        _emit 0x8B        ; 0040CC30  mov ecx, dword ptr [0x4cfa00]
        _emit 0x0D
        _emit 0x00
        _emit 0xFA
        _emit 0x4C
        _emit 0x00
        _emit 0x83        ; 0040CC36  sub esp, 0x6c
        _emit 0xEC
        _emit 0x6C
        _emit 0x8B        ; 0040CC39  mov eax, dword ptr [ecx + 0x10]
        _emit 0x41
        _emit 0x10
        _emit 0x53        ; 0040CC3C  push ebx
        _emit 0x55        ; 0040CC3D  push ebp
        _emit 0x56        ; 0040CC3E  push esi
        _emit 0x83        ; 0040CC3F  cmp eax, -1
        _emit 0xF8
        _emit 0xFF
        _emit 0x57        ; 0040CC42  push edi
        _emit 0x0F        ; 0040CC43  jne 0x40de9c
        _emit 0x85
        _emit 0x53
        _emit 0x12
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040CC49  mov edi, dword ptr [ecx + 0x15a]
        _emit 0xB9
        _emit 0x5A
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x83        ; 0040CC4F  cmp edi, 5
        _emit 0xFF
        _emit 0x05
        _emit 0x77        ; 0040CC52  ja 0x40cc8e
        _emit 0x3A
        _emit 0xFF        ; 0040CC54  jmp dword ptr [edi*4 + 0x40e3e8]
        _emit 0x24
        _emit 0xBD
        _emit 0xE8
        _emit 0xE3
        _emit 0x40
        _emit 0x00
        _emit 0xC7        ; 0040CC5B  mov dword ptr [esp + 0x24], 0x433240
        _emit 0x44
        _emit 0x24
        _emit 0x24
        _emit 0x40
        _emit 0x32
        _emit 0x43
        _emit 0x00
        _emit 0xEB        ; 0040CC63  jmp 0x40cc8e
        _emit 0x29
        _emit 0xC7        ; 0040CC65  mov dword ptr [esp + 0x24], 0x425a60
        _emit 0x44
        _emit 0x24
        _emit 0x24
        _emit 0x60
        _emit 0x5A
        _emit 0x42
        _emit 0x00
        _emit 0xEB        ; 0040CC6D  jmp 0x40cc8e
        _emit 0x1F
        _emit 0xC7        ; 0040CC6F  mov dword ptr [esp + 0x24], 0x445740
        _emit 0x44
        _emit 0x24
        _emit 0x24
        _emit 0x40
        _emit 0x57
        _emit 0x44
        _emit 0x00
        _emit 0xEB        ; 0040CC77  jmp 0x40cc8e
        _emit 0x15
        _emit 0x8B        ; 0040CC79  mov eax, dword ptr [ecx + 0x156]
        _emit 0x81
        _emit 0x56
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x69        ; 0040CC7F  imul eax, eax, 0xe03f
        _emit 0xC0
        _emit 0x3F
        _emit 0xE0
        _emit 0x00
        _emit 0x00
        _emit 0x05        ; 0040CC85  add eax, 0x4d1d80
        _emit 0x80
        _emit 0x1D
        _emit 0x4D
        _emit 0x00
        _emit 0x89        ; 0040CC8A  mov dword ptr [esp + 0x24], eax
        _emit 0x44
        _emit 0x24
        _emit 0x24
        _emit 0x8B        ; 0040CC8E  mov ebx, dword ptr [esp + 0x24]
        _emit 0x5C
        _emit 0x24
        _emit 0x24
        _emit 0x8B        ; 0040CC92  mov eax, dword ptr [ecx + 0x2c]
        _emit 0x41
        _emit 0x2C
        _emit 0xC1        ; 0040CC95  shl eax, 4
        _emit 0xE0
        _emit 0x04
        _emit 0x8B        ; 0040CC98  mov edx, dword ptr [ebx + 0x114]
        _emit 0x93
        _emit 0x14
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040CC9E  mov esi, dword ptr [ebx + 0x118]
        _emit 0xB3
        _emit 0x18
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x8D        ; 0040CCA4  lea edx, [eax + edx - 0x10]
        _emit 0x54
        _emit 0x10
        _emit 0xF0
        _emit 0x33        ; 0040CCA8  xor eax, eax
        _emit 0xC0
        _emit 0x89        ; 0040CCAA  mov dword ptr [esp + 0x20], edx
        _emit 0x54
        _emit 0x24
        _emit 0x20
        _emit 0x66        ; 0040CCAE  mov ax, word ptr [edx + 3]
        _emit 0x8B
        _emit 0x42
        _emit 0x03
        _emit 0x8B        ; 0040CCB2  mov edx, eax
        _emit 0xD0
        _emit 0x81        ; 0040CCB4  and edx, 0x1fff
        _emit 0xE2
        _emit 0xFF
        _emit 0x1F
        _emit 0x00
        _emit 0x00
        _emit 0x8D        ; 0040CCBA  lea edx, [edx + edx*4]
        _emit 0x14
        _emit 0x92
        _emit 0x8D        ; 0040CCBD  lea esi, [esi + edx*4]
        _emit 0x34
        _emit 0x96
        _emit 0x89        ; 0040CCC0  mov dword ptr [esp + 0x18], esi
        _emit 0x74
        _emit 0x24
        _emit 0x18
        _emit 0x83        ; 0040CCC4  cmp dword ptr [esi], 0
        _emit 0x3E
        _emit 0x00
        _emit 0x0F        ; 0040CCC7  je 0x40e3e0
        _emit 0x84
        _emit 0x13
        _emit 0x17
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040CCCD  mov ebp, dword ptr [esi + 4]
        _emit 0x6E
        _emit 0x04
        _emit 0x8B        ; 0040CCD0  mov edx, dword ptr [esi + 8]
        _emit 0x56
        _emit 0x08
        _emit 0xC1        ; 0040CCD3  shr eax, 0xe
        _emit 0xE8
        _emit 0x0E
        _emit 0x83        ; 0040CCD6  and eax, 1
        _emit 0xE0
        _emit 0x01
        _emit 0x83        ; 0040CCD9  cmp edi, 1
        _emit 0xFF
        _emit 0x01
        _emit 0x89        ; 0040CCDC  mov dword ptr [esp + 0x68], ebp
        _emit 0x6C
        _emit 0x24
        _emit 0x68
        _emit 0x89        ; 0040CCE0  mov dword ptr [esp + 0x28], edx
        _emit 0x54
        _emit 0x24
        _emit 0x28
        _emit 0x89        ; 0040CCE4  mov dword ptr [esp + 0x1c], eax
        _emit 0x44
        _emit 0x24
        _emit 0x1C
        _emit 0x7E        ; 0040CCE8  jle 0x40ccf2
        _emit 0x08
        _emit 0x81        ; 0040CCEA  add ebx, 0x11c
        _emit 0xC3
        _emit 0x1C
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0xEB        ; 0040CCF0  jmp 0x40cd15
        _emit 0x23
        _emit 0x8B        ; 0040CCF2  mov eax, dword ptr [ebx + 0xe00b]
        _emit 0x83
        _emit 0x0B
        _emit 0xE0
        _emit 0x00
        _emit 0x00
        _emit 0x85        ; 0040CCF8  test eax, eax
        _emit 0xC0
        _emit 0x7D        ; 0040CCFA  jge 0x40cd04
        _emit 0x08
        _emit 0x81        ; 0040CCFC  add ebx, 0x11c
        _emit 0xC3
        _emit 0x1C
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0xEB        ; 0040CD02  jmp 0x40cd15
        _emit 0x11
        _emit 0x8B        ; 0040CD04  mov edx, eax
        _emit 0xD0
        _emit 0xC1        ; 0040CD06  shl edx, 5
        _emit 0xE2
        _emit 0x05
        _emit 0x03        ; 0040CD09  add edx, eax
        _emit 0xD0
        _emit 0xC1        ; 0040CD0B  shl edx, 5
        _emit 0xE2
        _emit 0x05
        _emit 0x8D        ; 0040CD0E  lea ebx, [edx + ebx + 0x11c]
        _emit 0x9C
        _emit 0x1A
        _emit 0x1C
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x8A        ; 0040CD15  mov al, byte ptr [ecx + 0x151]
        _emit 0x81
        _emit 0x51
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x89        ; 0040CD1B  mov dword ptr [esp + 0x38], ebx
        _emit 0x5C
        _emit 0x24
        _emit 0x38
        _emit 0x84        ; 0040CD1F  test al, al
        _emit 0xC0
        _emit 0x0F        ; 0040CD21  je 0x40d500
        _emit 0x84
        _emit 0xD9
        _emit 0x07
        _emit 0x00
        _emit 0x00
        _emit 0x25        ; 0040CD27  and eax, 0xff
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040CD2C  mov edi, eax
        _emit 0xF8
        _emit 0xC1        ; 0040CD2E  shl edi, 4
        _emit 0xE7
        _emit 0x04
        _emit 0x03        ; 0040CD31  add edi, eax
        _emit 0xF8
        _emit 0x8D        ; 0040CD33  lea edi, [edi + edi*2]
        _emit 0x3C
        _emit 0x7F
        _emit 0xD1        ; 0040CD36  shl edi, 1
        _emit 0xE7
        _emit 0x2B        ; 0040CD38  sub edi, eax
        _emit 0xF8
        _emit 0xC1        ; 0040CD3A  shl edi, 4
        _emit 0xE7
        _emit 0x04
        _emit 0x89        ; 0040CD3D  mov dword ptr [esp + 0x2c], edi
        _emit 0x7C
        _emit 0x24
        _emit 0x2C
        _emit 0x8B        ; 0040CD41  mov eax, dword ptr [edi + 0x447938]
        _emit 0x87
        _emit 0x38
        _emit 0x79
        _emit 0x44
        _emit 0x00
        _emit 0x8A        ; 0040CD47  mov dl, byte ptr [eax + 4]
        _emit 0x50
        _emit 0x04
        _emit 0x84        ; 0040CD4A  test dl, dl
        _emit 0xD2
        _emit 0x0F        ; 0040CD4C  je 0x40d500
        _emit 0x84
        _emit 0xAE
        _emit 0x07
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040CD52  mov edx, dword ptr [ecx + 0x54]
        _emit 0x51
        _emit 0x54
        _emit 0x8B        ; 0040CD55  mov eax, dword ptr [edi + 0x447938]
        _emit 0x87
        _emit 0x38
        _emit 0x79
        _emit 0x44
        _emit 0x00
        _emit 0x89        ; 0040CD5B  mov dword ptr [esp + 0x78], edx
        _emit 0x54
        _emit 0x24
        _emit 0x78
        _emit 0x33        ; 0040CD5F  xor edx, edx
        _emit 0xD2
        _emit 0x8A        ; 0040CD61  mov dl, byte ptr [eax + 5]
        _emit 0x50
        _emit 0x05
        _emit 0xBB        ; 0040CD64  mov ebx, 0x64
        _emit 0x64
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x89        ; 0040CD69  mov dword ptr [ecx + 0x54], edx
        _emit 0x51
        _emit 0x54
        _emit 0x8B        ; 0040CD6C  mov edx, dword ptr [ecx + 0x44]
        _emit 0x51
        _emit 0x44
        _emit 0x89        ; 0040CD6F  mov dword ptr [esp + 0x4c], edx
        _emit 0x54
        _emit 0x24
        _emit 0x4C
        _emit 0x8B        ; 0040CD73  mov edx, dword ptr [ecx + 0x48]
        _emit 0x51
        _emit 0x48
        _emit 0x89        ; 0040CD76  mov dword ptr [esp + 0x50], edx
        _emit 0x54
        _emit 0x24
        _emit 0x50
        _emit 0x8B        ; 0040CD7A  mov edx, dword ptr [ecx + 0x4c]
        _emit 0x51
        _emit 0x4C
        _emit 0x89        ; 0040CD7D  mov dword ptr [esp + 0x54], edx
        _emit 0x54
        _emit 0x24
        _emit 0x54
        _emit 0x8B        ; 0040CD81  mov edx, dword ptr [ecx + 0x50]
        _emit 0x51
        _emit 0x50
        _emit 0x89        ; 0040CD84  mov dword ptr [esp + 0x58], edx
        _emit 0x54
        _emit 0x24
        _emit 0x58
        _emit 0x33        ; 0040CD88  xor edx, edx
        _emit 0xD2
        _emit 0x8A        ; 0040CD8A  mov dl, byte ptr [eax + 3]
        _emit 0x50
        _emit 0x03
        _emit 0x8B        ; 0040CD8D  mov eax, dword ptr [edi + 0x447934]
        _emit 0x87
        _emit 0x34
        _emit 0x79
        _emit 0x44
        _emit 0x00
        _emit 0x8B        ; 0040CD93  mov esi, edx
        _emit 0xF2
        _emit 0xC7        ; 0040CD95  mov dword ptr [esp + 0x64], 0
        _emit 0x44
        _emit 0x24
        _emit 0x64
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x2B        ; 0040CD9D  sub eax, esi
        _emit 0xC6
        _emit 0xC7        ; 0040CD9F  mov dword ptr [esp + 0x48], 0
        _emit 0x44
        _emit 0x24
        _emit 0x48
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x83        ; 0040CDA7  add eax, 0x64
        _emit 0xC0
        _emit 0x64
        _emit 0x99        ; 0040CDAA  cdq
        _emit 0xF7        ; 0040CDAB  idiv ebx
        _emit 0xFB
        _emit 0x85        ; 0040CDAD  test esi, esi
        _emit 0xF6
        _emit 0x89        ; 0040CDAF  mov dword ptr [esp + 0x30], edx
        _emit 0x54
        _emit 0x24
        _emit 0x30
        _emit 0x0F        ; 0040CDB3  jle 0x40d4d5
        _emit 0x8E
        _emit 0x1C
        _emit 0x07
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040CDB9  mov ebx, dword ptr [esp + 0x78]
        _emit 0x5C
        _emit 0x24
        _emit 0x78
        _emit 0x8B        ; 0040CDBD  mov eax, edx
        _emit 0xC2
        _emit 0xC1        ; 0040CDBF  shl eax, 4
        _emit 0xE0
        _emit 0x04
        _emit 0x8B        ; 0040CDC2  mov esi, dword ptr [eax + edi + 0x44794c]
        _emit 0xB4
        _emit 0x38
        _emit 0x4C
        _emit 0x79
        _emit 0x44
        _emit 0x00
        _emit 0x8D        ; 0040CDC9  lea eax, [eax + edi + 0x447930]
        _emit 0x84
        _emit 0x38
        _emit 0x30
        _emit 0x79
        _emit 0x44
        _emit 0x00
        _emit 0x89        ; 0040CDD0  mov dword ptr [esp + 0x5c], eax
        _emit 0x44
        _emit 0x24
        _emit 0x5C
        _emit 0x89        ; 0040CDD4  mov dword ptr [esp + 0x3c], esi
        _emit 0x74
        _emit 0x24
        _emit 0x3C
        _emit 0x8B        ; 0040CDD8  mov eax, dword ptr [eax + 0x18]
        _emit 0x40
        _emit 0x18
        _emit 0x83        ; 0040CDDB  and eax, 3
        _emit 0xE0
        _emit 0x03
        _emit 0x85        ; 0040CDDE  test esi, esi
        _emit 0xF6
        _emit 0x89        ; 0040CDE0  mov dword ptr [esp + 0x34], eax
        _emit 0x44
        _emit 0x24
        _emit 0x34
        _emit 0x0F        ; 0040CDE4  je 0x40d4a6
        _emit 0x84
        _emit 0xBC
        _emit 0x06
        _emit 0x00
        _emit 0x00
        _emit 0x33        ; 0040CDEA  xor eax, eax
        _emit 0xC0
        _emit 0x66        ; 0040CDEC  mov ax, word ptr [esi + 3]
        _emit 0x8B
        _emit 0x46
        _emit 0x03
        _emit 0x8B        ; 0040CDF0  mov esi, dword ptr [esp + 0x24]
        _emit 0x74
        _emit 0x24
        _emit 0x24
        _emit 0x89        ; 0040CDF4  mov dword ptr [esp + 0x6c], eax
        _emit 0x44
        _emit 0x24
        _emit 0x6C
        _emit 0x25        ; 0040CDF8  and eax, 0x1fff
        _emit 0xFF
        _emit 0x1F
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040CDFD  mov esi, dword ptr [esi + 0x118]
        _emit 0xB6
        _emit 0x18
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x8D        ; 0040CE03  lea eax, [eax + eax*4]
        _emit 0x04
        _emit 0x80
        _emit 0x8D        ; 0040CE06  lea esi, [esi + eax*4]
        _emit 0x34
        _emit 0x86
        _emit 0x89        ; 0040CE09  mov dword ptr [esp + 0x40], esi
        _emit 0x74
        _emit 0x24
        _emit 0x40
        _emit 0x8B        ; 0040CE0D  mov eax, dword ptr [esi]
        _emit 0x06
        _emit 0x85        ; 0040CE0F  test eax, eax
        _emit 0xC0
        _emit 0x0F        ; 0040CE11  je 0x40d4a2
        _emit 0x84
        _emit 0x8B
        _emit 0x06
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040CE17  mov edx, dword ptr [esi + 4]
        _emit 0x56
        _emit 0x04
        _emit 0x89        ; 0040CE1A  mov dword ptr [esp + 0x74], edx
        _emit 0x54
        _emit 0x24
        _emit 0x74
        _emit 0x8B        ; 0040CE1E  mov edx, dword ptr [esi + 8]
        _emit 0x56
        _emit 0x08
        _emit 0x89        ; 0040CE21  mov dword ptr [esp + 0x70], edx
        _emit 0x54
        _emit 0x24
        _emit 0x70
        _emit 0x8B        ; 0040CE25  mov edx, dword ptr [esi + 0x10]
        _emit 0x56
        _emit 0x10
        _emit 0x85        ; 0040CE28  test edx, edx
        _emit 0xD2
        _emit 0x74        ; 0040CE2A  je 0x40ce61
        _emit 0x35
        _emit 0x3B        ; 0040CE2C  cmp esi, dword ptr [esp + 0x64]
        _emit 0x74
        _emit 0x24
        _emit 0x64
        _emit 0x74        ; 0040CE30  je 0x40ce48
        _emit 0x16
        _emit 0x52        ; 0040CE32  push edx
        _emit 0x50        ; 0040CE33  push eax
        _emit 0xA1        ; 0040CE34  mov eax, dword ptr [0x425a44]
        _emit 0x44
        _emit 0x5A
        _emit 0x42
        _emit 0x00
        _emit 0x50        ; 0040CE39  push eax
        _emit 0xE8        ; 0040CE3A  call 0x4140c0
        _emit 0x81
        _emit 0x72
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040CE3F  mov ecx, dword ptr [0x4cfa00]
        _emit 0x0D
        _emit 0x00
        _emit 0xFA
        _emit 0x4C
        _emit 0x00
        _emit 0x83        ; 0040CE45  add esp, 0xc
        _emit 0xC4
        _emit 0x0C
        _emit 0xF6        ; 0040CE48  test byte ptr [esi + 0xc], 1
        _emit 0x46
        _emit 0x0C
        _emit 0x01
        _emit 0x74        ; 0040CE4C  je 0x40ce55
        _emit 0x07
        _emit 0xA1        ; 0040CE4E  mov eax, dword ptr [0x425a44]
        _emit 0x44
        _emit 0x5A
        _emit 0x42
        _emit 0x00
        _emit 0xEB        ; 0040CE53  jmp 0x40ce6d
        _emit 0x18
        _emit 0x8B        ; 0040CE55  mov edx, dword ptr [0x425a44]
        _emit 0x15
        _emit 0x44
        _emit 0x5A
        _emit 0x42
        _emit 0x00
        _emit 0x89        ; 0040CE5B  mov dword ptr [esp + 0x14], edx
        _emit 0x54
        _emit 0x24
        _emit 0x14
        _emit 0xEB        ; 0040CE5F  jmp 0x40ce7a
        _emit 0x19
        _emit 0x8A        ; 0040CE61  mov dl, byte ptr [esi + 0xc]
        _emit 0x56
        _emit 0x0C
        _emit 0x89        ; 0040CE64  mov dword ptr [esp + 0x14], eax
        _emit 0x44
        _emit 0x24
        _emit 0x14
        _emit 0xF6        ; 0040CE68  test dl, 1
        _emit 0xC2
        _emit 0x01
        _emit 0x74        ; 0040CE6B  je 0x40ce7a
        _emit 0x0D
        _emit 0x89        ; 0040CE6D  mov dword ptr [esp + 0x38], eax
        _emit 0x44
        _emit 0x24
        _emit 0x38
        _emit 0x05        ; 0040CE71  add eax, 0x400
        _emit 0x00
        _emit 0x04
        _emit 0x00
        _emit 0x00
        _emit 0x89        ; 0040CE76  mov dword ptr [esp + 0x14], eax
        _emit 0x44
        _emit 0x24
        _emit 0x14
        _emit 0x8B        ; 0040CE7A  mov edi, dword ptr [edi + 0x447938]
        _emit 0xBF
        _emit 0x38
        _emit 0x79
        _emit 0x44
        _emit 0x00
        _emit 0x33        ; 0040CE80  xor eax, eax
        _emit 0xC0
        _emit 0x8A        ; 0040CE82  mov al, byte ptr [edi + 6]
        _emit 0x47
        _emit 0x06
        _emit 0x83        ; 0040CE85  cmp eax, 4
        _emit 0xF8
        _emit 0x04
        _emit 0x0F        ; 0040CE88  ja 0x40d04c
        _emit 0x87
        _emit 0xBE
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0xFF        ; 0040CE8E  jmp dword ptr [eax*4 + 0x40e400]
        _emit 0x24
        _emit 0x85
        _emit 0x00
        _emit 0xE4
        _emit 0x40
        _emit 0x00
        _emit 0x8B        ; 0040CE95  mov eax, dword ptr [esp + 0x4c]
        _emit 0x44
        _emit 0x24
        _emit 0x4C
        _emit 0x8B        ; 0040CE99  mov edx, dword ptr [esp + 0x50]
        _emit 0x54
        _emit 0x24
        _emit 0x50
        _emit 0x89        ; 0040CE9D  mov dword ptr [ecx + 0x44], eax
        _emit 0x41
        _emit 0x44
        _emit 0x8B        ; 0040CEA0  mov eax, dword ptr [esp + 0x54]
        _emit 0x44
        _emit 0x24
        _emit 0x54
        _emit 0x89        ; 0040CEA4  mov dword ptr [ecx + 0x48], edx
        _emit 0x51
        _emit 0x48
        _emit 0x8B        ; 0040CEA7  mov edx, dword ptr [esp + 0x58]
        _emit 0x54
        _emit 0x24
        _emit 0x58
        _emit 0x89        ; 0040CEAB  mov dword ptr [ecx + 0x4c], eax
        _emit 0x41
        _emit 0x4C
        _emit 0xE9        ; 0040CEAE  jmp 0x40d049
        _emit 0x96
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0xF6        ; 0040CEB3  test byte ptr [0x4456fc], 1
        _emit 0x05
        _emit 0xFC
        _emit 0x56
        _emit 0x44
        _emit 0x00
        _emit 0x01
        _emit 0x74        ; 0040CEBA  je 0x40ceda
        _emit 0x1E
        _emit 0x0F        ; 0040CEBC  movsx eax, byte ptr [edi + 7]
        _emit 0xBE
        _emit 0x47
        _emit 0x07
        _emit 0x0F        ; 0040CEC0  movsx edx, byte ptr [edi + 8]
        _emit 0xBE
        _emit 0x57
        _emit 0x08
        _emit 0x89        ; 0040CEC4  mov dword ptr [ecx + 0x44], eax
        _emit 0x41
        _emit 0x44
        _emit 0x89        ; 0040CEC7  mov dword ptr [ecx + 0x48], edx
        _emit 0x51
        _emit 0x48
        _emit 0x0F        ; 0040CECA  movsx eax, byte ptr [edi + 9]
        _emit 0xBE
        _emit 0x47
        _emit 0x09
        _emit 0x0F        ; 0040CECE  movsx edx, byte ptr [edi + 0xa]
        _emit 0xBE
        _emit 0x57
        _emit 0x0A
        _emit 0x89        ; 0040CED2  mov dword ptr [ecx + 0x4c], eax
        _emit 0x41
        _emit 0x4C
        _emit 0xE9        ; 0040CED5  jmp 0x40d049
        _emit 0x6F
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x33        ; 0040CEDA  xor eax, eax
        _emit 0xC0
        _emit 0x8B        ; 0040CEDC  mov edx, dword ptr [esp + 0x2c]
        _emit 0x54
        _emit 0x24
        _emit 0x2C
        _emit 0x8A        ; 0040CEE0  mov al, byte ptr [edi + 4]
        _emit 0x47
        _emit 0x04
        _emit 0x8B        ; 0040CEE3  mov esi, eax
        _emit 0xF0
        _emit 0x0F        ; 0040CEE5  imul eax, dword ptr [esp + 0x48]
        _emit 0xAF
        _emit 0x44
        _emit 0x24
        _emit 0x48
        _emit 0x8B        ; 0040CEEA  mov ebp, dword ptr [edx + 0x44793c]
        _emit 0xAA
        _emit 0x3C
        _emit 0x79
        _emit 0x44
        _emit 0x00
        _emit 0x33        ; 0040CEF0  xor edx, edx
        _emit 0xD2
        _emit 0x8A        ; 0040CEF2  mov dl, byte ptr [edi + 3]
        _emit 0x57
        _emit 0x03
        _emit 0x03        ; 0040CEF5  add eax, ebp
        _emit 0xC5
        _emit 0x6B        ; 0040CEF7  imul eax, eax, 0x64
        _emit 0xC0
        _emit 0x64
        _emit 0x8B        ; 0040CEFA  mov ebp, edx
        _emit 0xEA
        _emit 0x0F        ; 0040CEFC  imul ebp, esi
        _emit 0xAF
        _emit 0xEE
        _emit 0x99        ; 0040CEFF  cdq
        _emit 0xF7        ; 0040CF00  idiv ebp
        _emit 0xFD
        _emit 0xBD        ; 0040CF02  mov ebp, 0x64
        _emit 0x64
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040CF07  mov esi, eax
        _emit 0xF0
        _emit 0x0F        ; 0040CF09  movsx eax, byte ptr [edi + 7]
        _emit 0xBE
        _emit 0x47
        _emit 0x07
        _emit 0x2B        ; 0040CF0D  sub ebp, esi
        _emit 0xEE
        _emit 0x0F        ; 0040CF0F  imul eax, ebp
        _emit 0xAF
        _emit 0xC5
        _emit 0x89        ; 0040CF12  mov dword ptr [esp + 0x64], eax
        _emit 0x44
        _emit 0x24
        _emit 0x64
        _emit 0x8B        ; 0040CF16  mov eax, esi
        _emit 0xC6
        _emit 0x0F        ; 0040CF18  imul eax, dword ptr [esp + 0x4c]
        _emit 0xAF
        _emit 0x44
        _emit 0x24
        _emit 0x4C
        _emit 0x8B        ; 0040CF1D  mov edx, dword ptr [esp + 0x64]
        _emit 0x54
        _emit 0x24
        _emit 0x64
        _emit 0x03        ; 0040CF21  add edx, eax
        _emit 0xD0
        _emit 0xB8        ; 0040CF23  mov eax, 0x51eb851f
        _emit 0x1F
        _emit 0x85
        _emit 0xEB
        _emit 0x51
        _emit 0xF7        ; 0040CF28  imul edx
        _emit 0xEA
        _emit 0xC1        ; 0040CF2A  sar edx, 5
        _emit 0xFA
        _emit 0x05
        _emit 0x8B        ; 0040CF2D  mov eax, edx
        _emit 0xC2
        _emit 0xC1        ; 0040CF2F  shr eax, 0x1f
        _emit 0xE8
        _emit 0x1F
        _emit 0x03        ; 0040CF32  add edx, eax
        _emit 0xD0
        _emit 0x0F        ; 0040CF34  movsx eax, byte ptr [edi + 8]
        _emit 0xBE
        _emit 0x47
        _emit 0x08
        _emit 0x0F        ; 0040CF38  imul eax, ebp
        _emit 0xAF
        _emit 0xC5
        _emit 0x89        ; 0040CF3B  mov dword ptr [esp + 0x64], eax
        _emit 0x44
        _emit 0x24
        _emit 0x64
        _emit 0x8B        ; 0040CF3F  mov eax, esi
        _emit 0xC6
        _emit 0x0F        ; 0040CF41  imul eax, dword ptr [esp + 0x50]
        _emit 0xAF
        _emit 0x44
        _emit 0x24
        _emit 0x50
        _emit 0x89        ; 0040CF46  mov dword ptr [ecx + 0x44], edx
        _emit 0x51
        _emit 0x44
        _emit 0x8B        ; 0040CF49  mov edx, dword ptr [esp + 0x64]
        _emit 0x54
        _emit 0x24
        _emit 0x64
        _emit 0x03        ; 0040CF4D  add edx, eax
        _emit 0xD0
        _emit 0xB8        ; 0040CF4F  mov eax, 0x51eb851f
        _emit 0x1F
        _emit 0x85
        _emit 0xEB
        _emit 0x51
        _emit 0xF7        ; 0040CF54  imul edx
        _emit 0xEA
        _emit 0xC1        ; 0040CF56  sar edx, 5
        _emit 0xFA
        _emit 0x05
        _emit 0x8B        ; 0040CF59  mov eax, edx
        _emit 0xC2
        _emit 0xC1        ; 0040CF5B  shr eax, 0x1f
        _emit 0xE8
        _emit 0x1F
        _emit 0x03        ; 0040CF5E  add edx, eax
        _emit 0xD0
        _emit 0x0F        ; 0040CF60  movsx eax, byte ptr [edi + 9]
        _emit 0xBE
        _emit 0x47
        _emit 0x09
        _emit 0x0F        ; 0040CF64  imul eax, ebp
        _emit 0xAF
        _emit 0xC5
        _emit 0x89        ; 0040CF67  mov dword ptr [esp + 0x64], eax
        _emit 0x44
        _emit 0x24
        _emit 0x64
        _emit 0x8B        ; 0040CF6B  mov eax, esi
        _emit 0xC6
        _emit 0x0F        ; 0040CF6D  imul eax, dword ptr [esp + 0x54]
        _emit 0xAF
        _emit 0x44
        _emit 0x24
        _emit 0x54
        _emit 0x89        ; 0040CF72  mov dword ptr [ecx + 0x48], edx
        _emit 0x51
        _emit 0x48
        _emit 0x8B        ; 0040CF75  mov edx, dword ptr [esp + 0x64]
        _emit 0x54
        _emit 0x24
        _emit 0x64
        _emit 0x03        ; 0040CF79  add edx, eax
        _emit 0xD0
        _emit 0xB8        ; 0040CF7B  mov eax, 0x51eb851f
        _emit 0x1F
        _emit 0x85
        _emit 0xEB
        _emit 0x51
        _emit 0xF7        ; 0040CF80  imul edx
        _emit 0xEA
        _emit 0xC1        ; 0040CF82  sar edx, 5
        _emit 0xFA
        _emit 0x05
        _emit 0x8B        ; 0040CF85  mov eax, edx
        _emit 0xC2
        _emit 0xC1        ; 0040CF87  shr eax, 0x1f
        _emit 0xE8
        _emit 0x1F
        _emit 0x03        ; 0040CF8A  add edx, eax
        _emit 0xD0
        _emit 0x89        ; 0040CF8C  mov dword ptr [ecx + 0x4c], edx
        _emit 0x51
        _emit 0x4C
        _emit 0x0F        ; 0040CF8F  movsx edx, byte ptr [edi + 0xa]
        _emit 0xBE
        _emit 0x57
        _emit 0x0A
        _emit 0x0F        ; 0040CF93  imul edx, ebp
        _emit 0xAF
        _emit 0xD5
        _emit 0xE9        ; 0040CF96  jmp 0x40d031
        _emit 0x96
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0xE8        ; 0040CF9B  call 0x417a22
        _emit 0x82
        _emit 0xAA
        _emit 0x00
        _emit 0x00
        _emit 0x99        ; 0040CFA0  cdq
        _emit 0xB9        ; 0040CFA1  mov ecx, 0x64
        _emit 0x64
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0xF7        ; 0040CFA6  idiv ecx
        _emit 0xF9
        _emit 0x8B        ; 0040CFA8  mov esi, ecx
        _emit 0xF1
        _emit 0x8B        ; 0040CFAA  mov edi, edx
        _emit 0xFA
        _emit 0x8B        ; 0040CFAC  mov edx, dword ptr [esp + 0x2c]
        _emit 0x54
        _emit 0x24
        _emit 0x2C
        _emit 0x2B        ; 0040CFB0  sub esi, edi
        _emit 0xF7
        _emit 0x8B        ; 0040CFB2  mov ebp, dword ptr [edx + 0x447938]
        _emit 0xAA
        _emit 0x38
        _emit 0x79
        _emit 0x44
        _emit 0x00
        _emit 0x8B        ; 0040CFB8  mov eax, esi
        _emit 0xC6
        _emit 0x0F        ; 0040CFBA  imul eax, dword ptr [esp + 0x4c]
        _emit 0xAF
        _emit 0x44
        _emit 0x24
        _emit 0x4C
        _emit 0x0F        ; 0040CFBF  movsx ecx, byte ptr [ebp + 7]
        _emit 0xBE
        _emit 0x4D
        _emit 0x07
        _emit 0x0F        ; 0040CFC3  imul ecx, edi
        _emit 0xAF
        _emit 0xCF
        _emit 0x03        ; 0040CFC6  add ecx, eax
        _emit 0xC8
        _emit 0xB8        ; 0040CFC8  mov eax, 0x51eb851f
        _emit 0x1F
        _emit 0x85
        _emit 0xEB
        _emit 0x51
        _emit 0xF7        ; 0040CFCD  imul ecx
        _emit 0xE9
        _emit 0xC1        ; 0040CFCF  sar edx, 5
        _emit 0xFA
        _emit 0x05
        _emit 0x8B        ; 0040CFD2  mov ecx, edx
        _emit 0xCA
        _emit 0x8B        ; 0040CFD4  mov eax, esi
        _emit 0xC6
        _emit 0xC1        ; 0040CFD6  shr ecx, 0x1f
        _emit 0xE9
        _emit 0x1F
        _emit 0x0F        ; 0040CFD9  imul eax, dword ptr [esp + 0x50]
        _emit 0xAF
        _emit 0x44
        _emit 0x24
        _emit 0x50
        _emit 0x03        ; 0040CFDE  add edx, ecx
        _emit 0xD1
        _emit 0x8B        ; 0040CFE0  mov ecx, dword ptr [0x4cfa00]
        _emit 0x0D
        _emit 0x00
        _emit 0xFA
        _emit 0x4C
        _emit 0x00
        _emit 0x89        ; 0040CFE6  mov dword ptr [ecx + 0x44], edx
        _emit 0x51
        _emit 0x44
        _emit 0x0F        ; 0040CFE9  movsx edx, byte ptr [ebp + 8]
        _emit 0xBE
        _emit 0x55
        _emit 0x08
        _emit 0x0F        ; 0040CFED  imul edx, edi
        _emit 0xAF
        _emit 0xD7
        _emit 0x03        ; 0040CFF0  add edx, eax
        _emit 0xD0
        _emit 0xB8        ; 0040CFF2  mov eax, 0x51eb851f
        _emit 0x1F
        _emit 0x85
        _emit 0xEB
        _emit 0x51
        _emit 0xF7        ; 0040CFF7  imul edx
        _emit 0xEA
        _emit 0xC1        ; 0040CFF9  sar edx, 5
        _emit 0xFA
        _emit 0x05
        _emit 0x8B        ; 0040CFFC  mov eax, edx
        _emit 0xC2
        _emit 0xC1        ; 0040CFFE  shr eax, 0x1f
        _emit 0xE8
        _emit 0x1F
        _emit 0x03        ; 0040D001  add edx, eax
        _emit 0xD0
        _emit 0x8B        ; 0040D003  mov eax, esi
        _emit 0xC6
        _emit 0x0F        ; 0040D005  imul eax, dword ptr [esp + 0x54]
        _emit 0xAF
        _emit 0x44
        _emit 0x24
        _emit 0x54
        _emit 0x89        ; 0040D00A  mov dword ptr [ecx + 0x48], edx
        _emit 0x51
        _emit 0x48
        _emit 0x0F        ; 0040D00D  movsx edx, byte ptr [ebp + 9]
        _emit 0xBE
        _emit 0x55
        _emit 0x09
        _emit 0x0F        ; 0040D011  imul edx, edi
        _emit 0xAF
        _emit 0xD7
        _emit 0x03        ; 0040D014  add edx, eax
        _emit 0xD0
        _emit 0xB8        ; 0040D016  mov eax, 0x51eb851f
        _emit 0x1F
        _emit 0x85
        _emit 0xEB
        _emit 0x51
        _emit 0xF7        ; 0040D01B  imul edx
        _emit 0xEA
        _emit 0xC1        ; 0040D01D  sar edx, 5
        _emit 0xFA
        _emit 0x05
        _emit 0x8B        ; 0040D020  mov eax, edx
        _emit 0xC2
        _emit 0xC1        ; 0040D022  shr eax, 0x1f
        _emit 0xE8
        _emit 0x1F
        _emit 0x03        ; 0040D025  add edx, eax
        _emit 0xD0
        _emit 0x89        ; 0040D027  mov dword ptr [ecx + 0x4c], edx
        _emit 0x51
        _emit 0x4C
        _emit 0x0F        ; 0040D02A  movsx edx, byte ptr [ebp + 0xa]
        _emit 0xBE
        _emit 0x55
        _emit 0x0A
        _emit 0x0F        ; 0040D02E  imul edx, edi
        _emit 0xAF
        _emit 0xD7
        _emit 0x0F        ; 0040D031  imul esi, dword ptr [esp + 0x58]
        _emit 0xAF
        _emit 0x74
        _emit 0x24
        _emit 0x58
        _emit 0x03        ; 0040D036  add edx, esi
        _emit 0xD6
        _emit 0xB8        ; 0040D038  mov eax, 0x51eb851f
        _emit 0x1F
        _emit 0x85
        _emit 0xEB
        _emit 0x51
        _emit 0xF7        ; 0040D03D  imul edx
        _emit 0xEA
        _emit 0xC1        ; 0040D03F  sar edx, 5
        _emit 0xFA
        _emit 0x05
        _emit 0x8B        ; 0040D042  mov eax, edx
        _emit 0xC2
        _emit 0xC1        ; 0040D044  shr eax, 0x1f
        _emit 0xE8
        _emit 0x1F
        _emit 0x03        ; 0040D047  add edx, eax
        _emit 0xD0
        _emit 0x89        ; 0040D049  mov dword ptr [ecx + 0x50], edx
        _emit 0x51
        _emit 0x50
        _emit 0x8B        ; 0040D04C  mov ebp, dword ptr [esp + 0x38]
        _emit 0x6C
        _emit 0x24
        _emit 0x38
        _emit 0xBF        ; 0040D050  mov edi, 0x4d1a20
        _emit 0x20
        _emit 0x1A
        _emit 0x4D
        _emit 0x00
        _emit 0x89        ; 0040D055  mov dword ptr [esp + 0x60], ebp
        _emit 0x6C
        _emit 0x24
        _emit 0x60
        _emit 0xC7        ; 0040D059  mov dword ptr [esp + 0x64], 0x100
        _emit 0x44
        _emit 0x24
        _emit 0x64
        _emit 0x00
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040D061  mov edx, dword ptr [ebp]
        _emit 0x55
        _emit 0x00
        _emit 0xF7        ; 0040D064  test edx, 0xffffff
        _emit 0xC2
        _emit 0xFF
        _emit 0xFF
        _emit 0xFF
        _emit 0x00
        _emit 0x0F        ; 0040D06A  je 0x40d10c
        _emit 0x84
        _emit 0x9C
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040D070  mov esi, dword ptr [ecx + 0x44]
        _emit 0x71
        _emit 0x44
        _emit 0x8B        ; 0040D073  mov eax, edx
        _emit 0xC2
        _emit 0xC1        ; 0040D075  shr eax, 0x10
        _emit 0xE8
        _emit 0x10
        _emit 0x25        ; 0040D078  and eax, 0xff
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0xC1        ; 0040D07D  shr eax, 3
        _emit 0xE8
        _emit 0x03
        _emit 0x03        ; 0040D080  add eax, esi
        _emit 0xC6
        _emit 0x83        ; 0040D082  cmp eax, 0x1f
        _emit 0xF8
        _emit 0x1F
        _emit 0x7E        ; 0040D085  jle 0x40d08e
        _emit 0x07
        _emit 0xB8        ; 0040D087  mov eax, 0x1f
        _emit 0x1F
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0xEB        ; 0040D08C  jmp 0x40d094
        _emit 0x06
        _emit 0x85        ; 0040D08E  test eax, eax
        _emit 0xC0
        _emit 0x7D        ; 0040D090  jge 0x40d094
        _emit 0x02
        _emit 0x33        ; 0040D092  xor eax, eax
        _emit 0xC0
        _emit 0x8B        ; 0040D094  mov esi, dword ptr [ecx + 0x48]
        _emit 0x71
        _emit 0x48
        _emit 0xC1        ; 0040D097  shr edx, 8
        _emit 0xEA
        _emit 0x08
        _emit 0x81        ; 0040D09A  and edx, 0xff
        _emit 0xE2
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0xC1        ; 0040D0A0  shr edx, 3
        _emit 0xEA
        _emit 0x03
        _emit 0x03        ; 0040D0A3  add edx, esi
        _emit 0xD6
        _emit 0x8B        ; 0040D0A5  mov esi, edx
        _emit 0xF2
        _emit 0x83        ; 0040D0A7  cmp esi, 0x1f
        _emit 0xFE
        _emit 0x1F
        _emit 0x7E        ; 0040D0AA  jle 0x40d0b3
        _emit 0x07
        _emit 0xBE        ; 0040D0AC  mov esi, 0x1f
        _emit 0x1F
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0xEB        ; 0040D0B1  jmp 0x40d0b9
        _emit 0x06
        _emit 0x85        ; 0040D0B3  test esi, esi
        _emit 0xF6
        _emit 0x7D        ; 0040D0B5  jge 0x40d0b9
        _emit 0x02
        _emit 0x33        ; 0040D0B7  xor esi, esi
        _emit 0xF6
        _emit 0x33        ; 0040D0B9  xor edx, edx
        _emit 0xD2
        _emit 0x8A        ; 0040D0BB  mov dl, byte ptr [ebp]
        _emit 0x55
        _emit 0x00
        _emit 0x8B        ; 0040D0BE  mov ebp, dword ptr [ecx + 0x4c]
        _emit 0x69
        _emit 0x4C
        _emit 0xC1        ; 0040D0C1  shr edx, 3
        _emit 0xEA
        _emit 0x03
        _emit 0x03        ; 0040D0C4  add edx, ebp
        _emit 0xD5
        _emit 0x83        ; 0040D0C6  cmp edx, 0x1f
        _emit 0xFA
        _emit 0x1F
        _emit 0x7E        ; 0040D0C9  jle 0x40d0d2
        _emit 0x07
        _emit 0xBA        ; 0040D0CB  mov edx, 0x1f
        _emit 0x1F
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0xEB        ; 0040D0D0  jmp 0x40d0d8
        _emit 0x06
        _emit 0x85        ; 0040D0D2  test edx, edx
        _emit 0xD2
        _emit 0x7D        ; 0040D0D4  jge 0x40d0d8
        _emit 0x02
        _emit 0x33        ; 0040D0D6  xor edx, edx
        _emit 0xD2
        _emit 0x8D        ; 0040D0D8  lea ebp, [edx + esi]
        _emit 0x2C
        _emit 0x32
        _emit 0x03        ; 0040D0DB  add ebp, eax
        _emit 0xE8
        _emit 0x75        ; 0040D0DD  jne 0x40d0e4
        _emit 0x05
        _emit 0xBA        ; 0040D0DF  mov edx, 1
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040D0E4  mov ebp, dword ptr [0x424704]
        _emit 0x2D
        _emit 0x04
        _emit 0x47
        _emit 0x42
        _emit 0x00
        _emit 0x85        ; 0040D0EA  test ebp, ebp
        _emit 0xED
        _emit 0x74        ; 0040D0EC  je 0x40d0fd
        _emit 0x0F
        _emit 0xC1        ; 0040D0EE  shl eax, 5
        _emit 0xE0
        _emit 0x05
        _emit 0x03        ; 0040D0F1  add eax, esi
        _emit 0xC6
        _emit 0xC1        ; 0040D0F3  shl eax, 6
        _emit 0xE0
        _emit 0x06
        _emit 0x03        ; 0040D0F6  add eax, edx
        _emit 0xC2
        _emit 0x66        ; 0040D0F8  mov word ptr [edi], ax
        _emit 0x89
        _emit 0x07
        _emit 0xEB        ; 0040D0FB  jmp 0x40d111
        _emit 0x14
        _emit 0xC1        ; 0040D0FD  shl eax, 5
        _emit 0xE0
        _emit 0x05
        _emit 0x03        ; 0040D100  add eax, esi
        _emit 0xC6
        _emit 0xC1        ; 0040D102  shl eax, 5
        _emit 0xE0
        _emit 0x05
        _emit 0x03        ; 0040D105  add eax, edx
        _emit 0xC2
        _emit 0x66        ; 0040D107  mov word ptr [edi], ax
        _emit 0x89
        _emit 0x07
        _emit 0xEB        ; 0040D10A  jmp 0x40d111
        _emit 0x05
        _emit 0x66        ; 0040D10C  mov word ptr [edi], 0
        _emit 0xC7
        _emit 0x07
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040D111  mov ebp, dword ptr [esp + 0x60]
        _emit 0x6C
        _emit 0x24
        _emit 0x60
        _emit 0x8B        ; 0040D115  mov eax, dword ptr [esp + 0x64]
        _emit 0x44
        _emit 0x24
        _emit 0x64
        _emit 0x83        ; 0040D119  add edi, 2
        _emit 0xC7
        _emit 0x02
        _emit 0x83        ; 0040D11C  add ebp, 4
        _emit 0xC5
        _emit 0x04
        _emit 0x48        ; 0040D11F  dec eax
        _emit 0x89        ; 0040D120  mov dword ptr [esp + 0x60], ebp
        _emit 0x6C
        _emit 0x24
        _emit 0x60
        _emit 0x89        ; 0040D124  mov dword ptr [esp + 0x64], eax
        _emit 0x44
        _emit 0x24
        _emit 0x64
        _emit 0x0F        ; 0040D128  jne 0x40d061
        _emit 0x85
        _emit 0x33
        _emit 0xFF
        _emit 0xFF
        _emit 0xFF
        _emit 0x8B        ; 0040D12E  mov eax, dword ptr [ecx + 0x15a]
        _emit 0x81
        _emit 0x5A
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x83        ; 0040D134  cmp eax, 5
        _emit 0xF8
        _emit 0x05
        _emit 0x0F        ; 0040D137  ja 0x40d452
        _emit 0x87
        _emit 0x15
        _emit 0x03
        _emit 0x00
        _emit 0x00
        _emit 0xFF        ; 0040D13D  jmp dword ptr [eax*4 + 0x40e414]
        _emit 0x24
        _emit 0x85
        _emit 0x14
        _emit 0xE4
        _emit 0x40
        _emit 0x00
        _emit 0x8B        ; 0040D144  mov ebp, dword ptr [esp + 0x5c]
        _emit 0x6C
        _emit 0x24
        _emit 0x5C
        _emit 0xF6        ; 0040D148  test byte ptr [ebp + 0x18], 4
        _emit 0x45
        _emit 0x18
        _emit 0x04
        _emit 0x74        ; 0040D14C  je 0x40d195
        _emit 0x47
        _emit 0x8B        ; 0040D14E  mov edx, dword ptr [esp + 0x30]
        _emit 0x54
        _emit 0x24
        _emit 0x30
        _emit 0x8B        ; 0040D152  mov eax, dword ptr [esp + 0x2c]
        _emit 0x44
        _emit 0x24
        _emit 0x2C
        _emit 0x42        ; 0040D156  inc edx
        _emit 0x8B        ; 0040D157  mov edi, dword ptr [esp + 0x40]
        _emit 0x7C
        _emit 0x24
        _emit 0x40
        _emit 0xC1        ; 0040D15B  shl edx, 4
        _emit 0xE2
        _emit 0x04
        _emit 0x8B        ; 0040D15E  mov esi, dword ptr [edi + 4]
        _emit 0x77
        _emit 0x04
        _emit 0x8B        ; 0040D161  mov eax, dword ptr [edx + eax + 0x447930]
        _emit 0x84
        _emit 0x02
        _emit 0x30
        _emit 0x79
        _emit 0x44
        _emit 0x00
        _emit 0x99        ; 0040D168  cdq
        _emit 0x81        ; 0040D169  and edx, 0xffff
        _emit 0xE2
        _emit 0xFF
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x03        ; 0040D16F  add eax, edx
        _emit 0xC2
        _emit 0x8B        ; 0040D171  mov ebx, eax
        _emit 0xD8
        _emit 0x8B        ; 0040D173  mov eax, dword ptr [esp + 0x3c]
        _emit 0x44
        _emit 0x24
        _emit 0x3C
        _emit 0xC1        ; 0040D177  sar ebx, 0x10
        _emit 0xFB
        _emit 0x10
        _emit 0x0F        ; 0040D17A  movsx edx, word ptr [eax + 5]
        _emit 0xBF
        _emit 0x50
        _emit 0x05
        _emit 0x2B        ; 0040D17E  sub ebx, edx
        _emit 0xDA
        _emit 0x8B        ; 0040D180  mov edx, esi
        _emit 0xD6
        _emit 0xD1        ; 0040D182  shr edx, 1
        _emit 0xEA
        _emit 0x03        ; 0040D184  add ebx, edx
        _emit 0xDA
        _emit 0x8B        ; 0040D186  mov edx, dword ptr [esp + 0x34]
        _emit 0x54
        _emit 0x24
        _emit 0x34
        _emit 0x2B        ; 0040D18A  sub ebx, esi
        _emit 0xDE
        _emit 0x83        ; 0040D18C  xor edx, 1
        _emit 0xF2
        _emit 0x01
        _emit 0x89        ; 0040D18F  mov dword ptr [esp + 0x34], edx
        _emit 0x54
        _emit 0x24
        _emit 0x34
        _emit 0xEB        ; 0040D193  jmp 0x40d1cb
        _emit 0x36
        _emit 0x8B        ; 0040D195  mov eax, dword ptr [esp + 0x30]
        _emit 0x44
        _emit 0x24
        _emit 0x30
        _emit 0x8B        ; 0040D199  mov edx, dword ptr [esp + 0x2c]
        _emit 0x54
        _emit 0x24
        _emit 0x2C
        _emit 0x40        ; 0040D19D  inc eax
        _emit 0x8B        ; 0040D19E  mov edi, dword ptr [esp + 0x40]
        _emit 0x7C
        _emit 0x24
        _emit 0x40
        _emit 0xC1        ; 0040D1A2  shl eax, 4
        _emit 0xE0
        _emit 0x04
        _emit 0x8B        ; 0040D1A5  mov eax, dword ptr [eax + edx + 0x447930]
        _emit 0x84
        _emit 0x10
        _emit 0x30
        _emit 0x79
        _emit 0x44
        _emit 0x00
        _emit 0x99        ; 0040D1AC  cdq
        _emit 0x81        ; 0040D1AD  and edx, 0xffff
        _emit 0xE2
        _emit 0xFF
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x03        ; 0040D1B3  add eax, edx
        _emit 0xC2
        _emit 0x8B        ; 0040D1B5  mov ebx, eax
        _emit 0xD8
        _emit 0x8B        ; 0040D1B7  mov eax, dword ptr [edi + 4]
        _emit 0x47
        _emit 0x04
        _emit 0xC1        ; 0040D1BA  sar ebx, 0x10
        _emit 0xFB
        _emit 0x10
        _emit 0xD1        ; 0040D1BD  shr eax, 1
        _emit 0xE8
        _emit 0x2B        ; 0040D1BF  sub ebx, eax
        _emit 0xD8
        _emit 0x8B        ; 0040D1C1  mov eax, dword ptr [esp + 0x3c]
        _emit 0x44
        _emit 0x24
        _emit 0x3C
        _emit 0x0F        ; 0040D1C5  movsx edx, word ptr [eax + 5]
        _emit 0xBF
        _emit 0x50
        _emit 0x05
        _emit 0x03        ; 0040D1C9  add ebx, edx
        _emit 0xDA
        _emit 0x0F        ; 0040D1CB  movsx esi, word ptr [eax + 7]
        _emit 0xBF
        _emit 0x70
        _emit 0x07
        _emit 0x8B        ; 0040D1CF  mov eax, dword ptr [ebp + 0x14]
        _emit 0x45
        _emit 0x14
        _emit 0x99        ; 0040D1D2  cdq
        _emit 0x81        ; 0040D1D3  and edx, 0xffff
        _emit 0xE2
        _emit 0xFF
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x03        ; 0040D1D9  add eax, edx
        _emit 0xC2
        _emit 0x8B        ; 0040D1DB  mov edx, dword ptr [edi + 8]
        _emit 0x57
        _emit 0x08
        _emit 0xC1        ; 0040D1DE  sar eax, 0x10
        _emit 0xF8
        _emit 0x10
        _emit 0x03        ; 0040D1E1  add esi, eax
        _emit 0xF0
        _emit 0x8B        ; 0040D1E3  mov eax, dword ptr [ecx + 0x28]
        _emit 0x41
        _emit 0x28
        _emit 0x2B        ; 0040D1E6  sub esi, edx
        _emit 0xF2
        _emit 0xA9        ; 0040D1E8  test eax, 0x40000000
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x40
        _emit 0x0F        ; 0040D1ED  je 0x40d43d
        _emit 0x84
        _emit 0x4A
        _emit 0x02
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040D1F3  mov ecx, dword ptr [0x447f2c]
        _emit 0x0D
        _emit 0x2C
        _emit 0x7F
        _emit 0x44
        _emit 0x00
        _emit 0xA1        ; 0040D1F9  mov eax, dword ptr [0x447f30]
        _emit 0x30
        _emit 0x7F
        _emit 0x44
        _emit 0x00
        _emit 0x2B        ; 0040D1FE  sub ebx, ecx
        _emit 0xD9
        _emit 0x8B        ; 0040D200  mov ecx, dword ptr [0x447dad]
        _emit 0x0D
        _emit 0xAD
        _emit 0x7D
        _emit 0x44
        _emit 0x00
        _emit 0x2B        ; 0040D206  sub esi, eax
        _emit 0xF0
        _emit 0xA1        ; 0040D208  mov eax, dword ptr [0x447dc1]
        _emit 0xC1
        _emit 0x7D
        _emit 0x44
        _emit 0x00
        _emit 0x03        ; 0040D20D  add ebx, ecx
        _emit 0xD9
        _emit 0x03        ; 0040D20F  add esi, eax
        _emit 0xF0
        _emit 0x89        ; 0040D211  mov dword ptr [esp + 0x44], esi
        _emit 0x74
        _emit 0x24
        _emit 0x44
        _emit 0xE9        ; 0040D215  jmp 0x40d456
        _emit 0x3C
        _emit 0x02
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040D21A  mov esi, dword ptr [esp + 0x5c]
        _emit 0x74
        _emit 0x24
        _emit 0x5C
        _emit 0xF6        ; 0040D21E  test byte ptr [esi + 0x18], 4
        _emit 0x46
        _emit 0x18
        _emit 0x04
        _emit 0x8B        ; 0040D222  mov eax, dword ptr [esp + 0x30]
        _emit 0x44
        _emit 0x24
        _emit 0x30
        _emit 0x74        ; 0040D226  je 0x40d265
        _emit 0x3D
        _emit 0x8B        ; 0040D228  mov edx, dword ptr [esp + 0x2c]
        _emit 0x54
        _emit 0x24
        _emit 0x2C
        _emit 0x40        ; 0040D22C  inc eax
        _emit 0xC1        ; 0040D22D  shl eax, 4
        _emit 0xE0
        _emit 0x04
        _emit 0x8B        ; 0040D230  mov edi, dword ptr [esp + 0x3c]
        _emit 0x7C
        _emit 0x24
        _emit 0x3C
        _emit 0x8B        ; 0040D234  mov eax, dword ptr [eax + edx + 0x447930]
        _emit 0x84
        _emit 0x10
        _emit 0x30
        _emit 0x79
        _emit 0x44
        _emit 0x00
        _emit 0x99        ; 0040D23B  cdq
        _emit 0x81        ; 0040D23C  and edx, 0xffff
        _emit 0xE2
        _emit 0xFF
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x03        ; 0040D242  add eax, edx
        _emit 0xC2
        _emit 0x8B        ; 0040D244  mov edx, dword ptr [esp + 0x40]
        _emit 0x54
        _emit 0x24
        _emit 0x40
        _emit 0x8B        ; 0040D248  mov ebx, eax
        _emit 0xD8
        _emit 0x0F        ; 0040D24A  movsx eax, word ptr [edi + 5]
        _emit 0xBF
        _emit 0x47
        _emit 0x05
        _emit 0x8B        ; 0040D24E  mov ebp, dword ptr [edx + 4]
        _emit 0x6A
        _emit 0x04
        _emit 0xC1        ; 0040D251  sar ebx, 0x10
        _emit 0xFB
        _emit 0x10
        _emit 0x2B        ; 0040D254  sub ebx, eax
        _emit 0xD8
        _emit 0x8B        ; 0040D256  mov eax, dword ptr [esp + 0x34]
        _emit 0x44
        _emit 0x24
        _emit 0x34
        _emit 0x2B        ; 0040D25A  sub ebx, ebp
        _emit 0xDD
        _emit 0x83        ; 0040D25C  xor eax, 1
        _emit 0xF0
        _emit 0x01
        _emit 0x89        ; 0040D25F  mov dword ptr [esp + 0x34], eax
        _emit 0x44
        _emit 0x24
        _emit 0x34
        _emit 0xEB        ; 0040D263  jmp 0x40d28c
        _emit 0x27
        _emit 0x8B        ; 0040D265  mov edx, dword ptr [esp + 0x2c]
        _emit 0x54
        _emit 0x24
        _emit 0x2C
        _emit 0x40        ; 0040D269  inc eax
        _emit 0xC1        ; 0040D26A  shl eax, 4
        _emit 0xE0
        _emit 0x04
        _emit 0x8B        ; 0040D26D  mov edi, dword ptr [esp + 0x3c]
        _emit 0x7C
        _emit 0x24
        _emit 0x3C
        _emit 0x8B        ; 0040D271  mov eax, dword ptr [eax + edx + 0x447930]
        _emit 0x84
        _emit 0x10
        _emit 0x30
        _emit 0x79
        _emit 0x44
        _emit 0x00
        _emit 0x99        ; 0040D278  cdq
        _emit 0x81        ; 0040D279  and edx, 0xffff
        _emit 0xE2
        _emit 0xFF
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x03        ; 0040D27F  add eax, edx
        _emit 0xC2
        _emit 0x8B        ; 0040D281  mov ebx, eax
        _emit 0xD8
        _emit 0x0F        ; 0040D283  movsx eax, word ptr [edi + 5]
        _emit 0xBF
        _emit 0x47
        _emit 0x05
        _emit 0xC1        ; 0040D287  sar ebx, 0x10
        _emit 0xFB
        _emit 0x10
        _emit 0x03        ; 0040D28A  add ebx, eax
        _emit 0xD8
        _emit 0x8B        ; 0040D28C  mov eax, dword ptr [esi + 0x14]
        _emit 0x46
        _emit 0x14
        _emit 0x99        ; 0040D28F  cdq
        _emit 0x81        ; 0040D290  and edx, 0xffff
        _emit 0xE2
        _emit 0xFF
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x03        ; 0040D296  add eax, edx
        _emit 0xC2
        _emit 0x0F        ; 0040D298  movsx edx, word ptr [edi + 7]
        _emit 0xBF
        _emit 0x57
        _emit 0x07
        _emit 0x8B        ; 0040D29C  mov esi, eax
        _emit 0xF0
        _emit 0x8B        ; 0040D29E  mov eax, dword ptr [ecx + 0x28]
        _emit 0x41
        _emit 0x28
        _emit 0xC1        ; 0040D2A1  sar esi, 0x10
        _emit 0xFE
        _emit 0x10
        _emit 0x03        ; 0040D2A4  add esi, edx
        _emit 0xF2
        _emit 0xA9        ; 0040D2A6  test eax, 0x40000000
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x40
        _emit 0xA1        ; 0040D2AB  mov eax, dword ptr [0x447f2c]
        _emit 0x2C
        _emit 0x7F
        _emit 0x44
        _emit 0x00
        _emit 0x74        ; 0040D2B0  je 0x40d2bc
        _emit 0x0A
        _emit 0x8B        ; 0040D2B2  mov edx, dword ptr [0x447f30]
        _emit 0x15
        _emit 0x30
        _emit 0x7F
        _emit 0x44
        _emit 0x00
        _emit 0x2B        ; 0040D2B8  sub ebx, eax
        _emit 0xD8
        _emit 0x2B        ; 0040D2BA  sub esi, edx
        _emit 0xF2
        _emit 0x8B        ; 0040D2BC  mov edi, dword ptr [0x447dad]
        _emit 0x3D
        _emit 0xAD
        _emit 0x7D
        _emit 0x44
        _emit 0x00
        _emit 0x8B        ; 0040D2C2  mov edx, dword ptr [0x447dc1]
        _emit 0x15
        _emit 0xC1
        _emit 0x7D
        _emit 0x44
        _emit 0x00
        _emit 0x03        ; 0040D2C8  add ebx, edi
        _emit 0xDF
        _emit 0x8B        ; 0040D2CA  mov edi, dword ptr [ecx + 0x68]
        _emit 0x79
        _emit 0x68
        _emit 0x8B        ; 0040D2CD  mov ecx, dword ptr [esp + 0x24]
        _emit 0x4C
        _emit 0x24
        _emit 0x24
        _emit 0x03        ; 0040D2D1  add esi, edx
        _emit 0xF2
        _emit 0xC1        ; 0040D2D3  shl edi, 4
        _emit 0xE7
        _emit 0x04
        _emit 0x8B        ; 0040D2D6  mov edx, dword ptr [ecx + 0x114]
        _emit 0x91
        _emit 0x14
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x89        ; 0040D2DC  mov dword ptr [esp + 0x44], esi
        _emit 0x74
        _emit 0x24
        _emit 0x44
        _emit 0x8A        ; 0040D2E0  mov cl, byte ptr [edi + edx + 1]
        _emit 0x4C
        _emit 0x17
        _emit 0x01
        _emit 0x03        ; 0040D2E4  add edi, edx
        _emit 0xFA
        _emit 0xF6        ; 0040D2E6  test cl, 8
        _emit 0xC1
        _emit 0x08
        _emit 0x74        ; 0040D2E9  je 0x40d330
        _emit 0x45
        _emit 0x66        ; 0040D2EB  mov bp, word ptr [edi + 2]
        _emit 0x8B
        _emit 0x6F
        _emit 0x02
        _emit 0x0F        ; 0040D2EF  movsx ecx, bp
        _emit 0xBF
        _emit 0xCD
        _emit 0x8B        ; 0040D2F2  mov edx, ecx
        _emit 0xD1
        _emit 0x0F        ; 0040D2F4  imul edx, eax
        _emit 0xAF
        _emit 0xD0
        _emit 0xB8        ; 0040D2F7  mov eax, 0xae147ae1
        _emit 0xE1
        _emit 0x7A
        _emit 0x14
        _emit 0xAE
        _emit 0xF7        ; 0040D2FC  imul edx
        _emit 0xEA
        _emit 0xC1        ; 0040D2FE  sar edx, 5
        _emit 0xFA
        _emit 0x05
        _emit 0x8B        ; 0040D301  mov eax, edx
        _emit 0xC2
        _emit 0xC1        ; 0040D303  shr eax, 0x1f
        _emit 0xE8
        _emit 0x1F
        _emit 0x03        ; 0040D306  add edx, eax
        _emit 0xD0
        _emit 0x03        ; 0040D308  add ebx, edx
        _emit 0xDA
        _emit 0x66        ; 0040D30A  test bp, bp
        _emit 0x85
        _emit 0xED
        _emit 0x7D        ; 0040D30D  jge 0x40d330
        _emit 0x21
        _emit 0x8B        ; 0040D30F  mov eax, ecx
        _emit 0xC1
        _emit 0x99        ; 0040D311  cdq
        _emit 0x8B        ; 0040D312  mov ebp, eax
        _emit 0xE8
        _emit 0xB8        ; 0040D314  mov eax, 0x66666667
        _emit 0x67
        _emit 0x66
        _emit 0x66
        _emit 0x66
        _emit 0x33        ; 0040D319  xor ebp, edx
        _emit 0xEA
        _emit 0x2B        ; 0040D31B  sub ebp, edx
        _emit 0xEA
        _emit 0xF7        ; 0040D31D  neg ebp
        _emit 0xDD
        _emit 0xC1        ; 0040D31F  shl ebp, 6
        _emit 0xE5
        _emit 0x06
        _emit 0xF7        ; 0040D322  imul ebp
        _emit 0xED
        _emit 0xC1        ; 0040D324  sar edx, 2
        _emit 0xFA
        _emit 0x02
        _emit 0x8B        ; 0040D327  mov ecx, edx
        _emit 0xCA
        _emit 0xC1        ; 0040D329  shr ecx, 0x1f
        _emit 0xE9
        _emit 0x1F
        _emit 0x03        ; 0040D32C  add edx, ecx
        _emit 0xD1
        _emit 0x03        ; 0040D32E  add ebx, edx
        _emit 0xDA
        _emit 0xF6        ; 0040D330  test byte ptr [edi + 1], 0x10
        _emit 0x47
        _emit 0x01
        _emit 0x10
        _emit 0x0F        ; 0040D334  je 0x40d456
        _emit 0x84
        _emit 0x1C
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x66        ; 0040D33A  mov di, word ptr [edi + 4]
        _emit 0x8B
        _emit 0x7F
        _emit 0x04
        _emit 0xB8        ; 0040D33E  mov eax, 0xae147ae1
        _emit 0xE1
        _emit 0x7A
        _emit 0x14
        _emit 0xAE
        _emit 0x0F        ; 0040D343  movsx ebp, di
        _emit 0xBF
        _emit 0xEF
        _emit 0x8B        ; 0040D346  mov ecx, ebp
        _emit 0xCD
        _emit 0x0F        ; 0040D348  imul ecx, dword ptr [0x447f30]
        _emit 0xAF
        _emit 0x0D
        _emit 0x30
        _emit 0x7F
        _emit 0x44
        _emit 0x00
        _emit 0xF7        ; 0040D34F  imul ecx
        _emit 0xE9
        _emit 0xC1        ; 0040D351  sar edx, 5
        _emit 0xFA
        _emit 0x05
        _emit 0x8B        ; 0040D354  mov eax, edx
        _emit 0xC2
        _emit 0xC1        ; 0040D356  shr eax, 0x1f
        _emit 0xE8
        _emit 0x1F
        _emit 0x03        ; 0040D359  add edx, eax
        _emit 0xD0
        _emit 0x03        ; 0040D35B  add esi, edx
        _emit 0xF2
        _emit 0x66        ; 0040D35D  test di, di
        _emit 0x85
        _emit 0xFF
        _emit 0x89        ; 0040D360  mov dword ptr [esp + 0x44], esi
        _emit 0x74
        _emit 0x24
        _emit 0x44
        _emit 0x0F        ; 0040D364  jge 0x40d456
        _emit 0x8D
        _emit 0xEC
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040D36A  mov eax, ebp
        _emit 0xC5
        _emit 0x99        ; 0040D36C  cdq
        _emit 0x33        ; 0040D36D  xor eax, edx
        _emit 0xC2
        _emit 0x2B        ; 0040D36F  sub eax, edx
        _emit 0xC2
        _emit 0x8D        ; 0040D371  lea ecx, [eax*4]
        _emit 0x0C
        _emit 0x85
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x2B        ; 0040D378  sub ecx, eax
        _emit 0xC8
        _emit 0xB8        ; 0040D37A  mov eax, 0x66666667
        _emit 0x67
        _emit 0x66
        _emit 0x66
        _emit 0x66
        _emit 0xF7        ; 0040D37F  neg ecx
        _emit 0xD9
        _emit 0xC1        ; 0040D381  shl ecx, 4
        _emit 0xE1
        _emit 0x04
        _emit 0xF7        ; 0040D384  imul ecx
        _emit 0xE9
        _emit 0xC1        ; 0040D386  sar edx, 2
        _emit 0xFA
        _emit 0x02
        _emit 0x8B        ; 0040D389  mov ecx, edx
        _emit 0xCA
        _emit 0xC1        ; 0040D38B  shr ecx, 0x1f
        _emit 0xE9
        _emit 0x1F
        _emit 0x03        ; 0040D38E  add edx, ecx
        _emit 0xD1
        _emit 0x03        ; 0040D390  add esi, edx
        _emit 0xF2
        _emit 0x89        ; 0040D392  mov dword ptr [esp + 0x44], esi
        _emit 0x74
        _emit 0x24
        _emit 0x44
        _emit 0xE9        ; 0040D396  jmp 0x40d456
        _emit 0xBB
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040D39B  mov esi, dword ptr [esp + 0x5c]
        _emit 0x74
        _emit 0x24
        _emit 0x5C
        _emit 0x8B        ; 0040D39F  mov edx, dword ptr [esp + 0x30]
        _emit 0x54
        _emit 0x24
        _emit 0x30
        _emit 0xF6        ; 0040D3A3  test byte ptr [esi + 0x18], 4
        _emit 0x46
        _emit 0x18
        _emit 0x04
        _emit 0x74        ; 0040D3A7  je 0x40d3e6
        _emit 0x3D
        _emit 0x8B        ; 0040D3A9  mov eax, dword ptr [esp + 0x2c]
        _emit 0x44
        _emit 0x24
        _emit 0x2C
        _emit 0x42        ; 0040D3AD  inc edx
        _emit 0xC1        ; 0040D3AE  shl edx, 4
        _emit 0xE2
        _emit 0x04
        _emit 0x8B        ; 0040D3B1  mov edi, dword ptr [esp + 0x3c]
        _emit 0x7C
        _emit 0x24
        _emit 0x3C
        _emit 0x8B        ; 0040D3B5  mov eax, dword ptr [edx + eax + 0x447930]
        _emit 0x84
        _emit 0x02
        _emit 0x30
        _emit 0x79
        _emit 0x44
        _emit 0x00
        _emit 0x99        ; 0040D3BC  cdq
        _emit 0x81        ; 0040D3BD  and edx, 0xffff
        _emit 0xE2
        _emit 0xFF
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x03        ; 0040D3C3  add eax, edx
        _emit 0xC2
        _emit 0x0F        ; 0040D3C5  movsx edx, word ptr [edi + 5]
        _emit 0xBF
        _emit 0x57
        _emit 0x05
        _emit 0x8B        ; 0040D3C9  mov ebx, eax
        _emit 0xD8
        _emit 0x8B        ; 0040D3CB  mov eax, dword ptr [esp + 0x40]
        _emit 0x44
        _emit 0x24
        _emit 0x40
        _emit 0xC1        ; 0040D3CF  sar ebx, 0x10
        _emit 0xFB
        _emit 0x10
        _emit 0x2B        ; 0040D3D2  sub ebx, edx
        _emit 0xDA
        _emit 0x8B        ; 0040D3D4  mov edx, dword ptr [eax + 4]
        _emit 0x50
        _emit 0x04
        _emit 0x8B        ; 0040D3D7  mov eax, dword ptr [esp + 0x34]
        _emit 0x44
        _emit 0x24
        _emit 0x34
        _emit 0x2B        ; 0040D3DB  sub ebx, edx
        _emit 0xDA
        _emit 0x83        ; 0040D3DD  xor eax, 1
        _emit 0xF0
        _emit 0x01
        _emit 0x89        ; 0040D3E0  mov dword ptr [esp + 0x34], eax
        _emit 0x44
        _emit 0x24
        _emit 0x34
        _emit 0xEB        ; 0040D3E4  jmp 0x40d40d
        _emit 0x27
        _emit 0x8B        ; 0040D3E6  mov eax, dword ptr [esp + 0x2c]
        _emit 0x44
        _emit 0x24
        _emit 0x2C
        _emit 0x42        ; 0040D3EA  inc edx
        _emit 0xC1        ; 0040D3EB  shl edx, 4
        _emit 0xE2
        _emit 0x04
        _emit 0x8B        ; 0040D3EE  mov edi, dword ptr [esp + 0x3c]
        _emit 0x7C
        _emit 0x24
        _emit 0x3C
        _emit 0x8B        ; 0040D3F2  mov eax, dword ptr [edx + eax + 0x447930]
        _emit 0x84
        _emit 0x02
        _emit 0x30
        _emit 0x79
        _emit 0x44
        _emit 0x00
        _emit 0x99        ; 0040D3F9  cdq
        _emit 0x81        ; 0040D3FA  and edx, 0xffff
        _emit 0xE2
        _emit 0xFF
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x03        ; 0040D400  add eax, edx
        _emit 0xC2
        _emit 0x0F        ; 0040D402  movsx edx, word ptr [edi + 5]
        _emit 0xBF
        _emit 0x57
        _emit 0x05
        _emit 0x8B        ; 0040D406  mov ebx, eax
        _emit 0xD8
        _emit 0xC1        ; 0040D408  sar ebx, 0x10
        _emit 0xFB
        _emit 0x10
        _emit 0x03        ; 0040D40B  add ebx, edx
        _emit 0xDA
        _emit 0x8B        ; 0040D40D  mov eax, dword ptr [esi + 0x14]
        _emit 0x46
        _emit 0x14
        _emit 0x99        ; 0040D410  cdq
        _emit 0x81        ; 0040D411  and edx, 0xffff
        _emit 0xE2
        _emit 0xFF
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x03        ; 0040D417  add eax, edx
        _emit 0xC2
        _emit 0x8B        ; 0040D419  mov esi, eax
        _emit 0xF0
        _emit 0x0F        ; 0040D41B  movsx eax, word ptr [edi + 7]
        _emit 0xBF
        _emit 0x47
        _emit 0x07
        _emit 0xC1        ; 0040D41F  sar esi, 0x10
        _emit 0xFE
        _emit 0x10
        _emit 0x03        ; 0040D422  add esi, eax
        _emit 0xF0
        _emit 0x8B        ; 0040D424  mov eax, dword ptr [ecx + 0x28]
        _emit 0x41
        _emit 0x28
        _emit 0xA9        ; 0040D427  test eax, 0x40000000
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x40
        _emit 0x74        ; 0040D42C  je 0x40d43d
        _emit 0x0F
        _emit 0x8B        ; 0040D42E  mov ecx, dword ptr [0x447f2c]
        _emit 0x0D
        _emit 0x2C
        _emit 0x7F
        _emit 0x44
        _emit 0x00
        _emit 0xA1        ; 0040D434  mov eax, dword ptr [0x447f30]
        _emit 0x30
        _emit 0x7F
        _emit 0x44
        _emit 0x00
        _emit 0x2B        ; 0040D439  sub ebx, ecx
        _emit 0xD9
        _emit 0x2B        ; 0040D43B  sub esi, eax
        _emit 0xF0
        _emit 0x8B        ; 0040D43D  mov ecx, dword ptr [0x447dad]
        _emit 0x0D
        _emit 0xAD
        _emit 0x7D
        _emit 0x44
        _emit 0x00
        _emit 0xA1        ; 0040D443  mov eax, dword ptr [0x447dc1]
        _emit 0xC1
        _emit 0x7D
        _emit 0x44
        _emit 0x00
        _emit 0x03        ; 0040D448  add ebx, ecx
        _emit 0xD9
        _emit 0x03        ; 0040D44A  add esi, eax
        _emit 0xF0
        _emit 0x89        ; 0040D44C  mov dword ptr [esp + 0x44], esi
        _emit 0x74
        _emit 0x24
        _emit 0x44
        _emit 0xEB        ; 0040D450  jmp 0x40d456
        _emit 0x04
        _emit 0x8B        ; 0040D452  mov esi, dword ptr [esp + 0x44]
        _emit 0x74
        _emit 0x24
        _emit 0x44
        _emit 0x8B        ; 0040D456  mov eax, dword ptr [esp + 0x6c]
        _emit 0x44
        _emit 0x24
        _emit 0x6C
        _emit 0x8B        ; 0040D45A  mov ebp, dword ptr [esp + 0x34]
        _emit 0x6C
        _emit 0x24
        _emit 0x34
        _emit 0x8B        ; 0040D45E  mov ecx, dword ptr [esp + 0x70]
        _emit 0x4C
        _emit 0x24
        _emit 0x70
        _emit 0x8B        ; 0040D462  mov edx, dword ptr [esp + 0x74]
        _emit 0x54
        _emit 0x24
        _emit 0x74
        _emit 0x25        ; 0040D466  and eax, 0xc000
        _emit 0x00
        _emit 0xC0
        _emit 0x00
        _emit 0x00
        _emit 0x03        ; 0040D46B  add eax, ebp
        _emit 0xC5
        _emit 0x50        ; 0040D46D  push eax
        _emit 0x8B        ; 0040D46E  mov eax, dword ptr [esp + 0x18]
        _emit 0x44
        _emit 0x24
        _emit 0x18
        _emit 0x51        ; 0040D472  push ecx
        _emit 0x8B        ; 0040D473  mov ecx, dword ptr [esp + 0x48]
        _emit 0x4C
        _emit 0x24
        _emit 0x48
        _emit 0x52        ; 0040D477  push edx
        _emit 0x56        ; 0040D478  push esi
        _emit 0x53        ; 0040D479  push ebx
        _emit 0x68        ; 0040D47A  push 0x4d1a20
        _emit 0x20
        _emit 0x1A
        _emit 0x4D
        _emit 0x00
        _emit 0x50        ; 0040D47F  push eax
        _emit 0x51        ; 0040D480  push ecx
        _emit 0xE8        ; 0040D481  call 0x40c140
        _emit 0xBA
        _emit 0xEC
        _emit 0xFF
        _emit 0xFF
        _emit 0x8B        ; 0040D486  mov ecx, dword ptr [0x4cfa00]
        _emit 0x0D
        _emit 0x00
        _emit 0xFA
        _emit 0x4C
        _emit 0x00
        _emit 0x8B        ; 0040D48C  mov edx, dword ptr [esp + 0x50]
        _emit 0x54
        _emit 0x24
        _emit 0x50
        _emit 0x8B        ; 0040D490  mov ebp, dword ptr [esp + 0x88]
        _emit 0xAC
        _emit 0x24
        _emit 0x88
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040D497  mov edi, dword ptr [esp + 0x4c]
        _emit 0x7C
        _emit 0x24
        _emit 0x4C
        _emit 0x8B        ; 0040D49B  mov esi, dword ptr [esp + 0x60]
        _emit 0x74
        _emit 0x24
        _emit 0x60
        _emit 0x83        ; 0040D49F  add esp, 0x20
        _emit 0xC4
        _emit 0x20
        _emit 0x89        ; 0040D4A2  mov dword ptr [esp + 0x64], esi
        _emit 0x74
        _emit 0x24
        _emit 0x64
        _emit 0x8B        ; 0040D4A6  mov eax, dword ptr [esp + 0x48]
        _emit 0x44
        _emit 0x24
        _emit 0x48
        _emit 0xBE        ; 0040D4AA  mov esi, 0x64
        _emit 0x64
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x40        ; 0040D4AF  inc eax
        _emit 0x89        ; 0040D4B0  mov dword ptr [esp + 0x48], eax
        _emit 0x44
        _emit 0x24
        _emit 0x48
        _emit 0x8D        ; 0040D4B4  lea eax, [edx + 1]
        _emit 0x42
        _emit 0x01
        _emit 0x99        ; 0040D4B7  cdq
        _emit 0xF7        ; 0040D4B8  idiv esi
        _emit 0xFE
        _emit 0x8B        ; 0040D4BA  mov esi, dword ptr [edi + 0x447938]
        _emit 0xB7
        _emit 0x38
        _emit 0x79
        _emit 0x44
        _emit 0x00
        _emit 0x33        ; 0040D4C0  xor eax, eax
        _emit 0xC0
        _emit 0x8A        ; 0040D4C2  mov al, byte ptr [esi + 3]
        _emit 0x46
        _emit 0x03
        _emit 0x8B        ; 0040D4C5  mov esi, dword ptr [esp + 0x48]
        _emit 0x74
        _emit 0x24
        _emit 0x48
        _emit 0x3B        ; 0040D4C9  cmp esi, eax
        _emit 0xF0
        _emit 0x89        ; 0040D4CB  mov dword ptr [esp + 0x30], edx
        _emit 0x54
        _emit 0x24
        _emit 0x30
        _emit 0x0F        ; 0040D4CF  jl 0x40cdbd
        _emit 0x8C
        _emit 0xE8
        _emit 0xF8
        _emit 0xFF
        _emit 0xFF
        _emit 0x8B        ; 0040D4D5  mov edx, dword ptr [esp + 0x78]
        _emit 0x54
        _emit 0x24
        _emit 0x78
        _emit 0x8B        ; 0040D4D9  mov eax, dword ptr [esp + 0x4c]
        _emit 0x44
        _emit 0x24
        _emit 0x4C
        _emit 0x8B        ; 0040D4DD  mov ebx, dword ptr [esp + 0x38]
        _emit 0x5C
        _emit 0x24
        _emit 0x38
        _emit 0x8B        ; 0040D4E1  mov esi, dword ptr [esp + 0x18]
        _emit 0x74
        _emit 0x24
        _emit 0x18
        _emit 0x89        ; 0040D4E5  mov dword ptr [ecx + 0x54], edx
        _emit 0x51
        _emit 0x54
        _emit 0x8B        ; 0040D4E8  mov edx, dword ptr [esp + 0x50]
        _emit 0x54
        _emit 0x24
        _emit 0x50
        _emit 0x89        ; 0040D4EC  mov dword ptr [ecx + 0x44], eax
        _emit 0x41
        _emit 0x44
        _emit 0x8B        ; 0040D4EF  mov eax, dword ptr [esp + 0x54]
        _emit 0x44
        _emit 0x24
        _emit 0x54
        _emit 0x89        ; 0040D4F3  mov dword ptr [ecx + 0x48], edx
        _emit 0x51
        _emit 0x48
        _emit 0x8B        ; 0040D4F6  mov edx, dword ptr [esp + 0x58]
        _emit 0x54
        _emit 0x24
        _emit 0x58
        _emit 0x89        ; 0040D4FA  mov dword ptr [ecx + 0x4c], eax
        _emit 0x41
        _emit 0x4C
        _emit 0x89        ; 0040D4FD  mov dword ptr [ecx + 0x50], edx
        _emit 0x51
        _emit 0x50
        _emit 0x8B        ; 0040D500  mov eax, dword ptr [esi + 0x10]
        _emit 0x46
        _emit 0x10
        _emit 0x85        ; 0040D503  test eax, eax
        _emit 0xC0
        _emit 0x74        ; 0040D505  je 0x40d54b
        _emit 0x44
        _emit 0x8B        ; 0040D507  mov ecx, dword ptr [0x425a44]
        _emit 0x0D
        _emit 0x44
        _emit 0x5A
        _emit 0x42
        _emit 0x00
        _emit 0x50        ; 0040D50D  push eax
        _emit 0x8B        ; 0040D50E  mov eax, dword ptr [esi]
        _emit 0x06
        _emit 0x50        ; 0040D510  push eax
        _emit 0x51        ; 0040D511  push ecx
        _emit 0xE8        ; 0040D512  call 0x4140c0
        _emit 0xA9
        _emit 0x6B
        _emit 0x00
        _emit 0x00
        _emit 0x8A        ; 0040D517  mov al, byte ptr [esi + 0xc]
        _emit 0x46
        _emit 0x0C
        _emit 0x83        ; 0040D51A  add esp, 0xc
        _emit 0xC4
        _emit 0x0C
        _emit 0xA8        ; 0040D51D  test al, 1
        _emit 0x01
        _emit 0x74        ; 0040D51F  je 0x40d539
        _emit 0x18
        _emit 0xA1        ; 0040D521  mov eax, dword ptr [0x425a44]
        _emit 0x44
        _emit 0x5A
        _emit 0x42
        _emit 0x00
        _emit 0x8B        ; 0040D526  mov ecx, dword ptr [0x4cfa00]
        _emit 0x0D
        _emit 0x00
        _emit 0xFA
        _emit 0x4C
        _emit 0x00
        _emit 0x8B        ; 0040D52C  mov ebx, eax
        _emit 0xD8
        _emit 0x05        ; 0040D52E  add eax, 0x400
        _emit 0x00
        _emit 0x04
        _emit 0x00
        _emit 0x00
        _emit 0x89        ; 0040D533  mov dword ptr [esp + 0x14], eax
        _emit 0x44
        _emit 0x24
        _emit 0x14
        _emit 0xEB        ; 0040D537  jmp 0x40d564
        _emit 0x2B
        _emit 0x8B        ; 0040D539  mov edx, dword ptr [0x425a44]
        _emit 0x15
        _emit 0x44
        _emit 0x5A
        _emit 0x42
        _emit 0x00
        _emit 0x8B        ; 0040D53F  mov ecx, dword ptr [0x4cfa00]
        _emit 0x0D
        _emit 0x00
        _emit 0xFA
        _emit 0x4C
        _emit 0x00
        _emit 0x89        ; 0040D545  mov dword ptr [esp + 0x14], edx
        _emit 0x54
        _emit 0x24
        _emit 0x14
        _emit 0xEB        ; 0040D549  jmp 0x40d564
        _emit 0x19
        _emit 0x8A        ; 0040D54B  mov dl, byte ptr [esi + 0xc]
        _emit 0x56
        _emit 0x0C
        _emit 0x8B        ; 0040D54E  mov eax, dword ptr [esi]
        _emit 0x06
        _emit 0xF6        ; 0040D550  test dl, 1
        _emit 0xC2
        _emit 0x01
        _emit 0x89        ; 0040D553  mov dword ptr [esp + 0x14], eax
        _emit 0x44
        _emit 0x24
        _emit 0x14
        _emit 0x74        ; 0040D557  je 0x40d564
        _emit 0x0B
        _emit 0x8B        ; 0040D559  mov ebx, eax
        _emit 0xD8
        _emit 0x05        ; 0040D55B  add eax, 0x400
        _emit 0x00
        _emit 0x04
        _emit 0x00
        _emit 0x00
        _emit 0x89        ; 0040D560  mov dword ptr [esp + 0x14], eax
        _emit 0x44
        _emit 0x24
        _emit 0x14
        _emit 0x8B        ; 0040D564  mov eax, dword ptr [ecx + 0x15a]
        _emit 0x81
        _emit 0x5A
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x83        ; 0040D56A  cmp eax, 5
        _emit 0xF8
        _emit 0x05
        _emit 0x77        ; 0040D56D  ja 0x40d5a4
        _emit 0x35
        _emit 0xFF        ; 0040D56F  jmp dword ptr [eax*4 + 0x40e42c]
        _emit 0x24
        _emit 0x85
        _emit 0x2C
        _emit 0xE4
        _emit 0x40
        _emit 0x00
        _emit 0x8B        ; 0040D576  mov eax, dword ptr [ecx + 0x156]
        _emit 0x81
        _emit 0x56
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x69        ; 0040D57C  imul eax, eax, 0xe03f
        _emit 0xC0
        _emit 0x3F
        _emit 0xE0
        _emit 0x00
        _emit 0x00
        _emit 0x05        ; 0040D582  add eax, 0x4dfd93
        _emit 0x93
        _emit 0xFD
        _emit 0x4D
        _emit 0x00
        _emit 0x50        ; 0040D587  push eax
        _emit 0xEB        ; 0040D588  jmp 0x40d596
        _emit 0x0C
        _emit 0x68        ; 0040D58A  push 0x4456d0
        _emit 0xD0
        _emit 0x56
        _emit 0x44
        _emit 0x00
        _emit 0xEB        ; 0040D58F  jmp 0x40d596
        _emit 0x05
        _emit 0x68        ; 0040D591  push 0x447d7d
        _emit 0x7D
        _emit 0x7D
        _emit 0x44
        _emit 0x00
        _emit 0xE8        ; 0040D596  call 0x40ca90
        _emit 0xF5
        _emit 0xF4
        _emit 0xFF
        _emit 0xFF
        _emit 0x8B        ; 0040D59B  mov ecx, dword ptr [0x4cfa00]
        _emit 0x0D
        _emit 0x00
        _emit 0xFA
        _emit 0x4C
        _emit 0x00
        _emit 0x83        ; 0040D5A1  add esp, 4
        _emit 0xC4
        _emit 0x04
        _emit 0x89        ; 0040D5A4  mov dword ptr [esp + 0x58], ebx
        _emit 0x5C
        _emit 0x24
        _emit 0x58
        _emit 0xBB        ; 0040D5A8  mov ebx, 0x4d1a20
        _emit 0x20
        _emit 0x1A
        _emit 0x4D
        _emit 0x00
        _emit 0xC7        ; 0040D5AD  mov dword ptr [esp + 0x64], 0x100
        _emit 0x44
        _emit 0x24
        _emit 0x64
        _emit 0x00
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040D5B5  mov edx, dword ptr [esp + 0x58]
        _emit 0x54
        _emit 0x24
        _emit 0x58
        _emit 0x8B        ; 0040D5B9  mov edx, dword ptr [edx]
        _emit 0x12
        _emit 0xF7        ; 0040D5BB  test edx, 0xffffff
        _emit 0xC2
        _emit 0xFF
        _emit 0xFF
        _emit 0xFF
        _emit 0x00
        _emit 0x0F        ; 0040D5C1  je 0x40d668
        _emit 0x84
        _emit 0xA1
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040D5C7  mov edi, dword ptr [ecx + 0x44]
        _emit 0x79
        _emit 0x44
        _emit 0x8B        ; 0040D5CA  mov eax, edx
        _emit 0xC2
        _emit 0xC1        ; 0040D5CC  shr eax, 0x10
        _emit 0xE8
        _emit 0x10
        _emit 0x25        ; 0040D5CF  and eax, 0xff
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0xC1        ; 0040D5D4  shr eax, 3
        _emit 0xE8
        _emit 0x03
        _emit 0x03        ; 0040D5D7  add eax, edi
        _emit 0xC7
        _emit 0x83        ; 0040D5D9  cmp eax, 0x1f
        _emit 0xF8
        _emit 0x1F
        _emit 0x7E        ; 0040D5DC  jle 0x40d5e5
        _emit 0x07
        _emit 0xB8        ; 0040D5DE  mov eax, 0x1f
        _emit 0x1F
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0xEB        ; 0040D5E3  jmp 0x40d5eb
        _emit 0x06
        _emit 0x85        ; 0040D5E5  test eax, eax
        _emit 0xC0
        _emit 0x7D        ; 0040D5E7  jge 0x40d5eb
        _emit 0x02
        _emit 0x33        ; 0040D5E9  xor eax, eax
        _emit 0xC0
        _emit 0x8B        ; 0040D5EB  mov esi, dword ptr [ecx + 0x48]
        _emit 0x71
        _emit 0x48
        _emit 0xC1        ; 0040D5EE  shr edx, 8
        _emit 0xEA
        _emit 0x08
        _emit 0x81        ; 0040D5F1  and edx, 0xff
        _emit 0xE2
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0xC1        ; 0040D5F7  shr edx, 3
        _emit 0xEA
        _emit 0x03
        _emit 0x03        ; 0040D5FA  add edx, esi
        _emit 0xD6
        _emit 0x8B        ; 0040D5FC  mov edi, edx
        _emit 0xFA
        _emit 0x83        ; 0040D5FE  cmp edi, 0x1f
        _emit 0xFF
        _emit 0x1F
        _emit 0x7E        ; 0040D601  jle 0x40d60a
        _emit 0x07
        _emit 0xBF        ; 0040D603  mov edi, 0x1f
        _emit 0x1F
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0xEB        ; 0040D608  jmp 0x40d610
        _emit 0x06
        _emit 0x85        ; 0040D60A  test edi, edi
        _emit 0xFF
        _emit 0x7D        ; 0040D60C  jge 0x40d610
        _emit 0x02
        _emit 0x33        ; 0040D60E  xor edi, edi
        _emit 0xFF
        _emit 0x8B        ; 0040D610  mov esi, dword ptr [esp + 0x58]
        _emit 0x74
        _emit 0x24
        _emit 0x58
        _emit 0x33        ; 0040D614  xor edx, edx
        _emit 0xD2
        _emit 0x8A        ; 0040D616  mov dl, byte ptr [esi]
        _emit 0x16
        _emit 0x8B        ; 0040D618  mov esi, edx
        _emit 0xF2
        _emit 0x8B        ; 0040D61A  mov edx, dword ptr [ecx + 0x4c]
        _emit 0x51
        _emit 0x4C
        _emit 0xC1        ; 0040D61D  shr esi, 3
        _emit 0xEE
        _emit 0x03
        _emit 0x03        ; 0040D620  add esi, edx
        _emit 0xF2
        _emit 0x83        ; 0040D622  cmp esi, 0x1f
        _emit 0xFE
        _emit 0x1F
        _emit 0x7E        ; 0040D625  jle 0x40d62e
        _emit 0x07
        _emit 0xBE        ; 0040D627  mov esi, 0x1f
        _emit 0x1F
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0xEB        ; 0040D62C  jmp 0x40d634
        _emit 0x06
        _emit 0x85        ; 0040D62E  test esi, esi
        _emit 0xF6
        _emit 0x7D        ; 0040D630  jge 0x40d634
        _emit 0x02
        _emit 0x33        ; 0040D632  xor esi, esi
        _emit 0xF6
        _emit 0x8D        ; 0040D634  lea edx, [esi + edi]
        _emit 0x14
        _emit 0x3E
        _emit 0x03        ; 0040D637  add edx, eax
        _emit 0xD0
        _emit 0x75        ; 0040D639  jne 0x40d640
        _emit 0x05
        _emit 0xBE        ; 0040D63B  mov esi, 1
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040D640  mov edx, dword ptr [0x424704]
        _emit 0x15
        _emit 0x04
        _emit 0x47
        _emit 0x42
        _emit 0x00
        _emit 0x85        ; 0040D646  test edx, edx
        _emit 0xD2
        _emit 0x74        ; 0040D648  je 0x40d659
        _emit 0x0F
        _emit 0xC1        ; 0040D64A  shl eax, 5
        _emit 0xE0
        _emit 0x05
        _emit 0x03        ; 0040D64D  add eax, edi
        _emit 0xC7
        _emit 0xC1        ; 0040D64F  shl eax, 6
        _emit 0xE0
        _emit 0x06
        _emit 0x03        ; 0040D652  add eax, esi
        _emit 0xC6
        _emit 0x66        ; 0040D654  mov word ptr [ebx], ax
        _emit 0x89
        _emit 0x03
        _emit 0xEB        ; 0040D657  jmp 0x40d66d
        _emit 0x14
        _emit 0xC1        ; 0040D659  shl eax, 5
        _emit 0xE0
        _emit 0x05
        _emit 0x03        ; 0040D65C  add eax, edi
        _emit 0xC7
        _emit 0xC1        ; 0040D65E  shl eax, 5
        _emit 0xE0
        _emit 0x05
        _emit 0x03        ; 0040D661  add eax, esi
        _emit 0xC6
        _emit 0x66        ; 0040D663  mov word ptr [ebx], ax
        _emit 0x89
        _emit 0x03
        _emit 0xEB        ; 0040D666  jmp 0x40d66d
        _emit 0x05
        _emit 0x66        ; 0040D668  mov word ptr [ebx], 0
        _emit 0xC7
        _emit 0x03
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040D66D  mov edx, dword ptr [esp + 0x58]
        _emit 0x54
        _emit 0x24
        _emit 0x58
        _emit 0x8B        ; 0040D671  mov eax, dword ptr [esp + 0x64]
        _emit 0x44
        _emit 0x24
        _emit 0x64
        _emit 0x83        ; 0040D675  add ebx, 2
        _emit 0xC3
        _emit 0x02
        _emit 0x83        ; 0040D678  add edx, 4
        _emit 0xC2
        _emit 0x04
        _emit 0x48        ; 0040D67B  dec eax
        _emit 0x89        ; 0040D67C  mov dword ptr [esp + 0x58], edx
        _emit 0x54
        _emit 0x24
        _emit 0x58
        _emit 0x89        ; 0040D680  mov dword ptr [esp + 0x64], eax
        _emit 0x44
        _emit 0x24
        _emit 0x64
        _emit 0x0F        ; 0040D684  jne 0x40d5b5
        _emit 0x85
        _emit 0x2B
        _emit 0xFF
        _emit 0xFF
        _emit 0xFF
        _emit 0x8B        ; 0040D68A  mov eax, dword ptr [ecx + 0x15a]
        _emit 0x81
        _emit 0x5A
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x83        ; 0040D690  cmp eax, 5
        _emit 0xF8
        _emit 0x05
        _emit 0x0F        ; 0040D693  ja 0x40e3e0
        _emit 0x87
        _emit 0x47
        _emit 0x0D
        _emit 0x00
        _emit 0x00
        _emit 0xFF        ; 0040D699  jmp dword ptr [eax*4 + 0x40e444]
        _emit 0x24
        _emit 0x85
        _emit 0x44
        _emit 0xE4
        _emit 0x40
        _emit 0x00
        _emit 0x83        ; 0040D6A0  cmp dword ptr [ecx + 0x40], -1
        _emit 0x79
        _emit 0x40
        _emit 0xFF
        _emit 0x0F        ; 0040D6A4  jne 0x40d7b0
        _emit 0x85
        _emit 0x06
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040D6AA  mov eax, dword ptr [esp + 0x24]
        _emit 0x44
        _emit 0x24
        _emit 0x24
        _emit 0x8B        ; 0040D6AE  mov ebx, dword ptr [esp + 0x20]
        _emit 0x5C
        _emit 0x24
        _emit 0x20
        _emit 0x8B        ; 0040D6B2  mov edx, dword ptr [eax + 0xdef9]
        _emit 0x90
        _emit 0xF9
        _emit 0xDE
        _emit 0x00
        _emit 0x00
        _emit 0x66        ; 0040D6B8  mov di, word ptr [ebx + 3]
        _emit 0x8B
        _emit 0x7B
        _emit 0x03
        _emit 0xF7        ; 0040D6BC  test edi, 0x4000
        _emit 0xC7
        _emit 0x00
        _emit 0x40
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040D6C2  mov eax, dword ptr [edx + 0x5c]
        _emit 0x42
        _emit 0x5C
        _emit 0x89        ; 0040D6C5  mov dword ptr [esp + 0x64], eax
        _emit 0x44
        _emit 0x24
        _emit 0x64
        _emit 0x74        ; 0040D6C9  je 0x40d6d2
        _emit 0x07
        _emit 0x83        ; 0040D6CB  xor eax, 1
        _emit 0xF0
        _emit 0x01
        _emit 0x89        ; 0040D6CE  mov dword ptr [esp + 0x64], eax
        _emit 0x44
        _emit 0x24
        _emit 0x64
        _emit 0x85        ; 0040D6D2  test eax, eax
        _emit 0xC0
        _emit 0x74        ; 0040D6D4  je 0x40d700
        _emit 0x2A
        _emit 0x8B        ; 0040D6D6  mov eax, dword ptr [ecx + 8]
        _emit 0x41
        _emit 0x08
        _emit 0x0F        ; 0040D6D9  movsx esi, word ptr [ebx + 5]
        _emit 0xBF
        _emit 0x73
        _emit 0x05
        _emit 0x99        ; 0040D6DD  cdq
        _emit 0x81        ; 0040D6DE  and edx, 0xffff
        _emit 0xE2
        _emit 0xFF
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x89        ; 0040D6E4  mov dword ptr [esp + 0x78], esi
        _emit 0x74
        _emit 0x24
        _emit 0x78
        _emit 0x03        ; 0040D6E8  add eax, edx
        _emit 0xC2
        _emit 0x8B        ; 0040D6EA  mov edx, dword ptr [esp + 0x18]
        _emit 0x54
        _emit 0x24
        _emit 0x18
        _emit 0x8B        ; 0040D6EE  mov esi, eax
        _emit 0xF0
        _emit 0x8B        ; 0040D6F0  mov eax, dword ptr [esp + 0x78]
        _emit 0x44
        _emit 0x24
        _emit 0x78
        _emit 0xC1        ; 0040D6F4  sar esi, 0x10
        _emit 0xFE
        _emit 0x10
        _emit 0x2B        ; 0040D6F7  sub esi, eax
        _emit 0xF0
        _emit 0x8B        ; 0040D6F9  mov eax, dword ptr [edx + 4]
        _emit 0x42
        _emit 0x04
        _emit 0x2B        ; 0040D6FC  sub esi, eax
        _emit 0xF0
        _emit 0xEB        ; 0040D6FE  jmp 0x40d717
        _emit 0x17
        _emit 0x8B        ; 0040D700  mov eax, dword ptr [ecx + 8]
        _emit 0x41
        _emit 0x08
        _emit 0x99        ; 0040D703  cdq
        _emit 0x81        ; 0040D704  and edx, 0xffff
        _emit 0xE2
        _emit 0xFF
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x03        ; 0040D70A  add eax, edx
        _emit 0xC2
        _emit 0x8B        ; 0040D70C  mov esi, eax
        _emit 0xF0
        _emit 0x0F        ; 0040D70E  movsx eax, word ptr [ebx + 5]
        _emit 0xBF
        _emit 0x43
        _emit 0x05
        _emit 0xC1        ; 0040D712  sar esi, 0x10
        _emit 0xFE
        _emit 0x10
        _emit 0x03        ; 0040D715  add esi, eax
        _emit 0xF0
        _emit 0xF7        ; 0040D717  test edi, 0x8000
        _emit 0xC7
        _emit 0x00
        _emit 0x80
        _emit 0x00
        _emit 0x00
        _emit 0x74        ; 0040D71D  je 0x40d73f
        _emit 0x20
        _emit 0x8B        ; 0040D71F  mov eax, dword ptr [ecx + 0xc]
        _emit 0x41
        _emit 0x0C
        _emit 0x0F        ; 0040D722  movsx ebx, word ptr [ebx + 7]
        _emit 0xBF
        _emit 0x5B
        _emit 0x07
        _emit 0x99        ; 0040D726  cdq
        _emit 0x81        ; 0040D727  and edx, 0xffff
        _emit 0xE2
        _emit 0xFF
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x03        ; 0040D72D  add eax, edx
        _emit 0xC2
        _emit 0x8B        ; 0040D72F  mov edx, dword ptr [esp + 0x18]
        _emit 0x54
        _emit 0x24
        _emit 0x18
        _emit 0xC1        ; 0040D733  sar eax, 0x10
        _emit 0xF8
        _emit 0x10
        _emit 0x2B        ; 0040D736  sub eax, ebx
        _emit 0xC3
        _emit 0x8B        ; 0040D738  mov ebx, dword ptr [edx + 8]
        _emit 0x5A
        _emit 0x08
        _emit 0x2B        ; 0040D73B  sub eax, ebx
        _emit 0xC3
        _emit 0xEB        ; 0040D73D  jmp 0x40d754
        _emit 0x15
        _emit 0x8B        ; 0040D73F  mov eax, dword ptr [ecx + 0xc]
        _emit 0x41
        _emit 0x0C
        _emit 0x99        ; 0040D742  cdq
        _emit 0x81        ; 0040D743  and edx, 0xffff
        _emit 0xE2
        _emit 0xFF
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x03        ; 0040D749  add eax, edx
        _emit 0xC2
        _emit 0x0F        ; 0040D74B  movsx edx, word ptr [ebx + 7]
        _emit 0xBF
        _emit 0x53
        _emit 0x07
        _emit 0xC1        ; 0040D74F  sar eax, 0x10
        _emit 0xF8
        _emit 0x10
        _emit 0x03        ; 0040D752  add eax, edx
        _emit 0xC2
        _emit 0xF7        ; 0040D754  test dword ptr [ecx + 0x28], 0x40000000
        _emit 0x41
        _emit 0x28
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x40
        _emit 0x74        ; 0040D75B  je 0x40d76d
        _emit 0x10
        _emit 0x8B        ; 0040D75D  mov edx, dword ptr [0x447f2c]
        _emit 0x15
        _emit 0x2C
        _emit 0x7F
        _emit 0x44
        _emit 0x00
        _emit 0x8B        ; 0040D763  mov ecx, dword ptr [0x447f30]
        _emit 0x0D
        _emit 0x30
        _emit 0x7F
        _emit 0x44
        _emit 0x00
        _emit 0x2B        ; 0040D769  sub esi, edx
        _emit 0xF2
        _emit 0x2B        ; 0040D76B  sub eax, ecx
        _emit 0xC1
        _emit 0x8B        ; 0040D76D  mov ecx, dword ptr [esp + 0x64]
        _emit 0x4C
        _emit 0x24
        _emit 0x64
        _emit 0x8B        ; 0040D771  mov edx, dword ptr [0x447dc1]
        _emit 0x15
        _emit 0xC1
        _emit 0x7D
        _emit 0x44
        _emit 0x00
        _emit 0x81        ; 0040D777  and edi, 0xc000
        _emit 0xE7
        _emit 0x00
        _emit 0xC0
        _emit 0x00
        _emit 0x00
        _emit 0x03        ; 0040D77D  add eax, edx
        _emit 0xC2
        _emit 0x0B        ; 0040D77F  or edi, ecx
        _emit 0xF9
        _emit 0x8B        ; 0040D781  mov ecx, dword ptr [esp + 0x28]
        _emit 0x4C
        _emit 0x24
        _emit 0x28
        _emit 0x57        ; 0040D785  push edi
        _emit 0x51        ; 0040D786  push ecx
        _emit 0x55        ; 0040D787  push ebp
        _emit 0x50        ; 0040D788  push eax
        _emit 0xA1        ; 0040D789  mov eax, dword ptr [0x447dad]
        _emit 0xAD
        _emit 0x7D
        _emit 0x44
        _emit 0x00
        _emit 0x03        ; 0040D78E  add esi, eax
        _emit 0xF0
        _emit 0x8B        ; 0040D790  mov ecx, dword ptr [esp + 0x24]
        _emit 0x4C
        _emit 0x24
        _emit 0x24
        _emit 0x8B        ; 0040D794  mov edx, dword ptr [esp + 0x28]
        _emit 0x54
        _emit 0x24
        _emit 0x28
        _emit 0x56        ; 0040D798  push esi
        _emit 0x68        ; 0040D799  push 0x4d1a20
        _emit 0x20
        _emit 0x1A
        _emit 0x4D
        _emit 0x00
        _emit 0x51        ; 0040D79E  push ecx
        _emit 0x52        ; 0040D79F  push edx
        _emit 0xE8        ; 0040D7A0  call 0x40c140
        _emit 0x9B
        _emit 0xE9
        _emit 0xFF
        _emit 0xFF
        _emit 0x83        ; 0040D7A5  add esp, 0x20
        _emit 0xC4
        _emit 0x20
        _emit 0x5F        ; 0040D7A8  pop edi
        _emit 0x5E        ; 0040D7A9  pop esi
        _emit 0x5D        ; 0040D7AA  pop ebp
        _emit 0x5B        ; 0040D7AB  pop ebx
        _emit 0x83        ; 0040D7AC  add esp, 0x6c
        _emit 0xC4
        _emit 0x6C
        _emit 0xC3        ; 0040D7AF  ret
        _emit 0x8B        ; 0040D7B0  mov eax, dword ptr [ecx + 0x5c]
        _emit 0x41
        _emit 0x5C
        _emit 0x8B        ; 0040D7B3  mov ebx, dword ptr [esp + 0x20]
        _emit 0x5C
        _emit 0x24
        _emit 0x20
        _emit 0x85        ; 0040D7B7  test eax, eax
        _emit 0xC0
        _emit 0x74        ; 0040D7B9  je 0x40d7f2
        _emit 0x37
        _emit 0xF6        ; 0040D7BB  test byte ptr [ebx + 9], 1
        _emit 0x43
        _emit 0x09
        _emit 0x01
        _emit 0x75        ; 0040D7BF  jne 0x40d7f2
        _emit 0x31
        _emit 0x8B        ; 0040D7C1  mov ebp, dword ptr [esp + 0x18]
        _emit 0x6C
        _emit 0x24
        _emit 0x18
        _emit 0x0F        ; 0040D7C5  movsx eax, word ptr [ebx + 5]
        _emit 0xBF
        _emit 0x43
        _emit 0x05
        _emit 0x8B        ; 0040D7C9  mov edi, dword ptr [ebp + 4]
        _emit 0x7D
        _emit 0x04
        _emit 0x8B        ; 0040D7CC  mov esi, edi
        _emit 0xF7
        _emit 0xD1        ; 0040D7CE  shr esi, 1
        _emit 0xEE
        _emit 0x2B        ; 0040D7D0  sub esi, eax
        _emit 0xF0
        _emit 0x8B        ; 0040D7D2  mov eax, dword ptr [ecx + 8]
        _emit 0x41
        _emit 0x08
        _emit 0x99        ; 0040D7D5  cdq
        _emit 0x81        ; 0040D7D6  and edx, 0xffff
        _emit 0xE2
        _emit 0xFF
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x03        ; 0040D7DC  add eax, edx
        _emit 0xC2
        _emit 0xC1        ; 0040D7DE  sar eax, 0x10
        _emit 0xF8
        _emit 0x10
        _emit 0x03        ; 0040D7E1  add esi, eax
        _emit 0xF0
        _emit 0x8B        ; 0040D7E3  mov eax, dword ptr [esp + 0x1c]
        _emit 0x44
        _emit 0x24
        _emit 0x1C
        _emit 0x2B        ; 0040D7E7  sub esi, edi
        _emit 0xF7
        _emit 0x83        ; 0040D7E9  xor eax, 1
        _emit 0xF0
        _emit 0x01
        _emit 0x89        ; 0040D7EC  mov dword ptr [esp + 0x1c], eax
        _emit 0x44
        _emit 0x24
        _emit 0x1C
        _emit 0xEB        ; 0040D7F0  jmp 0x40d812
        _emit 0x20
        _emit 0x8B        ; 0040D7F2  mov ebp, dword ptr [esp + 0x18]
        _emit 0x6C
        _emit 0x24
        _emit 0x18
        _emit 0x8B        ; 0040D7F6  mov eax, dword ptr [ecx + 8]
        _emit 0x41
        _emit 0x08
        _emit 0x0F        ; 0040D7F9  movsx esi, word ptr [ebx + 5]
        _emit 0xBF
        _emit 0x73
        _emit 0x05
        _emit 0x8B        ; 0040D7FD  mov edx, dword ptr [ebp + 4]
        _emit 0x55
        _emit 0x04
        _emit 0xD1        ; 0040D800  shr edx, 1
        _emit 0xEA
        _emit 0x2B        ; 0040D802  sub esi, edx
        _emit 0xF2
        _emit 0x99        ; 0040D804  cdq
        _emit 0x81        ; 0040D805  and edx, 0xffff
        _emit 0xE2
        _emit 0xFF
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x03        ; 0040D80B  add eax, edx
        _emit 0xC2
        _emit 0xC1        ; 0040D80D  sar eax, 0x10
        _emit 0xF8
        _emit 0x10
        _emit 0x03        ; 0040D810  add esi, eax
        _emit 0xF0
        _emit 0x8B        ; 0040D812  mov eax, dword ptr [ecx + 0xc]
        _emit 0x41
        _emit 0x0C
        _emit 0x0F        ; 0040D815  movsx edi, word ptr [ebx + 7]
        _emit 0xBF
        _emit 0x7B
        _emit 0x07
        _emit 0x99        ; 0040D819  cdq
        _emit 0x81        ; 0040D81A  and edx, 0xffff
        _emit 0xE2
        _emit 0xFF
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x03        ; 0040D820  add eax, edx
        _emit 0xC2
        _emit 0x8B        ; 0040D822  mov edx, dword ptr [ebp + 8]
        _emit 0x55
        _emit 0x08
        _emit 0xC1        ; 0040D825  sar eax, 0x10
        _emit 0xF8
        _emit 0x10
        _emit 0x03        ; 0040D828  add edi, eax
        _emit 0xF8
        _emit 0x8B        ; 0040D82A  mov eax, dword ptr [ecx + 0x28]
        _emit 0x41
        _emit 0x28
        _emit 0x2B        ; 0040D82D  sub edi, edx
        _emit 0xFA
        _emit 0xA9        ; 0040D82F  test eax, 0x40000000
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x40
        _emit 0x74        ; 0040D834  je 0x40d854
        _emit 0x1E
        _emit 0xA1        ; 0040D836  mov eax, dword ptr [0x447f2c]
        _emit 0x2C
        _emit 0x7F
        _emit 0x44
        _emit 0x00
        _emit 0x8B        ; 0040D83B  mov edx, dword ptr [0x447dad]
        _emit 0x15
        _emit 0xAD
        _emit 0x7D
        _emit 0x44
        _emit 0x00
        _emit 0x03        ; 0040D841  add edx, eax
        _emit 0xD0
        _emit 0xA1        ; 0040D843  mov eax, dword ptr [0x447f30]
        _emit 0x30
        _emit 0x7F
        _emit 0x44
        _emit 0x00
        _emit 0x2B        ; 0040D848  sub esi, edx
        _emit 0xF2
        _emit 0x8B        ; 0040D84A  mov edx, dword ptr [0x447dc1]
        _emit 0x15
        _emit 0xC1
        _emit 0x7D
        _emit 0x44
        _emit 0x00
        _emit 0x03        ; 0040D850  add edx, eax
        _emit 0xD0
        _emit 0x2B        ; 0040D852  sub edi, edx
        _emit 0xFA
        _emit 0xA1        ; 0040D854  mov eax, dword ptr [0x42470c]
        _emit 0x0C
        _emit 0x47
        _emit 0x42
        _emit 0x00
        _emit 0x85        ; 0040D859  test eax, eax
        _emit 0xC0
        _emit 0x89        ; 0040D85B  mov dword ptr [esp + 0x78], eax
        _emit 0x44
        _emit 0x24
        _emit 0x78
        _emit 0x74        ; 0040D85F  je 0x40d8c3
        _emit 0x62
        _emit 0x8B        ; 0040D861  mov ebp, dword ptr [esp + 0x1c]
        _emit 0x6C
        _emit 0x24
        _emit 0x1C
        _emit 0xBB        ; 0040D865  mov ebx, 0x4c
        _emit 0x4C
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0xEB        ; 0040D86A  jmp 0x40d872
        _emit 0x06
        _emit 0x8B        ; 0040D86C  mov ecx, dword ptr [0x4cfa00]
        _emit 0x0D
        _emit 0x00
        _emit 0xFA
        _emit 0x4C
        _emit 0x00
        _emit 0xA1        ; 0040D872  mov eax, dword ptr [0x41eda4]
        _emit 0xA4
        _emit 0xED
        _emit 0x41
        _emit 0x00
        _emit 0x8B        ; 0040D877  mov ecx, dword ptr [ebx + ecx + 0xd9]
        _emit 0x8C
        _emit 0x0B
        _emit 0xD9
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x55        ; 0040D87E  push ebp
        _emit 0x50        ; 0040D87F  push eax
        _emit 0x51        ; 0040D880  push ecx
        _emit 0xE8        ; 0040D881  call 0x40c860
        _emit 0xDA
        _emit 0xEF
        _emit 0xFF
        _emit 0xFF
        _emit 0x8B        ; 0040D886  mov edx, ebx
        _emit 0xD3
        _emit 0x83        ; 0040D888  add esp, 0xc
        _emit 0xC4
        _emit 0x0C
        _emit 0x83        ; 0040D88B  sub ebx, 4
        _emit 0xEB
        _emit 0x04
        _emit 0x85        ; 0040D88E  test edx, edx
        _emit 0xD2
        _emit 0x75        ; 0040D890  jne 0x40d86c
        _emit 0xDA
        _emit 0xBB        ; 0040D892  mov ebx, 0x4c
        _emit 0x4C
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0xEB        ; 0040D897  jmp 0x40d89d
        _emit 0x04
        _emit 0x8B        ; 0040D899  mov ebp, dword ptr [esp + 0x1c]
        _emit 0x6C
        _emit 0x24
        _emit 0x1C
        _emit 0x8B        ; 0040D89D  mov ecx, dword ptr [0x4cfa00]
        _emit 0x0D
        _emit 0x00
        _emit 0xFA
        _emit 0x4C
        _emit 0x00
        _emit 0xA1        ; 0040D8A3  mov eax, dword ptr [0x41eda0]
        _emit 0xA0
        _emit 0xED
        _emit 0x41
        _emit 0x00
        _emit 0x55        ; 0040D8A8  push ebp
        _emit 0x50        ; 0040D8A9  push eax
        _emit 0x8B        ; 0040D8AA  mov edx, dword ptr [ebx + ecx + 0x89]
        _emit 0x94
        _emit 0x0B
        _emit 0x89
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x52        ; 0040D8B1  push edx
        _emit 0xE8        ; 0040D8B2  call 0x40c860
        _emit 0xA9
        _emit 0xEF
        _emit 0xFF
        _emit 0xFF
        _emit 0x8B        ; 0040D8B7  mov eax, ebx
        _emit 0xC3
        _emit 0x83        ; 0040D8B9  add esp, 0xc
        _emit 0xC4
        _emit 0x0C
        _emit 0x83        ; 0040D8BC  sub ebx, 4
        _emit 0xEB
        _emit 0x04
        _emit 0x85        ; 0040D8BF  test eax, eax
        _emit 0xC0
        _emit 0x75        ; 0040D8C1  jne 0x40d899
        _emit 0xD6
        _emit 0x8B        ; 0040D8C3  mov ecx, dword ptr [esp + 0x20]
        _emit 0x4C
        _emit 0x24
        _emit 0x20
        _emit 0x8B        ; 0040D8C7  mov ebx, dword ptr [esp + 0x1c]
        _emit 0x5C
        _emit 0x24
        _emit 0x1C
        _emit 0x8B        ; 0040D8CB  mov eax, dword ptr [esp + 0x28]
        _emit 0x44
        _emit 0x24
        _emit 0x28
        _emit 0x66        ; 0040D8CF  mov dx, word ptr [ecx + 3]
        _emit 0x8B
        _emit 0x51
        _emit 0x03
        _emit 0x8B        ; 0040D8D3  mov ecx, dword ptr [esp + 0x68]
        _emit 0x4C
        _emit 0x24
        _emit 0x68
        _emit 0x81        ; 0040D8D7  and edx, 0xc000
        _emit 0xE2
        _emit 0x00
        _emit 0xC0
        _emit 0x00
        _emit 0x00
        _emit 0x03        ; 0040D8DD  add edx, ebx
        _emit 0xD3
        _emit 0x52        ; 0040D8DF  push edx
        _emit 0x8B        ; 0040D8E0  mov edx, dword ptr [esp + 0x18]
        _emit 0x54
        _emit 0x24
        _emit 0x18
        _emit 0x50        ; 0040D8E4  push eax
        _emit 0x8B        ; 0040D8E5  mov eax, dword ptr [esp + 0x20]
        _emit 0x44
        _emit 0x24
        _emit 0x20
        _emit 0x51        ; 0040D8E9  push ecx
        _emit 0x57        ; 0040D8EA  push edi
        _emit 0x56        ; 0040D8EB  push esi
        _emit 0x68        ; 0040D8EC  push 0x4d1a20
        _emit 0x20
        _emit 0x1A
        _emit 0x4D
        _emit 0x00
        _emit 0x52        ; 0040D8F1  push edx
        _emit 0x50        ; 0040D8F2  push eax
        _emit 0xE8        ; 0040D8F3  call 0x40c140
        _emit 0x48
        _emit 0xE8
        _emit 0xFF
        _emit 0xFF
        _emit 0x8B        ; 0040D8F8  mov eax, dword ptr [esp + 0x98]
        _emit 0x84
        _emit 0x24
        _emit 0x98
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x83        ; 0040D8FF  add esp, 0x20
        _emit 0xC4
        _emit 0x20
        _emit 0x85        ; 0040D902  test eax, eax
        _emit 0xC0
        _emit 0x0F        ; 0040D904  je 0x40e3e0
        _emit 0x84
        _emit 0xD6
        _emit 0x0A
        _emit 0x00
        _emit 0x00
        _emit 0xBE        ; 0040D90A  mov esi, 0x4c
        _emit 0x4C
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040D90F  mov edx, dword ptr [0x4cfa00]
        _emit 0x15
        _emit 0x00
        _emit 0xFA
        _emit 0x4C
        _emit 0x00
        _emit 0x8B        ; 0040D915  mov ecx, dword ptr [0x41eda4]
        _emit 0x0D
        _emit 0xA4
        _emit 0xED
        _emit 0x41
        _emit 0x00
        _emit 0x53        ; 0040D91B  push ebx
        _emit 0x51        ; 0040D91C  push ecx
        _emit 0x8B        ; 0040D91D  mov eax, dword ptr [esi + edx + 0xd9]
        _emit 0x84
        _emit 0x16
        _emit 0xD9
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x50        ; 0040D924  push eax
        _emit 0xE8        ; 0040D925  call 0x40c900
        _emit 0xD6
        _emit 0xEF
        _emit 0xFF
        _emit 0xFF
        _emit 0x8B        ; 0040D92A  mov ecx, esi
        _emit 0xCE
        _emit 0x83        ; 0040D92C  add esp, 0xc
        _emit 0xC4
        _emit 0x0C
        _emit 0x83        ; 0040D92F  sub esi, 4
        _emit 0xEE
        _emit 0x04
        _emit 0x85        ; 0040D932  test ecx, ecx
        _emit 0xC9
        _emit 0x75        ; 0040D934  jne 0x40d90f
        _emit 0xD9
        _emit 0xBE        ; 0040D936  mov esi, 0x4c
        _emit 0x4C
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0xA1        ; 0040D93B  mov eax, dword ptr [0x4cfa00]
        _emit 0x00
        _emit 0xFA
        _emit 0x4C
        _emit 0x00
        _emit 0x8B        ; 0040D940  mov edx, dword ptr [0x41eda0]
        _emit 0x15
        _emit 0xA0
        _emit 0xED
        _emit 0x41
        _emit 0x00
        _emit 0x53        ; 0040D946  push ebx
        _emit 0x52        ; 0040D947  push edx
        _emit 0x8B        ; 0040D948  mov ecx, dword ptr [esi + eax + 0x89]
        _emit 0x8C
        _emit 0x06
        _emit 0x89
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x51        ; 0040D94F  push ecx
        _emit 0xE8        ; 0040D950  call 0x40c900
        _emit 0xAB
        _emit 0xEF
        _emit 0xFF
        _emit 0xFF
        _emit 0x8B        ; 0040D955  mov edx, esi
        _emit 0xD6
        _emit 0x83        ; 0040D957  add esp, 0xc
        _emit 0xC4
        _emit 0x0C
        _emit 0x83        ; 0040D95A  sub esi, 4
        _emit 0xEE
        _emit 0x04
        _emit 0x85        ; 0040D95D  test edx, edx
        _emit 0xD2
        _emit 0x75        ; 0040D95F  jne 0x40d93b
        _emit 0xDA
        _emit 0x8B        ; 0040D961  mov ecx, dword ptr [0x4cfa00]
        _emit 0x0D
        _emit 0x00
        _emit 0xFA
        _emit 0x4C
        _emit 0x00
        _emit 0x8B        ; 0040D967  mov eax, dword ptr [ecx + 0x15a]
        _emit 0x81
        _emit 0x5A
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x85        ; 0040D96D  test eax, eax
        _emit 0xC0
        _emit 0x0F        ; 0040D96F  jne 0x40e3e0
        _emit 0x85
        _emit 0x6B
        _emit 0x0A
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040D975  mov eax, dword ptr [ecx + 0x15e]
        _emit 0x81
        _emit 0x5E
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x52        ; 0040D97B  push edx
        _emit 0xC1        ; 0040D97C  sar eax, 2
        _emit 0xF8
        _emit 0x02
        _emit 0x83        ; 0040D97F  and eax, 3
        _emit 0xE0
        _emit 0x03
        _emit 0x52        ; 0040D982  push edx
        _emit 0x83        ; 0040D983  add eax, 4
        _emit 0xC0
        _emit 0x04
        _emit 0x52        ; 0040D986  push edx
        _emit 0x52        ; 0040D987  push edx
        _emit 0x6A        ; 0040D988  push 0x60
        _emit 0x60
        _emit 0xC1        ; 0040D98A  shl eax, 6
        _emit 0xE0
        _emit 0x06
        _emit 0x50        ; 0040D98D  push eax
        _emit 0x8B        ; 0040D98E  mov eax, dword ptr [ecx + 0xc]
        _emit 0x41
        _emit 0x0C
        _emit 0x99        ; 0040D991  cdq
        _emit 0x81        ; 0040D992  and edx, 0xffff
        _emit 0xE2
        _emit 0xFF
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x6A        ; 0040D998  push 0x20
        _emit 0x20
        _emit 0x03        ; 0040D99A  add eax, edx
        _emit 0xC2
        _emit 0x8B        ; 0040D99C  mov edx, dword ptr [0x447f30]
        _emit 0x15
        _emit 0x30
        _emit 0x7F
        _emit 0x44
        _emit 0x00
        _emit 0xC1        ; 0040D9A2  sar eax, 0x10
        _emit 0xF8
        _emit 0x10
        _emit 0x2B        ; 0040D9A5  sub eax, edx
        _emit 0xC2
        _emit 0x6A        ; 0040D9A7  push 0x40
        _emit 0x40
        _emit 0x83        ; 0040D9A9  sub eax, 0x40
        _emit 0xE8
        _emit 0x40
        _emit 0x50        ; 0040D9AC  push eax
        _emit 0x8B        ; 0040D9AD  mov eax, dword ptr [ecx + 8]
        _emit 0x41
        _emit 0x08
        _emit 0x8B        ; 0040D9B0  mov ecx, dword ptr [0x447f2c]
        _emit 0x0D
        _emit 0x2C
        _emit 0x7F
        _emit 0x44
        _emit 0x00
        _emit 0x99        ; 0040D9B6  cdq
        _emit 0x81        ; 0040D9B7  and edx, 0xffff
        _emit 0xE2
        _emit 0xFF
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x03        ; 0040D9BD  add eax, edx
        _emit 0xC2
        _emit 0xC1        ; 0040D9BF  sar eax, 0x10
        _emit 0xF8
        _emit 0x10
        _emit 0x2B        ; 0040D9C2  sub eax, ecx
        _emit 0xC1
        _emit 0x83        ; 0040D9C4  sub eax, 0x20
        _emit 0xE8
        _emit 0x20
        _emit 0x50        ; 0040D9C7  push eax
        _emit 0x68        ; 0040D9C8  push 0x424f74
        _emit 0x74
        _emit 0x4F
        _emit 0x42
        _emit 0x00
        _emit 0xE8        ; 0040D9CD  call 0x40b4c0
        _emit 0xEE
        _emit 0xDA
        _emit 0xFF
        _emit 0xFF
        _emit 0x8B        ; 0040D9D2  mov ecx, dword ptr [0x4cfa00]
        _emit 0x0D
        _emit 0x00
        _emit 0xFA
        _emit 0x4C
        _emit 0x00
        _emit 0x6A        ; 0040D9D8  push 0
        _emit 0x00
        _emit 0x6A        ; 0040D9DA  push 0
        _emit 0x00
        _emit 0x6A        ; 0040D9DC  push 0
        _emit 0x00
        _emit 0x8B        ; 0040D9DE  mov edx, dword ptr [ecx + 0x15e]
        _emit 0x91
        _emit 0x5E
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040D9E4  mov eax, dword ptr [ecx + 0xc]
        _emit 0x41
        _emit 0x0C
        _emit 0x83        ; 0040D9E7  and edx, 3
        _emit 0xE2
        _emit 0x03
        _emit 0x6A        ; 0040D9EA  push 0
        _emit 0x00
        _emit 0x83        ; 0040D9EC  add edx, 4
        _emit 0xC2
        _emit 0x04
        _emit 0x6A        ; 0040D9EF  push 0x40
        _emit 0x40
        _emit 0xC1        ; 0040D9F1  shl edx, 6
        _emit 0xE2
        _emit 0x06
        _emit 0x52        ; 0040D9F4  push edx
        _emit 0x8B        ; 0040D9F5  mov edi, dword ptr [0x447f30]
        _emit 0x3D
        _emit 0x30
        _emit 0x7F
        _emit 0x44
        _emit 0x00
        _emit 0x99        ; 0040D9FB  cdq
        _emit 0x81        ; 0040D9FC  and edx, 0xffff
        _emit 0xE2
        _emit 0xFF
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x6A        ; 0040DA02  push 0x20
        _emit 0x20
        _emit 0x03        ; 0040DA04  add eax, edx
        _emit 0xC2
        _emit 0x6A        ; 0040DA06  push 0x40
        _emit 0x40
        _emit 0xC1        ; 0040DA08  sar eax, 0x10
        _emit 0xF8
        _emit 0x10
        _emit 0x2B        ; 0040DA0B  sub eax, edi
        _emit 0xC7
        _emit 0x8B        ; 0040DA0D  mov edi, dword ptr [0x447f2c]
        _emit 0x3D
        _emit 0x2C
        _emit 0x7F
        _emit 0x44
        _emit 0x00
        _emit 0x83        ; 0040DA13  sub eax, 0x20
        _emit 0xE8
        _emit 0x20
        _emit 0x50        ; 0040DA16  push eax
        _emit 0x8B        ; 0040DA17  mov eax, dword ptr [ecx + 8]
        _emit 0x41
        _emit 0x08
        _emit 0x99        ; 0040DA1A  cdq
        _emit 0x81        ; 0040DA1B  and edx, 0xffff
        _emit 0xE2
        _emit 0xFF
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x03        ; 0040DA21  add eax, edx
        _emit 0xC2
        _emit 0xC1        ; 0040DA23  sar eax, 0x10
        _emit 0xF8
        _emit 0x10
        _emit 0x2B        ; 0040DA26  sub eax, edi
        _emit 0xC7
        _emit 0x83        ; 0040DA28  sub eax, 0x20
        _emit 0xE8
        _emit 0x20
        _emit 0x50        ; 0040DA2B  push eax
        _emit 0x68        ; 0040DA2C  push 0x424f74
        _emit 0x74
        _emit 0x4F
        _emit 0x42
        _emit 0x00
        _emit 0xE8        ; 0040DA31  call 0x40b4c0
        _emit 0x8A
        _emit 0xDA
        _emit 0xFF
        _emit 0xFF
        _emit 0x83        ; 0040DA36  add esp, 0x58
        _emit 0xC4
        _emit 0x58
        _emit 0x5F        ; 0040DA39  pop edi
        _emit 0x5E        ; 0040DA3A  pop esi
        _emit 0x5D        ; 0040DA3B  pop ebp
        _emit 0x5B        ; 0040DA3C  pop ebx
        _emit 0x83        ; 0040DA3D  add esp, 0x6c
        _emit 0xC4
        _emit 0x6C
        _emit 0xC3        ; 0040DA40  ret
        _emit 0x8B        ; 0040DA41  mov eax, dword ptr [ecx + 0x5c]
        _emit 0x41
        _emit 0x5C
        _emit 0x85        ; 0040DA44  test eax, eax
        _emit 0xC0
        _emit 0x74        ; 0040DA46  je 0x40da79
        _emit 0x31
        _emit 0x8B        ; 0040DA48  mov eax, dword ptr [ecx + 8]
        _emit 0x41
        _emit 0x08
        _emit 0x8B        ; 0040DA4B  mov ebx, dword ptr [esp + 0x20]
        _emit 0x5C
        _emit 0x24
        _emit 0x20
        _emit 0x99        ; 0040DA4F  cdq
        _emit 0x0F        ; 0040DA50  movsx edi, word ptr [ebx + 5]
        _emit 0xBF
        _emit 0x7B
        _emit 0x05
        _emit 0x81        ; 0040DA54  and edx, 0xffff
        _emit 0xE2
        _emit 0xFF
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x03        ; 0040DA5A  add eax, edx
        _emit 0xC2
        _emit 0x8B        ; 0040DA5C  mov esi, eax
        _emit 0xF0
        _emit 0x8B        ; 0040DA5E  mov eax, dword ptr [esp + 0x18]
        _emit 0x44
        _emit 0x24
        _emit 0x18
        _emit 0xC1        ; 0040DA62  sar esi, 0x10
        _emit 0xFE
        _emit 0x10
        _emit 0x8B        ; 0040DA65  mov edx, dword ptr [eax + 4]
        _emit 0x50
        _emit 0x04
        _emit 0x8B        ; 0040DA68  mov eax, dword ptr [esp + 0x1c]
        _emit 0x44
        _emit 0x24
        _emit 0x1C
        _emit 0x2B        ; 0040DA6C  sub esi, edi
        _emit 0xF7
        _emit 0x2B        ; 0040DA6E  sub esi, edx
        _emit 0xF2
        _emit 0x83        ; 0040DA70  xor eax, 1
        _emit 0xF0
        _emit 0x01
        _emit 0x89        ; 0040DA73  mov dword ptr [esp + 0x1c], eax
        _emit 0x44
        _emit 0x24
        _emit 0x1C
        _emit 0xEB        ; 0040DA77  jmp 0x40da94
        _emit 0x1B
        _emit 0x8B        ; 0040DA79  mov eax, dword ptr [ecx + 8]
        _emit 0x41
        _emit 0x08
        _emit 0x8B        ; 0040DA7C  mov ebx, dword ptr [esp + 0x20]
        _emit 0x5C
        _emit 0x24
        _emit 0x20
        _emit 0x99        ; 0040DA80  cdq
        _emit 0x81        ; 0040DA81  and edx, 0xffff
        _emit 0xE2
        _emit 0xFF
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x03        ; 0040DA87  add eax, edx
        _emit 0xC2
        _emit 0x0F        ; 0040DA89  movsx edx, word ptr [ebx + 5]
        _emit 0xBF
        _emit 0x53
        _emit 0x05
        _emit 0x8B        ; 0040DA8D  mov esi, eax
        _emit 0xF0
        _emit 0xC1        ; 0040DA8F  sar esi, 0x10
        _emit 0xFE
        _emit 0x10
        _emit 0x03        ; 0040DA92  add esi, edx
        _emit 0xF2
        _emit 0xA1        ; 0040DA94  mov eax, dword ptr [0x447dad]
        _emit 0xAD
        _emit 0x7D
        _emit 0x44
        _emit 0x00
        _emit 0x03        ; 0040DA99  add esi, eax
        _emit 0xF0
        _emit 0x8B        ; 0040DA9B  mov eax, dword ptr [ecx + 0xc]
        _emit 0x41
        _emit 0x0C
        _emit 0x99        ; 0040DA9E  cdq
        _emit 0x0F        ; 0040DA9F  movsx edi, word ptr [ebx + 7]
        _emit 0xBF
        _emit 0x7B
        _emit 0x07
        _emit 0x81        ; 0040DAA3  and edx, 0xffff
        _emit 0xE2
        _emit 0xFF
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040DAA9  mov ecx, dword ptr [ecx + 0x68]
        _emit 0x49
        _emit 0x68
        _emit 0x03        ; 0040DAAC  add eax, edx
        _emit 0xC2
        _emit 0x8B        ; 0040DAAE  mov edx, dword ptr [0x447dc1]
        _emit 0x15
        _emit 0xC1
        _emit 0x7D
        _emit 0x44
        _emit 0x00
        _emit 0xC1        ; 0040DAB4  sar eax, 0x10
        _emit 0xF8
        _emit 0x10
        _emit 0x03        ; 0040DAB7  add edx, eax
        _emit 0xD0
        _emit 0x8B        ; 0040DAB9  mov eax, dword ptr [esp + 0x24]
        _emit 0x44
        _emit 0x24
        _emit 0x24
        _emit 0x03        ; 0040DABD  add edi, edx
        _emit 0xFA
        _emit 0x8B        ; 0040DABF  mov edx, dword ptr [eax + 0x114]
        _emit 0x90
        _emit 0x14
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0xC1        ; 0040DAC5  shl ecx, 4
        _emit 0xE1
        _emit 0x04
        _emit 0x03        ; 0040DAC8  add ecx, edx
        _emit 0xCA
        _emit 0xF6        ; 0040DACA  test byte ptr [ecx + 1], 8
        _emit 0x41
        _emit 0x01
        _emit 0x08
        _emit 0x74        ; 0040DACE  je 0x40db21
        _emit 0x51
        _emit 0x66        ; 0040DAD0  mov bx, word ptr [ecx + 2]
        _emit 0x8B
        _emit 0x59
        _emit 0x02
        _emit 0xB8        ; 0040DAD4  mov eax, 0xae147ae1
        _emit 0xE1
        _emit 0x7A
        _emit 0x14
        _emit 0xAE
        _emit 0x0F        ; 0040DAD9  movsx edx, bx
        _emit 0xBF
        _emit 0xD3
        _emit 0x89        ; 0040DADC  mov dword ptr [esp + 0x78], edx
        _emit 0x54
        _emit 0x24
        _emit 0x78
        _emit 0x0F        ; 0040DAE0  imul edx, dword ptr [0x447f2c]
        _emit 0xAF
        _emit 0x15
        _emit 0x2C
        _emit 0x7F
        _emit 0x44
        _emit 0x00
        _emit 0xF7        ; 0040DAE7  imul edx
        _emit 0xEA
        _emit 0xC1        ; 0040DAE9  sar edx, 5
        _emit 0xFA
        _emit 0x05
        _emit 0x8B        ; 0040DAEC  mov eax, edx
        _emit 0xC2
        _emit 0xC1        ; 0040DAEE  shr eax, 0x1f
        _emit 0xE8
        _emit 0x1F
        _emit 0x03        ; 0040DAF1  add edx, eax
        _emit 0xD0
        _emit 0x03        ; 0040DAF3  add esi, edx
        _emit 0xF2
        _emit 0x66        ; 0040DAF5  test bx, bx
        _emit 0x85
        _emit 0xDB
        _emit 0x7D        ; 0040DAF8  jge 0x40db1d
        _emit 0x23
        _emit 0x8B        ; 0040DAFA  mov eax, dword ptr [esp + 0x78]
        _emit 0x44
        _emit 0x24
        _emit 0x78
        _emit 0x99        ; 0040DAFE  cdq
        _emit 0x8B        ; 0040DAFF  mov ebx, eax
        _emit 0xD8
        _emit 0xB8        ; 0040DB01  mov eax, 0x66666667
        _emit 0x67
        _emit 0x66
        _emit 0x66
        _emit 0x66
        _emit 0x33        ; 0040DB06  xor ebx, edx
        _emit 0xDA
        _emit 0x2B        ; 0040DB08  sub ebx, edx
        _emit 0xDA
        _emit 0xF7        ; 0040DB0A  neg ebx
        _emit 0xDB
        _emit 0xC1        ; 0040DB0C  shl ebx, 6
        _emit 0xE3
        _emit 0x06
        _emit 0xF7        ; 0040DB0F  imul ebx
        _emit 0xEB
        _emit 0xC1        ; 0040DB11  sar edx, 2
        _emit 0xFA
        _emit 0x02
        _emit 0x8B        ; 0040DB14  mov eax, edx
        _emit 0xC2
        _emit 0xC1        ; 0040DB16  shr eax, 0x1f
        _emit 0xE8
        _emit 0x1F
        _emit 0x03        ; 0040DB19  add edx, eax
        _emit 0xD0
        _emit 0x03        ; 0040DB1B  add esi, edx
        _emit 0xF2
        _emit 0x8B        ; 0040DB1D  mov ebx, dword ptr [esp + 0x20]
        _emit 0x5C
        _emit 0x24
        _emit 0x20
        _emit 0x8A        ; 0040DB21  mov al, byte ptr [ecx + 1]
        _emit 0x41
        _emit 0x01
        _emit 0xA8        ; 0040DB24  test al, 0x10
        _emit 0x10
        _emit 0x88        ; 0040DB26  mov byte ptr [esp + 0x13], al
        _emit 0x44
        _emit 0x24
        _emit 0x13
        _emit 0x74        ; 0040DB2A  je 0x40db84
        _emit 0x58
        _emit 0x66        ; 0040DB2C  mov cx, word ptr [ecx + 4]
        _emit 0x8B
        _emit 0x49
        _emit 0x04
        _emit 0xB8        ; 0040DB30  mov eax, 0xae147ae1
        _emit 0xE1
        _emit 0x7A
        _emit 0x14
        _emit 0xAE
        _emit 0x0F        ; 0040DB35  movsx edx, cx
        _emit 0xBF
        _emit 0xD1
        _emit 0x89        ; 0040DB38  mov dword ptr [esp + 0x78], edx
        _emit 0x54
        _emit 0x24
        _emit 0x78
        _emit 0x0F        ; 0040DB3C  imul edx, dword ptr [0x447f30]
        _emit 0xAF
        _emit 0x15
        _emit 0x30
        _emit 0x7F
        _emit 0x44
        _emit 0x00
        _emit 0xF7        ; 0040DB43  imul edx
        _emit 0xEA
        _emit 0xC1        ; 0040DB45  sar edx, 5
        _emit 0xFA
        _emit 0x05
        _emit 0x8B        ; 0040DB48  mov eax, edx
        _emit 0xC2
        _emit 0xC1        ; 0040DB4A  shr eax, 0x1f
        _emit 0xE8
        _emit 0x1F
        _emit 0x03        ; 0040DB4D  add edx, eax
        _emit 0xD0
        _emit 0x03        ; 0040DB4F  add edi, edx
        _emit 0xFA
        _emit 0x66        ; 0040DB51  test cx, cx
        _emit 0x85
        _emit 0xC9
        _emit 0x7D        ; 0040DB54  jge 0x40db80
        _emit 0x2A
        _emit 0x8B        ; 0040DB56  mov eax, dword ptr [esp + 0x78]
        _emit 0x44
        _emit 0x24
        _emit 0x78
        _emit 0x99        ; 0040DB5A  cdq
        _emit 0x33        ; 0040DB5B  xor eax, edx
        _emit 0xC2
        _emit 0x2B        ; 0040DB5D  sub eax, edx
        _emit 0xC2
        _emit 0x8D        ; 0040DB5F  lea ecx, [eax*4]
        _emit 0x0C
        _emit 0x85
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x2B        ; 0040DB66  sub ecx, eax
        _emit 0xC8
        _emit 0xB8        ; 0040DB68  mov eax, 0x66666667
        _emit 0x67
        _emit 0x66
        _emit 0x66
        _emit 0x66
        _emit 0xF7        ; 0040DB6D  neg ecx
        _emit 0xD9
        _emit 0xC1        ; 0040DB6F  shl ecx, 4
        _emit 0xE1
        _emit 0x04
        _emit 0xF7        ; 0040DB72  imul ecx
        _emit 0xE9
        _emit 0xC1        ; 0040DB74  sar edx, 2
        _emit 0xFA
        _emit 0x02
        _emit 0x8B        ; 0040DB77  mov ecx, edx
        _emit 0xCA
        _emit 0xC1        ; 0040DB79  shr ecx, 0x1f
        _emit 0xE9
        _emit 0x1F
        _emit 0x03        ; 0040DB7C  add edx, ecx
        _emit 0xD1
        _emit 0x03        ; 0040DB7E  add edi, edx
        _emit 0xFA
        _emit 0x8A        ; 0040DB80  mov al, byte ptr [esp + 0x13]
        _emit 0x44
        _emit 0x24
        _emit 0x13
        _emit 0xA8        ; 0040DB84  test al, 2
        _emit 0x02
        _emit 0x0F        ; 0040DB86  je 0x40dc6d
        _emit 0x84
        _emit 0xE1
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0xA8        ; 0040DB8C  test al, 4
        _emit 0x04
        _emit 0x0F        ; 0040DB8E  je 0x40dc15
        _emit 0x84
        _emit 0x81
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x85        ; 0040DB94  test esi, esi
        _emit 0xF6
        _emit 0x7E        ; 0040DB96  jle 0x40db9e
        _emit 0x06
        _emit 0x2B        ; 0040DB98  sub esi, ebp
        _emit 0xF5
        _emit 0x85        ; 0040DB9A  test esi, esi
        _emit 0xF6
        _emit 0x7F        ; 0040DB9C  jg 0x40db98
        _emit 0xFA
        _emit 0x85        ; 0040DB9E  test edi, edi
        _emit 0xFF
        _emit 0x7E        ; 0040DBA0  jle 0x40dbac
        _emit 0x0A
        _emit 0x8B        ; 0040DBA2  mov eax, dword ptr [esp + 0x28]
        _emit 0x44
        _emit 0x24
        _emit 0x28
        _emit 0x2B        ; 0040DBA6  sub edi, eax
        _emit 0xF8
        _emit 0x85        ; 0040DBA8  test edi, edi
        _emit 0xFF
        _emit 0x7F        ; 0040DBAA  jg 0x40dba6
        _emit 0xFA
        _emit 0x81        ; 0040DBAC  cmp edi, 0x1e0
        _emit 0xFF
        _emit 0xE0
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040DBB2  mov ebx, edi
        _emit 0xDF
        _emit 0x0F        ; 0040DBB4  jge 0x40e3e0
        _emit 0x8D
        _emit 0x26
        _emit 0x08
        _emit 0x00
        _emit 0x00
        _emit 0x81        ; 0040DBBA  cmp esi, 0x280
        _emit 0xFE
        _emit 0x80
        _emit 0x02
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040DBC0  mov edi, esi
        _emit 0xFE
        _emit 0x7D        ; 0040DBC2  jge 0x40dc01
        _emit 0x3D
        _emit 0x8B        ; 0040DBC4  mov edx, dword ptr [esp + 0x20]
        _emit 0x54
        _emit 0x24
        _emit 0x20
        _emit 0x8B        ; 0040DBC8  mov ecx, dword ptr [esp + 0x28]
        _emit 0x4C
        _emit 0x24
        _emit 0x28
        _emit 0x66        ; 0040DBCC  mov ax, word ptr [edx + 3]
        _emit 0x8B
        _emit 0x42
        _emit 0x03
        _emit 0x8B        ; 0040DBD0  mov edx, dword ptr [esp + 0x1c]
        _emit 0x54
        _emit 0x24
        _emit 0x1C
        _emit 0x25        ; 0040DBD4  and eax, 0xc000
        _emit 0x00
        _emit 0xC0
        _emit 0x00
        _emit 0x00
        _emit 0x03        ; 0040DBD9  add eax, edx
        _emit 0xC2
        _emit 0x8B        ; 0040DBDB  mov edx, dword ptr [esp + 0x14]
        _emit 0x54
        _emit 0x24
        _emit 0x14
        _emit 0x50        ; 0040DBDF  push eax
        _emit 0x8B        ; 0040DBE0  mov eax, dword ptr [esp + 0x1c]
        _emit 0x44
        _emit 0x24
        _emit 0x1C
        _emit 0x51        ; 0040DBE4  push ecx
        _emit 0x55        ; 0040DBE5  push ebp
        _emit 0x53        ; 0040DBE6  push ebx
        _emit 0x57        ; 0040DBE7  push edi
        _emit 0x68        ; 0040DBE8  push 0x4d1a20
        _emit 0x20
        _emit 0x1A
        _emit 0x4D
        _emit 0x00
        _emit 0x52        ; 0040DBED  push edx
        _emit 0x50        ; 0040DBEE  push eax
        _emit 0xE8        ; 0040DBEF  call 0x40c140
        _emit 0x4C
        _emit 0xE5
        _emit 0xFF
        _emit 0xFF
        _emit 0x03        ; 0040DBF4  add edi, ebp
        _emit 0xFD
        _emit 0x83        ; 0040DBF6  add esp, 0x20
        _emit 0xC4
        _emit 0x20
        _emit 0x81        ; 0040DBF9  cmp edi, 0x280
        _emit 0xFF
        _emit 0x80
        _emit 0x02
        _emit 0x00
        _emit 0x00
        _emit 0x7C        ; 0040DBFF  jl 0x40dbc4
        _emit 0xC3
        _emit 0x03        ; 0040DC01  add ebx, dword ptr [esp + 0x28]
        _emit 0x5C
        _emit 0x24
        _emit 0x28
        _emit 0x81        ; 0040DC05  cmp ebx, 0x1e0
        _emit 0xFB
        _emit 0xE0
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x7C        ; 0040DC0B  jl 0x40dbba
        _emit 0xAD
        _emit 0x5F        ; 0040DC0D  pop edi
        _emit 0x5E        ; 0040DC0E  pop esi
        _emit 0x5D        ; 0040DC0F  pop ebp
        _emit 0x5B        ; 0040DC10  pop ebx
        _emit 0x83        ; 0040DC11  add esp, 0x6c
        _emit 0xC4
        _emit 0x6C
        _emit 0xC3        ; 0040DC14  ret
        _emit 0x85        ; 0040DC15  test esi, esi
        _emit 0xF6
        _emit 0x7E        ; 0040DC17  jle 0x40dc1f
        _emit 0x06
        _emit 0x2B        ; 0040DC19  sub esi, ebp
        _emit 0xF5
        _emit 0x85        ; 0040DC1B  test esi, esi
        _emit 0xF6
        _emit 0x7F        ; 0040DC1D  jg 0x40dc19
        _emit 0xFA
        _emit 0x81        ; 0040DC1F  cmp esi, 0x280
        _emit 0xFE
        _emit 0x80
        _emit 0x02
        _emit 0x00
        _emit 0x00
        _emit 0x0F        ; 0040DC25  jge 0x40e3e0
        _emit 0x8D
        _emit 0xB5
        _emit 0x07
        _emit 0x00
        _emit 0x00
        _emit 0x66        ; 0040DC2B  mov cx, word ptr [ebx + 3]
        _emit 0x8B
        _emit 0x4B
        _emit 0x03
        _emit 0x8B        ; 0040DC2F  mov eax, dword ptr [esp + 0x1c]
        _emit 0x44
        _emit 0x24
        _emit 0x1C
        _emit 0x8B        ; 0040DC33  mov edx, dword ptr [esp + 0x28]
        _emit 0x54
        _emit 0x24
        _emit 0x28
        _emit 0x81        ; 0040DC37  and ecx, 0xc000
        _emit 0xE1
        _emit 0x00
        _emit 0xC0
        _emit 0x00
        _emit 0x00
        _emit 0x03        ; 0040DC3D  add ecx, eax
        _emit 0xC8
        _emit 0x8B        ; 0040DC3F  mov eax, dword ptr [esp + 0x14]
        _emit 0x44
        _emit 0x24
        _emit 0x14
        _emit 0x51        ; 0040DC43  push ecx
        _emit 0x8B        ; 0040DC44  mov ecx, dword ptr [esp + 0x1c]
        _emit 0x4C
        _emit 0x24
        _emit 0x1C
        _emit 0x52        ; 0040DC48  push edx
        _emit 0x55        ; 0040DC49  push ebp
        _emit 0x57        ; 0040DC4A  push edi
        _emit 0x56        ; 0040DC4B  push esi
        _emit 0x68        ; 0040DC4C  push 0x4d1a20
        _emit 0x20
        _emit 0x1A
        _emit 0x4D
        _emit 0x00
        _emit 0x50        ; 0040DC51  push eax
        _emit 0x51        ; 0040DC52  push ecx
        _emit 0xE8        ; 0040DC53  call 0x40c140
        _emit 0xE8
        _emit 0xE4
        _emit 0xFF
        _emit 0xFF
        _emit 0x03        ; 0040DC58  add esi, ebp
        _emit 0xF5
        _emit 0x83        ; 0040DC5A  add esp, 0x20
        _emit 0xC4
        _emit 0x20
        _emit 0x81        ; 0040DC5D  cmp esi, 0x280
        _emit 0xFE
        _emit 0x80
        _emit 0x02
        _emit 0x00
        _emit 0x00
        _emit 0x7C        ; 0040DC63  jl 0x40dc2b
        _emit 0xC6
        _emit 0x5F        ; 0040DC65  pop edi
        _emit 0x5E        ; 0040DC66  pop esi
        _emit 0x5D        ; 0040DC67  pop ebp
        _emit 0x5B        ; 0040DC68  pop ebx
        _emit 0x83        ; 0040DC69  add esp, 0x6c
        _emit 0xC4
        _emit 0x6C
        _emit 0xC3        ; 0040DC6C  ret
        _emit 0xA8        ; 0040DC6D  test al, 4
        _emit 0x04
        _emit 0x74        ; 0040DC6F  je 0x40dccc
        _emit 0x5B
        _emit 0x8B        ; 0040DC71  mov ebx, dword ptr [esp + 0x28]
        _emit 0x5C
        _emit 0x24
        _emit 0x28
        _emit 0x85        ; 0040DC75  test edi, edi
        _emit 0xFF
        _emit 0x7E        ; 0040DC77  jle 0x40dc7f
        _emit 0x06
        _emit 0x2B        ; 0040DC79  sub edi, ebx
        _emit 0xFB
        _emit 0x85        ; 0040DC7B  test edi, edi
        _emit 0xFF
        _emit 0x7F        ; 0040DC7D  jg 0x40dc79
        _emit 0xFA
        _emit 0x81        ; 0040DC7F  cmp edi, 0x1e0
        _emit 0xFF
        _emit 0xE0
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x0F        ; 0040DC85  jge 0x40e3e0
        _emit 0x8D
        _emit 0x55
        _emit 0x07
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040DC8B  mov edx, dword ptr [esp + 0x20]
        _emit 0x54
        _emit 0x24
        _emit 0x20
        _emit 0x8B        ; 0040DC8F  mov ecx, dword ptr [esp + 0x1c]
        _emit 0x4C
        _emit 0x24
        _emit 0x1C
        _emit 0x66        ; 0040DC93  mov ax, word ptr [edx + 3]
        _emit 0x8B
        _emit 0x42
        _emit 0x03
        _emit 0x8B        ; 0040DC97  mov edx, dword ptr [esp + 0x18]
        _emit 0x54
        _emit 0x24
        _emit 0x18
        _emit 0x25        ; 0040DC9B  and eax, 0xc000
        _emit 0x00
        _emit 0xC0
        _emit 0x00
        _emit 0x00
        _emit 0x03        ; 0040DCA0  add eax, ecx
        _emit 0xC1
        _emit 0x8B        ; 0040DCA2  mov ecx, dword ptr [esp + 0x14]
        _emit 0x4C
        _emit 0x24
        _emit 0x14
        _emit 0x50        ; 0040DCA6  push eax
        _emit 0x53        ; 0040DCA7  push ebx
        _emit 0x55        ; 0040DCA8  push ebp
        _emit 0x57        ; 0040DCA9  push edi
        _emit 0x56        ; 0040DCAA  push esi
        _emit 0x68        ; 0040DCAB  push 0x4d1a20
        _emit 0x20
        _emit 0x1A
        _emit 0x4D
        _emit 0x00
        _emit 0x51        ; 0040DCB0  push ecx
        _emit 0x52        ; 0040DCB1  push edx
        _emit 0xE8        ; 0040DCB2  call 0x40c140
        _emit 0x89
        _emit 0xE4
        _emit 0xFF
        _emit 0xFF
        _emit 0x03        ; 0040DCB7  add edi, ebx
        _emit 0xFB
        _emit 0x83        ; 0040DCB9  add esp, 0x20
        _emit 0xC4
        _emit 0x20
        _emit 0x81        ; 0040DCBC  cmp edi, 0x1e0
        _emit 0xFF
        _emit 0xE0
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x7C        ; 0040DCC2  jl 0x40dc8b
        _emit 0xC7
        _emit 0x5F        ; 0040DCC4  pop edi
        _emit 0x5E        ; 0040DCC5  pop esi
        _emit 0x5D        ; 0040DCC6  pop ebp
        _emit 0x5B        ; 0040DCC7  pop ebx
        _emit 0x83        ; 0040DCC8  add esp, 0x6c
        _emit 0xC4
        _emit 0x6C
        _emit 0xC3        ; 0040DCCB  ret
        _emit 0x66        ; 0040DCCC  mov ax, word ptr [ebx + 3]
        _emit 0x8B
        _emit 0x43
        _emit 0x03
        _emit 0x8B        ; 0040DCD0  mov ecx, dword ptr [esp + 0x1c]
        _emit 0x4C
        _emit 0x24
        _emit 0x1C
        _emit 0x25        ; 0040DCD4  and eax, 0xc000
        _emit 0x00
        _emit 0xC0
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040DCD9  mov edx, dword ptr [esp + 0x14]
        _emit 0x54
        _emit 0x24
        _emit 0x14
        _emit 0x03        ; 0040DCDD  add eax, ecx
        _emit 0xC1
        _emit 0x8B        ; 0040DCDF  mov ecx, dword ptr [esp + 0x28]
        _emit 0x4C
        _emit 0x24
        _emit 0x28
        _emit 0x50        ; 0040DCE3  push eax
        _emit 0x8B        ; 0040DCE4  mov eax, dword ptr [esp + 0x1c]
        _emit 0x44
        _emit 0x24
        _emit 0x1C
        _emit 0x51        ; 0040DCE8  push ecx
        _emit 0x55        ; 0040DCE9  push ebp
        _emit 0x57        ; 0040DCEA  push edi
        _emit 0x56        ; 0040DCEB  push esi
        _emit 0x68        ; 0040DCEC  push 0x4d1a20
        _emit 0x20
        _emit 0x1A
        _emit 0x4D
        _emit 0x00
        _emit 0x52        ; 0040DCF1  push edx
        _emit 0x50        ; 0040DCF2  push eax
        _emit 0xE8        ; 0040DCF3  call 0x40c140
        _emit 0x48
        _emit 0xE4
        _emit 0xFF
        _emit 0xFF
        _emit 0x83        ; 0040DCF8  add esp, 0x20
        _emit 0xC4
        _emit 0x20
        _emit 0x5F        ; 0040DCFB  pop edi
        _emit 0x5E        ; 0040DCFC  pop esi
        _emit 0x5D        ; 0040DCFD  pop ebp
        _emit 0x5B        ; 0040DCFE  pop ebx
        _emit 0x83        ; 0040DCFF  add esp, 0x6c
        _emit 0xC4
        _emit 0x6C
        _emit 0xC3        ; 0040DD02  ret
        _emit 0x8B        ; 0040DD03  mov eax, dword ptr [ecx + 0x5c]
        _emit 0x41
        _emit 0x5C
        _emit 0x85        ; 0040DD06  test eax, eax
        _emit 0xC0
        _emit 0x74        ; 0040DD08  je 0x40dd3b
        _emit 0x31
        _emit 0x8B        ; 0040DD0A  mov eax, dword ptr [ecx + 8]
        _emit 0x41
        _emit 0x08
        _emit 0x8B        ; 0040DD0D  mov ebx, dword ptr [esp + 0x20]
        _emit 0x5C
        _emit 0x24
        _emit 0x20
        _emit 0x99        ; 0040DD11  cdq
        _emit 0x0F        ; 0040DD12  movsx edi, word ptr [ebx + 5]
        _emit 0xBF
        _emit 0x7B
        _emit 0x05
        _emit 0x81        ; 0040DD16  and edx, 0xffff
        _emit 0xE2
        _emit 0xFF
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x03        ; 0040DD1C  add eax, edx
        _emit 0xC2
        _emit 0x8B        ; 0040DD1E  mov edx, dword ptr [esp + 0x18]
        _emit 0x54
        _emit 0x24
        _emit 0x18
        _emit 0x8B        ; 0040DD22  mov esi, eax
        _emit 0xF0
        _emit 0x8B        ; 0040DD24  mov eax, dword ptr [esp + 0x1c]
        _emit 0x44
        _emit 0x24
        _emit 0x1C
        _emit 0xC1        ; 0040DD28  sar esi, 0x10
        _emit 0xFE
        _emit 0x10
        _emit 0x2B        ; 0040DD2B  sub esi, edi
        _emit 0xF7
        _emit 0x8B        ; 0040DD2D  mov edi, dword ptr [edx + 4]
        _emit 0x7A
        _emit 0x04
        _emit 0x2B        ; 0040DD30  sub esi, edi
        _emit 0xF7
        _emit 0x83        ; 0040DD32  xor eax, 1
        _emit 0xF0
        _emit 0x01
        _emit 0x89        ; 0040DD35  mov dword ptr [esp + 0x1c], eax
        _emit 0x44
        _emit 0x24
        _emit 0x1C
        _emit 0xEB        ; 0040DD39  jmp 0x40dd56
        _emit 0x1B
        _emit 0x8B        ; 0040DD3B  mov eax, dword ptr [ecx + 8]
        _emit 0x41
        _emit 0x08
        _emit 0x8B        ; 0040DD3E  mov ebx, dword ptr [esp + 0x20]
        _emit 0x5C
        _emit 0x24
        _emit 0x20
        _emit 0x99        ; 0040DD42  cdq
        _emit 0x81        ; 0040DD43  and edx, 0xffff
        _emit 0xE2
        _emit 0xFF
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x03        ; 0040DD49  add eax, edx
        _emit 0xC2
        _emit 0x8B        ; 0040DD4B  mov esi, eax
        _emit 0xF0
        _emit 0x0F        ; 0040DD4D  movsx eax, word ptr [ebx + 5]
        _emit 0xBF
        _emit 0x43
        _emit 0x05
        _emit 0xC1        ; 0040DD51  sar esi, 0x10
        _emit 0xFE
        _emit 0x10
        _emit 0x03        ; 0040DD54  add esi, eax
        _emit 0xF0
        _emit 0x8B        ; 0040DD56  mov eax, dword ptr [ecx + 0xc]
        _emit 0x41
        _emit 0x0C
        _emit 0x99        ; 0040DD59  cdq
        _emit 0x81        ; 0040DD5A  and edx, 0xffff
        _emit 0xE2
        _emit 0xFF
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x03        ; 0040DD60  add eax, edx
        _emit 0xC2
        _emit 0x0F        ; 0040DD62  movsx edx, word ptr [ebx + 7]
        _emit 0xBF
        _emit 0x53
        _emit 0x07
        _emit 0x8B        ; 0040DD66  mov edi, eax
        _emit 0xF8
        _emit 0x8B        ; 0040DD68  mov eax, dword ptr [ecx + 0x28]
        _emit 0x41
        _emit 0x28
        _emit 0xC1        ; 0040DD6B  sar edi, 0x10
        _emit 0xFF
        _emit 0x10
        _emit 0x03        ; 0040DD6E  add edi, edx
        _emit 0xFA
        _emit 0xA9        ; 0040DD70  test eax, 0x40000000
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x40
        _emit 0x74        ; 0040DD75  je 0x40dd95
        _emit 0x1E
        _emit 0xA1        ; 0040DD77  mov eax, dword ptr [0x447f2c]
        _emit 0x2C
        _emit 0x7F
        _emit 0x44
        _emit 0x00
        _emit 0x8B        ; 0040DD7C  mov edx, dword ptr [0x447dad]
        _emit 0x15
        _emit 0xAD
        _emit 0x7D
        _emit 0x44
        _emit 0x00
        _emit 0x03        ; 0040DD82  add edx, eax
        _emit 0xD0
        _emit 0xA1        ; 0040DD84  mov eax, dword ptr [0x447f30]
        _emit 0x30
        _emit 0x7F
        _emit 0x44
        _emit 0x00
        _emit 0x2B        ; 0040DD89  sub esi, edx
        _emit 0xF2
        _emit 0x8B        ; 0040DD8B  mov edx, dword ptr [0x447dc1]
        _emit 0x15
        _emit 0xC1
        _emit 0x7D
        _emit 0x44
        _emit 0x00
        _emit 0x03        ; 0040DD91  add edx, eax
        _emit 0xD0
        _emit 0x2B        ; 0040DD93  sub edi, edx
        _emit 0xFA
        _emit 0x8B        ; 0040DD95  mov ecx, dword ptr [ecx + 0x156]
        _emit 0x89
        _emit 0x56
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x8D        ; 0040DD9B  lea eax, [ecx - 0xa]
        _emit 0x41
        _emit 0xF6
        _emit 0x83        ; 0040DD9E  cmp eax, 0xb
        _emit 0xF8
        _emit 0x0B
        _emit 0x0F        ; 0040DDA1  ja 0x40de7f
        _emit 0x87
        _emit 0xD8
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x33        ; 0040DDA7  xor ecx, ecx
        _emit 0xC9
        _emit 0x8A        ; 0040DDA9  mov cl, byte ptr [eax + 0x40e470]
        _emit 0x88
        _emit 0x70
        _emit 0xE4
        _emit 0x40
        _emit 0x00
        _emit 0xFF        ; 0040DDAF  jmp dword ptr [ecx*4 + 0x40e45c]
        _emit 0x24
        _emit 0x8D
        _emit 0x5C
        _emit 0xE4
        _emit 0x40
        _emit 0x00
        _emit 0x8B        ; 0040DDB6  mov ecx, dword ptr [0x4dfc91]
        _emit 0x0D
        _emit 0x91
        _emit 0xFC
        _emit 0x4D
        _emit 0x00
        _emit 0x85        ; 0040DDBC  test ecx, ecx
        _emit 0xC9
        _emit 0x0F        ; 0040DDBE  je 0x40de7f
        _emit 0x84
        _emit 0xBB
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0xA1        ; 0040DDC4  mov eax, dword ptr [0x4dfc85]
        _emit 0x85
        _emit 0xFC
        _emit 0x4D
        _emit 0x00
        _emit 0x0F        ; 0040DDC9  imul eax, ebp
        _emit 0xAF
        _emit 0xC5
        _emit 0x99        ; 0040DDCC  cdq
        _emit 0xF7        ; 0040DDCD  idiv ecx
        _emit 0xF9
        _emit 0x8B        ; 0040DDCF  mov ecx, dword ptr [esp + 0x14]
        _emit 0x4C
        _emit 0x24
        _emit 0x14
        _emit 0x8B        ; 0040DDD3  mov edx, ebp
        _emit 0xD5
        _emit 0x2B        ; 0040DDD5  sub edx, eax
        _emit 0xD0
        _emit 0x2B        ; 0040DDD7  sub ebp, eax
        _emit 0xE8
        _emit 0x03        ; 0040DDD9  add ecx, edx
        _emit 0xCA
        _emit 0x03        ; 0040DDDB  add esi, ebp
        _emit 0xF5
        _emit 0x89        ; 0040DDDD  mov dword ptr [esp + 0x14], ecx
        _emit 0x4C
        _emit 0x24
        _emit 0x14
        _emit 0xE9        ; 0040DDE1  jmp 0x40de7d
        _emit 0x97
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040DDE6  mov ecx, dword ptr [0x470058]
        _emit 0x0D
        _emit 0x58
        _emit 0x00
        _emit 0x47
        _emit 0x00
        _emit 0xB8        ; 0040DDEC  mov eax, 1
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x85        ; 0040DDF1  test ecx, ecx
        _emit 0xC9
        _emit 0x75        ; 0040DDF3  jne 0x40de03
        _emit 0x0E
        _emit 0xA1        ; 0040DDF5  mov eax, dword ptr [0x4701c4]
        _emit 0xC4
        _emit 0x01
        _emit 0x47
        _emit 0x00
        _emit 0x83        ; 0040DDFA  cmp eax, -1
        _emit 0xF8
        _emit 0xFF
        _emit 0x0F        ; 0040DDFD  je 0x40e3e0
        _emit 0x84
        _emit 0xDD
        _emit 0x05
        _emit 0x00
        _emit 0x00
        _emit 0x69        ; 0040DE03  imul eax, eax, 0xe03f
        _emit 0xC0
        _emit 0x3F
        _emit 0xE0
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040DE09  mov ecx, dword ptr [eax + 0x4dfc91]
        _emit 0x88
        _emit 0x91
        _emit 0xFC
        _emit 0x4D
        _emit 0x00
        _emit 0x85        ; 0040DE0F  test ecx, ecx
        _emit 0xC9
        _emit 0x74        ; 0040DE11  je 0x40de7f
        _emit 0x6C
        _emit 0x8B        ; 0040DE13  mov eax, dword ptr [eax + 0x4dfc85]
        _emit 0x80
        _emit 0x85
        _emit 0xFC
        _emit 0x4D
        _emit 0x00
        _emit 0xEB        ; 0040DE19  jmp 0x40de77
        _emit 0x5C
        _emit 0x8B        ; 0040DE1B  mov ecx, dword ptr [0x4dfca1]
        _emit 0x0D
        _emit 0xA1
        _emit 0xFC
        _emit 0x4D
        _emit 0x00
        _emit 0x85        ; 0040DE21  test ecx, ecx
        _emit 0xC9
        _emit 0x74        ; 0040DE23  je 0x40de7f
        _emit 0x5A
        _emit 0xA1        ; 0040DE25  mov eax, dword ptr [0x4dfc9d]
        _emit 0x9D
        _emit 0xFC
        _emit 0x4D
        _emit 0x00
        _emit 0x0F        ; 0040DE2A  imul eax, ebp
        _emit 0xAF
        _emit 0xC5
        _emit 0x99        ; 0040DE2D  cdq
        _emit 0xF7        ; 0040DE2E  idiv ecx
        _emit 0xF9
        _emit 0x8B        ; 0040DE30  mov edx, dword ptr [esp + 0x14]
        _emit 0x54
        _emit 0x24
        _emit 0x14
        _emit 0x8B        ; 0040DE34  mov ecx, ebp
        _emit 0xCD
        _emit 0x2B        ; 0040DE36  sub ecx, eax
        _emit 0xC8
        _emit 0x2B        ; 0040DE38  sub ebp, eax
        _emit 0xE8
        _emit 0x03        ; 0040DE3A  add edx, ecx
        _emit 0xD1
        _emit 0x03        ; 0040DE3C  add esi, ebp
        _emit 0xF5
        _emit 0x89        ; 0040DE3E  mov dword ptr [esp + 0x14], edx
        _emit 0x54
        _emit 0x24
        _emit 0x14
        _emit 0xEB        ; 0040DE42  jmp 0x40de7d
        _emit 0x39
        _emit 0x8B        ; 0040DE44  mov ecx, dword ptr [0x470058]
        _emit 0x0D
        _emit 0x58
        _emit 0x00
        _emit 0x47
        _emit 0x00
        _emit 0xB8        ; 0040DE4A  mov eax, 1
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x85        ; 0040DE4F  test ecx, ecx
        _emit 0xC9
        _emit 0x75        ; 0040DE51  jne 0x40de61
        _emit 0x0E
        _emit 0xA1        ; 0040DE53  mov eax, dword ptr [0x4701c4]
        _emit 0xC4
        _emit 0x01
        _emit 0x47
        _emit 0x00
        _emit 0x83        ; 0040DE58  cmp eax, -1
        _emit 0xF8
        _emit 0xFF
        _emit 0x0F        ; 0040DE5B  je 0x40e3e0
        _emit 0x84
        _emit 0x7F
        _emit 0x05
        _emit 0x00
        _emit 0x00
        _emit 0x69        ; 0040DE61  imul eax, eax, 0xe03f
        _emit 0xC0
        _emit 0x3F
        _emit 0xE0
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040DE67  mov ecx, dword ptr [eax + 0x4dfca1]
        _emit 0x88
        _emit 0xA1
        _emit 0xFC
        _emit 0x4D
        _emit 0x00
        _emit 0x85        ; 0040DE6D  test ecx, ecx
        _emit 0xC9
        _emit 0x74        ; 0040DE6F  je 0x40de7f
        _emit 0x0E
        _emit 0x8B        ; 0040DE71  mov eax, dword ptr [eax + 0x4dfc9d]
        _emit 0x80
        _emit 0x9D
        _emit 0xFC
        _emit 0x4D
        _emit 0x00
        _emit 0x0F        ; 0040DE77  imul eax, ebp
        _emit 0xAF
        _emit 0xC5
        _emit 0x99        ; 0040DE7A  cdq
        _emit 0xF7        ; 0040DE7B  idiv ecx
        _emit 0xF9
        _emit 0x8B        ; 0040DE7D  mov ebp, eax
        _emit 0xE8
        _emit 0x66        ; 0040DE7F  mov dx, word ptr [ebx + 3]
        _emit 0x8B
        _emit 0x53
        _emit 0x03
        _emit 0x8B        ; 0040DE83  mov eax, dword ptr [esp + 0x1c]
        _emit 0x44
        _emit 0x24
        _emit 0x1C
        _emit 0x81        ; 0040DE87  and edx, 0xc000
        _emit 0xE2
        _emit 0x00
        _emit 0xC0
        _emit 0x00
        _emit 0x00
        _emit 0x03        ; 0040DE8D  add edx, eax
        _emit 0xD0
        _emit 0x8B        ; 0040DE8F  mov eax, dword ptr [esp + 0x28]
        _emit 0x44
        _emit 0x24
        _emit 0x28
        _emit 0x52        ; 0040DE93  push edx
        _emit 0x50        ; 0040DE94  push eax
        _emit 0x55        ; 0040DE95  push ebp
        _emit 0x57        ; 0040DE96  push edi
        _emit 0xE9        ; 0040DE97  jmp 0x40d790
        _emit 0xF4
        _emit 0xF8
        _emit 0xFF
        _emit 0xFF
        _emit 0x85        ; 0040DE9C  test eax, eax
        _emit 0xC0
        _emit 0x0F        ; 0040DE9E  jle 0x40df28
        _emit 0x8E
        _emit 0x84
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x8D        ; 0040DEA4  lea eax, [eax + eax*4]
        _emit 0x04
        _emit 0x80
        _emit 0x8D        ; 0040DEA7  lea esi, [eax*4 + 0x424f60]
        _emit 0x34
        _emit 0x85
        _emit 0x60
        _emit 0x4F
        _emit 0x42
        _emit 0x00
        _emit 0x8B        ; 0040DEAE  mov eax, dword ptr [eax*4 + 0x424f60]
        _emit 0x04
        _emit 0x85
        _emit 0x60
        _emit 0x4F
        _emit 0x42
        _emit 0x00
        _emit 0x85        ; 0040DEB5  test eax, eax
        _emit 0xC0
        _emit 0x0F        ; 0040DEB7  je 0x40e3e0
        _emit 0x84
        _emit 0x23
        _emit 0x05
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040DEBD  mov edx, dword ptr [esi + 0x10]
        _emit 0x56
        _emit 0x10
        _emit 0x85        ; 0040DEC0  test edx, edx
        _emit 0xD2
        _emit 0x74        ; 0040DEC2  je 0x40dedb
        _emit 0x17
        _emit 0x8B        ; 0040DEC4  mov ecx, dword ptr [0x425a44]
        _emit 0x0D
        _emit 0x44
        _emit 0x5A
        _emit 0x42
        _emit 0x00
        _emit 0x52        ; 0040DECA  push edx
        _emit 0x50        ; 0040DECB  push eax
        _emit 0x51        ; 0040DECC  push ecx
        _emit 0xE8        ; 0040DECD  call 0x4140c0
        _emit 0xEE
        _emit 0x61
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040DED2  mov ecx, dword ptr [0x4cfa00]
        _emit 0x0D
        _emit 0x00
        _emit 0xFA
        _emit 0x4C
        _emit 0x00
        _emit 0x83        ; 0040DED8  add esp, 0xc
        _emit 0xC4
        _emit 0x0C
        _emit 0x8B        ; 0040DEDB  mov edx, dword ptr [ecx + 0x4c]
        _emit 0x51
        _emit 0x4C
        _emit 0x8B        ; 0040DEDE  mov eax, dword ptr [ecx + 0x48]
        _emit 0x41
        _emit 0x48
        _emit 0x52        ; 0040DEE1  push edx
        _emit 0x8B        ; 0040DEE2  mov edx, dword ptr [ecx + 0x44]
        _emit 0x51
        _emit 0x44
        _emit 0x50        ; 0040DEE5  push eax
        _emit 0x8B        ; 0040DEE6  mov eax, dword ptr [ecx + 0x54]
        _emit 0x41
        _emit 0x54
        _emit 0x52        ; 0040DEE9  push edx
        _emit 0x8B        ; 0040DEEA  mov edx, dword ptr [esi + 8]
        _emit 0x56
        _emit 0x08
        _emit 0x50        ; 0040DEED  push eax
        _emit 0x8B        ; 0040DEEE  mov eax, dword ptr [esi + 4]
        _emit 0x46
        _emit 0x04
        _emit 0x6A        ; 0040DEF1  push 0
        _emit 0x00
        _emit 0x6A        ; 0040DEF3  push 0
        _emit 0x00
        _emit 0x52        ; 0040DEF5  push edx
        _emit 0x50        ; 0040DEF6  push eax
        _emit 0x8B        ; 0040DEF7  mov eax, dword ptr [ecx + 0xc]
        _emit 0x41
        _emit 0x0C
        _emit 0x99        ; 0040DEFA  cdq
        _emit 0x81        ; 0040DEFB  and edx, 0xffff
        _emit 0xE2
        _emit 0xFF
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x03        ; 0040DF01  add eax, edx
        _emit 0xC2
        _emit 0xC1        ; 0040DF03  sar eax, 0x10
        _emit 0xF8
        _emit 0x10
        _emit 0x50        ; 0040DF06  push eax
        _emit 0x8B        ; 0040DF07  mov eax, dword ptr [ecx + 8]
        _emit 0x41
        _emit 0x08
        _emit 0x99        ; 0040DF0A  cdq
        _emit 0x81        ; 0040DF0B  and edx, 0xffff
        _emit 0xE2
        _emit 0xFF
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x03        ; 0040DF11  add eax, edx
        _emit 0xC2
        _emit 0xC1        ; 0040DF13  sar eax, 0x10
        _emit 0xF8
        _emit 0x10
        _emit 0x50        ; 0040DF16  push eax
        _emit 0x56        ; 0040DF17  push esi
        _emit 0xE8        ; 0040DF18  call 0x40b4c0
        _emit 0xA3
        _emit 0xD5
        _emit 0xFF
        _emit 0xFF
        _emit 0x83        ; 0040DF1D  add esp, 0x2c
        _emit 0xC4
        _emit 0x2C
        _emit 0x5F        ; 0040DF20  pop edi
        _emit 0x5E        ; 0040DF21  pop esi
        _emit 0x5D        ; 0040DF22  pop ebp
        _emit 0x5B        ; 0040DF23  pop ebx
        _emit 0x83        ; 0040DF24  add esp, 0x6c
        _emit 0xC4
        _emit 0x6C
        _emit 0xC3        ; 0040DF27  ret
        _emit 0x83        ; 0040DF28  add eax, 0xa
        _emit 0xC0
        _emit 0x0A
        _emit 0x83        ; 0040DF2B  cmp eax, 8
        _emit 0xF8
        _emit 0x08
        _emit 0x0F        ; 0040DF2E  ja 0x40e3e0
        _emit 0x87
        _emit 0xAC
        _emit 0x04
        _emit 0x00
        _emit 0x00
        _emit 0xFF        ; 0040DF34  jmp dword ptr [eax*4 + 0x40e47c]
        _emit 0x24
        _emit 0x85
        _emit 0x7C
        _emit 0xE4
        _emit 0x40
        _emit 0x00
        _emit 0xBE        ; 0040DF3B  mov esi, 0x470208
        _emit 0x08
        _emit 0x02
        _emit 0x47
        _emit 0x00
        _emit 0xC7        ; 0040DF40  mov dword ptr [esp + 0x68], 0x400
        _emit 0x44
        _emit 0x24
        _emit 0x68
        _emit 0x00
        _emit 0x04
        _emit 0x00
        _emit 0x00
        _emit 0xBD        ; 0040DF48  mov ebp, 4
        _emit 0x04
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x83        ; 0040DF4D  or ebx, 0xffffffff
        _emit 0xCB
        _emit 0xFF
        _emit 0x39        ; 0040DF50  cmp dword ptr [esi - 0x28], ebp
        _emit 0x6E
        _emit 0xD8
        _emit 0x0F        ; 0040DF53  jne 0x40dfe5
        _emit 0x85
        _emit 0x8C
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x39        ; 0040DF59  cmp dword ptr [esi - 0x18], ebx
        _emit 0x5E
        _emit 0xE8
        _emit 0x0F        ; 0040DF5C  jne 0x40dfe5
        _emit 0x85
        _emit 0x83
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0xF7        ; 0040DF62  test dword ptr [esi], 0x80000000
        _emit 0x06
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x80
        _emit 0x74        ; 0040DF68  je 0x40dfe5
        _emit 0x7B
        _emit 0x8B        ; 0040DF6A  mov edi, dword ptr [esi + 0x30]
        _emit 0x7E
        _emit 0x30
        _emit 0x8B        ; 0040DF6D  mov eax, dword ptr [esi - 0x1c]
        _emit 0x46
        _emit 0xE4
        _emit 0x2B        ; 0040DF70  sub eax, edi
        _emit 0xC7
        _emit 0x99        ; 0040DF72  cdq
        _emit 0x81        ; 0040DF73  and edx, 0xfffff
        _emit 0xE2
        _emit 0xFF
        _emit 0xFF
        _emit 0x0F
        _emit 0x00
        _emit 0x03        ; 0040DF79  add eax, edx
        _emit 0xC2
        _emit 0xC1        ; 0040DF7B  sar eax, 0x14
        _emit 0xF8
        _emit 0x14
        _emit 0x89        ; 0040DF7E  mov dword ptr [ecx + 0x4c], eax
        _emit 0x41
        _emit 0x4C
        _emit 0x89        ; 0040DF81  mov dword ptr [ecx + 0x48], eax
        _emit 0x41
        _emit 0x48
        _emit 0x89        ; 0040DF84  mov dword ptr [ecx + 0x44], eax
        _emit 0x41
        _emit 0x44
        _emit 0x8B        ; 0040DF87  mov edx, dword ptr [ecx + 0x4c]
        _emit 0x51
        _emit 0x4C
        _emit 0x8B        ; 0040DF8A  mov ecx, dword ptr [ecx + 0x48]
        _emit 0x49
        _emit 0x48
        _emit 0x52        ; 0040DF8D  push edx
        _emit 0x51        ; 0040DF8E  push ecx
        _emit 0x50        ; 0040DF8F  push eax
        _emit 0x8B        ; 0040DF90  mov eax, edi
        _emit 0xC7
        _emit 0x8B        ; 0040DF92  mov ecx, dword ptr [0x447f30]
        _emit 0x0D
        _emit 0x30
        _emit 0x7F
        _emit 0x44
        _emit 0x00
        _emit 0x99        ; 0040DF98  cdq
        _emit 0x81        ; 0040DF99  and edx, 0xffff
        _emit 0xE2
        _emit 0xFF
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x6A        ; 0040DF9F  push 3
        _emit 0x03
        _emit 0x03        ; 0040DFA1  add eax, edx
        _emit 0xC2
        _emit 0x6A        ; 0040DFA3  push 0x60
        _emit 0x60
        _emit 0xC1        ; 0040DFA5  sar eax, 0x10
        _emit 0xF8
        _emit 0x10
        _emit 0x2B        ; 0040DFA8  sub eax, ecx
        _emit 0xC1
        _emit 0x6A        ; 0040DFAA  push 0
        _emit 0x00
        _emit 0x6A        ; 0040DFAC  push 0x20
        _emit 0x20
        _emit 0x83        ; 0040DFAE  sub eax, 0x10
        _emit 0xE8
        _emit 0x10
        _emit 0x68        ; 0040DFB1  push 0x80
        _emit 0x80
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x50        ; 0040DFB6  push eax
        _emit 0x8B        ; 0040DFB7  mov eax, dword ptr [esi - 0x20]
        _emit 0x46
        _emit 0xE0
        _emit 0x8B        ; 0040DFBA  mov ecx, dword ptr [0x447f2c]
        _emit 0x0D
        _emit 0x2C
        _emit 0x7F
        _emit 0x44
        _emit 0x00
        _emit 0x99        ; 0040DFC0  cdq
        _emit 0x81        ; 0040DFC1  and edx, 0xffff
        _emit 0xE2
        _emit 0xFF
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x03        ; 0040DFC7  add eax, edx
        _emit 0xC2
        _emit 0xC1        ; 0040DFC9  sar eax, 0x10
        _emit 0xF8
        _emit 0x10
        _emit 0x2B        ; 0040DFCC  sub eax, ecx
        _emit 0xC1
        _emit 0x83        ; 0040DFCE  sub eax, 0x38
        _emit 0xE8
        _emit 0x38
        _emit 0x50        ; 0040DFD1  push eax
        _emit 0x68        ; 0040DFD2  push 0x424f74
        _emit 0x74
        _emit 0x4F
        _emit 0x42
        _emit 0x00
        _emit 0xE8        ; 0040DFD7  call 0x40b4c0
        _emit 0xE4
        _emit 0xD4
        _emit 0xFF
        _emit 0xFF
        _emit 0x8B        ; 0040DFDC  mov ecx, dword ptr [0x4cfa00]
        _emit 0x0D
        _emit 0x00
        _emit 0xFA
        _emit 0x4C
        _emit 0x00
        _emit 0x83        ; 0040DFE2  add esp, 0x2c
        _emit 0xC4
        _emit 0x2C
        _emit 0x8B        ; 0040DFE5  mov eax, dword ptr [esp + 0x68]
        _emit 0x44
        _emit 0x24
        _emit 0x68
        _emit 0x81        ; 0040DFE9  add esi, 0x17e
        _emit 0xC6
        _emit 0x7E
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x48        ; 0040DFEF  dec eax
        _emit 0x89        ; 0040DFF0  mov dword ptr [esp + 0x68], eax
        _emit 0x44
        _emit 0x24
        _emit 0x68
        _emit 0x0F        ; 0040DFF4  jne 0x40df50
        _emit 0x85
        _emit 0x56
        _emit 0xFF
        _emit 0xFF
        _emit 0xFF
        _emit 0x5F        ; 0040DFFA  pop edi
        _emit 0x5E        ; 0040DFFB  pop esi
        _emit 0x5D        ; 0040DFFC  pop ebp
        _emit 0x5B        ; 0040DFFD  pop ebx
        _emit 0x83        ; 0040DFFE  add esp, 0x6c
        _emit 0xC4
        _emit 0x6C
        _emit 0xC3        ; 0040E001  ret
        _emit 0x8B        ; 0040E002  mov edx, dword ptr [ecx + 0x4c]
        _emit 0x51
        _emit 0x4C
        _emit 0x8B        ; 0040E005  mov eax, dword ptr [ecx + 0x48]
        _emit 0x41
        _emit 0x48
        _emit 0x52        ; 0040E008  push edx
        _emit 0x8B        ; 0040E009  mov edx, dword ptr [ecx + 0x44]
        _emit 0x51
        _emit 0x44
        _emit 0x50        ; 0040E00C  push eax
        _emit 0x8B        ; 0040E00D  mov eax, dword ptr [ecx + 0x5c]
        _emit 0x41
        _emit 0x5C
        _emit 0xC1        ; 0040E010  shl eax, 0x1e
        _emit 0xE0
        _emit 0x1E
        _emit 0x52        ; 0040E013  push edx
        _emit 0x8B        ; 0040E014  mov edx, dword ptr [ecx + 0x15a]
        _emit 0x91
        _emit 0x5A
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x83        ; 0040E01A  add eax, 2
        _emit 0xC0
        _emit 0x02
        _emit 0x8B        ; 0040E01D  mov edi, dword ptr [0x447f2c]
        _emit 0x3D
        _emit 0x2C
        _emit 0x7F
        _emit 0x44
        _emit 0x00
        _emit 0x50        ; 0040E023  push eax
        _emit 0x8B        ; 0040E024  mov eax, dword ptr [ecx + 0x156]
        _emit 0x81
        _emit 0x56
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0xC1        ; 0040E02A  shl edx, 7
        _emit 0xE2
        _emit 0x07
        _emit 0x52        ; 0040E02D  push edx
        _emit 0x99        ; 0040E02E  cdq
        _emit 0x83        ; 0040E02F  and edx, 7
        _emit 0xE2
        _emit 0x07
        _emit 0x03        ; 0040E032  add eax, edx
        _emit 0xC2
        _emit 0x8B        ; 0040E034  mov edx, dword ptr [0x447f30]
        _emit 0x15
        _emit 0x30
        _emit 0x7F
        _emit 0x44
        _emit 0x00
        _emit 0xC1        ; 0040E03A  sar eax, 3
        _emit 0xF8
        _emit 0x03
        _emit 0xC1        ; 0040E03D  shl eax, 7
        _emit 0xE0
        _emit 0x07
        _emit 0x50        ; 0040E040  push eax
        _emit 0x8B        ; 0040E041  mov eax, dword ptr [ecx + 0xc]
        _emit 0x41
        _emit 0x0C
        _emit 0x8B        ; 0040E044  mov ecx, dword ptr [ecx + 8]
        _emit 0x49
        _emit 0x08
        _emit 0x2B        ; 0040E047  sub eax, edx
        _emit 0xC2
        _emit 0x2B        ; 0040E049  sub ecx, edi
        _emit 0xCF
        _emit 0x68        ; 0040E04B  push 0x80
        _emit 0x80
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x83        ; 0040E050  sub eax, 0x40
        _emit 0xE8
        _emit 0x40
        _emit 0x68        ; 0040E053  push 0x80
        _emit 0x80
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x83        ; 0040E058  sub ecx, 0x40
        _emit 0xE9
        _emit 0x40
        _emit 0x50        ; 0040E05B  push eax
        _emit 0x51        ; 0040E05C  push ecx
        _emit 0x68        ; 0040E05D  push 0x424f74
        _emit 0x74
        _emit 0x4F
        _emit 0x42
        _emit 0x00
        _emit 0xE8        ; 0040E062  call 0x40b4c0
        _emit 0x59
        _emit 0xD4
        _emit 0xFF
        _emit 0xFF
        _emit 0x83        ; 0040E067  add esp, 0x2c
        _emit 0xC4
        _emit 0x2C
        _emit 0x5F        ; 0040E06A  pop edi
        _emit 0x5E        ; 0040E06B  pop esi
        _emit 0x5D        ; 0040E06C  pop ebp
        _emit 0x5B        ; 0040E06D  pop ebx
        _emit 0x83        ; 0040E06E  add esp, 0x6c
        _emit 0xC4
        _emit 0x6C
        _emit 0xC3        ; 0040E071  ret
        _emit 0x8B        ; 0040E072  mov edx, dword ptr [ecx + 0x4c]
        _emit 0x51
        _emit 0x4C
        _emit 0x8B        ; 0040E075  mov eax, dword ptr [ecx + 0x48]
        _emit 0x41
        _emit 0x48
        _emit 0x52        ; 0040E078  push edx
        _emit 0x8B        ; 0040E079  mov edx, dword ptr [ecx + 0x44]
        _emit 0x51
        _emit 0x44
        _emit 0x50        ; 0040E07C  push eax
        _emit 0x8B        ; 0040E07D  mov eax, dword ptr [ecx + 0xc]
        _emit 0x41
        _emit 0x0C
        _emit 0x52        ; 0040E080  push edx
        _emit 0x6A        ; 0040E081  push 4
        _emit 0x04
        _emit 0x99        ; 0040E083  cdq
        _emit 0x81        ; 0040E084  and edx, 0xffff
        _emit 0xE2
        _emit 0xFF
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x6A        ; 0040E08A  push 0
        _emit 0x00
        _emit 0x03        ; 0040E08C  add eax, edx
        _emit 0xC2
        _emit 0x6A        ; 0040E08E  push 0
        _emit 0x00
        _emit 0x68        ; 0040E090  push 0x200
        _emit 0x00
        _emit 0x02
        _emit 0x00
        _emit 0x00
        _emit 0x68        ; 0040E095  push 0x200
        _emit 0x00
        _emit 0x02
        _emit 0x00
        _emit 0x00
        _emit 0xC1        ; 0040E09A  sar eax, 0x10
        _emit 0xF8
        _emit 0x10
        _emit 0x50        ; 0040E09D  push eax
        _emit 0x8B        ; 0040E09E  mov eax, dword ptr [ecx + 8]
        _emit 0x41
        _emit 0x08
        _emit 0x99        ; 0040E0A1  cdq
        _emit 0x81        ; 0040E0A2  and edx, 0xffff
        _emit 0xE2
        _emit 0xFF
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x03        ; 0040E0A8  add eax, edx
        _emit 0xC2
        _emit 0xC1        ; 0040E0AA  sar eax, 0x10
        _emit 0xF8
        _emit 0x10
        _emit 0x50        ; 0040E0AD  push eax
        _emit 0x68        ; 0040E0AE  push 0x424f74
        _emit 0x74
        _emit 0x4F
        _emit 0x42
        _emit 0x00
        _emit 0xE8        ; 0040E0B3  call 0x40b4c0
        _emit 0x08
        _emit 0xD4
        _emit 0xFF
        _emit 0xFF
        _emit 0x83        ; 0040E0B8  add esp, 0x2c
        _emit 0xC4
        _emit 0x2C
        _emit 0x5F        ; 0040E0BB  pop edi
        _emit 0x5E        ; 0040E0BC  pop esi
        _emit 0x5D        ; 0040E0BD  pop ebp
        _emit 0x5B        ; 0040E0BE  pop ebx
        _emit 0x83        ; 0040E0BF  add esp, 0x6c
        _emit 0xC4
        _emit 0x6C
        _emit 0xC3        ; 0040E0C2  ret
        _emit 0x8B        ; 0040E0C3  mov eax, dword ptr [ecx + 0x4c]
        _emit 0x41
        _emit 0x4C
        _emit 0x8B        ; 0040E0C6  mov edx, dword ptr [ecx + 0x48]
        _emit 0x51
        _emit 0x48
        _emit 0x50        ; 0040E0C9  push eax
        _emit 0x8B        ; 0040E0CA  mov eax, dword ptr [ecx + 0x44]
        _emit 0x41
        _emit 0x44
        _emit 0x52        ; 0040E0CD  push edx
        _emit 0x8B        ; 0040E0CE  mov edx, dword ptr [ecx + 0xc]
        _emit 0x51
        _emit 0x0C
        _emit 0x50        ; 0040E0D1  push eax
        _emit 0x8B        ; 0040E0D2  mov eax, dword ptr [ecx + 8]
        _emit 0x41
        _emit 0x08
        _emit 0x8B        ; 0040E0D5  mov ecx, dword ptr [ecx + 0x156]
        _emit 0x89
        _emit 0x56
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x6A        ; 0040E0DB  push 0
        _emit 0x00
        _emit 0x52        ; 0040E0DD  push edx
        _emit 0x50        ; 0040E0DE  push eax
        _emit 0x51        ; 0040E0DF  push ecx
        _emit 0xE8        ; 0040E0E0  call 0x40bed0
        _emit 0xEB
        _emit 0xDD
        _emit 0xFF
        _emit 0xFF
        _emit 0x83        ; 0040E0E5  add esp, 0x1c
        _emit 0xC4
        _emit 0x1C
        _emit 0x5F        ; 0040E0E8  pop edi
        _emit 0x5E        ; 0040E0E9  pop esi
        _emit 0x5D        ; 0040E0EA  pop ebp
        _emit 0x5B        ; 0040E0EB  pop ebx
        _emit 0x83        ; 0040E0EC  add esp, 0x6c
        _emit 0xC4
        _emit 0x6C
        _emit 0xC3        ; 0040E0EF  ret
        _emit 0x8B        ; 0040E0F0  mov edx, dword ptr [ecx + 0x4c]
        _emit 0x51
        _emit 0x4C
        _emit 0x8B        ; 0040E0F3  mov eax, dword ptr [ecx + 0x48]
        _emit 0x41
        _emit 0x48
        _emit 0x8B        ; 0040E0F6  mov esi, dword ptr [0x447f30]
        _emit 0x35
        _emit 0x30
        _emit 0x7F
        _emit 0x44
        _emit 0x00
        _emit 0x8B        ; 0040E0FC  mov ebp, dword ptr [0x447f2c]
        _emit 0x2D
        _emit 0x2C
        _emit 0x7F
        _emit 0x44
        _emit 0x00
        _emit 0x52        ; 0040E102  push edx
        _emit 0x8B        ; 0040E103  mov edx, dword ptr [ecx + 0x44]
        _emit 0x51
        _emit 0x44
        _emit 0x50        ; 0040E106  push eax
        _emit 0x8B        ; 0040E107  mov eax, dword ptr [ecx + 0xc]
        _emit 0x41
        _emit 0x0C
        _emit 0x52        ; 0040E10A  push edx
        _emit 0x8B        ; 0040E10B  mov edx, dword ptr [ecx + 8]
        _emit 0x51
        _emit 0x08
        _emit 0x2B        ; 0040E10E  sub eax, esi
        _emit 0xC6
        _emit 0x6A        ; 0040E110  push 4
        _emit 0x04
        _emit 0x50        ; 0040E112  push eax
        _emit 0x8B        ; 0040E113  mov eax, dword ptr [ecx + 0x156]
        _emit 0x81
        _emit 0x56
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x2B        ; 0040E119  sub edx, ebp
        _emit 0xD5
        _emit 0x52        ; 0040E11B  push edx
        _emit 0x50        ; 0040E11C  push eax
        _emit 0xE8        ; 0040E11D  call 0x40bfb0
        _emit 0x8E
        _emit 0xDE
        _emit 0xFF
        _emit 0xFF
        _emit 0xA1        ; 0040E122  mov eax, dword ptr [0x4cfa00]
        _emit 0x00
        _emit 0xFA
        _emit 0x4C
        _emit 0x00
        _emit 0x8B        ; 0040E127  mov edi, dword ptr [0x447f2c]
        _emit 0x3D
        _emit 0x2C
        _emit 0x7F
        _emit 0x44
        _emit 0x00
        _emit 0x8B        ; 0040E12D  mov ebp, dword ptr [0x447f30]
        _emit 0x2D
        _emit 0x30
        _emit 0x7F
        _emit 0x44
        _emit 0x00
        _emit 0x8B        ; 0040E133  mov ecx, dword ptr [eax + 0x4c]
        _emit 0x48
        _emit 0x4C
        _emit 0x8B        ; 0040E136  mov edx, dword ptr [eax + 0x48]
        _emit 0x50
        _emit 0x48
        _emit 0x51        ; 0040E139  push ecx
        _emit 0x8B        ; 0040E13A  mov ecx, dword ptr [eax + 0x44]
        _emit 0x48
        _emit 0x44
        _emit 0x52        ; 0040E13D  push edx
        _emit 0x8B        ; 0040E13E  mov edx, dword ptr [eax + 0xc]
        _emit 0x50
        _emit 0x0C
        _emit 0x8B        ; 0040E141  mov eax, dword ptr [eax + 8]
        _emit 0x40
        _emit 0x08
        _emit 0x51        ; 0040E144  push ecx
        _emit 0x6A        ; 0040E145  push 4
        _emit 0x04
        _emit 0x6A        ; 0040E147  push 0x40
        _emit 0x40
        _emit 0x6A        ; 0040E149  push 0
        _emit 0x00
        _emit 0x2B        ; 0040E14B  sub eax, edi
        _emit 0xC7
        _emit 0x6A        ; 0040E14D  push 0x20
        _emit 0x20
        _emit 0x2B        ; 0040E14F  sub edx, ebp
        _emit 0xD5
        _emit 0x6A        ; 0040E151  push 0x60
        _emit 0x60
        _emit 0x83        ; 0040E153  add eax, 0x20
        _emit 0xC0
        _emit 0x20
        _emit 0x52        ; 0040E156  push edx
        _emit 0x50        ; 0040E157  push eax
        _emit 0x68        ; 0040E158  push 0x424f74
        _emit 0x74
        _emit 0x4F
        _emit 0x42
        _emit 0x00
        _emit 0xE8        ; 0040E15D  call 0x40b4c0
        _emit 0x5E
        _emit 0xD3
        _emit 0xFF
        _emit 0xFF
        _emit 0x83        ; 0040E162  add esp, 0x48
        _emit 0xC4
        _emit 0x48
        _emit 0x5F        ; 0040E165  pop edi
        _emit 0x5E        ; 0040E166  pop esi
        _emit 0x5D        ; 0040E167  pop ebp
        _emit 0x5B        ; 0040E168  pop ebx
        _emit 0x83        ; 0040E169  add esp, 0x6c
        _emit 0xC4
        _emit 0x6C
        _emit 0xC3        ; 0040E16C  ret
        _emit 0x8B        ; 0040E16D  mov edx, dword ptr [ecx + 0x4c]
        _emit 0x51
        _emit 0x4C
        _emit 0x8B        ; 0040E170  mov eax, dword ptr [ecx + 0x48]
        _emit 0x41
        _emit 0x48
        _emit 0x52        ; 0040E173  push edx
        _emit 0x8B        ; 0040E174  mov edx, dword ptr [ecx + 0x44]
        _emit 0x51
        _emit 0x44
        _emit 0x50        ; 0040E177  push eax
        _emit 0x8B        ; 0040E178  mov eax, dword ptr [ecx + 0xc]
        _emit 0x41
        _emit 0x0C
        _emit 0x8B        ; 0040E17B  mov ecx, dword ptr [ecx + 8]
        _emit 0x49
        _emit 0x08
        _emit 0x52        ; 0040E17E  push edx
        _emit 0x6A        ; 0040E17F  push 0
        _emit 0x00
        _emit 0x68        ; 0040E181  push 0x240
        _emit 0x40
        _emit 0x02
        _emit 0x00
        _emit 0x00
        _emit 0x6A        ; 0040E186  push 0
        _emit 0x00
        _emit 0x6A        ; 0040E188  push 0x40
        _emit 0x40
        _emit 0x68        ; 0040E18A  push 0x100
        _emit 0x00
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x81        ; 0040E18F  sub ecx, 0xc0
        _emit 0xE9
        _emit 0xC0
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x50        ; 0040E195  push eax
        _emit 0x51        ; 0040E196  push ecx
        _emit 0x68        ; 0040E197  push 0x424f74
        _emit 0x74
        _emit 0x4F
        _emit 0x42
        _emit 0x00
        _emit 0xE8        ; 0040E19C  call 0x40b4c0
        _emit 0x1F
        _emit 0xD3
        _emit 0xFF
        _emit 0xFF
        _emit 0xB8        ; 0040E1A1  mov eax, 0x66666667
        _emit 0x67
        _emit 0x66
        _emit 0x66
        _emit 0x66
        _emit 0x83        ; 0040E1A6  add esp, 0x2c
        _emit 0xC4
        _emit 0x2C
        _emit 0xF7        ; 0040E1A9  imul dword ptr [0x470044]
        _emit 0x2D
        _emit 0x44
        _emit 0x00
        _emit 0x47
        _emit 0x00
        _emit 0xC1        ; 0040E1AF  sar edx, 2
        _emit 0xFA
        _emit 0x02
        _emit 0x8B        ; 0040E1B2  mov eax, edx
        _emit 0xC2
        _emit 0xC1        ; 0040E1B4  shr eax, 0x1f
        _emit 0xE8
        _emit 0x1F
        _emit 0x03        ; 0040E1B7  add edx, eax
        _emit 0xD0
        _emit 0x74        ; 0040E1B9  je 0x40e1f3
        _emit 0x38
        _emit 0xA1        ; 0040E1BB  mov eax, dword ptr [0x4cfa00]
        _emit 0x00
        _emit 0xFA
        _emit 0x4C
        _emit 0x00
        _emit 0xC1        ; 0040E1C0  shl edx, 6
        _emit 0xE2
        _emit 0x06
        _emit 0x8B        ; 0040E1C3  mov ecx, dword ptr [eax + 0x4c]
        _emit 0x48
        _emit 0x4C
        _emit 0x51        ; 0040E1C6  push ecx
        _emit 0x8B        ; 0040E1C7  mov ecx, dword ptr [eax + 0x48]
        _emit 0x48
        _emit 0x48
        _emit 0x51        ; 0040E1CA  push ecx
        _emit 0x8B        ; 0040E1CB  mov ecx, dword ptr [eax + 0x44]
        _emit 0x48
        _emit 0x44
        _emit 0x51        ; 0040E1CE  push ecx
        _emit 0x6A        ; 0040E1CF  push 0
        _emit 0x00
        _emit 0x68        ; 0040E1D1  push 0x200
        _emit 0x00
        _emit 0x02
        _emit 0x00
        _emit 0x00
        _emit 0x52        ; 0040E1D6  push edx
        _emit 0x8B        ; 0040E1D7  mov edx, dword ptr [eax + 0xc]
        _emit 0x50
        _emit 0x0C
        _emit 0x8B        ; 0040E1DA  mov eax, dword ptr [eax + 8]
        _emit 0x40
        _emit 0x08
        _emit 0x6A        ; 0040E1DD  push 0x40
        _emit 0x40
        _emit 0x6A        ; 0040E1DF  push 0x40
        _emit 0x40
        _emit 0x83        ; 0040E1E1  add eax, 0x40
        _emit 0xC0
        _emit 0x40
        _emit 0x52        ; 0040E1E4  push edx
        _emit 0x50        ; 0040E1E5  push eax
        _emit 0x68        ; 0040E1E6  push 0x424f74
        _emit 0x74
        _emit 0x4F
        _emit 0x42
        _emit 0x00
        _emit 0xE8        ; 0040E1EB  call 0x40b4c0
        _emit 0xD0
        _emit 0xD2
        _emit 0xFF
        _emit 0xFF
        _emit 0x83        ; 0040E1F0  add esp, 0x2c
        _emit 0xC4
        _emit 0x2C
        _emit 0x8B        ; 0040E1F3  mov ecx, dword ptr [0x4cfa00]
        _emit 0x0D
        _emit 0x00
        _emit 0xFA
        _emit 0x4C
        _emit 0x00
        _emit 0xBE        ; 0040E1F9  mov esi, 0xa
        _emit 0x0A
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040E1FE  mov edx, dword ptr [ecx + 0x4c]
        _emit 0x51
        _emit 0x4C
        _emit 0x8B        ; 0040E201  mov eax, dword ptr [ecx + 0x48]
        _emit 0x41
        _emit 0x48
        _emit 0x52        ; 0040E204  push edx
        _emit 0x8B        ; 0040E205  mov edx, dword ptr [ecx + 0x44]
        _emit 0x51
        _emit 0x44
        _emit 0x50        ; 0040E208  push eax
        _emit 0xA1        ; 0040E209  mov eax, dword ptr [0x470044]
        _emit 0x44
        _emit 0x00
        _emit 0x47
        _emit 0x00
        _emit 0x52        ; 0040E20E  push edx
        _emit 0x6A        ; 0040E20F  push 0
        _emit 0x00
        _emit 0x99        ; 0040E211  cdq
        _emit 0xF7        ; 0040E212  idiv esi
        _emit 0xFE
        _emit 0x8B        ; 0040E214  mov eax, dword ptr [ecx + 0xc]
        _emit 0x41
        _emit 0x0C
        _emit 0x8B        ; 0040E217  mov ecx, dword ptr [ecx + 8]
        _emit 0x49
        _emit 0x08
        _emit 0x68        ; 0040E21A  push 0x200
        _emit 0x00
        _emit 0x02
        _emit 0x00
        _emit 0x00
        _emit 0x81        ; 0040E21F  add ecx, 0x80
        _emit 0xC1
        _emit 0x80
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0xC1        ; 0040E225  shl edx, 6
        _emit 0xE2
        _emit 0x06
        _emit 0x52        ; 0040E228  push edx
        _emit 0x6A        ; 0040E229  push 0x40
        _emit 0x40
        _emit 0x6A        ; 0040E22B  push 0x40
        _emit 0x40
        _emit 0x50        ; 0040E22D  push eax
        _emit 0x51        ; 0040E22E  push ecx
        _emit 0x68        ; 0040E22F  push 0x424f74
        _emit 0x74
        _emit 0x4F
        _emit 0x42
        _emit 0x00
        _emit 0xE8        ; 0040E234  call 0x40b4c0
        _emit 0x87
        _emit 0xD2
        _emit 0xFF
        _emit 0xFF
        _emit 0xA1        ; 0040E239  mov eax, dword ptr [0x4cfa00]
        _emit 0x00
        _emit 0xFA
        _emit 0x4C
        _emit 0x00
        _emit 0x8B        ; 0040E23E  mov edx, dword ptr [eax + 0x4c]
        _emit 0x50
        _emit 0x4C
        _emit 0x8B        ; 0040E241  mov ecx, dword ptr [eax + 0x48]
        _emit 0x48
        _emit 0x48
        _emit 0x52        ; 0040E244  push edx
        _emit 0x8B        ; 0040E245  mov edx, dword ptr [eax + 0x44]
        _emit 0x50
        _emit 0x44
        _emit 0x51        ; 0040E248  push ecx
        _emit 0x8B        ; 0040E249  mov ecx, dword ptr [eax + 0xc]
        _emit 0x48
        _emit 0x0C
        _emit 0x52        ; 0040E24C  push edx
        _emit 0x8B        ; 0040E24D  mov edx, dword ptr [eax + 8]
        _emit 0x50
        _emit 0x08
        _emit 0x6A        ; 0040E250  push 0
        _emit 0x00
        _emit 0x68        ; 0040E252  push 0x240
        _emit 0x40
        _emit 0x02
        _emit 0x00
        _emit 0x00
        _emit 0x68        ; 0040E257  push 0x100
        _emit 0x00
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x6A        ; 0040E25C  push 0x5c
        _emit 0x5C
        _emit 0x83        ; 0040E25E  add ecx, 0x40
        _emit 0xC1
        _emit 0x40
        _emit 0x68        ; 0040E261  push 0x100
        _emit 0x00
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x81        ; 0040E266  sub edx, 0x80
        _emit 0xEA
        _emit 0x80
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x51        ; 0040E26C  push ecx
        _emit 0x52        ; 0040E26D  push edx
        _emit 0x68        ; 0040E26E  push 0x424f74
        _emit 0x74
        _emit 0x4F
        _emit 0x42
        _emit 0x00
        _emit 0xE8        ; 0040E273  call 0x40b4c0
        _emit 0x48
        _emit 0xD2
        _emit 0xFF
        _emit 0xFF
        _emit 0x83        ; 0040E278  add esp, 0x58
        _emit 0xC4
        _emit 0x58
        _emit 0x5F        ; 0040E27B  pop edi
        _emit 0x5E        ; 0040E27C  pop esi
        _emit 0x5D        ; 0040E27D  pop ebp
        _emit 0x5B        ; 0040E27E  pop ebx
        _emit 0x83        ; 0040E27F  add esp, 0x6c
        _emit 0xC4
        _emit 0x6C
        _emit 0xC3        ; 0040E282  ret
        _emit 0x8B        ; 0040E283  mov eax, dword ptr [ecx + 0x4c]
        _emit 0x41
        _emit 0x4C
        _emit 0x8B        ; 0040E286  mov edx, dword ptr [ecx + 0x48]
        _emit 0x51
        _emit 0x48
        _emit 0x50        ; 0040E289  push eax
        _emit 0x8B        ; 0040E28A  mov eax, dword ptr [ecx + 0x44]
        _emit 0x41
        _emit 0x44
        _emit 0x52        ; 0040E28D  push edx
        _emit 0x8B        ; 0040E28E  mov edx, dword ptr [ecx + 0xc]
        _emit 0x51
        _emit 0x0C
        _emit 0x50        ; 0040E291  push eax
        _emit 0x8B        ; 0040E292  mov eax, dword ptr [ecx + 8]
        _emit 0x41
        _emit 0x08
        _emit 0x6A        ; 0040E295  push 0
        _emit 0x00
        _emit 0x68        ; 0040E297  push 0x240
        _emit 0x40
        _emit 0x02
        _emit 0x00
        _emit 0x00
        _emit 0x68        ; 0040E29C  push 0x200
        _emit 0x00
        _emit 0x02
        _emit 0x00
        _emit 0x00
        _emit 0x6A        ; 0040E2A1  push 0x40
        _emit 0x40
        _emit 0x68        ; 0040E2A3  push 0xc0
        _emit 0xC0
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x2D        ; 0040E2A8  sub eax, 0x100
        _emit 0x00
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x52        ; 0040E2AD  push edx
        _emit 0x50        ; 0040E2AE  push eax
        _emit 0x68        ; 0040E2AF  push 0x424f74
        _emit 0x74
        _emit 0x4F
        _emit 0x42
        _emit 0x00
        _emit 0xE8        ; 0040E2B4  call 0x40b4c0
        _emit 0x07
        _emit 0xD2
        _emit 0xFF
        _emit 0xFF
        _emit 0x83        ; 0040E2B9  add esp, 0x2c
        _emit 0xC4
        _emit 0x2C
        _emit 0x5F        ; 0040E2BC  pop edi
        _emit 0x5E        ; 0040E2BD  pop esi
        _emit 0x5D        ; 0040E2BE  pop ebp
        _emit 0x5B        ; 0040E2BF  pop ebx
        _emit 0x83        ; 0040E2C0  add esp, 0x6c
        _emit 0xC4
        _emit 0x6C
        _emit 0xC3        ; 0040E2C3  ret
        _emit 0x8B        ; 0040E2C4  mov ecx, dword ptr [ecx + 0x156]
        _emit 0x89
        _emit 0x56
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0xB8        ; 0040E2CA  mov eax, 0x66666667
        _emit 0x67
        _emit 0x66
        _emit 0x66
        _emit 0x66
        _emit 0xF7        ; 0040E2CF  imul ecx
        _emit 0xE9
        _emit 0xC1        ; 0040E2D1  sar edx, 3
        _emit 0xFA
        _emit 0x03
        _emit 0x8B        ; 0040E2D4  mov ecx, edx
        _emit 0xCA
        _emit 0xC1        ; 0040E2D6  shr ecx, 0x1f
        _emit 0xE9
        _emit 0x1F
        _emit 0x03        ; 0040E2D9  add edx, ecx
        _emit 0xD1
        _emit 0x8B        ; 0040E2DB  mov eax, edx
        _emit 0xC2
        _emit 0x4A        ; 0040E2DD  dec edx
        _emit 0x85        ; 0040E2DE  test eax, eax
        _emit 0xC0
        _emit 0x0F        ; 0040E2E0  je 0x40e3e0
        _emit 0x84
        _emit 0xFA
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0xA1        ; 0040E2E6  mov eax, dword ptr [0x4246cc]
        _emit 0xCC
        _emit 0x46
        _emit 0x42
        _emit 0x00
        _emit 0x42        ; 0040E2EB  inc edx
        _emit 0x89        ; 0040E2EC  mov dword ptr [esp + 0x64], edx
        _emit 0x54
        _emit 0x24
        _emit 0x64
        _emit 0x8B        ; 0040E2F0  mov ebx, dword ptr [0x424704]
        _emit 0x1D
        _emit 0x04
        _emit 0x47
        _emit 0x42
        _emit 0x00
        _emit 0xBF        ; 0040E2F6  mov edi, 0x1de
        _emit 0xDE
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x85        ; 0040E2FB  test ebx, ebx
        _emit 0xDB
        _emit 0x8D        ; 0040E2FD  lea ecx, [eax + 2]
        _emit 0x48
        _emit 0x02
        _emit 0x8D        ; 0040E300  lea edx, [eax + 0x500]
        _emit 0x90
        _emit 0x00
        _emit 0x05
        _emit 0x00
        _emit 0x00
        _emit 0x8D        ; 0040E306  lea esi, [eax + 0x502]
        _emit 0xB0
        _emit 0x02
        _emit 0x05
        _emit 0x00
        _emit 0x00
        _emit 0x74        ; 0040E30C  je 0x40e36e
        _emit 0x60
        _emit 0x89        ; 0040E30E  mov dword ptr [esp + 0x68], edi
        _emit 0x7C
        _emit 0x24
        _emit 0x68
        _emit 0xBF        ; 0040E312  mov edi, 0x27e
        _emit 0x7E
        _emit 0x02
        _emit 0x00
        _emit 0x00
        _emit 0x66        ; 0040E317  mov bx, word ptr [esi]
        _emit 0x8B
        _emit 0x1E
        _emit 0x66        ; 0040E31A  mov bp, word ptr [edx]
        _emit 0x8B
        _emit 0x2A
        _emit 0xC1        ; 0040E31D  shr ebx, 2
        _emit 0xEB
        _emit 0x02
        _emit 0xC1        ; 0040E320  shr ebp, 2
        _emit 0xED
        _emit 0x02
        _emit 0x81        ; 0040E323  and ebx, 0x39e7
        _emit 0xE3
        _emit 0xE7
        _emit 0x39
        _emit 0x00
        _emit 0x00
        _emit 0x81        ; 0040E329  and ebp, 0x39e7
        _emit 0xE5
        _emit 0xE7
        _emit 0x39
        _emit 0x00
        _emit 0x00
        _emit 0x03        ; 0040E32F  add ebx, ebp
        _emit 0xDD
        _emit 0x66        ; 0040E331  mov bp, word ptr [ecx]
        _emit 0x8B
        _emit 0x29
        _emit 0xC1        ; 0040E334  shr ebp, 2
        _emit 0xED
        _emit 0x02
        _emit 0x81        ; 0040E337  and ebp, 0x39e7
        _emit 0xE5
        _emit 0xE7
        _emit 0x39
        _emit 0x00
        _emit 0x00
        _emit 0x83        ; 0040E33D  add eax, 2
        _emit 0xC0
        _emit 0x02
        _emit 0x03        ; 0040E340  add ebx, ebp
        _emit 0xDD
        _emit 0x66        ; 0040E342  mov bp, word ptr [eax - 2]
        _emit 0x8B
        _emit 0x68
        _emit 0xFE
        _emit 0xC1        ; 0040E346  shr ebp, 2
        _emit 0xED
        _emit 0x02
        _emit 0x81        ; 0040E349  and ebp, 0x39e7
        _emit 0xE5
        _emit 0xE7
        _emit 0x39
        _emit 0x00
        _emit 0x00
        _emit 0x83        ; 0040E34F  add ecx, 2
        _emit 0xC1
        _emit 0x02
        _emit 0x03        ; 0040E352  add ebx, ebp
        _emit 0xDD
        _emit 0x83        ; 0040E354  add edx, 2
        _emit 0xC2
        _emit 0x02
        _emit 0x66        ; 0040E357  mov word ptr [eax - 2], bx
        _emit 0x89
        _emit 0x58
        _emit 0xFE
        _emit 0x83        ; 0040E35B  add esi, 2
        _emit 0xC6
        _emit 0x02
        _emit 0x4F        ; 0040E35E  dec edi
        _emit 0x75        ; 0040E35F  jne 0x40e317
        _emit 0xB6
        _emit 0x8B        ; 0040E361  mov edi, dword ptr [esp + 0x68]
        _emit 0x7C
        _emit 0x24
        _emit 0x68
        _emit 0x4F        ; 0040E365  dec edi
        _emit 0x89        ; 0040E366  mov dword ptr [esp + 0x68], edi
        _emit 0x7C
        _emit 0x24
        _emit 0x68
        _emit 0x75        ; 0040E36A  jne 0x40e312
        _emit 0xA6
        _emit 0xEB        ; 0040E36C  jmp 0x40e3cc
        _emit 0x5E
        _emit 0x89        ; 0040E36E  mov dword ptr [esp + 0x68], edi
        _emit 0x7C
        _emit 0x24
        _emit 0x68
        _emit 0xBF        ; 0040E372  mov edi, 0x27e
        _emit 0x7E
        _emit 0x02
        _emit 0x00
        _emit 0x00
        _emit 0x66        ; 0040E377  mov bx, word ptr [esi]
        _emit 0x8B
        _emit 0x1E
        _emit 0x66        ; 0040E37A  mov bp, word ptr [edx]
        _emit 0x8B
        _emit 0x2A
        _emit 0xC1        ; 0040E37D  shr ebx, 2
        _emit 0xEB
        _emit 0x02
        _emit 0xC1        ; 0040E380  shr ebp, 2
        _emit 0xED
        _emit 0x02
        _emit 0x81        ; 0040E383  and ebx, 0x1ce7
        _emit 0xE3
        _emit 0xE7
        _emit 0x1C
        _emit 0x00
        _emit 0x00
        _emit 0x81        ; 0040E389  and ebp, 0x1ce7
        _emit 0xE5
        _emit 0xE7
        _emit 0x1C
        _emit 0x00
        _emit 0x00
        _emit 0x03        ; 0040E38F  add ebx, ebp
        _emit 0xDD
        _emit 0x66        ; 0040E391  mov bp, word ptr [ecx]
        _emit 0x8B
        _emit 0x29
        _emit 0xC1        ; 0040E394  shr ebp, 2
        _emit 0xED
        _emit 0x02
        _emit 0x81        ; 0040E397  and ebp, 0x1ce7
        _emit 0xE5
        _emit 0xE7
        _emit 0x1C
        _emit 0x00
        _emit 0x00
        _emit 0x83        ; 0040E39D  add eax, 2
        _emit 0xC0
        _emit 0x02
        _emit 0x03        ; 0040E3A0  add ebx, ebp
        _emit 0xDD
        _emit 0x66        ; 0040E3A2  mov bp, word ptr [eax - 2]
        _emit 0x8B
        _emit 0x68
        _emit 0xFE
        _emit 0xC1        ; 0040E3A6  shr ebp, 2
        _emit 0xED
        _emit 0x02
        _emit 0x81        ; 0040E3A9  and ebp, 0x1ce7
        _emit 0xE5
        _emit 0xE7
        _emit 0x1C
        _emit 0x00
        _emit 0x00
        _emit 0x83        ; 0040E3AF  add ecx, 2
        _emit 0xC1
        _emit 0x02
        _emit 0x03        ; 0040E3B2  add ebx, ebp
        _emit 0xDD
        _emit 0x83        ; 0040E3B4  add edx, 2
        _emit 0xC2
        _emit 0x02
        _emit 0x66        ; 0040E3B7  mov word ptr [eax - 2], bx
        _emit 0x89
        _emit 0x58
        _emit 0xFE
        _emit 0x83        ; 0040E3BB  add esi, 2
        _emit 0xC6
        _emit 0x02
        _emit 0x4F        ; 0040E3BE  dec edi
        _emit 0x75        ; 0040E3BF  jne 0x40e377
        _emit 0xB6
        _emit 0x8B        ; 0040E3C1  mov edi, dword ptr [esp + 0x68]
        _emit 0x7C
        _emit 0x24
        _emit 0x68
        _emit 0x4F        ; 0040E3C5  dec edi
        _emit 0x89        ; 0040E3C6  mov dword ptr [esp + 0x68], edi
        _emit 0x7C
        _emit 0x24
        _emit 0x68
        _emit 0x75        ; 0040E3CA  jne 0x40e372
        _emit 0xA6
        _emit 0x8B        ; 0040E3CC  mov ecx, dword ptr [esp + 0x64]
        _emit 0x4C
        _emit 0x24
        _emit 0x64
        _emit 0xA1        ; 0040E3D0  mov eax, dword ptr [0x4246cc]
        _emit 0xCC
        _emit 0x46
        _emit 0x42
        _emit 0x00
        _emit 0x49        ; 0040E3D5  dec ecx
        _emit 0x89        ; 0040E3D6  mov dword ptr [esp + 0x64], ecx
        _emit 0x4C
        _emit 0x24
        _emit 0x64
        _emit 0x0F        ; 0040E3DA  jne 0x40e2f0
        _emit 0x85
        _emit 0x10
        _emit 0xFF
        _emit 0xFF
        _emit 0xFF
        _emit 0x5F        ; 0040E3E0  pop edi
        _emit 0x5E        ; 0040E3E1  pop esi
        _emit 0x5D        ; 0040E3E2  pop ebp
        _emit 0x5B        ; 0040E3E3  pop ebx
        _emit 0x83        ; 0040E3E4  add esp, 0x6c
        _emit 0xC4
        _emit 0x6C
        _emit 0xC3        ; 0040E3E7  ret
        _emit 0x79        ; 0040E3E8  jns 0x40e3b6
        _emit 0xCC
        _emit 0x40        ; 0040E3EA  inc eax
        _emit 0x00        ; 0040E3EB  add byte ptr [ecx - 0x34], bh
        _emit 0x79
        _emit 0xCC
        _emit 0x40        ; 0040E3EE  inc eax
        _emit 0x00        ; 0040E3EF  add byte ptr [ebx - 0x34], bl
        _emit 0x5B
        _emit 0xCC
        _emit 0x40        ; 0040E3F2  inc eax
        _emit 0x00        ; 0040E3F3  add byte ptr [ebp - 0x34], ah
        _emit 0x65
        _emit 0xCC
        _emit 0x40        ; 0040E3F6  inc eax
        _emit 0x00        ; 0040E3F7  add byte ptr [edi - 0x34], ch
        _emit 0x6F
        _emit 0xCC
        _emit 0x40        ; 0040E3FA  inc eax
        _emit 0x00        ; 0040E3FB  add byte ptr [ecx - 0x34], bh
        _emit 0x79
        _emit 0xCC
        _emit 0x40        ; 0040E3FE  inc eax
        _emit 0x00        ; 0040E3FF  add byte ptr [ebp - 0x43ffbf32], dl
        _emit 0x95
        _emit 0xCE
        _emit 0x40
        _emit 0x00
        _emit 0xBC
        _emit 0xCE        ; 0040E405  into
        _emit 0x40        ; 0040E406  inc eax
        _emit 0x00        ; 0040E407  add dl, bl
        _emit 0xDA
        _emit 0xCE        ; 0040E409  into
        _emit 0x40        ; 0040E40A  inc eax
        _emit 0x00        ; 0040E40B  add byte ptr [ebx - 0x64ffbf32], dh
        _emit 0xB3
        _emit 0xCE
        _emit 0x40
        _emit 0x00
        _emit 0x9B
        _emit 0xCF        ; 0040E411  iretd
        _emit 0x40        ; 0040E412  inc eax
        _emit 0x00        ; 0040E413  add byte ptr [ecx + edx*8 + 0x40], al
        _emit 0x44
        _emit 0xD1
        _emit 0x40
        _emit 0x00        ; 0040E417  add byte ptr [ecx + edx*8 + 0x40], al
        _emit 0x44
        _emit 0xD1
        _emit 0x40
        _emit 0x00        ; 0040E41B  add byte ptr [ebx - 0x64ffbf2d], bl
        _emit 0x9B
        _emit 0xD3
        _emit 0x40
        _emit 0x00
        _emit 0x9B
        _emit 0xD3        ; 0040E421  rol dword ptr [eax], cl
        _emit 0x40
        _emit 0x00
        _emit 0x1A        ; 0040E424  sbb dl, dl
        _emit 0xD2
        _emit 0x40        ; 0040E426  inc eax
        _emit 0x00        ; 0040E427  add byte ptr [ecx + edx*8 + 0x40], al
        _emit 0x44
        _emit 0xD1
        _emit 0x40
        _emit 0x00        ; 0040E42B  add byte ptr [esi - 0x2b], dh
        _emit 0x76
        _emit 0xD5
        _emit 0x40        ; 0040E42E  inc eax
        _emit 0x00        ; 0040E42F  add byte ptr [esi - 0x2b], dh
        _emit 0x76
        _emit 0xD5
        _emit 0x40        ; 0040E432  inc eax
        _emit 0x00        ; 0040E433  add byte ptr [edx - 0x5bffbf2b], cl
        _emit 0x8A
        _emit 0xD5
        _emit 0x40
        _emit 0x00
        _emit 0xA4
        _emit 0xD5        ; 0040E439  aad 0x40
        _emit 0x40
        _emit 0x00        ; 0040E43B  add byte ptr [ecx + 0x760040d5], dl
        _emit 0x91
        _emit 0xD5
        _emit 0x40
        _emit 0x00
        _emit 0x76
        _emit 0xD5        ; 0040E441  aad 0x40
        _emit 0x40
        _emit 0x00        ; 0040E443  add byte ptr [eax - 0x5fffbf2a], ah
        _emit 0xA0
        _emit 0xD6
        _emit 0x40
        _emit 0x00
        _emit 0xA0
        _emit 0xD6        ; 0040E449  salc
        _emit 0x40        ; 0040E44A  inc eax
        _emit 0x00        ; 0040E44B  add byte ptr [ebx], al
        _emit 0x03
        _emit 0xDD        ; 0040E44D  fld qword ptr [eax]
        _emit 0x40
        _emit 0x00
        _emit 0x03        ; 0040E450  add ebx, ebp
        _emit 0xDD
        _emit 0x40        ; 0040E452  inc eax
        _emit 0x00        ; 0040E453  add byte ptr [ecx - 0x26], al
        _emit 0x41
        _emit 0xDA
        _emit 0x40        ; 0040E456  inc eax
        _emit 0x00        ; 0040E457  add byte ptr [eax - 0x49ffbf2a], ah
        _emit 0xA0
        _emit 0xD6
        _emit 0x40
        _emit 0x00
        _emit 0xB6
        _emit 0xDD        ; 0040E45D  fld qword ptr [eax]
        _emit 0x40
        _emit 0x00
        _emit 0xE6        ; 0040E460  out 0xdd, al
        _emit 0xDD
        _emit 0x40        ; 0040E462  inc eax
        _emit 0x00        ; 0040E463  add byte ptr [ebx], bl
        _emit 0x1B
        _emit 0xDE        ; 0040E465  fiadd word ptr [eax]
        _emit 0x40
        _emit 0x00
        _emit 0x44        ; 0040E468  inc esp
        _emit 0xDE        ; 0040E469  fiadd word ptr [eax]
        _emit 0x40
        _emit 0x00
        _emit 0x7F        ; 0040E46C  jg 0x40e44c
        _emit 0xDE
        _emit 0x40        ; 0040E46E  inc eax
        _emit 0x00        ; 0040E46F  add byte ptr [eax], al
        _emit 0x00
        _emit 0x01        ; 0040E471  add dword ptr [esp + eax], eax
        _emit 0x04
        _emit 0x04
        _emit 0x04        ; 0040E474  add al, 4
        _emit 0x04
        _emit 0x04        ; 0040E476  add al, 4
        _emit 0x04
        _emit 0x04        ; 0040E478  add al, 4
        _emit 0x04
        _emit 0x02        ; 0040E47A  add al, byte ptr [ebx]
        _emit 0x03
        _emit 0xC4        ; 0040E47C  .byte 0xc4
        _emit 0xE2        ; 0040E47D  loop 0x40e4bf
        _emit 0x40
        _emit 0x00        ; 0040E47F  add al, ah
        _emit 0xE0
        _emit 0xE3        ; 0040E481  jecxz 0x40e4c3
        _emit 0x40
        _emit 0x00        ; 0040E483  add byte ptr [ebx + 0x6d0040e2], al
        _emit 0x83
        _emit 0xE2
        _emit 0x40
        _emit 0x00
        _emit 0x6D
        _emit 0xE1        ; 0040E489  loope 0x40e4cb
        _emit 0x40
        _emit 0x00        ; 0040E48B  add al, dh
        _emit 0xF0
        _emit 0xE0        ; 0040E48D  loopne 0x40e4cf
        _emit 0x40
        _emit 0x00        ; 0040E48F  add bl, al
        _emit 0xC3
        _emit 0xE0        ; 0040E491  loopne 0x40e4d3
        _emit 0x40
        _emit 0x00        ; 0040E493  add byte ptr [edx - 0x20], dh
        _emit 0x72
        _emit 0xE0
        _emit 0x40        ; 0040E496  inc eax
        _emit 0x00        ; 0040E497  add byte ptr [edx], al
        _emit 0x02
        _emit 0xE0        ; 0040E499  loopne 0x40e4db
        _emit 0x40
        _emit 0x00        ; 0040E49B  add byte ptr [ebx], bh
        _emit 0x3B
        _emit 0xDF        ; 0040E49D  fild word ptr [eax]
        _emit 0x40
        _emit 0x00
    }
}
