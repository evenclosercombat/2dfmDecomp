/*
 * globals.c - initialized game-wide data with no code of its own (.data 0x41e2f0-0x41e40b;
 * the second object, linked after online.c).
 *
 * gpsScriptCommandNames is the editor's list of names for the script's conditions, values and
 * commands (unused by the game itself); the comments give the Shift-JIS text and a translation.
 * Its one empty entry was a "" literal, which VC6 places in .bss - at 0x424748, in the middle of the
 * uninitialized data defined in asm/game_bss.txt - so it is written as that bss symbol here.
 */
#include "kgt.h"

extern char gszEmptyCommandName[4];         /* 0x424748: "" literal of globals.c (gpsScriptCommandNames[9]), in .bss */

int giFrameMs = 40;  /* 0x41e2f0: tick period in ms; vGameLoop sets it to 10 at start-up (100 ticks per second), the 40 is never used */
/* 0x41e2f4: caption of the warning boxes (vSpawnTaskModalWithWarning) */
char *gpsContact = "\230A\227\215";  /* 連絡 (notice) */
char gszFileSignatures[2][8] = { "2DKGT2G", "2DKGT2K" };  /* 0x41e2f8: file signatures: [0] system/demo/stage, [1] character */
char *gpsScriptCommandNames[60] = {  /* 0x41e308: the editor's script command names (unused by the game) */
    "\226\263\202\265",  /* 無し (none) */
    "\222n\217\343\202\311\202\242\202\351\202\251\201H",  /* 地上にいるか？ (on the ground?) */
    "\227\247\202\301\202\304\202\242\202\351\202\251\201H",  /* 立っているか？ (standing?) */
    "\202\265\202\341\202\252\202\361\202\305\202\242\202\351\202\251\201H",  /* しゃがんでいるか？ (crouching?) */
    "\221O\225\373\226\312\202\311\223\374\227\315\202\263\202\352\202\304\202\242\202\351\202\251\201H",  /* 前方面に入力されているか？ (forward being input?) */
    "\214\343\225\373\226\312\202\311\223\374\227\315\202\263\202\352\202\304\202\242\202\351\202\251\201H",  /* 後方面に入力されているか？ (back being input?) */
    "\217\343\225\373\226\312\202\311\223\374\227\315\202\263\202\352\202\304\202\242\202\351\202\251\201H",  /* 上方面に入力されているか？ (up being input?) */
    "\211\272\225\373\226\312\202\311\223\374\227\315\202\263\202\352\202\304\202\242\202\351\202\251\201H",  /* 下方面に入力されているか？ (down being input?) */
    "\203\214\203o\201[\203j\203\205\201[\203g\203\211\203\213\202\251\201H",  /* レバーニュートラルか？ (stick in neutral?) */
    gszEmptyCommandName,
    "\202w\215\300\225W",  /* Ｘ座標 (X position) */
    "\202x\215\300\225W",  /* Ｙ座標 (Y position) */
    "\202l\202`\202o\202w\215\300\225W",  /* ＭＡＰＸ座標 (map X position) */
    "\202l\202`\202o\202x\215\300\225W",  /* ＭＡＰＹ座標 (map Y position) */
    "\220e\202w\215\300\225W",  /* 親Ｘ座標 (parent X position) */
    "\220e\202x\215\300\225W",  /* 親Ｙ座標 (parent Y position) */
    "\203^\203C\203\200",  /* タイム (time) */
    "\203\211\203E\203\223\203h\220\224",  /* ラウンド数 (round number) */
    "\217\211\212\372\220\335\222\350",  /* 初期設定 (initial settings) */
    "\215\300\225W\210\332\223\256",  /* 座標移動 (move) */
    "\217\360\214\217\225\252\212\362:\213Z\222\206\227L\214\370",  /* 条件分岐:技中有効 (conditional branch: valid during a skill) */
    "\203T\203E\203\223\203h",  /* サウンド (sound) */
    "\203I\203u\203W\203F\203N\203g\224\255\220\266",  /* オブジェクト発生 (create object) */
    "\217I\227\271",  /* 終了 (end) */
    "\221\314\227\315\203`\203F\203b\203N",  /* 体力チェック (life check) */
    "\215\300\225W\201E\211\346\221\234\225\317\215X",  /* 座標・画像変更 (position / image change) */
    "\223\212\202\260",  /* 投げ (throw) */
    "\203\213\201[\203v",  /* ループ (loop) */
    "\210\332\223\256",  /* 移動 (go to) */
    "\214\304\217o",  /* 呼出 (call) */
    "\211\346\221\234\214\304\217o",  /* 画像呼出 (image call) */
    "\215\300\225W",  /* 座標 (position) */
    "\203p\203\214\203b\203g\203A\203j\203\201\201E\227h\202\347\202\265",  /* パレットアニメ・揺らし (palette animation / shake) */
    "\203v\203\214\201[\203\204\201[\201F\223\301\216\352\214\370\211\312",  /* プレーヤー：特殊効果 (player: special effect) */
    "\203X\203y\203V\203\203\203\213\203Q\201[\203W\203`\203F\203b\203N",  /* スペシャルゲージチェック (special gauge check) */
    "\203\211\203C\203t\203Q\201[\203W\203`\203F\203b\203N",  /* ライフゲージチェック (life gauge check) */
    "\223\301\216\352\203L\203\203\203\211:\213\337\202\255\202\311\202\242\202\351\202\251",  /* 特殊キャラ:近くにいるか (special character: is it near?) */
    "\223\301\216\352\203L\203\203\203\211:\216i\227\337",  /* 特殊キャラ:司令 (special character: order) */
    "\221\212\216\350\201F\203X\203N\203\212\203v\203g\225\317\215X",  /* 相手：スクリプト変更 (opponent: script change) */
    "\203Q\201[\203W\225\317\215X",  /* ゲージ変更 (gauge change) */
    "\217\360\214\217\225\252\212\362:\212\356\226{\225\252\212\362",  /* 条件分岐:基本分岐 (conditional branch: basic branch) */
    "\203\212\203A\203N\203V\203\207\203\223",  /* リアクション (reaction) */
    "\215U\214\202\230g",  /* 攻撃枠 (attack box) */
    "\226h\214\344\230g",  /* 防御枠 (guard box) */
    "\203v\203\214\201[\203\204\201[\201F\203X\203g\203b\203v",  /* プレーヤー：ストップ (player: stop) */
    "\203V\203X\203e\203\200\203I\203u\203W\203F\203N\203g",  /* システムオブジェクト (system object) */
    "\203p\203b\203h\220U\223\256",  /* パッド振動 (pad vibration) */
    "\222\264\213Z\224w\214i",  /* 超技背景 (super move background) */
    "\203L\203\203\203\223\203Z\203\213\217\360\214\217",  /* キャンセル条件 (cancel conditions) */
    "\225\317\220\224\202\311\202\346\202\351\225\252\212\362",  /* 変数による分岐 (branch by variable) */
    "\203\211\203\223\203_\203\200\225\252\212\362",  /* ランダム分岐 (random branch) */
    "\203X\203e\201[\203W\212\356\226{\220\335\222\350",  /* ステージ基本設定 (stage basic settings) */
    "\203f\203\202\212\356\226{\220\335\222\350",  /* デモ基本設定 (demo basic settings) */
    "\220F\225\317\215X",  /* 色変更 (colour change) */
    "\203R\203}\203\223\203h\223\374\227\315\225\252\212\362",  /* コマンド入力分岐 (command input branch) */
    "\216c\221\234",  /* 残像 (after-image) */
    "\203X\203e\201[\203W\225\\\216\246\215\300\225W\220\335\222\350",  /* ステージ表示座標設定 (stage display position settings) */
    "\225\\\216\246\216\236\212\324",  /* 表示時間 (display time) */
    "\203q\203b\203g\225\\\216\246\220\335\222\350",  /* ヒット表示設定 (hit display settings) */
    "\217I\227\271",  /* 終了 (end) */
};
int giLocalOnlineSlot = -1;  /* 0x41e3f8: gkgtLoadedCharacter slot of the local player, -1 = none */
int giKeyRepeatDelay = 50;  /* 0x41e3fc: auto-repeat delay in frames */
int giKeyRepeatRate = 5;  /* 0x41e400: auto-repeat interval in frames */
int giCdTrack = -1;  /* 0x41e404: CD track to play, -1 = none */
char gszCdDrive[] = "d:";  /* 0x41e408: "X:" drive of the CD (d: is tried first, see vLoadsoundFromDisc) */
