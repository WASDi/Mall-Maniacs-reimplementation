/* cinematic.c — offline trailer capture mode (PORT ADDITION, see
 * cinematic.h and Trailer.md). Compiled only with -DMANIAC_CINEMATIC=ON.
 *
 * Shot file (one directive per line, '#' starts a comment):
 *   shot   <name>            clip name (default output build/trailer/clips/<name>.mkv)
 *   level  <n>               level index 0..4
 *   mode   <name>            varujakten (default), matkrig, frogesport, vagnrace
 *   character <n>            local player's character index (default 0)
 *   frames <n>               captured frames (25 fps)
 *   start  <seconds>         warm-up game time before the first captured frame
 *   size   <w> <h>           capture size (default 1920 804)
 *   world_speed <s>          game-time scale (1 normal, 0 frozen)
 *   env    <NAME> <value>    environment variable exported before startup
 *   ease   <in> <out>        seconds of ease-in/ease-out on the camera path
 *   blur   <n>               motion-blur sub-frames per frame in --final renders (default 4)
 *   shutter <degrees>        shutter angle (default 180)
 *   hold   <n>               show every n-th frame n times (a lower, steppier frame
 *                            rate on the same 25 fps timeline; 2 = 12.5 fps)
 *   key    <t> <x> <y> <z> <tx> <ty> <tz> [fov]
 *                            camera keyframe at shot time t (seconds from the
 *                            first captured frame): position, look-at point
 *                            (world units, up is -y) and vertical FOV in
 *                            degrees (default and maximum: the game's 73.74)
 * A shot without keys keeps the game's follow camera.
 *
 * Staging (montage shots; use world_speed 0 so the sim does not move them):
 *   players <n>              round player count 1..8 (levels define 8 start positions)
 *   cast   <c0> [c1 ...]     character index per player slot (default: the game's pick)
 *   actor  <slot> <x> <y> <z> <yaw> [anim <name>] [frame <f>] [fps <r>] [once]
 *                            place player <slot> facing yaw degrees (0 = +z) and pose
 *                            anim/<name>.anm at frame f + r * shot time (default 25
 *                            frames/s, the game's action rate; fps 0 freezes the pose;
 *                            loops unless once)
 *   pose   <slot> <t> <anim> [frame <f>] [fps <r>] [once]
 *                            from shot time t, switch the actor to another animation
 *                            (frames count from t; up to 4 segments per actor)
 *   actor  <slot> hide       hide that player (and its shadow)
 *   cart   <slot> <x> <y> <z> <yaw> [spin <deg/s>]   place that player's cart
 *   cart   <slot> hide
 *   item   <id> <x> <y> <z> [rot <yaw> <pitch> <roll>] [spin <yaw/s> <pitch/s> <roll/s>]
 *          [vel <vx> <vy> <vz>] [gravity <g>] [from <t>]
 *                            a free item prop using level item slot id's mesh (1..30);
 *                            moves as p + v t + g t^2 / 2 on y (up is -y, so g > 0 falls);
 *                            with from, hidden until shot time t and t counts from there
 *   follow <slot> <ox> <oy> <oz> <ax> <ay> <az> [rel] [smooth <s>] [from <t>]
 *                            from shot time t (default 0) the camera rides at offset o
 *                            from player <slot> and looks at offset a; with rel, x/z
 *                            offsets turn with the player's travel direction (+z ahead,
 *                            +x right). Position and heading are smoothed (default
 *                            0.15 s) so the AI's steering jitter does not shake the shot
 *   zones  all               draw every zone, not just the camera's (portal culling
 *                            hides everything once the camera leaves the zone
 *                            volumes, e.g. for aerial or high crane shots)
 *   shake  <t0> <t1> <amplitude> [hz]
 *                            camera shake decaying from amplitude (world units) at t0
 *                            to 0 at t1; deterministic, default 9 Hz (up to 8 per shot)
 *
 * Captures are drafts (one render per frame) unless `--final` is given.
 * `--preview` writes a fast review clip instead: a third of the shot's
 * size, every second frame (12.5 fps), to build/trailer/clips_preview/.
 * MANIAC_CINE_SURVEY=1 prints the level's item spawn points at the first
 * captured frame and every player's position each captured frame.
 *
 * `--freecam` opens the same shot interactively instead of capturing it:
 * mouse look, WASD move, Q/E down/up, Shift fast, wheel speed, Z/X FOV,
 * K append a keyframe to the shot file, Enter preview the keyed path,
 * T freeze/unfreeze game time, G framing guides, P print pose, Esc quit. */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "anim.h"
#include "charselect.h"
#include "cinematic.h"
#include "custom_helpers.h"
#include "gameplay.h"
#include "level.h"
#include "levelselect.h"
#include "menu.h"
#include "platform_sdl2.h"
#include "player.h"
#include "player_camera.h"
#include "player_setup.h"
#include "scene.h"
#include "stubs.h"
#include "time.h"
#include "zone.h"

/* 25 fps = one frame per 40 ms sim tick (maniac.cfg obj_update_time). */
#define CINE_FPS 25
#define CINE_FREECAM_FPS 60        /* warm-up step only; live freecam uses wall time */
#define CINE_CLOCK_BASE_MS 1000.0   /* nonzero start: getGameTime treats a 0 cache as unset */
#define CINE_MAX_KEYS 64
#define CINE_MAX_BLUR 64
#define CINE_BLUR_DEFAULT 4
#define CINE_SHUTTER_DEFAULT 180.0
#define CINE_DEG2RAD 0.017453292519943295
/* sceneRender: screen x spans +-camera.nWidth per unit depth and y spans
 * 0.75 of that (1.333 Y scale), so the game's camera (nWidth 1.0) has a
 * vertical FOV of 2*atan(0.75). Wider would outrun the widescreen
 * culling cone, which assumes nWidth 1.0. */
#define CINE_FOV_DEFAULT 73.7397952917
#define CINE_FREECAM_PREVIEW_WIDTH 1280
#define CINE_FREECAM_LOOK_DIST 2000.0
#define CINE_FREECAM_SPEED 3000.0   /* world units per second */
#define CINE_MOUSE_RAD 0.0025       /* radians per mouse count */

typedef struct CineKey {
    double t;
    double p[3];   /* camera position */
    double a[3];   /* look-at point */
    double fov;    /* vertical degrees */
} CineKey;

#define CINE_MAX_ACTORS 8
#define CINE_MAX_PROPS 16
#define CINE_MAX_SHAKES 8
#define CINE_PREVIEW_DIV 3          /* --preview: a third of the capture size */
#define CINE_ANIM_FPS 25.0          /* action sets step once per 40 ms tick */
#define CINE_MAX_SEGMENTS 4
/* sceneObjSetPosOrient angle units: maniac.cfg start_positions use -32364
 * (about -178 degrees) and 16182 (about 89), i.e. 65536 per turn. */
#define CINE_DEG2ANGLE (65536.0 / 360.0)

typedef struct CineSegment {
    double dblStart;        /* shot time the segment begins */
    char   szAnim[32];
    double dblFrame0;
    double dblFps;
    int    bOnce;
    AnmSet *pSet;           /* resolved after the round starts */
} CineSegment;

typedef struct CineActor {
    int    bUsed;
    int    bHide;
    double p[3];
    double dblYaw;          /* degrees, 0 faces +z */
    int    nSegs;           /* 0: keep the game's pose */
    CineSegment aSegs[CINE_MAX_SEGMENTS];
} CineActor;

typedef enum { CINE_PROP_CART, CINE_PROP_ITEM } CinePropKind;

typedef struct CineProp {
    CinePropKind eKind;
    int    nRef;            /* player slot (cart) or item id 1..30 (item) */
    int    bHide;
    double p[3];
    double adblRot[3];      /* degrees: yaw, pitch, roll (cart uses yaw) */
    double adblSpin[3];     /* degrees per shot second */
    double adblVel[3];      /* world units per shot second (items) */
    double dblGravity;      /* world units per second^2 on y (items) */
    double dblFrom;         /* items: hidden before this shot time; motion starts here */
    SceneNode *pNode;       /* resolved after the round starts */
} CineProp;

typedef struct CineShot {
    char   szName[64];
    int    nLevel;
    int    nMode;          /* index into s_apfnModeInit */
    int    nCharacter;
    int    nFrames;
    double dblStartSec;
    int    nWidth;
    int    nHeight;
    double dblWorldSpeed;
    double dblEaseIn;
    double dblEaseOut;
    int    nBlur;          /* sub-frames averaged per video frame */
    int    nHold;          /* each captured frame is written this many times */
    double dblShutterDeg;
    int    nKeys;
    CineKey aKeys[CINE_MAX_KEYS];
    int    nPlayers;       /* 0: the game's count for the level/difficulty */
    int    nCast;
    int    anCast[CINE_MAX_ACTORS];
    CineActor aActors[CINE_MAX_ACTORS];
    int    nProps;
    CineProp aProps[CINE_MAX_PROPS];
    int    bAllZones;
    int    nFollowSlot;     /* -1: none */
    double adblFollowOff[3];
    double adblFollowAim[3];
    int    bFollowRel;
    double dblFollowSmooth;
    double dblFollowFrom;
    int    nShakes;
    double aShakes[CINE_MAX_SHAKES][4];   /* t0, t1, amplitude, hz */
} CineShot;

