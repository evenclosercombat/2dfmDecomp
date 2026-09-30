/*
 * online.c - DirectPlay netplay (based on the DirectX 5 SDK DirectPlay samples), the first object
 * of the game (0x402470-0x403300).
 *
 * The netplay menu is never reachable in the shipped game (menu command 0xa29 is not in the
 * menu), but the code is linked in, and vOnlineBroadcastInput / iOnlineProcessReceivedMessages are
 * called every frame; with no session (gpDirectPlay NULL) they do nothing.
 *
 * Model: every peer creates one DirectPlay player and takes a gkgtLoadedCharacter slot for it
 * (iOnlineState 1); remote players get slots too (iOnlineState 2, dpidOnline = their DirectPlay
 * id).  Each frame the local input word is sent to everybody; a remote slot's iOnlineLag counts the
 * frames it still owes us, and the main loop waits while any remote peer is behind.  The netplay
 * messages below are exchanged besides; chat and status lines go to the debug log (debug.c).
 *
 * Only the TCP/IP service provider is offered; the host's IP address is shown in the window title
 * (guLocalIpAddr, from vGetLocalHostIpAddress through Winsock - which nothing calls, so it is 0).
 */
#define INITGUID
#include "kgt.h"

/* {AFD1FC20-AC5F-11D1-B41F-000001374734}: the game's DirectPlay application GUID */
DEFINE_GUID(guidKgt2kApp, 0xafd1fc20, 0xac5f, 0x11d1, 0xb4, 0x1f, 0x00, 0x00, 0x01, 0x37, 0x47, 0x34);

/* network messages (first byte of each message) */
#define NETMSG_INPUT    0       /* this frame's input word (sent unguaranteed, every frame) */
#define NETMSG_LEAVE    1       /* the sender leaves the session */
#define NETMSG_JOIN     2       /* the sender joined: its name and DirectPlay id */
#define NETMSG_HERE     3       /* answer to NETMSG_JOIN: "I am here" with the answerer's name */
#define NETMSG_CHAT     4       /* chat text */

#if defined(_WIN64)
/* 64-bit build: dplayx.dll is loaded when netplay starts up (iInitOnline) instead of being imported.
   64-bit Windows has a 64-bit dplayx.dll only with the optional DirectPlay feature, and llvm-mingw
   has no x86-64 import library for it; without it the provider list stays empty and the netplay
   dialog reports that DirectPlay could not be created. */
static HRESULT (WINAPI *gpfnDirectPlayEnumerateA)(LPDPENUMDPCALLBACKA, LPVOID);
static HRESULT (WINAPI *gpfnDirectPlayCreate)(LPGUID, LPDIRECTPLAY *, IUnknown *);

static void vLoadDirectPlay(void)
{
    HMODULE hDplay = LoadLibraryA("dplayx.dll");

    if (hDplay) {
        gpfnDirectPlayEnumerateA = (HRESULT (WINAPI *)(LPDPENUMDPCALLBACKA, LPVOID))(void (*)(void))GetProcAddress(hDplay, "DirectPlayEnumerateA");
        gpfnDirectPlayCreate = (HRESULT (WINAPI *)(LPGUID, LPDIRECTPLAY *, IUnknown *))(void (*)(void))GetProcAddress(hDplay, "DirectPlayCreate");
    }
}

#define DirectPlayEnumerateA(pfnCallback, pContext) \
    (gpfnDirectPlayEnumerateA ? gpfnDirectPlayEnumerateA(pfnCallback, pContext) : DPERR_UNAVAILABLE)
#define DirectPlayCreate(pGuid, ppDirectPlay, pUnknown) \
    (gpfnDirectPlayCreate ? gpfnDirectPlayCreate(pGuid, ppDirectPlay, pUnknown) : DPERR_UNAVAILABLE)
#endif

#pragma pack(push, 1)
typedef struct { BYTE bType; DWORD dwInput; } NETMSG_INPUT_T;               /* 5 bytes */
typedef struct { BYTE bType; char szName[32]; DPID dpid; } NETMSG_JOIN_T;   /* 37 bytes (also NETMSG_HERE) */
typedef struct { BYTE bType; char szText[64]; } NETMSG_CHAT_T;              /* 65 bytes */
#pragma pack(pop)

/*
 * Shows a task-modal error box with the caption "error".
 * szText: the message.
 */
void vOnlineShowError(LPCSTR szText)
{
    MessageBoxA(NULL, szText, "error", MB_TASKMODAL);
}

