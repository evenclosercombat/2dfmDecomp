/*
 * battle.c - the fighting itself (0x40e4a0-0x413d40): hit detection between attack and guard boxes,
 * damage, guard and hit reactions, pushing bodies apart and the screen/stage walls, reading command
 * inputs out of the input buffer, the CPU opponent, the built-in movement of a player (walk, crouch,
 * jump, guard, turn) and vjmpReadScript, the interpreter of the skill scripts that every READ_SCRIPT
 * engine object (players, their effects, system/demo/stage objects) runs each frame.
 *
 * Conventions used throughout:
 *   * positions, momentum and gravity are 16.16 fixed point (pixels << 16); box coordinates in the
 *     script steps are plain pixels, so the hit code works in pixels (iPosX / 0x10000);
 *   * kgtEngineObject.iPlayerLookingRight = 1 means the object is flipped (faces towards smaller x),
 *     and box x offsets are mirrored then;
 *   * input words (giInputBuffer, one DWORD per frame, 1024 frames per player): bit 0 back, bit 1
 *     forward, bit 2 up, bit 3 down (relative to the facing, except in guard-button mode where they are
 *     left/right), bits 4-9 buttons A-F, 0x100 also the line-switch button;
 *   * a "(step << 16) | skill" int names a script position (DS branches, hit junctions, loops).
 *
 * The second half (from vHandleCpuCommands) was originally a separate file (battle_b.c) appended to this one;
 * it repeats the extern declarations it needs.
 */
#include "kgt.h"

/* ---- externs not (yet) in globals.h / protos.h ------------------------------------------- */
extern kgtEngineObject *gpkgtCurrentEngineObject;  /* 0x4cfa00: object whose handler is running */
extern kgtEngineObject gkgtEngineObjects[1024];    /* 0x4701e0: all engine objects */
extern kgtGameState gkgtGameState;                 /* 0x470020: state of the current game */
extern unk_0x650_struct gAfterImageTrails[100];    /* 0x447f80: after-image trails (script command AI), kgtEngineObject.cAfterImageIdx - 1 */
extern int giInputBufferPos;                       /* 0x447ee0: current frame in giInputBuffer (0-1023) */
extern int giStoryModeSide;                        /* 0x424f24: copy of giStoryModePlayerIdx taken when the story character is chosen; indexes giCurrentStoryStep */
extern int giCurrentStoryStep[3];                  /* 0x424f28: story entry per side */
extern int giCameraX;                              /* 0x447f2c: camera x (pixels) */
extern int giCameraY;                              /* 0x447f30: camera y (pixels) */
extern int giHitJudge[2];                          /* 0x42470c: hit-judge display (hit boxes and player state), copy of giConfigTestplayHitjudge; the next int (0x424710) enables the line switch button */

kgtEngineObject *kgtoNewEngineObject(kgtJumptableEndpoints iJumpIdx, int iDepth, int iPosX, int iPosY);
void vAddToSpecialGauge(int iPlayerIdx, int iAdd);
void vAddToHealth(kgt_character_struct *pChar, int iLifeAdd);
/* ------------------------------------------------------------------------------------------ */

/* An attack (FA, 0x18) or guard (FD, 0x19) hit/hurtbox script step: the view of the steps that
   kgtEngineObject.pAttackBoxes / pGuardBoxes point into (same bytes as kgtScriptStep.box). */
#pragma pack(push, 1)
typedef struct kgtHitboxStep {
    char cType;                 /* 0x00 script command code (0x18 / 0x19; set to 0 by a clash, see vAdjustHitboxes) */
    short shX;                  /* 0x01 centre x relative to the object (pixels; mirrored when it is flipped) */
    short shY;                  /* 0x03 centre y relative to the object (pixels) */
    short shW;                  /* 0x05 half width (pixels); 0 clears the slot */
    short shH;                  /* 0x07 half height (pixels); 0 clears the slot */
    BYTE cIndex;                /* 0x09 slot in pAttackBoxes / pGuardBoxes (0-19) */
    BYTE cFlags;                /* 0x0a FA: 1 cancellable, 2 continuous hit, 4 shaves life through guard, 8 misses a guarding
                                   opponent, 0x10 misses on the ground, 0x20 misses in the air, 0x40 unblockable (guard fail),
                                   0x80 misses an opponent in a hit reaction;
                                   FD: 1 body (pushes other bodies away), 2 hurt box, 4 throw hurt box */
    BYTE cFlags2;               /* 0x0b FA: 0x80 against a box without power both boxes are switched off (clash);
                                   FD: damage taken through this box, percent of the attack power */
    BYTE cPower;                /* 0x0c FA: attack power (damage before corrections), 0 = no damage */
    BYTE pad_0d[3];
} kgtHitboxStep;

/* The reaction step (script command 0x17), kgtEngineObject.pReactionStep: the hit junction
   (kgt_character_struct.kgtHitJunctions index) the opponent takes, by its situation; 0 = none. */
typedef struct kgtReactionStep {
    char cType;                 /* 0x00 script command code (0x17) */
    WORD wStand;                /* 0x01 opponent standing */
    WORD wCrouch;               /* 0x03 opponent crouching (holding down) */
    WORD wAir;                  /* 0x05 opponent in the air */
    WORD wStandGuard;           /* 0x07 opponent guarding standing */
    WORD wCrouchGuard;          /* 0x09 opponent guarding crouched */
    WORD wAirGuard;             /* 0x0b opponent guarding in the air */
    BYTE pad_0d[3];
} kgtReactionStep;

/* Command branch step (script command 0x24): a command to be entered within cTime frames; the
   skill/step to branch to overlay bytes 1-3 (kgtScriptStep.jump). */
typedef struct kgtComStep {
    char cType;                 /* 0x00 script command code (0x24) */
    BYTE pad_01[3];             /* 0x01 branch skill (WORD) and step, read through kgtScriptStep.jump */
    BYTE cTime;                 /* 0x04 frames searched back in the input buffer; 0 = never */
    WORD wInputs[5];            /* 0x05 same format as kgtCharacterCommand.shCommandInputs (only presses are checked) */
    BYTE pad_0f;
} kgtComStep;
#pragma pack(pop)

/* kgtEngineObject.pWork015E read as the flag word it shares memory with (kgtEngineObject.iStateFlags):
   bits 0-1 stance (0 standing, 1 crouching, 2 in the air); bits 2-3: 0 free, 4 in an action (skill),
   8 in a hit reaction, 0xc guarding; 0x10 its attack has already hit (no further hits until the
   script clears it, see FA flag 2) */
#define OBJ_FLAGS(pObj) (*(int *)&(pObj)->pWork015E)

int giObjectLayers[2] = { 0x50, 0x46 };                    /* 0x41f130: player depth by line (kgtEngineObject.iLine & 1) */
int giCpuDirTableA[16] = { 0, 0, 2, 10, 8, 9, 1, 5, 4, 6, 1, 4, 2, 8, 0, 0 };  /* 0x41f138: CPU command direction -> input bits */
int giCpuDirTableB[16] = { 0, 0, 1, 9, 8, 10, 2, 6, 4, 5, 2, 4, 1, 8, 0, 0 };  /* 0x41f178: the same, mirrored */
/* debug message format for an object deleted off screen (not used by the code; vjmpReadScript
   has its own copy of the text in dead code) */
char gszObjDeleted[] = "OBJ\217\301\226\305 / %d , %d / %s";  /* OBJ消滅 / %d , %d / %s */

/*
 * Deletes the current engine object: an object created by a character (STORY_ENGINE_OBJECT) is removed from the owner's
 * M-number slots, its after-image trail is released, and its handler becomes RESET_IDX so the engine
 * frees it.  Called when a script ends or an object leaves the screen or loses its parent.
 * Globals: reads gpkgtCurrentEngineObject; changes gkgtLoadedCharacter[].pMNumberObjs and
 * gAfterImageTrails[].bInUse.
 */
void vDeleteCurrentEngineObject(void)
{
    kgtEngineObject *pObj = gpkgtCurrentEngineObject;
    kgt_character_struct *pChar;
    int iMNumber;

    /* forget it in the owner's M-number table (script command O) */
    if (pObj->iObjectType == STORY_ENGINE_OBJECT) {
        pChar = &gkgtLoadedCharacter[pObj->iPlayerIdx];
        for (iMNumber = 0; iMNumber < 10; iMNumber++) {
            if (pChar->pMNumberObjs[iMNumber] == pObj)
                pChar->pMNumberObjs[iMNumber] = NULL;
        }
    }
    /* release its after-image trail (cAfterImageIdx is 1-based) */
    if (pObj->cAfterImageIdx)
        gAfterImageTrails[pObj->cAfterImageIdx - 1].bInUse = 0;
    /* the engine deletes it on its next pass */
    pObj->iJumpIdx = RESET_IDX;
}

/*
 * Clears the six DS branch registrations (script command DS: landing, attack hits, clash, wall hit,
 * offset, throw) of the current object, if it is a player or a character's object.
 * Returns nothing (void despite the Ghidra name's i prefix).
 * Globals: reads gpkgtCurrentEngineObject.
 */
void iResetDsSkillIndices(void)
{
    kgtEngineObject *pObj = gpkgtCurrentEngineObject;

    if (pObj->iObjectType <= STORY_ENGINE_OBJECT) {
        pObj->iDsLandingSkillIdx = 0;
        pObj->iDsGuardedSkillIdx = 0;
        pObj->iDsAttackHitsSkillIdx = 0;
        pObj->iDsWallHitSkillIdx = 0;
        pObj->iDsAttackClashSkillIdx = 0;
        pObj->iDsThrowSkillIdx = 0;
    }
}

/*
 * Forgets the reaction step (script command 0x17) of pObj, so its attacks use no hit junctions.
 * pObj: the engine object.
 */
void vResetReactionSkillBlock(kgtEngineObject *pObj)
{
    pObj->pReactionStep = NULL;
}

/*
 * Clears all 20 guard box and 20 attack box slots of pObj (a new skill starts without boxes).
 * pObj: the engine object.
 */
void vMemzeroHitboxArrays(kgtEngineObject *pObj)
{
    vMemzero(pObj->pGuardBoxes, sizeof(pObj->pGuardBoxes));
    vMemzero(pObj->pAttackBoxes, sizeof(pObj->pAttackBoxes));
}

/*
 * Turns the current player object towards its nearest enemy: copies the character's iTargetFacing
 * (computed by vFindNearestEnemyPlayer) into the object and the character's iFacing.  Nothing
 * happens in guard-button mode (iOptionFlags & 8), where the player turns by the direction pressed.
 * Returns 1 if the facing changed (the caller then plays the turn skill), else 0.
 * Globals: reads gpkgtCurrentEngineObject; changes gkgtLoadedCharacter[].iFacing.
 */
int iAssignPlayerLookingRight(void)
{
    kgtEngineObject *pObj = gpkgtCurrentEngineObject;
    int iPlayer = pObj->iPlayerIdx;
    int iFacing;

    /* guard-button mode: no automatic turning */
    if (gkgtLoadedCharacter[iPlayer].iOptionFlags & 8)
        return 0;
    iFacing = gkgtLoadedCharacter[iPlayer].iTargetFacing;
    if (pObj->iPlayerLookingRight == iFacing)
        return 0;
    gkgtLoadedCharacter[iPlayer].iFacing = iFacing;
    pObj->iPlayerLookingRight = iFacing;
    return 1;
}

/*
 * Finds the nearest living enemy player on the same line as the current (player) object, stores it in
 * the character's pNearestEnemy and updates iTargetFacing to point at it.  When the target facing
 * changes, the left/right bits of this frame's input are swapped, because inputs are stored relative to
 * the facing (bit 0 back, bit 1 forward).
 * Globals: reads gpkgtCurrentEngineObject, gkgtEngineObjects, giInputBufferPos; changes
 * gkgtLoadedCharacter[].pNearestEnemy / .iTargetFacing and giInputBuffer.
 */
void vFindNearestEnemyPlayer(void)
{
    int iPlayer = gpkgtCurrentEngineObject->iPlayerIdx;
    int iBestDist = 0x19000000;  /* no enemy yet: 0x19000000 = 6400 pixels in 16.16 */
    int iBestX;
    int iDist;
    int iFacing;
    int i;
    kgtEngineObject *pOther;

    gkgtLoadedCharacter[iPlayer].pNearestEnemy = NULL;
    /* scan every script-driven player object that is alive, active, already
       visible and marked in our enemy bitmask */
    pOther = gkgtEngineObjects;
    for (i = 0; i < 1024; i++, pOther++) {
        if (pOther->iJumpIdx == READ_SCRIPT && pOther != gpkgtCurrentEngineObject
            && pOther->iObjectType == PLAYER_ENGINE_OBJECT
            && gpkgtCurrentEngineObject->iLine == pOther->iLine
            && gkgtLoadedCharacter[pOther->iPlayerIdx].iHealth
            && gkgtLoadedCharacter[pOther->iPlayerIdx].iOnlineState
            && gkgtLoadedCharacter[pOther->iPlayerIdx].bImageShown
            && (*(int *)&gkgtLoadedCharacter[iPlayer].cEnemyBitmask & (1 << pOther->iPlayerIdx))) {
            iDist = abs(gpkgtCurrentEngineObject->iPosX - pOther->iPosX);
            if (iDist < iBestDist) {
                iBestDist = iDist;
                iBestX = pOther->iPosX;
                gkgtLoadedCharacter[iPlayer].pNearestEnemy = pOther;
            }
        }
    }
    /* face it (not in guard-button mode): 1 = flipped, the enemy is at a smaller or equal x */
    if (!(gkgtLoadedCharacter[iPlayer].iOptionFlags & 8) && iBestDist < 0x19000000) {
        iFacing = gpkgtCurrentEngineObject->iPosX >= iBestX;
        if (iFacing != gkgtLoadedCharacter[iPlayer].iTargetFacing) {
            gkgtLoadedCharacter[iPlayer].iTargetFacing = iFacing;
            /* back/forward swap meaning with the facing */
            if (giInputBuffer[iPlayer][giInputBufferPos] & 3)
                giInputBuffer[iPlayer][giInputBufferPos] ^= 3;
        }
    }
}

/*
 * Adds iAdd (may be negative) to a character's special gauge, moving whole stocks in and out of
 * iSpecialGaugeTokens: the gauge runs from 0 to iSpecialMax per stock, and at most iStockMax stocks
 * are kept (then the gauge stays at 0).  Characters without a special gauge (iStockMax 0) are skipped.
 * iPlayerIdx: character (0-7); iAdd: amount.
 * Globals: changes gkgtLoadedCharacter[iPlayerIdx].iSpecialGauge / .iSpecialGaugeTokens.
 */
void vAddToSpecialGauge(int iPlayerIdx, int iAdd)
{
    if (gkgtLoadedCharacter[iPlayerIdx].iStockMax) {
        gkgtLoadedCharacter[iPlayerIdx].iSpecialGauge += iAdd;
        /* underflow: break stocks back into the gauge (or stop at 0) */
        while (gkgtLoadedCharacter[iPlayerIdx].iSpecialGauge < 0) {
            if (gkgtLoadedCharacter[iPlayerIdx].iSpecialGaugeTokens) {
                gkgtLoadedCharacter[iPlayerIdx].iSpecialGaugeTokens--;
                gkgtLoadedCharacter[iPlayerIdx].iSpecialGauge += gkgtLoadedCharacter[iPlayerIdx].iSpecialMax;
            } else {
                gkgtLoadedCharacter[iPlayerIdx].iSpecialGauge = 0;
            }
        }
        /* overflow: turn full gauges into stocks while there is room */
        while (gkgtLoadedCharacter[iPlayerIdx].iSpecialGauge >= gkgtLoadedCharacter[iPlayerIdx].iSpecialMax) {
            if (gkgtLoadedCharacter[iPlayerIdx].iSpecialGaugeTokens >= gkgtLoadedCharacter[iPlayerIdx].iStockMax) {
                gkgtLoadedCharacter[iPlayerIdx].iSpecialGauge = 0;
            } else {
                gkgtLoadedCharacter[iPlayerIdx].iSpecialGauge -= gkgtLoadedCharacter[iPlayerIdx].iSpecialMax;
                gkgtLoadedCharacter[iPlayerIdx].iSpecialGaugeTokens++;
            }
        }
        /* all stocks full: the gauge stays empty */
        if (gkgtLoadedCharacter[iPlayerIdx].iSpecialGaugeTokens >= gkgtLoadedCharacter[iPlayerIdx].iStockMax) {
            gkgtLoadedCharacter[iPlayerIdx].iSpecialGaugeTokens = gkgtLoadedCharacter[iPlayerIdx].iStockMax;
            gkgtLoadedCharacter[iPlayerIdx].iSpecialGauge = 0;
        }
    }
}

/*
 * Changes a character's life by iLifeAdd (negative = damage).  Damage taken at low life (at or below
 * cLifeRevThreshold percent) is scaled by cLifeRevCorrection percent (at least 1).  When life reaches
 * 0 the character is knocked out: the lose skill starts (through iHitJunctionIdx), and in story mode
 * the defeat gives win points and triggers the story entry's effects (heal/charge someone, remove the
 * defeated CPU, or bring in the next CPU).  Otherwise life is clamped to iLifeMax and the red damage
 * bar grows by the damage.
 * pChar: the character; iLifeAdd: life to add.
 * Globals: reads gkgtGameState.kgtGameMode, giCurrentStoryStep[giStoryModeSide]; changes
 * gkgtLoadedCharacter[] (life, win points, CPU settings) and may create a player object.
 */
void vAddToHealth(kgt_character_struct *pChar, int iLifeAdd)
{
    int iLifeMax = pChar->iLifeMax;
    int iStackPad1;  /* unused */
    int iPlayer = 0;
    kgtEngineObject *pObj;
    kgtStoryEntry *pEntry;
    kgtStoryEntryCpu *pCpu;
    int *pPlayerIdx;

    /* low-life damage reduction (percentages) */
    if (pChar->iHealth <= (BYTE)pChar->cLifeRevThreshold * iLifeMax / 100 && iLifeAdd < 0) {
        iLifeAdd = (BYTE)pChar->cLifeRevCorrection * iLifeAdd / -100;
        if (iLifeAdd == 0)
            iLifeAdd = 1;
        iLifeAdd = -iLifeAdd;
    }
    /* knocked out */
    if (pChar->iHealth + iLifeAdd <= 0) {
        pObj = pChar->pkgtoSelf;
        pChar->iHealth = 0;
        pObj->iHitJunctionIdx = (unsigned short)pChar->shSkillIdxLoss;  /* jump to the lose skill on the next script frame */
        pChar->iDamageBar = 0;
        pChar->iDamageBarDelay = 0;
        if (gkgtGameState.kgtGameMode == GAME_MODE_STORY) {
            /* the story entry being fought (story data lives in character 0) */
            pEntry = &gkgtLoadedCharacter[0].kgtStoryEntries[giCurrentStoryStep[giStoryModeSide]];
            if (pObj->iPlayerIdx == 0) {
                /* the story player lost: cCpuWins names who gets the points (0 = last attacker) */
                if (pEntry->cCpuWins != 0) {
                    if ((BYTE)pEntry->cCpuWins > 0 && (BYTE)pEntry->cCpuWins <= 8)
                        gkgtLoadedCharacter[(BYTE)pEntry->cCpuWins - 1].iWinPoints += (BYTE)pEntry->cCpuWinsNumber;
                } else if (pChar->pLastOpponent) {
                    gkgtLoadedCharacter[pChar->pLastOpponent->iPlayerIdx].iWinPoints += (BYTE)pEntry->cCpuWinsNumber;
                }
            } else {
                /* a story CPU lost: points go to cVictoryPointsTarget (0 = last attacker) */
                pCpu = &pEntry->kgtStoryEntryCPUs[pObj->iPlayerIdx - 1];
                if (pCpu->cVictoryPointsTarget != 0) {
                    if ((BYTE)pCpu->cVictoryPointsTarget > 0 && (BYTE)pCpu->cVictoryPointsTarget <= 8)
                        gkgtLoadedCharacter[(BYTE)pCpu->cVictoryPointsTarget - 1].iWinPoints += (BYTE)pCpu->cVictoryPointsAmount;
                } else if (pChar->pLastOpponent) {
                    gkgtLoadedCharacter[pChar->pLastOpponent->iPlayerIdx].iWinPoints += (BYTE)pCpu->cVictoryPointsAmount;
                }

                /* bits 7-8: defeat effect: 1 gives life/special to the player (character 0), 2 to the last attacker */
                switch ((pCpu->uBitmask >> 7) & 3) {
                case 1:
                    vAddToHealth(&gkgtLoadedCharacter[iPlayer], pCpu->cEffectLifeIncrease);
                    vAddToSpecialGauge(iPlayer, pCpu->cEffectSpecialIncrease);
                    break;
                case 2:
                    if (pChar->pLastOpponent) {
                        iPlayer = pChar->pLastOpponent->iPlayerIdx;
                        vAddToHealth(&gkgtLoadedCharacter[iPlayer], pCpu->cEffectLifeIncrease);
                        vAddToSpecialGauge(iPlayer, pCpu->cEffectSpecialIncrease);
                    }
                    break;
                }
                /* bits 5-6: what happens to the defeated CPU: 1 it leaves, 2 the next CPU appears in its place */
                switch ((pCpu->uBitmask >> 5) & 3) {
                case 1:
                    pObj->iJumpIdx = RESET_IDX;
                    return;
                case 2:
                    pPlayerIdx = &pObj->iPlayerIdx;      /* matching: reading through the address keeps the iPlayerIdx load after the RESET_IDX store */
                    pObj->iJumpIdx = RESET_IDX;
                    iPlayer = *pPlayerIdx - 1;
                    /* iPlayer = the defeated player - 1 = its CPU entry; spawn that entry's character anew */
                    pCpu = &gkgtLoadedCharacter[0].kgtStoryEntries[giCurrentStoryStep[giStoryModeSide]].kgtStoryEntryCPUs[iPlayer];
                    if (pCpu->cCharacterIdx) {
                        pChar = &gkgtLoadedCharacter[iPlayer + 1];
                        /* depth 0x50 (line 0), start x in 16.16, y 0x398 = the ground of line 0 */
                        kgtoNewEngineObject(READ_SCRIPT, 0x50, (unsigned short)pCpu->shStartPos << 16, 0x3980000)->iPlayerIdx = iPlayer + 1;
                        pChar->bCpuControlled = 1;
                        pChar->iCpuLevel = (BYTE)pCpu->cCpuLevel;
                        if (pChar->iCpuLevel < 0)
                            pChar->iCpuLevel = 0;
                        if (pChar->iCpuLevel > 100)
                            pChar->iCpuLevel = 100;
                        pChar->iCpuMode = 1;
                    }
                    return;
                }
            }
        }
        /* not in story mode, or no story effect ended the function */
        if (pChar->iLosses == 0)
            pChar->iLosses = 1;
        pObj->iDepth = 60;  /* draw layer of a knocked-out player */
    } else {
        /* still alive */
        pChar->iHealth += iLifeAdd;
        pChar->iDamageBar -= iLifeAdd;
        pChar->iDamageBarDelay = 20;  /* frames before the red damage bar starts to shrink */
        if (pChar->iDamageBar < 0)
            pChar->iDamageBar = 0;
        if (pChar->iHealth > iLifeMax)
            pChar->iHealth = iLifeMax;
    }
}

/*
 * Computes the centre of the overlap of two boxes, given by centre and half size in pixels, as a 16.16
 * position: ((min right + max left) / 2, (min bottom + max top) / 2).  Used to place hit sparks.
 * pCentre: receives x and y (16.16); iX, iY, iW, iH: first box; iOtherX .. iOtherH: second box.
 */
void vHitboxCalc(int *pCentre, int iX, int iY, int iW, int iH, int iOtherX, int iOtherY, int iOtherW, int iOtherH)
{
    /* right and bottom edge of the overlap (the smaller ones) */
    if (iX + iW > iOtherX + iOtherW)
        pCentre[0] = iOtherX + iOtherW;
    else
        pCentre[0] = iX + iW;
    if (iY + iH > iOtherY + iOtherH)
        pCentre[1] = iOtherY + iOtherH;
    else
        pCentre[1] = iY + iH;
    /* plus left and top edge (the larger ones) */
    if (iX - iW < iOtherX - iOtherW)
        pCentre[0] += iOtherX - iOtherW;
    else
        pCentre[0] += iX - iW;
    if (iY - iH < iOtherY - iOtherH)
        pCentre[1] += iOtherY - iOtherH;
    else
        pCentre[1] += iY - iH;
    pCentre[0] *= 0x8000;      /* halve and convert to 16.16: the centre of the overlap */
    pCentre[1] *= 0x8000;
}

/*
 * Attack against attack: every attack box of every script object is compared with the attack boxes
 * of the later objects that are on the same line, belong to another player that is an enemy, and are
 * not in a hit reaction.  When two attack boxes overlap:
 *   * both without power: both objects lose all their attack boxes;
 *   * DS branch 5 (iDsAttackClashSkillIdx) of either object is taken, and the boxes are cleared;
 *   * with clashes enabled (kgtSystem.cSystemBitmask & 2) and some power involved: a clash mark
 *     (system skill shSkillIdxOffsetHitMark) appears at the centre of the overlap, the players' cancel
 *     state becomes 2, both lose their attack boxes and both freeze for cStiffTimeOffset frames;
 *   * one side without power and FA flag 0x80 on either box: both script steps are switched off
 *     (their command code is overwritten with 0, so the step does nothing from now on) and DS branch 3
 *     (iDsGuardedSkillIdx, normally the guarded branch) of non-player objects is taken.
 * Player 1 touching an enemy makes it the target shown for 1000 frames (gkgtGameState.iTargetPlayer).
 * Globals: reads gkgtEngineObjects, gkgtKgtSystem, giCameraX/Y; changes engine objects, the box steps,
 * gkgtLoadedCharacter[].iCurrentActionCancellableFlag and gkgtGameState.iTargetPlayer/dwTargetPlayerTimer.
 */
void vAdjustHitboxes(void)
{
    kgtHitboxStep *pBox;
    int iX;
    kgtHitboxStep *pOtherBox;
    int iOtherH;
    int iH;
    int iW;
    int iY;
    kgtSkill **ppStackPad1;  /* unused */
    kgtSkill **ppStackPad2;  /* unused */
    int iOtherW;
    int iOtherObj;
    int iOtherBox;
    int iBox;
    int iObj;
    int iCentre[2];
    kgtEngineObject *pObj;
    kgtEngineObject *pOther;
    kgtEngineObject *pNew;
    int iOtherX;
    int iOtherY;
    int iSkill;  /* system skill index, later reused for the clash freeze time */
    kgt_character_struct *pChar;
    kgt_character_struct *pOtherChar;

    pObj = gkgtEngineObjects;
    for (iObj = 0; iObj < 1024; iObj++, pObj++) {
        if (pObj->iJumpIdx != READ_SCRIPT)
            continue;
        for (iBox = 20; iBox-- > 0; ) {
            pBox = (kgtHitboxStep *)pObj->pAttackBoxes[iBox];
            if (!pBox)
                continue;
            if (pObj->iPlayerLookingRight & 1)
                /* box centre in pixels (the object position is 16.16); x mirrored when flipped */
                iX = pObj->iPosX / 0x10000 - pBox->shX;
            else
                iX = pBox->shX + pObj->iPosX / 0x10000;
            iY = pObj->iPosY / 0x10000 + pBox->shY;
            iW = pBox->shW;
            iH = pBox->shH;
            /* each pair once: only the objects after this one */
            for (iOtherObj = iObj + 1, pOther = &gkgtEngineObjects[iObj + 1]; iOtherObj < 1024; iOtherObj++, pOther++) {
                if (pOther->iJumpIdx != READ_SCRIPT)
                    continue;
                if ((pObj->iLine ^ pOther->iLine) & 1)
                    continue;
                if (pObj->iPlayerIdx == pOther->iPlayerIdx)
                    continue;
                if ((int)pOther->pWork015E & 8)  /* OBJ_FLAGS: in a hit reaction */
                    continue;
                if (!(*(int *)&gkgtLoadedCharacter[pObj->iPlayerIdx].cEnemyBitmask & (1 << pOther->iPlayerIdx)))
                    continue;
                for (iOtherBox = 20; iOtherBox-- > 0; ) {
                    pOtherBox = (kgtHitboxStep *)pOther->pAttackBoxes[iOtherBox];
                    if (!pOtherBox)
                        continue;
                    if (pOther->iPlayerLookingRight & 1)
                        iOtherX = pOther->iPosX / 0x10000 - pOtherBox->shX;
                    else
                        iOtherX = pOtherBox->shX + pOther->iPosX / 0x10000;
                    iOtherY = pOtherBox->shY + pOther->iPosY / 0x10000;
                    iOtherH = pOtherBox->shH;
                    iOtherW = pOtherBox->shW;
                    /* no overlap (half sizes: left = x - w, right = x + w) */
                    if (iW + iX <= iOtherX - iOtherW || iX - iW >= iOtherW + iOtherX
                        || iH + iY <= iOtherY - iOtherH || iY - iH >= iOtherH + iOtherY)
                        continue;
                    /* player 1 is fighting it: show it as the target for 1000 frames */
                    if (pObj->iPlayerIdx == 0) {
                        gkgtGameState.iTargetPlayer = pOther->iPlayerIdx;
                        gkgtGameState.dwTargetPlayerTimer = 1000;
                    }
                    /* two powerless boxes just cancel each other */
                    if (pBox->cPower == 0 && pOtherBox->cPower == 0) {
                        vMemzero(pObj->pAttackBoxes, sizeof(pObj->pAttackBoxes));
                        vMemzero(pOther->pAttackBoxes, sizeof(pOther->pAttackBoxes));
                        continue;
                    }
                    /* DS branch 5 (attack clash) of either side */
                    if (pObj->iDsAttackClashSkillIdx) {
                        pObj->iHitJunctionIdx = pObj->iDsAttackClashSkillIdx;
                        pObj->iDsAttackClashSkillIdx = 0;
                        vMemzero(pObj->pAttackBoxes, sizeof(pObj->pAttackBoxes));
                        vMemzero(pOther->pAttackBoxes, sizeof(pOther->pAttackBoxes));
                    }
                    if (pOther->iDsAttackClashSkillIdx) {
                        pOther->iHitJunctionIdx = pOther->iDsAttackClashSkillIdx;
                        pOther->iDsAttackClashSkillIdx = 0;
                        vMemzero(pObj->pAttackBoxes, sizeof(pObj->pAttackBoxes));
                        vMemzero(pOther->pAttackBoxes, sizeof(pOther->pAttackBoxes));
                    }
                    /* clashes enabled in the system file */
                    if (gkgtKgtSystem.cSystemBitmask & 2) {
                        if (pBox->cPower || pOtherBox->cPower) {
                            vHitboxCalc(iCentre, iX, iY, iW, iH, iOtherX, iOtherY, iOtherW, iOtherH);
                            /* clash mark at depth 0x5d, in screen coordinates (no 0x40000000 flag) */
                            pNew = kgtoNewEngineObject(READ_SCRIPT, 0x5d, iCentre[0] - giCameraX * 0x10000, iCentre[1] - giCameraY * 0x10000);
                            iSkill = (unsigned short)gkgtKgtSystem.shSkillIdxOffsetHitMark;
                            pNew->iSkillIdx = iSkill;
                            pNew->iObjectType = SYSTEM_ENGINE_OBJECT;
                            pNew->iSkillScriptIdx = (unsigned short)gkgtKgtSystem.kgtCore.pSkillsAlloc[iSkill].shStartingStepIdx;
                        }
                        pChar = &gkgtLoadedCharacter[pObj->iPlayerIdx];
                        pOtherChar = &gkgtLoadedCharacter[pOther->iPlayerIdx];
                        /* a clash makes cancellable actions cancellable at once (state 2) */
                        if (pChar->iCurrentActionCancellableFlag)
                            pChar->iCurrentActionCancellableFlag = 2;
                        if (pOtherChar->iCurrentActionCancellableFlag)
                            pOtherChar->iCurrentActionCancellableFlag = 2;
                        vMemzero(pObj->pAttackBoxes, sizeof(pObj->pAttackBoxes));
                        vMemzero(pOther->pAttackBoxes, sizeof(pOther->pAttackBoxes));
                        /* both freeze for the clash hit stop */
                        iSkill = (BYTE)gkgtKgtSystem.cStiffTimeOffset;
                        pOther->iOpponentDowntimeInFrames = iSkill;
                        pObj->iOpponentDowntimeInFrames = iSkill;
                    }
                    /* a powerless box meets an FA flag-0x80 box: switch both steps off for good */
                    if ((pBox->cPower == 0 || pOtherBox->cPower == 0) && ((pBox->cFlags2 & 0x80) || (pOtherBox->cFlags2 & 0x80))) {
                        pOtherBox->cType = pBox->cType = 0;
                        if (pObj->iObjectType != PLAYER_ENGINE_OBJECT && pObj->iDsGuardedSkillIdx) {
                            pObj->iHitJunctionIdx = pObj->iDsGuardedSkillIdx;
                            pObj->iDsGuardedSkillIdx = 0;
                        }
                        if (pOther->iObjectType != PLAYER_ENGINE_OBJECT && pOther->iDsGuardedSkillIdx) {
                            pOther->iHitJunctionIdx = pOther->iDsGuardedSkillIdx;
                            pOther->iDsGuardedSkillIdx = 0;
                        }
                    }
                }
            }
        }
    }
}

/*
 * Attack against body: every attack box (FA) of every script object is compared with the hurt and
 * throw boxes (FD flags 2 / 4) of all other objects.  For the first hit of an attack (OBJ_FLAGS 0x10
 * then blocks further hits until the script allows them again) it decides whether the target guards,
 * picks the target's hit junction from the attacker's reaction step (script command 0x17), spawns the
 * hit spark, applies hit stop, damage (shaving through guard, combo and life corrections), special
 * gauge and the combo counter, and takes the DS branches for attack hits (2) and guarded attacks (3).
 * Globals: reads gkgtEngineObjects, gkgtKgtSystem, giInputBuffer, giInputBufferPos, giObjectLayers;
 * changes engine objects, gkgtLoadedCharacter[] (last opponent/attacker, hit flags, times hit, combo
 * count, life and gauge through vAddToHealth/vAddToSpecialGauge) and gkgtGameState.iTargetPlayer.
 * Built from the original's machine code: VC6 does not yet compile the C to exactly these bytes.
 * The dead `if (0)` copy of the C body keeps the file's string literals and imported symbols in
 * the original order; the plain C version is on the `nonmatching` branch.
 */
