#include "compat_types.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "gx.h"
#include "font.h"
#include "menu.h"
#include "charselect.h"
#include "player.h"
#include "record.h"
#include "options.h"
#include "input.h"
#include "custom_helpers.h"
#include "sound.h"
#include "sen.h"
#include "scene.h"
#include "levelselect.h"

/* =====================================================================
 * Character-select subsystem — reimplementation of stateCharacterSelect
 * @0x41efa0 and stateCharSelectOk @0x41ef40. New file per milestone:
 * Spela -> 4-mode select (stateGameTypeSelect @0x41c010) -> character
 * pick -> level select.
 *
 * Original draws: char name centered at y=10 with mixed fonts, two
 * sinus-wobble quads (sin(g_flCharAnimTime)*5 +100 / -496), portrait
 * quad (char tex @0x45a660 or QUESTION fallback @0x45a688), stat rows
 * Snabbhet @0x450a94 / Styrka @0x450a8c / Smidighet @0x450a80 with bars
 * sized from g_roundInitb4/b8/bc @0x4501b4/0x4501b8/0x4501bc
 * (bar width = stat*0x2e+0x17, inner = stat*0x2e+1). 3D preview model
 * (sceneNodeAllocChild @0x4319e0, sceneryObjAlloc @0x430200, anmLoad @0x433a90)
 * and 2D portrait. Keys: 0 Right / 1 Left cycle g_nCharSelIdx
 * 0..9 wrap; 2 Down decrements the row cursor (clamp -1->0); 3 Up toggles
 * the row cursor 0<->1 (the disasm computes row=(row==0)?1:0). Left/Right
 * only act on row 0 (the active character row; row 1's value pointer is
 * null in the original). 6 Enter validates the row-0 action
 * (stateCharSelectOk); 7 Esc -> game-type select.
 *
 * Global addresses mirror the original (see charselect.h). Rendering uses
 * gxDrawPolygon @0x433440 and gxDrawQuadColor @0x414470. Fade resets to
 * 0 at end of frame (g_nMenuFadeTarget/Cur @0x45a6f0/0x45a6ec).
 * ===================================================================== */

int   g_nCharSelIdx = 0;        /* @0x45d480 */
int   g_nCharSelSaved = 0;      /* @0x45d450 */
int   g_nCharSelIdxPrev = -1;   /* @0x45d484 */
int   g_nCharSelRow = 0;        /* @0x45d490 */
float g_flCharModelRot = 1.5339824f; /* @0x45d488 */
float g_flCharModelZoom = 2000.0f;   /* @0x45d48c */
float g_flCharAnimTime = 0;     /* @0x45d410 */
int   g_nCharAnimFrame = 0;     /* @0x45d498 */
float g_flCharAnimAccum = 0;    /* @0x45d49c */
SceneNode *g_pCharModelNode = NULL;  /* @0x45a6c4 current preview node */
SceneNode *g_pCharModelNodePrev = NULL; /* @0x45a6c8 previous node */
AnmFile *g_pCharAnim = NULL;       /* @0x45a6d8 current anim */
AnmFile *g_pCharAnimPrev = NULL;   /* @0x45a6dc previous anim */
int   g_nCharModelSwapFlag = 0; /* @0x45d494 */
/* g_pSceneRoot is the first field of g_camFollowBlock (scene.h). */
int g_anMenuCharTex[10] = {0};/* @0x45a660 per-char tex */
int g_hMenuTexTom = 0;     /* @0x45a688 */
void *g_pCharSelAnimData = NULL;/* @0x45a6d0 anim-data pointer passed to anmLoad */
void *g_pThrowAnimData = NULL;  /* @0x45a6d4 anim-data block (fileReadRaw of anim\s_throw2.an; also read by endScene @0x425076) */

/* Rebuilt tables (original @0x45013c / 0x450164 / 0x4501b4). Order matches
 * original indexing: EAX*4+0x45013c where EAX=g_nCharSelIdx. Display names
 * decoded from .rdata (see ghidra reads 0x450340 etc); scene names are
 * uppercase model identifiers at 0x4502ec etc. Stats from 0x4501b4/b8/bc. */