/*
 * Finds the player slot that belongs to a DirectPlay player.
 * dpid: the DirectPlay id.
 * Returns the slot (0-7), or -1.
 * Globals: reads gkgtLoadedCharacter[].dpidOnline.
 */
int iFindOnlinePlayerSlotById(DPID dpid)
{
    int iSlot;
    for (iSlot = 0; iSlot < 8; iSlot++) {
        if (gkgtLoadedCharacter[iSlot].dpidOnline == dpid)
            return iSlot;
    }
    return -1;
}

/*
 * Finds a player slot not used by netplay (dpidOnline 0).
 * Returns the slot (0-7), or -1.
 * Globals: reads gkgtLoadedCharacter[].dpidOnline.
 */
int iFindFreeOnlinePlayerSlot(void)
{
    int iSlot;
    for (iSlot = 0; iSlot < 8; iSlot++) {
        if (gkgtLoadedCharacter[iSlot].dpidOnline == 0)
            return iSlot;
    }
    return -1;
}

/*
 * Gives a free player slot to a DirectPlay player, as a remote peer (iOnlineState 2).
 * dpid: the DirectPlay id.
 * Returns the slot (0-7), or -1 when all are taken.
 * Globals: changes gkgtLoadedCharacter[slot].iOnlineState / .dpidOnline.
 */
int iAllocOnlinePlayerSlot(DPID dpid)
{
    int iSlot;
    kgt_character_struct *pChar;

    iSlot = iFindFreeOnlinePlayerSlot();
    if (iSlot == -1)
        return -1;
    pChar = gkgtLoadedCharacter + iSlot;
    pChar->iOnlineState = 2;
    pChar->dpidOnline = dpid;
    return iSlot;
}

/*
 * Sends a message to all players of the session, guaranteed delivery (no-op without a session).
 * pData, dwSize: the message.
 * Globals: reads gpDirectPlay, gdpidLocalPlayer.
 */
void vDpSendToAllGuaranteed(LPVOID pData, DWORD dwSize)
{
    if (gpDirectPlay)
        IDirectPlay2_Send(gpDirectPlay, gdpidLocalPlayer, DPID_ALLPLAYERS, DPSEND_GUARANTEED, pData, dwSize);
}

/*
 * Sends a message to one player, guaranteed delivery (no-op without a session).
 * dpidTo: the receiver; pData, dwSize: the message.
 * Globals: reads gpDirectPlay, gdpidLocalPlayer.
 */
void vDpSendToPlayerGuaranteed(DPID dpidTo, LPVOID pData, DWORD dwSize)
{
    if (gpDirectPlay)
        IDirectPlay2_Send(gpDirectPlay, gdpidLocalPlayer, dpidTo, DPSEND_GUARANTEED, pData, dwSize);
}

/*
 * Sends a message to all players of the session, unguaranteed (no-op without a session).
 * pData, dwSize: the message.
 * Globals: reads gpDirectPlay, gdpidLocalPlayer.
 */
void vDpSendToAll(LPVOID pData, DWORD dwSize)
{
    if (gpDirectPlay)
        IDirectPlay2_Send(gpDirectPlay, gdpidLocalPlayer, DPID_ALLPLAYERS, 0, pData, dwSize);
}

/*
 * Once per frame: sends this frame's input word to everybody (once for every slot in state 1,
 * normally just the local one) and counts one more frame owed by every remote slot (state 2).
 * dwInput: the local input word (giInputBuffer[0] of this frame).
 * Globals: reads gpDirectPlay, gkgtLoadedCharacter[].iOnlineState; changes
 * gkgtLoadedCharacter[].iOnlineLag.
 */
void vOnlineBroadcastInput(DWORD dwInput)
{
    NETMSG_INPUT_T inputMsg;
    int iSlot;

    if (gpDirectPlay) {
        for (iSlot = 0; iSlot < 8; iSlot++) {
            switch (gkgtLoadedCharacter[iSlot].iOnlineState) {
            case 1:
                inputMsg.bType = NETMSG_INPUT;
                inputMsg.dwInput = dwInput;
                vDpSendToAll(&inputMsg, sizeof(inputMsg));
                break;
            case 2:
                gkgtLoadedCharacter[iSlot].iOnlineLag++;
                break;
            }
        }
    }
}