typedef int (*CineStateFunc)(int nType, int nKey, int nKeyType);

/* The menu path a player takes into a round: game type, character, level. */
static const char *const s_apszModes[] = { "varujakten", "matkrig", "frogesport", "vagnrace" };
static const CineStateFunc s_apfnModeInit[] = {
    modeInitVarujakten, modeInitMatkrig, modeInitFrogesport, modeInitVagnrace
};
static const CineStateFunc s_apfnLevelInit[] = {
    stateLevelInit0, stateLevelInit1, stateLevelInit2, stateLevelInit3, stateLevelInit4
};

static int s_bActive;
static int s_bFreecam;
static int s_bFinal;                 /* --final: motion-blur sub-frames on */
static int s_bPreview;               /* --preview: small, every second frame */
static int s_bSurvey;                /* MANIAC_CINE_SURVEY=1 */
static CineShot s_shot;
static char s_szShotPath[1024];
static char s_szOutPath[1024];
static double s_dblGameMs = CINE_CLOCK_BASE_MS;
static double s_dblShotSec;          /* camera time for the frame being rendered */
static int s_bCapturing;
static FILE *s_pEncoder;
static unsigned char *s_pFrame;
static int s_nFrameW, s_nFrameH;
static int s_nFramesWritten;
static int s_bFailed;
static unsigned short *s_pAccum;     /* per-channel sub-frame sums (motion blur) */
static int s_nAccumCount;

/* Free camera state. */
/* FREECAM_OFF during warm-up (game camera untouched), FREECAM_FOLLOW adopts
 * the game camera's pose on the next frame, then FLY or PLAY. */
typedef enum { FREECAM_OFF, FREECAM_FOLLOW, FREECAM_FLY, FREECAM_PLAY } FreecamMode;
static FreecamMode s_eFreeMode;
static double s_adblFreePos[3];
static double s_dblYaw, s_dblPitch;  /* radians; yaw 0 looks along +z, pitch > 0 looks up */
static double s_dblFreeFov = CINE_FOV_DEFAULT;
static double s_dblSpeedScale = 1.0;
static int s_bGuides = 1;
static int s_bTrace;                 /* MANIAC_CINE_TRACE=1: per-frame camera pose on stderr */

/* --- Shot file ------------------------------------------------------------ */

static char *trimLine(char *psz)
{
    char *pEnd;
    char *pHash = strchr(psz, '#');
    if (pHash) *pHash = '\0';
    while (*psz == ' ' || *psz == '\t') psz++;
    pEnd = psz + strlen(psz);
    while (pEnd > psz && (pEnd[-1] == ' ' || pEnd[-1] == '\t' ||
                          pEnd[-1] == '\r' || pEnd[-1] == '\n'))
        *--pEnd = '\0';
    return psz;
}

static int parseKey(const char *pszArgs, CineKey *pKey)
{
    int n = sscanf(pszArgs, "%lf %lf %lf %lf %lf %lf %lf %lf",
                   &pKey->t, &pKey->p[0], &pKey->p[1], &pKey->p[2],
                   &pKey->a[0], &pKey->a[1], &pKey->a[2], &pKey->fov);
    if (n == 7) pKey->fov = CINE_FOV_DEFAULT;
    if (n < 7) return 0;
    if (pKey->fov > CINE_FOV_DEFAULT) pKey->fov = CINE_FOV_DEFAULT;
    if (pKey->fov < 5.0) pKey->fov = 5.0;
    return 1;
}

static int parseCast(const char *pszArgs)
{
    char szBuf[256];
    char *pTok;
    snprintf(szBuf, sizeof(szBuf), "%s", pszArgs);
    s_shot.nCast = 0;
    for (pTok = strtok(szBuf, " \t"); pTok != NULL; pTok = strtok(NULL, " \t")) {
        int c = atoi(pTok);
        if (s_shot.nCast >= CINE_MAX_ACTORS || c < 0 || c > 9) return 0;
        s_shot.anCast[s_shot.nCast++] = c;
    }
    return s_shot.nCast > 0;
}

/* "anim <name> [frame <f>] [fps <r>] [once]" tokens of an actor or pose line. */
static int parseSegment(CineSegment *pS, double dblStart, char **apTok, int nTok)
{
    int i;
    memset(pS, 0, sizeof(*pS));
    pS->dblStart = dblStart;
    pS->dblFps = CINE_ANIM_FPS;
    for (i = 0; i < nTok; i++) {
        if (strcmp(apTok[i], "anim") == 0 && i + 1 < nTok)
            snprintf(pS->szAnim, sizeof(pS->szAnim), "%s", apTok[++i]);
        else if (strcmp(apTok[i], "frame") == 0 && i + 1 < nTok)
            pS->dblFrame0 = atof(apTok[++i]);
        else if (strcmp(apTok[i], "fps") == 0 && i + 1 < nTok)
            pS->dblFps = atof(apTok[++i]);
        else if (strcmp(apTok[i], "once") == 0)
            pS->bOnce = 1;
        else
            return 0;
    }
    return pS->szAnim[0] != '\0';
}

/* actor / pose / cart / item lines; see the header comment for the grammar. */
static int parseStaging(const char *pszLine)
{
    char szBuf[512];
    char *apTok[24];
    int nTok = 0, i;
    char *pTok;
    snprintf(szBuf, sizeof(szBuf), "%s", pszLine);
    for (pTok = strtok(szBuf, " \t"); pTok != NULL && nTok < 24; pTok = strtok(NULL, " \t"))
        apTok[nTok++] = pTok;
    if (nTok < 3) return 0;
    if (strcmp(apTok[0], "actor") == 0) {
        int nSlot = atoi(apTok[1]);
        CineActor *pA;
        if (nSlot < 0 || nSlot >= CINE_MAX_ACTORS) return 0;
        pA = &s_shot.aActors[nSlot];
        memset(pA, 0, sizeof(*pA));
        pA->bUsed = 1;
        if (strcmp(apTok[2], "hide") == 0) {
            pA->bHide = 1;
            return 1;
        }
        if (nTok < 6) return 0;
        for (i = 0; i < 3; i++) pA->p[i] = atof(apTok[2 + i]);
        pA->dblYaw = atof(apTok[5]);
        if (nTok > 6) {
            pA->nSegs = 1;
            return parseSegment(&pA->aSegs[0], 0.0, apTok + 6, nTok - 6);
        }
        return 1;
    }
    if (strcmp(apTok[0], "pose") == 0) {
        int nSlot = atoi(apTok[1]);
        double dblStart = atof(apTok[2]);
        CineActor *pA;
        if (nSlot < 0 || nSlot >= CINE_MAX_ACTORS || nTok < 4) return 0;
        pA = &s_shot.aActors[nSlot];
        if (!pA->bUsed || pA->bHide || pA->nSegs >= CINE_MAX_SEGMENTS) return 0;
        if (pA->nSegs > 0 && dblStart <= pA->aSegs[pA->nSegs - 1].dblStart) return 0;
        /* "pose <slot> <t> <anim> rest..." parses like "anim <anim> rest..." */
        apTok[2] = "anim";
        if (!parseSegment(&pA->aSegs[pA->nSegs], dblStart, apTok + 2, nTok - 2)) return 0;
        pA->nSegs++;
        return 1;
    }
    {
        CineProp *pP;
        int nAxes;
        if (s_shot.nProps >= CINE_MAX_PROPS) return 0;
        pP = &s_shot.aProps[s_shot.nProps];
        memset(pP, 0, sizeof(*pP));
        pP->eKind = strcmp(apTok[0], "cart") == 0 ? CINE_PROP_CART : CINE_PROP_ITEM;
        pP->nRef = atoi(apTok[1]);
        if (pP->eKind == CINE_PROP_CART && (pP->nRef < 0 || pP->nRef >= CINE_MAX_ACTORS)) return 0;
        if (pP->eKind == CINE_PROP_ITEM && (pP->nRef < 1 || pP->nRef > LEVEL_ITEM_SLOT_COUNT)) return 0;
        if (pP->eKind == CINE_PROP_CART && strcmp(apTok[2], "hide") == 0) {
            pP->bHide = 1;
            s_shot.nProps++;
            return 1;
        }
        if (nTok < 5) return 0;
        for (i = 0; i < 3; i++) pP->p[i] = atof(apTok[2 + i]);
        i = 5;
        nAxes = 3;
        if (pP->eKind == CINE_PROP_CART) {
            if (nTok < 6) return 0;
            pP->adblRot[0] = atof(apTok[5]);
            i = 6;
            nAxes = 1;
        }
        for (; i < nTok; i++) {
            double *pdbl;
            int k;
            if (strcmp(apTok[i], "gravity") == 0 && pP->eKind == CINE_PROP_ITEM && i + 1 < nTok) {
                pP->dblGravity = atof(apTok[++i]);
                continue;
            }
            if (strcmp(apTok[i], "from") == 0 && pP->eKind == CINE_PROP_ITEM && i + 1 < nTok) {
                pP->dblFrom = atof(apTok[++i]);
                continue;
            }
            if (strcmp(apTok[i], "rot") == 0 && pP->eKind == CINE_PROP_ITEM) pdbl = pP->adblRot;
            else if (strcmp(apTok[i], "vel") == 0 && pP->eKind == CINE_PROP_ITEM) pdbl = pP->adblVel;
            else if (strcmp(apTok[i], "spin") == 0) pdbl = pP->adblSpin;
            else return 0;
            if (i + nAxes >= nTok) return 0;
            for (k = 0; k < nAxes; k++) pdbl[k] = atof(apTok[++i]);
        }
        s_shot.nProps++;
        return 1;
    }
}