const char *g_apCharNames[10] = { /* @0x45013c */
    "Roland Bl\xe5vind",     /* 0 @0x4503c4 */
    "Susanne Spira",         /* 1 @0x4503b4 */
    "\xc5ke L\xf6nn",         /* 2 @0x4503a8 Åke Lönn */
    "Agata von G\xf6rdel",   /* 3 @0x450394 */
    "Hektor Kvot",           /* 4 @0x450388 */
    "Hugo Spandex",          /* 5 @0x450378 */
    "Bosse B\xe4nkpress",    /* 6 @0x450368 */
    "Klara Blixt",           /* 7 @0x45035c */
    "Kalle Kallsup",         /* 8 @0x45034c */
    "Kajsa Komet"            /* 9 @0x450340 */
};
const char *g_apCharSceneNames[10] = { /* @0x450164 (model ids, must match MESH/NAME names in CHARACTERS.SEN) */
    "ROLAND",   /* 0 @0x450338 */
    "KAJSA",    /* 1 @0x450330 */
    "BERRY",    /* 2 @0x450328 */
    "BRITTA",   /* 3 @0x450320 */
    "FLOTTY",   /* 4 @0x450318 */
    "MILOS",    /* 5 @0x450310 */
    "AXEL",     /* 6 @0x450308 */
    "NIKOLINA", /* 7 @0x4502fc */
    "VONKEL",   /* 8 @0x4502f4 */
    "PILOTTA"   /* 9 @0x4502ec */
};
const int g_kCharStatSpeed[10] = { /* @0x4501b4 */
    3,3,3,4,2,3,3,2,4,2
};
const int g_kCharStatStrength[10] = { /* @0x4501b8 */
    3,3,4,2,3,3,2,4,2,3
};
const int g_kCharStatAgility[10] = { /* @0x4501bc */
    3,4,2,3,3,2,4,2,3,4
};


/* Player record layout lives in player.h (PlayerRecord @0x456210, 8 × 0x374;
 * nCharIdx at +0x150 = the g_apPlayers @0x456360 view). The MOV
 * [0x456360],EAX in stateCharSelectOk @0x41ef78 and the ADD ECX,0x374
 * strides in playerSetupCharacters @0x41b76f @0x41b7b5 prove the stride. */
extern int g_nLocalPlayerIdx;          /* @0x458104 */
int g_nLocalPlayerIdx = 0;

/* textDrawMixedCase @0x41ffc0 — lower-case a-z and å/ä/ö (0xe5/0xe4/0xf6)
 * in the small 200-font, others in the large 200-font. Advances x per token. */
void textDrawMixedCase(int x, int y, const char *text) /* @0x41ffc0 */
{
    if (text == NULL) return;
    gxFont *small = g_hMenuMsfnt; /* @0x45a64c */
    gxFont *big = g_hMenuMfnt;    /* @0x45a650 */
    if (small == NULL || big == NULL) {
        if (g_hMenuFont) { textDraw(g_hMenuFont, 0x2004, x, y, (char*)text); }
        return;
    }
    const char *p = text;
    char token[256];
    int cx = x;
    while (*p != '\0') {
        unsigned int u = (unsigned char)*p;
        const char *q = p;
        if ((u > 0x60 && u < 0x7b) || u == 0xe5 || u == 0xe4 || u == 0xf6) {
            while (*q != '\0') {
                u = (unsigned char)*q;
                if (!((u > 0x60 && u < 0x7b) || u == 0xe5 || u == 0xe4 || u == 0xf6)) break;
                q++;
            }
            int n = (int)(q - p);
            if (n > 0) {
                if (n > 255) n = 255;
                memcpy(token, p, (size_t)n); token[n]='\0';
                textDraw(small, 0x2004, cx, y, token);
                cx += textWidth(small, token);
            }
        } else {
            while (*q != '\0') {
                u = (unsigned char)*q;
                if ((u > 0x60 && u < 0x7b) || u == 0xe5 || u == 0xe4 || u == 0xf6) break;
                q++;
            }
            int n = (int)(q - p);
            if (n > 0) {
                if (n > 255) n = 255;
                memcpy(token, p, (size_t)n); token[n]='\0';
                textDraw(big, 0x2004, cx, y, token);
                cx += textWidth(big, token);
            }
        }
        p = q;
        if (*p == '\0') break;
    }
}