/*
 * Handles all waiting DirectPlay messages: a sender without a slot gets one; input words go into
 * the sender slot's giInputBuffer row (its own position, a ring of 256 here) and pay off one frame
 * of its lag; join / here / leave / chat messages are reported in the debug log, and a join is
 * answered with a NETMSG_HERE.  System messages (from id 0) are skipped.
 * Returns the number of slots still owed input (iOnlineLag > 0); the main loop repeats the call
 * until it is 0.  Returns 0 without a session.
 * Globals: reads gpDirectPlay, giLocalOnlineSlot; changes gkgtLoadedCharacter[] (netplay fields),
 * giInputBuffer, the debug log.
 */
int iOnlineProcessReceivedMessages(void)
{
    DPID dpidFrom;
    DWORD dwSize;
    DPID dpidTo;
    int iLagging;
    int iPlayer;
    int iSender;
    NETMSG_JOIN_T hereMsg;
    char szLeft[256];
    char szJoined[256];
    char szChat[256];
    char szHere[256];
    BYTE cMsg[1024];

    /* NB: dwSize is only set once; each Receive lowers it to the size of the message read, so a
       later, longer message does not fit (DPERR_BUFFERTOOSMALL ends the loop) */
    dwSize = 1024;
    iLagging = 0;
    if (gpDirectPlay == NULL)
        return 0;

    while (IDirectPlay2_Receive(gpDirectPlay, &dpidFrom, &dpidTo, DPRECEIVE_ALL, cMsg, &dwSize) == DP_OK) {
        if (dpidFrom == 0)
            continue;       /* system message */
        /* the sender's slot (a new peer gets one) */
        iSender = iFindOnlinePlayerSlotById(dpidFrom);
        if (iSender == -1)
            iSender = iAllocOnlinePlayerSlot(dpidFrom);
        switch (cMsg[0]) {
        case NETMSG_INPUT:
            /* its next input word; one frame less owed */
            gkgtLoadedCharacter[iSender].iInputBufferPos = (gkgtLoadedCharacter[iSender].iInputBufferPos + 1) & 0xff;
            giInputBuffer[iSender][gkgtLoadedCharacter[iSender].iInputBufferPos] = *(DWORD *)&cMsg[1];
            gkgtLoadedCharacter[iSender].iOnlineLag--;
            break;
        case NETMSG_JOIN:
            sprintf(szJoined, "%s \202\263\202\361\202\252\223\374\202\301\202\304\202\253\202\334\202\265\202\275\202\346", &cMsg[1]);  /* %s さんが入ってきましたよ (%s has come in) */
            iSetDebugInfo(szJoined, 0xff00);
            /* answer with our (local slot's character) name; hereMsg.dpid is left unset */
            hereMsg.bType = NETMSG_HERE;
            memcpy(hereMsg.szName, gkgtLoadedCharacter[giLocalOnlineSlot].kgtCore.szName, 32);
            vDpSendToPlayerGuaranteed(dpidFrom, &hereMsg, sizeof(hereMsg));
            gkgtLoadedCharacter[iSender].iOnlineLag = 0;
            gkgtLoadedCharacter[iSender].iOnlineState = 2;
            break;
        case NETMSG_HERE:
            sprintf(szHere, "%s \202\263\202\361\202\252\202\242\202\351\202\346", &cMsg[1]);  /* %s さんがいるよ (%s is here) */
            iSetDebugInfo(szHere, 0xff00ff);
            gkgtLoadedCharacter[iSender].iOnlineLag = 0;
            gkgtLoadedCharacter[iSender].iOnlineState = 2;
            break;
        case NETMSG_LEAVE:
            sprintf(szLeft, "%s \202\263\202\361\202\252\224\262\202\257\202\275\202\346", gkgtLoadedCharacter[iSender].kgtCore.szName);  /* %s さんが抜けたよ (%s has left) */
            iSetDebugInfo(szLeft, 0xff);
            /* the slot stays active but is no longer waited for */
            gkgtLoadedCharacter[iSender].iOnlineState = 1;
            gkgtLoadedCharacter[iSender].iOnlineLag = 0;
            break;
        case NETMSG_CHAT:
            sprintf(szChat, "%s:%s", gkgtLoadedCharacter[iSender].kgtCore.szName, &cMsg[1]);
            iSetDebugInfo(szChat, 0xffff);
            break;
        }
    }

    /* how many peers are still behind */
    for (iPlayer = 0; iPlayer < 8; iPlayer++) {
        if (gkgtLoadedCharacter[iPlayer].iOnlineLag > 0)
            iLagging++;
    }
    return iLagging;
}

/*
 * Sends a chat line to everybody and shows it in the local debug log as "<name>:<text>" (not
 * called).
 * szText: the text (64 bytes are sent).
 * Globals: reads gkgtLoadedCharacter[0].kgtCore.szName.
 */