static int parseShot(const char *pszPath)
{
    char szLine[512];
    int nLine = 0;
    FILE *pFile = fopen(pszPath, "r");
    if (pFile == NULL) {
        fprintf(stderr, "[cinematic] cannot open shot file '%s'\n", pszPath);
        return 0;
    }
    memset(&s_shot, 0, sizeof(s_shot));
    snprintf(s_shot.szName, sizeof(s_shot.szName), "shot");
    s_shot.nFrames = 48;
    s_shot.nWidth = 1920;
    s_shot.nHeight = 804;
    s_shot.dblWorldSpeed = 1.0;
    s_shot.nBlur = CINE_BLUR_DEFAULT;
    s_shot.nHold = 1;
    s_shot.nFollowSlot = -1;
    s_shot.dblShutterDeg = CINE_SHUTTER_DEFAULT;
    while (fgets(szLine, sizeof(szLine), pFile)) {
        char szKey[32], szA[256], szB[256];
        char *psz = trimLine(szLine);
        int nArgs;
        nLine++;
        if (*psz == '\0') continue;
        szA[0] = szB[0] = '\0';
        nArgs = sscanf(psz, "%31s %255s %255[^\n]", szKey, szA, szB);
        if (strcmp(szKey, "shot") == 0 && nArgs >= 2) {
            snprintf(s_shot.szName, sizeof(s_shot.szName), "%s", szA);
        } else if (strcmp(szKey, "level") == 0 && nArgs >= 2) {
            s_shot.nLevel = atoi(szA);
        } else if (strcmp(szKey, "mode") == 0 && nArgs >= 2) {
            int m;
            s_shot.nMode = -1;
            for (m = 0; m < 4; m++)
                if (strcmp(szA, s_apszModes[m]) == 0) s_shot.nMode = m;
        } else if (strcmp(szKey, "character") == 0 && nArgs >= 2) {
            s_shot.nCharacter = atoi(szA);
        } else if (strcmp(szKey, "frames") == 0 && nArgs >= 2) {
            s_shot.nFrames = atoi(szA);
        } else if (strcmp(szKey, "start") == 0 && nArgs >= 2) {
            s_shot.dblStartSec = atof(szA);
        } else if (strcmp(szKey, "size") == 0 && nArgs >= 3) {
            s_shot.nWidth = atoi(szA);
            s_shot.nHeight = atoi(szB);
        } else if (strcmp(szKey, "world_speed") == 0 && nArgs >= 2) {
            s_shot.dblWorldSpeed = atof(szA);
        } else if (strcmp(szKey, "env") == 0 && nArgs >= 3) {
            setenv(szA, szB, 1);
        } else if (strcmp(szKey, "follow") == 0) {
            char szRest[256];
            int nRead = 0;
            szRest[0] = '\0';
            s_shot.dblFollowSmooth = 0.15;
            if (sscanf(psz + 6, "%d %lf %lf %lf %lf %lf %lf%n", &s_shot.nFollowSlot,
                       &s_shot.adblFollowOff[0], &s_shot.adblFollowOff[1], &s_shot.adblFollowOff[2],
                       &s_shot.adblFollowAim[0], &s_shot.adblFollowAim[1], &s_shot.adblFollowAim[2],
                       &nRead) < 7) {
                fprintf(stderr, "[cinematic] %s:%d: bad follow\n", pszPath, nLine);
                fclose(pFile);
                return 0;
            }
            snprintf(szRest, sizeof(szRest), "%s", psz + 6 + nRead);
            {
                char *pTok;
                for (pTok = strtok(szRest, " \t"); pTok != NULL; pTok = strtok(NULL, " \t")) {
                    if (strcmp(pTok, "rel") == 0) s_shot.bFollowRel = 1;
                    else if (strcmp(pTok, "smooth") == 0 && (pTok = strtok(NULL, " \t")) != NULL)
                        s_shot.dblFollowSmooth = atof(pTok);
                    else if (strcmp(pTok, "from") == 0 && (pTok = strtok(NULL, " \t")) != NULL)
                        s_shot.dblFollowFrom = atof(pTok);
                }
            }
        } else if (strcmp(szKey, "zones") == 0 && nArgs >= 2 && strcmp(szA, "all") == 0) {
            s_shot.bAllZones = 1;
        } else if (strcmp(szKey, "shake") == 0) {
            double *pS;
            int n;
            if (s_shot.nShakes >= CINE_MAX_SHAKES) {
                fprintf(stderr, "[cinematic] %s:%d: too many shakes\n", pszPath, nLine);
                fclose(pFile);
                return 0;
            }
            pS = s_shot.aShakes[s_shot.nShakes];
            pS[3] = 9.0;
            n = sscanf(psz + 5, "%lf %lf %lf %lf", &pS[0], &pS[1], &pS[2], &pS[3]);
            if (n < 3 || pS[1] <= pS[0]) {
                fprintf(stderr, "[cinematic] %s:%d: bad shake\n", pszPath, nLine);
                fclose(pFile);
                return 0;
            }
            s_shot.nShakes++;
        } else if (strcmp(szKey, "ease") == 0 && nArgs >= 3) {
            s_shot.dblEaseIn = atof(szA);
            s_shot.dblEaseOut = atof(szB);
        } else if (strcmp(szKey, "hold") == 0 && nArgs >= 2) {
            s_shot.nHold = atoi(szA) < 1 ? 1 : atoi(szA);
        } else if (strcmp(szKey, "blur") == 0 && nArgs >= 2) {
            s_shot.nBlur = atoi(szA);
        } else if (strcmp(szKey, "shutter") == 0 && nArgs >= 2) {
            s_shot.dblShutterDeg = atof(szA);
        } else if (strcmp(szKey, "players") == 0 && nArgs >= 2) {
            s_shot.nPlayers = atoi(szA);
        } else if (strcmp(szKey, "cast") == 0) {
            if (!parseCast(psz + 4)) {
                fprintf(stderr, "[cinematic] %s:%d: bad cast\n", pszPath, nLine);
                fclose(pFile);
                return 0;
            }
        } else if (strcmp(szKey, "actor") == 0 || strcmp(szKey, "pose") == 0 ||
                   strcmp(szKey, "cart") == 0 || strcmp(szKey, "item") == 0) {
            if (!parseStaging(psz)) {
                fprintf(stderr, "[cinematic] %s:%d: bad %s line\n", pszPath, nLine, szKey);
                fclose(pFile);
                return 0;
            }
        } else if (strcmp(szKey, "key") == 0) {
            CineKey key;
            if (s_shot.nKeys >= CINE_MAX_KEYS || !parseKey(psz + 3, &key)) {
                fprintf(stderr, "[cinematic] %s:%d: bad or too many keys\n", pszPath, nLine);
                fclose(pFile);
                return 0;
            }
            if (s_shot.nKeys > 0 && key.t <= s_shot.aKeys[s_shot.nKeys - 1].t) {
                fprintf(stderr, "[cinematic] %s:%d: key times must increase\n", pszPath, nLine);
                fclose(pFile);
                return 0;
            }
            s_shot.aKeys[s_shot.nKeys++] = key;
        } else {
            fprintf(stderr, "[cinematic] %s:%d: unsupported directive '%s' ignored\n",
                    pszPath, nLine, psz);
        }
    }
    fclose(pFile);
    if (s_shot.nFrames <= 0 || s_shot.nWidth <= 0 || s_shot.nHeight <= 0) {
        fprintf(stderr, "[cinematic] %s: frames and size must be positive\n", pszPath);
        return 0;
    }
    if (s_shot.nBlur < 1 || s_shot.nBlur > CINE_MAX_BLUR ||
        s_shot.dblShutterDeg < 0.0 || s_shot.dblShutterDeg > 360.0) {
        fprintf(stderr, "[cinematic] %s: blur must be 1..%d and shutter 0..360\n",
                pszPath, CINE_MAX_BLUR);
        return 0;
    }
    if (s_shot.nPlayers < 0 || s_shot.nPlayers > CINE_MAX_ACTORS) {
        fprintf(stderr, "[cinematic] %s: players must be 1..%d\n", pszPath, CINE_MAX_ACTORS);
        return 0;
    }
    if (s_shot.nLevel < 0 || s_shot.nLevel > 4 || s_shot.nMode < 0) {
        fprintf(stderr, "[cinematic] %s: level must be 0..4 and mode a known game type\n", pszPath);
        return 0;
    }
    return 1;
}

/* --- Camera path ------------------------------------------------------------ */

/* Ease time warp: camera speed ramps (smoothstep) from 0 over the first
 * dblEaseIn seconds and back to 0 over the last dblEaseOut seconds of the
 * keyed span, rescaled so the warped time still ends on the last key. */