/* stateCharSelectOk @0x41ef40 — validate char idx. If g_nCharSelIdx >=
 * g_nLevelCount+5 then stay (sfx 5), else assign to player 0 and enter
 * level select. */
int stateCharSelectOk(int nType, int nKey, int nKeyType) /* @0x41ef40 */
{
    (void)nType; (void)nKey; (void)nKeyType;
    if (g_nCharSelIdx >= g_nLevelCount + 5) {
        sndPlaySfx(0,1,5,0xffff,0,0x400);
        g_pStateFunc = stateCharacterSelect;
        appLog("[charselect] invalid char %d >= %d+5 — stay", g_nCharSelIdx, g_nLevelCount);
        return 0;
    }
    g_playerRecords[0].nCharIdx = g_nCharSelIdx;
    g_nLocalPlayerIdx = 0;
    g_pStateFunc = stateLevelSelect;
    g_nMenuFadeTarget = 0;
    appLog("[charselect] char %d '%s' selected -> level select", g_nCharSelIdx, g_apCharNames[g_nCharSelIdx]);
    return 0;
}

/* stateCharacterSelect @0x41efa0 — character picker. Input handling
 * mirrors 0x41efa0: Right/Left adjust char idx (0..9 wrap), Up/Down
 * keep single row 0, Enter -> stateCharSelectOk, Esc -> game-type
 * select. Frame (nType==0) renders the 3D model via the scene graph plus
 * the name, wobble bars, portrait and three stat rows. */