/* the original machine code; the C in the dead block keeps the literals and imports */
__declspec(naked) void vHandleHitboxEffects(void)
{
    if (0) {
        kgtHitboxStep * pGuard;
        int iGuardBox;
        int bGuard;
        int iOtherY;
        kgt_character_struct * pOtherChar;
        int iY;
        int iOtherX;
        int iH;
        int iW;
        int iPlayer;
        int iOtherW;
        kgtSkill * * ppGuard;
        int iOtherH;
        int iX;
        kgtHitboxStep * pBox;
        int iOtherObj;
        int iObj;
        int iPower;
        int iCentre[2];
        kgtEngineObject * pObj;
        kgtEngineObject * pOther;
        kgtEngineObject * pNew;
        int iInput;
        kgtReactionStep * pReaction;
        kgt_character_struct * pChar;
        int iBox;
        int iStackPad1;  /* unused */
        int iDir;
        int iJunction;  /* hit junction index; also reused for the hit stop time */
        int iDamage;
        int * pComboCount;
        BYTE cFlags;
        register kgtCharacterHitJunction * pHitJunction;  /* attacker's hit junction (hit spark) */
        iW = 0;  /* matching: dead store (overwritten before any use) */
        if (0) vMemzero(& iH, 0);  /* matching: dead address-taking keeps iH in memory, as in the original */
        for (iObj = 0, pObj = gkgtEngineObjects; iObj < 1024; pObj++, iObj++) {
            /* attackers: script objects not switching lines whose attack has not hit yet */
            if ((pObj->iLine & 2) || pObj->iJumpIdx != READ_SCRIPT ||(OBJ_FLAGS(pObj) & 0x10)) continue;
            for (iBox = 0; iBox < 20; iBox++) {
                pBox =(kgtHitboxStep *) pObj->pAttackBoxes[iBox];
                if (!pBox) continue;
                /* attack box centre and half size in pixels (x mirrored when flipped) */
                if (pObj->iPlayerLookingRight & 1) {
                    iX = pObj->iPosX / 0x10000 - pBox->shX;
                } else {
                    iX = pBox->shX + pObj->iPosX / 0x10000;
                }
                iY = pBox->shY + pObj->iPosY / 0x10000;
                iW = pBox->shW;
                iH = pBox->shH;
                for (iOtherObj = 0, pOther = gkgtEngineObjects; iOtherObj < 1024; iOtherObj++, pOther++) {
                    /* targets: script objects on the same line, of another owner, marked as enemies, not switching lines */
                    if (pOther->iJumpIdx != READ_SCRIPT) goto no_hit;
                    if ((pOther->iLine ^ pObj->iLine) & 1) goto no_hit;
                    if (pObj->iOwnerIdx == pOther->iOwnerIdx) goto no_hit;
                    iPlayer = pObj->iPlayerIdx;
                    if (!(*(int *) & gkgtLoadedCharacter[iPlayer].cEnemyBitmask &(1 << pOther->iPlayerIdx))) goto no_hit;
                    if (pOther->iLine & 2) goto no_hit;
                    /* players: FA flags 0x10 / 0x20 miss grounded / airborne targets, 8 misses a guarding target (0xc),
                       0x80 one in a hit reaction (8) */
                    if (pOther->iObjectType == PLAYER_ENGINE_OBJECT) {
                        if (pOther->iPosY == pOther->iGroundY && pOther->iYMomentum == 0) {
                            cFlags = pBox->cFlags;
                            if (cFlags & 0x10) goto no_hit;
                        } else {
                            cFlags = pBox->cFlags;
                            if (cFlags & 0x20) goto no_hit;
                        }
                        /* matching: empty block boundary */
                        if (0) goto boundary1;
                        boundary1 :
                        if ((cFlags & 8) &&(OBJ_FLAGS(pOther) & 0xc) == 0xc) goto no_hit;
                        if ((cFlags & 0x80) &&(OBJ_FLAGS(pOther) & 0xc) == 8) goto no_hit;
                    }
                    /* the first overlapping hurt or throw box (FD flags 2 / 4) is hit */
                    ppGuard = pOther->pGuardBoxes;
                    for (iGuardBox = 0; iGuardBox < 20; iGuardBox++, ppGuard++) {
                        pGuard =(kgtHitboxStep *) * ppGuard;
                        if (!pGuard || !(pGuard->cFlags & 6)) continue;
                        if (!(pOther->iPlayerLookingRight & 1)) iOtherX = pGuard->shX + pOther->iPosX / 0x10000; else iOtherX = pOther->iPosX / 0x10000 - pGuard->shX;
                        iOtherY = pGuard->shY + pOther->iPosY / 0x10000;
                        iOtherH = pGuard->shH;
                        iOtherW = pGuard->shW;
                        if (iW + iX <= iOtherX - iOtherW || iX - iW >= iOtherW + iOtherX || iH + iY <= iOtherY - iOtherH || iY - iH >= iOtherH + iOtherY) continue;
                        goto hit;
                    }
                    no_hit :
                    continue;
                    hit :
                    {
                        /* a hit: gather the attack power and the target's input (or the one stored by PS) */
                        bGuard = 0;
                        iPower =(BYTE) pBox->cPower;
                        iInput = giInputBuffer[pOther->iPlayerIdx][giInputBufferPos];
                        pOtherChar = & gkgtLoadedCharacter[pOther->iPlayerIdx];
                        /* player against anything: remember each other as last opponent */
                        if (pObj->iObjectType == PLAYER_ENGINE_OBJECT) {
                            gkgtLoadedCharacter[iPlayer].pLastOpponent = pOther;
                            pOtherChar->pLastOpponent = pObj;
                        }
                        /* player 1 hit it: show it as the target for 1000 frames */
                        if (pObj->iPlayerIdx == 0) {
                            gkgtGameState.iTargetPlayer = pOther->iPlayerIdx;
                            gkgtGameState.dwTargetPlayerTimer = 1000;
                        }
                        if (gkgtLoadedCharacter[pOther->iPlayerIdx].bUseStoredInput) iInput = *(int *) & gkgtLoadedCharacter[pOther->iPlayerIdx].cStoredInput;
                        OBJ_FLAGS(pObj) |= 0x10;  /* this attack has hit */
                        /* attacker drawn in front of the target */
                        if (pObj->iObjectType == PLAYER_ENGINE_OBJECT) pObj->iDepth = giObjectLayers[pObj->iLine & 1] + 1;
                        if (pOther->iObjectType == PLAYER_ENGINE_OBJECT) pOther->iDepth = giObjectLayers[pObj->iLine & 1] - 1;
                        switch (pOther->iObjectType) {
                            case PLAYER_ENGINE_OBJECT :
                            if (gkgtLoadedCharacter[iPlayer].iCurrentActionCancellableFlag) gkgtLoadedCharacter[iPlayer].iCurrentActionCancellableFlag = 2;
                            /* the input bit that means away from the attacker: 1 (back) outside guard-button mode, the
                               only case in which it is used */
                            if (!(pOtherChar->iTargetFacing &&(pOtherChar->iOptionFlags & 8))) iDir = 1; else iDir =(pObj->iPosX <= pOther->iPosX) + 1;
                            /* a free target guards: CPUs with iCpuLevel percent chance; guard-button mode with the guard
                               button, or auto guard (option 1) with no input; otherwise holding back, or auto guard
                               with nothing but down held */
                            if (!(OBJ_FLAGS(pOther) & 0xc)) {
                                if (pOtherChar->bCpuControlled) {
                                    if (rand() % 100 < pOtherChar->iCpuLevel) bGuard = 1;
                                } else if (pOtherChar->iOptionFlags & 8) {
                                    if ((giInputBuffer[pOther->iPlayerIdx][giInputBufferPos] &(1 <<(pOtherChar->cGuardButton + 4))) ||((pOtherChar->iOptionFlags & 1) && iInput == 0)) bGuard = 1;
                                } else {
                                    if ((iInput & iDir) ||((pOtherChar->iOptionFlags & 1) && !(iInput & ~8))) bGuard = 1;
                                }
                            }
                            /* already guarding: keeps guarding */
                            if ((OBJ_FLAGS(pOther) & 0xc) == 0xc) bGuard = 1;
                            if (pBox->cFlags & 0x40) bGuard = 0;  /* unblockable attack */
                            if ((!(pOtherChar->iOptionFlags & 2)) &&(pOther->iPosY < pOther->iGroundY)) bGuard = 0;  /* no guarding in the air without option 2 */
                            /* choose the hit junction from the attacker's reaction step by the target's situation */
                            if (pObj->pReactionStep) {
                                pReaction =(kgtReactionStep *) pObj->pReactionStep;
                                /* target in the air? (matching: both arms give the same value; spelling kept from the matching
                                   attempts) */
                                if ((pObj->iPosY < pObj->iGroundY ? pOther->iPosY : pOther->iPosY) < pOther->iGroundY) {
                                    if (!(!(bGuard))) iJunction = pReaction->wAirGuard; else iJunction = pReaction->wAir;  /* matching: value-neutral double negation from the matching attempts */
                                } else if (iInput & 8) {
                                    if (bGuard != 0) {
                                        iJunction = pReaction->wCrouchGuard;
                                        /* CPUs never take a junction flagged cDoing & 1; they switch to the other guard */
                                        if (((pOtherChar->bCpuControlled)) &&(((gkgtKgtSystem.kgtHitJunctions[iJunction].cDoing & 1)))) iJunction = pReaction->wStandGuard;
                                    } else {
                                        iJunction = pReaction->wCrouch;
                                    }
                                } else {
                                    if (bGuard != 0) {
                                        iJunction = pReaction->wStandGuard;
                                        if (pOtherChar->bCpuControlled &&(gkgtKgtSystem.kgtHitJunctions[iJunction].cDoing & 1)) iJunction = pReaction->wCrouchGuard;
                                    } else {
                                        iJunction = pReaction->wStand;
                                    }
                                }
                                if (iJunction) {
                                    /* the target jumps to its own skill for this junction; junctions whose system entry has
                                       cDoing bit 0 set count as not guarded */
                                    pOther->iHitJunctionIdx =(WORD) pOtherChar->kgtHitJunctions[iJunction].shAllotmentIdx;
                                    if (gkgtKgtSystem.kgtHitJunctions[iJunction].cDoing & 1) bGuard = 0;
                                    if (pBox->cPower != 0) {
                                        /* hit spark of the attacker's junction: an object at depth 0x5d at the centre of the overlap,
                                           in stage coordinates (0x40000000) */
                                        pHitJunction = & gkgtLoadedCharacter[iPlayer].kgtHitJunctions[iJunction];
                                        if (pHitJunction->shSparkIdx) {
                                            pChar = & gkgtLoadedCharacter[pObj->iPlayerIdx];
                                            vHitboxCalc(iCentre, iX, iY, iW, iH, iOtherX, iOtherY, pGuard->shW, pGuard->shH);
                                            pNew = kgtoNewEngineObject(READ_SCRIPT, 0x5d, iCentre[0], iCentre[1]);
                                            pNew->iPlayerIdx = pObj->iPlayerIdx;
                                            pNew->iPlayerLookingRight = pObj->iPlayerLookingRight;
                                            pNew->iLine = pObj->iLine;
                                            pNew->iSkillIdx =(WORD) pHitJunction->shSparkIdx;
                                            pNew->iObjectType = STORY_ENGINE_OBJECT;
                                            pNew->iSkillScriptIdx =(WORD) pChar->kgtCore.pSkillsAlloc[(WORD) pHitJunction->shSparkIdx].shStartingStepIdx;
                                            pNew->iFlags |= 0x40000000;
                                        }
                                    }
                                    /* a junction without a skill: report it and skip the hit */
                                    if (!pOther->iHitJunctionIdx) if (iPower) {
                                        iSetDebugInfo("reaction error 1", 0xff);
                                        goto next_object;
                                    }
                                    goto react_done;
                                }
                            }
                            /* no reaction step or junction for this situation */
                            if (iPower) {
                                iSetDebugInfo("reaction error 2", 0xff);
                                goto next_object;
                            }
                            react_done :
                            /* guarded: face the attacker; hit stop, guard state (0xc) and shaving (FA flag 4,
                               cShaveRatio percent of the power, at least 1); then the attacker's DS branch 3 (guarded) */
                            if (bGuard) {
                                pOther->iPlayerLookingRight = pObj->iPlayerLookingRight ^ 1;
                                if (iPower) {
                                    iJunction =(BYTE) gkgtKgtSystem.cStiffTimeGuard;
                                    pOther->iOpponentDowntimeInFrames = iJunction;
                                    pObj->iOpponentDowntimeInFrames = iJunction;
                                    OBJ_FLAGS(pOther) |= 0xc;
                                    if (pBox->cFlags & 4) {
                                        iDamage =(BYTE) gkgtLoadedCharacter[iPlayer].cShaveRatio * iPower / 100;
                                        if (iDamage == 0) iDamage = 1;
                                        vAddToHealth(pOtherChar, - iDamage);
                                    }
                                }
                                if (pObj->iDsGuardedSkillIdx) {
                                    pObj->iHitJunctionIdx = pObj->iDsGuardedSkillIdx;
                                    pObj->iDsGuardedSkillIdx = 0;
                                }
                            } else {
                                /* hit a throw box: the attacker's hit counts as a throw (iHitFlags 1 | 2) */
                                if (pGuard->cFlags & 4) {
                                    gkgtLoadedCharacter[iPlayer].iHitFlags |= 3;
                                    pOtherChar->iTimesHit++;
                                    iSetDebugInfo("\223\212\202\260\202\342\202\347\202\352\202\311\215U\214\202", 0x8fffff);  /* 投げやられに攻撃 (attack on a throw hurt box) */
                                }
                                pOther->iPlayerLookingRight = pObj->iPlayerLookingRight ^ 1;
                                if (iPower) {
                                    /* hit stop, special gauge for both sides, target in a hit reaction (8) */
                                    iJunction =(BYTE) gkgtKgtSystem.cStiffTimeHit;
                                    pOther->iOpponentDowntimeInFrames = iJunction;
                                    pObj->iOpponentDowntimeInFrames = iJunction;
                                    vAddToSpecialGauge(pObj->iPlayerIdx, gkgtLoadedCharacter[iPlayer].shSpecialGaugeIncreaseOnAttack);
                                    vAddToSpecialGauge(pOther->iPlayerIdx, pOtherChar->shSpecialGaugeIncreaseOnHit);
                                    OBJ_FLAGS(pOther) =(OBJ_FLAGS(pOther) & ~4) | 8;
                                    /* combo correction: cCharacterRev percent less per hit already in the combo (at least 1),
                                       then the hurt box's damage percent (FD cFlags2, at least 1) */
                                    iPower = iPower +((BYTE) gkgtLoadedCharacter[iPlayer].cCharacterRev * pOtherChar->iComboCount * iPower / - 100);
                                    if (iPower < 1) iPower = 1;
                                    iDamage =(BYTE) pGuard->cFlags2 * iPower / 100;
                                    if (iDamage < 1) iDamage = 1;
                                    vAddToHealth(pOtherChar, - iDamage);
                                    pOtherChar->iComboCount++;
                                }
                                /* combo counter object (from the second hit) that reads the count through pWork015E */
                                pComboCount = & pOtherChar->iComboCount;
                                if (* pComboCount > 1) {
                                    pNew = kgtoNewEngineObject(HIT_COMBO_COUNTER, 0x5e,((iOtherX << 16)), iOtherY << 16);
                                    pNew->iPlayerIdx = * pComboCount;
                                    pNew->pWork015E =(kgtEngineObject *) pComboCount;
                                    pNew->iFlags |= 0x40000000;
                                    if (0) iSetDebugInfo("%d", * pComboCount);  /* matching: dead call that keeps the %d literal in .data */
                                }
                                if (pObj->iObjectType == PLAYER_ENGINE_OBJECT) {
                                    gkgtLoadedCharacter[iPlayer].iHitFlags |= 1;  /* a player's attack connected */
                                    pOtherChar->iTimesHit++;
                                } else if (pObj->iDsAttackHitsSkillIdx) {
                                    pObj->iHitJunctionIdx = pObj->iDsAttackHitsSkillIdx;
                                    pObj->iDsAttackHitsSkillIdx = 0;
                                }
                            }
                            /* the target remembers a player attacker, or an object that follows its parent, for the
                               screen-edge push-back (vHandleHitMovements) */
                            if (pObj->iObjectType == PLAYER_ENGINE_OBJECT) {
                                pOtherChar->iLastAttacker =(int) pObj;
                            } else {
                                if (pObj->iFlags & 0x20000000) pOtherChar->iLastAttacker =(int) pObj;
                            }
                            break;
                            case STORY_ENGINE_OBJECT :
                            /* a character's object (projectile) was hit: the attacker's DS branch 2 */
                            if (pObj->iDsAttackHitsSkillIdx) {
                                pObj->iHitJunctionIdx = pObj->iDsAttackHitsSkillIdx;
                                pObj->iDsAttackHitsSkillIdx = 0;
                            }
                            break;
                        }
                        goto next_object;
                    }
                    next_object :
                    ;
                }
            }
        }
    }
    __asm {
        _emit 0x83        ; 0040F010  sub esp, 0x48
        _emit 0xEC
        _emit 0x48
        _emit 0x53        ; 0040F013  push ebx
        _emit 0x55        ; 0040F014  push ebp
        _emit 0x56        ; 0040F015  push esi
        _emit 0x57        ; 0040F016  push edi
        _emit 0xC7        ; 0040F017  mov dword ptr [esp + 0x48], 0
        _emit 0x44
        _emit 0x24
        _emit 0x48
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0xBF        ; 0040F01F  mov edi, 0x4701e0
        _emit 0xE0
        _emit 0x01
        _emit 0x47
        _emit 0x00
        _emit 0xF6        ; 0040F024  test byte ptr [edi + 0x14], 2
        _emit 0x47
        _emit 0x14
        _emit 0x02
        _emit 0x0F        ; 0040F028  jne 0x40f8eb
        _emit 0x85
        _emit 0xBD
        _emit 0x08
        _emit 0x00
        _emit 0x00
        _emit 0x83        ; 0040F02E  cmp dword ptr [edi], 4
        _emit 0x3F
        _emit 0x04
        _emit 0x0F        ; 0040F031  jne 0x40f8eb
        _emit 0x85
        _emit 0xB4
        _emit 0x08
        _emit 0x00
        _emit 0x00
        _emit 0xF6        ; 0040F037  test byte ptr [edi + 0x15e], 0x10
        _emit 0x87
        _emit 0x5E
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x10
        _emit 0x0F        ; 0040F03E  jne 0x40f8eb
        _emit 0x85
        _emit 0xA7
        _emit 0x08
        _emit 0x00
        _emit 0x00
        _emit 0xC7        ; 0040F044  mov dword ptr [esp + 0x3c], 0
        _emit 0x44
        _emit 0x24
        _emit 0x3C
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040F04C  mov eax, dword ptr [esp + 0x3c]
        _emit 0x44
        _emit 0x24
        _emit 0x3C
        _emit 0x8B        ; 0040F050  mov eax, dword ptr [edi + eax*4 + 0x89]
        _emit 0x84
        _emit 0x87
        _emit 0x89
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x85        ; 0040F057  test eax, eax
        _emit 0xC0
        _emit 0x89        ; 0040F059  mov dword ptr [esp + 0x14], eax
        _emit 0x44
        _emit 0x24
        _emit 0x14
        _emit 0x0F        ; 0040F05D  je 0x40f8d9
        _emit 0x84
        _emit 0x76
        _emit 0x08
        _emit 0x00
        _emit 0x00
        _emit 0x8A        ; 0040F063  mov al, byte ptr [edi + 0x5c]
        _emit 0x47
        _emit 0x5C
        _emit 0x8B        ; 0040F066  mov esi, dword ptr [esp + 0x14]
        _emit 0x74
        _emit 0x24
        _emit 0x14
        _emit 0xA8        ; 0040F06A  test al, 1
        _emit 0x01
        _emit 0x8B        ; 0040F06C  mov eax, dword ptr [edi + 8]
        _emit 0x47
        _emit 0x08
        _emit 0x0F        ; 0040F06F  movsx ecx, word ptr [esi + 1]
        _emit 0xBF
        _emit 0x4E
        _emit 0x01
        _emit 0x99        ; 0040F073  cdq
        _emit 0x74        ; 0040F074  je 0x40f089
        _emit 0x13
        _emit 0x81        ; 0040F076  and edx, 0xffff
        _emit 0xE2
        _emit 0xFF
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x03        ; 0040F07C  add eax, edx
        _emit 0xC2
        _emit 0xC1        ; 0040F07E  sar eax, 0x10
        _emit 0xF8
        _emit 0x10
        _emit 0x2B        ; 0040F081  sub eax, ecx
        _emit 0xC1
        _emit 0x89        ; 0040F083  mov dword ptr [esp + 0x24], eax
        _emit 0x44
        _emit 0x24
        _emit 0x24
        _emit 0xEB        ; 0040F087  jmp 0x40f09a
        _emit 0x11
        _emit 0x81        ; 0040F089  and edx, 0xffff
        _emit 0xE2
        _emit 0xFF
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x03        ; 0040F08F  add eax, edx
        _emit 0xC2
        _emit 0xC1        ; 0040F091  sar eax, 0x10
        _emit 0xF8
        _emit 0x10
        _emit 0x03        ; 0040F094  add ecx, eax
        _emit 0xC8
        _emit 0x89        ; 0040F096  mov dword ptr [esp + 0x24], ecx
        _emit 0x4C
        _emit 0x24
        _emit 0x24
        _emit 0x8B        ; 0040F09A  mov eax, dword ptr [edi + 0xc]
        _emit 0x47
        _emit 0x0C
        _emit 0xC7        ; 0040F09D  mov dword ptr [esp + 0x44], 0
        _emit 0x44
        _emit 0x24
        _emit 0x44
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x0F        ; 0040F0A5  movsx ecx, word ptr [esi + 3]
        _emit 0xBF
        _emit 0x4E
        _emit 0x03
        _emit 0x99        ; 0040F0A9  cdq
        _emit 0x81        ; 0040F0AA  and edx, 0xffff
        _emit 0xE2
        _emit 0xFF
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0xBD        ; 0040F0B0  mov ebp, 0x4701e0
        _emit 0xE0
        _emit 0x01
        _emit 0x47
        _emit 0x00
        _emit 0x03        ; 0040F0B5  add eax, edx
        _emit 0xC2
        _emit 0x0F        ; 0040F0B7  movsx edx, word ptr [esi + 5]
        _emit 0xBF
        _emit 0x56
        _emit 0x05
        _emit 0xC1        ; 0040F0BB  sar eax, 0x10
        _emit 0xF8
        _emit 0x10
        _emit 0x03        ; 0040F0BE  add ecx, eax
        _emit 0xC8
        _emit 0x89        ; 0040F0C0  mov dword ptr [esp + 0x30], edx
        _emit 0x54
        _emit 0x24
        _emit 0x30
        _emit 0x0F        ; 0040F0C4  movsx eax, word ptr [esi + 7]
        _emit 0xBF
        _emit 0x46
        _emit 0x07
        _emit 0x89        ; 0040F0C8  mov dword ptr [esp + 0x40], ecx
        _emit 0x4C
        _emit 0x24
        _emit 0x40
        _emit 0x89        ; 0040F0CC  mov dword ptr [esp + 0x2c], eax
        _emit 0x44
        _emit 0x24
        _emit 0x2C
        _emit 0x83        ; 0040F0D0  cmp dword ptr [ebp], 4
        _emit 0x7D
        _emit 0x00
        _emit 0x04
        _emit 0x0F        ; 0040F0D4  jne 0x40f8bf
        _emit 0x85
        _emit 0xE5
        _emit 0x07
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040F0DA  mov edx, dword ptr [ebp + 0x14]
        _emit 0x55
        _emit 0x14
        _emit 0x8B        ; 0040F0DD  mov esi, dword ptr [edi + 0x14]
        _emit 0x77
        _emit 0x14
        _emit 0x8B        ; 0040F0E0  mov ecx, edx
        _emit 0xCA
        _emit 0x33        ; 0040F0E2  xor ecx, esi
        _emit 0xCE
        _emit 0xF6        ; 0040F0E4  test cl, 1
        _emit 0xC1
        _emit 0x01
        _emit 0x0F        ; 0040F0E7  jne 0x40f8bf
        _emit 0x85
        _emit 0xD2
        _emit 0x07
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040F0ED  mov eax, dword ptr [edi + 0x60]
        _emit 0x47
        _emit 0x60
        _emit 0x8B        ; 0040F0F0  mov ecx, dword ptr [ebp + 0x60]
        _emit 0x4D
        _emit 0x60
        _emit 0x3B        ; 0040F0F3  cmp eax, ecx
        _emit 0xC1
        _emit 0x0F        ; 0040F0F5  je 0x40f8bf
        _emit 0x84
        _emit 0xC4
        _emit 0x07
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040F0FB  mov eax, dword ptr [edi + 0x156]
        _emit 0x87
        _emit 0x56
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040F101  mov ecx, dword ptr [ebp + 0x156]
        _emit 0x8D
        _emit 0x56
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x69        ; 0040F107  imul eax, eax, 0xe03f
        _emit 0xC0
        _emit 0x3F
        _emit 0xE0
        _emit 0x00
        _emit 0x00
        _emit 0xBE        ; 0040F10D  mov esi, 1
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x89        ; 0040F112  mov dword ptr [esp + 0x18], eax
        _emit 0x44
        _emit 0x24
        _emit 0x18
        _emit 0xD3        ; 0040F116  shl esi, cl
        _emit 0xE6
        _emit 0x85        ; 0040F118  test dword ptr [eax + 0x4dfd37], esi
        _emit 0xB0
        _emit 0x37
        _emit 0xFD
        _emit 0x4D
        _emit 0x00
        _emit 0x0F        ; 0040F11E  je 0x40f8bf
        _emit 0x84
        _emit 0x9B
        _emit 0x07
        _emit 0x00
        _emit 0x00
        _emit 0xF6        ; 0040F124  test dl, 2
        _emit 0xC2
        _emit 0x02
        _emit 0x0F        ; 0040F127  jne 0x40f8bf
        _emit 0x85
        _emit 0x92
        _emit 0x07
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040F12D  mov eax, dword ptr [ebp + 0x15a]
        _emit 0x85
        _emit 0x5A
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x85        ; 0040F133  test eax, eax
        _emit 0xC0
        _emit 0x75        ; 0040F135  jne 0x40f194
        _emit 0x5D
        _emit 0x8B        ; 0040F137  mov ecx, dword ptr [ebp + 0xc]
        _emit 0x4D
        _emit 0x0C
        _emit 0x8B        ; 0040F13A  mov eax, dword ptr [ebp + 0x58]
        _emit 0x45
        _emit 0x58
        _emit 0x3B        ; 0040F13D  cmp ecx, eax
        _emit 0xC8
        _emit 0x75        ; 0040F13F  jne 0x40f159
        _emit 0x18
        _emit 0x8B        ; 0040F141  mov eax, dword ptr [ebp + 0x1c]
        _emit 0x45
        _emit 0x1C
        _emit 0x85        ; 0040F144  test eax, eax
        _emit 0xC0
        _emit 0x75        ; 0040F146  jne 0x40f159
        _emit 0x11
        _emit 0x8B        ; 0040F148  mov edx, dword ptr [esp + 0x14]
        _emit 0x54
        _emit 0x24
        _emit 0x14
        _emit 0x8A        ; 0040F14C  mov al, byte ptr [edx + 0xa]
        _emit 0x42
        _emit 0x0A
        _emit 0xA8        ; 0040F14F  test al, 0x10
        _emit 0x10
        _emit 0x0F        ; 0040F151  jne 0x40f8bf
        _emit 0x85
        _emit 0x68
        _emit 0x07
        _emit 0x00
        _emit 0x00
        _emit 0xEB        ; 0040F157  jmp 0x40f168
        _emit 0x0F
        _emit 0x8B        ; 0040F159  mov eax, dword ptr [esp + 0x14]
        _emit 0x44
        _emit 0x24
        _emit 0x14
        _emit 0x8A        ; 0040F15D  mov al, byte ptr [eax + 0xa]
        _emit 0x40
        _emit 0x0A
        _emit 0xA8        ; 0040F160  test al, 0x20
        _emit 0x20
        _emit 0x0F        ; 0040F162  jne 0x40f8bf
        _emit 0x85
        _emit 0x57
        _emit 0x07
        _emit 0x00
        _emit 0x00
        _emit 0xA8        ; 0040F168  test al, 8
        _emit 0x08
        _emit 0x74        ; 0040F16A  je 0x40f17e
        _emit 0x12
        _emit 0x8B        ; 0040F16C  mov ecx, dword ptr [ebp + 0x15e]
        _emit 0x8D
        _emit 0x5E
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x83        ; 0040F172  and ecx, 0xc
        _emit 0xE1
        _emit 0x0C
        _emit 0x80        ; 0040F175  cmp cl, 0xc
        _emit 0xF9
        _emit 0x0C
        _emit 0x0F        ; 0040F178  je 0x40f8bf
        _emit 0x84
        _emit 0x41
        _emit 0x07
        _emit 0x00
        _emit 0x00
        _emit 0xA8        ; 0040F17E  test al, 0x80
        _emit 0x80
        _emit 0x74        ; 0040F180  je 0x40f194
        _emit 0x12
        _emit 0x8B        ; 0040F182  mov edx, dword ptr [ebp + 0x15e]
        _emit 0x95
        _emit 0x5E
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x83        ; 0040F188  and edx, 0xc
        _emit 0xE2
        _emit 0x0C
        _emit 0x80        ; 0040F18B  cmp dl, 8
        _emit 0xFA
        _emit 0x08
        _emit 0x0F        ; 0040F18E  je 0x40f8bf
        _emit 0x84
        _emit 0x2B
        _emit 0x07
        _emit 0x00
        _emit 0x00
        _emit 0x8D        ; 0040F194  lea eax, [ebp + 0xd9]
        _emit 0x85
        _emit 0xD9
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0xC7        ; 0040F19A  mov dword ptr [esp + 0x1c], 0
        _emit 0x44
        _emit 0x24
        _emit 0x1C
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x89        ; 0040F1A2  mov dword ptr [esp + 0x20], eax
        _emit 0x44
        _emit 0x24
        _emit 0x20
        _emit 0x8B        ; 0040F1A6  mov ecx, dword ptr [esp + 0x20]
        _emit 0x4C
        _emit 0x24
        _emit 0x20
        _emit 0x8B        ; 0040F1AA  mov ecx, dword ptr [ecx]
        _emit 0x09
        _emit 0x85        ; 0040F1AC  test ecx, ecx
        _emit 0xC9
        _emit 0x89        ; 0040F1AE  mov dword ptr [esp + 0x34], ecx
        _emit 0x4C
        _emit 0x24
        _emit 0x34
        _emit 0x0F        ; 0040F1B2  je 0x40f25f
        _emit 0x84
        _emit 0xA7
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0xF6        ; 0040F1B8  test byte ptr [ecx + 0xa], 6
        _emit 0x41
        _emit 0x0A
        _emit 0x06
        _emit 0x0F        ; 0040F1BC  je 0x40f25f
        _emit 0x84
        _emit 0x9D
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0xF6        ; 0040F1C2  test byte ptr [ebp + 0x5c], 1
        _emit 0x45
        _emit 0x5C
        _emit 0x01
        _emit 0x74        ; 0040F1C6  je 0x40f1e1
        _emit 0x19
        _emit 0x8B        ; 0040F1C8  mov eax, dword ptr [ebp + 8]
        _emit 0x45
        _emit 0x08
        _emit 0x0F        ; 0040F1CB  movsx esi, word ptr [ecx + 1]
        _emit 0xBF
        _emit 0x71
        _emit 0x01
        _emit 0x99        ; 0040F1CF  cdq
        _emit 0x81        ; 0040F1D0  and edx, 0xffff
        _emit 0xE2
        _emit 0xFF
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x03        ; 0040F1D6  add eax, edx
        _emit 0xC2
        _emit 0x8B        ; 0040F1D8  mov ebx, eax
        _emit 0xD8
        _emit 0xC1        ; 0040F1DA  sar ebx, 0x10
        _emit 0xFB
        _emit 0x10
        _emit 0x2B        ; 0040F1DD  sub ebx, esi
        _emit 0xDE
        _emit 0xEB        ; 0040F1DF  jmp 0x40f1f6
        _emit 0x15
        _emit 0x8B        ; 0040F1E1  mov eax, dword ptr [ebp + 8]
        _emit 0x45
        _emit 0x08
        _emit 0x0F        ; 0040F1E4  movsx ebx, word ptr [ecx + 1]
        _emit 0xBF
        _emit 0x59
        _emit 0x01
        _emit 0x99        ; 0040F1E8  cdq
        _emit 0x81        ; 0040F1E9  and edx, 0xffff
        _emit 0xE2
        _emit 0xFF
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x03        ; 0040F1EF  add eax, edx
        _emit 0xC2
        _emit 0xC1        ; 0040F1F1  sar eax, 0x10
        _emit 0xF8
        _emit 0x10
        _emit 0x03        ; 0040F1F4  add ebx, eax
        _emit 0xD8
        _emit 0x8B        ; 0040F1F6  mov eax, dword ptr [ebp + 0xc]
        _emit 0x45
        _emit 0x0C
        _emit 0x89        ; 0040F1F9  mov dword ptr [esp + 0x28], ebx
        _emit 0x5C
        _emit 0x24
        _emit 0x28
        _emit 0x0F        ; 0040F1FD  movsx esi, word ptr [ecx + 3]
        _emit 0xBF
        _emit 0x71
        _emit 0x03
        _emit 0x99        ; 0040F201  cdq
        _emit 0x81        ; 0040F202  and edx, 0xffff
        _emit 0xE2
        _emit 0xFF
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x03        ; 0040F208  add eax, edx
        _emit 0xC2
        _emit 0x0F        ; 0040F20A  movsx edx, word ptr [ecx + 7]
        _emit 0xBF
        _emit 0x51
        _emit 0x07
        _emit 0xC1        ; 0040F20E  sar eax, 0x10
        _emit 0xF8
        _emit 0x10
        _emit 0x03        ; 0040F211  add esi, eax
        _emit 0xF0
        _emit 0x0F        ; 0040F213  movsx eax, word ptr [ecx + 5]
        _emit 0xBF
        _emit 0x41
        _emit 0x05
        _emit 0x8B        ; 0040F217  mov ecx, dword ptr [esp + 0x30]
        _emit 0x4C
        _emit 0x24
        _emit 0x30
        _emit 0x89        ; 0040F21B  mov dword ptr [esp + 0x38], esi
        _emit 0x74
        _emit 0x24
        _emit 0x38
        _emit 0x8B        ; 0040F21F  mov esi, dword ptr [esp + 0x24]
        _emit 0x74
        _emit 0x24
        _emit 0x24
        _emit 0x2B        ; 0040F223  sub ebx, eax
        _emit 0xD8
        _emit 0x03        ; 0040F225  add ecx, esi
        _emit 0xCE
        _emit 0x3B        ; 0040F227  cmp ecx, ebx
        _emit 0xCB
        _emit 0x7E        ; 0040F229  jle 0x40f25f
        _emit 0x34
        _emit 0x8B        ; 0040F22B  mov ecx, dword ptr [esp + 0x28]
        _emit 0x4C
        _emit 0x24
        _emit 0x28
        _emit 0x8B        ; 0040F22F  mov ebx, dword ptr [esp + 0x30]
        _emit 0x5C
        _emit 0x24
        _emit 0x30
        _emit 0x03        ; 0040F233  add eax, ecx
        _emit 0xC1
        _emit 0x8B        ; 0040F235  mov ecx, esi
        _emit 0xCE
        _emit 0x2B        ; 0040F237  sub ecx, ebx
        _emit 0xCB
        _emit 0x3B        ; 0040F239  cmp ecx, eax
        _emit 0xC8
        _emit 0x7D        ; 0040F23B  jge 0x40f25f
        _emit 0x22
        _emit 0x8B        ; 0040F23D  mov eax, dword ptr [esp + 0x38]
        _emit 0x44
        _emit 0x24
        _emit 0x38
        _emit 0x8B        ; 0040F241  mov ecx, dword ptr [esp + 0x40]
        _emit 0x4C
        _emit 0x24
        _emit 0x40
        _emit 0x8B        ; 0040F245  mov ebx, dword ptr [esp + 0x2c]
        _emit 0x5C
        _emit 0x24
        _emit 0x2C
        _emit 0x8B        ; 0040F249  mov esi, eax
        _emit 0xF0
        _emit 0x2B        ; 0040F24B  sub esi, edx
        _emit 0xF2
        _emit 0x03        ; 0040F24D  add ebx, ecx
        _emit 0xD9
        _emit 0x3B        ; 0040F24F  cmp ebx, esi
        _emit 0xDE
        _emit 0x7E        ; 0040F251  jle 0x40f25f
        _emit 0x0C
        _emit 0x8B        ; 0040F253  mov esi, dword ptr [esp + 0x2c]
        _emit 0x74
        _emit 0x24
        _emit 0x2C
        _emit 0x03        ; 0040F257  add edx, eax
        _emit 0xD0
        _emit 0x2B        ; 0040F259  sub ecx, esi
        _emit 0xCE
        _emit 0x3B        ; 0040F25B  cmp ecx, edx
        _emit 0xCA
        _emit 0x7C        ; 0040F25D  jl 0x40f281
        _emit 0x22
        _emit 0x8B        ; 0040F25F  mov eax, dword ptr [esp + 0x1c]
        _emit 0x44
        _emit 0x24
        _emit 0x1C
        _emit 0x8B        ; 0040F263  mov edx, dword ptr [esp + 0x20]
        _emit 0x54
        _emit 0x24
        _emit 0x20
        _emit 0x40        ; 0040F267  inc eax
        _emit 0x83        ; 0040F268  add edx, 4
        _emit 0xC2
        _emit 0x04
        _emit 0x83        ; 0040F26B  cmp eax, 0x14
        _emit 0xF8
        _emit 0x14
        _emit 0x89        ; 0040F26E  mov dword ptr [esp + 0x1c], eax
        _emit 0x44
        _emit 0x24
        _emit 0x1C
        _emit 0x89        ; 0040F272  mov dword ptr [esp + 0x20], edx
        _emit 0x54
        _emit 0x24
        _emit 0x20
        _emit 0x0F        ; 0040F276  jl 0x40f1a6
        _emit 0x8C
        _emit 0x2A
        _emit 0xFF
        _emit 0xFF
        _emit 0xFF
        _emit 0xE9        ; 0040F27C  jmp 0x40f8bf
        _emit 0x3E
        _emit 0x06
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040F281  mov edx, dword ptr [esp + 0x14]
        _emit 0x54
        _emit 0x24
        _emit 0x14
        _emit 0x8B        ; 0040F285  mov ecx, dword ptr [ebp + 0x156]
        _emit 0x8D
        _emit 0x56
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040F28B  mov ebx, dword ptr [0x447ee0]
        _emit 0x1D
        _emit 0xE0
        _emit 0x7E
        _emit 0x44
        _emit 0x00
        _emit 0xC7        ; 0040F291  mov dword ptr [esp + 0x10], 0
        _emit 0x44
        _emit 0x24
        _emit 0x10
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x8A        ; 0040F299  mov al, byte ptr [edx + 0xc]
        _emit 0x42
        _emit 0x0C
        _emit 0x8B        ; 0040F29C  mov esi, eax
        _emit 0xF0
        _emit 0x8B        ; 0040F29E  mov eax, ecx
        _emit 0xC1
        _emit 0xC1        ; 0040F2A0  shl eax, 0xa
        _emit 0xE0
        _emit 0x0A
        _emit 0x03        ; 0040F2A3  add eax, ebx
        _emit 0xC3
        _emit 0x81        ; 0040F2A5  and esi, 0xff
        _emit 0xE6
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x89        ; 0040F2AB  mov dword ptr [esp + 0x4c], esi
        _emit 0x74
        _emit 0x24
        _emit 0x4C
        _emit 0x8B        ; 0040F2AF  mov edx, dword ptr [eax*4 + 0x4280e0]
        _emit 0x14
        _emit 0x85
        _emit 0xE0
        _emit 0x80
        _emit 0x42
        _emit 0x00
        _emit 0x8B        ; 0040F2B6  mov eax, ecx
        _emit 0xC1
        _emit 0x69        ; 0040F2B8  imul eax, eax, 0xe03f
        _emit 0xC0
        _emit 0x3F
        _emit 0xE0
        _emit 0x00
        _emit 0x00
        _emit 0x89        ; 0040F2BE  mov dword ptr [esp + 0x1c], edx
        _emit 0x54
        _emit 0x24
        _emit 0x1C
        _emit 0x8B        ; 0040F2C2  mov edx, dword ptr [edi + 0x15a]
        _emit 0x97
        _emit 0x5A
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x8D        ; 0040F2C8  lea ebx, [eax + 0x4d1d80]
        _emit 0x98
        _emit 0x80
        _emit 0x1D
        _emit 0x4D
        _emit 0x00
        _emit 0x85        ; 0040F2CE  test edx, edx
        _emit 0xD2
        _emit 0x8B        ; 0040F2D0  mov edx, dword ptr [esp + 0x18]
        _emit 0x54
        _emit 0x24
        _emit 0x18
        _emit 0x89        ; 0040F2D4  mov dword ptr [esp + 0x20], ebx
        _emit 0x5C
        _emit 0x24
        _emit 0x20
        _emit 0x75        ; 0040F2D8  jne 0x40f2e6
        _emit 0x0C
        _emit 0x89        ; 0040F2DA  mov dword ptr [edx + 0x4dfc79], ebp
        _emit 0xAA
        _emit 0x79
        _emit 0xFC
        _emit 0x4D
        _emit 0x00
        _emit 0x89        ; 0040F2E0  mov dword ptr [ebx + 0xdef9], edi
        _emit 0xBB
        _emit 0xF9
        _emit 0xDE
        _emit 0x00
        _emit 0x00
        _emit 0x83        ; 0040F2E6  cmp dword ptr [edi + 0x156], 0
        _emit 0xBF
        _emit 0x56
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x75        ; 0040F2ED  jne 0x40f2ff
        _emit 0x10
        _emit 0x89        ; 0040F2EF  mov dword ptr [0x4701c4], ecx
        _emit 0x0D
        _emit 0xC4
        _emit 0x01
        _emit 0x47
        _emit 0x00
        _emit 0xC7        ; 0040F2F5  mov dword ptr [0x4701c8], 0x3e8
        _emit 0x05
        _emit 0xC8
        _emit 0x01
        _emit 0x47
        _emit 0x00
        _emit 0xE8
        _emit 0x03
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040F2FF  mov ecx, dword ptr [eax + 0x4dfd7f]
        _emit 0x88
        _emit 0x7F
        _emit 0xFD
        _emit 0x4D
        _emit 0x00
        _emit 0x85        ; 0040F305  test ecx, ecx
        _emit 0xC9
        _emit 0x74        ; 0040F307  je 0x40f313
        _emit 0x0A
        _emit 0x8B        ; 0040F309  mov eax, dword ptr [eax + 0x4dfd83]
        _emit 0x80
        _emit 0x83
        _emit 0xFD
        _emit 0x4D
        _emit 0x00
        _emit 0x89        ; 0040F30F  mov dword ptr [esp + 0x1c], eax
        _emit 0x44
        _emit 0x24
        _emit 0x1C
        _emit 0x8B        ; 0040F313  mov ecx, dword ptr [edi + 0x15e]
        _emit 0x8F
        _emit 0x5E
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x83        ; 0040F319  or ecx, 0x10
        _emit 0xC9
        _emit 0x10
        _emit 0x89        ; 0040F31C  mov dword ptr [edi + 0x15e], ecx
        _emit 0x8F
        _emit 0x5E
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040F322  mov eax, dword ptr [edi + 0x15a]
        _emit 0x87
        _emit 0x5A
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x85        ; 0040F328  test eax, eax
        _emit 0xC0
        _emit 0x75        ; 0040F32A  jne 0x40f33d
        _emit 0x11
        _emit 0x8B        ; 0040F32C  mov ecx, dword ptr [edi + 0x14]
        _emit 0x4F
        _emit 0x14
        _emit 0x83        ; 0040F32F  and ecx, 1
        _emit 0xE1
        _emit 0x01
        _emit 0x8B        ; 0040F332  mov eax, dword ptr [ecx*4 + 0x41f130]
        _emit 0x04
        _emit 0x8D
        _emit 0x30
        _emit 0xF1
        _emit 0x41
        _emit 0x00
        _emit 0x40        ; 0040F339  inc eax
        _emit 0x89        ; 0040F33A  mov dword ptr [edi + 4], eax
        _emit 0x47
        _emit 0x04
        _emit 0x8B        ; 0040F33D  mov eax, dword ptr [ebp + 0x15a]
        _emit 0x85
        _emit 0x5A
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x85        ; 0040F343  test eax, eax
        _emit 0xC0
        _emit 0x75        ; 0040F345  jne 0x40f358
        _emit 0x11
        _emit 0x8B        ; 0040F347  mov ecx, dword ptr [edi + 0x14]
        _emit 0x4F
        _emit 0x14
        _emit 0x83        ; 0040F34A  and ecx, 1
        _emit 0xE1
        _emit 0x01
        _emit 0x8B        ; 0040F34D  mov eax, dword ptr [ecx*4 + 0x41f130]
        _emit 0x04
        _emit 0x8D
        _emit 0x30
        _emit 0xF1
        _emit 0x41
        _emit 0x00
        _emit 0x48        ; 0040F354  dec eax
        _emit 0x89        ; 0040F355  mov dword ptr [ebp + 4], eax
        _emit 0x45
        _emit 0x04
        _emit 0x8B        ; 0040F358  mov eax, dword ptr [ebp + 0x15a]
        _emit 0x85
        _emit 0x5A
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x83        ; 0040F35E  sub eax, 0
        _emit 0xE8
        _emit 0x00
        _emit 0x74        ; 0040F361  je 0x40f384
        _emit 0x21
        _emit 0x48        ; 0040F363  dec eax
        _emit 0x0F        ; 0040F364  jne 0x40f8bf
        _emit 0x85
        _emit 0x55
        _emit 0x05
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040F36A  mov eax, dword ptr [edi + 0x6c]
        _emit 0x47
        _emit 0x6C
        _emit 0x85        ; 0040F36D  test eax, eax
        _emit 0xC0
        _emit 0x0F        ; 0040F36F  je 0x40f8bf
        _emit 0x84
        _emit 0x4A
        _emit 0x05
        _emit 0x00
        _emit 0x00
        _emit 0x89        ; 0040F375  mov dword ptr [edi + 0x38], eax
        _emit 0x47
        _emit 0x38
        _emit 0xC7        ; 0040F378  mov dword ptr [edi + 0x6c], 0
        _emit 0x47
        _emit 0x6C
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0xE9        ; 0040F37F  jmp 0x40f8bf
        _emit 0x3B
        _emit 0x05
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040F384  mov eax, dword ptr [edx + 0x4dfd05]
        _emit 0x82
        _emit 0x05
        _emit 0xFD
        _emit 0x4D
        _emit 0x00
        _emit 0x85        ; 0040F38A  test eax, eax
        _emit 0xC0
        _emit 0x74        ; 0040F38C  je 0x40f398
        _emit 0x0A
        _emit 0xC7        ; 0040F38E  mov dword ptr [edx + 0x4dfd05], 2
        _emit 0x82
        _emit 0x05
        _emit 0xFD
        _emit 0x4D
        _emit 0x00
        _emit 0x02
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040F398  mov eax, dword ptr [ebx + 0xdf51]
        _emit 0x83
        _emit 0x51
        _emit 0xDF
        _emit 0x00
        _emit 0x00
        _emit 0x85        ; 0040F39E  test eax, eax
        _emit 0xC0
        _emit 0x74        ; 0040F3A0  je 0x40f3bd
        _emit 0x1B
        _emit 0xF6        ; 0040F3A2  test byte ptr [ebx + 0x7cb6], 8
        _emit 0x83
        _emit 0xB6
        _emit 0x7C
        _emit 0x00
        _emit 0x00
        _emit 0x08
        _emit 0x74        ; 0040F3A9  je 0x40f3bd
        _emit 0x12
        _emit 0x8B        ; 0040F3AB  mov ecx, dword ptr [edi + 8]
        _emit 0x4F
        _emit 0x08
        _emit 0x8B        ; 0040F3AE  mov eax, dword ptr [ebp + 8]
        _emit 0x45
        _emit 0x08
        _emit 0x33        ; 0040F3B1  xor edx, edx
        _emit 0xD2
        _emit 0x3B        ; 0040F3B3  cmp ecx, eax
        _emit 0xC8
        _emit 0x0F        ; 0040F3B5  setle dl
        _emit 0x9E
        _emit 0xC2
        _emit 0x42        ; 0040F3B8  inc edx
        _emit 0x8B        ; 0040F3B9  mov ecx, edx
        _emit 0xCA
        _emit 0xEB        ; 0040F3BB  jmp 0x40f3c2
        _emit 0x05
        _emit 0xB9        ; 0040F3BD  mov ecx, 1
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0xF6        ; 0040F3C2  test byte ptr [ebp + 0x15e], 0xc
        _emit 0x85
        _emit 0x5E
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x0C
        _emit 0x75        ; 0040F3C9  jne 0x40f449
        _emit 0x7E
        _emit 0x8B        ; 0040F3CB  mov eax, dword ptr [ebx + 0xdf5d]
        _emit 0x83
        _emit 0x5D
        _emit 0xDF
        _emit 0x00
        _emit 0x00
        _emit 0x85        ; 0040F3D1  test eax, eax
        _emit 0xC0
        _emit 0x74        ; 0040F3D3  je 0x40f3ec
        _emit 0x17
        _emit 0xE8        ; 0040F3D5  call 0x417a22
        _emit 0x48
        _emit 0x86
        _emit 0x00
        _emit 0x00
        _emit 0x99        ; 0040F3DA  cdq
        _emit 0xB9        ; 0040F3DB  mov ecx, 0x64
        _emit 0x64
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0xF7        ; 0040F3E0  idiv ecx
        _emit 0xF9
        _emit 0x3B        ; 0040F3E2  cmp edx, dword ptr [ebx + 0xdf61]
        _emit 0x93
        _emit 0x61
        _emit 0xDF
        _emit 0x00
        _emit 0x00
        _emit 0x7D        ; 0040F3E8  jge 0x40f449
        _emit 0x5F
        _emit 0xEB        ; 0040F3EA  jmp 0x40f441
        _emit 0x55
        _emit 0x8B        ; 0040F3EC  mov eax, dword ptr [ebx + 0x7cb6]
        _emit 0x83
        _emit 0xB6
        _emit 0x7C
        _emit 0x00
        _emit 0x00
        _emit 0xA8        ; 0040F3F2  test al, 8
        _emit 0x08
        _emit 0x74        ; 0040F3F4  je 0x40f42c
        _emit 0x36
        _emit 0x8A        ; 0040F3F6  mov cl, byte ptr [ebx + 0x7ca9]
        _emit 0x8B
        _emit 0xA9
        _emit 0x7C
        _emit 0x00
        _emit 0x00
        _emit 0xBA        ; 0040F3FC  mov edx, 1
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x83        ; 0040F401  add ecx, 4
        _emit 0xC1
        _emit 0x04
        _emit 0xD3        ; 0040F404  shl edx, cl
        _emit 0xE2
        _emit 0x8B        ; 0040F406  mov ecx, dword ptr [ebp + 0x156]
        _emit 0x8D
        _emit 0x56
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0xC1        ; 0040F40C  shl ecx, 0xa
        _emit 0xE1
        _emit 0x0A
        _emit 0x03        ; 0040F40F  add ecx, dword ptr [0x447ee0]
        _emit 0x0D
        _emit 0xE0
        _emit 0x7E
        _emit 0x44
        _emit 0x00
        _emit 0x85        ; 0040F415  test dword ptr [ecx*4 + 0x4280e0], edx
        _emit 0x14
        _emit 0x8D
        _emit 0xE0
        _emit 0x80
        _emit 0x42
        _emit 0x00
        _emit 0x75        ; 0040F41C  jne 0x40f441
        _emit 0x23
        _emit 0xA8        ; 0040F41E  test al, 1
        _emit 0x01
        _emit 0x74        ; 0040F420  je 0x40f449
        _emit 0x27
        _emit 0x8B        ; 0040F422  mov eax, dword ptr [esp + 0x1c]
        _emit 0x44
        _emit 0x24
        _emit 0x1C
        _emit 0x85        ; 0040F426  test eax, eax
        _emit 0xC0
        _emit 0x75        ; 0040F428  jne 0x40f449
        _emit 0x1F
        _emit 0xEB        ; 0040F42A  jmp 0x40f441
        _emit 0x15
        _emit 0x8B        ; 0040F42C  mov edx, dword ptr [esp + 0x1c]
        _emit 0x54
        _emit 0x24
        _emit 0x1C
        _emit 0x85        ; 0040F430  test edx, ecx
        _emit 0xCA
        _emit 0x75        ; 0040F432  jne 0x40f441
        _emit 0x0D
        _emit 0xA8        ; 0040F434  test al, 1
        _emit 0x01
        _emit 0x74        ; 0040F436  je 0x40f449
        _emit 0x11
        _emit 0x8B        ; 0040F438  mov eax, edx
        _emit 0xC2
        _emit 0xA9        ; 0040F43A  test eax, 0xfffffff7
        _emit 0xF7
        _emit 0xFF
        _emit 0xFF
        _emit 0xFF
        _emit 0x75        ; 0040F43F  jne 0x40f449
        _emit 0x08
        _emit 0xC7        ; 0040F441  mov dword ptr [esp + 0x10], 1
        _emit 0x44
        _emit 0x24
        _emit 0x10
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040F449  mov eax, dword ptr [ebp + 0x15e]
        _emit 0x85
        _emit 0x5E
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x83        ; 0040F44F  and eax, 0xc
        _emit 0xE0
        _emit 0x0C
        _emit 0x3C        ; 0040F452  cmp al, 0xc
        _emit 0x0C
        _emit 0x75        ; 0040F454  jne 0x40f461
        _emit 0x0B
        _emit 0xB8        ; 0040F456  mov eax, 1
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x89        ; 0040F45B  mov dword ptr [esp + 0x10], eax
        _emit 0x44
        _emit 0x24
        _emit 0x10
        _emit 0xEB        ; 0040F45F  jmp 0x40f465
        _emit 0x04
        _emit 0x8B        ; 0040F461  mov eax, dword ptr [esp + 0x10]
        _emit 0x44
        _emit 0x24
        _emit 0x10
        _emit 0x8B        ; 0040F465  mov ecx, dword ptr [esp + 0x14]
        _emit 0x4C
        _emit 0x24
        _emit 0x14
        _emit 0xF6        ; 0040F469  test byte ptr [ecx + 0xa], 0x40
        _emit 0x41
        _emit 0x0A
        _emit 0x40
        _emit 0x74        ; 0040F46D  je 0x40f475
        _emit 0x06
        _emit 0x33        ; 0040F46F  xor eax, eax
        _emit 0xC0
        _emit 0x89        ; 0040F471  mov dword ptr [esp + 0x10], eax
        _emit 0x44
        _emit 0x24
        _emit 0x10
        _emit 0xF6        ; 0040F475  test byte ptr [ebx + 0x7cb6], 2
        _emit 0x83
        _emit 0xB6
        _emit 0x7C
        _emit 0x00
        _emit 0x00
        _emit 0x02
        _emit 0x75        ; 0040F47C  jne 0x40f48e
        _emit 0x10
        _emit 0x8B        ; 0040F47E  mov edx, dword ptr [ebp + 0xc]
        _emit 0x55
        _emit 0x0C
        _emit 0x8B        ; 0040F481  mov ecx, dword ptr [ebp + 0x58]
        _emit 0x4D
        _emit 0x58
        _emit 0x3B        ; 0040F484  cmp edx, ecx
        _emit 0xD1
        _emit 0x7D        ; 0040F486  jge 0x40f48e
        _emit 0x06
        _emit 0x33        ; 0040F488  xor eax, eax
        _emit 0xC0
        _emit 0x89        ; 0040F48A  mov dword ptr [esp + 0x10], eax
        _emit 0x44
        _emit 0x24
        _emit 0x10
        _emit 0x8B        ; 0040F48E  mov ecx, dword ptr [edi + 0x129]
        _emit 0x8F
        _emit 0x29
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x85        ; 0040F494  test ecx, ecx
        _emit 0xC9
        _emit 0x0F        ; 0040F496  je 0x40f657
        _emit 0x84
        _emit 0xBB
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040F49C  mov edx, dword ptr [edi + 0xc]
        _emit 0x57
        _emit 0x0C
        _emit 0x3B        ; 0040F49F  cmp edx, dword ptr [edi + 0x58]
        _emit 0x57
        _emit 0x58
        _emit 0x8B        ; 0040F4A2  mov edx, dword ptr [ebp + 0xc]
        _emit 0x55
        _emit 0x0C
        _emit 0x3B        ; 0040F4A5  cmp edx, dword ptr [ebp + 0x58]
        _emit 0x55
        _emit 0x58
        _emit 0x7D        ; 0040F4A8  jge 0x40f4be
        _emit 0x14
        _emit 0x85        ; 0040F4AA  test eax, eax
        _emit 0xC0
        _emit 0x74        ; 0040F4AC  je 0x40f4b6
        _emit 0x08
        _emit 0x33        ; 0040F4AE  xor eax, eax
        _emit 0xC0
        _emit 0x66        ; 0040F4B0  mov ax, word ptr [ecx + 0xb]
        _emit 0x8B
        _emit 0x41
        _emit 0x0B
        _emit 0xEB        ; 0040F4B4  jmp 0x40f525
        _emit 0x6F
        _emit 0x33        ; 0040F4B6  xor eax, eax
        _emit 0xC0
        _emit 0x66        ; 0040F4B8  mov ax, word ptr [ecx + 5]
        _emit 0x8B
        _emit 0x41
        _emit 0x05
        _emit 0xEB        ; 0040F4BC  jmp 0x40f525
        _emit 0x67
        _emit 0xF6        ; 0040F4BE  test byte ptr [esp + 0x1c], 8
        _emit 0x44
        _emit 0x24
        _emit 0x1C
        _emit 0x08
        _emit 0x74        ; 0040F4C3  je 0x40f4f6
        _emit 0x31
        _emit 0x85        ; 0040F4C5  test eax, eax
        _emit 0xC0
        _emit 0x74        ; 0040F4C7  je 0x40f4ee
        _emit 0x25
        _emit 0x8B        ; 0040F4C9  mov edx, dword ptr [ebx + 0xdf5d]
        _emit 0x93
        _emit 0x5D
        _emit 0xDF
        _emit 0x00
        _emit 0x00
        _emit 0x33        ; 0040F4CF  xor eax, eax
        _emit 0xC0
        _emit 0x66        ; 0040F4D1  mov ax, word ptr [ecx + 9]
        _emit 0x8B
        _emit 0x41
        _emit 0x09
        _emit 0x85        ; 0040F4D5  test edx, edx
        _emit 0xD2
        _emit 0x74        ; 0040F4D7  je 0x40f525
        _emit 0x4C
        _emit 0x8D        ; 0040F4D9  lea edx, [eax + eax*8]
        _emit 0x14
        _emit 0xC0
        _emit 0xF6        ; 0040F4DC  test byte ptr [edx*4 + 0x438694], 1
        _emit 0x04
        _emit 0x95
        _emit 0x94
        _emit 0x86
        _emit 0x43
        _emit 0x00
        _emit 0x01
        _emit 0x74        ; 0040F4E4  je 0x40f525
        _emit 0x3F
        _emit 0x33        ; 0040F4E6  xor eax, eax
        _emit 0xC0
        _emit 0x66        ; 0040F4E8  mov ax, word ptr [ecx + 7]
        _emit 0x8B
        _emit 0x41
        _emit 0x07
        _emit 0xEB        ; 0040F4EC  jmp 0x40f525
        _emit 0x37
        _emit 0x33        ; 0040F4EE  xor eax, eax
        _emit 0xC0
        _emit 0x66        ; 0040F4F0  mov ax, word ptr [ecx + 3]
        _emit 0x8B
        _emit 0x41
        _emit 0x03
        _emit 0xEB        ; 0040F4F4  jmp 0x40f525
        _emit 0x2F
        _emit 0x85        ; 0040F4F6  test eax, eax
        _emit 0xC0
        _emit 0x74        ; 0040F4F8  je 0x40f51f
        _emit 0x25
        _emit 0x8B        ; 0040F4FA  mov edx, dword ptr [ebx + 0xdf5d]
        _emit 0x93
        _emit 0x5D
        _emit 0xDF
        _emit 0x00
        _emit 0x00
        _emit 0x33        ; 0040F500  xor eax, eax
        _emit 0xC0
        _emit 0x66        ; 0040F502  mov ax, word ptr [ecx + 7]
        _emit 0x8B
        _emit 0x41
        _emit 0x07
        _emit 0x85        ; 0040F506  test edx, edx
        _emit 0xD2
        _emit 0x74        ; 0040F508  je 0x40f525
        _emit 0x1B
        _emit 0x8D        ; 0040F50A  lea edx, [eax + eax*8]
        _emit 0x14
        _emit 0xC0
        _emit 0xF6        ; 0040F50D  test byte ptr [edx*4 + 0x438694], 1
        _emit 0x04
        _emit 0x95
        _emit 0x94
        _emit 0x86
        _emit 0x43
        _emit 0x00
        _emit 0x01
        _emit 0x74        ; 0040F515  je 0x40f525
        _emit 0x0E
        _emit 0x33        ; 0040F517  xor eax, eax
        _emit 0xC0
        _emit 0x66        ; 0040F519  mov ax, word ptr [ecx + 9]
        _emit 0x8B
        _emit 0x41
        _emit 0x09
        _emit 0xEB        ; 0040F51D  jmp 0x40f525
        _emit 0x06
        _emit 0x33        ; 0040F51F  xor eax, eax
        _emit 0xC0
        _emit 0x66        ; 0040F521  mov ax, word ptr [ecx + 1]
        _emit 0x8B
        _emit 0x41
        _emit 0x01
        _emit 0x85        ; 0040F525  test eax, eax
        _emit 0xC0
        _emit 0x0F        ; 0040F527  je 0x40f657
        _emit 0x84
        _emit 0x2A
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x8D        ; 0040F52D  lea ecx, [eax*4]
        _emit 0x0C
        _emit 0x85
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x33        ; 0040F534  xor edx, edx
        _emit 0xD2
        _emit 0x8D        ; 0040F536  lea eax, [eax + eax*8]
        _emit 0x04
        _emit 0xC0
        _emit 0x66        ; 0040F539  mov dx, word ptr [ecx + ebx + 0x6daa]
        _emit 0x8B
        _emit 0x94
        _emit 0x19
        _emit 0xAA
        _emit 0x6D
        _emit 0x00
        _emit 0x00
        _emit 0x89        ; 0040F541  mov dword ptr [ebp + 0x38], edx
        _emit 0x55
        _emit 0x38
        _emit 0x8A        ; 0040F544  mov dl, byte ptr [eax*4 + 0x438694]
        _emit 0x14
        _emit 0x85
        _emit 0x94
        _emit 0x86
        _emit 0x43
        _emit 0x00
        _emit 0xF6        ; 0040F54B  test dl, 1
        _emit 0xC2
        _emit 0x01
        _emit 0x74        ; 0040F54E  je 0x40f558
        _emit 0x08
        _emit 0xC7        ; 0040F550  mov dword ptr [esp + 0x10], 0
        _emit 0x44
        _emit 0x24
        _emit 0x10
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040F558  mov edx, dword ptr [esp + 0x14]
        _emit 0x54
        _emit 0x24
        _emit 0x14
        _emit 0x8A        ; 0040F55C  mov al, byte ptr [edx + 0xc]
        _emit 0x42
        _emit 0x0C
        _emit 0x84        ; 0040F55F  test al, al
        _emit 0xC0
        _emit 0x0F        ; 0040F561  je 0x40f635
        _emit 0x84
        _emit 0xCE
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040F567  mov eax, dword ptr [esp + 0x18]
        _emit 0x44
        _emit 0x24
        _emit 0x18
        _emit 0x66        ; 0040F56B  cmp word ptr [ecx + eax + 0x4d8b2c], 0
        _emit 0x83
        _emit 0xBC
        _emit 0x01
        _emit 0x2C
        _emit 0x8B
        _emit 0x4D
        _emit 0x00
        _emit 0x00
        _emit 0x8D        ; 0040F574  lea ebx, [ecx + eax + 0x4d8b2c]
        _emit 0x9C
        _emit 0x01
        _emit 0x2C
        _emit 0x8B
        _emit 0x4D
        _emit 0x00
        _emit 0x0F        ; 0040F57B  je 0x40f631
        _emit 0x84
        _emit 0xB0
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040F581  mov ecx, dword ptr [esp + 0x34]
        _emit 0x4C
        _emit 0x24
        _emit 0x34
        _emit 0x8B        ; 0040F585  mov esi, dword ptr [edi + 0x156]
        _emit 0xB7
        _emit 0x56
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040F58B  mov edx, dword ptr [esp + 0x28]
        _emit 0x54
        _emit 0x24
        _emit 0x28
        _emit 0x69        ; 0040F58F  imul esi, esi, 0xe03f
        _emit 0xF6
        _emit 0x3F
        _emit 0xE0
        _emit 0x00
        _emit 0x00
        _emit 0x0F        ; 0040F595  movsx eax, word ptr [ecx + 7]
        _emit 0xBF
        _emit 0x41
        _emit 0x07
        _emit 0x50        ; 0040F599  push eax
        _emit 0x81        ; 0040F59A  add esi, 0x4d1d80
        _emit 0xC6
        _emit 0x80
        _emit 0x1D
        _emit 0x4D
        _emit 0x00
        _emit 0x0F        ; 0040F5A0  movsx eax, word ptr [ecx + 5]
        _emit 0xBF
        _emit 0x41
        _emit 0x05
        _emit 0x8B        ; 0040F5A4  mov ecx, dword ptr [esp + 0x3c]
        _emit 0x4C
        _emit 0x24
        _emit 0x3C
        _emit 0x50        ; 0040F5A8  push eax
        _emit 0x8B        ; 0040F5A9  mov eax, dword ptr [esp + 0x34]
        _emit 0x44
        _emit 0x24
        _emit 0x34
        _emit 0x51        ; 0040F5AD  push ecx
        _emit 0x8B        ; 0040F5AE  mov ecx, dword ptr [esp + 0x3c]
        _emit 0x4C
        _emit 0x24
        _emit 0x3C
        _emit 0x52        ; 0040F5B2  push edx
        _emit 0x8B        ; 0040F5B3  mov edx, dword ptr [esp + 0x50]
        _emit 0x54
        _emit 0x24
        _emit 0x50
        _emit 0x50        ; 0040F5B7  push eax
        _emit 0x8B        ; 0040F5B8  mov eax, dword ptr [esp + 0x38]
        _emit 0x44
        _emit 0x24
        _emit 0x38
        _emit 0x51        ; 0040F5BC  push ecx
        _emit 0x52        ; 0040F5BD  push edx
        _emit 0x8D        ; 0040F5BE  lea ecx, [esp + 0x6c]
        _emit 0x4C
        _emit 0x24
        _emit 0x6C
        _emit 0x50        ; 0040F5C2  push eax
        _emit 0x51        ; 0040F5C3  push ecx
        _emit 0xE8        ; 0040F5C4  call 0x40eab0
        _emit 0xE7
        _emit 0xF4
        _emit 0xFF
        _emit 0xFF
        _emit 0x8B        ; 0040F5C9  mov edx, dword ptr [esp + 0x78]
        _emit 0x54
        _emit 0x24
        _emit 0x78
        _emit 0x8B        ; 0040F5CD  mov eax, dword ptr [esp + 0x74]
        _emit 0x44
        _emit 0x24
        _emit 0x74
        _emit 0x52        ; 0040F5D1  push edx
        _emit 0x50        ; 0040F5D2  push eax
        _emit 0x6A        ; 0040F5D3  push 0x5d
        _emit 0x5D
        _emit 0x6A        ; 0040F5D5  push 4
        _emit 0x04
        _emit 0xE8        ; 0040F5D7  call 0x406570
        _emit 0x94
        _emit 0x6F
        _emit 0xFF
        _emit 0xFF
        _emit 0x8B        ; 0040F5DC  mov ecx, dword ptr [edi + 0x156]
        _emit 0x8F
        _emit 0x56
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040F5E2  mov edx, dword ptr [edi + 0x5c]
        _emit 0x57
        _emit 0x5C
        _emit 0x89        ; 0040F5E5  mov dword ptr [eax + 0x156], ecx
        _emit 0x88
        _emit 0x56
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040F5EB  mov ecx, dword ptr [edi + 0x14]
        _emit 0x4F
        _emit 0x14
        _emit 0x89        ; 0040F5EE  mov dword ptr [eax + 0x14], ecx
        _emit 0x48
        _emit 0x14
        _emit 0x33        ; 0040F5F1  xor ecx, ecx
        _emit 0xC9
        _emit 0x66        ; 0040F5F3  mov cx, word ptr [ebx]
        _emit 0x8B
        _emit 0x0B
        _emit 0x89        ; 0040F5F6  mov dword ptr [eax + 0x5c], edx
        _emit 0x50
        _emit 0x5C
        _emit 0x89        ; 0040F5F9  mov dword ptr [eax + 0x30], ecx
        _emit 0x48
        _emit 0x30
        _emit 0xC7        ; 0040F5FC  mov dword ptr [eax + 0x15a], 1
        _emit 0x80
        _emit 0x5A
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x8D        ; 0040F606  lea edx, [ecx + ecx*4]
        _emit 0x14
        _emit 0x89
        _emit 0x83        ; 0040F609  add esp, 0x34
        _emit 0xC4
        _emit 0x34
        _emit 0xC1        ; 0040F60C  shl edx, 3
        _emit 0xE2
        _emit 0x03
        _emit 0x2B        ; 0040F60F  sub edx, ecx
        _emit 0xD1
        _emit 0x8B        ; 0040F611  mov ecx, dword ptr [esi + 0x110]
        _emit 0x8E
        _emit 0x10
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x33        ; 0040F617  xor esi, esi
        _emit 0xF6
        _emit 0x66        ; 0040F619  mov si, word ptr [edx + ecx + 0x20]
        _emit 0x8B
        _emit 0x74
        _emit 0x0A
        _emit 0x20
        _emit 0x8B        ; 0040F61E  mov ecx, dword ptr [eax + 0x28]
        _emit 0x48
        _emit 0x28
        _emit 0x81        ; 0040F621  or ecx, 0x40000000
        _emit 0xC9
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x40
        _emit 0x89        ; 0040F627  mov dword ptr [eax + 0x2c], esi
        _emit 0x70
        _emit 0x2C
        _emit 0x8B        ; 0040F62A  mov esi, dword ptr [esp + 0x4c]
        _emit 0x74
        _emit 0x24
        _emit 0x4C
        _emit 0x89        ; 0040F62E  mov dword ptr [eax + 0x28], ecx
        _emit 0x48
        _emit 0x28
        _emit 0x8B        ; 0040F631  mov ebx, dword ptr [esp + 0x20]
        _emit 0x5C
        _emit 0x24
        _emit 0x20
        _emit 0x8B        ; 0040F635  mov eax, dword ptr [ebp + 0x38]
        _emit 0x45
        _emit 0x38
        _emit 0x85        ; 0040F638  test eax, eax
        _emit 0xC0
        _emit 0x75        ; 0040F63A  jne 0x40f672
        _emit 0x36
        _emit 0x85        ; 0040F63C  test esi, esi
        _emit 0xF6
        _emit 0x74        ; 0040F63E  je 0x40f672
        _emit 0x32
        _emit 0x68        ; 0040F640  push 0xff
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x68        ; 0040F645  push 0x41f1d0
        _emit 0xD0
        _emit 0xF1
        _emit 0x41
        _emit 0x00
        _emit 0xE8        ; 0040F64A  call 0x415190
        _emit 0x41
        _emit 0x5B
        _emit 0x00
        _emit 0x00
        _emit 0x83        ; 0040F64F  add esp, 8
        _emit 0xC4
        _emit 0x08
        _emit 0xE9        ; 0040F652  jmp 0x40f8bf
        _emit 0x68
        _emit 0x02
        _emit 0x00
        _emit 0x00
        _emit 0x85        ; 0040F657  test esi, esi
        _emit 0xF6
        _emit 0x74        ; 0040F659  je 0x40f672
        _emit 0x17
        _emit 0x68        ; 0040F65B  push 0xff
        _emit 0xFF
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x68        ; 0040F660  push 0x41f1e4
        _emit 0xE4
        _emit 0xF1
        _emit 0x41
        _emit 0x00
        _emit 0xE8        ; 0040F665  call 0x415190
        _emit 0x26
        _emit 0x5B
        _emit 0x00
        _emit 0x00
        _emit 0x83        ; 0040F66A  add esp, 8
        _emit 0xC4
        _emit 0x08
        _emit 0xE9        ; 0040F66D  jmp 0x40f8bf
        _emit 0x4D
        _emit 0x02
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040F672  mov eax, dword ptr [esp + 0x10]
        _emit 0x44
        _emit 0x24
        _emit 0x10
        _emit 0x85        ; 0040F676  test eax, eax
        _emit 0xC0
        _emit 0x0F        ; 0040F678  je 0x40f700
        _emit 0x84
        _emit 0x82
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040F67E  mov edx, dword ptr [edi + 0x5c]
        _emit 0x57
        _emit 0x5C
        _emit 0x83        ; 0040F681  xor edx, 1
        _emit 0xF2
        _emit 0x01
        _emit 0x85        ; 0040F684  test esi, esi
        _emit 0xF6
        _emit 0x89        ; 0040F686  mov dword ptr [ebp + 0x5c], edx
        _emit 0x55
        _emit 0x5C
        _emit 0x74        ; 0040F689  je 0x40f6e6
        _emit 0x5B
        _emit 0x33        ; 0040F68B  xor eax, eax
        _emit 0xC0
        _emit 0xA0        ; 0040F68D  mov al, byte ptr [0x43a29a]
        _emit 0x9A
        _emit 0xA2
        _emit 0x43
        _emit 0x00
        _emit 0x89        ; 0040F692  mov dword ptr [ebp + 0x40], eax
        _emit 0x45
        _emit 0x40
        _emit 0x89        ; 0040F695  mov dword ptr [edi + 0x40], eax
        _emit 0x47
        _emit 0x40
        _emit 0x8B        ; 0040F698  mov eax, dword ptr [esp + 0x14]
        _emit 0x44
        _emit 0x24
        _emit 0x14
        _emit 0x8B        ; 0040F69C  mov edx, dword ptr [ebp + 0x15e]
        _emit 0x95
        _emit 0x5E
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x83        ; 0040F6A2  or edx, 0xc
        _emit 0xCA
        _emit 0x0C
        _emit 0x8A        ; 0040F6A5  mov cl, byte ptr [eax + 0xa]
        _emit 0x48
        _emit 0x0A
        _emit 0x89        ; 0040F6A8  mov dword ptr [ebp + 0x15e], edx
        _emit 0x95
        _emit 0x5E
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0xF6        ; 0040F6AE  test cl, 4
        _emit 0xC1
        _emit 0x04
        _emit 0x74        ; 0040F6B1  je 0x40f6e6
        _emit 0x33
        _emit 0x8B        ; 0040F6B3  mov edx, dword ptr [esp + 0x18]
        _emit 0x54
        _emit 0x24
        _emit 0x18
        _emit 0x33        ; 0040F6B7  xor ecx, ecx
        _emit 0xC9
        _emit 0xB8        ; 0040F6B9  mov eax, 0x51eb851f
        _emit 0x1F
        _emit 0x85
        _emit 0xEB
        _emit 0x51
        _emit 0x8A        ; 0040F6BE  mov cl, byte ptr [edx + 0x4d9a25]
        _emit 0x8A
        _emit 0x25
        _emit 0x9A
        _emit 0x4D
        _emit 0x00
        _emit 0x0F        ; 0040F6C4  imul ecx, esi
        _emit 0xAF
        _emit 0xCE
        _emit 0xF7        ; 0040F6C7  imul ecx
        _emit 0xE9
        _emit 0xC1        ; 0040F6C9  sar edx, 5
        _emit 0xFA
        _emit 0x05
        _emit 0x8B        ; 0040F6CC  mov eax, edx
        _emit 0xC2
        _emit 0xC1        ; 0040F6CE  shr eax, 0x1f
        _emit 0xE8
        _emit 0x1F
        _emit 0x03        ; 0040F6D1  add edx, eax
        _emit 0xD0
        _emit 0x75        ; 0040F6D3  jne 0x40f6da
        _emit 0x05
        _emit 0xBA        ; 0040F6D5  mov edx, 1
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0xF7        ; 0040F6DA  neg edx
        _emit 0xDA
        _emit 0x52        ; 0040F6DC  push edx
        _emit 0x53        ; 0040F6DD  push ebx
        _emit 0xE8        ; 0040F6DE  call 0x40e7c0
        _emit 0xDD
        _emit 0xF0
        _emit 0xFF
        _emit 0xFF
        _emit 0x83        ; 0040F6E3  add esp, 8
        _emit 0xC4
        _emit 0x08
        _emit 0x8B        ; 0040F6E6  mov eax, dword ptr [edi + 0x68]
        _emit 0x47
        _emit 0x68
        _emit 0x85        ; 0040F6E9  test eax, eax
        _emit 0xC0
        _emit 0x0F        ; 0040F6EB  je 0x40f896
        _emit 0x84
        _emit 0xA5
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x89        ; 0040F6F1  mov dword ptr [edi + 0x38], eax
        _emit 0x47
        _emit 0x38
        _emit 0xC7        ; 0040F6F4  mov dword ptr [edi + 0x68], 0
        _emit 0x47
        _emit 0x68
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0xE9        ; 0040F6FB  jmp 0x40f896
        _emit 0x96
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040F700  mov ecx, dword ptr [esp + 0x34]
        _emit 0x4C
        _emit 0x24
        _emit 0x34
        _emit 0xF6        ; 0040F704  test byte ptr [ecx + 0xa], 4
        _emit 0x41
        _emit 0x0A
        _emit 0x04
        _emit 0x74        ; 0040F708  je 0x40f73c
        _emit 0x32
        _emit 0x8B        ; 0040F70A  mov eax, dword ptr [esp + 0x18]
        _emit 0x44
        _emit 0x24
        _emit 0x18
        _emit 0x68        ; 0040F70E  push 0x8fffff
        _emit 0xFF
        _emit 0xFF
        _emit 0x8F
        _emit 0x00
        _emit 0x68        ; 0040F713  push 0x41f1f8
        _emit 0xF8
        _emit 0xF1
        _emit 0x41
        _emit 0x00
        _emit 0x8B        ; 0040F718  mov ecx, dword ptr [eax + 0x4dfcad]
        _emit 0x88
        _emit 0xAD
        _emit 0xFC
        _emit 0x4D
        _emit 0x00
        _emit 0x83        ; 0040F71E  or ecx, 3
        _emit 0xC9
        _emit 0x03
        _emit 0x89        ; 0040F721  mov dword ptr [eax + 0x4dfcad], ecx
        _emit 0x88
        _emit 0xAD
        _emit 0xFC
        _emit 0x4D
        _emit 0x00
        _emit 0x8B        ; 0040F727  mov eax, dword ptr [ebx + 0xdf31]
        _emit 0x83
        _emit 0x31
        _emit 0xDF
        _emit 0x00
        _emit 0x00
        _emit 0x40        ; 0040F72D  inc eax
        _emit 0x89        ; 0040F72E  mov dword ptr [ebx + 0xdf31], eax
        _emit 0x83
        _emit 0x31
        _emit 0xDF
        _emit 0x00
        _emit 0x00
        _emit 0xE8        ; 0040F734  call 0x415190
        _emit 0x57
        _emit 0x5A
        _emit 0x00
        _emit 0x00
        _emit 0x83        ; 0040F739  add esp, 8
        _emit 0xC4
        _emit 0x08
        _emit 0x8B        ; 0040F73C  mov edx, dword ptr [edi + 0x5c]
        _emit 0x57
        _emit 0x5C
        _emit 0x83        ; 0040F73F  xor edx, 1
        _emit 0xF2
        _emit 0x01
        _emit 0x85        ; 0040F742  test esi, esi
        _emit 0xF6
        _emit 0x89        ; 0040F744  mov dword ptr [ebp + 0x5c], edx
        _emit 0x55
        _emit 0x5C
        _emit 0x0F        ; 0040F747  je 0x40f80e
        _emit 0x84
        _emit 0xC1
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x33        ; 0040F74D  xor eax, eax
        _emit 0xC0
        _emit 0xA0        ; 0040F74F  mov al, byte ptr [0x43a299]
        _emit 0x99
        _emit 0xA2
        _emit 0x43
        _emit 0x00
        _emit 0x89        ; 0040F754  mov dword ptr [ebp + 0x40], eax
        _emit 0x45
        _emit 0x40
        _emit 0x89        ; 0040F757  mov dword ptr [edi + 0x40], eax
        _emit 0x47
        _emit 0x40
        _emit 0x8B        ; 0040F75A  mov eax, dword ptr [esp + 0x18]
        _emit 0x44
        _emit 0x24
        _emit 0x18
        _emit 0x8B        ; 0040F75E  mov edx, dword ptr [edi + 0x156]
        _emit 0x97
        _emit 0x56
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x0F        ; 0040F764  movsx ecx, word ptr [eax + 0x4d9a3e]
        _emit 0xBF
        _emit 0x88
        _emit 0x3E
        _emit 0x9A
        _emit 0x4D
        _emit 0x00
        _emit 0x51        ; 0040F76B  push ecx
        _emit 0x52        ; 0040F76C  push edx
        _emit 0xE8        ; 0040F76D  call 0x40e6f0
        _emit 0x7E
        _emit 0xEF
        _emit 0xFF
        _emit 0xFF
        _emit 0x0F        ; 0040F772  movsx eax, word ptr [ebx + 0x7cc0]
        _emit 0xBF
        _emit 0x83
        _emit 0xC0
        _emit 0x7C
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040F779  mov ecx, dword ptr [ebp + 0x156]
        _emit 0x8D
        _emit 0x56
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x50        ; 0040F77F  push eax
        _emit 0x51        ; 0040F780  push ecx
        _emit 0xE8        ; 0040F781  call 0x40e6f0
        _emit 0x6A
        _emit 0xEF
        _emit 0xFF
        _emit 0xFF
        _emit 0x8B        ; 0040F786  mov eax, dword ptr [esp + 0x28]
        _emit 0x44
        _emit 0x24
        _emit 0x28
        _emit 0x33        ; 0040F78A  xor ecx, ecx
        _emit 0xC9
        _emit 0x8B        ; 0040F78C  mov edx, dword ptr [ebp + 0x15e]
        _emit 0x95
        _emit 0x5E
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x83        ; 0040F792  add esp, 0x10
        _emit 0xC4
        _emit 0x10
        _emit 0x8A        ; 0040F795  mov cl, byte ptr [eax + 0x4d9a28]
        _emit 0x88
        _emit 0x28
        _emit 0x9A
        _emit 0x4D
        _emit 0x00
        _emit 0x83        ; 0040F79B  and edx, 0xfffffffb
        _emit 0xE2
        _emit 0xFB
        _emit 0x0F        ; 0040F79E  imul ecx, dword ptr [ebx + 0xdf01]
        _emit 0xAF
        _emit 0x8B
        _emit 0x01
        _emit 0xDF
        _emit 0x00
        _emit 0x00
        _emit 0x0F        ; 0040F7A5  imul ecx, esi
        _emit 0xAF
        _emit 0xCE
        _emit 0x83        ; 0040F7A8  or edx, 8
        _emit 0xCA
        _emit 0x08
        _emit 0xB8        ; 0040F7AB  mov eax, 0xae147ae1
        _emit 0xE1
        _emit 0x7A
        _emit 0x14
        _emit 0xAE
        _emit 0x89        ; 0040F7B0  mov dword ptr [ebp + 0x15e], edx
        _emit 0x95
        _emit 0x5E
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0xF7        ; 0040F7B6  imul ecx
        _emit 0xE9
        _emit 0xC1        ; 0040F7B8  sar edx, 5
        _emit 0xFA
        _emit 0x05
        _emit 0x8B        ; 0040F7BB  mov ecx, edx
        _emit 0xCA
        _emit 0xC1        ; 0040F7BD  shr ecx, 0x1f
        _emit 0xE9
        _emit 0x1F
        _emit 0x03        ; 0040F7C0  add edx, ecx
        _emit 0xD1
        _emit 0x03        ; 0040F7C2  add esi, edx
        _emit 0xF2
        _emit 0x83        ; 0040F7C4  cmp esi, 1
        _emit 0xFE
        _emit 0x01
        _emit 0x7D        ; 0040F7C7  jge 0x40f7ce
        _emit 0x05
        _emit 0xBE        ; 0040F7C9  mov esi, 1
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040F7CE  mov edx, dword ptr [esp + 0x34]
        _emit 0x54
        _emit 0x24
        _emit 0x34
        _emit 0x33        ; 0040F7D2  xor ecx, ecx
        _emit 0xC9
        _emit 0xB8        ; 0040F7D4  mov eax, 0x51eb851f
        _emit 0x1F
        _emit 0x85
        _emit 0xEB
        _emit 0x51
        _emit 0x8A        ; 0040F7D9  mov cl, byte ptr [edx + 0xb]
        _emit 0x4A
        _emit 0x0B
        _emit 0x0F        ; 0040F7DC  imul ecx, esi
        _emit 0xAF
        _emit 0xCE
        _emit 0xF7        ; 0040F7DF  imul ecx
        _emit 0xE9
        _emit 0xC1        ; 0040F7E1  sar edx, 5
        _emit 0xFA
        _emit 0x05
        _emit 0x8B        ; 0040F7E4  mov eax, edx
        _emit 0xC2
        _emit 0xC1        ; 0040F7E6  shr eax, 0x1f
        _emit 0xE8
        _emit 0x1F
        _emit 0x03        ; 0040F7E9  add edx, eax
        _emit 0xD0
        _emit 0x83        ; 0040F7EB  cmp edx, 1
        _emit 0xFA
        _emit 0x01
        _emit 0x7D        ; 0040F7EE  jge 0x40f7f5
        _emit 0x05
        _emit 0xBA        ; 0040F7F0  mov edx, 1
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0xF7        ; 0040F7F5  neg edx
        _emit 0xDA
        _emit 0x52        ; 0040F7F7  push edx
        _emit 0x53        ; 0040F7F8  push ebx
        _emit 0xE8        ; 0040F7F9  call 0x40e7c0
        _emit 0xC2
        _emit 0xEF
        _emit 0xFF
        _emit 0xFF
        _emit 0x8B        ; 0040F7FE  mov eax, dword ptr [ebx + 0xdf01]
        _emit 0x83
        _emit 0x01
        _emit 0xDF
        _emit 0x00
        _emit 0x00
        _emit 0x83        ; 0040F804  add esp, 8
        _emit 0xC4
        _emit 0x08
        _emit 0x40        ; 0040F807  inc eax
        _emit 0x89        ; 0040F808  mov dword ptr [ebx + 0xdf01], eax
        _emit 0x83
        _emit 0x01
        _emit 0xDF
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040F80E  mov esi, dword ptr [esp + 0x20]
        _emit 0x74
        _emit 0x24
        _emit 0x20
        _emit 0x8B        ; 0040F812  mov eax, dword ptr [esi + 0xdf01]
        _emit 0x86
        _emit 0x01
        _emit 0xDF
        _emit 0x00
        _emit 0x00
        _emit 0x8D        ; 0040F818  lea ebx, [esi + 0xdf01]
        _emit 0x9E
        _emit 0x01
        _emit 0xDF
        _emit 0x00
        _emit 0x00
        _emit 0x83        ; 0040F81E  cmp eax, 1
        _emit 0xF8
        _emit 0x01
        _emit 0x7E        ; 0040F821  jle 0x40f859
        _emit 0x36
        _emit 0x8B        ; 0040F823  mov eax, dword ptr [esp + 0x38]
        _emit 0x44
        _emit 0x24
        _emit 0x38
        _emit 0xC1        ; 0040F827  shl eax, 0x10
        _emit 0xE0
        _emit 0x10
        _emit 0x50        ; 0040F82A  push eax
        _emit 0x8B        ; 0040F82B  mov eax, dword ptr [esp + 0x2c]
        _emit 0x44
        _emit 0x24
        _emit 0x2C
        _emit 0xC1        ; 0040F82F  shl eax, 0x10
        _emit 0xE0
        _emit 0x10
        _emit 0x50        ; 0040F832  push eax
        _emit 0x6A        ; 0040F833  push 0x5e
        _emit 0x5E
        _emit 0x6A        ; 0040F835  push 7
        _emit 0x07
        _emit 0xE8        ; 0040F837  call 0x406570
        _emit 0x34
        _emit 0x6D
        _emit 0xFF
        _emit 0xFF
        _emit 0x8B        ; 0040F83C  mov ecx, dword ptr [ebx]
        _emit 0x0B
        _emit 0x83        ; 0040F83E  add esp, 0x10
        _emit 0xC4
        _emit 0x10
        _emit 0x89        ; 0040F841  mov dword ptr [eax + 0x156], ecx
        _emit 0x88
        _emit 0x56
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040F847  mov ecx, dword ptr [eax + 0x28]
        _emit 0x48
        _emit 0x28
        _emit 0x81        ; 0040F84A  or ecx, 0x40000000
        _emit 0xC9
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x40
        _emit 0x89        ; 0040F850  mov dword ptr [eax + 0x15e], ebx
        _emit 0x98
        _emit 0x5E
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x89        ; 0040F856  mov dword ptr [eax + 0x28], ecx
        _emit 0x48
        _emit 0x28
        _emit 0x8B        ; 0040F859  mov eax, dword ptr [edi + 0x15a]
        _emit 0x87
        _emit 0x5A
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x85        ; 0040F85F  test eax, eax
        _emit 0xC0
        _emit 0x75        ; 0040F861  jne 0x40f885
        _emit 0x22
        _emit 0x8B        ; 0040F863  mov eax, dword ptr [esp + 0x18]
        _emit 0x44
        _emit 0x24
        _emit 0x18
        _emit 0x8B        ; 0040F867  mov ecx, dword ptr [eax + 0x4dfcad]
        _emit 0x88
        _emit 0xAD
        _emit 0xFC
        _emit 0x4D
        _emit 0x00
        _emit 0x83        ; 0040F86D  or ecx, 1
        _emit 0xC9
        _emit 0x01
        _emit 0x89        ; 0040F870  mov dword ptr [eax + 0x4dfcad], ecx
        _emit 0x88
        _emit 0xAD
        _emit 0xFC
        _emit 0x4D
        _emit 0x00
        _emit 0x8B        ; 0040F876  mov eax, dword ptr [esi + 0xdf31]
        _emit 0x86
        _emit 0x31
        _emit 0xDF
        _emit 0x00
        _emit 0x00
        _emit 0x40        ; 0040F87C  inc eax
        _emit 0x89        ; 0040F87D  mov dword ptr [esi + 0xdf31], eax
        _emit 0x86
        _emit 0x31
        _emit 0xDF
        _emit 0x00
        _emit 0x00
        _emit 0xEB        ; 0040F883  jmp 0x40f896
        _emit 0x11
        _emit 0x8B        ; 0040F885  mov eax, dword ptr [edi + 0x6c]
        _emit 0x47
        _emit 0x6C
        _emit 0x85        ; 0040F888  test eax, eax
        _emit 0xC0
        _emit 0x74        ; 0040F88A  je 0x40f896
        _emit 0x0A
        _emit 0x89        ; 0040F88C  mov dword ptr [edi + 0x38], eax
        _emit 0x47
        _emit 0x38
        _emit 0xC7        ; 0040F88F  mov dword ptr [edi + 0x6c], 0
        _emit 0x47
        _emit 0x6C
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040F896  mov eax, dword ptr [edi + 0x15a]
        _emit 0x87
        _emit 0x5A
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x85        ; 0040F89C  test eax, eax
        _emit 0xC0
        _emit 0x75        ; 0040F89E  jne 0x40f8ac
        _emit 0x0C
        _emit 0x8B        ; 0040F8A0  mov edx, dword ptr [esp + 0x20]
        _emit 0x54
        _emit 0x24
        _emit 0x20
        _emit 0x89        ; 0040F8A4  mov dword ptr [edx + 0xdefd], edi
        _emit 0xBA
        _emit 0xFD
        _emit 0xDE
        _emit 0x00
        _emit 0x00
        _emit 0xEB        ; 0040F8AA  jmp 0x40f8bf
        _emit 0x13
        _emit 0xF7        ; 0040F8AC  test dword ptr [edi + 0x28], 0x20000000
        _emit 0x47
        _emit 0x28
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x20
        _emit 0x74        ; 0040F8B3  je 0x40f8bf
        _emit 0x0A
        _emit 0x8B        ; 0040F8B5  mov eax, dword ptr [esp + 0x20]
        _emit 0x44
        _emit 0x24
        _emit 0x20
        _emit 0x89        ; 0040F8B9  mov dword ptr [eax + 0xdefd], edi
        _emit 0xB8
        _emit 0xFD
        _emit 0xDE
        _emit 0x00
        _emit 0x00
        _emit 0x8B        ; 0040F8BF  mov eax, dword ptr [esp + 0x44]
        _emit 0x44
        _emit 0x24
        _emit 0x44
        _emit 0x81        ; 0040F8C3  add ebp, 0x17e
        _emit 0xC5
        _emit 0x7E
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x40        ; 0040F8C9  inc eax
        _emit 0x3D        ; 0040F8CA  cmp eax, 0x400
        _emit 0x00
        _emit 0x04
        _emit 0x00
        _emit 0x00
        _emit 0x89        ; 0040F8CF  mov dword ptr [esp + 0x44], eax
        _emit 0x44
        _emit 0x24
        _emit 0x44
        _emit 0x0F        ; 0040F8D3  jl 0x40f0d0
        _emit 0x8C
        _emit 0xF7
        _emit 0xF7
        _emit 0xFF
        _emit 0xFF
        _emit 0x8B        ; 0040F8D9  mov eax, dword ptr [esp + 0x3c]
        _emit 0x44
        _emit 0x24
        _emit 0x3C
        _emit 0x40        ; 0040F8DD  inc eax
        _emit 0x83        ; 0040F8DE  cmp eax, 0x14
        _emit 0xF8
        _emit 0x14
        _emit 0x89        ; 0040F8E1  mov dword ptr [esp + 0x3c], eax
        _emit 0x44
        _emit 0x24
        _emit 0x3C
        _emit 0x0F        ; 0040F8E5  jl 0x40f04c
        _emit 0x8C
        _emit 0x61
        _emit 0xF7
        _emit 0xFF
        _emit 0xFF
        _emit 0x8B        ; 0040F8EB  mov eax, dword ptr [esp + 0x48]
        _emit 0x44
        _emit 0x24
        _emit 0x48
        _emit 0x81        ; 0040F8EF  add edi, 0x17e
        _emit 0xC7
        _emit 0x7E
        _emit 0x01
        _emit 0x00
        _emit 0x00
        _emit 0x40        ; 0040F8F5  inc eax
        _emit 0x3D        ; 0040F8F6  cmp eax, 0x400
        _emit 0x00
        _emit 0x04
        _emit 0x00
        _emit 0x00
        _emit 0x89        ; 0040F8FB  mov dword ptr [esp + 0x48], eax
        _emit 0x44
        _emit 0x24
        _emit 0x48
        _emit 0x0F        ; 0040F8FF  jl 0x40f024
        _emit 0x8C
        _emit 0x1F
        _emit 0xF7
        _emit 0xFF
        _emit 0xFF
        _emit 0x5F        ; 0040F905  pop edi
        _emit 0x5E        ; 0040F906  pop esi
        _emit 0x5D        ; 0040F907  pop ebp
        _emit 0x5B        ; 0040F908  pop ebx
        _emit 0x83        ; 0040F909  add esp, 0x48
        _emit 0xC4
        _emit 0x48
        _emit 0xC3        ; 0040F90C  ret
        _emit 0x90        ; 0040F90D  nop
        _emit 0x90        ; 0040F90E  nop
        _emit 0x90        ; 0040F90F  nop
    }
}