void vSendOnlineChatMessage(char *szText)
{
    NETMSG_CHAT_T chatMsg;
    char szLine[256];

    chatMsg.bType = NETMSG_CHAT;
    memcpy(chatMsg.szText, szText, 64);
    vDpSendToAllGuaranteed(&chatMsg, sizeof(chatMsg));
    sprintf(szLine, "%s:%s", gkgtLoadedCharacter[0].kgtCore.szName, szText);
    iSetDebugInfo(szLine, 0xffff);
}

/*
 * DirectPlayEnumerate callback: records the TCP/IP service provider (the only one kept), up to 20.
 * pGuidSp: the provider's GUID; szSpName: its name; dwMajorVersion, dwMinorVersion, pContext:
 * unused.
 * Returns TRUE to continue the enumeration, FALSE when the table is full.
 * Globals: changes gszServiceProviderNames, gServiceProviderGuids, giNumServiceProviders.
 */
BOOL FAR PASCAL bDirectPlayEnumerateCallback(LPGUID pGuidSp, LPSTR szSpName, DWORD dwMajorVersion, DWORD dwMinorVersion, LPVOID pContext)
{
    if (giNumServiceProviders >= 20)
        return FALSE;
    if (IsEqualGUID(pGuidSp, &DPSPGUID_TCPIP)) {
        strcpy(gszServiceProviderNames[giNumServiceProviders], szSpName);
        gServiceProviderGuids[giNumServiceProviders++] = *pGuidSp;
    }
    return TRUE;
}

/*
 * Lists the DirectPlay service providers (only TCP/IP is kept, see above).
 * Returns 1.
 * Globals: changes gServiceProviderGuids, gszServiceProviderNames, giNumServiceProviders.
 */
int iEnumDirectPlayServiceProviders(void)
{
    ZeroMemory(gServiceProviderGuids, sizeof(gServiceProviderGuids));
    ZeroMemory(gszServiceProviderNames, sizeof(gszServiceProviderNames));
    giNumServiceProviders = 0;
    DirectPlayEnumerateA(bDirectPlayEnumerateCallback, NULL);   /* DirectPlayEnumerate in the DirectX SDK's dplay.h is this macro; mingw-w64's has only the A/W names */
    return 1;
}

/*
 * Netplay start-up (from vInitializeWindowsAndMemory): sets the application GUID and lists the
 * service providers.
 * Returns 1.
 * Globals: changes gpAppGuid (and see iEnumDirectPlayServiceProviders).
 */
int iInitOnline(void)
{
    gpAppGuid = (LPGUID)&guidKgt2kApp;
#if defined(_WIN64)
    vLoadDirectPlay();
#endif
    iEnumDirectPlayServiceProviders();
    return 1;
}

/*
 * Leaves the session (at exit): tells the others (NETMSG_LEAVE) and releases DirectPlay.
 * Returns 1.
 * Globals: changes gpDirectPlay.
 */
int iOnlineCloseSession(void)
{
    BYTE cMsg;

    if (gpDirectPlay) {
        cMsg = NETMSG_LEAVE;
        vDpSendToAllGuaranteed(&cMsg, 1);
        IDirectPlay2_Release(gpDirectPlay);
        gpDirectPlay = NULL;
    }
    return 1;
}

/*
 * EnumSessions callback: records each session found (name and instance GUID), up to 20.
 * pSessionDesc: the session; pdwTimeOut, pContext: unused; dwFlags: DPESC_TIMEDOUT ends the
 * enumeration.
 * Returns TRUE to go on, FALSE when timed out or the table is full.
 * Globals: changes gszSessionNames, gSessionGuids, giNumSessionsFound.
 */
BOOL FAR PASCAL bEnumSessionsCallback(LPCDPSESSIONDESC2 pSessionDesc, LPDWORD pdwTimeOut, DWORD dwFlags, LPVOID pContext)
{
    if (dwFlags & DPESC_TIMEDOUT)
        return FALSE;
    strcpy(gszSessionNames[giNumSessionsFound], pSessionDesc->lpszSessionNameA);
    gSessionGuids[giNumSessionsFound++] = pSessionDesc->guidInstance;
    return giNumSessionsFound < 20;
}

/*
 * Searches for open sessions of this game (synchronous, default timeout).  The names of an earlier
 * search are not cleared, only giNumSessionsFound.
 * Returns 0.
 * Globals: reads gpDirectPlay, gpAppGuid; changes giNumSessionsFound (and see
 * bEnumSessionsCallback).
 */
