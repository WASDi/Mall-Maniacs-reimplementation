#ifndef CONFIG_H
#define CONFIG_H

#include <windows.h>
#include "mstring.h"

/* =====================================================================
 * config.h — the .cfg (DFF) database read from maniac.cfg. The original
 * represents the three node kinds as C++ objects behind a one-slot
 * vtable whose only virtual is the type query (configNodeTypeBlock/
 * Value/String @0x436590/0x4365b0/0x4367d0 returning 0/1/2). The rebuild
 * keeps the exact layouts and stores the type id directly in nType.
 *
 * Grammar (configParseFile): tokens separated by whitespace/','; the
 * single-char tokens '=' '[' ']' '{' '}' structure the file; "..." are
 * string literals; "key = value" creates value/string nodes; "name { }"
 * a single anonymous block; "name[] { } { } ..." an indexed block array
 * (configEnvFind addresses entries as "name[2]/sub"). Comments run from
 * '/' to end of line.
 * ===================================================================== */

#define CONFIG_NODE_BLOCK  0    /* configNodeTypeBlock  @0x436590 */
#define CONFIG_NODE_VALUE  1    /* configNodeTypeValue  @0x4365b0 */
#define CONFIG_NODE_STRING 2    /* configNodeTypeString @0x4367d0 */

typedef struct ConfigToken {
    MString text;               /* +0x00 token text (quotes included) */
    struct ConfigToken *pNext;  /* +0x08 */
    int nDepth;                 /* +0x0c */
    struct ConfigToken *pChild; /* +0x10 inner list of a '{' token */
} ConfigToken;                  /* 0x14 */

typedef struct ConfigNode {
    int nType;                  /* +0x00 type id (original: pVtbl slot 0) */
    MString key;                /* +0x04 key name */
    struct ConfigNode *pChild;  /* +0x0c first child */
    struct ConfigNode *pParent; /* +0x10 */
    struct ConfigNode *pPrev;   /* +0x14 */
    struct ConfigNode *pNext;   /* +0x18 */
} ConfigNode;                   /* 0x1c */

typedef struct ConfigBlockNode {
    ConfigNode base;            /* 0x1c */
    int nCount;                 /* +0x1c number of blocks in the array */
    int nIndex;                 /* +0x20 index of this block */
    struct ConfigNode *pFirst;  /* +0x24 */
    struct ConfigNode *pLast;   /* +0x28 */
} ConfigBlockNode;              /* 0x2c */

typedef struct ConfigStringNode {
    ConfigNode base;            /* 0x1c */
    MString value;              /* +0x1c */
} ConfigStringNode;             /* 0x24 */

typedef struct ConfigValueNode {
    ConfigNode base;            /* 0x1c */
    int _pad1c;                 /* +0x1c */
    double dValue;              /* +0x20 */
} ConfigValueNode;              /* 0x28 */

typedef struct ConfigEnv {
    MString name;               /* +0x00 file name (configEnvSetName) */
    ConfigToken *pTokenHead;    /* +0x08 */
    ConfigToken *pTokenTail;    /* +0x0c */
    ConfigNode *pRoot;          /* +0x10 parsed node list */
} ConfigEnv;                    /* 0x14 */

/* --- environment / node access --- */
void        configEnvCtor(ConfigEnv *pEnv);                                    /* @0x4357c0 */
void        configEnvSetName(MString *pStr, const char *pPsz);                 /* @0x4354a0 */
int         configParseFile(ConfigEnv *pEnv, const char *pPsz);                /* @0x435890 */
ConfigNode *configEnvGetValue(ConfigEnv *pEnv, ConfigNode *pNode, const char *pKey); /* @0x436560 */
ConfigNode *configEnvGetValueByIndex(ConfigEnv *pEnv, const char *pKey); /* @0x436550 */
void        configEnvGetString(MString *pOut, ConfigNode *pNode, const char *pKey); /* @0x436990 */
double      configEnvGetDouble2(ConfigEnv *pEnv, ConfigNode *pNode, const char *pKey); /* @0x436a20 */
MString    *configNodeGetValue(MString *pOut, ConfigNode *pNode);              /* @0x436940 */
double      configEnvGetDouble(ConfigNode *pNode);                             /* @0x4369f0 node-direct */
ConfigNode *configNodeGetId(ConfigNode *pNode);                                /* @0x436850 first child */
ConfigNode *configFindNode(ConfigEnv *pEnv, ConfigNode *pNode, const char *pKey); /* @0x4367e0 */
void        configNodeDtor(ConfigNode *pNode);                                 /* @0x435700 */
ConfigNode *configNextNode(ConfigEnv *pEnv, ConfigNode *pNode);                /* @0x436870 */
void        configEnvFreeChildren(ConfigEnv *pEnv);                            /* @0x4368a0 */
unsigned char *configGetValue(const char *pKey);                               /* @0x408c60 */
void        configMasterLoad(void);                                            /* @0x410350 */
void        configStringDtor(ConfigEnv *pEnv);                                 /* @0x407140 */
void        configMasterEnvCtor(void);                                         /* @0x409b90 */
void        configMasterEnvAtexit(void);                                       /* @0x409ba0 */
void        configMasterEnvInit(void);                                         /* @0x409b80 */

/* movieFrameUpdate @0x40af80 — demo (movie) record/playback (implemented in
 * config.c). */
void        movieFrameUpdate(void);                                            /* @0x40af80 */

/* --- demo/movie database (config-based recorder/player) --- */
ConfigNode *configEnvFindValue(ConfigEnv *pEnv, const char *pKey);             /* @0x4365a0 */
MString    *configNodeGetKey(MString *pOut, ConfigNode *pNode);                /* @0x436900 */
ConfigValueNode *configEnvAddValue(ConfigNode *pParent, const char *pKey,
                                   double dValue);                             /* @0x436eb0 */
ConfigBlockNode *configBlockNodeNew(ConfigEnv *pEnv, ConfigNode *pTail,
                                    const char *pPsz);                         /* @0x436d50 */

/* MovieDb is the config environment + movie-name string + current frame-node
 * cursor the demo recorder/player uses (original 0x455e68..0x455e87). */
typedef struct MovieDb {
    ConfigEnv   env;          /* +0x00 g_pMovieDb @0x455e68 */
    MString     mstrMovie;    /* +0x14 g_movieName @0x455e7c */
    ConfigNode *pFrameNode;   /* +0x1c g_pMovieFrameNode @0x455e84 */
} MovieDb;                    /* 0x20 */

extern MovieDb g_movieDb;      /* @0x455e68 */
extern int     g_nMovieFrame;  /* @0x455e88 */
#define g_pMovieDb       (g_movieDb.env)
#define g_movieName      (g_movieDb.mstrMovie)
#define g_pMovieFrameNode (g_movieDb.pFrameNode)

/* --- master config values (configMasterLoad) --- */
extern ConfigEnv g_configEnvMaster;    /* @0x455e48 */
extern int g_nConsoleLogMaxLevel;      /* @0x4580e4 log_max_level */
extern int g_nConsoleLogOn;            /* @0x4580d8 log_to_file */
extern MString g_mstrLogFileName;      /* @0x4580dc log_file_name */
extern float g_configMapX;             /* @0x45833c */
extern float g_configMapY;             /* @0x458340 */
extern float g_configMapZoom;          /* @0x458338 */
extern int g_bAiEnabled;               /* @0x458358 (ai_mode value discarded, flag set to 1) */

#endif /* CONFIG_H */