/*
 * Moves every script object for this frame and keeps bodies apart:
 *   * momentum += gravity, position += momentum (16.16), unless the object is in hit stop;
 *   * objects that follow their parent (iFlags 0x20000000) are placed at the parent plus their offset;
 *   * players: the push-back from the last frame is applied, a thrown opponent is carried along, and
 *     the player is kept inside the stage (x 50..1230 pixels) or, in versus modes, story fights with
 *     option 4, or with a DS wall branch registered, inside the screen (camera x + 50..590); a player
 *     in a hit reaction pushed against a wall pushes its attacker back by the overlap instead, and the
 *     DS wall branch (4) is taken;
 *   * the body boxes (FD flag 1) of players and their objects on the same line push each other apart
 *     by a quarter of the overlap per frame (only the one in the air when just one of them is; a
 *     knocked-out character does not push the other).
 * Globals: reads gkgtGameState, giCameraX, giCurrentStoryStep[giStoryModeSide], gpkgtCurrentEngineObject;
 * changes gkgtEngineObjects[] positions and gkgtLoadedCharacter[].iPushBackX.
 */
void vHandleHitMovements(void)
{
    kgtEngineObject *pObj;
    int iW;
    int iStackPad1;  /* unused */
    int bOtherDead;
    int bDead;
    /* matching: dead initializer; it changes the reload order of spilled variables at a join and
       with it the tail merging */
    int iX = 0;
    kgtSkill **ppStackPad2;  /* unused */
    kgtSkill **ppStackPad3;  /* unused */
    int iObj;
    int iOtherH;
    int iH;
    int iOtherObj;
    int iOtherBox;
    int iBox;
    int iY;
    kgtEngineObject *pParent;
    kgtEngineObject *pThrown;
    kgtEngineObject *pAttacker;
    kgtEngineObject *pOther;
    kgtHitboxStep *pBox;
    kgtHitboxStep *pOtherBox;
    int iPlayer;
    int bStageWalls;
    int bWallHit;
    int iPush;
    int iOtherX;
    int iOtherY;
    int iOtherW;
    int iPushObj;
    int iPushOther;

    pObj = gkgtEngineObjects;
    for (iObj = 0; iObj < 1024; iObj++, pObj++) {
        bDead = 0;
        if (pObj->iJumpIdx != READ_SCRIPT)
            continue;
        /* paused: character objects stand still (the test reads the current object's type, not pObj's) */
        if (gkgtGameState.bPaused) {
            switch (gpkgtCurrentEngineObject->iObjectType) {
            case PLAYER_ENGINE_OBJECT:
            case STORY_ENGINE_OBJECT:
            case CHARACTER_ENGINE_OBJECT:
                continue;
            }
        }
        /* move (16.16) unless in hit stop */
        if (pObj->iOpponentDowntimeInFrames == 0) {
            pObj->iXMomentum += pObj->iXGravity;
            pObj->iPosX += pObj->iXMomentum;
            pObj->iYMomentum += pObj->iYGravity;
            pObj->iPosY += pObj->iYMomentum;
        }
        /* follows its parent at (shParentOffsetX, shParentOffsetY) pixels, mirrored when the parent is flipped */
        if (pObj->iFlags & 0x20000000) {
            pParent = pObj->pParent;
            if (pParent->iPlayerLookingRight & 1)
                pObj->iPosX = pParent->iPosX - (pObj->shParentOffsetX << 16);
            else
                pObj->iPosX = (pObj->shParentOffsetX << 16) + pParent->iPosX;
            pObj->iPosY = (pObj->shParentOffsetY << 16) + pObj->pParent->iPosY;
        }
        if (pObj->iObjectType == PLAYER_ENGINE_OBJECT) {
            if (gkgtLoadedCharacter[pObj->iPlayerIdx].iHealth == 0)
                bDead = 1;
            /* apply the push-back collected last frame */
            pObj->iPosX += gkgtLoadedCharacter[pObj->iPlayerIdx].iPushBackX;
            iPlayer = pObj->iPlayerIdx;
            gkgtLoadedCharacter[iPlayer].iPushBackX = 0;
            if ((gkgtLoadedCharacter[iPlayer].iThrowFlags & 0x10)  /* throw flag 0x10 (from script command RC) */
                && (pThrown = gkgtLoadedCharacter[iPlayer].pLastOpponent) != NULL
                && pThrown->iObjectType == PLAYER_ENGINE_OBJECT) {
                /* carry the thrown opponent along */
                if (pObj->iPlayerLookingRight & 1)
                    pThrown->iPosX = pObj->iPosX - gkgtLoadedCharacter[iPlayer].iThrowOffsetX;
                else
                    pThrown->iPosX = gkgtLoadedCharacter[iPlayer].iThrowOffsetX + pObj->iPosX;
                pThrown->iPosY = gkgtLoadedCharacter[iPlayer].iThrowOffsetY + pObj->iPosY;
            }
            /* walls: the stage edges, or the screen edges in versus modes and story fights with option 4 */
            bWallHit = 0;
            bStageWalls = 0;
            switch (gkgtGameState.kgtGameMode) {
            case GAME_MODE_VS_SINGLE:
            case GAME_MODE_VS_TEAM:
                bStageWalls = 1;
                break;
            case GAME_MODE_STORY:
                if (gkgtLoadedCharacter[0].kgtStoryEntries[giCurrentStoryStep[giStoryModeSide]].cOptionsBitmask & 4)
                    bStageWalls = 1;
            }

            /* despite its name, bStageWalls 1 selects the screen edges */
            switch (bStageWalls) {
            case 0:
                /* stage edges at x 50 and 1230 pixels (0x32 / 0x4ce << 16); with a DS wall branch registered
                   the screen edges apply instead */
                if (pObj->iDsWallHitSkillIdx == 0) {
                    /* a player in a hit reaction (OBJ_FLAGS 8) pushes its attacker back by the overlap, unless the
                       attacker is itself in a hit reaction */
                    if (pObj->iPosX < 0x320000) {
                        if (!bDead && (OBJ_FLAGS(pObj) & 8)
                            && (pAttacker = (kgtEngineObject *)gkgtLoadedCharacter[iPlayer].iLastAttacker) != NULL
                            && (OBJ_FLAGS(pAttacker) & 0xc) != 8
                            && (iPush = 0x320000 - pObj->iPosX) > 0)
                            gkgtLoadedCharacter[pAttacker->iPlayerIdx].iPushBackX += iPush;
                        pObj->iPosX = 0x320000;
                        bWallHit = 1;
                    }
                    if (pObj->iPosX > 0x4ce0000) {
                        if (!bDead && (OBJ_FLAGS(pObj) & 8)
                            && (pAttacker = (kgtEngineObject *)gkgtLoadedCharacter[pObj->iPlayerIdx].iLastAttacker) != NULL
                            && (OBJ_FLAGS(pAttacker) & 0xc) != 8
                            && (iPush = pObj->iPosX - 0x4ce0000) > 0)
                            gkgtLoadedCharacter[pAttacker->iPlayerIdx].iPushBackX -= iPush;
                        pObj->iPosX = 0x4ce0000;
                        bWallHit = 1;
                    }
                    break;
                }
                /* fall through */
            case 1:
                /* screen edges: camera x + 50 and camera x + 590 pixels (the screen is 640 wide) */
                if (pObj->iPosX - giCameraX * 0x10000 < 0x320000) {
                    if (!bDead && (OBJ_FLAGS(pObj) & 8)
                        && (pAttacker = (kgtEngineObject *)gkgtLoadedCharacter[iPlayer].iLastAttacker) != NULL
                        && (OBJ_FLAGS(pAttacker) & 0xc) != 8
                        && (iPush = (0x32 - giCameraX) * 0x10000 - pObj->iPosX) > 0)
                        gkgtLoadedCharacter[pAttacker->iPlayerIdx].iPushBackX += iPush;
                    pObj->iPosX = (giCameraX + 0x32) * 0x10000;
                    bWallHit = 1;
                }
                if (pObj->iPosX - giCameraX * 0x10000 > 0x24e0000) {
                    if (!bDead && (OBJ_FLAGS(pObj) & 8)
                        && (pAttacker = (kgtEngineObject *)gkgtLoadedCharacter[pObj->iPlayerIdx].iLastAttacker) != NULL
                        && (OBJ_FLAGS(pAttacker) & 0xc) != 8
                        && (iPush = pObj->iPosX - (giCameraX + 0x24e) * 0x10000) > 0)
                        gkgtLoadedCharacter[pAttacker->iPlayerIdx].iPushBackX -= iPush;
                    pObj->iPosX = (giCameraX + 0x24e) * 0x10000;
                    bWallHit = 1;
                }
                break;
            }
            /* DS branch 4 (hits the wall) */
            if (bWallHit && pObj->iDsWallHitSkillIdx) {
                pObj->iHitJunctionIdx = pObj->iDsWallHitSkillIdx;
                pObj->iDsWallHitSkillIdx = 0;
            }
        }
        /* only players and their objects have bodies */
        if (pObj->iObjectType > STORY_ENGINE_OBJECT)
            continue;

        /* push apart overlapping bodies (guard boxes with the body flag 1), each pair once */
        for (iBox = 20; iBox-- > 0; ) {
            pBox = (kgtHitboxStep *)pObj->pGuardBoxes[iBox];
            if (!pBox || !(pBox->cFlags & 1))
                continue;
            if (pObj->iPlayerLookingRight & 1)
                iX = pObj->iPosX / 0x10000 - pBox->shX;
            else
                iX = pBox->shX + pObj->iPosX / 0x10000;
            iY = pBox->shY + pObj->iPosY / 0x10000;
            iW = pBox->shW;
            iH = pBox->shH;
            iOtherObj = iObj + 1;
            pOther = &gkgtEngineObjects[iObj + 1];
            for (; iOtherObj < 1024; iOtherObj++, pOther++) {
                bOtherDead = 0;
                if (pOther->iJumpIdx != READ_SCRIPT)
                    continue;
                if ((pOther->iLine ^ pObj->iLine) & 1)
                    continue;
                if (pOther->iObjectType == PLAYER_ENGINE_OBJECT && gkgtLoadedCharacter[pOther->iPlayerIdx].iHealth == 0)
                    bOtherDead = 1;
                for (iOtherBox = 20; iOtherBox-- > 0; ) {
                    pOtherBox = (kgtHitboxStep *)pOther->pGuardBoxes[iOtherBox];
                    if (!pOtherBox || !(pOtherBox->cFlags & 1))
                        continue;
                    if (pOther->iPlayerLookingRight & 1)
                        iOtherX = pOther->iPosX / 0x10000 - pOtherBox->shX;
                    else
                        iOtherX = pOtherBox->shX + pOther->iPosX / 0x10000;
                    iOtherY = pOtherBox->shY + pOther->iPosY / 0x10000;
                    iOtherH = pOtherBox->shH;
                    iOtherW = pOtherBox->shW;
                    /* no overlap? */
                    if (iW + iX <= iOtherX - iOtherW)
                        continue;
                    if (iX - iW >= iOtherW + iOtherX)
                        continue;
                    if (iH + iY <= iOtherY - iOtherH)
                        continue;
                    if (iY - iH >= iOtherH + iOtherY)
                        continue;
                    /* overlap width in pixels; the one on the right is pushed right */
                    if (iX > iOtherX) {
                        iPushObj = iOtherW - iX + iOtherX + iW;
                        iPushOther = -iPushObj;
                    } else {
                        iPushOther = iOtherW - iOtherX + iW + iX;
                        iPushObj = -iPushOther;
                    }
                    /* 0x4000 = a quarter pixel in 16.16: a quarter of the overlap per frame; when only one is in
                       the air, only that one moves; a knocked-out character pushes nobody */
                    if (pObj->iPosY < pObj->iGroundY) {
                        if (pOther->iPosY < pOther->iGroundY) {
                            if (!bOtherDead)
                                pObj->iPosX += iPushObj * 0x4000;
                            if (!bDead)
                                pOther->iPosX += iPushOther * 0x4000;
                        } else {
                            if (!bOtherDead)
                                pObj->iPosX += iPushObj * 0x4000;
                        }
                    } else {
                        if (pOther->iPosY < pOther->iGroundY) {
                            if (!bDead)
                                pOther->iPosX += iPushOther * 0x4000;
                        } else {
                            if (!bOtherDead)
                                pObj->iPosX += iPushObj * 0x4000;
                            if (!bDead)
                                pOther->iPosX += iPushOther * 0x4000;
                        }
                    }
                }
            }
        }
    }
}