int iOnlineEnumSessions(void)
{
    DPSESSIONDESC2 sessionDesc;

    ZeroMemory(&sessionDesc, sizeof(sessionDesc));
    sessionDesc.dwSize = sizeof(sessionDesc);
    sessionDesc.guidApplication = *gpAppGuid;
    giNumSessionsFound = 0;
    IDirectPlay2_EnumSessions(gpDirectPlay, &sessionDesc, 0, bEnumSessionsCallback, NULL, DPENUMSESSIONS_AVAILABLE);
    return 0;
}

/*
 * After hosting or joining: tells everybody we joined (NETMSG_JOIN with the player name and our
 * DirectPlay id) and sets the window title to
 * "kgt2k  Game:<session>  Player:<local slot's character name>  IP:a.b.c.d  <host name>".
 * szSessionName: the session's name.
 * Globals: reads gdpidLocalPlayer, gszPlayerName, giLocalOnlineSlot, guLocalIpAddr (network byte
 * order, hence ntohl), gszLocalHostName, ghWnd.
 */
void vOnlineAnnouncePlayerAndSetTitle(char *szSessionName)
{
    NETMSG_JOIN_T joinMsg;
    char szTitle[256];

    joinMsg.bType = NETMSG_JOIN;
    joinMsg.dpid = gdpidLocalPlayer;
    memcpy(joinMsg.szName, gszPlayerName, 32);
    vDpSendToAllGuaranteed(&joinMsg, sizeof(joinMsg));
    sprintf(szTitle, "kgt2k  Game:%s  Player:%s  IP:%02d.%02d.%02d.%02d  %s", szSessionName,
            gkgtLoadedCharacter[giLocalOnlineSlot].kgtCore.szName,
            (int)(ntohl(guLocalIpAddr) >> 24), (int)((ntohl(guLocalIpAddr) >> 16) & 0xff),
            (int)((ntohl(guLocalIpAddr) >> 8) & 0xff), (int)(ntohl(guLocalIpAddr) & 0xff), gszLocalHostName);
    SetWindowTextA(ghWnd, szTitle);
}

/*
 * Hosts a new session named gszSessionName (up to 50 players, host migration, keep-alive), creates
 * the local DirectPlay player ("Host", long name gszPlayerName) and gives it a slot in state 1.
 * Returns 0 (also on success).
 * Globals: reads gpDirectPlay, gpAppGuid, gszSessionName, gszPlayerName; changes gDpName,
 * gdpidLocalPlayer, giLocalOnlineSlot, gdpidHost, the slot's netplay fields, the window title.
 */
int iOnlineHostSession(void)
{
    DPSESSIONDESC2 sessionDesc;
    HRESULT hr;

    /* create the session */
    ZeroMemory(&sessionDesc, sizeof(sessionDesc));
    sessionDesc.dwSize = sizeof(sessionDesc);
    sessionDesc.dwFlags = DPSESSION_MIGRATEHOST | DPSESSION_KEEPALIVE;
    sessionDesc.guidApplication = *gpAppGuid;
    sessionDesc.dwMaxPlayers = 50;
    sessionDesc.lpszSessionNameA = gszSessionName;
    hr = IDirectPlay2_Open(gpDirectPlay, &sessionDesc, DPOPEN_CREATE);
    if (hr != DP_OK) {
        if (hr != DPERR_USERCANCEL) {
            vOnlineShowError("\220V\213K\203Q\201[\203\200\215\354\220\254\216\270\224s\201i\202s\201Q\202s\201j");  /* 新規ゲーム作成失敗（Ｔ＿Ｔ） (creating a new game failed) */
            return 0;
        }
        return 0;
    }
    /* our DirectPlay player */
    ZeroMemory(&gDpName, sizeof(gDpName));
    gDpName.dwSize = sizeof(gDpName);
    gDpName.lpszShortNameA = "Host";
    gDpName.lpszLongNameA = gszPlayerName;
    if (IDirectPlay2_CreatePlayer(gpDirectPlay, &gdpidLocalPlayer, &gDpName, NULL, NULL, 0, 0) != DP_OK) {
        vOnlineShowError("\202\257\202\307\201A\203v\203\214\203C\203\204\201[\215\354\202\352\202\310\202\251\202\301\202\275\202\346\201i\202s\201Q\202s\201j");  /* けど、プレイヤー作れなかったよ（Ｔ＿Ｔ） (but the player could not be created) */
        return 0;
    }
    /* and its slot (a failure is only reported; slot -1 is then used anyway) */
    giLocalOnlineSlot = iAllocOnlinePlayerSlot(gdpidLocalPlayer);
    if (giLocalOnlineSlot == -1)
        vOnlineShowError("\203L\203\203\203\211\215\354\220\254\216\270\224s\202\265\202\275\202\346\201E\201E\201E\202\334\202\240\202\242\202\242\202\251");  /* キャラ作成失敗したよ・・・まあいいか (creating the character failed... oh well) */
    vOnlineAnnouncePlayerAndSetTitle(gszSessionName);
    gdpidHost = gdpidLocalPlayer;
    gkgtLoadedCharacter[giLocalOnlineSlot].iOnlineState = 1;
    return 0;
}