static double easeIntegral(double x, double dblRamp)
{
    /* integral of smoothstep(s / ramp) ds from 0 to x, x in [0, ramp] */
    double u = x / dblRamp;
    return dblRamp * (u * u * u - 0.5 * u * u * u * u);
}

static double easeWarp(double t)
{
    double t0 = s_shot.aKeys[0].t;
    double dblSpan = s_shot.aKeys[s_shot.nKeys - 1].t - t0;
    double a = s_shot.dblEaseIn, b = s_shot.dblEaseOut;
    double x, dblArea, dblTotal;
    if (dblSpan <= 0.0 || (a <= 0.0 && b <= 0.0)) return t;
    if (a + b > dblSpan) {
        double k = dblSpan / (a + b);
        a *= k;
        b *= k;
    }
    x = t - t0;
    if (x <= 0.0) return t0;
    if (x >= dblSpan) return t0 + dblSpan;
    dblTotal = dblSpan - 0.5 * a - 0.5 * b;   /* each ramp covers half its length */
    if (a > 0.0 && x < a) {
        dblArea = easeIntegral(x, a);
    } else if (x <= dblSpan - b) {
        dblArea = 0.5 * a + (x - a);
    } else {
        dblArea = dblTotal - easeIntegral(dblSpan - x, b);
    }
    return t0 + dblArea / dblTotal * dblSpan;
}

/* Catmull-Rom through the keys with the key times as knots (Barry-Goldman
 * pyramid), so velocity stays continuous across keys. The ends extrapolate
 * a phantom key; t is clamped to the keyed span. */
static void keyPoint(int i, int nComp, double *pdblT, double *pdblV)
{
    const CineKey *k = s_shot.aKeys;
    int n = s_shot.nKeys;
    int j;
    if (i < 0 || i >= n) {
        int e = (i < 0) ? 0 : n - 1;
        int f = (i < 0) ? 1 : n - 2;
        *pdblT = 2.0 * k[e].t - k[f].t;
        for (j = 0; j < 3; j++) {
            double ve = nComp ? k[e].a[j] : k[e].p[j];
            double vf = nComp ? k[f].a[j] : k[f].p[j];
            pdblV[j] = 2.0 * ve - vf;
        }
        pdblV[3] = 2.0 * k[e].fov - k[f].fov;
        return;
    }
    *pdblT = k[i].t;
    for (j = 0; j < 3; j++) pdblV[j] = nComp ? k[i].a[j] : k[i].p[j];
    pdblV[3] = k[i].fov;
}

static void lerpKnots(const double *a, double ta, const double *b, double tb, double t, double *out)
{
    double w = (tb - ta) != 0.0 ? (t - ta) / (tb - ta) : 0.0;
    int j;
    for (j = 0; j < 4; j++) out[j] = a[j] + (b[j] - a[j]) * w;
}

static void cameraSample(double t, double *pdblPos, double *pdblAim, double *pdblFov)
{
    const CineKey *k = s_shot.aKeys;
    int n = s_shot.nKeys;
    int nSeg = 0, c;
    if (n == 1 || t <= k[0].t || t >= k[n - 1].t) {
        const CineKey *pK = (n == 1 || t <= k[0].t) ? &k[0] : &k[n - 1];
        memcpy(pdblPos, pK->p, sizeof(pK->p));
        memcpy(pdblAim, pK->a, sizeof(pK->a));
        *pdblFov = pK->fov;
        return;
    }
    while (nSeg + 1 < n - 1 && t >= k[nSeg + 1].t) nSeg++;
    for (c = 0; c < 2; c++) {
        double t0, t1, t2, t3, P0[4], P1[4], P2[4], P3[4];
        double A1[4], A2[4], A3[4], B1[4], B2[4], C[4];
        keyPoint(nSeg - 1, c, &t0, P0);
        keyPoint(nSeg, c, &t1, P1);
        keyPoint(nSeg + 1, c, &t2, P2);
        keyPoint(nSeg + 2, c, &t3, P3);
        lerpKnots(P0, t0, P1, t1, t, A1);
        lerpKnots(P1, t1, P2, t2, t, A2);
        lerpKnots(P2, t2, P3, t3, t, A3);
        lerpKnots(A1, t0, A2, t2, t, B1);
        lerpKnots(A2, t1, A3, t3, t, B2);
        lerpKnots(B1, t1, B2, t2, t, C);
        memcpy(c ? pdblAim : pdblPos, C, 3 * sizeof(double));
        if (c == 0) *pdblFov = C[3];
    }
}

static void cameraSet(const double *pdblPos, const double *pdblAim, double dblFov)
{
    SceneCameraBlock *cb = (SceneCameraBlock *)g_pSceneRoot;
    sceneObjSetPos(g_pSceneRoot, (int)lround(pdblPos[0]), (int)lround(pdblPos[1]),
                   (int)lround(pdblPos[2]), 2);
    sceneNodeFacePos(g_pSceneRoot, 0, (float)pdblAim[0], (float)pdblAim[1], (float)pdblAim[2], 2);
    if (dblFov > CINE_FOV_DEFAULT) dblFov = CINE_FOV_DEFAULT;
    cb->nWidth = (float)(tan(dblFov * 0.5 * CINE_DEG2RAD) / 0.75);
}

static void freecamAim(double *pdblAim)
{
    pdblAim[0] = s_adblFreePos[0] + sin(s_dblYaw) * cos(s_dblPitch) * CINE_FREECAM_LOOK_DIST;
    pdblAim[1] = s_adblFreePos[1] - sin(s_dblPitch) * CINE_FREECAM_LOOK_DIST;
    pdblAim[2] = s_adblFreePos[2] + cos(s_dblYaw) * cos(s_dblPitch) * CINE_FREECAM_LOOK_DIST;
}

static void freecamLookAt(const double *pdblPos, const double *pdblAim)
{
    double d[3];
    int j;
    for (j = 0; j < 3; j++) {
        s_adblFreePos[j] = pdblPos[j];
        d[j] = pdblAim[j] - pdblPos[j];
    }
    s_dblYaw = atan2(d[0], d[2]);
    s_dblPitch = atan2(-d[1], sqrt(d[0] * d[0] + d[2] * d[2]));
}

/* --- Staging ------------------------------------------------------------------ */

/* Gameplay loads these into PlayerRecord.apAnmSets[0..10] (playerSetupRound). */
static const char *const s_apszRecordAnims[11] = {
    "s_pick1", "s_pick2", "s_flpick1", "s_flpick2", "s_throw1", "s_throw2",
    "s_run", "s_stand", "s_grab", "s_oops", "s_winner"
};

/* Find or load an actor's animation set. Extra sets (s_loser, s_clap,
 * cash_sit, ...) are loaded exactly as playerSetupRound loads the built-in
 * ones: anmLoadFile -> anmSetAlloc -> anmSetMeshSlot for the 3 LOD meshes. */
static AnmSet *actorAnimSet(PlayerRecord *pRec, const char *pszAnim)
{
    char szPath[64];
    AnmSet *pSet;
    int k;
    for (k = 0; k < 11; k++)
        if (strcmp(pszAnim, s_apszRecordAnims[k]) == 0) return pRec->apAnmSets[k];
    snprintf(szPath, sizeof(szPath), "anim\\%s.anm", pszAnim);
    pSet = anmSetAlloc(anmLoadFile(szPath, NULL, pRec->pCharSceneObj));
    if (pSet == NULL || pSet->pAnm == NULL) return NULL;
    for (k = 0; k < 3; k++) anmSetMeshSlot(pSet, pRec->pCharMeshSlots[k], k);
    return pSet;
}

static int resolveStaging(void)
{
    int i, k;
    for (i = 0; i < CINE_MAX_ACTORS; i++) {
        CineActor *pA = &s_shot.aActors[i];
        if (!pA->bUsed) continue;
        if (i >= g_nPlayerCount) {
            fprintf(stderr, "[cinematic] actor %d: round has only %d players (use 'players')\n",
                    i, g_nPlayerCount);
            return 0;
        }
        for (k = 0; k < pA->nSegs; k++) {
            CineSegment *pS = &pA->aSegs[k];
            pS->pSet = actorAnimSet(&g_playerRecords[i], pS->szAnim);
            if (pS->pSet == NULL) {
                fprintf(stderr, "[cinematic] actor %d: animation '%s' not found\n", i, pS->szAnim);
                return 0;
            }
        }
    }
    for (i = 0; i < s_shot.nProps; i++) {
        CineProp *pP = &s_shot.aProps[i];
        if (pP->eKind == CINE_PROP_CART) {
            if (pP->nRef >= g_nPlayerCount) {
                fprintf(stderr, "[cinematic] cart %d: round has only %d players\n", pP->nRef, g_nPlayerCount);
                return 0;
            }
            pP->pNode = g_playerRecords[pP->nRef].pCartSceneObj;
        } else {
            void *pMesh = g_apLevelItemSlots[pP->nRef - 1].nMeshId;
            if (pMesh == NULL) {
                fprintf(stderr, "[cinematic] item %d has no mesh in this level\n", pP->nRef);
                return 0;
            }
            /* Same allocation the game uses for its own free props. */
            pP->pNode = (SceneNode *)sceneryObjAlloc(NULL, 0, 0, 0, 0, 0, 0, 0, pMesh);
        }
    }
    return 1;
}