/*
 * The hit processing of one frame, called by the main loop after the objects' scripts ran: clears the
 * characters' hit flags, resolves attack clashes (vAdjustHitboxes), attack hits (vHandleHitboxEffects)
 * and movement (vHandleHitMovements), then gives every player whose attack connected while its
 * opponent's did not (a trade does not count) its DS branch 2 (attack hits), and for a throw also
 * DS branch 6.
 * Globals: changes gkgtLoadedCharacter[] and the engine objects (through the callees).
 */
void vHitboxHandling(void)
{
    int i;
    kgt_character_struct *pChar;
    kgt_character_struct *pOppChar;
    kgtEngineObject *pSelf;
    int iHitFlags;

    /* per-frame hit results start empty */
    pChar = gkgtLoadedCharacter;
    for (i = 0; i < 8; i++, pChar++) {
        pChar->iHitFlags = 0;
        pChar->iTimesHit = 0;
    }
    vAdjustHitboxes();
    vHandleHitboxEffects();
    vHandleHitMovements();
    /* DS branches of the players whose attack connected (unless a hit junction is already pending) */
    pChar = gkgtLoadedCharacter;
    for (i = 0; i < 8; i++, pChar++) {
        if (pChar->iOnlineState == 0)
            continue;
        pSelf = pChar->pkgtoSelf;
        if (pSelf->iHitJunctionIdx != 0 || pChar->pLastOpponent == NULL)
            continue;
        pOppChar = &gkgtLoadedCharacter[pChar->pLastOpponent->iPlayerIdx];
        iHitFlags = pChar->iHitFlags;
        if (iHitFlags && pOppChar->iHitFlags == 0 && pOppChar->iTimesHit) {
            /* our attack connected and the opponent's did not */
            if (pSelf->iDsAttackHitsSkillIdx) {
                pSelf->iHitJunctionIdx = pSelf->iDsAttackHitsSkillIdx;
                pSelf->iDsAttackHitsSkillIdx = 0;
            }
            /* hit flag 2: it was a throw (DS branch 6) */
            if ((iHitFlags & 2) && pSelf->iDsThrowSkillIdx) {
                pSelf->iHitJunctionIdx = pSelf->iDsThrowSkillIdx;
                pSelf->iDsThrowSkillIdx = 0;
            }
        }
    }
}