/*
 * Joins session iSession of the last search: searches again (to refresh DirectPlay's list), opens
 * the session by its instance GUID, creates the local DirectPlay player ("Join", long name
 * gszPlayerName) and gives it a slot in state 1.
 * iSession: index into gSessionGuids / gszSessionNames.
 * Returns 1 on success, 0 on failure (after an error box, except when the user cancelled).
 * Globals: reads gpDirectPlay, gpAppGuid, gSessionGuids, gszSessionNames, gszPlayerName; changes
 * the session list, gDpName, gdpidLocalPlayer, giLocalOnlineSlot, gdpidHost (0: not the host),
 * the slot's netplay fields, the window title.
 */
int iOnlineJoinSession(int iSession)
{
    DPSESSIONDESC2 sessionDesc;
    HRESULT hr;

    /* search again */
    ZeroMemory(&sessionDesc, sizeof(sessionDesc));
    sessionDesc.dwSize = sizeof(sessionDesc);
    sessionDesc.guidApplication = *gpAppGuid;
    IDirectPlay2_EnumSessions(gpDirectPlay, &sessionDesc, 0, bEnumSessionsCallback, NULL, DPENUMSESSIONS_AVAILABLE);
    if (giNumSessionsFound == 0) {
        vOnlineShowError("\203Q\201[\203\200\203Z\203b\203V\203\207\203\223\202\252\214\251\202\302\202\251\202\347\202\310\202\251\202\301\202\275\202\346");  /* ゲームセッションが見つからなかったよ (no game session was found) */
        return 0;
    }
    /* open the chosen session */
    ZeroMemory(&sessionDesc, sizeof(sessionDesc));
    sessionDesc.dwSize = sizeof(sessionDesc);
    sessionDesc.guidInstance = gSessionGuids[iSession];
    hr = IDirectPlay2_Open(gpDirectPlay, &sessionDesc, DPOPEN_JOIN);
    if (hr != DP_OK) {
        if (hr != DPERR_USERCANCEL)
            vOnlineShowError("\220\332\221\261\202\311\216\270\224s\202\265\202\275\202\346\201i\202s\201Q\202s\201j");  /* 接続に失敗したよ（Ｔ＿Ｔ） (the connection failed) */
        return 0;
    }
    /* our DirectPlay player */
    ZeroMemory(&gDpName, sizeof(gDpName));
    gDpName.dwSize = sizeof(gDpName);
    gDpName.lpszShortNameA = "Join";
    gDpName.lpszLongNameA = gszPlayerName;
    if (IDirectPlay2_CreatePlayer(gpDirectPlay, &gdpidLocalPlayer, &gDpName, NULL, NULL, 0, 0) != DP_OK) {
        vOnlineShowError("\202\257\202\307\201A\203v\203\214\203C\203\204\201[\215\354\202\352\202\310\202\251\202\301\202\275\202\346\201i\202s\201Q\202s\201j");  /* けど、プレイヤー作れなかったよ（Ｔ＿Ｔ） (but the player could not be created) */
        return 0;
    }
    /* and its slot */
    giLocalOnlineSlot = iAllocOnlinePlayerSlot(gdpidLocalPlayer);
    if (giLocalOnlineSlot == -1)
        vOnlineShowError("\203L\203\203\203\211\215\354\220\254\216\270\224s\202\265\202\275\202\346\201E\201E\201E\202\334\202\240\202\242\202\242\202\251");  /* キャラ作成失敗したよ・・・まあいいか (creating the character failed... oh well) */
    vOnlineAnnouncePlayerAndSetTitle(gszSessionNames[iSession]);
    gdpidHost = 0;
    gkgtLoadedCharacter[giLocalOnlineSlot].iOnlineState = 1;
    return 1;
}