/* Pose an animation set at a fractional frame: point its track cursor at
 * the integer frame and let sceneObjectAnimStepLerp blend toward the next
 * keyframe (it resolves the absolute pose with hold semantics). */
static void animPose(AnmSet *pSet, double dblFrame, int bLoop)
{
    AnmFile *pAnm = pSet->pAnm;
    int nFrame;
    if (pAnm == NULL || pAnm->nFrameCount <= 0) return;
    if (bLoop) {
        dblFrame = fmod(dblFrame, (double)pAnm->nFrameCount);
        if (dblFrame < 0.0) dblFrame += pAnm->nFrameCount;
    } else {
        if (dblFrame > pAnm->nFrameCount - 1) dblFrame = pAnm->nFrameCount - 1;
        if (dblFrame < 0.0) dblFrame = 0.0;
    }
    nFrame = (int)floor(dblFrame);
    pAnm->nFrame = nFrame;
    pAnm->pCurTrack = (AnmTrack *)pAnm->pTrackBase + nFrame;
    sceneObjectAnimStepLerp((SceneObjAnimList *)pSet, (byte)(bLoop ? 1 : 0), (float)(dblFrame - nFrame));
}

static void placeFacing(SceneNode *pNode, const double *p, double dblYawDeg)
{
    double dblYaw = dblYawDeg * CINE_DEG2RAD;
    sceneObjSetPos(pNode, (int)lround(p[0]), (int)lround(p[1]), (int)lround(p[2]), 2);
    sceneNodeFacePos(pNode, 0, (float)(p[0] + sin(dblYaw) * 1000.0), (float)p[1],
                     (float)(p[2] + cos(dblYaw) * 1000.0), 2);
}

static short degToAngle(double dblDeg)
{
    return (short)lround(fmod(dblDeg, 360.0) * CINE_DEG2ANGLE);
}

static void applyStaging(double tShot)
{
    double t = tShot;
    int i;
    for (i = 0; i < CINE_MAX_ACTORS && i < g_nPlayerCount; i++) {
        CineActor *pA = &s_shot.aActors[i];
        if (!pA->bUsed || pA->bHide) continue;
        placeFacing(g_playerRecords[i].pCharSceneNode, pA->p, pA->dblYaw);
        if (pA->nSegs > 0) {
            const CineSegment *pS = &pA->aSegs[0];
            int k;
            for (k = 1; k < pA->nSegs; k++)
                if (t >= pA->aSegs[k].dblStart) pS = &pA->aSegs[k];
            animPose(pS->pSet, pS->dblFrame0 + pS->dblFps * (t - pS->dblStart), !pS->bOnce);
        }
    }
    for (i = 0; i < s_shot.nProps; i++) {
        CineProp *pP = &s_shot.aProps[i];
        if (pP->bHide || pP->pNode == NULL) continue;
        if (pP->eKind == CINE_PROP_CART) {
            placeFacing(pP->pNode, pP->p, pP->adblRot[0] + pP->adblSpin[0] * t);
        } else {
            double t = tShot - pP->dblFrom;   /* item clock (hidden while negative) */
            if (t < 0.0) t = 0.0;
            sceneObjSetPos(pP->pNode, (int)lround(pP->p[0] + pP->adblVel[0] * t),
                           (int)lround(pP->p[1] + pP->adblVel[1] * t + 0.5 * pP->dblGravity * t * t),
                           (int)lround(pP->p[2] + pP->adblVel[2] * t), 2);
            sceneObjSetPosOrient(pP->pNode, degToAngle(pP->adblRot[0] + pP->adblSpin[0] * t),
                                 degToAngle(pP->adblRot[1] + pP->adblSpin[1] * t),
                                 degToAngle(pP->adblRot[2] + pP->adblSpin[2] * t), 2);
        }
    }
}

/* `zones all`: after zoneConnUpdateCulling, show every zone's detail cells
 * the way that function shows the camera's own zone (nLevel 0 + reset
 * flags). Visibility only; the culling pass recomputes it every frame. */
void cinematicZonesOverride(void)
{
    ZoneConn *pConn;
    int i;
    if (!s_bActive || !s_shot.bAllZones) return;
    for (pConn = (ZoneConn *)g_pZoneConnHead; pConn != NULL; pConn = pConn->pPrev) {
        SceneDetailGrid *pGrid = (SceneDetailGrid *)pConn->pDetailLevels;
        for (i = 0; i < pConn->nCount; i++) {
            int nCell = pConn->pMeshIdx[i];
            SceneDetailCell *pCell = &((SceneDetailCell *)pGrid->pRowBuf)[nCell];
            if (pCell->nLevel == -1) {
                pCell->nLevel = 0;
                sceneObjResetFlags((SceneNode *)(size_t)pGrid->pCells[nCell], 2);
            }
        }
    }
}

/* Follow camera state: exponentially smoothed player position and travel
 * heading, advanced on every scene apply (warm-up included, so the filter
 * has settled by the first captured frame). Deterministic: it depends only
 * on the shot's game state and times. */
static int s_bFollowInit;
static double s_adblFollowPos[3];
static double s_dblFollowHeading;
static double s_dblFollowLastT;

static int followCamera(double t, double *pdblPos, double *pdblAim)
{
    int anRaw[4] = {0};
    double dt, a, vx, vz, c, s;
    int j;
    if (s_shot.nFollowSlot < 0 || s_shot.nFollowSlot >= g_nPlayerCount) return 0;
    sceneNodeGetPosWorld(g_playerRecords[s_shot.nFollowSlot].pCharSceneNode, (float *)anRaw, 4);
    if (!s_bFollowInit) {
        for (j = 0; j < 3; j++) s_adblFollowPos[j] = anRaw[j];
        s_dblFollowHeading = 0.0;
        s_dblFollowLastT = t;
        s_bFollowInit = 1;
    }
    dt = t - s_dblFollowLastT;
    s_dblFollowLastT = t;
    if (dt > 0.0) {
        a = 1.0 - exp(-dt / (s_shot.dblFollowSmooth > 0.0 ? s_shot.dblFollowSmooth : 1e-6));
        vx = anRaw[0] - s_adblFollowPos[0];
        vz = anRaw[2] - s_adblFollowPos[2];
        for (j = 0; j < 3; j++) s_adblFollowPos[j] += (anRaw[j] - s_adblFollowPos[j]) * a;
        if (vx * vx + vz * vz > 100.0) {
            /* Heading turns toward the travel direction along the short way. */
            double h = atan2(vx, vz), d = h - s_dblFollowHeading;
            while (d > 3.141592653589793) d -= 2 * 3.141592653589793;
            while (d < -3.141592653589793) d += 2 * 3.141592653589793;
            s_dblFollowHeading += d * (1.0 - exp(-dt / 0.4));
        }
    }
    if (t < s_shot.dblFollowFrom) return 0;
    c = s_shot.bFollowRel ? cos(s_dblFollowHeading) : 1.0;
    s = s_shot.bFollowRel ? sin(s_dblFollowHeading) : 0.0;
    /* Player frame: +z ahead, +x right (yaw convention of placeFacing). */
    pdblPos[0] = s_adblFollowPos[0] + s_shot.adblFollowOff[0] * c + s_shot.adblFollowOff[2] * s;
    pdblPos[1] = s_adblFollowPos[1] + s_shot.adblFollowOff[1];
    pdblPos[2] = s_adblFollowPos[2] - s_shot.adblFollowOff[0] * s + s_shot.adblFollowOff[2] * c;
    pdblAim[0] = s_adblFollowPos[0] + s_shot.adblFollowAim[0] * c + s_shot.adblFollowAim[2] * s;
    pdblAim[1] = s_adblFollowPos[1] + s_shot.adblFollowAim[1];
    pdblAim[2] = s_adblFollowPos[2] - s_shot.adblFollowAim[0] * s + s_shot.adblFollowAim[2] * c;
    return 1;
}

/* Camera shake: incommensurate sines per axis, decaying linearly over
 * each shake's span; the aim point moves by half as much so the view
 * jolts rather than drifts. */
static void applyShake(double t, double *pdblPos, double *pdblAim)
{
    static const double adblRatio[3] = { 1.0, 1.37, 0.83 };
    int s, j;
    for (s = 0; s < s_shot.nShakes; s++) {
        const double *pS = s_shot.aShakes[s];
        double dblAmp;
        if (t < pS[0] || t > pS[1]) continue;
        dblAmp = pS[2] * (1.0 - (t - pS[0]) / (pS[1] - pS[0]));
        for (j = 0; j < 3; j++) {
            double w = 2.0 * 3.141592653589793 * pS[3] * adblRatio[j];
            double d = dblAmp * (0.7 * sin(w * t + j * 1.9) + 0.3 * sin(2.3 * w * t + j * 0.7));
            pdblPos[j] += d;
            pdblAim[j] += 0.5 * d;
        }
    }
}