/*
 * Does the input direction dir (input & 0xf) satisfy the direction of command input wCmd?  Command directions: 0 any, 1 neutral, 2 forward, 3 down-forward,
 * 4 down, 5 down-back, 6 back, 7 up-back, 8 up, 9 up-forward, 10-13 single bits; mirrored when the
 * player faces the other way in guard-button mode (there inputs are absolute left/right).  Expects
 * iPlayer (the player index) in the calling function; sets bOk to 1 on a match.
 */
#define MATCH_DIRECTION(wCmd, dir, bOk)                                                                               \
    if (gpkgtCurrentEngineObject->iPlayerLookingRight && (gkgtLoadedCharacter[iPlayer].iOptionFlags & 8)) {  \
        switch ((wCmd) & 0xf) {                                                                                      \
        case 0: bOk = 1; break;                                                                                      \
        case 1: if ((dir) == 0) bOk = 1; break;                                                                      \
        case 6: if ((dir) == 2) bOk = 1; break;                                                                      \
        case 5: if ((dir) == 10) bOk = 1; break;                                                                     \
        case 4: if ((dir) == 8) bOk = 1; break;                                                                      \
        case 3: if ((dir) == 9) bOk = 1; break;                                                                      \
        case 2: if ((dir) == 1) bOk = 1; break;                                                                      \
        case 9: if ((dir) == 5) bOk = 1; break;                                                                      \
        case 8: if ((dir) == 4) bOk = 1; break;                                                                      \
        case 7: if ((dir) == 6) bOk = 1; break;                                                                      \
        case 12: if ((dir) & 1) bOk = 1; break;                                                                       \
        case 11: if ((dir) & 4) bOk = 1; break;                                                                       \
        case 10: if ((dir) & 2) bOk = 1; break;                                                                       \
        case 13: if ((dir) & 8) bOk = 1; break;                                                                       \
        }                                                                                                            \
    } else {                                                                                                         \
        switch ((wCmd) & 0xf) {                                                                                      \
        case 0: bOk = 1; break;                                                                                      \
        case 1: if ((dir) == 0) bOk = 1; break;                                                                      \
        case 2: if ((dir) == 2) bOk = 1; break;                                                                      \
        case 3: if ((dir) == 10) bOk = 1; break;                                                                     \
        case 4: if ((dir) == 8) bOk = 1; break;                                                                      \
        case 5: if ((dir) == 9) bOk = 1; break;                                                                      \
        case 6: if ((dir) == 1) bOk = 1; break;                                                                      \
        case 7: if ((dir) == 5) bOk = 1; break;                                                                      \
        case 8: if ((dir) == 4) bOk = 1; break;                                                                      \
        case 9: if ((dir) == 6) bOk = 1; break;                                                                      \
        case 10: if ((dir) & 1) bOk = 1; break;                                                                       \
        case 11: if ((dir) & 4) bOk = 1; break;                                                                       \
        case 12: if ((dir) & 2) bOk = 1; break;                                                                       \
        case 13: if ((dir) & 8) bOk = 1; break;                                                                       \
        }                                                                                                            \
    }

/*
 * Looks for a command (kgt_character_struct.kgtCommands, from index iCommandIdx on) whose inputs
 * appear in the current player's input buffer, searching back from the current frame over the
 * command's shCommandTime frames and matching its inputs from the last to the first.  Input words:
 * bits 0-3 direction (see MATCH_DIRECTION), bits 4-9 buttons, 0x2000 input in use (the command ends at the last one with it),
 * bits 14-15 the kind: 0 press, 1 repeat (press timing / 4 times), 2 hold (timing frames), 3 rotation
 * (a full turn of the stick, either way).  A long command (over 29 frames) clears the buffer once
 * entered, so it cannot come out again.
 * Returns the skill to start: the command's crouching, near/far standing or air skill by the
 * player's situation, or 0 (outside the fighting phase, just before landing, or nothing entered).
 * iCommandIdx: first command to check (0, or iLastCommandIdx to find a follow-up).
 * Globals: reads gpkgtCurrentEngineObject, gkgtGameState.dwRoundPhase, giInputBufferPos; changes
 * giInputBuffer (clearing) and gkgtLoadedCharacter[].iLastCommandIdx.
 */
int iHandlePlayerCommandSequence(int iCommandIdx)
{
    int iPlayer;
    int iCmd;
    kgtCharacterCommand *pCmd;
    int iFramesLeft;
    int iPos;
    int iInputIdx;
    int bHeld;
    int iRotB;  /* rotation B (forward, down, back, up) state; the parameter iCommandIdx is reused for rotation A */
    int iCountLeft;
    int iCountLeftB;
    DWORD dwCmdInput;
    DWORD *pInputs;
    DWORD dwDir;
    int iChecks;  /* checks passed at this frame: direction + buttons, 2 = the input is there */
    int iChecks2;
    DWORD dwInput;
    DWORD dwDir2;
    int iButtons;
    WORD wButtons;
    int iHeldFrames;
    int iNeededFrames;
    int iHoldPos;
    int iSkill;
    int iClearCount;
    int iClearPos;
    kgtEngineObject *pStackPad1;  /* unused */

    iPlayer = gpkgtCurrentEngineObject->iPlayerIdx;
    if (gkgtGameState.dwRoundPhase != 1)
        return 0;
    pInputs = giInputBuffer[iPlayer];
    /* no commands while falling (y grows downwards) within 50 pixels above the ground */
    if (gpkgtCurrentEngineObject->iPosY < gpkgtCurrentEngineObject->iGroundY
        && gpkgtCurrentEngineObject->iPosY + 0x320000 > gpkgtCurrentEngineObject->iGroundY
        && gpkgtCurrentEngineObject->iYMomentum > 0)
        return 0;
    for (iCmd = iCommandIdx; iCmd < 100; iCmd++) {
        pCmd = &gkgtLoadedCharacter[iPlayer].kgtCommands[iCmd];
        /* unused command slots have no time */
        if (pCmd->shCommandTime == 0)
            continue;
        iFramesLeft = (WORD)pCmd->shCommandTime;
        /* iInputIdx: index of the input being looked for; iPos: buffer frame (1024 frames, wraps);
           iCommandIdx / iRotB: rotation states (-1 = not started) */
        iInputIdx = 9;
        iPos = giInputBufferPos;
        bHeld = 0;
        iCommandIdx = -1;
        iRotB = -1;
        /* find the last input of the command */
        while (!((WORD)pCmd->shCommandInputs[iInputIdx] & 0x2000)) {      /* end of the command */
            if (iInputIdx-- == 0)
                return 0;
        }
        /* timing * 4: repeats count down by 4 per press, rotations by 1 per quarter turn */
        iCountLeft = iCountLeftB = (WORD)pCmd->shCommandInputTimings[iInputIdx] << 2;
        /* walk back through the buffer */
        for (; iFramesLeft > 0; iPos = (iPos - 1) & 0x3ff, iFramesLeft--) {
            iChecks = 0;
            dwCmdInput = (WORD)pCmd->shCommandInputs[iInputIdx];
            dwInput = pInputs[iPos];
            dwDir = dwInput & 0xf;
            switch (dwCmdInput >> 14) {
            case 0:     /* press */
                MATCH_DIRECTION(dwCmdInput, dwDir, iChecks);
                /* buttons pressed at this very frame (down now, up the frame before), or none needed */
                iButtons = dwCmdInput & 0x3f0;
                if ((dwCmdInput & 0x3f0) == 0 || ((pInputs[iPos] & iButtons) == iButtons && !(pInputs[(iPos - 1) & 0x3ff] & iButtons)))
                    iChecks++;
                break;
            case 1:     /* repeat */
                /* every new press of the direction + buttons counts; a press must be released
                   (all buttons up) before the next one counts */
                switch (bHeld) {
                case 0:
                    iChecks = 0;
                    wButtons = dwCmdInput & 0x3f0;
                    MATCH_DIRECTION(dwCmdInput, dwDir, iChecks);
                    if ((wButtons & dwInput) == wButtons || wButtons == 0)
                        iChecks++;
                    if (iChecks > 1) {
                        bHeld = 1;
                        iCountLeft -= 4;
                        if (iCountLeft > 0)
                            continue;
                        goto matched;
                    }
                    break;
                case 1:
                    if (!(dwInput & 0x3f0))
                        bHeld = 0;
                    break;
                }
                continue;
            case 2:     /* hold */
                wButtons = dwCmdInput & 0x3f0;
                iHeldFrames = 0;
                iNeededFrames = (WORD)pCmd->shCommandInputTimings[iInputIdx];
                /* held for the input's timing frames in a row, back from this frame */
                for (iHoldPos = iPos; iHeldFrames < iNeededFrames; iHoldPos = (iHoldPos - 1) & 0x3ff, iHeldFrames++) {
                    iChecks2 = 0;
                    dwInput = pInputs[iHoldPos];
                    dwDir2 = dwInput & 0xf;
                    MATCH_DIRECTION(dwCmdInput, dwDir2, iChecks2);
                    if ((wButtons & dwInput) == wButtons || wButtons == 0)
                        iChecks2++;
                    if (iChecks2 < 2)
                        break;
                }
                if (iHeldFrames != iNeededFrames)
                    continue;
                goto matched;
            case 3:     /* rotation */
                /* first frame of a rotation: the latest direction sets the starting quarter of both directions;
                   then each state waits for the next direction back in time:
                   A (iCommandIdx): up <- forward <- down <- back <- up ...  (in time: back, down, forward, up)
                   B (iRotB, += 3 = -1 mod 4): up <- back <- down <- forward ...  (in time: forward, down, back, up)
                   with timing * 4 quarter turns needed */
                if (iCommandIdx < 0 && iRotB < 0) {
                    iCountLeft = iCountLeftB = (WORD)pCmd->shCommandInputTimings[iInputIdx] << 2;
                    switch (dwDir) {
                    case 1:
                    case 5:
                        iCommandIdx = 3;
                        break;
                    case 4:
                    case 6:
                        iCommandIdx = 0;
                        break;
                    case 8:
                    case 9:
                        iCommandIdx = 2;
                        break;
                    case 2:
                    case 10:
                        iCommandIdx = 1;
                        break;
                    }
                    iRotB = iCommandIdx;
                    continue;
                }
                switch (iCommandIdx & 3) {
                case 0:
                    if (dwDir == 2) {
                        iCommandIdx++;
                        iCountLeft--;
                    }
                    break;
                case 1:
                    if (dwDir == 8) {
                        iCommandIdx++;
                        iCountLeft--;
                    }
                    break;
                case 2:
                    if (dwDir == 1) {
                        iCommandIdx++;
                        iCountLeft--;
                    }
                    break;
                case 3:
                    if (dwDir == 4) {
                        iCommandIdx++;
                        iCountLeft--;
                    }
                    break;
                }
                /* rotation A complete */
                if (iCountLeft < 2)
                    iChecks = 2;
                switch (iRotB & 3) {
                case 2:
                    if (dwDir == 2) {
                        iRotB += 3;
                        iCountLeftB--;
                    }
                    break;
                case 3:
                    if (dwDir == 8) {
                        iRotB += 3;
                        iCountLeftB--;
                    }
                    break;
                case 0:
                    if (dwDir == 1) {
                        iRotB += 3;
                        iCountLeftB--;
                    }
                    break;
                case 1:
                    if (dwDir == 4) {
                        iRotB += 3;
                        iCountLeftB--;
                    }
                    break;
                }
                /* rotation B complete */
                if (iCountLeftB < 2)
                    goto matched;
                break;
            default:
                continue;
            }
            if (iChecks <= 1)
                continue;
matched:
            /* this input matched: go on with the previous one (or finish) */
            if (--iInputIdx == -1) {
                /* whole command entered */
                if ((WORD)pCmd->shCommandTime > 29) {
                    /* long command: clear 1004 buffer frames from 20 frames back (the negative index wraps
                       into 0..1023) */
                    iClearPos = giInputBufferPos - 20;
                    for (iClearCount = 0x3ec; iClearCount != 0; iClearCount--) {
                        if (iClearPos < 0)
                            iClearPos += (DWORD)(0x3ff - iClearPos) / 0x400 * 0x400;
                        giInputBuffer[iPlayer][iClearPos--] = 0;
                    }
                }
                gkgtLoadedCharacter[gpkgtCurrentEngineObject->iPlayerIdx].iLastCommandIdx = iCmd + 1;
                /* pick the command's skill by the situation: on the ground and still (crouching: down held;
                   standing: near / far from the nearest enemy), or in the air */
                if (gpkgtCurrentEngineObject->iPosY == gpkgtCurrentEngineObject->iGroundY && gpkgtCurrentEngineObject->iYGravity == 0 && gpkgtCurrentEngineObject->iYMomentum == 0) {
                    gpkgtCurrentEngineObject->iYGravity = 0;
                    gpkgtCurrentEngineObject->iYMomentum = 0;
                    if (giInputBuffer[iPlayer][giInputBufferPos] & 8) {
                        iSkill = (WORD)pCmd->shCrouchedCommandSet;
                        if (iSkill)
                            return iSkill;
                    } else {
                        if (gkgtLoadedCharacter[gpkgtCurrentEngineObject->iPlayerIdx].pNearestEnemy
                            && abs((gkgtLoadedCharacter[gpkgtCurrentEngineObject->iPlayerIdx].iCurrentXPos
                                    - gkgtLoadedCharacter[gkgtLoadedCharacter[gpkgtCurrentEngineObject->iPlayerIdx].pNearestEnemy->iPlayerIdx].iCurrentXPos) / 0x10000)
                               <= gkgtLoadedCharacter[gpkgtCurrentEngineObject->iPlayerIdx].shNearDistance)
                            iSkill = (WORD)pCmd->shStandNearCommandSet;
                        else
                            iSkill = (WORD)pCmd->shStandFarCommandSet;
                        if (iSkill)
                            return iSkill;
                    }
                } else if (gpkgtCurrentEngineObject->iPosY < gpkgtCurrentEngineObject->iGroundY) {
                    iSkill = (WORD)pCmd->shAirCommandSet;
                    if (iSkill)
                        return iSkill;
                }
            } else {
                /* count for the next input (not multiplied by 4 as for the last input) */
                iCountLeft = (WORD)pCmd->shCommandInputTimings[iInputIdx];
            }
        }
    }
    return 0;
}

/*
 * Script command 0x24 (command branch): was the command in the step entered within the last cTime
 * frames?  The step holds up to 5 inputs (those in use marked 0x2000), matched from the last back
 * to the first like iHandlePlayerCommandSequence does for presses; other kinds of input never match.
 * pSkill: the script step (kgtComStep).  Returns 1 if the command was entered, else 0.
 * Globals: reads gpkgtCurrentEngineObject, giInputBuffer, giInputBufferPos.
 */
int process_COM_skillblock(kgtSkill *pSkill)
{
    kgtComStep *pStep = (kgtComStep *)pSkill;
    int iPlayer;
    DWORD *pInputs;
    int iFramesLeft;
    int iPos;
    int iInputIdx;
    WORD *pInput;
    DWORD dwDir;
    DWORD dwCmdInput;
    int iChecks;
    int iButtons;

    iPlayer = gpkgtCurrentEngineObject->iPlayerIdx;
    pInputs = giInputBuffer[iPlayer];
    if (pStep->cTime) {
        iFramesLeft = pStep->cTime;
        iPos = giInputBufferPos;
        /* find the last input of the command */
        iInputIdx = 4;
        while (!(pStep->wInputs[iInputIdx] & 0x2000)) {
            if (iInputIdx-- == 0)
                return 0;
        }
        if (iFramesLeft > 0) {
            pInput = &pStep->wInputs[iInputIdx];
            /* walk back through the buffer (1024 frames, wraps) */
            for (; iFramesLeft > 0; iPos = (iPos - 1) & 0x3ff, iFramesLeft--) {
                dwCmdInput = *pInput;
                iChecks = 0;
                dwDir = pInputs[iPos] & 0xf;
                /* presses only: direction and buttons newly pressed at this frame */
                if (!(dwCmdInput >> 14)) {
                    MATCH_DIRECTION(dwCmdInput, dwDir, iChecks);
                    iButtons = dwCmdInput & 0x3f0;
                    if ((dwCmdInput & 0x3f0) == 0 || ((pInputs[iPos] & iButtons) == iButtons && !(pInputs[(iPos - 1) & 0x3ff] & iButtons)))
                        iChecks++;
                    if (iChecks > 1) {
                        iInputIdx--;
                        pInput--;
                        if (iInputIdx == -1)
                            return 1;
                    }
                }
            }
        }
    }
    return 0;
}

/*
 * Starts skill iSkillIdx on the current object at its first script step and clears its boxes.
 * Returns 0, or 1 if iSkillIdx is 0 (then only the boxes are cleared).
 * Globals: reads gpkgtCurrentEngineObject and the owner's skill table.
 */
int iSwitchCurrentObjectSkill(int iSkillIdx)
{
    kgt_character_struct *pChar;

    vMemzeroHitboxArrays(gpkgtCurrentEngineObject);
    if (iSkillIdx) {
        pChar = gkgtLoadedCharacter;
        pChar += gpkgtCurrentEngineObject->iPlayerIdx;
        gpkgtCurrentEngineObject->iSkillIdx = iSkillIdx;
        gpkgtCurrentEngineObject->iSkillScriptIdx = (WORD)pChar->kgtCore.pSkillsAlloc[iSkillIdx].shStartingStepIdx;
        return 0;
    }
    return 1;
}

/*
 * Switches the current object to skill iSkillIdx unless it already runs it (callers set iSkillIdx to
 * -1 first to force a restart): new script position, no boxes, no reaction step, no DS branches, no
 * image wait; a player standing still on the ground also stops, and forgets SC returns and SF loops.
 * Globals: changes gpkgtCurrentEngineObject.
 */
void vAssignSkillAndResetOtherValues(int iSkillIdx)
{
    kgtEngineObject *pObj;

    if (iSkillIdx == gpkgtCurrentEngineObject->iSkillIdx)
        return;
    iSwitchCurrentObjectSkill(iSkillIdx);
    vResetReactionSkillBlock(gpkgtCurrentEngineObject);
    iResetDsSkillIndices();
    gpkgtCurrentEngineObject->iImageWaitFrames = 0;
    if (gpkgtCurrentEngineObject->iObjectType == PLAYER_ENGINE_OBJECT) {
        pObj = gpkgtCurrentEngineObject;
        /* on the ground and not moving vertically: stop */
        if (pObj->iPosY == pObj->iGroundY && pObj->iYMomentum == 0) {
            pObj->iYGravity = 0;
            pObj->iYMomentum = 0;
            pObj->iXGravity = 0;
            pObj->iXMomentum = 0;
        }
        pObj->iReturnSkillIdx = 0;
        pObj->cLoopCount = 0;
    }
}

/*
 * The built-in movement of a player object that is free (no action running): by its stance
 * (OBJ_FLAGS bits 0-1) and the input it starts the win/lose/draw pose, turning, falling, crouching,
 * jumping (forward/back/up), guarding (guard button), walking or standing skills, and handles the
 * line-switch button.  In guard-button mode (iOptionFlags & 8) left/right are absolute and also turn
 * the player.
 * Returns 0 (a few paths return no value; the callers ignore it).
 * Globals: reads gpkgtCurrentEngineObject, giInputBuffer, giHitJudge[1] (line switch enabled).
 */
int vHandleMovementSkills(void)
{
    int iPlayer;
    DWORD dwInput;
    int bGuardButton;
    kgt_character_struct *pChar;
    int iStackPad1;  /* unused */

    iPlayer = gpkgtCurrentEngineObject->iPlayerIdx;
    pChar = &gkgtLoadedCharacter[iPlayer];
    dwInput = giInputBuffer[iPlayer][giInputBufferPos];
    bGuardButton = pChar->iOptionFlags & 8;
    switch (OBJ_FLAGS(gpkgtCurrentEngineObject) & 3) {
    case 0:     /* standing */
        /* round over: win, lose or draw pose, as an action (OBJ_FLAGS 4) */
        if (pChar->iRoundResult > 0) {
            switch (pChar->iRoundResult) {
            case 1:
                gpkgtCurrentEngineObject->iSkillIdx = -1;
                vAssignSkillAndResetOtherValues((WORD)pChar->shSkillIdxVictory);
                OBJ_FLAGS(gpkgtCurrentEngineObject) = (OBJ_FLAGS(gpkgtCurrentEngineObject) & ~8) | 4;
                return;
            case 2:
                gpkgtCurrentEngineObject->iSkillIdx = -1;
                vAssignSkillAndResetOtherValues((WORD)pChar->shSkillIdxLoss);
                OBJ_FLAGS(gpkgtCurrentEngineObject) = (OBJ_FLAGS(gpkgtCurrentEngineObject) & ~8) | 4;
                return;
            case 3:
                gpkgtCurrentEngineObject->iSkillIdx = -1;
                vAssignSkillAndResetOtherValues((WORD)pChar->shSkillIdxDraw);
                OBJ_FLAGS(gpkgtCurrentEngineObject) = (OBJ_FLAGS(gpkgtCurrentEngineObject) & ~8) | 4;
                return;
            }
        }
        /* turn towards the enemy (automatic turning only outside guard-button mode) */
        if (!bGuardButton && iAssignPlayerLookingRight())
            vAssignSkillAndResetOtherValues((WORD)pChar->shSkillIdxTurnStanding);
        if (gpkgtCurrentEngineObject->iPosY < gpkgtCurrentEngineObject->iGroundY) {
            /* no ground below: fall (stance 2) */
            vAssignSkillAndResetOtherValues((WORD)pChar->shSkillIdxFalling);
            OBJ_FLAGS(gpkgtCurrentEngineObject) = (OBJ_FLAGS(gpkgtCurrentEngineObject) & ~1) | 2;
            return 0;
        }
        /* down: crouch (stance 1) */
        if (dwInput & 8) {
            vAssignSkillAndResetOtherValues((WORD)pChar->shSkillIdxMidCrouch);
            OBJ_FLAGS(gpkgtCurrentEngineObject) = (OBJ_FLAGS(gpkgtCurrentEngineObject) & ~2) | 1;
            return 0;
        }
        /* up: jump forward, back or straight up (stance 2) */
        if (dwInput & 4) {
            if (dwInput & 2) {
                vAssignSkillAndResetOtherValues((WORD)pChar->shSkillIdxFrontJump);
                if (bGuardButton)
                    gpkgtCurrentEngineObject->iPlayerLookingRight = 0;
            } else if (dwInput & 1) {
                if (bGuardButton) {
                    vAssignSkillAndResetOtherValues((WORD)pChar->shSkillIdxFrontJump);
                    gpkgtCurrentEngineObject->iPlayerLookingRight = 1;
                } else {
                    vAssignSkillAndResetOtherValues((WORD)pChar->shSkillIdxBackJump);
                }
            } else {
                vAssignSkillAndResetOtherValues((WORD)pChar->shSkillIdxJumpUp);
            }
            OBJ_FLAGS(gpkgtCurrentEngineObject) = (OBJ_FLAGS(gpkgtCurrentEngineObject) & ~1) | 2;
            return 0;
        }
        /* guard button, walking (in guard-button mode the direction also sets the facing), or standing */
        if ((pChar->iOptionFlags & 8) && (dwInput & (1 << (pChar->cGuardButton + 4)))) {
            vAssignSkillAndResetOtherValues((WORD)pChar->shSkillIdxButtonGuardStand);
        } else if (dwInput & 2) {
            if (bGuardButton) {
                if (gpkgtCurrentEngineObject->iPlayerLookingRight)
                    gpkgtCurrentEngineObject->iSkillIdx = -1;
                gpkgtCurrentEngineObject->iPlayerLookingRight = 0;
            }
            vAssignSkillAndResetOtherValues((WORD)pChar->shSkillIdxForward);
        } else if (dwInput & 1) {
            if (bGuardButton) {
                if (gpkgtCurrentEngineObject->iPlayerLookingRight != 1)
                    gpkgtCurrentEngineObject->iSkillIdx = -1;
                gpkgtCurrentEngineObject->iPlayerLookingRight = 1;
                vAssignSkillAndResetOtherValues((WORD)pChar->shSkillIdxForward);
            } else {
                vAssignSkillAndResetOtherValues((WORD)pChar->shSkillIdxBackward);
            }
        } else if (gpkgtCurrentEngineObject->iSkillIdx != (WORD)pChar->shSkillIdxStandFromCrouch
                   && gpkgtCurrentEngineObject->iSkillIdx != (WORD)pChar->shSkillIdxTurnStanding) {
            vAssignSkillAndResetOtherValues((WORD)pChar->shSkillIdxStanding);
        }
        /* layer switch button */
        if (giHitJudge[1] && (dwInput & 0x100) && !(gpkgtCurrentEngineObject->iLine & 2))
            gpkgtCurrentEngineObject->iLine = (gpkgtCurrentEngineObject->iLine ^ 1) | 2;
        break;
    case 1:     /* crouching */
        /* turn while crouched */
        if (!bGuardButton && iAssignPlayerLookingRight())
            vAssignSkillAndResetOtherValues((WORD)pChar->shSkillIdxTurnCrouching);
        if (gpkgtCurrentEngineObject->iPosY < gpkgtCurrentEngineObject->iGroundY) {
            OBJ_FLAGS(gpkgtCurrentEngineObject) = (OBJ_FLAGS(gpkgtCurrentEngineObject) & ~1) | 2;
            vAssignSkillAndResetOtherValues((WORD)pChar->shSkillIdxFalling);
            return 0;
        }
        /* still down: crouch walk, guard or keep crouching; otherwise stand up */
        if (dwInput & 8) {
            if ((dwInput & 2) && pChar->iHasCrouchAdvance) {
                vAssignSkillAndResetOtherValues((WORD)pChar->shSkillIdxCrouchAdvance);
                if (bGuardButton)
                    gpkgtCurrentEngineObject->iPlayerLookingRight = 0;
                return 0;
            }
            if ((dwInput & 1) && pChar->iHasCrouchRetreat && bGuardButton) {
                vAssignSkillAndResetOtherValues((WORD)pChar->shSkillIdxCrouchAdvance);
                gpkgtCurrentEngineObject->iPlayerLookingRight = 1;
                return 0;
            }
            if ((dwInput & 1) && pChar->iHasCrouchRetreat && !bGuardButton) {
                vAssignSkillAndResetOtherValues((WORD)pChar->shSkillIdxCrouchRetreat);
                return 0;
            }
            if (gpkgtCurrentEngineObject->iSkillIdx == (WORD)pChar->shSkillIdxMidCrouch
                || gpkgtCurrentEngineObject->iSkillIdx == (WORD)pChar->shSkillIdxTurnCrouching)
                break;
            if ((pChar->iOptionFlags & 8) && (dwInput & (1 << (pChar->cGuardButton + 4)))) {
                vAssignSkillAndResetOtherValues((WORD)pChar->shSkillIdxButtonGuardCrouch);
                return 0;
            }
            vAssignSkillAndResetOtherValues((WORD)pChar->shSkillIdxCrouching);
            return 0;
        } else {
            OBJ_FLAGS(gpkgtCurrentEngineObject) &= ~3;
            vAssignSkillAndResetOtherValues((WORD)pChar->shSkillIdxStandFromCrouch);
            return 0;
        }
    case 2:     /* in the air */
        /* air guard with the guard button (option 2) */
        if ((pChar->iOptionFlags & 2) && bGuardButton && (dwInput & (1 << (pChar->cGuardButton + 4)))) {
            vAssignSkillAndResetOtherValues((WORD)pChar->shSkillIdxButtonGuardAir);
            return 0;
        }
        break;
    }
    return 0;
}