/* controls of dialog 101 (the code uses the numbers; these names are not referenced) */
#define IDC_SESSIONS        1001    /* list box of found sessions (0x3e9) */
#define IDC_STATUS          1005    /* group box whose caption shows the search status (0x3ed) */
#define IDC_SEARCH          1002    /* search button (0x3ea; not in the dialog template, sent by WM_INITDIALOG) */
#define IDC_HOST            1006    /* "host" button (0x3ee) */
#define IDC_JOIN            1007    /* "join" button (0x3ef) */
#define IDC_SESSIONNAME     1008    /* edit box: session name to host (0x3f0) */
#define IDC_PLAYERNAME      1009    /* edit box: player name (0x3f1) */

/*
 * Dialog procedure of the netplay dialog (resource 101).  WM_INITDIALOG creates the DirectPlay
 * object (IDirectPlay2A through a DirectPlay 1 object, on the first service provider found, i.e.
 * TCP/IP) and starts a search; the buttons search, host a session or join the selected one.
 * hDlg, uMsg, wParam, lParam: the usual.
 * Returns FALSE (the messages are all treated as not handled).
 * Globals: changes gpDirectPlay, gszPlayerName, gszSessionName (and what the session functions
 * change).
 */
INT_PTR CALLBACK iOnlineDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    LRESULT iSelection;
    int iSession;
    char *iStackPad1;  /* unused */

    switch (uMsg) {
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case 0x3ef:
            /* IDC_JOIN: join the session selected in IDC_SESSIONS */
            iSelection = SendDlgItemMessageA(hDlg, 0x3e9, LB_GETCURSEL, 0, 0);
            if (iSelection < 0 || !gszSessionNames[iSelection][0]) {
                MessageBoxA(hDlg, "\214\237\215\365\202\265\202\304\203^\203C\203g\203\213\202\360\216w\222\350\202\265\202\304\202\255\202\276\202\263\202\242", NULL, 0);  /* 検索してタイトルを指定してください (search, then choose a title) */
                return FALSE;
            }
            GetDlgItemTextA(hDlg, 0x3f1, gszPlayerName, 32);
            if (!iOnlineJoinSession((int)iSelection)) {
                MessageBoxA(hDlg, "\216Q\211\301\202\311\216\270\224s\202\265\202\334\202\265\202\275\201B\220\335\222\350\202\360\212m\224F\202\265\202\304\211\272\202\263\202\242\201B", NULL, 0);  /* 参加に失敗しました。設定を確認して下さい。 (joining failed; check the settings) */
                return FALSE;
            }
            EndDialog(hDlg, 1);
            return FALSE;
        case 0x3ea:
            /* IDC_SEARCH: list the open sessions */
            if (gpDirectPlay == NULL) {
                MessageBoxA(hDlg, "\220\332\221\261\225\373\226@\202\360\221I\202\361\202\305\202\255\202\276\202\263\202\242\201B", NULL, 0);  /* 接続方法を選んでください。 (choose a connection method) */
                return FALSE;
            }
            SendDlgItemMessageA(hDlg, 0x3e9, LB_RESETCONTENT, 0, 0);
            SendDlgItemMessageA(hDlg, 0x3ed, WM_SETTEXT, 0, (LPARAM)"\214\237\215\365\222\206");  /* 検索中 (searching) */
            iOnlineEnumSessions();
            SendDlgItemMessageA(hDlg, 0x3e9, LB_SETCURSEL, 0, 0);
            for (iSession = 0; iSession < 20; iSession++) {
                if (gszSessionNames[iSession][0]) {
                    SendDlgItemMessageA(hDlg, 0x3e9, LB_ADDSTRING, 0, (LPARAM)gszSessionNames[iSession]);
                    SendDlgItemMessageA(hDlg, 0x3e9, LB_SETITEMDATA, 0, iSession);
                }
            }
            /* nothing found: disable join and the list */
            if (giNumSessionsFound == 0) {
                SendDlgItemMessageA(hDlg, 0x3ed, WM_SETTEXT, 0, (LPARAM)"\203Q\201[\203\200\203Z\203b\203V\203\207\203\223\202\252\214\251\202\302\202\251\202\347\202\310\202\251\202\301\202\275\202\346");  /* ゲームセッションが見つからなかったよ (no game session was found) */
                EnableWindow(GetDlgItem(hDlg, 0x3ef), FALSE);
                EnableWindow(GetDlgItem(hDlg, 0x3e9), FALSE);
                return FALSE;
            }
            SendDlgItemMessageA(hDlg, 0x3ed, WM_SETTEXT, 0, (LPARAM)"\212\371\221\266\202\314\203Q\201[\203\200\202\311\216Q\211\301\202\267\202\351");  /* 既存のゲームに参加する (join an existing game) */
            EnableWindow(GetDlgItem(hDlg, 0x3ef), TRUE);
            EnableWindow(GetDlgItem(hDlg, 0x3e9), TRUE);
            return FALSE;
        case IDCANCEL:
            EndDialog(hDlg, 0);
            return FALSE;
        case 0x3ee:
            /* IDC_HOST: host a session with the names typed in */
            if (gpDirectPlay == NULL) {
                MessageBoxA(hDlg, "\202\334\202\270\220\332\221\261\225\373\226@\202\360\221I\202\361\202\305\202\255\202\276\202\263\202\242\201B", NULL, 0);  /* まず接続方法を選んでください。 (first choose a connection method) */
                return FALSE;
            }
            GetDlgItemTextA(hDlg, 0x3f1, gszPlayerName, 32);
            GetDlgItemTextA(hDlg, 0x3f0, gszSessionName, 32);
            iOnlineHostSession();
            EndDialog(hDlg, 1);
            return FALSE;
        }
        break;

    case WM_INITDIALOG:
        /* join and the session list stay disabled until a search finds something; the names come
           from kgt2k.INI; 0x3e8 (a connection list) is not in the template */
        EnableWindow(GetDlgItem(hDlg, 0x3ef), FALSE);
        EnableWindow(GetDlgItem(hDlg, 0x3e9), FALSE);
        SetDlgItemTextA(hDlg, 0x3f1, gszPlayerName);
        SetDlgItemTextA(hDlg, 0x3f0, gszSessionName);
        SendDlgItemMessageA(hDlg, 0x3e9, LB_SETCURSEL, 0, 0);
        SendDlgItemMessageA(hDlg, 0x3e8, LB_SETCURSEL, 0, 0);
        {
        LPDIRECTPLAY pDirectPlay1 = NULL;
        LPDIRECTPLAY2A pDirectPlay2 = NULL;
        /* a fresh DirectPlay object on the first service provider (TCP/IP) */
        if (gpDirectPlay) {
            IDirectPlay2_Release(gpDirectPlay);
            gpDirectPlay = NULL;
        }
        if (DirectPlayCreate(gServiceProviderGuids, &pDirectPlay1, NULL) != DP_OK) {
            vOnlineShowError("DirectPlay\202\252\215\354\220\254\202\305\202\253\202\310\202\251\202\301\202\275");  /* DirectPlayが作成できなかった (DirectPlay could not be created) */
            return FALSE;
        }
        if (IDirectPlay_QueryInterface(pDirectPlay1, &IID_IDirectPlay2A, (LPVOID *)&pDirectPlay2) != DP_OK) {
            vOnlineShowError("DirectPlay\202\252\215\354\220\254\202\305\202\253\202\310\202\251\202\301\202\275");  /* DirectPlayが作成できなかった (DirectPlay could not be created) */
            IDirectPlay_Release(pDirectPlay1);
            return FALSE;
        }
        gpDirectPlay = pDirectPlay2;
        IDirectPlay_Release(pDirectPlay1);
        /* and search right away (IDC_SEARCH) */
        SendMessageA(hDlg, WM_COMMAND, 0x3ea, 0);
        }
        break;
    }
    return FALSE;
}