void cinematicSceneApply(void)
{
    double adblPos[3], adblAim[3], dblFov;
    if (!s_bActive) return;
    applyStaging(s_dblShotSec);
    if (s_bFreecam) {
        if (s_eFreeMode == FREECAM_OFF) return;
        if (s_eFreeMode == FREECAM_FOLLOW) {
            /* Adopt the game camera's pose the first time we take over. */
            int anCam[4] = {0}, anAim[4] = {0};
            sceneNodeGetPos(g_pSceneRoot, 0, anCam, 4);
            sceneNodeGetPosWorld(g_pCamAimNode, (float *)anAim, 4);
            adblPos[0] = anCam[0]; adblPos[1] = anCam[1]; adblPos[2] = anCam[2];
            adblAim[0] = anAim[0]; adblAim[1] = anAim[1]; adblAim[2] = anAim[2];
            freecamLookAt(adblPos, adblAim);
            s_eFreeMode = FREECAM_FLY;
        }
        if (s_eFreeMode == FREECAM_PLAY) {
            cameraSample(easeWarp(s_dblShotSec), adblPos, adblAim, &dblFov);
        } else {
            memcpy(adblPos, s_adblFreePos, sizeof(adblPos));
            freecamAim(adblAim);
            dblFov = s_dblFreeFov;
        }
        cameraSet(adblPos, adblAim, dblFov);
        return;
    }
    if (s_shot.nFollowSlot >= 0) {
        dblFov = s_shot.nKeys > 0 ? s_shot.aKeys[0].fov : CINE_FOV_DEFAULT;
        if (followCamera(s_dblShotSec, adblPos, adblAim)) {
            applyShake(s_dblShotSec, adblPos, adblAim);
            cameraSet(adblPos, adblAim, dblFov);
            return;
        }
        /* before `from`: fall through to keys / the game camera */
    }
    if (s_shot.nKeys == 0) {          /* keep the game's follow camera */
        if (s_bCapturing && s_bTrace) {
            int anCam[4] = {0}, anAim[4] = {0};
            sceneNodeGetPos(g_pSceneRoot, 0, anCam, 4);
            sceneNodeGetPosWorld(g_pCamAimNode, (float *)anAim, 4);
            fprintf(stderr, "[trace] %.4f %d %d %d %d %d %d follow\n", s_dblShotSec,
                    anCam[0], anCam[1], anCam[2], anAim[0], anAim[1], anAim[2]);
        }
        return;
    }
    cameraSample(easeWarp(s_dblShotSec), adblPos, adblAim, &dblFov);
    applyShake(s_dblShotSec, adblPos, adblAim);
    cameraSet(adblPos, adblAim, dblFov);
    if (s_bCapturing && s_bTrace)
        fprintf(stderr, "[trace] %.4f %.3f %.3f %.3f %.3f %.3f %.3f %.3f\n", s_dblShotSec,
                adblPos[0], adblPos[1], adblPos[2], adblAim[0], adblAim[1], adblAim[2], dblFov);
}

/* --- Init and queries --------------------------------------------------------- */

int cinematicInit(int argc, char **argv)
{
    const char *pszShot = NULL;
    const char *pszOut = NULL;
    int i;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--cinematic") == 0 && i + 1 < argc)
            pszShot = argv[++i];
        else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc)
            pszOut = argv[++i];
        else if (strcmp(argv[i], "--freecam") == 0)
            s_bFreecam = 1;
        else if (strcmp(argv[i], "--final") == 0)
            s_bFinal = 1;
        else if (strcmp(argv[i], "--preview") == 0)
            s_bPreview = 1;
    }
    if (pszShot == NULL) return 0;
    snprintf(s_szShotPath, sizeof(s_szShotPath), "%s", pszShot);
    if (!parseShot(pszShot)) return -1;
    if (pszOut != NULL)
        snprintf(s_szOutPath, sizeof(s_szOutPath), "%s", pszOut);
    else
        snprintf(s_szOutPath, sizeof(s_szOutPath), "build/trailer/%s/%s.mkv",
                 s_bPreview ? "clips_preview" : "clips", s_shot.szName);
    if (s_bPreview) {
        /* Depth-of-field blur (MANIAC_DOF) is a circle-of-confusion radius in
         * output pixels; scale it with the smaller preview frame so previews
         * show the same relative blur as the full-size render. */
        const char *pszDof = getenv("MANIAC_DOF");
        s_bFinal = 0;
        if (pszDof != NULL) {
            char szDof[32];
            snprintf(szDof, sizeof(szDof), "%g", atof(pszDof) / CINE_PREVIEW_DIV);
            setenv("MANIAC_DOF", szDof, 1);
        }
    }
    s_bSurvey = getenv("MANIAC_CINE_SURVEY") != NULL && atoi(getenv("MANIAC_CINE_SURVEY")) != 0;
    /* Capture never needs a sound device; keep any explicit choice. */
    if (!s_bFreecam) setenv("SDL_AUDIODRIVER", "dummy", 0);
    s_bTrace = getenv("MANIAC_CINE_TRACE") != NULL && atoi(getenv("MANIAC_CINE_TRACE")) != 0;
    s_bActive = 1;
    return 1;
}

int cinematicActive(void)
{
    return s_bActive;
}

int cinematicFreecam(void)
{
    return s_bActive && s_bFreecam;
}

void cinematicWindowSize(int *pnWidth, int *pnHeight)
{
    if (s_bFreecam) {
        /* Same aspect as the capture, at a size that fits on screen. */
        *pnWidth = CINE_FREECAM_PREVIEW_WIDTH;
        *pnHeight = (int)lround((double)CINE_FREECAM_PREVIEW_WIDTH * s_shot.nHeight / s_shot.nWidth);
        return;
    }
    if (s_bPreview) {
        *pnWidth = (s_shot.nWidth / CINE_PREVIEW_DIV) & ~1;
        *pnHeight = (s_shot.nHeight / CINE_PREVIEW_DIV) & ~1;
        return;
    }
    *pnWidth = s_shot.nWidth;
    *pnHeight = s_shot.nHeight;
}

unsigned int cinematicTicks(void)
{
    return (unsigned int)(s_dblGameMs + 0.5);
}

int cinematicHideHud(void)
{
    return s_bActive;
}

/* Nodes hidden around sceneRender: the guidance arrows plus hidden actors
 * and carts. Each subtree's bType bytes are saved and restored exactly
 * (LOD slots may already be hidden by the detail grid), so the hiding
 * never leaks into game state. */
#define CINE_MAX_HIDDEN 512
static SceneNode *s_apHidden[CINE_MAX_HIDDEN];
static unsigned char s_abHiddenType[CINE_MAX_HIDDEN];
static int s_nHidden;

static void hideTree(SceneNode *pNode)
{
    SceneNode *pChild;
    if (pNode == NULL || s_nHidden >= CINE_MAX_HIDDEN) return;
    s_apHidden[s_nHidden] = pNode;
    s_abHiddenType[s_nHidden++] = pNode->bType;
    pNode->bType = 1;   /* hidden, as sceneNodeSetHiddenFlag mode 1 */
    for (pChild = pNode->pChild; pChild != NULL; pChild = pChild->pNextSib) hideTree(pChild);
}

void cinematicHideOverlays(void)
{
    int i;
    s_nHidden = 0;
    if (!cinematicHideHud()) return;
    hideTree(g_pGoodsArrowObj);
    hideTree(g_pCartArrowObj);
    for (i = 0; i < CINE_MAX_ACTORS && i < g_nPlayerCount; i++)
        if (s_shot.aActors[i].bUsed && s_shot.aActors[i].bHide)
            hideTree(g_playerRecords[i].pCharSceneNode);
    for (i = 0; i < s_shot.nProps; i++) {
        const CineProp *pP = &s_shot.aProps[i];
        if (pP->eKind == CINE_PROP_CART && pP->bHide && pP->nRef < g_nPlayerCount)
            hideTree(g_playerRecords[pP->nRef].pCartSceneObj);
        else if (pP->eKind == CINE_PROP_ITEM && s_dblShotSec < pP->dblFrom)
            hideTree(pP->pNode);
    }
}

void cinematicRestoreOverlays(void)
{
    while (s_nHidden > 0) {
        s_nHidden--;
        s_apHidden[s_nHidden]->bType = s_abHiddenType[s_nHidden];
    }
}

int cinematicGuides(void)
{
    return s_bActive && s_bFreecam && s_bGuides;
}

/* --- Capture -------------------------------------------------------------- */

int cinematicWantsFrame(void)
{
    return s_bActive && s_bCapturing && !s_bFailed;
}

unsigned char *cinematicFrameBuffer(int nWidth, int nHeight)
{
    if (s_pFrame == NULL || nWidth != s_nFrameW || nHeight != s_nFrameH) {
        if (s_pEncoder != NULL) {
            /* The encoder was opened for a fixed size; a resize mid-clip
             * would corrupt the raw stream. */
            appLog("[cinematic] drawable changed %dx%d -> %dx%d mid-clip",
                   s_nFrameW, s_nFrameH, nWidth, nHeight);
            s_bFailed = 1;
        }
        free(s_pFrame);
        s_pFrame = (unsigned char *)malloc((size_t)nWidth * (size_t)nHeight * 3);
        s_nFrameW = nWidth;
        s_nFrameH = nHeight;
    }
    return s_pFrame;
}