/*
 * Second half of battle.c (0x411270-0x413d40), once a separate file battle_b.c:
 * CPU opponents, turning inputs into skills, and the skill script interpreter.  It repeats the
 * declarations it needs (the duplicates are harmless).
 */

extern char gAfterImageTrailsBase[];        /* 0x447930: one element before gAfterImageTrails (inside gkgtLoadedStage): the trail code indexes from here */

/* ---- declarations not (yet) in globals.h / protos.h (shared with battle.c's first half) ---- */
extern int giObjectLayers[2];                      /* 0x41f130: player depth by line (kgtEngineObject.iLine & 1) */
extern int giCpuDirTableA[16];                     /* 0x41f138: CPU command direction -> input bits */
extern int giCpuDirTableB[16];                     /* 0x41f178: the same, mirrored */
extern kgtEngineObject *gpkgtCurrentEngineObject;  /* 0x4cfa00: object whose handler is running */
extern kgtEngineObject gkgtEngineObjects[1024];    /* 0x4701e0: all engine objects */
extern kgtGameState gkgtGameState;                 /* 0x470020: state of the current game */
extern unk_0x650_struct gAfterImageTrails[100];    /* 0x447f80: after-image trails (script command AI), kgtEngineObject.cAfterImageIdx - 1 */
extern int giInputBufferPos;                       /* 0x447ee0: current frame in giInputBuffer (0-1023) */
extern int giStoryModeSide;                        /* 0x424f24: copy of giStoryModePlayerIdx taken when the story character is chosen; indexes giCurrentStoryStep */
extern int giCurrentStoryStep[3];                  /* 0x424f28: story entry per side */
extern int giCameraX;                              /* 0x447f2c: camera x (pixels) */
extern int giCameraY;                              /* 0x447f30: camera y (pixels) */

kgtEngineObject *kgtoNewEngineObject(kgtJumptableEndpoints iJumpIdx, int iDepth, int iPosX, int iPosY);
void vAddToSpecialGauge(int iPlayerIdx, int iAdd);
void vAddToHealth(kgt_character_struct *pChar, int iLifeAdd);
void vDeleteCurrentEngineObject(void);
void iResetDsSkillIndices(void);

void vResetReactionSkillBlock(kgtEngineObject *pObj);
void vMemzeroHitboxArrays(kgtEngineObject *pObj);
int iAssignPlayerLookingRight(void);
void vFindNearestEnemyPlayer(void);
int iHandlePlayerCommandSequence(int iCommandIdx);
int iSwitchCurrentObjectSkill(int iSkillIdx);
void vAssignSkillAndResetOtherValues(int iSkillIdx);
int vHandleMovementSkills(void);
void vHandleCpuCommands(void);
void vProcessInputsIntoSkills(void);
void vCheckIfDrawThenMove(void);
void vjmpReadScript(void);

/* ------------------------------------------------------------------------------------------ */

/*
 * Produces the input of a CPU-controlled player (kgt_character_struct.bCpuControlled) in its input
 * buffer, by iCpuMode:
 *   1 (normal CPU): the whole buffer is cleared every frame.  While fighting, every iCpuCommandTimer
 *     frames (random, shorter at higher iCpuLevel) a CPU command is chosen: the last of the 100
 *     kgtCpuCommands that passes its probability, its distance range and its own/enemy air-ground
 *     conditions.  Its steps are then entered one by one, each after wTiming plus a random delay; a
 *     step naming a command writes that command's inputs backwards from the current frame (the way
 *     iHandlePlayerCommandSequence reads them), a step without one only writes its direction;
 *   2: copies the other player's (iPlayerIdx ^ 1) input of this frame;
 *   3: nothing;  4: holds up (jumps) while fighting.
 * Without an enemy the buffer is just cleared.
 * Globals: reads gpkgtCurrentEngineObject, gkgtGameState.dwRoundPhase, giInputBufferPos,
 * giCpuDirTableA/B; changes giInputBuffer and the character's CPU state (iCpuCommandIdx,
 * iCpuStepIdx, iCpuStepDelay, iCpuCommandTimer).
 */
void vHandleCpuCommands(void)
{
    int iPlayerIdx;
    kgt_character_struct *pChar;
    kgt_character_struct *pOppChar;
    int iOppPlayer;
    kgtCpuCommand *pCmd;
    kgtCharacterCPUCommandSkillFull *pStep;
    kgtCharacterCommand *pCommand;
    int *pDirTable;
    int iCmd, j, iInputIdx;
    int iLevel, iDist;
    int iPos;
    int iCmdInput;
    int iRepeat;
    DWORD dwInput;
    char szDebug[256];  /* debug text of the current step (built but never shown) */

    iPlayerIdx = gpkgtCurrentEngineObject->iPlayerIdx;
    pChar = &gkgtLoadedCharacter[iPlayerIdx];
    if (!pChar->bCpuControlled)
        return;
    /* no enemy: no input */
    if (pChar->pNearestEnemy == NULL) {
        vMemzero(giInputBuffer[iPlayerIdx], sizeof(giInputBuffer[0]));
        return;
    }
    iOppPlayer = pChar->pNearestEnemy->iPlayerIdx;
    pOppChar = &gkgtLoadedCharacter[iOppPlayer];

    switch (pChar->iCpuMode) {
    case 1:  /* normal CPU */
        vMemzero(giInputBuffer[iPlayerIdx], sizeof(giInputBuffer[0]));
        if (gkgtGameState.dwRoundPhase != 1)
            return;
        if (pChar->iCpuCommandIdx == 0) {
            /* pick a new command */
            if (pChar->iCpuLevel == 0)
                return;
            if (--pChar->iCpuCommandTimer > 0)
                return;
            iLevel = pChar->iCpuLevel;
            /* next choice in 50 - level + rand(0 .. 100 - level) frames */
            pChar->iCpuCommandTimer = rand() % (101 - iLevel) - iLevel + 50;
            for (iCmd = 0, pCmd = pChar->kgtCpuCommands; iCmd < 100; iCmd++, pCmd++) {
                iDist = abs(pChar->iCurrentXPos - pOppChar->iCurrentXPos) / 0x10000;
                /* probability in percent; distance in pixels between the characters */
                if (rand() % 100 < pCmd->cProbability
                    && pCmd->wIntervalMin <= iDist && pCmd->wIntervalMax >= iDist) {
                    /* bit 0: only while in the air (else only on the ground); bit 1: the same for the enemy */
                    if (pCmd->cIsFighterAirborneBitmask & 1) {
                        if (pChar->iCurrentYPos == pChar->pkgtoSelf->iGroundY)
                            continue;
                    } else {
                        if (pChar->iCurrentYPos < pChar->pkgtoSelf->iGroundY)
                            continue;
                    }
                    if (pCmd->cIsFighterAirborneBitmask & 2) {
                        if (pOppChar->iCurrentYPos == pOppChar->pkgtoSelf->iGroundY)
                            continue;
                    } else {
                        if (pOppChar->iCurrentYPos < pOppChar->pkgtoSelf->iGroundY)
                            continue;
                    }
                    pChar->iCpuCommandIdx = iCmd + 1;
                    pChar->iCpuStepDelay = 0;
                    pChar->iCpuStepIdx = -1;  /* a later command that passes replaces it: the last one wins */
                }
            }
        }
        if (pChar->iCpuCommandIdx != 0) {
            pCmd = &pChar->kgtCpuCommands[pChar->iCpuCommandIdx - 1];
            if (--pChar->iCpuStepDelay < 0) {
                /* next step of the command */
                /* past the last step, or a step not in use (its direction word lacks 0x2000): the command is done
                   (the tests read kgtSteps[10] when the index reaches 10, as the original does) */
                if (++pChar->iCpuStepIdx >= 10)
                    pChar->iCpuCommandIdx = 0;
                if (!(*(WORD *)&pCmd->kgtSteps[pChar->iCpuStepIdx].cDirection & 0x2000))
                    pChar->iCpuCommandIdx = 0;
                /* wait the step's timing plus a random delay that shrinks with the level */
                pChar->iCpuStepDelay = rand() % (101 - pChar->iCpuLevel) + pCmd->kgtSteps[pChar->iCpuStepIdx].wTiming;
                if (pChar->iCpuCommandIdx != 0)
                    sprintf(szDebug, "%s %d", pChar->kgtCommands[pChar->kgtCpuCommands[pChar->iCpuCommandIdx - 1].kgtSteps[pChar->iCpuStepIdx].wCommandIdx - 1].szName, pChar->iCpuStepDelay);
            }
        }
        if (pChar->iCpuCommandIdx == 0)
            return;
        iPos = giInputBufferPos;
        pStep = &pChar->kgtCpuCommands[pChar->iCpuCommandIdx - 1].kgtSteps[pChar->iCpuStepIdx];

        /* direction table: CPU direction -> input bits, mirrored in guard-button mode when the enemy is
           to the left */
        if ((pChar->iOptionFlags & 8) && pChar->iCurrentXPos > pOppChar->iCurrentXPos)
            pDirTable = giCpuDirTableB;
        else
            pDirTable = giCpuDirTableA;

        if (pStep->wCommandIdx == 0) {
            giInputBuffer[gpkgtCurrentEngineObject->iPlayerIdx][iPos] = pDirTable[pStep->cDirection & 0xf];
            return;
        }
        /* feed the command's inputs into the buffer, newest first */
        pCommand = &pChar->kgtCommands[pStep->wCommandIdx - 1];
        for (iInputIdx = 9; iInputIdx >= 0; iInputIdx--) {
            /* command input word: bits 0-3 direction, 4-9 buttons, 0x2000 used, 14-15 kind */
            iCmdInput = (WORD)pCommand->shCommandInputs[iInputIdx];
            iRepeat = (WORD)pCommand->shCommandInputTimings[iInputIdx];
            if (!(iCmdInput & 0x2000))
                continue;
            /* direction bits, the buttons one by one, plus the step's own direction */
            dwInput = pDirTable[iCmdInput & 0xf];
            if (iCmdInput & 0x10) dwInput |= 0x10;
            if (iCmdInput & 0x20) dwInput |= 0x20;
            if (iCmdInput & 0x40) dwInput |= 0x40;
            if (iCmdInput & 0x80) dwInput |= 0x80;
            if (iCmdInput & 0x100) dwInput |= 0x100;
            if (iCmdInput & 0x200) dwInput |= 0x200;
            dwInput |= pDirTable[pStep->cDirection & 0xf];
            /* written backwards from the current frame (iPos wraps at 1024): press = 1 frame; repeat = released
               and pressed frames, iRepeat times; hold = iRepeat frames; rotation = iRepeat turns of four
               directions */
            switch ((iCmdInput >> 14) & 3) {
            case 0:
                giInputBuffer[gpkgtCurrentEngineObject->iPlayerIdx][iPos--] = dwInput;
                if (iPos < 0) iPos = 0x3ff;
                break;
            case 1:
                for (j = 0; j < iRepeat; j++) {
                    giInputBuffer[gpkgtCurrentEngineObject->iPlayerIdx][iPos--] = 0;
                    if (iPos < 0) iPos = 0x3ff;
                    giInputBuffer[gpkgtCurrentEngineObject->iPlayerIdx][iPos--] = dwInput;
                    if (iPos < 0) iPos = 0x3ff;
                }
                break;
            case 2:
                for (j = 0; j < iRepeat; j++) {
                    giInputBuffer[gpkgtCurrentEngineObject->iPlayerIdx][iPos--] = dwInput;
                    if (iPos < 0) iPos = 0x3ff;
                }
                break;
            case 3:
                for (j = 0; j < iRepeat; j++) {
                    giInputBuffer[gpkgtCurrentEngineObject->iPlayerIdx][iPos--] = dwInput | 2;
                    if (iPos < 0) iPos = 0x3ff;
                    giInputBuffer[gpkgtCurrentEngineObject->iPlayerIdx][iPos--] = dwInput | 8;
                    if (iPos < 0) iPos = 0x3ff;
                    giInputBuffer[gpkgtCurrentEngineObject->iPlayerIdx][iPos--] = dwInput | 1;
                    if (iPos < 0) iPos = 0x3ff;
                    giInputBuffer[gpkgtCurrentEngineObject->iPlayerIdx][iPos--] = dwInput | 4;
                    if (iPos < 0) iPos = 0x3ff;
                }
                break;
            }
        }
        return;
    case 2:
        /* copy the other player's input (players 0/1, 2/3, ...) */
        giInputBuffer[iPlayerIdx][giInputBufferPos] = giInputBuffer[iPlayerIdx ^ 1][giInputBufferPos];
        return;
    case 3:
        return;
    case 4:
        if (gkgtGameState.dwRoundPhase == 1)
            giInputBuffer[iPlayerIdx][giInputBufferPos] = 4;  /* up */
        break;
    }
}

/*
 * Per-frame control of a player object (called from vjmpReadScript): after a won round it plays the
 * victory pose once it stands still; otherwise it runs the CPU, handles landing, and then, by the
 * action state (iStateFlags bits 2-3): free (0) -> a command skill or the built-in movement; in a hit
 * reaction (8) -> nothing; in an action (4) or guarding (0xc) -> a command may cancel the action if the
 * cancel condition set by script command C allows it:
 *   cCancelFlags bits 0-2: 1 = only once the action's attack connected or clashed (cancel state 2),
 *     2 = always, other = never;
 *   bits 3-5: 0 = skills whose level (byte 2 of their first step) is within cCancelLevelMin..Max,
 *     1 = only wCancelSkillIdx, other = none.
 * Globals: reads gkgtGameState.dwRoundPhase; changes gpkgtCurrentEngineObject and the character.
 */
void vProcessInputsIntoSkills(void)
{
    int iPlayerIdx;
    kgt_character_struct *pChar;
    int iSkillIdx;
    kgtSkill *pStep;

    iPlayerIdx = gpkgtCurrentEngineObject->iPlayerIdx;
    pChar = &gkgtLoadedCharacter[iPlayerIdx];
    if (gkgtGameState.dwRoundPhase == 2 && !(gpkgtCurrentEngineObject->iStateFlags & 0xc)
        && gpkgtCurrentEngineObject->iPosY == gpkgtCurrentEngineObject->iGroundY
        && gpkgtCurrentEngineObject->iYMomentum == 0 && gkgtLoadedCharacter[iPlayerIdx].iHealth != 0) {
        /* round won: play the victory pose once standing */
        vAssignSkillAndResetOtherValues((WORD)gkgtLoadedCharacter[iPlayerIdx].shSkillIdxVictory);
        return;
    }
    /* CPU players get their input for this frame */
    vHandleCpuCommands();

    if (gpkgtCurrentEngineObject->iPosY >= gpkgtCurrentEngineObject->iGroundY && gpkgtCurrentEngineObject->iYMomentum > 0) {
        /* landed (y grows downwards: at or below the ground while moving down): stop, stand, and take the
           DS landing branch (branch 1, (step << 16) | skill) or turn to the enemy and become free */
        gpkgtCurrentEngineObject->iPosY = gpkgtCurrentEngineObject->iGroundY;
        gpkgtCurrentEngineObject->iXGravity = 0;
        gpkgtCurrentEngineObject->iXMomentum = 0;
        gpkgtCurrentEngineObject->iYGravity = 0;
        gpkgtCurrentEngineObject->iYMomentum = 0;
        gpkgtCurrentEngineObject->iStateFlags &= ~3;
        gpkgtCurrentEngineObject->iSkillIdx = -1;
        if (gpkgtCurrentEngineObject->iDsLandingSkillIdx != 0) {
            gpkgtCurrentEngineObject->iSkillIdx = gpkgtCurrentEngineObject->iDsLandingSkillIdx & 0xffff;
            gpkgtCurrentEngineObject->iSkillScriptIdx = (WORD)gkgtLoadedCharacter[gpkgtCurrentEngineObject->iPlayerIdx].kgtCore.pSkillsAlloc[gpkgtCurrentEngineObject->iSkillIdx].shStartingStepIdx + ((DWORD)gpkgtCurrentEngineObject->iDsLandingSkillIdx >> 16);
            gpkgtCurrentEngineObject->iDsLandingSkillIdx = 0;
            gpkgtCurrentEngineObject->iStateFlags = (gpkgtCurrentEngineObject->iStateFlags & ~8) | 4;
            gpkgtCurrentEngineObject->iImageWaitFrames = 0;
            vMemzeroHitboxArrays(gpkgtCurrentEngineObject);
        } else {
            iAssignPlayerLookingRight();
            gpkgtCurrentEngineObject->iStateFlags &= ~0x1c;
        }
    }

    switch (gpkgtCurrentEngineObject->iStateFlags & 0xc) {  /* action state */
    case 0:
        /* free: a command entered starts its skill as an action (4); on the ground it stops */
        iSkillIdx = iHandlePlayerCommandSequence(0);
        if (iSkillIdx != 0) {
            vAssignSkillAndResetOtherValues(iSkillIdx);
            switch (gpkgtCurrentEngineObject->iStateFlags & 3) {
            case 0:
            case 1:
                gpkgtCurrentEngineObject->iXGravity = 0;
                gpkgtCurrentEngineObject->iXMomentum = 0;
                break;
            }
            gpkgtCurrentEngineObject->iStateFlags = (gpkgtCurrentEngineObject->iStateFlags & ~8) | 4;
            return;
        }
        /* no command: the built-in movement (both arms make the same call; the comparison has no effect) */
        if (gpkgtCurrentEngineObject->iSkillScriptIdx == (WORD)pChar->kgtCore.pSkillsAlloc[gpkgtCurrentEngineObject->iSkillIdx + 2].shStartingStepIdx)
            vHandleMovementSkills();
        else
            vHandleMovementSkills();
        break;
    case 8:  /* in a hit reaction */
        break;
    default:
        /* in an action: may it be cancelled into a command skill? */
        switch (gkgtLoadedCharacter[iPlayerIdx].cCancelFlags & 7) {
        case 1:
            if (gkgtLoadedCharacter[iPlayerIdx].iCurrentActionCancellableFlag <= 1)
                return;
        case 2:
            break;
        default:
            return;
        }
        /* may the command's skill cancel the action? */
        iSkillIdx = iHandlePlayerCommandSequence(0);
        if (iSkillIdx == 0)
            return;
        pStep = &gkgtLoadedCharacter[iPlayerIdx].kgtCore.pSkillScriptsAlloc[(WORD)gkgtLoadedCharacter[iPlayerIdx].kgtCore.pSkillsAlloc[iSkillIdx].shStartingStepIdx];
        switch ((gkgtLoadedCharacter[iPlayerIdx].cCancelFlags >> 3) & 7) {
        case 0:
            /* any skill whose level is within the range */
            if (((BYTE *)pStep)[2] < gkgtLoadedCharacter[iPlayerIdx].cCancelLevelMin || ((BYTE *)pStep)[2] > gkgtLoadedCharacter[iPlayerIdx].cCancelLevelMax)
                return;
            break;
        case 1:
            /* only one given skill */
            if (gkgtLoadedCharacter[iPlayerIdx].wCancelSkillIdx == 0)
                return;
            if (iSkillIdx != gkgtLoadedCharacter[iPlayerIdx].wCancelSkillIdx)
                return;
            break;
        default:
            return;
        }
        /* cancel: the new skill may hit again */
        gpkgtCurrentEngineObject->iStateFlags &= ~0x10;
        vAssignSkillAndResetOtherValues(iSkillIdx);
        pChar->iCurrentActionCancellableFlag = 0;
        break;
    }
}

/*
 * Called when a player's skill script ends: forgets the reaction step, the DS branches and the last
 * attacker; a decided round starts the win/lose/draw pose.  Otherwise the player becomes free (and,
 * unless it is switching lines, loses its stance and cancel condition): on the ground it stops, turns
 * to the enemy and crouches if down is held; in the air it starts the falling skill; then
 * vProcessInputsIntoSkills picks what comes next.
 * Globals: reads gpkgtCurrentEngineObject, giInputBuffer, giInputBufferPos.
 */
void vCheckIfDrawThenMove(void)
{
    kgt_character_struct *pChar;
    kgtEngineObject *pObj;
    DWORD dwInput;
    int iPlayerIdx;

    iPlayerIdx = gpkgtCurrentEngineObject->iPlayerIdx;
    pChar = &gkgtLoadedCharacter[iPlayerIdx];
    dwInput = giInputBuffer[iPlayerIdx][giInputBufferPos];
    vResetReactionSkillBlock(gpkgtCurrentEngineObject);
    iResetDsSkillIndices();
    pChar->iLastAttacker = 0;
    /* 1 won, 2 lost, 3 draw: the pose skill as an action (OBJ_FLAGS 4) */
    switch (pChar->iRoundResult) {
    case 1:
        gpkgtCurrentEngineObject->iSkillIdx = -1;
        vAssignSkillAndResetOtherValues((WORD)pChar->shSkillIdxVictory);
        gpkgtCurrentEngineObject->iStateFlags = (gpkgtCurrentEngineObject->iStateFlags & ~8) | 4;
        return;

    case 2:
        gpkgtCurrentEngineObject->iSkillIdx = -1;
        vAssignSkillAndResetOtherValues((WORD)pChar->shSkillIdxLoss);
        gpkgtCurrentEngineObject->iStateFlags = (gpkgtCurrentEngineObject->iStateFlags & ~8) | 4;
        return;

    case 3:
        gpkgtCurrentEngineObject->iSkillIdx = -1;
        vAssignSkillAndResetOtherValues((WORD)pChar->shSkillIdxDraw);
        gpkgtCurrentEngineObject->iStateFlags = (gpkgtCurrentEngineObject->iStateFlags & ~8) | 4;
        return;

    default:
        gpkgtCurrentEngineObject->iStateFlags &= ~0xc;  /* free */
        if (gpkgtCurrentEngineObject->iLine & 2)
            return;
        gpkgtCurrentEngineObject->iStateFlags &= ~3;
        vMemzero(&pChar->cCancelType, 6);  /* no cancel condition (script command C block) */
        pObj = gpkgtCurrentEngineObject;
        if (pObj->iPosY == pObj->iGroundY && pObj->iYMomentum == 0) {
            pObj->iXGravity = 0;
            pObj->iXMomentum = 0;
            iAssignPlayerLookingRight();
            if (dwInput & 8) {
                gpkgtCurrentEngineObject->iStateFlags |= 1;
                vProcessInputsIntoSkills();
                return;
            }
        } else {
            gpkgtCurrentEngineObject->iStateFlags |= 2;
            iSwitchCurrentObjectSkill((WORD)pChar->shSkillIdxFalling);
        }
        vProcessInputsIntoSkills();
        return;
    }
}


/* One 16-byte skill script step, viewed per command type (member comments: command code and name).
   Every step starts with its command code; skill/step pairs name a branch target (step 1 = the
   skill's first step).  kgtHitboxStep, kgtReactionStep and kgtComStep above are other views. */
#pragma pack(push, 1)
typedef union kgtScriptStep {
    BYTE cBytes[16];
    struct { BYTE cType; short shXGravity; short shXMomentum; short shYMomentum; short shYGravity; BYTE cFlags; } move;   /* 01 M: movement in script units; cFlags 1 adds, 2 / 4 / 8 / 0x10 keep x momentum / y momentum / x gravity / y gravity */
    struct { BYTE cType; BYTE cCondition; WORD wSkill; BYTE cStep; } ds;                                                 /* 02 DS: register a branch taken on an event (cCondition 1-6) */
    struct { BYTE cType; BYTE cUnk01; WORD wSound; } sound;                                                              /* 03 S: play sound wSound of the owner */
    struct { BYTE cType; BYTE cFlags; WORD wSkill; BYTE cStep; WORD wOutSkill; BYTE cOutStep; short shX; short shY; BYTE cMNumber; BYTE cDepth; } obj; /* 04 O: create an object, see vjmpReadScript */
    struct { BYTE cType; BYTE cFlags; WORD wImage; short shX; short shY; } rc;                                           /* 07 RC: throw, place the opponent (wImage = common image) */
    struct { BYTE cType; BYTE cCount; WORD wSkill; BYTE cStep; } loop;                                                   /* 09 SF: loop cCount times */
    struct { BYTE cType; WORD wSkill; BYTE cStep; } jump;                                                                /* 0A SG goto, 0B SC call, 24 command branch */
    struct { BYTE cType; WORD wWait; WORD wImage; short shX; short shY; BYTE bNoTurn; } image;                           /* 0C I: show an image for wWait frames (0 = forever) */
    struct { BYTE cType; BYTE cFlash; char cRed, cGreen, cBlue, cAlpha; WORD wDuration; BYTE cFlags;
             BYTE cSwayX, cShakeX, cSwayXTime, cSwayY, cShakeY, cSwayYTime; } eb;                                        /* 0E EB: colour flash (cFlags 1 own, 2 opponent, 4 stage, 8 system) and screen sway / shake */
    struct { BYTE cType; BYTE cUnk01; WORD wSkill; BYTE cStep; BYTE cFlags; BYTE cValue; char cAdd; } gs;                /* 10 GS: special stock check / use */
    struct { BYTE cType; BYTE cUnk01; WORD wSkill; BYTE cStep; BYTE cFlags; WORD wValue; } gl;                           /* 11 GL: life check */
    struct { BYTE cType; BYTE cFlags; BYTE cHitJunction; BYTE cUnk03; short shX; short shY; } rp;                        /* 14 RP: release the thrown opponent into hit junction cHitJunction */
    struct { BYTE cType; BYTE cUnk01; short shLife; short shSpecial; short shOppLife; short shOppSpecial; } gc;          /* 15 GC: change life / special gauge of self and opponent */
    struct { BYTE cType; BYTE cFlags; WORD wSkill; BYTE cStep; BYTE cUnk05[2]; BYTE cCondition; } db;                    /* 16 DB: branch on stance or input */
    struct { BYTE cType; short shX, shY, shW, shH; BYTE cIndex; BYTE cFlags; } box;                                      /* 18 FA attack box, 19 FD guard box (see kgtHitboxStep) */
    struct { BYTE cType; BYTE cSelf; BYTE cOpp; } ps;                                                                    /* 1A PS: stop self / opponents for some frames */
    struct { BYTE cType; BYTE cBytes[6]; } cancel;                                                                            /* 1E C: cancel condition, bytes 0-5 copied as a whole */
    struct { BYTE cType; WORD wSkill; BYTE cStep; BYTE cVar; BYTE cFlags; BYTE cSrc; short shValue; short shCompare; } var; /* 1F V: variable operation and branch */
    struct { BYTE cType; WORD wRange; WORD wThreshold; BYTE cUnk05; WORD wSkill; BYTE cStep; } rnd;                      /* 20 RANDOM: random branch */
    struct { BYTE cType; BYTE cBlend; char cRed, cGreen, cBlue, cAlpha; } col;                                          /* 23 COL OBJ: colour of the object */
    struct { BYTE cType; BYTE cUnk01[2]; BYTE cLength; BYTE cInterval; } ai;                                             /* 25 AI: after-image trail */
} kgtScriptStep;

typedef struct { BYTE cBytes[6]; } kgtCancelBlock;
/* colour flash state (script command EB) of a character (kgt_character_struct.flash); the system and
   stage copies are the gi*Flash* globals below.  iType 1 smooth, 2 blinking, 3 random; iBase*: the
   colour to return to */
typedef struct { int iType; int iRed, iGreen, iBlue, iAlpha; int iTimeLeft; int iBaseRed, iBaseGreen, iBaseBlue, iBaseAlpha; int iDuration; } kgtFlash;
#pragma pack(pop)

extern short gshSystemVariables[16];        /* 0x4456b0: system variables (script command V), kgtSystem + 0x12470 */
extern int giSystemFlashType;               /* 0x4456d0: colour effect of system objects (layout of kgt_character_struct.flash): 1 smooth, 2 blinking, 3 random */
extern int giSystemFlashRed;                /* 0x4456d4: red */
extern int giSystemFlashGreen;              /* 0x4456d8: green */
extern int giSystemFlashBlue;               /* 0x4456dc: blue */
extern int giSystemFlashAlpha;              /* 0x4456e0: alpha */
extern int giSystemFlashTimeLeft[5];        /* 0x4456e4: [0] frames left, [1..4] base red, green, blue, alpha */
extern int giSystemFlashDuration;           /* 0x4456f8: total frames */
extern int giGravityScalar;                 /* 0x445700: multiplier of script gravity values (game speed) */
extern int giGamespeedFrames;               /* 0x445704: game speed setting in effect */
extern int giStageFlashType;                /* 0x447d7d: colour effect of stage objects (layout of kgt_character_struct.flash), kgt_stage + 0x263d */
extern int giStageFlashRed;                 /* 0x447d81: red */
extern int giStageFlashGreen;               /* 0x447d85: green */
extern int giStageFlashBlue;                /* 0x447d89: blue */
extern int giStageFlashAlpha;               /* 0x447d8d: alpha */
extern int giStageFlashTimeLeft[5];         /* 0x447d91: [0] frames left, [1..4] base red, green, blue, alpha */
extern int giStageFlashDuration;            /* 0x447da5: total frames */
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
extern int giPlayerMomentumScalar;          /* 0x541f78: multiplier of script momentum values (game speed) */

int iSubtractTwoFiftySixIfAboveOneTwentySeven(char cValue);
int process_COM_skillblock(kgtSkill *pSkill);

/* the current object's after-image trail, gAfterImageTrails[cAfterImageIdx - 1], addressed as the original
   does: byte offset i = cAfterImageIdx * sizeof from the element before the array */
#define FX (*(unk_0x650_struct *)((char *)gAfterImageTrails + i - sizeof(unk_0x650_struct)))
/* one frame of the after-image trail (FX.aiData[k*4 .. k*4+3]); frame 0 is the trail's own header,
   frames 1-100 the ring: position (16.16), flags (image flip bit | facing * 4), image step (as int) */
typedef struct { int iX, iY, iFlags, pImage; } kgtTrailFrame;

/*
 * Handler of READ_SCRIPT engine objects (engine jump table): runs the object's skill script.
 * First frame (iProcessStep 0): sets the object up; players get their life for the game mode (story
 * carry-over and recovery), enemy mask, gauges, a colour nobody else uses, the R1 companion object and
 * the start skill; story CPUs may enter late (after a time, or when someone's life drops).
 * Every frame: the ground level of its line, a frame of its after-image trail, a pending jump (hit
 * junction or DS branch), hit stop, and per type: players find their enemy, turn input into skills and
 * shrink the damage bar; characters' objects land, and are deleted off the stage or with their parent;
 * the R1 companion follows its player.  Then, when the current image's wait is over, script steps run
 * until the next image step (at most 300), one command per step (codes in kgtScriptStep).  At the end
 * of a skill: SC return, SF loop, players go back to free movement, pictures and UI objects restart
 * while drawn, anything else is deleted.
 * Globals: changes gpkgtCurrentEngineObject, gkgtLoadedCharacter[] (and the system / demo / stage data
 * as owners), gAfterImageTrails, the colour flash and screen shake globals, gshSystemVariables and
 * giInputBuffer; reads gkgtGameState, giGamespeedFrames, giGravityScalar, giPlayerMomentumScalar,
 * giCameraX/Y, giObjectLayers, giCurrentStoryStep.
 */