/*
 * Runs the netplay dialog (menu command 0xa29, see vHandleWmCommand; not in the shipped menu).
 * hInstance: the module (the template, dialog 101, is compiled in by resources.c on this branch);
 * hWnd: the owner window.
 */
void vSpawnOnlineDialog(HINSTANCE hInstance, HWND hWnd)
{
    iEmbeddedDialogBoxParamA(hInstance, MAKEINTRESOURCE(101), hWnd, iOnlineDlgProc, 0);
}

/*
 * Looks up the local host name and its first IP address through Winsock 1.1 (not called, so
 * guLocalIpAddr stays 0).  NB: a failing gethostbyname (NULL) is not checked.
 * Globals: changes gszLocalHostName, gpLocalHostEnt, guLocalIpAddr (network byte order).
 */
void vGetLocalHostIpAddress(void)
{
    WSADATA wsaData;

    gszLocalHostName[0] = 0;
    gpLocalHostEnt = NULL;
    guLocalIpAddr = 0;
    if (WSAStartup(0x101, &wsaData) == 0) {
        if (gethostname(gszLocalHostName, 256) == 0) {
            gpLocalHostEnt = gethostbyname(gszLocalHostName);
            guLocalIpAddr = *(u_long *)gpLocalHostEnt->h_addr_list[0];
        }
    }
}