int stateCharacterSelect(int nType, int nKey, int nKeyType) /* @0x41efa0 */
{
    /* Sync saved idx on entry (original MOV [0x45d480],ECX where ECX=[0x45d450]). */
    g_nCharSelIdx = g_nCharSelSaved;

    if (nType == 1 && nKeyType == 2) {
        switch (nKey) {
        case 0: /* Right — only on row 0 (original @0x41f0ee): inc char idx if <10 */
            if (g_nCharSelRow == 0 && g_nCharSelIdx < 10) {
                sndPlaySfx(0,1,2,0xffff,0,0x400);
                g_nCharSelIdx++;
                appLog("[charselect] Right -> char %d", g_nCharSelIdx);
            }
            break;
        case 1: /* Left — only on row 0 (original @0x41f0a5): dec char idx if >-1 */
            if (g_nCharSelRow == 0 && g_nCharSelIdx > -1) {
                sndPlaySfx(0,1,2,0xffff,0,0x400);
                g_nCharSelIdx--;
                appLog("[charselect] Left -> char %d", g_nCharSelIdx);
            }
            break;
        case 2: /* Down — row dec (original @0x41f06e): row-1, wrap -1 -> 0 */
            sndPlaySfx(0,1,1,0xffff,0,0x400);
            g_nCharSelRow = g_nCharSelRow - 1;
            if (g_nCharSelRow == -1) g_nCharSelRow = 0;
            break;
        case 3: /* Up — toggle row 0<->1 (original @0x41f03c): row = (row==0)?1:0 */
            sndPlaySfx(0,1,1,0xffff,0,0x400);
            g_nCharSelRow = (g_nCharSelRow == 0) ? 1 : 0;
            break;
        case 6: /* Enter — original @0x41f136 stores g_pStateFunc from a per-row
                   action table (row 0 -> stateCharSelectOk; row 1 -> runtime
                   buffer @0x4550d8, empty in static data). Row 0 is the active
                   character row; row 1's action is not statically resolvable, so
                   it is treated as a no-op that stays in this state. */
            sndPlaySfx(0,1,3,0xffff,0,0x400);
            if (g_nCharSelRow == 0) {
                g_pStateFunc = stateCharSelectOk;
                appLog("[charselect] Enter -> stateCharSelectOk");
            } else {
                appLog("[charselect] Enter on row 1 (no-op)");
            }
            return 0;
        case 7: /* Esc */
            sndPlaySfx(0,1,4,0xffff,0,0x400);
            g_pStateFunc = stateGameTypeSelect;
            appLog("[charselect] Esc -> game-type select");
            return 0;
        default: break;
        }
    }

    /* Wrap 0..9 (original at 0x41f50e). */
    if (g_nCharSelIdx == -1) g_nCharSelIdx = 9;
    else if (g_nCharSelIdx == 10) g_nCharSelIdx = 0;
    g_nCharSelSaved = g_nCharSelIdx;

    if (nType != 0) return 0;

    /* Frame — original 3D character preview via scene graph.
     * Model (re)allocation: original @0x41f553. Per-frame update:
     * original @0x41f647. Camera/root: original @0x41f7b8. State match
     * verified against the disassembly at each call site below. */
    {
        /* The scene system and the character .SEN (mesh table + anim-data
         * block @0x45a6d0) are initialised by the game-flow path that enters
         * this state — the original stateCharacterSelect does NOT call
         * sceneSystemInit / sceneLoadSen. Keep the call hierarchy intact:
         * g_pCharSelAnimData is populated by that external init, not here. */
        if (g_nCharSelIdx != g_nCharSelIdxPrev || g_pCharModelNode == NULL) {
            g_nCharSelIdxPrev = g_nCharSelIdx;
            /* Free the previously displayed model (original @0x41f558). */
            if (g_pCharModelNodePrev != NULL) {
                sceneNodeFree(g_pCharModelNodePrev, 1);   /* @0x430460 */
                anmFree(g_pCharAnimPrev);                  /* @0x434050 */
            }
            g_pCharModelNodePrev = g_pCharModelNode;       /* @0x45a6c8 */
            g_pCharAnimPrev      = g_pCharAnim;             /* @0x45a6dc */
            g_nCharModelSwapFlag = 0;                      /* @0x45d494 */
            g_flCharModelRot = 1.5339824f;                 /* @0x3fc45989 */
            g_flCharModelZoom = 2000.0f;                   /* @0x44fa0000 */
            /* sceneNodeAllocChild — original pushes (0, 0, 0x352, 0, 0x591),
             * i.e. pParent=0, channel=0x352, channel2=0, channel3=0, channel4=0x591. */
            g_pCharModelNode = sceneNodeAllocChild(0, 0, (void *)0x352, 0, (void *)0x591);
            sceneObjSetPosOrient(g_pCharModelNode, 0, 0, 0, 0x2);  /* @0x4307d0 */
            void *id;
            if (g_nCharSelIdx >= g_nLevelCount + 5)
                id = scenNameToId("QUESTION");             /* @0x450aa0 fallback */
            else
                id = scenNameToId(g_apCharSceneNames[g_nCharSelIdx]);
            SceneNode *pScen = sceneryObjAlloc(g_pCharModelNode, 0, 0, 0, 0, 0, 0, 0,
                                               id);   /* @0x430200 */
            /* anmLoad — original passes *0x45a6d0 as pData (the anim block from
             * the loaded .SEN), 0 as pMasterNode, and the sceneryObj as pObj. */
            g_pCharAnim = anmLoad(g_pCharSelAnimData, 0, pScen);     /* @0x433a90 */
            eventAnimReset(g_pCharAnim);                   /* @0x434270 */
            eventAnimStep(g_pCharAnim, 1);                 /* @0x434090 */
            appLog("[charselect] 3D model idx %d '%s' id=%x node=%p anim=%p",
                   g_nCharSelIdx, g_apCharNames[g_nCharSelIdx], id, g_pCharModelNode, g_pCharAnim);
        }
        /* Per-frame animation stepping (original @0x41f647): advance anim
         * frames until the int() of the accumulator catches up. Original
         * does FABS(frame-accum) >10.0 (double @0x44b6c0) via x87 (no call). */
        {
            float diff = (float)g_nCharAnimFrame - g_flCharAnimAccum;
            if (diff < 0) diff = -diff;
            if (diff > 10.0f) g_flCharAnimAccum = (float)g_nCharAnimFrame;
        }
        g_flCharAnimAccum += g_flFrameDelta;
        {
            int nAnimTarget = (int)g_flCharAnimAccum;
            while (nAnimTarget > g_nCharAnimFrame) {
                g_nCharAnimFrame++;
                eventAnimStep(g_pCharAnim, 1);             /* @0x434090 */
            }
        }
        /* Place the model on a circle from the rotation (original @0x41f6be):
         * x = (int)(sin(rot)*zoom) - 0x1f4,  y = 0,  z = (int)(cos(rot)*zoom) + 0x320. */
        {
            float vec[8];
            sceneNodeGetPosWorld(g_pCharModelNode, vec, 0x2);  /* @0x430e80 (side effect) */
            int x = (int)(sinf(g_flCharModelRot) * g_flCharModelZoom) - 0x1f4;
            int z = (int)(cosf(g_flCharModelRot) * g_flCharModelZoom) + 0x320;
            sceneObjSetPos(g_pCharModelNode, x, 0, z, 0x2);    /* @0x430660 */
        }
        /* Per-frame orientation pitch (original @0x41f725): pitch = frameDelta * -653.0. */
        sceneObjSetPosOrient(g_pCharModelNode, 0,
                             (short)((int)(g_flFrameDelta * -653.0f)), 0, 0x5);  /* @0x4307d0 */
        /* Rotation + zoom easing (original @0x41f733 / @0x41f74e). */
        g_flCharModelRot -= g_flFrameDelta * 0.0628f;                  /* 0x44b6b4 */
        if (g_flCharModelZoom > 700.0f)
            g_flCharModelZoom -= g_flFrameDelta * 20.0f;               /* 0x44b6b0 */
        /* Fade the previous model out by sliding it (original @0x41f779).
         * GetPosWorld mode 2 returns local x,y,z as int bits in the float
         * buffer; the disasm does LEA ECX,[ESP+0x78] then after the 3 pushes
         * reads [ESP+0x88] = ECX+4 = y (second element). The threshold is
         * 0x4e20 = 20000. The slide uses mode 5 (oriented delta) with
         * y=0x1f4 (500). */
        if (g_pCharModelNodePrev != NULL) {
            float vec2[8];
            sceneNodeGetPosWorld(g_pCharModelNodePrev, vec2, 0x2); /* @0x430e80 */
            if (((int *)vec2)[1] < 0x4e20) {                            /* y < 20000 */
                sceneObjSetPos(g_pCharModelNodePrev, 0, 0x1f4, 0, 0x5);  /* @0x430660 mode 5 */
            }
        }
        /* Camera placement (original @0x41f7b8): g_pSceneRoot is the camera
         * block allocated by menuInit @0x41a24e via sceneNodeAlloc @0x4318e0
         * with {1.0,10.0,500000,0,0,0x1000,0x1000} (mode 2). No hack needed here. */
        sceneObjSetPos(g_pSceneRoot, 0, -1600, -2000, 0x2);        /* @0x430660 */
        sceneNodeFacePos(g_pSceneRoot, 0, -2100.0f, 0.0f, 1000.0f, 0x2);  /* @0x431030 */
        sceneRender(g_pSceneRoot);                                      /* @0x42f1c0 */
    }

    /* Render 2D UI — matches original coordinates. */
    {
        if (g_nCharSelIdx <0 || g_nCharSelIdx >=10) {
            appLog("[charselect] idx %d out of range, clamping to 0", g_nCharSelIdx);
            g_nCharSelIdx = 0;
            g_nCharSelSaved = 0;
        }
        const char *name = g_apCharNames[g_nCharSelIdx];
        if (name == NULL) name = "Unknown";
        /* Name width + center at 0x140 (320). Original computestotal width
         * via two-font textWidth then x=0x140 - w/2, y=10. */
        int w = 0;
        {
            const char *p = name;
            char tok[256];
            gxFont *smallF = g_hMenuFontSmall;
            gxFont *bigF = g_hMenuFont;
            if (smallF == NULL || bigF == NULL) {
                appLog("[charselect] fonts null small=%p big=%p msfnt=%p mfnt=%p", smallF, bigF, g_hMenuMsfnt, g_hMenuMfnt);
                /* fallback width estimate */
                w = (int)strlen(name) * 8;
            } else {
                while (*p != '\0') {
                    unsigned int u = (unsigned char)*p;
                    const char *q = p;
                    if ((u > 0x60 && u < 0x7b) || u == 0xe5 || u == 0xe4 || u == 0xf6) {
                        while (*q) { u=(unsigned char)*q; if(!((u>0x60&&u<0x7b)||u==0xe5||u==0xe4||u==0xf6)) break; q++; }
                        int n=(int)(q-p); if(n>0){ if(n>255)n=255; memcpy(tok,p,n); tok[n]=0; w+=textWidth(smallF,tok); }
                    } else {
                        while (*q) { u=(unsigned char)*q; if((u>0x60&&u<0x7b)||u==0xe5||u==0xe4||u==0xf6) break; q++; }
                        int n=(int)(q-p); if(n>0){ if(n>255)n=255; memcpy(tok,p,n); tok[n]=0; w+=textWidth(bigF,tok); }
                    }
                    p = q;
                }
            }
        }
        g_flCharAnimTime += g_flFrameDelta * 0.3f; /* @0x44b530 0.3f */
        {
            int cx = 0x140 - w/2;
            const char *p = name;
            char tok[256];
            while (*p != '\0') {
                unsigned int u = (unsigned char)*p;
                const char *q = p;
                if ((u > 0x60 && u < 0x7b) || u == 0xe5 || u == 0xe4 || u == 0xf6) {
                    while (*q) { u=(unsigned char)*q; if(!((u>0x60&&u<0x7b)||u==0xe5||u==0xe4||u==0xf6)) break; q++; }
                    int n=(int)(q-p); if(n>0){ if(n>255)n=255; memcpy(tok,p,n); tok[n]=0; textDraw(g_hMenuMsfnt,0x2004,cx,10,tok); cx+=textWidth(g_hMenuMsfnt,tok); p=q; continue; }
                }
                q = p;
                while (*q) { unsigned int u2=(unsigned char)*q; if((u2>0x60&&u2<0x7b)||u2==0xe5||u2==0xe4||u2==0xf6) break; q++; }
                int n=(int)(q-p); if(n>0){ if(n>255)n=255; memcpy(tok,p,n); tok[n]=0; textDraw(g_hMenuMfnt,0x2004,cx,10,tok); cx+=textWidth(g_hMenuMfnt,tok); p=q; }
            }
        }
        /* Wobble arrows (original 0x41f9bf-0x41fb7d): left = 0x104 - w/2 + sin*5,
           right = w/2 + 0x150 - sin*5, both width 0x2b, y 0x500-0x3000,
           UV left 0x5800/0x8300, right 0x8400/0xaf00. The double at 0x44b698 is 5.0. */
        {
            if (g_hMenuTexGfx && g_hMenuMsfnt && g_hMenuMfnt) {
                float s = sinf(g_flCharAnimTime);
                double s5 = (double)s * 5.0; /* @0x44b698 */
                int leftBase = (int)((double)(0x104 - w/2) + s5);   /* FILD 0x104-w/2, FADDP */
                int rightBase = (int)((double)(w/2 + 0x150) - s5);  /* FILD w/2+0x150, FSUBP (PTRADD w/2+0x54*4) */
                GxVert v0,v1,v2,v3; GxColorUv uv;
                uv.nTexture = g_hMenuTexGfx; uv.nParam5 = 0; uv.pad=0;
                uv.U=0x5800; uv.V=0x3300; uv.gwU=0x8300; uv.V2=0x3300; uv.gwU2=0x8300; uv.hV=0x5e00; uv.U2=0x5800; uv.hV2=0x5e00;
                v0.x = leftBase <<8; v1.x = (leftBase + 0x2b) <<8; v2.x=v1.x; v3.x=v0.x;
                v0.y=0x500; v1.y=0x500; v2.y=0x3000; v3.y=0x3000;
                setSignVerts(&v0,&v1,&v2,&v3);
                gxDrawPolygon(&v0,&v1,&v2,&v3,0x2004,&uv);
                uv.U=0x8400; uv.gwU=0xaf00; uv.gwU2=0xaf00; uv.U2=0x8400;
                v0.x = rightBase <<8; v1.x = (rightBase + 0x2b) <<8; v2.x=v1.x; v3.x=v0.x;
                setSignVerts(&v0,&v1,&v2,&v3);
                gxDrawPolygon(&v0,&v1,&v2,&v3,0x2004,&uv);
            }
        }
        /* Portrait quad (original 0xa00,0x3c00 - 0x10900,0x13b00, UV 0..0xff00 or char tex). */
        if (g_hMenuTexGfx) {
            GxVert v0,v1,v2,v3; GxColorUv uv;
            int tex = 0;
            if (g_nCharSelIdx < g_nLevelCount + 5 && g_nCharSelIdx >=0 && g_nCharSelIdx <10 && g_anMenuCharTex[g_nCharSelIdx])
                tex = g_anMenuCharTex[g_nCharSelIdx];
            else
                tex = g_hMenuTexTom ? g_hMenuTexTom : g_hMenuTexGfx;
            if (tex == 0) tex = g_hMenuTexGfx;
            uv.nTexture = tex; uv.nParam5 = 0; uv.pad=0;
            uv.U=0; uv.V=0; uv.gwU=0xff00; uv.V2=0; uv.gwU2=0xff00; uv.hV=0xff00; uv.U2=0; uv.hV2=0xff00;
            v0.x=0xa00; v1.x=0x10900; v2.x=0x10900; v3.x=0xa00;
            v0.y=0x3c00; v1.y=0x3c00; v2.y=0x13b00; v3.y=0x13b00;
            setSignVerts(&v0,&v1,&v2,&v3);
            gxDrawPolygon(&v0,&v1,&v2,&v3,0x2004,&uv);
        }
        /* The 3D mesh model is rendered earlier in this frame (see the
         * scene-graph block above, original sceneNodeAllocChild @0x4319e0 /
         * sceneryObjAlloc @0x430200 / anmLoad @0x433a90 path). This portrait
         * quad is drawn on top as the original does. */
        /* Stat rows: Snabbhet @0x11d,0x146 ; Styrka @0x11d,0x178 ; Smidighet @0x11d,0x1aa
         * Each has label (mixed fonts), background bar (10, y, 0x109, y+0x17, 0,0x5f...),
         * foreground bar width stat*0x2e+0x17 / +1. */
        {
            const char *labels[3] = {"Snabbhet", "Styrka", "Smidighet"}; /* @0x450a94/0x450a8c/0x450a80 */
            const int *stats[3] = {g_kCharStatSpeed, g_kCharStatStrength, g_kCharStatAgility};
            int ys[3] = {0x146, 0x178, 0x1aa};
            int y2s[3]= {0x150, 0x182, 0x1b4};
            int y3s[3]= {0x155, 0x187, 0x1b9};
            for(int i=0;i<3;i++){
                int x=0x11d; int y=ys[i];
                /* label via mixed fonts */
                if (i==0) { /* Snabbhet drawn via two-font loop like original 0x41fd0e */
                    const char *s=labels[i]; char tok[128]; const char *p=s;
                    int cx=x;
                    while(*p){ unsigned int u=(unsigned char)*p; const char *q=p; int n;
                        if((u>0x60&&u<0x7b)||u==0xe5||u==0xe4||u==0xf6){ while(*q){u=(unsigned char)*q; if(!((u>0x60&&u<0x7b)||u==0xe5||u==0xe4||u==0xf6))break; q++;} n=(int)(q-p); if(n>0){memcpy(tok,p,n);tok[n]=0;textDraw(g_hMenuMsfnt,0x2004,cx,y,tok);cx+=textWidth(g_hMenuMsfnt,tok);} }
                        else { while(*q){u=(unsigned char)*q; if((u>0x60&&u<0x7b)||u==0xe5||u==0xe4||u==0xf6)break; q++;} n=(int)(q-p); if(n>0){memcpy(tok,p,n);tok[n]=0;textDraw(g_hMenuMfnt,0x2004,cx,y,tok);cx+=textWidth(g_hMenuMfnt,tok);} }
                        p=q;
                    }
                } else {
                    textDrawMixedCase(x, y, labels[i]);
                }
                /* background bar — guarded, original always draws */
                if (g_hMenuTexGfx) gxDrawQuadColor(g_hMenuTexGfx,10,y,0x109,y+0x17,0,0x5f,0xff,0x76);
                if (g_hMenuTexGfx && g_nCharSelIdx >=0 && g_nCharSelIdx <10 && g_nCharSelIdx < g_nLevelCount + 5) {
                    int st = stats[i][g_nCharSelIdx];
                    if(st<0) st=0;
                    if(st>5) st=5;
                    int w = st * 0x2e + 0x17;
                    int wi = st * 0x2e + 1;
                    gxDrawQuadColor(g_hMenuTexGfx,0x16,y2s[i], w, y3s[i],0,0x77, wi,0x7c);
                }
            }
        }
    }
    g_nMenuFadeTarget = 0;
    g_nMenuFadeCur = 0;
    return 0;
}
