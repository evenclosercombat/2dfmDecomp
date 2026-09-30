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
 * Not byte-identical: VC6 compiles this C to slightly different register and stack-slot choices.
 * The `main` branch builds this function from the original's machine code instead.
 * Still different: 2304 vs 2336 bytes. The face-picture calls pass iX/iY locals (the original evaluates x before
 * y).  Left: the constant-1 register (orig edi, ours ebp) - which then leaves the original a free ebp for
 * a constant-0 register in the VS case (cmp eax,ebp / mov [..],ebp), so that case's 2P stock block is
 * not cross-jumped into the story one as ours is (-> the goto set_script); wIdx is kept in di by the
 * original but spilled by us; the -1 block reloads iWork016E in the original. Case order, variable
 * choice, declaration order and the form of the first if have no effect.
 */
void vjmpHandleBattleInterface(void)
{
    kgtEngineObject *pObj;
    int i;
    int iX, iY;         /* position of the next object (16.16) */
    int iDx, iDy;       /* spacing of the 1P victory marks (16.16) */
    int iDx2, iDy2;     /* spacing of the 2P victory marks */
    int bMarked;        /* the side's first "not won" mark has been remembered */
    int iAlive, iLastAlive;     /* story: CPUs still standing / the last one found */
    WORD wIdx;          /* unused */

    /* the story target expires */
    if (gkgtGameState.dwTargetPlayerTimer != 0 && --gkgtGameState.dwTargetPlayerTimer == 0)
        gkgtGameState.iTargetPlayer = -1;

    switch (gpkgtCurrentEngineObject->iProcessStep) {
    case 0:
        /* set up; no maximum may be 0 (the gauges divide by them) */
        gpkgtCurrentEngineObject->iProcessStep = 1;
        for (i = 0; i < 8; i++) {
            if (gkgtLoadedCharacter[i].iLifeMax == 0)
                gkgtLoadedCharacter[i].iLifeMax = 1;
            if (gkgtLoadedCharacter[i].iSpecialMax == 0)
                gkgtLoadedCharacter[i].iSpecialMax = 1;
        }
        kgtoNewEngineObject(UPDATE_TIMER, 1, 0, 0);

        /* victory marks 1P: one per win needed, at x/y + n * spacing; the first "not won" one is
           remembered (iPlayerIdx) with the wins it stands for (iObjectType) */
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
                /* matching: reading the skill through &pObj->iSkillIdx makes VC6 re-read the field
                   (vc6-matching-notes/matching-techniques.md, "Reading several fields through pointers") */
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

        /* victory marks 2P (remembered in pWork015E / pWork0162) */
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

        /* face pictures (the characters' stage face skill); story mode: only the player's, and only
           if its face skill has more than one step before the R1 skill that follows it (the
           opponent's face is created later, when there is a target) */
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

            /* 2P's face, mirrored */
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

        /* life and special gauges: iPlayerIdx 10 / 11 = 1P / 2P life, 20 / 21 = 1P / 2P special */
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

        /* special stock numbers: digit objects without a skill yet; counts shown = -1 so that the
           first update below sets them */
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
        return;
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
        /* *(int *)pObj is the face object's iJumpIdx: 1 = RESET_IDX, i.e. it was ended */
        if (gpkgtCurrentEngineObject->iWork0176 != 0 && *(int *)gpkgtCurrentEngineObject->iWork0176 == 1)
            gpkgtCurrentEngineObject->iWork0176 = 0;
        /* no target: end the face; a target: show its face (create the face object, mirrored, at the
           2P face position the first time) */
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
        /* stock digits: 1P's count, and the target's count on the other side (story) - digit n is
           system skill (&shSkillIdxSpecialStockNumber0)[n], restarted when the count changes */
        i = 1;
        if (gpkgtCurrentEngineObject->iWork016A != gkgtLoadedCharacter[0].iSpecialGaugeTokens) {
            gpkgtCurrentEngineObject->iWork016A = gkgtLoadedCharacter[0].iSpecialGaugeTokens;
            OBJ(gpkgtCurrentEngineObject->iWork0166)->iSkillIdx = OBJ(gpkgtCurrentEngineObject->iWork0166)->iStartSkillIdx = (WORD)(&gkgtKgtSystem.shSkillIdxSpecialStockNumber0)[gkgtLoadedCharacter[0].iSpecialGaugeTokens];
            OBJ(gpkgtCurrentEngineObject->iWork0166)->iImageWaitFrames = 0;
            OBJ(gpkgtCurrentEngineObject->iWork0166)->iSkillScriptIdx = (WORD)gkgtKgtSystem.kgtCore.pSkillsAlloc[OBJ(gpkgtCurrentEngineObject->iWork0166)->iSkillIdx].shStartingStepIdx;
        }
        /* (always true here) no target: hide the second digit (-1 = the count is shown again when a
           target appears) */
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
set_script:     /* shared tail with the versus case below */
            OBJ(gpkgtCurrentEngineObject->iWork016E)->iSkillScriptIdx = (WORD)gkgtKgtSystem.kgtCore.pSkillsAlloc[OBJ(gpkgtCurrentEngineObject->iWork016E)->iSkillIdx].shStartingStepIdx;
        }
        break;
    case GAME_MODE_VS_SINGLE:
    case GAME_MODE_VS_TEAM:
        /* stock digits of 1P and 2P */
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
    /* a new win: the remembered mark of the side becomes "won" */
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
 * Not byte-identical: VC6 compiles this C to slightly different register and stack-slot choices.
 * The `main` branch builds this function from the original's machine code instead.
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
void vBlitImageRect16(kgtBMPINFO *pImage, int iX, int iY, int iRectW, int iRectH, int iSrcX, int iSrcY,
                      UINT uDrawFlags, int iTintR, int iTintG, int iTintB)
{
    BYTE * pSrc;            /* next source pixel */
    int iColors;            /* palette entries left to convert */
    int iRed, iGreen, iBlue;    /* tinted channels (0..31); iRed also the RGB565 red term of mode 2 */
    int iSkipX, iSkipY;     /* source pixels / pixel rows skipped by the left / top clipping */
    WORD * pDst;            /* next destination pixel */
    int iInvG, iInvB;       /* mode 4: source weights (32 - destination weight) */
    register int iStep;     /* +1, or -1 when mirrored */
    WORD * pPalEntry;       /* palette entry being converted */
    WORD * pTint;           /* gwTintedPalette16 entry being written */
    int iRow, iCol;         /* rows / pixels left */
    int iDstSkip;           /* 640 - iRectW: from the end of a row to the start of the next */
    WORD wColor, wDest;     /* modes 0 and 3: source and destination pixel */
    WORD wPalColor, wDestColor; /* mode 4: untinted source colour (RGB555) and destination pixel */
    WORD wColor1, wDest1;   /* mode 1 (wDest1 unused) */
    WORD wColor2, wDest2;   /* mode 2 */
    WORD * pPalette;        /* the bitmap's palette (RGB555) */
    int iStackPad1 = 0;     /* matching: unused padding locals, see the header */
    int iStackPad2 = 0;
    int iStackPad3 = 0;
    int iStackPad4 = 0;
    int iStackPad5 = 0;
    int iStackPad6 = 0;
    int iStackPad7 = 0;
    /* matching: dead stores and dead address-taking (see the header); all overwritten below */
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
    /* the palette, then the pixels */
    pPalette =(WORD *) pImage->pData;
    iColors = pImage->iColorsUsed + 1;
    pSrc =(BYTE *)(pPalette + pImage->iColorsUsed + 1);
    iSkipX = 0;
    iSkipY = 0;
    pPalEntry = pPalette;
    /* clip to the 640x480 screen */
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
    if (iRectW <= 0 || iRectH <= 0) return;
    /* tint the palette and convert it to the screen format; 0 stays 0 (transparent) */
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
            /* RGB565 (green's lowest bit 0) or RGB555 */
            if (*(volatile int *) & giScreenMode) * pTint++ =(((((iRed << 5))) + iGreen) << 6) + iBlue; else * pTint++ =(((iRed << 5) + iGreen) << 5) + iBlue;
        } else {
            * pTint++ = 0;
        }
    }
    /* first source pixel and, reusing iSrcY, the source step from the end of one drawn row to the start
       of the next.  The rows are stored bottom-up, so upright drawing starts at the bottom row of the
       rectangle and goes backwards; mirrored drawing starts at the right end of the row. */
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
        case 0 :    /* copy, colour 0 transparent */
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
        case 1 :    /* 50% mix: halve both (masks drop each channel's lowest bit so nothing carries
                       into the next channel: 0x7bef RGB565, 0x3def RGB555) and add */
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
        case 2 :    /* add with saturation */
        if (*(volatile int *) & giScreenMode) {
            /* RGB565: per channel, clamped */
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
                        /* RGB555 in one addition: add the channels without their top bits
                           (0x7bdf); a channel that overflowed has its top bit (0x8420) set
                           in wCarry, and m - (m >> 5) turns that bit into all-ones for the
                           channel (saturation) */
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
        case 3 :    /* subtract with saturation (per channel, results below one step become 0) */
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
        case 4 :    /* alpha: source weight 32 - tint, destination weight tint, / 32; the source is
                       the untinted RGB555 palette colour (pPalette), gwTintedPalette16 only
                       decides transparency.  iY is reused as the low channel's source weight. */
        iY = 32 - iTintR;
        iInvB = 32 - iTintB;
        iInvG = 32 - iTintG;
        if (*(volatile int *) & giScreenMode) {
            /* RGB565: the RGB555 source's top and middle channels must move up one bit */
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
        case 5 :    /* opaque copy (colour 0 drawn too) */
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
 * Not byte-identical: VC6 compiles this C to slightly different register and stack-slot choices.
 * The `main` branch builds this function from the original's machine code instead.
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
void vDrawKgtImage16(kgtImageHeader *pHeader, BYTE *pSrc, WORD *pPalette, int iX, int iY, int iDrawW, int iDrawH, int iFlags)
{
    int iSkipX, iSkipY;     /* source pixels / pixels of whole rows skipped by the left / top clipping */
    WORD *pDst;             /* next destination pixel */
    int iDstSkip;           /* 640 - iDrawW: from the end of a row to the start of the next */
    int iStackPad1 = 0;     /* matching: unused padding locals, see the header */
    int iStackPad2 = 0;
    int iStackPad3 = 0;
    int iStackPad4 = 0;
    int iStackPad5 = 0;
    int iStackPad6 = 0;
    int iStackPad7 = 0;
    int iStackPad8 = 0;
    int iStackPad9 = 0;

    if (0) vMemzero(&iX, 0);    /* matching: keeps iX in memory (see the header) */
    /* clip */
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
        return;
    pDst = (WORD *)gpFrameBits + iY * 640 + iX;
    iDstSkip = 640 - iDrawW;
    /* first source pixel; from here iY is the source step (+1 / -1 mirrored) and iX the source skip
       from the end of one drawn row to the start of the next (negative when flipped vertically: the
       rows are then read from the last one up) */
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
    case 0: {       /* copy, transparent where the palette entry is 0 */
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
        return;
    }
    case 1:         /* 50% mix: halve both with the channel masks 0x7bef (RGB565) / 0x3def (RGB555) and add */
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
        return;
    case 2:         /* add with saturation: RGB565 per channel; RGB555 in one addition (see
                       vBlitImageRect16 mode 2: carries of the 0x7bdf parts saturate via 0x8420) */
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
        return;
    case 3:         /* subtract with saturation, channel by channel */
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
        return;
    case 4: {       /* alpha: (dest * alpha + source * (32 - alpha)) / 32 per channel */
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
                        /* matching: the (short)/(WORD) casts on iAlpha/iInvAlpha/wDest below are value-neutral
                           for the bits each term keeps; they only steer VC6's conversion narrowing and
                           term evaluation order (vc6-matching-notes/matching-techniques.md) */
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
                        /* matching: value-neutral casts, as above */
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
        return;
    }
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
 * Not byte-identical: VC6 compiles this C to slightly different register and stack-slot choices.
 * The `main` branch builds this function from the original's machine code instead.
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
void vDrawCurrentEngineObject(void)
{
    int iSavedAOrRed;       /* the object's alpha saved during the after-image frames; then the red
                               channel of the palette conversion */
    int iBlurCol;           /* blur: pixels left in the row */
    int iBlurPasses;        /* blur: passes left */
    WORD * pTintDst;        /* gwTintedPalette16 entry being written */
    int iLayerRed;          /* after-image palette conversion: red channel */
    kgtImageHeader *pHeader;    /* the image of the current step */
    kgtSkillImageStep *pStep;   /* the current image step */
    kgt_character_struct *pChar;    /* the file the object uses (system/demo/stage cast to it: only
                                       kgtCore and the fields read here are used) */
    DWORD *pPalette;        /* palette in the file format (256 dwords B, G, R, x) */
    BYTE *pPixels;          /* the image's (decompressed) pixels */
    int iImgW, iImgH;       /* size drawn: row length and number of rows
                               (pHeader->iWidth / ->iHeight) */
    int bFlip;              /* mirror: the step's flag 0x4000, toggled when the object looks right */
    int iX, iY;             /* screen position (pixels) */
    int iRed, iGreen, iBlue;    /* palette conversion (iRed unused) */
    int k;                  /* palette entries left; hit box index (19..0); a temporary x (matching) */
    int iDrawW;             /* gauges: width left after the cut */
    WORD wImage;            /* held: the step's image number and flags */
    UINT uFlags;            /* flip flags passed to vDrawKgtImage16 */
    int iPlayer;            /* gauges: player shown */
    int bHitJudge;          /* copy of giHitJudge */
    int bHeldFlip;          /* held: mirror as the holding opponent */
    BYTE cScrollFlags;      /* stage: the scroll step's flags */
    kgtSkillStageScroll *pScroll;   /* stage: the scroll step (index in iDsGuardedSkillIdx) */
    DWORD *pLayerPalEntry;  /* after-image palette conversion: source entry */
    DWORD *pPalEntry;       /* palette conversion: source entry */
    WORD *pLayerTintDst;    /* after-image palette conversion: gwTintedPalette16 entry */
    int iSet;               /* after-image trail number (cAfterImageIdx, 1-based) */
    int *pAI;               /* the trail as ints: [1] next slot, [2] -> the AI step (pInfo), [3] frames
                               until the next capture; frame n at [(n + 1) * 4 + 0..3] = x, y (16.16),
                               flags, image step */
    kgtImageHeader *pPrevHeader;    /* image of the previous trail frame (not decompressed again) */
    kgtImageHeader *pLayerHeader;
    kgtSkillImageStep *pLayerStep;
    int iLayerW, iLayerH;
    int bLayerFlip;
    int iLayerImage;        /* the frame's image number and flip flags (wImage) */
    int iFrame, iSlot;      /* trail frames drawn / ring buffer slot (0..99) */
    int iSavedBlend, iSavedR, iSavedG, iSavedB, iSavedA;   /* the object's blend mode and colour,
                                                              restored after the trail (iSavedA unused) */
    int iPercent;           /* trail colour fade / random mix (percent) */
    kgtBMPINFO *pBmp;
    kgtEngineObject *pObj;  /* shadows: object scanned */
    WORD *pBlurTL, *pBlurTR, *pBlurBL, *pBlurBR;   /* blur: a pixel and its right, lower and lower
                                                     right neighbours */
    int iRow, iCol;         /* tiling position / blur rows left */
    int iInv;               /* 100 - iPercent */

    /* matching: dead initial stores */
    pPalette = 0;
    pLayerTintDst = 0;
    if (gpkgtCurrentEngineObject->iDrawFlag == -1) {
        if (0) goto boundary1; boundary1:;
        /* the file whose skills the object runs */
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
        /* the image step shown is the one before the next step to run */
        pStep = (kgtSkillImageStep *)&pChar->kgtCore.pSkillScriptsAlloc[gpkgtCurrentEngineObject->iSkillScriptIdx - 1];
        pHeader = &pChar->kgtCore.pImageHeaders[pStep->wImage & 0x1fff];
        if (pHeader->pAlloc == NULL)
            return;
        iImgW = pHeader->iWidth;
        iImgH = pHeader->iHeight;
        if (0) goto boundary4; boundary4:;
        if (0) goto boundary5; boundary5:;
        /* the step's mirror flag (0x4000); palette: system/demo/stage files use their first palette,
           characters the one of their colour (0x108 dwords each), or the first when no colour is set */
        bFlip = (pStep->wImage & 0x4000) >> 14;
        if (gpkgtCurrentEngineObject->iObjectType > STORY_ENGINE_OBJECT)
            pPalette = (DWORD *)pChar->kgtCore.kgtPalettes;
        else if (pChar->iColor < 0)
            pPalette = (DWORD *)pChar->kgtCore.kgtPalettes;
        else
            pPalette = (DWORD *)pChar->kgtCore.kgtPalettes + pChar->iColor * 0x108;

        /* the after-image trail: its last pInfo[3] captured frames, oldest first, each with the
           colour mode pInfo[6] (0 the object's colour, 1 the trail's colour, 2 fading from the trail's
           colour (oldest frame) to the object's (newest), 3 the trail's colour on odd frames of
           giReverseShakeDirection and 2 on the others, 4 a random mix) in blend mode pInfo[5];
           pInfo[4] = 0 switches the trail's drawing off */
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
                    /* frame iSlot: its image step, flip flags (bit 2: mirrored) and position */
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
                            /* compressed images are unpacked into gpGlobalMemoryAlloc (not again for the same image);
                               an image with its own palette (iFlags bit 0) has it in the 0x400 bytes before the pixels */
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
                            /* the frame's colour */
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
                                /* older frames are closer to the trail's colour: iPercent is the
                                   frame's age in percent of the trail length (interval * count) */
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
                            /* tint the palette into gwTintedPalette16 (5 bits per channel, see the header) */
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
                                    /* RGB565 (green's lowest bit 0) / RGB555 */
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
                            /* screen position of the frame, as for the object itself below */
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
                /* the object's own blend mode and colour again */
                gpkgtCurrentEngineObject->iColorBlendtype = iSavedBlend;
                gpkgtCurrentEngineObject->iColorRed = iSavedR;
                gpkgtCurrentEngineObject->iColorGreen = iSavedG;
                gpkgtCurrentEngineObject->iColorBlue = iSavedB;
                gpkgtCurrentEngineObject->iColorAlpha = iSavedAOrRed;
            }
        }

        /* the object's own image: unpack it if compressed; its own palette if it has one */
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
        /* the file's colour effect (flash) sets the object's colour */
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
        /* tint the palette into gwTintedPalette16 */
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
                /* RGB565 (green's lowest bit 0) / RGB555 */
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
            /* characters */
            if (gpkgtCurrentEngineObject->iOpponentDowntimeInFrames == -1) {
                /* held by the opponent (iOpponentDowntimeInFrames -1): drawn with the opponent's
                   orientation, anchored at its top left, and flipped vertically if the step says so */
                bHeldFlip = pChar->pLastOpponent->iPlayerLookingRight;
                if (0) goto boundary64; boundary64:;
                wImage = pStep->wImage;
                if (0) goto boundary65; boundary65:;
                if (wImage & 0x4000)
                    bHeldFlip = bHeldFlip ^ 1;
                if (bHeldFlip) {
                    /* matching: x computed in three statements through k (see the header) */
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
                return;
            }
            /* the image is centred on the object's x (plus the step's offset) and stands on its y;
               looking right it is mirrored around x, unless the step keeps its direction (cFlags
               bit 0) */
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
            /* camera-relative (and shaken) */
            if (gpkgtCurrentEngineObject->iFlags & 0x40000000) {
                if (0) goto boundary77; boundary77:;
                if (0) goto boundary78; boundary78:;
                if (0) goto boundary79; boundary79:;
                iX -= giShakeXOffset + giCameraX;
                iY -= giShakeYOffset + giCameraY;
            }
            if (0) goto boundary80; boundary80:;
            /* hit judge (test play): the guard boxes and attack boxes filled below the image ... */
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
            /* ... and outlined above it; for players also the state: two 64x32 cells of text.bmp
               chosen by iStateFlags bits 2-3 (guard/hit) and bits 0-1 (stance) */
            if (!bHitJudge)
                return;
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
            return;
        case STAGE_ENGINE_OBJECT:
            /* stage: x from the object's x (mirrored: the image's right edge), shaken, not
               camera-relative; the scroll step moves it by shScrollX / shScrollY percent of the
               camera (parallax); a negative value also shifts it by that percent of the screen
               (640 * |s| / 100 = |s| * 64 / 10, 480 * |s| / 100 = |s| * 48 / 10) */
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
            /* tiling: repeat the image across the screen in x (2), y (4) or both */
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
                    return;
                }
                while (iX > 0)
                    iX -= iImgW;
                for (; iX < 640; iX += iImgW)
                    vDrawKgtImage16(pHeader, pPixels, gwTintedPalette16, iX, iY, iImgW, iImgH, (pStep->wImage & 0xc000) + bFlip);
                if (0) goto boundary102; boundary102:;
                return;
            }
            if (cScrollFlags & 4) {
                if (0) goto boundary103; boundary103:;
                while (iY > 0)
                    iY = iY - iImgH;
                if (0) goto boundary104; boundary104:;
                for (; iY < 480; iY += iImgH)
                    vDrawKgtImage16(pHeader, pPixels, gwTintedPalette16, iX, iY, iImgW, iImgH, (pStep->wImage & 0xc000) + bFlip);
                return;
            }
            vDrawKgtImage16(pHeader, pPixels, gwTintedPalette16, iX, iY, iImgW, iImgH, (pStep->wImage & 0xc000) + bFlip);
            if (0) goto boundary105; boundary105:;
            return;
        case SYSTEM_ENGINE_OBJECT:
        case DEMO_ENGINE_OBJECT:
            /* system/demo: x from the object's x (mirrored: the image's right edge) */
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
            /* camera-relative (and shaken) */
            if (gpkgtCurrentEngineObject->iFlags & 0x40000000) {
                if (0) goto boundary109; boundary109:;
                iX -= giShakeXOffset + giCameraX;
                iY -= giShakeYOffset + giCameraY;
            }
            if (0) goto boundary110; boundary110:;
            if (0) goto boundary111; boundary111:;
            /* gauges are cut according to the value they show: 1P gauges lose their left part (the
               pixels and x move right), 2P gauges (in story mode the target's) their right part */
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
                        return;
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
                        return;
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
            return;
        }
        vDrawKgtImage16(pHeader, pPixels, gwTintedPalette16, iX, iY, iImgW, iImgH, uFlags);
        if (0) goto boundary125; boundary125:;
        return;
    }

    if (gpkgtCurrentEngineObject->iDrawFlag > 0) {
        /* one of the built-in bitmaps (unpacked first if compressed) */
        pBmp = &gkgtBitmaps[gpkgtCurrentEngineObject->iDrawFlag];
        if (pBmp->pData == NULL)
            return;
        if (pBmp->iCompressedSize)
            iKgtDecompress((BYTE *)gpGlobalMemoryAlloc, (BYTE *)pBmp->pData, pBmp->iCompressedSize);
        vBlitImageRect16(pBmp, gpkgtCurrentEngineObject->iPosX / 0x10000, gpkgtCurrentEngineObject->iPosY / 0x10000,
                         pBmp->iWidth, pBmp->iHeight, 0, 0, gpkgtCurrentEngineObject->iColorBlendtype,
                         gpkgtCurrentEngineObject->iColorRed, gpkgtCurrentEngineObject->iColorGreen, gpkgtCurrentEngineObject->iColorBlue);
        return;
    }

    switch (gpkgtCurrentEngineObject->iDrawFlag) {
    case -2:    /* shadows of the script objects that have one (iFlags bit 31): a 128x32 cell of
                   text.bmp subtracted below the object at its ground level, fainter the higher it is */
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
    case -3:    /* effect animation (vjmpFadeDriftEffect): 128x128 frame iPlayerIdx / 8 of row iObjectType,
                   additive, centred on iPosX/iPosY (plain pixels) */
        vBlitImageRect16(&gkgtBitmaps[1],
                         gpkgtCurrentEngineObject->iPosX - giCameraX - 0x40,
                         gpkgtCurrentEngineObject->iPosY - giCameraY - 0x40,
                         0x80, 0x80, gpkgtCurrentEngineObject->iPlayerIdx / 8 * 0x80, gpkgtCurrentEngineObject->iObjectType * 0x80,
                         gpkgtCurrentEngineObject->iPlayerLookingRight * 0x40000000 + 2,
                         gpkgtCurrentEngineObject->iColorRed, gpkgtCurrentEngineObject->iColorGreen, gpkgtCurrentEngineObject->iColorBlue);
        break;
    case -4:    /* overlay: the top left 512x512 of text.bmp in alpha mode (vjmpScreenControl step 16) */
        vBlitImageRect16(&gkgtBitmaps[1],
                         gpkgtCurrentEngineObject->iPosX / 0x10000, gpkgtCurrentEngineObject->iPosY / 0x10000,
                         0x200, 0x200, 0, 0, 4,
                         gpkgtCurrentEngineObject->iColorRed, gpkgtCurrentEngineObject->iColorGreen, gpkgtCurrentEngineObject->iColorBlue);
        break;
    case -5:    /* iPlayerIdx in small digits at iPosX/iPosY (pixels) */
        if (0) goto boundary127; boundary127:;
        if (0) goto boundary128; boundary128:;
        vDrawNumberSmall(gpkgtCurrentEngineObject->iPlayerIdx, gpkgtCurrentEngineObject->iPosX, gpkgtCurrentEngineObject->iPosY, 0,
                         gpkgtCurrentEngineObject->iColorRed, gpkgtCurrentEngineObject->iColorGreen, gpkgtCurrentEngineObject->iColorBlue);
        break;
    case -6:    /* iPlayerIdx in large digits plus a 96x32 label from text.bmp, camera-relative */
        vDrawNumberLarge(gpkgtCurrentEngineObject->iPlayerIdx, gpkgtCurrentEngineObject->iPosX - giCameraX, gpkgtCurrentEngineObject->iPosY - giCameraY, 4,
                         gpkgtCurrentEngineObject->iColorRed, gpkgtCurrentEngineObject->iColorGreen, gpkgtCurrentEngineObject->iColorBlue);
        vBlitImageRect16(&gkgtBitmaps[1],
                         gpkgtCurrentEngineObject->iPosX - giCameraX + 0x20, gpkgtCurrentEngineObject->iPosY - giCameraY,
                         0x60, 0x20, 0, 0x40, 4,
                         gpkgtCurrentEngineObject->iColorRed, gpkgtCurrentEngineObject->iColorGreen, gpkgtCurrentEngineObject->iColorBlue);
        break;
    case -7:    /* "round N": the word, the round number's tens (if any) and ones digits, and a
                   256x92 picture below */
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
    case -8:    /* a 192x64 caption from text.bmp */
        vBlitImageRect16(&gkgtBitmaps[1],
                         gpkgtCurrentEngineObject->iPosX - 0x100, gpkgtCurrentEngineObject->iPosY,
                         0xc0, 0x40, 0x200, 0x240, 0,
                         gpkgtCurrentEngineObject->iColorRed, gpkgtCurrentEngineObject->iColorGreen, gpkgtCurrentEngineObject->iColorBlue);
        if (0) goto boundary132; boundary132:;
        pBlurTL = 0; pBlurTR = 0; pBlurBL = 0; pBlurBR = 0;    /* matching: dead stores that set the load
                                                                  order of the blur pointers (header) */
        break;
    case -10:   /* blur the whole screen iPlayerIdx / 20 times: each pixel becomes the average of
                   itself and its right, lower and lower right neighbours (a quarter of each, with the
                   masks 0xe79c (RGB565) / 0x739c (RGB555) clearing the 2 low bits of every channel) */
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