static int openEncoder(void)
{
    char szCmd[2048];
    snprintf(szCmd, sizeof(szCmd),
             "ffmpeg -y -loglevel error -f rawvideo -pix_fmt rgb24 -s %dx%d -r %s -i - "
             "-vf vflip -c:v ffv1 -level 3 -pix_fmt bgr0 '%s'",
             s_nFrameW, s_nFrameH, s_bPreview ? "12.5" : "25", s_szOutPath);
    s_pEncoder = popen(szCmd, "w");
    if (s_pEncoder == NULL) {
        appLog("[cinematic] cannot start encoder: %s", szCmd);
        return 0;
    }
    appLog("[cinematic] encoding %dx%d @%d fps -> %s", s_nFrameW, s_nFrameH, CINE_FPS, s_szOutPath);
    return 1;
}

/* Each rendered sub-frame is added to the accumulator; writeFrame then
 * emits the average as one video frame. */
void cinematicSubmitFrame(void)
{
    size_t nBytes = (size_t)s_nFrameW * (size_t)s_nFrameH * 3;
    size_t i;
    if (s_bFailed || s_pFrame == NULL) return;
    if (s_pAccum == NULL) {
        s_pAccum = (unsigned short *)calloc(nBytes, sizeof(unsigned short));
        if (s_pAccum == NULL) {
            s_bFailed = 1;
            return;
        }
    }
    for (i = 0; i < nBytes; i++) s_pAccum[i] += s_pFrame[i];
    s_nAccumCount++;
}

static void writeFrame(void)
{
    size_t nBytes = (size_t)s_nFrameW * (size_t)s_nFrameH * 3;
    size_t i;
    int nHalf = s_nAccumCount / 2;
    if (s_bFailed || s_nAccumCount == 0) return;
    for (i = 0; i < nBytes; i++) {
        s_pFrame[i] = (unsigned char)((s_pAccum[i] + nHalf) / s_nAccumCount);
        s_pAccum[i] = 0;
    }
    s_nAccumCount = 0;
    if (s_pEncoder == NULL && !openEncoder()) {
        s_bFailed = 1;
        return;
    }
    if (fwrite(s_pFrame, 1, nBytes, s_pEncoder) != nBytes) {
        appLog("[cinematic] encoder write failed at frame %d", s_nFramesWritten);
        s_bFailed = 1;
        return;
    }
    s_nFramesWritten++;
}

/* `hold`: write the last finished frame again (s_pFrame keeps it, since
 * glFlip only reads back while capturing). */
static void repeatFrame(void)
{
    size_t nBytes = (size_t)s_nFrameW * (size_t)s_nFrameH * 3;
    if (s_bFailed || s_pEncoder == NULL || s_pFrame == NULL) return;
    if (fwrite(s_pFrame, 1, nBytes, s_pEncoder) != nBytes) {
        appLog("[cinematic] encoder write failed at frame %d", s_nFramesWritten);
        s_bFailed = 1;
        return;
    }
    s_nFramesWritten++;
}

/* --- Run loops -------------------------------------------------------------- */

static int startRound(void)
{
    /* Same sequence the menus run: game type, character pick, level init
     * (playerSetupCharacters + "run <level>"). */
    s_apfnModeInit[s_shot.nMode](0, 0, 0);
    g_nCharSelIdx = s_shot.nCast > 0 ? s_shot.anCast[0] : s_shot.nCharacter;
    stateCharSelectOk(0, 0, 0);
    if (s_shot.nPlayers > 0) g_nPlayerCount = s_shot.nPlayers;
    if (s_shot.nCast > 0) {
        /* stateLevelInitN with the cast applied between character setup and
         * the run command: g_nLevelIdx, playerSetupCharacters,
         * unloadGameWorld, "run <level>". */
        char szRun[16];
        int i;
        g_nLevelIdx = s_shot.nLevel;
        playerSetupCharacters();
        for (i = 0; i < s_shot.nCast && i < g_nPlayerCount; i++)
            g_playerRecords[i].nCharIdx = s_shot.anCast[i];
        unloadGameWorld();
        snprintf(szRun, sizeof(szRun), "run %d", s_shot.nLevel);
        commandDispatch(0, szRun);
    } else {
        s_apfnLevelInit[s_shot.nLevel](0, 0, 0);
    }
    if (g_bGameActive == 0) {
        appLog("[cinematic] round for level %d did not start", s_shot.nLevel);
        return 0;
    }
    return resolveStaging();
}

static void printPose(const char *pszTag, double t)
{
    double adblAim[3];
    freecamAim(adblAim);
    fprintf(stderr, "%s %.2f %.0f %.0f %.0f %.0f %.0f %.0f %.1f\n", pszTag, t,
            s_adblFreePos[0], s_adblFreePos[1], s_adblFreePos[2],
            adblAim[0], adblAim[1], adblAim[2], s_dblFreeFov);
}

/* K: append the current pose as a key line to the shot file. The first key
 * of an empty shot is at t 0; each later one is the previous key time plus
 * the seconds since the last K press (2 s when the shot already had keys at
 * startup), so the recorded rhythm becomes the path timing. */
static void freecamRecordKey(double dblNowSec, double *pdblLastKeySec)
{
    CineKey key;
    FILE *pFile;
    if (s_shot.nKeys >= CINE_MAX_KEYS) {
        fprintf(stderr, "[freecam] key limit reached\n");
        return;
    }
    if (s_shot.nKeys == 0)
        key.t = 0.0;
    else if (*pdblLastKeySec < 0.0)
        key.t = s_shot.aKeys[s_shot.nKeys - 1].t + 2.0;
    else
        key.t = s_shot.aKeys[s_shot.nKeys - 1].t + (dblNowSec - *pdblLastKeySec);
    key.t = floor(key.t * 10.0 + 0.5) / 10.0;
    if (s_shot.nKeys > 0 && key.t <= s_shot.aKeys[s_shot.nKeys - 1].t)
        key.t = s_shot.aKeys[s_shot.nKeys - 1].t + 0.1;
    memcpy(key.p, s_adblFreePos, sizeof(key.p));
    freecamAim(key.a);
    key.fov = s_dblFreeFov;
    s_shot.aKeys[s_shot.nKeys++] = key;
    *pdblLastKeySec = dblNowSec;

    pFile = fopen(s_szShotPath, "a");
    if (pFile == NULL) {
        fprintf(stderr, "[freecam] cannot append to %s\n", s_szShotPath);
        return;
    }
    fprintf(pFile, "key %6.2f   %6.0f %6.0f %6.0f   %6.0f %6.0f %6.0f   %.1f\n",
            key.t, key.p[0], key.p[1], key.p[2], key.a[0], key.a[1], key.a[2], key.fov);
    fclose(pFile);
    fprintf(stderr, "[freecam] key %d at t=%.2f appended to %s\n", s_shot.nKeys, key.t, s_szShotPath);
}