void vjmpReadScript(void)
{
    kgt_character_struct *pOwner;  /* owner of the script: its character, or the system / demo / stage data viewed through the same type */
    kgtScriptStep *pStep;
    kgtEngineObject *pObj;
    kgtEngineObject **ppSlot;
    kgt_character_struct *pChar;
    kgtStoryEntry *pEntry;
    kgtStoryEntryCpu *pCpu;
    kgtSkill *pLastImage;  /* the image step shown last (after-image trail) */
    kgtFlash *pFlash;  /* EB colour flash target; also a plain pointer to the trail */
    int iPos;  /* trail ring position */

    int *pStackPad1;  /* unused */
    int iStepCount;  /* steps run this frame + 1; 0 stops at an image step */
    int i, j, k;  /* scratch; i has several unrelated uses (trail offset, DS condition, player, skill) */

    int iColor, bFound;
    int iDir, bAdd;
    int iValue, iX, iY, iDepth;  /* iX also holds GC's own life change */
    int iFlags, bAbsolute;
    int iInput, bHit;
    BYTE bNot;
    WORD wSkill;  /* skill index; also the image step's wait */
    short shValue, *pVar;
    int iOppLife;
    char szMsg[256];

    switch (gpkgtCurrentEngineObject->iObjectType) {
    case PLAYER_ENGINE_OBJECT:
    case STORY_ENGINE_OBJECT:
    case CHARACTER_ENGINE_OBJECT:
        pOwner = &gkgtLoadedCharacter[gpkgtCurrentEngineObject->iPlayerIdx];
        break;
    case SYSTEM_ENGINE_OBJECT:
        pOwner = (kgt_character_struct *)&gkgtKgtSystem;
        break;
    case DEMO_ENGINE_OBJECT:
        pOwner = (kgt_character_struct *)&gkgtLoadedDemo;
        break;
    case STAGE_ENGINE_OBJECT:
        pOwner = (kgt_character_struct *)&gkgtLoadedStage;
        break;
    }

    /* iProcessStep 0: first frame (then falls into 1); 1: running; anything else: nothing to do */
    switch (gpkgtCurrentEngineObject->iProcessStep) {
    case 0:
        /* first frame of the object */
        gpkgtCurrentEngineObject->iProcessStep = 1;
        switch (gpkgtCurrentEngineObject->iObjectType) {
        case PLAYER_ENGINE_OBJECT:
            gpkgtCurrentEngineObject->iDepth = giObjectLayers[gpkgtCurrentEngineObject->iLine & 1];
            switch (gkgtGameState.kgtGameMode) {
            case GAME_MODE_STORY:
                pEntry = &gkgtLoadedCharacter[0].kgtStoryEntries[giCurrentStoryStep[giStoryModeSide]];
                if (gpkgtCurrentEngineObject->iPlayerIdx == 0) {
                    /* the player: life carried over between fights? */
                    if (gkgtGameState.iCurrentRound == 1) {
                        if (!pEntry->bFirstLifeCarryOver)
                            pOwner->iHealth = pOwner->dwLifeGaugeMax;
                    } else if (pEntry->bFirstLifeCarryOver) {
                        if (pEntry->cLifeRecovery == 100) {
                            pOwner->iHealth = pOwner->dwLifeGaugeMax;
                        } else if (pEntry->cLifeRecovery) {
                            pOwner->iHealth += (BYTE)pEntry->cLifeRecovery * pOwner->dwLifeGaugeMax / 100;
                            if ((DWORD)pOwner->iHealth > pOwner->dwLifeGaugeMax)
                                pOwner->iHealth = pOwner->dwLifeGaugeMax;
                        }
                    } else {
                        pOwner->iHealth = pOwner->dwLifeGaugeMax;
                    }
                    /* every CPU is an enemy unless marked "player ignore" */
                    pOwner->iEnemyBitmask = -2;
                    if (pEntry->kgtStoryEntryCPUs[0].uBitmask & 0x200) pOwner->iEnemyBitmask = -4;
                    if (pEntry->kgtStoryEntryCPUs[1].uBitmask & 0x200) pOwner->iEnemyBitmask &= ~0x04;
                    if (pEntry->kgtStoryEntryCPUs[2].uBitmask & 0x200) pOwner->iEnemyBitmask &= ~0x08;
                    if (pEntry->kgtStoryEntryCPUs[3].uBitmask & 0x200) pOwner->iEnemyBitmask &= ~0x10;
                    if (pEntry->kgtStoryEntryCPUs[4].uBitmask & 0x200) pOwner->iEnemyBitmask &= ~0x20;
                    if (pEntry->kgtStoryEntryCPUs[5].uBitmask & 0x200) pOwner->iEnemyBitmask &= ~0x40;
                    if (pEntry->kgtStoryEntryCPUs[6].uBitmask & 0x200) pOwner->iEnemyBitmask &= ~0x80;
                    pOwner->iShowLife = 1;
                } else {
                    /* a CPU opponent of story mode */
                    pCpu = &pEntry->kgtStoryEntryCPUs[gpkgtCurrentEngineObject->iPlayerIdx - 1];
                    pOwner->iHealth = pOwner->dwLifeGaugeMax;
                    pOwner->iShowLife = pCpu->uBitmask & 1;
                    pOwner->iEnemyBitmask = (BYTE)pCpu->cEnemyBitmask;
                    /* uBitmask bits 1-2: how the CPU enters the fight */
                    switch ((pCpu->uBitmask >> 1) & 3) {
                    case 1:     /* appears after a time */
                        gpkgtCurrentEngineObject->iOpponentDowntimeInFrames = (BYTE)pCpu->cTimeMethodNumber * 100;
                        if (pCpu->cTimeMethodNumberRandom)
                            gpkgtCurrentEngineObject->iOpponentDowntimeInFrames += rand() % (BYTE)pCpu->cTimeMethodNumberRandom * 100;
                        break;
                    case 2:     /* appears when someone's life falls below a value */
                        if (gkgtLoadedCharacter[(BYTE)pCpu->cLifeMethodTarget].iHealth >= (BYTE)pCpu->cLifeMethodAmount) {


                            gpkgtCurrentEngineObject->iProcessStep = 0;  /* not yet: try again next frame */
                            return;
                        }
                        break;
                    }
                    pOwner->iColor = -1;
                    pOwner->iSpecialGaugeTokens = pOwner->cStartingStock;
                    pOwner->iSpecialGauge = 0;
                    if (!(pCpu->uBitmask & 0x200))
                        gpkgtCurrentEngineObject->iPlayerLookingRight = 1;
                }
                gpkgtCurrentEngineObject->iOwnerIdx = gpkgtCurrentEngineObject->iPlayerIdx;
                break;
            case GAME_MODE_VS_SINGLE:
            case GAME_MODE_VS_TEAM:
                pOwner->iHealth = pOwner->dwLifeGaugeMax;
                gpkgtCurrentEngineObject->iOwnerIdx = gpkgtCurrentEngineObject->iPlayerIdx;
                /* versus: every other player is an enemy; player 1 starts at x 390, the others at x 890 facing left */
                pOwner->iEnemyBitmask = -1 - (1 << gpkgtCurrentEngineObject->iPlayerIdx);
                if (gpkgtCurrentEngineObject->iPlayerIdx != 0) {
                    pOwner->iFacing = 1;
                    gpkgtCurrentEngineObject->iPlayerLookingRight = 1;
                }
                if (gpkgtCurrentEngineObject->iPlayerIdx == 0) {
                    gpkgtCurrentEngineObject->iPosX = 0x1860000;
                    pOwner->iCurrentXPos = 0x1860000;
                } else {
                    gpkgtCurrentEngineObject->iPosX = 0x37a0000;
                    pOwner->iCurrentXPos = 0x37a0000;
                }
                break;
            }

            pOwner->iCurrentXPos = gpkgtCurrentEngineObject->iPosX;
            pOwner->iCurrentYPos = gpkgtCurrentEngineObject->iPosY;
            /* battle copies of the file values and a clean per-fight state */
            pOwner->iLifeMax = pOwner->dwLifeGaugeMax;
            pOwner->iComboCount = 0;
            if ((int)gkgtGameState.iCurrentRound < 2) {
                pOwner->iSpecialGaugeTokens = pOwner->cStartingStock;
                pOwner->iSpecialGauge = 0;
            }
            pOwner->iStockMax = pOwner->iSpecialStockMax;
            pOwner->iSpecialMax = pOwner->iSpecialGaugeMax;
            pOwner->iDamageBar = 0;
            pOwner->iDamageBarDelay = 0;
            pOwner->pkgtoSelf = gpkgtCurrentEngineObject;
            pOwner->pLastOpponent = NULL;
            pOwner->pNearestEnemy = NULL;
            pOwner->iUnkDF6D = 0;
            pOwner->iUnkDF71 = 0;
            pOwner->iCpuCommandIdx = 0;
            pOwner->iCpuStepDelay = 0;
            pOwner->iCpuCommandTimer = 0;
            pOwner->iUnkDF35 = 20;
            pOwner->iOnlineState = 1;
            pOwner->iLastCommandIdx = 0;
            pOwner->iThrowFlags = 0;
            pOwner->iRoundResult = 0;
            pOwner->iPushBackX = 0;
            pOwner->iLastAttacker = 0;
            pOwner->bImageShown = 0;
            pOwner->iWinPoints = 0;
            pOwner->iHasCrouchAdvance = 0;
            pOwner->iHasCrouchRetreat = 0;
            /* a skill is empty when the next skill starts right after its first step */
            if ((WORD)pOwner->kgtCore.pSkillsAlloc[(WORD)pOwner->shSkillIdxCrouchAdvance + 1].shStartingStepIdx > (WORD)pOwner->kgtCore.pSkillsAlloc[(WORD)pOwner->shSkillIdxCrouchAdvance].shStartingStepIdx + 1)
                pOwner->iHasCrouchAdvance = 1;
            if ((WORD)pOwner->kgtCore.pSkillsAlloc[(WORD)pOwner->shSkillIdxCrouchRetreat + 1].shStartingStepIdx > (WORD)pOwner->kgtCore.pSkillsAlloc[(WORD)pOwner->shSkillIdxCrouchRetreat].shStartingStepIdx + 1)
                pOwner->iHasCrouchRetreat = 1;

            /* pick a colour nobody else uses */
            iColor = gkgtLoadedCharacter[gpkgtCurrentEngineObject->iPlayerIdx].iColor;
            if (iColor == -1)
                iColor = 0;
            for (j = 0; j < 8; j++) {
                bFound = 0;
                for (i = 0; i < 8; i++) {
                    if (gpkgtCurrentEngineObject->iPlayerIdx != i && iColor == gkgtLoadedCharacter[i].iColor)
                        bFound = 1;
                }
                if (!bFound) {
                    gkgtLoadedCharacter[gpkgtCurrentEngineObject->iPlayerIdx].iColor = iColor;
                    break;
                }
                iColor = (iColor + 1) % 8;
            }

            switch (gkgtGameState.kgtGameMode) {
            case GAME_MODE_VS_TEAM:
                /* team battle: carry over the gauges of the previous member */
                if (gkgtGameState.iCarryOverPlayer == gpkgtCurrentEngineObject->iPlayerIdx) {
                    pOwner->iHealth = gkgtGameState.dwTempHealth;
                    pOwner->iSpecialGaugeTokens = gkgtGameState.dwTempSpecialGaugeTokens;
                    pOwner->iSpecialGauge = gkgtGameState.iTempSpecialGauge;
                }
                break;
            }

            /* skill R1 runs as an extra object alongside the player */
            if ((WORD)pOwner->kgtCore.pSkillsAlloc[(WORD)pOwner->shSkillIdxR1 + 1].shStartingStepIdx > (WORD)pOwner->kgtCore.pSkillsAlloc[(WORD)pOwner->shSkillIdxR1].shStartingStepIdx + 1) {
                pObj = kgtoNewEngineObject(gpkgtCurrentEngineObject->iJumpIdx, 13, 0, 0);
                pObj->iObjectType = CHARACTER_ENGINE_OBJECT;
                pObj->iPlayerIdx = gpkgtCurrentEngineObject->iPlayerIdx;
                pObj->iSkillIdx = (WORD)pOwner->shSkillIdxR1;
                pObj->iSkillScriptIdx = (WORD)pOwner->kgtCore.pSkillsAlloc[pObj->iSkillIdx].shStartingStepIdx;
            } else {
                gpkgtCurrentEngineObject->iFlags |= 0x80000000;
            }
            /* start skill as an action; clean input buffer, cancel block, variables (0x20 bytes from shVarA),
               colour flash and M-number slots; the player is in stage coordinates (0x40000000) */
            iSwitchCurrentObjectSkill((WORD)pOwner->shSkillIdxStart);
            gpkgtCurrentEngineObject->iStateFlags = (gpkgtCurrentEngineObject->iStateFlags & ~8) | 4;
            vMemzero(giInputBuffer[gpkgtCurrentEngineObject->iPlayerIdx], sizeof(giInputBuffer[0]));
            vMemzero(&pOwner->cCancelType, 6);
            vMemzero(&pOwner->shVarA, 0x20);
            vMemzero(&pOwner->flash, sizeof(pOwner->flash));
            gpkgtCurrentEngineObject->iFlags |= 0x40000000;
            for (k = 0; k < 10; k++)
                pOwner->pMNumberObjs[k] = NULL;
            break;
        case STORY_ENGINE_OBJECT:
            gpkgtCurrentEngineObject->iOwnerIdx = gpkgtCurrentEngineObject->iPlayerIdx;
            break;
        case SYSTEM_ENGINE_OBJECT:
        case DEMO_ENGINE_OBJECT:
            break;
        case STAGE_ENGINE_OBJECT:
            /* stage script objects keep their start skill and step in these fields */
            gpkgtCurrentEngineObject->iDsLandingSkillIdx = gpkgtCurrentEngineObject->iSkillIdx;
            gpkgtCurrentEngineObject->iDsGuardedSkillIdx = gpkgtCurrentEngineObject->iSkillScriptIdx;
            break;
        case CHARACTER_ENGINE_OBJECT:
            gpkgtCurrentEngineObject->iFlags |= 0x40000000;
            break;
        }
        gpkgtCurrentEngineObject->iStartSkillIdx = gpkgtCurrentEngineObject->iSkillIdx;  /* falls through into case 1 */
    case 1:
        break;
    default:
        return;
    }

    /* every frame: the ground of its line (y 850 / 920 pixels, 16.16) */
    if (gpkgtCurrentEngineObject->iLine & 1)
        gpkgtCurrentEngineObject->iGroundY = 0x3520000;
    else
        gpkgtCurrentEngineObject->iGroundY = 0x3980000;

    /* paused: characters' objects do nothing */
    if (gkgtGameState.bPaused) {
        switch (gpkgtCurrentEngineObject->iObjectType) {
        case PLAYER_ENGINE_OBJECT:
        case STORY_ENGINE_OBJECT:
        case CHARACTER_ENGINE_OBJECT:
            return;
        }
    }

    /* record a frame of the after-image trail */
    if (gpkgtCurrentEngineObject->cAfterImageIdx) {
        /* FX = gAfterImageTrails[cAfterImageIdx - 1]; the original addresses it as a byte offset from the
           element before the array (FX, defined above the function) */
        i = gpkgtCurrentEngineObject->cAfterImageIdx * sizeof(unk_0x650_struct);
        if (((kgtScriptStep *)FX.pStep)->ai.cInterval) {
            pLastImage = &pOwner->kgtCore.pSkillScriptsAlloc[gpkgtCurrentEngineObject->iSkillScriptIdx - 1];
            iPos = FX.iPos;
            /* every cInterval frames store a frame in the ring of 100 */
            if (--FX.iTimer < 0) {
                FX.iTimer = ((kgtScriptStep *)FX.pStep)->ai.cInterval;
                pFlash = (void *)&FX;  /* matching: frame iPos + 1 through a pointer reproduces the original's address arithmetic */
                ((kgtTrailFrame *)pFlash)[iPos + 1].iX = gpkgtCurrentEngineObject->iPosX;
                ((kgtTrailFrame *)pFlash)[iPos + 1].iY = gpkgtCurrentEngineObject->iPosY;
                ((kgtTrailFrame *)pFlash)[iPos + 1].iFlags = ((((kgtScriptStep *)pLastImage)->image.wImage >> 14) & 1) + gpkgtCurrentEngineObject->iPlayerLookingRight * 4;
                ((kgtTrailFrame *)pFlash)[iPos + 1].pImage = (int)pLastImage;
                FX.iPos = (FX.iPos + 1) % 100;
            }
        }
    }

    if (gpkgtCurrentEngineObject->iHitJunctionIdx) {
        /* a pending jump (hit junction, DS branch, KO): skill (low word) at step (high word) */
        gpkgtCurrentEngineObject->iSkillIdx = -1;
        vAssignSkillAndResetOtherValues(gpkgtCurrentEngineObject->iHitJunctionIdx & 0xffff);
        gpkgtCurrentEngineObject->iSkillScriptIdx += (DWORD)gpkgtCurrentEngineObject->iHitJunctionIdx >> 16;
        gpkgtCurrentEngineObject->iHitJunctionIdx = 0;
        gpkgtCurrentEngineObject->cLoopCount = 0;
        iResetDsSkillIndices();
        vResetReactionSkillBlock(gpkgtCurrentEngineObject);
        vMemzeroHitboxArrays(gpkgtCurrentEngineObject);
    } else {
        if (gpkgtCurrentEngineObject->iOpponentDowntimeInFrames) {
            /* hit stop or PS stop: frozen for that many frames (-1 = until released, e.g. while thrown) */
            if (gpkgtCurrentEngineObject->iOpponentDowntimeInFrames == -1)
                return;
            gpkgtCurrentEngineObject->iOpponentDowntimeInFrames--;
            return;
        }
        if (gpkgtCurrentEngineObject->iObjectType == PLAYER_ENGINE_OBJECT)
            /* a PS stop's stored input only counts while frozen */
            gkgtLoadedCharacter[gpkgtCurrentEngineObject->iPlayerIdx].bUseStoredInput = 0;
        switch (gpkgtCurrentEngineObject->iObjectType) {
        case PLAYER_ENGINE_OBJECT:
            /* players: enemy, input into skills, the red damage bar (both life arms are the same), position */
            vFindNearestEnemyPlayer();
            vProcessInputsIntoSkills();
            if (pOwner->iDamageBar) {
                if (pOwner->iDamageBarDelay)
                    pOwner->iDamageBarDelay--;
                else if (pOwner->iHealth)
                    pOwner->iDamageBar--;
                else
                    pOwner->iDamageBar--;
            }
            pOwner->iCurrentXPos = gpkgtCurrentEngineObject->iPosX;
            pOwner->iCurrentYPos = gpkgtCurrentEngineObject->iPosY;
            break;
        case STORY_ENGINE_OBJECT:
            if (gpkgtCurrentEngineObject->iPosY >= gpkgtCurrentEngineObject->iGroundY && gpkgtCurrentEngineObject->iYMomentum > 0) {
                /* landed */
                gpkgtCurrentEngineObject->iPosY = gpkgtCurrentEngineObject->iGroundY;
                gpkgtCurrentEngineObject->iXGravity = 0;
                gpkgtCurrentEngineObject->iXMomentum = 0;
                gpkgtCurrentEngineObject->iYGravity = 0;
                gpkgtCurrentEngineObject->iYMomentum = 0;
                gpkgtCurrentEngineObject->iStateFlags &= ~3;
                gpkgtCurrentEngineObject->iSkillIdx = -1;
                if (gpkgtCurrentEngineObject->iDsLandingSkillIdx) {
                    gpkgtCurrentEngineObject->iSkillIdx = gpkgtCurrentEngineObject->iDsLandingSkillIdx & 0xffff;
                    gpkgtCurrentEngineObject->iSkillScriptIdx = (WORD)gkgtLoadedCharacter[gpkgtCurrentEngineObject->iPlayerIdx].kgtCore.pSkillsAlloc[gpkgtCurrentEngineObject->iSkillIdx].shStartingStepIdx + ((DWORD)gpkgtCurrentEngineObject->iDsLandingSkillIdx >> 16);
                    gpkgtCurrentEngineObject->iDsLandingSkillIdx = 0;
                    gpkgtCurrentEngineObject->iImageWaitFrames = 0;
                    vMemzeroHitboxArrays(gpkgtCurrentEngineObject);
                } else {
                    iAssignPlayerLookingRight();
                    gpkgtCurrentEngineObject->iStateFlags &= ~0x1c;
                }
            }
            /* more than 50 pixels off the stage (x -50..1330, y -50..1010)?  deleted unless it follows a parent */
            if ((gpkgtCurrentEngineObject->iPosX < -0x320000 || gpkgtCurrentEngineObject->iPosX > 0x5320000
                 || gpkgtCurrentEngineObject->iPosY < -0x320000 || gpkgtCurrentEngineObject->iPosY > 0x3f20000)
                && !(gpkgtCurrentEngineObject->iFlags & 0x20000000))
                vDeleteCurrentEngineObject();
            if (0)      /* debug output, disabled in the release (the format stays in .data) */
                sprintf(szMsg, "OBJ\217\301\226\305 / %d , %d / %d", gpkgtCurrentEngineObject->iPosX, gpkgtCurrentEngineObject->iPosY, gpkgtCurrentEngineObject->iPlayerIdx);   /* OBJ消滅 (object deleted) */
            /* a follower goes with its parent */
            if ((gpkgtCurrentEngineObject->iFlags & 0x20000000) && gpkgtCurrentEngineObject->pParent->iJumpIdx == RESET_IDX)
                vDeleteCurrentEngineObject();
            break;
        case CHARACTER_ENGINE_OBJECT:
            /* the R1 companion: follows its player (on the ground) while the player shows a script image */
            if (gpkgtCurrentEngineObject->pParent->iJumpIdx == RESET_IDX)
                goto destroy;
            if (gpkgtCurrentEngineObject->pParent->iDrawFlag != -1)
                return;
            gpkgtCurrentEngineObject->iPosX = gpkgtCurrentEngineObject->pParent->iPosX;
            gpkgtCurrentEngineObject->iPosY = gpkgtCurrentEngineObject->pParent->iGroundY;
            gpkgtCurrentEngineObject->iPlayerLookingRight = gpkgtCurrentEngineObject->pParent->iPlayerLookingRight;
            break;
        }
    }

    /* still showing the last image?  The image step adds wait * giGamespeedFrames, each frame takes 100;
       -1 = forever */
    if (gpkgtCurrentEngineObject->iImageWaitFrames < 0)
        return;
    gpkgtCurrentEngineObject->iImageWaitFrames -= 100;
    if (gpkgtCurrentEngineObject->iImageWaitFrames >= 0)
        return;

    iStepCount = 1;
    /* attack boxes with FA flag 2 (continuous hit) may hit again */
    i = 20;
    while (i-- > 0) {
        if (gpkgtCurrentEngineObject->pAttackBoxes[i] && (((kgtScriptStep *)gpkgtCurrentEngineObject->pAttackBoxes[i])->box.cFlags & 2))
            gpkgtCurrentEngineObject->iStateFlags &= ~0x10;
    }

    /* run script steps until an image step (or 300 steps) */
    while (iStepCount) {
        /* a script that never reaches an image step: report it and delete the object */
        if (++iStepCount > 300) {
            sprintf(szMsg, "ScriptMainLoopError %d %d - nd:%d step:%d", gpkgtCurrentEngineObject->iPlayerIdx, gpkgtCurrentEngineObject->iObjectType,
                    gpkgtCurrentEngineObject->iSkillIdx, gpkgtCurrentEngineObject->iSkillScriptIdx - (WORD)pOwner->kgtCore.pSkillsAlloc[gpkgtCurrentEngineObject->iSkillIdx + 1].shStartingStepIdx);
            iSetDebugInfo(szMsg, 0x8080ff);
            goto destroy;
        }
        /* past the skill's last step (where the next skill starts) */
        if ((WORD)pOwner->kgtCore.pSkillsAlloc[gpkgtCurrentEngineObject->iSkillIdx + 1].shStartingStepIdx <= gpkgtCurrentEngineObject->iSkillScriptIdx)
            goto end_of_skill;
dispatch:
        pStep = (kgtScriptStep *)&pOwner->kgtCore.pSkillScriptsAlloc[gpkgtCurrentEngineObject->iSkillScriptIdx];
        /* the command code; branches set iSkillScriptIdx to the target step - 1 and let next_step add 1
           (break and goto next_step both end there) */
        switch (pStep->cBytes[0]) {
        case 0x05:      /* end */
        case 0x29:  /* end as well */
            if (gpkgtCurrentEngineObject->iObjectType != PLAYER_ENGINE_OBJECT)
                goto destroy;
end_of_skill:
            if (gpkgtCurrentEngineObject->iReturnSkillIdx) {
                /* return from SC */
                gpkgtCurrentEngineObject->iSkillIdx = gpkgtCurrentEngineObject->iReturnSkillIdx & 0xffff;
                gpkgtCurrentEngineObject->iSkillScriptIdx = (WORD)pOwner->kgtCore.pSkillsAlloc[gpkgtCurrentEngineObject->iSkillIdx].shStartingStepIdx + (gpkgtCurrentEngineObject->iReturnSkillIdx >> 16) + 1;
                gpkgtCurrentEngineObject->iReturnSkillIdx = 0;
                goto dispatch;
            }
            if (gpkgtCurrentEngineObject->cLoopCount) {
                /* SF loop: again from the loop target, or on after the SF step once the count runs out */
                if (--gpkgtCurrentEngineObject->cLoopCount == 0) {
                    gpkgtCurrentEngineObject->iSkillIdx = gpkgtCurrentEngineObject->iLoopReturn & 0xffff;
                    gpkgtCurrentEngineObject->iSkillScriptIdx = (WORD)pOwner->kgtCore.pSkillsAlloc[gpkgtCurrentEngineObject->iSkillIdx].shStartingStepIdx + (gpkgtCurrentEngineObject->iLoopReturn >> 16) + 1;
                } else {
                    gpkgtCurrentEngineObject->iSkillIdx = gpkgtCurrentEngineObject->iLoopTarget & 0xffff;
                    gpkgtCurrentEngineObject->iSkillScriptIdx = (WORD)pOwner->kgtCore.pSkillsAlloc[gpkgtCurrentEngineObject->iSkillIdx].shStartingStepIdx + (gpkgtCurrentEngineObject->iLoopTarget >> 16);
                }
                goto dispatch;
            }
            gpkgtCurrentEngineObject->iImageWaitFrames = 0;
            iResetDsSkillIndices();
            gpkgtCurrentEngineObject->iStateFlags &= ~0x10;
            switch (gpkgtCurrentEngineObject->iObjectType) {
            case PLAYER_ENGINE_OBJECT:
                pOwner->iCurrentActionCancellableFlag = 0;
                pOwner->iComboCount = 0;
                gpkgtCurrentEngineObject->iSkillIdx = -1;
                pOwner->iThrowFlags = 0;
                vCheckIfDrawThenMove();
                goto dispatch;
            case STORY_ENGINE_OBJECT:
                if (gpkgtCurrentEngineObject->iStartSkillIdx == (WORD)pOwner->shSkillIdxCharSelectPic) {
                    if (gpkgtCurrentEngineObject->iDrawFlag == 0) {
                        vDeleteCurrentEngineObject();
                        return;
                    }
                    gpkgtCurrentEngineObject->iSkillIdx = gpkgtCurrentEngineObject->iStartSkillIdx;
                    gpkgtCurrentEngineObject->iSkillScriptIdx = (WORD)pOwner->kgtCore.pSkillsAlloc[gpkgtCurrentEngineObject->iSkillIdx].shStartingStepIdx;
                    goto dispatch;
                }
                if (gpkgtCurrentEngineObject->iStartSkillIdx != (WORD)pOwner->shSkillIdxStageFacePic || gpkgtCurrentEngineObject->iDrawFlag == 0)
                    goto destroy;
                gpkgtCurrentEngineObject->iSkillIdx = gpkgtCurrentEngineObject->iStartSkillIdx;
                gpkgtCurrentEngineObject->iSkillScriptIdx = (WORD)pOwner->kgtCore.pSkillsAlloc[gpkgtCurrentEngineObject->iSkillIdx].shStartingStepIdx;
                goto dispatch;
            case SYSTEM_ENGINE_OBJECT:
                if (pOwner->kgtCore.pSkillsAlloc[gpkgtCurrentEngineObject->iStartSkillIdx].iDefaultScriptGroup & 0x20)
                    goto destroy;
            case DEMO_ENGINE_OBJECT:
            case STAGE_ENGINE_OBJECT:
            case CHARACTER_ENGINE_OBJECT:
                /* loop the skill while drawn */
                if (gpkgtCurrentEngineObject->iDrawFlag == 0)
                    goto destroy;
                gpkgtCurrentEngineObject->iSkillIdx = gpkgtCurrentEngineObject->iStartSkillIdx;
                gpkgtCurrentEngineObject->iSkillScriptIdx = (WORD)pOwner->kgtCore.pSkillsAlloc[gpkgtCurrentEngineObject->iSkillIdx].shStartingStepIdx;
                goto dispatch;
            default:
                goto destroy;
            }

        case 0x0c:      /* I: image */
            if (gpkgtCurrentEngineObject->iObjectType == PLAYER_ENGINE_OBJECT)
                pOwner->bImageShown = 1;
            wSkill = pStep->image.wWait;
            gpkgtCurrentEngineObject->iDrawFlag = -1;
            if (wSkill) {
                gpkgtCurrentEngineObject->iImageWaitFrames += wSkill * giGamespeedFrames;
                iStepCount = 0;  /* matching: the store in both arms keeps 0 in ecx as in the original */
            } else {
                gpkgtCurrentEngineObject->iImageWaitFrames = -1;
                iStepCount = 0;  /* matching: see above */
            }
            break;

        case 0x01:      /* M: movement */
            /* x values are mirrored for a flipped object; script units * giPlayerMomentumScalar /
               giGravityScalar (game speed) give 16.16 values */
            bAdd = 0;
            iDir = 1;
            if (gpkgtCurrentEngineObject->iPlayerLookingRight)
                iDir = -1;
            if (pStep->move.cFlags & 1)
                bAdd = 1;
            iValue = pStep->move.shXMomentum * giPlayerMomentumScalar * iDir;
            if (!(pStep->move.cFlags & 2)) {
                if (bAdd)
                    gpkgtCurrentEngineObject->iXMomentum += iValue;
                else
                    gpkgtCurrentEngineObject->iXMomentum = iValue;
            }
            iValue = pStep->move.shYMomentum * giPlayerMomentumScalar;
            if (!(pStep->move.cFlags & 4)) {
                if (bAdd)
                    gpkgtCurrentEngineObject->iYMomentum += iValue;
                else
                    gpkgtCurrentEngineObject->iYMomentum = iValue;
            }
            iValue = pStep->move.shXGravity * giGravityScalar * iDir;
            if (!(pStep->move.cFlags & 8)) {
                if (bAdd)
                    gpkgtCurrentEngineObject->iXGravity += iValue;
                else
                    gpkgtCurrentEngineObject->iXGravity = iValue;
            }
            iValue = pStep->move.shYGravity * giGravityScalar;
            if (pStep->move.cFlags & 0x10)
                goto next_step;
            if (bAdd)
                gpkgtCurrentEngineObject->iYGravity += iValue;
            else
                gpkgtCurrentEngineObject->iYGravity = iValue;
            break;

        case 0x02:      /* DS: conditional branch registration */
            /* conditions: 1 landing, 2 attack hits, 3 attack guarded (also an FA flag-0x80 clash), 4 wall,
               5 attacks clash, 6 throw; value (step << 16) | skill */
            i = pStep->ds.cCondition;
            if (i == 0)
                goto next_step;
            iValue = (pStep->ds.cStep << 16) + pStep->ds.wSkill;
            switch (i) {
            case 1: gpkgtCurrentEngineObject->iDsLandingSkillIdx = iValue; break;
            case 2: gpkgtCurrentEngineObject->iDsAttackHitsSkillIdx = iValue; break;
            case 3: gpkgtCurrentEngineObject->iDsGuardedSkillIdx = iValue; break;
            case 4: gpkgtCurrentEngineObject->iDsWallHitSkillIdx = iValue; break;
            case 5: gpkgtCurrentEngineObject->iDsAttackClashSkillIdx = iValue; break;
            case 6: gpkgtCurrentEngineObject->iDsThrowSkillIdx = iValue; break;
            default: goto next_step;
            }
            break;

        case 0x0a:      /* SG: goto */
            if (!pStep->jump.wSkill)
                goto next_step;
            gpkgtCurrentEngineObject->iSkillIdx = pStep->jump.wSkill;
            gpkgtCurrentEngineObject->iSkillScriptIdx = (WORD)pOwner->kgtCore.pSkillsAlloc[gpkgtCurrentEngineObject->iSkillIdx].shStartingStepIdx + pStep->jump.cStep - 1;
            break;

        case 0x09:      /* SF: loop */
            if (!pStep->loop.cCount)
                goto next_step;
            wSkill = pStep->loop.wSkill;
            if (!wSkill)
                goto next_step;
            /* run wSkill from cStep cCount times, then continue after this step */
            gpkgtCurrentEngineObject->iLoopReturn = ((gpkgtCurrentEngineObject->iSkillScriptIdx - (WORD)pOwner->kgtCore.pSkillsAlloc[gpkgtCurrentEngineObject->iSkillIdx].shStartingStepIdx) << 16) + gpkgtCurrentEngineObject->iSkillIdx;
            gpkgtCurrentEngineObject->cLoopCount = pStep->loop.cCount;
            gpkgtCurrentEngineObject->iLoopTarget = (pStep->loop.cStep << 16) + wSkill;
            gpkgtCurrentEngineObject->iSkillIdx = wSkill;
            gpkgtCurrentEngineObject->iSkillScriptIdx = (WORD)pOwner->kgtCore.pSkillsAlloc[wSkill].shStartingStepIdx + pStep->loop.cStep - 1;
            break;

        case 0x0b:      /* SC: call */
            if (!pStep->jump.wSkill)
                goto next_step;
            /* come back to this step later: (step offset << 16) | skill */
            gpkgtCurrentEngineObject->iReturnSkillIdx = ((gpkgtCurrentEngineObject->iSkillScriptIdx - (WORD)pOwner->kgtCore.pSkillsAlloc[gpkgtCurrentEngineObject->iSkillIdx].shStartingStepIdx) << 16) + gpkgtCurrentEngineObject->iSkillIdx;
            gpkgtCurrentEngineObject->iSkillIdx = pStep->jump.wSkill;
            gpkgtCurrentEngineObject->iSkillScriptIdx = (WORD)pOwner->kgtCore.pSkillsAlloc[gpkgtCurrentEngineObject->iSkillIdx].shStartingStepIdx + pStep->jump.cStep - 1;
            break;

        case 0x04:      /* O: object */
            if (gpkgtCurrentEngineObject->iObjectType < SYSTEM_ENGINE_OBJECT && !(pStep->obj.cFlags & 4)) {
                /* the M. number slot is taken: "it's out" branch or replace the object */
                ppSlot = &pOwner->pMNumberObjs[pStep->obj.cMNumber];
                if (*ppSlot) {
                    for (i = 0, pObj = gkgtEngineObjects; i < 1024; i++, pObj++) {
                        if (pObj == *ppSlot) {
                            if (pStep->obj.wOutSkill) {
                                gpkgtCurrentEngineObject->iSkillIdx = pStep->obj.wOutSkill;
                                gpkgtCurrentEngineObject->iSkillScriptIdx = (WORD)pOwner->kgtCore.pSkillsAlloc[gpkgtCurrentEngineObject->iSkillIdx].shStartingStepIdx + pStep->obj.cOutStep - 1;
                                goto next_step;
                            }
                            pObj->iJumpIdx = RESET_IDX;
                            *ppSlot = NULL;
                            break;
                        }
                    }
                }
            }
            if (!pStep->obj.wSkill)
                goto next_step;
            iFlags = pStep->obj.cFlags;
            /* flag 0x40: screen coordinates; system objects decide by the skill's group (flags 9: relative),
               demo objects are always absolute, stage objects never.  Flags 0-1 place it behind / in front /
               at depth cDepth */
            bAbsolute = iFlags & 0x40;
            switch (gpkgtCurrentEngineObject->iObjectType) {
            case SYSTEM_ENGINE_OBJECT:
                if (pOwner->kgtCore.pSkillsAlloc[pStep->obj.wSkill].iDefaultScriptGroup & 9) {
                    bAbsolute = 0;
                    break;
                }
                bAbsolute = 1;
                break;
            case DEMO_ENGINE_OBJECT:
                bAbsolute = 1;
                break;
            case STAGE_ENGINE_OBJECT:
                bAbsolute = 0;
                break;
            }
            if (!bAbsolute) {
                if (gpkgtCurrentEngineObject->iPlayerLookingRight & 1)
                    iX = gpkgtCurrentEngineObject->iPosX - (pStep->obj.shX * 0x10000);
                else
                    iX = (pStep->obj.shX << 16) + gpkgtCurrentEngineObject->iPosX;
                iY = (pStep->obj.shY << 16) + gpkgtCurrentEngineObject->iPosY;
            } else {
                iX = pStep->obj.shX << 16;
                iY = pStep->obj.shY << 16;
            }

            iDepth = gpkgtCurrentEngineObject->iDepth;
            switch (iFlags & 3) {
            case 0:     /* behind */
                if (--iDepth < 10)
                    iDepth = 10;
                break;
            case 1:     /* in front */
                if (++iDepth > 0x7f)
                    iDepth = 0x7f;
                break;
            case 2:
                iDepth = pStep->obj.cDepth;
                break;
            }
            pObj = kgtoNewEngineObject(gpkgtCurrentEngineObject->iJumpIdx, iDepth, iX, iY);
            if (0)      /* debug output, disabled in the release (the format stays in .data) */
                sprintf(szMsg, "OBJ\217o\214\273 / %d , %d / %s", iX, iY, pOwner->kgtCore.pSkillsAlloc[pStep->obj.wSkill].szName);   /* OBJ出現 (object appears) */
            pObj->iObjectType = STORY_ENGINE_OBJECT;
            pObj->iPlayerIdx = gpkgtCurrentEngineObject->iPlayerIdx;
            switch (gpkgtCurrentEngineObject->iObjectType) {
            case SYSTEM_ENGINE_OBJECT:
                pObj->iObjectType = SYSTEM_ENGINE_OBJECT;
                break;
            case DEMO_ENGINE_OBJECT:
                pObj->iObjectType = DEMO_ENGINE_OBJECT;
                break;
            case STAGE_ENGINE_OBJECT:
                pObj->iObjectType = STAGE_ENGINE_OBJECT;
                break;
            case PLAYER_ENGINE_OBJECT:
            case STORY_ENGINE_OBJECT:
            case CHARACTER_ENGINE_OBJECT:
                pObj->iObjectType = STORY_ENGINE_OBJECT;
                break;
            }
            pObj->iPlayerLookingRight = gpkgtCurrentEngineObject->iPlayerLookingRight;
            pObj->iLine = gpkgtCurrentEngineObject->iLine;
            pObj->iSkillIdx = pStep->obj.wSkill;
            pObj->iSkillScriptIdx = (WORD)pOwner->kgtCore.pSkillsAlloc[pObj->iSkillIdx].shStartingStepIdx + pStep->obj.cStep;
            if (!bAbsolute)
                pObj->iFlags |= 0x40000000;  /* stage coordinates (scrolls with the camera) */
            if (gpkgtCurrentEngineObject->iObjectType < SYSTEM_ENGINE_OBJECT) {
                if (!(pStep->obj.cFlags & 4))
                    pOwner->pMNumberObjs[pStep->obj.cMNumber] = pObj;
                if (pStep->obj.cFlags & 8)
                    pObj->iFlags |= 0x80000000;  /* O flag 8 (meaning unknown) */
            }
            if (!(pStep->obj.cFlags & 0x20))
                goto next_step;
            /* keeps its position relative to the parent */
            pObj->iFlags |= 0x20000000;
            pObj->shParentOffsetX = pStep->obj.shX;
            pObj->shParentOffsetY = pStep->obj.shY;

            break;

        case 0x18:      /* FA: attack box */
            /* the slot points at this very step; a zero width or height clears it.  Players: FA flag 1
               makes the action cancellable, flag 2 lets the attack hit again */
            ppSlot = (kgtEngineObject **)&gpkgtCurrentEngineObject->pAttackBoxes[pStep->box.cIndex];
            *(kgtScriptStep **)ppSlot = pStep;
            if (!pStep->box.shW || !pStep->box.shH)
                goto clear_box;
            if (gpkgtCurrentEngineObject->iObjectType != PLAYER_ENGINE_OBJECT)
                goto next_step;
            if (pStep->box.cFlags & 1)
                pOwner->iCurrentActionCancellableFlag = 1;
            else
                pOwner->iCurrentActionCancellableFlag = 0;
            if (!(pStep->box.cFlags & 2))
                goto next_step;
            gpkgtCurrentEngineObject->iStateFlags &= ~0x10;
            break;

        case 0x19:      /* FD: guard box */
            ppSlot = (kgtEngineObject **)&gpkgtCurrentEngineObject->pGuardBoxes[pStep->box.cIndex];
            *(kgtScriptStep **)ppSlot = pStep;
            if (pStep->box.shW && pStep->box.shH)
                goto next_step;
clear_box:
            *ppSlot = NULL;
            break;

        case 0x17:      /* reaction */
            gpkgtCurrentEngineObject->pReactionStep = (kgtSkill *)pStep;
            break;

        case 0x16:      /* DB: basic branch */
            /* flag 1 inverts the condition, flag 2 disables it (never true) */
            bNot = pStep->db.cFlags & 1;
            bHit = 0;
            if (!(pStep->db.cFlags & 2)) {
                iInput = giInputBuffer[gpkgtCurrentEngineObject->iPlayerIdx][giInputBufferPos];
                switch (pStep->db.cCondition) {
                case 1:     /* on the ground */
                    if (gpkgtCurrentEngineObject->iPosY >= gpkgtCurrentEngineObject->iGroundY)
                        bHit = 1;
                    break;
                case 2:     /* standing */
                    if (gpkgtCurrentEngineObject->iPosY >= gpkgtCurrentEngineObject->iGroundY && !(iInput & 8))
                        bHit = 1;
                    break;
                case 3:     /* crouching */
                    if (gpkgtCurrentEngineObject->iPosY >= gpkgtCurrentEngineObject->iGroundY && (iInput & 8))
                        bHit = 1;
                    break;
                case 4:     /* forward */
                    if ((gkgtLoadedCharacter[gpkgtCurrentEngineObject->iPlayerIdx].iOptionFlags & 8) && gpkgtCurrentEngineObject->iPlayerLookingRight) {
                        if (iInput & 1)
                            bHit = 1;
                    } else {
                        if (iInput & 2)
                            bHit = 1;
                    }
                    break;
                case 5:     /* backward */
                    if ((gkgtLoadedCharacter[gpkgtCurrentEngineObject->iPlayerIdx].iOptionFlags & 8) && gpkgtCurrentEngineObject->iPlayerLookingRight) {
                        if (iInput & 2)
                            bHit = 1;
                    } else {
                        if (iInput & 1)
                            bHit = 1;
                    }
                    break;
                case 6:     /* up */
                    if (iInput & 4)
                        bHit = 1;
                    break;
                case 7:     /* down */
                    if (iInput & 8)
                        bHit = 1;
                    break;
                case 8:     /* lever neutral */
                    if (!(iInput & 0xf))
                        bHit = 1;
                    break;
                }
            }
            if (bHit) {
                if (bNot)
                    goto next_step;
            } else {
                if (!bNot)
                    goto next_step;
            }
            if (!pStep->db.wSkill)
                goto next_step;
            gpkgtCurrentEngineObject->iSkillIdx = pStep->db.wSkill;
            gpkgtCurrentEngineObject->iSkillScriptIdx = (WORD)pOwner->kgtCore.pSkillsAlloc[gpkgtCurrentEngineObject->iSkillIdx].shStartingStepIdx + pStep->db.cStep - 1;
            break;

        case 0x07:      /* RC: throw - place the opponent */
            if (gpkgtCurrentEngineObject->iObjectType != PLAYER_ENGINE_OBJECT || !pStep->rc.wImage)
                goto next_step;
            pObj = pOwner->pLastOpponent;
            if (!pObj)
                goto next_step;
            /* flag 1: the thrower is drawn in front; flag 4: the opponent faces the same way as the thrower */
            if (pStep->rc.cFlags & 1) {
                gpkgtCurrentEngineObject->iDepth = giObjectLayers[gpkgtCurrentEngineObject->iLine & 1] + 1;
                pObj->iDepth = giObjectLayers[pObj->iLine & 1] - 1;
            } else {
                gpkgtCurrentEngineObject->iDepth = giObjectLayers[gpkgtCurrentEngineObject->iLine & 1] - 1;
                pObj->iDepth = giObjectLayers[pObj->iLine & 1] + 1;
            }
            if (gpkgtCurrentEngineObject->iPlayerLookingRight & 1) {
                pObj->iPosX = gpkgtCurrentEngineObject->iPosX - (pStep->rc.shX << 16);
                if (pStep->rc.cFlags & 4)
                    pObj->iPlayerLookingRight = 1;
                else
                    pObj->iPlayerLookingRight = 0;
            } else {
                pObj->iPosX = (pStep->rc.shX << 16) + gpkgtCurrentEngineObject->iPosX;
                if (pStep->rc.cFlags & 4)
                    pObj->iPlayerLookingRight = 0;
                else
                    pObj->iPlayerLookingRight = 1;
            }
            pObj->iPosY = (pStep->rc.shY << 16) + gpkgtCurrentEngineObject->iPosY;
            vMemzeroHitboxArrays(gpkgtCurrentEngineObject);
            pObj->iXMomentum = 0;
            pObj->iYMomentum = 0;
            pObj->iXGravity = 0;
            pObj->iYGravity = 0;
            if (pObj->iObjectType == PLAYER_ENGINE_OBJECT) {
                /* show the thrown player's common image: its script step 0 is rewritten as an image step
                   (E number of common image wImage, flags 4/8 as image bits 14/15) */
                kgtScriptStep *pThrownImage = (kgtScriptStep *)gkgtLoadedCharacter[pObj->iPlayerIdx].kgtCore.pSkillScriptsAlloc;
                pObj->iDrawFlag = -1;
                pThrownImage->image.wImage = (gkgtLoadedCharacter[pObj->iPlayerIdx].kgtCommonImages[pStep->rc.wImage].shENumber & 0x1fff) | ((WORD)(pStep->rc.cFlags & 0xc) << 12);
                pThrownImage->cBytes[0] = 0x0c;
                pObj->iSkillIdx = 0;
                pObj->iSkillScriptIdx = 1;
                pThrownImage->image.shX = gkgtLoadedCharacter[pObj->iPlayerIdx].kgtCommonImages[pStep->rc.wImage].shXMovement;
                pThrownImage->image.shY = gkgtLoadedCharacter[pObj->iPlayerIdx].kgtCommonImages[pStep->rc.wImage].shYMovement;
                vMemzeroHitboxArrays(pObj);
                vResetReactionSkillBlock(pObj);
                pOwner->iThrowFlags = pStep->rc.cFlags | 0x20;  /* 0x20: a throw is in progress */
                pOwner->iThrowOffsetX = pStep->rc.shX << 16;
                pOwner->iThrowOffsetY = pStep->rc.shY << 16;
                pObj->iStateFlags = (pObj->iStateFlags & ~5) | 0xa;
            }
            pObj->iOpponentDowntimeInFrames = -1;  /* frozen until released (RP) */
            break;

        case 0x14:      /* RP: partner script modification */
            if (gpkgtCurrentEngineObject->iObjectType != PLAYER_ENGINE_OBJECT)
                goto next_step;
            pObj = pOwner->pLastOpponent;
            if (!pObj)
                goto next_step;
            if (pStep->rp.cFlags & 1) {
                gpkgtCurrentEngineObject->iDepth = giObjectLayers[gpkgtCurrentEngineObject->iLine & 1] + 1;
                pObj->iDepth = giObjectLayers[pObj->iLine & 1] - 1;
            } else {
                gpkgtCurrentEngineObject->iDepth = giObjectLayers[gpkgtCurrentEngineObject->iLine & 1] - 1;
                pObj->iDepth = giObjectLayers[pObj->iLine & 1] + 1;
            }
            if (gpkgtCurrentEngineObject->iPlayerLookingRight & 1) {
                pObj->iPosX = gpkgtCurrentEngineObject->iPosX - (pStep->rp.shX << 16);
                if (pStep->rp.cFlags & 4)
                    pObj->iPlayerLookingRight = 1;
                else
                    pObj->iPlayerLookingRight = 0;
            } else {
                pObj->iPosX = (pStep->rp.shX << 16) + gpkgtCurrentEngineObject->iPosX;
                if (pStep->rp.cFlags & 4)
                    pObj->iPlayerLookingRight = 0;
                else
                    pObj->iPlayerLookingRight = 1;
            }
            pObj->iPosY = (pStep->rp.shY << 16) + gpkgtCurrentEngineObject->iPosY;
            vMemzeroHitboxArrays(gpkgtCurrentEngineObject);
            if (pObj->iObjectType != PLAYER_ENGINE_OBJECT)
                goto next_step;
            /* the opponent takes hit junction cHitJunction (its own skill for it) and moves again */
            i = pStep->rp.cHitJunction;
            if (i)
                pObj->iHitJunctionIdx = (WORD)gkgtLoadedCharacter[pObj->iPlayerIdx].kgtHitJunctions[i].shAllotmentIdx;
            pObj->iStateFlags = (pObj->iStateFlags & ~5) | 0xa;
            pObj->iOpponentDowntimeInFrames = 0;
            pOwner->iThrowFlags = 0;
            break;

        case 0x11:      /* GL: life gauge check */
            /* flag 1: branch while life <= wValue, else while life >= wValue; a branch to skill 0 takes a
               follow-up command instead (iHandlePlayerCommandSequence from the last command) */
            if (pStep->gl.cFlags & 1) {
                if (pOwner->iHealth > pStep->gl.wValue)
                    goto next_step;
            } else {
                if (pOwner->iHealth < pStep->gl.wValue || !pStep->gl.wSkill)
                    goto next_step;
            }
            gpkgtCurrentEngineObject->iSkillIdx = pStep->gl.wSkill;
            gpkgtCurrentEngineObject->iSkillScriptIdx = (WORD)pOwner->kgtCore.pSkillsAlloc[gpkgtCurrentEngineObject->iSkillIdx].shStartingStepIdx + pStep->gl.cStep - 1;
            if (gpkgtCurrentEngineObject->iSkillIdx)
                goto next_step;
            i = iHandlePlayerCommandSequence(pOwner->iLastCommandIdx);
            if (!i)
                goto next_step;
            gpkgtCurrentEngineObject->iSkillIdx = i;
            gpkgtCurrentEngineObject->iSkillScriptIdx = (WORD)pOwner->kgtCore.pSkillsAlloc[i].shStartingStepIdx - 1;
            break;

        case 0x15:      /* GC: gauge change */
            pChar = &gkgtLoadedCharacter[gpkgtCurrentEngineObject->iPlayerIdx];
            iX = pStep->gc.shLife;
            iOppLife = pStep->gc.shOppLife;
            if (iX)
                vAddToHealth(pChar, iX);
            vAddToSpecialGauge(gpkgtCurrentEngineObject->iPlayerIdx, pStep->gc.shSpecial);
            /* then the opponent: the last one, else the nearest enemy */
            if (pChar->pLastOpponent)
                pChar = &gkgtLoadedCharacter[pChar->pLastOpponent->iPlayerIdx];
            else if (pChar->pNearestEnemy)
                pChar = &gkgtLoadedCharacter[pChar->pNearestEnemy->iPlayerIdx];
            else
                goto next_step;
            if (pChar) {
                if (iOppLife)
                    vAddToHealth(pChar, iOppLife);
                vAddToSpecialGauge(pChar->pkgtoSelf->iPlayerIdx, pStep->gc.shOppSpecial);
            }
            goto next_step;

        default:
            break;

        case 0x03:      /* S: sound */
            vHandlePlayingSingleSound(&pOwner->kgtCore.pkgtSounds[pStep->sound.wSound]);
            break;

        case 0x0e:      /* EB: palette flash / picture sway */
            if (pStep->eb.cFlash) {
                if (pStep->eb.cFlags & 1) {
                    /* own side */
                    pFlash = (kgtFlash *)&gkgtLoadedCharacter[gpkgtCurrentEngineObject->iPlayerIdx].flash;
                    pFlash->iType = pStep->eb.cFlash;
                    pFlash->iRed = pStep->eb.cRed;
                    pFlash->iGreen = pStep->eb.cGreen;
                    pFlash->iBlue = pStep->eb.cBlue;
                    pFlash->iAlpha = pStep->eb.cAlpha;
                    pFlash->iBaseRed = gpkgtCurrentEngineObject->iColorRed;
                    pFlash->iBaseGreen = gpkgtCurrentEngineObject->iColorGreen;
                    pFlash->iBaseBlue = gpkgtCurrentEngineObject->iColorBlue;
                    pFlash->iBaseAlpha = gpkgtCurrentEngineObject->iColorAlpha;
                    pFlash->iDuration = pStep->eb.wDuration;
                    pFlash->iTimeLeft = pStep->eb.wDuration;
                }
                if ((pStep->eb.cFlags & 2) && pOwner->pLastOpponent) {
                    /* opponent side */
                    pFlash = (kgtFlash *)&gkgtLoadedCharacter[pOwner->pLastOpponent->iPlayerIdx].flash;
                    pFlash->iType = pStep->eb.cFlash;
                    pFlash->iRed = pStep->eb.cRed;
                    pFlash->iGreen = pStep->eb.cGreen;
                    pFlash->iBlue = pStep->eb.cBlue;
                    pFlash->iAlpha = pStep->eb.cAlpha;
                    pFlash->iBaseRed = 0;
                    pFlash->iBaseGreen = 0;
                    pFlash->iBaseBlue = 0;
                    pFlash->iBaseAlpha = 0;
                    pFlash->iDuration = pStep->eb.wDuration;
                    pFlash->iTimeLeft = pStep->eb.wDuration;
                }
                if (pStep->eb.cFlags & 4) {
                    /* background */
                    giStageFlashType = pStep->eb.cFlash;
                    giStageFlashTimeLeft[1] = 0;
                    giStageFlashTimeLeft[2] = 0;
                    giStageFlashRed = pStep->eb.cRed;
                    giStageFlashTimeLeft[3] = 0;
                    giStageFlashGreen = pStep->eb.cGreen;
                    giStageFlashTimeLeft[4] = 0;
                    giStageFlashBlue = pStep->eb.cBlue;
                    giStageFlashAlpha = pStep->eb.cAlpha;
                    giStageFlashDuration = pStep->eb.wDuration;
                    giStageFlashTimeLeft[0] = pStep->eb.wDuration;
                }
                if (pStep->eb.cFlags & 8) {
                    /* system */
                    giSystemFlashType = pStep->eb.cFlash;
                    giSystemFlashRed = pStep->eb.cRed;
                    giSystemFlashGreen = pStep->eb.cGreen;
                    giSystemFlashBlue = pStep->eb.cBlue;
                    giSystemFlashAlpha = pStep->eb.cAlpha;
                    giSystemFlashTimeLeft[1] = 0;
                    giSystemFlashTimeLeft[2] = 0;
                    giSystemFlashTimeLeft[3] = 0;
                    giSystemFlashTimeLeft[4] = 0;
                    giSystemFlashDuration = pStep->eb.wDuration;
                    giSystemFlashTimeLeft[0] = pStep->eb.wDuration;
                }
            }
            /* screen sway / shake (vCalculateShake): mode, amplitude, frames */
            if (pStep->eb.cSwayX) {
                giShakeXMode = pStep->eb.cSwayX;
                giShakeXOffset = 0;
                giShakeXAmplitude = pStep->eb.cShakeX;
                giShakeXDuration = pStep->eb.cSwayXTime;
                giShakeXTimeLeft = pStep->eb.cSwayXTime;
            }
            if (!pStep->eb.cSwayY)
                goto next_step;
            giShakeYOffset = 0;
            giShakeYMode = pStep->eb.cSwayY;
            giShakeYAmplitude = pStep->eb.cShakeY;
            giShakeYDuration = pStep->eb.cSwayYTime;
            giShakeYTimeLeft = pStep->eb.cSwayYTime;
            break;

        case 0x10:      /* GS: special gauge check */
            /* flag 1: with more than cValue stocks, add cAdd (signed) to them, clamped (a full stock empties
               the gauge); otherwise branch.  Without flag 1: branch with at least cValue stocks.  A branch
               to skill 0 takes a follow-up command instead */
            if (pStep->gs.cFlags & 1) {
                if (gkgtLoadedCharacter[gpkgtCurrentEngineObject->iPlayerIdx].iSpecialGaugeTokens > pStep->gs.cValue) {
                    gkgtLoadedCharacter[gpkgtCurrentEngineObject->iPlayerIdx].iSpecialGaugeTokens += iSubtractTwoFiftySixIfAboveOneTwentySeven(pStep->gs.cAdd);
                    if (gkgtLoadedCharacter[gpkgtCurrentEngineObject->iPlayerIdx].iSpecialGaugeTokens < 0)
                        gkgtLoadedCharacter[gpkgtCurrentEngineObject->iPlayerIdx].iSpecialGaugeTokens = 0;
                    if ((DWORD)gkgtLoadedCharacter[gpkgtCurrentEngineObject->iPlayerIdx].iSpecialGaugeTokens < (DWORD)gkgtLoadedCharacter[gpkgtCurrentEngineObject->iPlayerIdx].iSpecialStockMax)
                        goto next_step;
                    gkgtLoadedCharacter[gpkgtCurrentEngineObject->iPlayerIdx].iSpecialGaugeTokens = gkgtLoadedCharacter[gpkgtCurrentEngineObject->iPlayerIdx].iSpecialStockMax;
                    gkgtLoadedCharacter[gpkgtCurrentEngineObject->iPlayerIdx].iSpecialGauge = 0;
                    break;
                }
                gpkgtCurrentEngineObject->iSkillIdx = pStep->gs.wSkill;
                if (!gpkgtCurrentEngineObject->iSkillIdx) {
                    i = iHandlePlayerCommandSequence(gkgtLoadedCharacter[gpkgtCurrentEngineObject->iPlayerIdx].iLastCommandIdx);
                    if (i)
                        gpkgtCurrentEngineObject->iSkillIdx = i;
                }
                gpkgtCurrentEngineObject->iSkillScriptIdx = (WORD)pOwner->kgtCore.pSkillsAlloc[gpkgtCurrentEngineObject->iSkillIdx].shStartingStepIdx + pStep->gs.cStep - 1;
            } else {
                if (gkgtLoadedCharacter[gpkgtCurrentEngineObject->iPlayerIdx].iSpecialGaugeTokens < pStep->gs.cValue || !pStep->gs.wSkill)
                    goto next_step;
                gpkgtCurrentEngineObject->iSkillIdx = pStep->gs.wSkill;
                if (!gpkgtCurrentEngineObject->iSkillIdx) {
                    i = iHandlePlayerCommandSequence(gkgtLoadedCharacter[gpkgtCurrentEngineObject->iPlayerIdx].iLastCommandIdx);
                    if (i)
                        gpkgtCurrentEngineObject->iSkillIdx = i;
                }
                gpkgtCurrentEngineObject->iSkillScriptIdx = (WORD)pOwner->kgtCore.pSkillsAlloc[gpkgtCurrentEngineObject->iSkillIdx].shStartingStepIdx + pStep->gs.cStep - 1;
            }
            break;

        case 0x1a:      /* PS: player stop */
            /* stop the player (and the objects that follow it) for cSelf frames and the other active players
               for cOpp frames; each keeps its current input for the hit checks */
            if (gpkgtCurrentEngineObject->iObjectType == PLAYER_ENGINE_OBJECT && pStep->ps.cSelf) {
                gpkgtCurrentEngineObject->iOpponentDowntimeInFrames += pStep->ps.cSelf;
                i = gpkgtCurrentEngineObject->iPlayerIdx;
                gkgtLoadedCharacter[i].dwStoredInput = giInputBuffer[i][giInputBufferPos];
                gkgtLoadedCharacter[i].bUseStoredInput = 1;
                for (j = 0, pObj = gkgtEngineObjects; j < 1024; j++, pObj++) {
                    if (pObj->iJumpIdx == READ_SCRIPT && pObj->iPlayerIdx == i && (pObj->iFlags & 0x20000000))
                        pObj->iOpponentDowntimeInFrames += pStep->ps.cSelf;
                }
            }
            if (!pStep->ps.cOpp)
                goto next_step;
            for (iValue = 0; iValue < 8; iValue++) {  /* iValue: player index here */
                if (iValue == gpkgtCurrentEngineObject->iPlayerIdx || !gkgtLoadedCharacter[iValue].iOnlineState)
                    continue;
                gkgtLoadedCharacter[iValue].pkgtoSelf->iOpponentDowntimeInFrames += pStep->ps.cOpp;
                gkgtLoadedCharacter[iValue].dwStoredInput = giInputBuffer[gkgtLoadedCharacter[iValue].pkgtoSelf->iPlayerIdx][giInputBufferPos];
                gkgtLoadedCharacter[iValue].bUseStoredInput = 1;
                for (j = 0, pObj = gkgtEngineObjects; j < 1024; j++, pObj++) {
                    if (pObj->iJumpIdx == READ_SCRIPT && pObj->iPlayerIdx == gkgtLoadedCharacter[iValue].pkgtoSelf->iPlayerIdx && (pObj->iFlags & 0x20000000))
                        pObj->iOpponentDowntimeInFrames += pStep->ps.cOpp;
                }
            }
            break;

        case 0x1e:      /* C: cancel condition */
            *(kgtCancelBlock *)&pOwner->cCancelType = *(kgtCancelBlock *)pStep;  /* code, cCancelFlags, cCancelLevelMin, wCancelSkillIdx, cCancelLevelMax */
            break;

        case 0x1f:      /* V: variable */
            /* cVar / cSrc: bits 6-7 scope (0 object, 1 owner, 2 system), bits 0-5 index; cSrc scope 3 is a
               value: 0/1 position, 2/3 camera, 4/5 parent position (pixels), 6 game timer / 100, 7 round */
            switch (pStep->var.cVar >> 6) {
            case 0: pVar = &gpkgtCurrentEngineObject->shVarA + (pStep->var.cVar & 0x3f); break;
            case 1: pVar = &pOwner->shVarA + (pStep->var.cVar & 0x3f); break;
            case 2: pVar = &gshSystemVariables[pStep->var.cVar & 0x3f]; break;
            }
            if (pStep->var.cFlags & 0x80) {
                switch (pStep->var.cSrc >> 6) {
                case 0: shValue = (&gpkgtCurrentEngineObject->shVarA)[pStep->var.cSrc & 0x3f]; break;
                case 1: shValue = (&pOwner->shVarA)[pStep->var.cSrc & 0x3f]; break;
                case 2: shValue = gshSystemVariables[pStep->var.cSrc & 0x3f]; break;
                case 3:
                    switch (pStep->var.cSrc & 0x3f) {
                    case 0: shValue = gpkgtCurrentEngineObject->iPosX / 0x10000; break;
                    case 1: shValue = gpkgtCurrentEngineObject->iPosY / 0x10000; break;
                    case 2: shValue = giCameraX; break;
                    case 3: shValue = giCameraY; break;
                    case 4: shValue = gpkgtCurrentEngineObject->pParent->iPosX / 0x10000; break;
                    case 5: shValue = gpkgtCurrentEngineObject->pParent->iPosY / 0x10000; break;
                    case 6: shValue = gkgtGameState.iGameTimerInFrames / 100; break;
                    case 7: shValue = gkgtGameState.iCurrentRound; break;
                    }
                    break;
                }
            } else {
                shValue = pStep->var.shValue;
            }
            /* cFlags bits 0-1: 1 set, 2 add (clamped to +-30000); bits 2-3: branch if 1 equal to, 2 greater,
               3 less than shCompare; 0x80: the value comes from cSrc instead of shValue */
            switch (pStep->var.cFlags & 3) {
            case 1:     /* set */
                *pVar = shValue;
                break;
            case 2:     /* add */
                iValue = *pVar + shValue;
                if (iValue < -30000)
                    iValue = -30000;
                else if (iValue > 30000)
                    iValue = 30000;
                *pVar = iValue;
                break;
            }
            switch ((pStep->var.cFlags >> 2) & 3) {
            case 1:
                if (*pVar != pStep->var.shCompare)
                    goto next_step;
                break;
            case 2:
                if (*pVar <= pStep->var.shCompare)
                    goto next_step;
                break;
            case 3:
                if (*pVar >= pStep->var.shCompare)
                    goto next_step;
                break;
            default:
                goto next_step;
            }
            if (!pStep->var.wSkill)
                goto next_step;
            gpkgtCurrentEngineObject->iSkillIdx = pStep->var.wSkill;
            gpkgtCurrentEngineObject->iSkillScriptIdx = (WORD)pOwner->kgtCore.pSkillsAlloc[gpkgtCurrentEngineObject->iSkillIdx].shStartingStepIdx + pStep->var.cStep - 1;
            break;



        case 0x20:      /* RANDOM: branch when rand() % (wRange + 1) > wThreshold */
            if (rand() % (pStep->rnd.wRange + 1) <= pStep->rnd.wThreshold || !pStep->rnd.wSkill)
                goto next_step;
            gpkgtCurrentEngineObject->iSkillIdx = pStep->rnd.wSkill;
            gpkgtCurrentEngineObject->iSkillScriptIdx = (WORD)pOwner->kgtCore.pSkillsAlloc[gpkgtCurrentEngineObject->iSkillIdx].shStartingStepIdx + pStep->rnd.cStep - 1;
            break;

        case 0x23:      /* COL OBJ: colour */
            gpkgtCurrentEngineObject->iColorBlendtype = pStep->col.cBlend;
            gpkgtCurrentEngineObject->iColorRed = pStep->col.cRed;
            gpkgtCurrentEngineObject->iColorGreen = pStep->col.cGreen;
            gpkgtCurrentEngineObject->iColorBlue = pStep->col.cBlue;
            /* alpha only for blend type 4 */
            if (gpkgtCurrentEngineObject->iColorBlendtype == 4)
                gpkgtCurrentEngineObject->iColorAlpha = pStep->col.cAlpha;
            else
                gpkgtCurrentEngineObject->iColorAlpha = 0;
            break;

        case 0x24:      /* command branch */
            if (!process_COM_skillblock((kgtSkill *)pStep))
                goto next_step;
            gpkgtCurrentEngineObject->iSkillIdx = pStep->jump.wSkill;
            gpkgtCurrentEngineObject->iSkillScriptIdx = (WORD)pOwner->kgtCore.pSkillsAlloc[gpkgtCurrentEngineObject->iSkillIdx].shStartingStepIdx + pStep->jump.cStep - 1;
            break;

        case 0x25:      /* AI: after-image trail */
            /* a trail in use: length or interval 0 releases it; otherwise take a free one (of 100) */
            if (gpkgtCurrentEngineObject->cAfterImageIdx) {
                i = gpkgtCurrentEngineObject->cAfterImageIdx - 1;
                if (!pStep->ai.cLength || !pStep->ai.cInterval) {
                    gpkgtCurrentEngineObject->cAfterImageIdx = 0;
                    gAfterImageTrails[i].bInUse = 0;
                    break;
                }
            } else {
                for (i = 0; i < 100; i++) {
                    if (!gAfterImageTrails[i].bInUse) {
                        gAfterImageTrails[i].bInUse = 1;
                        gpkgtCurrentEngineObject->cAfterImageIdx = i + 1;
                        break;
                    }
                }
            }
            /* no free trail */
            if (i == 100) {
                iSetDebugInfo("\216c\221\234\213\226\227e\224\315\210\315\203I\201[\203o\201[", 0x4444ff);  /* 残像許容範囲オーバー */
                goto next_step;
            }
            gAfterImageTrails[i].iPos = 0;
            gAfterImageTrails[i].pStep = (kgtSkill *)pStep;
            gAfterImageTrails[i].iTimer = 0;
            memset(&gAfterImageTrails[i].aiData[4], 0, 400 * sizeof(int));  /* clear the 100 frames (kgtTrailFrame, 4 ints each) */


            break;
        }
next_step:
        /* on to the next step */
        gpkgtCurrentEngineObject->iSkillScriptIdx++;
    }
    return;

destroy:
    /* delete the object */
    vDeleteCurrentEngineObject();
}