static int runFreecam(void)
{
    double dblDt = 1.0 / CINE_FREECAM_FPS;
    double dblNowSec = 0.0;
    unsigned int nWallPrev;
    double dblLastKeySec = -1.0;
    int bFrozen = (s_shot.dblWorldSpeed == 0.0);
    int nWarmup = (int)(s_shot.dblStartSec * CINE_FREECAM_FPS + 0.5);
    int i;

    /* Warm up without input so the round reaches the shot's start time. */
    for (i = 0; i < nWarmup; i++) {
        if (!platformPumpEvents()) return 0;
        s_dblGameMs += 1000.0 * dblDt * s_shot.dblWorldSpeed;
        gameRunFrame(0);
    }
    s_eFreeMode = FREECAM_FOLLOW;
    if (s_shot.nKeys > 0) {
        freecamLookAt(s_shot.aKeys[0].p, s_shot.aKeys[0].a);
        s_dblFreeFov = s_shot.aKeys[0].fov;
        s_eFreeMode = FREECAM_FLY;
    }
    fprintf(stderr, "[freecam] mouse look, WASD move, Q/E down/up, Shift fast, wheel speed, Z/X FOV,\n"
                    "[freecam] K add key, Enter preview path, T freeze time, G guides, P print, Esc quit\n");

    /* Live freecam runs on wall time so the recorded key rhythm and the
     * Enter preview match what was flown, whatever the frame rate. */
    nWallPrev = platformWallTicks();
    while (g_bGameActive != 0) {
        CineInput in;
        double adblMove[3] = {0.0, 0.0, 0.0};
        double dblSpeed;
        unsigned int nWall;
        int j;
        if (!platformPumpEvents()) break;
        nWall = platformWallTicks();
        dblDt = (nWall - nWallPrev) / 1000.0;
        nWallPrev = nWall;
        if (dblDt > 0.1) dblDt = 0.1;
        platformCineInput(&in);
        if (in.bQuit) break;

        s_dblYaw += in.nMouseDX * CINE_MOUSE_RAD;
        s_dblPitch -= in.nMouseDY * CINE_MOUSE_RAD;
        if (s_dblPitch > 1.5) s_dblPitch = 1.5;
        if (s_dblPitch < -1.5) s_dblPitch = -1.5;
        if (in.nWheel != 0) {
            s_dblSpeedScale *= pow(1.25, in.nWheel);
            fprintf(stderr, "[freecam] speed x%.2f\n", s_dblSpeedScale);
        }
        if (in.bFovIn || in.bFovOut) {
            s_dblFreeFov += in.bFovOut ? 20.0 * dblDt : -20.0 * dblDt;
            if (s_dblFreeFov > CINE_FOV_DEFAULT) s_dblFreeFov = CINE_FOV_DEFAULT;
            if (s_dblFreeFov < 5.0) s_dblFreeFov = 5.0;
        }
        /* Forward/right in the ground plane; up is -y. */
        if (in.bForward) { adblMove[0] += sin(s_dblYaw); adblMove[2] += cos(s_dblYaw); }
        if (in.bBack)    { adblMove[0] -= sin(s_dblYaw); adblMove[2] -= cos(s_dblYaw); }
        if (in.bRight)   { adblMove[0] += cos(s_dblYaw); adblMove[2] -= sin(s_dblYaw); }
        if (in.bLeft)    { adblMove[0] -= cos(s_dblYaw); adblMove[2] += sin(s_dblYaw); }
        if (in.bUp)   adblMove[1] -= 1.0;
        if (in.bDown) adblMove[1] += 1.0;
        dblSpeed = CINE_FREECAM_SPEED * s_dblSpeedScale * (in.bFast ? 4.0 : 1.0) * dblDt;
        for (j = 0; j < 3; j++) s_adblFreePos[j] += adblMove[j] * dblSpeed;

        if (in.bGuides) s_bGuides = !s_bGuides;
        if (in.bFreeze) {
            bFrozen = !bFrozen;
            fprintf(stderr, "[freecam] game time %s\n", bFrozen ? "frozen" : "running");
        }
        if (in.bPrint) printPose("[freecam] pose", dblNowSec);
        if (in.bRecord) freecamRecordKey(dblNowSec, &dblLastKeySec);
        if (in.bPlay) {
            if (s_eFreeMode == FREECAM_PLAY) {
                s_eFreeMode = FREECAM_FLY;
            } else if (s_shot.nKeys > 0) {
                s_eFreeMode = FREECAM_PLAY;
                s_dblShotSec = s_shot.aKeys[0].t;
                fprintf(stderr, "[freecam] preview %.2f..%.2f s\n",
                        s_shot.aKeys[0].t, s_shot.aKeys[s_shot.nKeys - 1].t);
            }
        }
        if (s_eFreeMode == FREECAM_PLAY) {
            s_dblShotSec += dblDt;
            if (s_dblShotSec > s_shot.aKeys[s_shot.nKeys - 1].t + 0.5) {
                double adblPos[3], adblAim[3], dblFov;
                cameraSample(s_shot.aKeys[s_shot.nKeys - 1].t, adblPos, adblAim, &dblFov);
                freecamLookAt(adblPos, adblAim);
                s_eFreeMode = FREECAM_FLY;
            }
        }

        dblNowSec += dblDt;
        if (s_eFreeMode != FREECAM_PLAY) s_dblShotSec = dblNowSec;
        if (!bFrozen) s_dblGameMs += 1000.0 * dblDt * (s_shot.dblWorldSpeed != 0.0 ? s_shot.dblWorldSpeed : 1.0);
        gameRunFrame(1);
    }
    return 0;
}

/* MANIAC_CINE_SURVEY: item spawn points (as roundStartInit places them:
 * x = event origin Z, y = height, z = origin X) once, then every player. */
static void surveyFrame(int nFrame)
{
    int i;
    if (nFrame == 0) {
        for (i = 0; i < LEVEL_ITEM_SLOT_COUNT; i++) {
            const LevelItemSlot *pSlot = &g_apLevelItemSlots[i];
            if (pSlot->pEventObj == NULL) continue;
            fprintf(stderr, "[survey] item %d %d %d %d %s\n", i + 1, (int)pSlot->pEventObj->flPosZ,
                    (int)pSlot->pEventObj->flHeight, (int)pSlot->pEventObj->flPosX, pSlot->szName);
        }
    }
    for (i = 0; i < g_nPlayerCount; i++) {
        int anPos[4] = {0};
        sceneNodeGetPosWorld(g_playerRecords[i].pCharSceneNode, (float *)anPos, 4);
        fprintf(stderr, "[survey] player %d %d %d %d %d\n", nFrame, i, anPos[0], anPos[1], anPos[2]);
    }
}

static int runCapture(void)
{
    int nWarmup = (int)(s_shot.dblStartSec * CINE_FPS + 0.5);
    int nTotal = nWarmup + s_shot.nFrames;
    int nBlur = s_bFinal ? s_shot.nBlur : 1;
    double dblShutterSec = s_shot.dblShutterDeg / 360.0 / CINE_FPS;
    double dblTickBoundaryMs;
    int i, k;
    int nStatus = 0;

    /* The fixed-step loop ticks once getGameTime() reaches g_nGameTime +
     * g_nObjUpdateTime; getGameTime() trails the virtual clock by a constant.
     * Translate the round's tick phase into virtual ms so every frame can be
     * centred mid-tick: with one tick per frame and a shutter shorter than a
     * tick, no sub-frame window crosses a tick, so moving objects stay sharp
     * and evenly spaced while the camera (sampled per sub-frame) blurs. */
    dblTickBoundaryMs = s_dblGameMs - getGameTime() + g_nGameTime;
    if (g_nObjUpdateTime * CINE_FPS != 1000)
        appLog("[cinematic] obj_update_time %d ms is not one %d fps frame; ticks will drift",
               g_nObjUpdateTime, CINE_FPS);

    appLog("[cinematic] shot '%s': level %d, %d warm-up + %d frames, %d keys, world speed %.3f, "
           "%s, blur %d x %.0f deg", s_shot.szName, s_shot.nLevel, nWarmup, s_shot.nFrames,
           s_shot.nKeys, s_shot.dblWorldSpeed, s_bFinal ? "final" : "draft", nBlur,
           s_shot.dblShutterDeg);

    /* Frame i sits half a tick after tick boundary i + 1. Final frames render
     * nBlur sub-frames spread evenly over the open shutter (centred on the
     * frame time, so shot time t still means frame t*25) and average them;
     * game and camera time both move per sub-frame. */
    for (i = 0; i < nTotal && !s_bFailed && g_bGameActive != 0; i++) {
        int nSub = (i >= nWarmup) ? nBlur : 1;
        double dblFrameSec = (double)(i + 1) / CINE_FPS;
        for (k = 0; k < nSub; k++) {
            double dblOffset = (nSub > 1) ? ((k + 0.5) / nSub - 0.5) * dblShutterSec : 0.0;
            if (!platformPumpEvents()) {
                appLog("[cinematic] window closed at frame %d", i);
                s_bFailed = 1;
                break;
            }
            s_dblGameMs = dblTickBoundaryMs + 0.5 * g_nObjUpdateTime +
                          (dblFrameSec + dblOffset) * 1000.0 * s_shot.dblWorldSpeed;
            s_dblShotSec = (double)(i - nWarmup) / CINE_FPS + dblOffset;
            /* --preview keeps even shot frames only (12.5 fps); `hold n`
             * renders every n-th frame and repeats it below. */
            s_bCapturing = (i >= nWarmup) && (!s_bPreview || ((i - nWarmup) & 1) == 0) &&
                           ((i - nWarmup) % s_shot.nHold == 0);
            gameRunFrame(1);
            if (s_bSurvey && i >= nWarmup && k == 0) surveyFrame(i - nWarmup);
            if (s_bCapturing && s_bTrace && g_nPlayerCount > 1) {
                int anPos[4] = {0};
                sceneNodeGetPosWorld(g_playerRecords[1].pCharSceneObj, (float *)anPos, 4);
                fprintf(stderr, "[ptrace] %d %d %.5f %d %d %d %d\n", i - nWarmup, k,
                        s_dblShotSec, anPos[0], anPos[1], anPos[2], g_nGameTime);
            }
            if (g_bGameActive == 0) {
                appLog("[cinematic] round ended at frame %d", i);
                break;
            }
        }
        if (s_bCapturing) {
            writeFrame();
        } else if (i >= nWarmup && (!s_bPreview || ((i - nWarmup) & 1) == 0) && s_nFramesWritten > 0) {
            repeatFrame();   /* `hold`: the previous frame again */
        }
    }
    s_bCapturing = 0;

    if (s_pEncoder != NULL) {
        nStatus = pclose(s_pEncoder);
        s_pEncoder = NULL;
    }
    free(s_pFrame);
    s_pFrame = NULL;
    free(s_pAccum);
    s_pAccum = NULL;
    appLog("[cinematic] wrote %d/%d frames to %s (encoder status %d)",
           s_nFramesWritten, s_shot.nFrames, s_szOutPath, nStatus);
    fprintf(stderr, "[cinematic] wrote %d/%d frames to %s\n",
            s_nFramesWritten, s_shot.nFrames, s_szOutPath);
    {
        int nExpected = s_bPreview ? (s_shot.nFrames + 1) / 2 : s_shot.nFrames;
        return (s_bFailed || nStatus != 0 || s_nFramesWritten != nExpected) ? 1 : 0;
    }
}

int cinematicRun(void)
{
    if (!startRound()) return 1;
    return s_bFreecam ? runFreecam() : runCapture();
}
