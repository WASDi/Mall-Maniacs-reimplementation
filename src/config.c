#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "config.h"
#include "pool.h"
#include "stubs.h"
#include "gameplay.h"
#include "menu.h"
#include "time.h"
#include "util.h"
#include "custom_helpers.h"

/* =====================================================================
 * config.c — DFF config database (maniac.exe 0x408xxx / 0x4103xx /
 * 0x4355xx-0x436xxx). Parses the XOR-obfuscated maniac.cfg (decoded to
 * the temporary sommar.sol by gameInit) into a ConfigNode tree and
 * serves lookups for the master settings, levels, objects and items.
 * Tokenizing/stream helpers map to the CRT (ifstream/fgetc); the node
 * type vtable is reduced to the nType id (see config.h).
 * ===================================================================== */

ConfigEnv g_configEnvMaster;           /* @0x455e48 */
int g_nConsoleLogMaxLevel;             /* @0x4580e4 */
int g_nConsoleLogOn;                   /* @0x4580d8 */
MString g_mstrLogFileName;             /* @0x4580dc */
float g_configMapX;                    /* @0x45833c */
float g_configMapY;                    /* @0x458340 */
float g_configMapZoom;                 /* @0x458338 */
int g_bAiEnabled;                      /* @0x458358 */

/* Demo/movie database globals (0x455e68..0x455e87). g_movieName is the
 * MovieDb name field and g_pMovieFrameNode the recorder's current frame
 * block cursor; both are zero-initialized until movieCmd sets them. */
MovieDb g_movieDb;                     /* @0x455e68 */
int     g_nMovieFrame;                 /* @0x455e88 */

/* shared empty-string source for the configEnvSetName-style resets in
 * configEnvFind (original passes the "" literal @0x4550d8). */
static MString mstrEmpty = { "", 1 };

static ConfigToken *configBuildTokenTree(ConfigEnv *pEnv, ConfigToken *pParent, ConfigToken *pToken); /* @0x435b80 */
static int configParseTokensToTree(ConfigEnv *pEnv, ConfigNode *pParent, ConfigToken *pToken);        /* @0x435c30 */

/* configTokenNodeCtor @0x435650 — 0x14-byte token: ctor-empty text,
 * assign from pPsz, zero links. */
static ConfigToken *configTokenNodeCtor(ConfigToken *pThis, const char *pPsz) /* @0x435650 */
{
    MString tmp;
    mStringCtorEmpty(&pThis->text);
    mStringCtorFromCStr(&tmp, pPsz);
    mStringAssignCopy(&pThis->text, &tmp);
    mStringFree(&tmp);
    pThis->pNext = NULL;
    pThis->nDepth = 0;
    pThis->pChild = NULL;
    return pThis;
}

/* configTokenNodeDtor @0x4368f0 — free the token text buffer. */
static void configTokenNodeDtor(ConfigToken *pThis) /* @0x4368f0 */
{
    memFreeDirect(pThis->text.pPsz);
}

/* configParseAddToken @0x435820 — append a token to the env's list. */
static int configParseAddToken(ConfigEnv *pEnv, const char *pPsz) /* @0x435820 */
{
    ConfigToken *pToken = (ConfigToken *)malloc(sizeof(ConfigToken)); /* operator_new @0x43dd42 */
    if (pToken != NULL) {
        configTokenNodeCtor(pToken, pPsz);
    } else {
        pToken = NULL;
    }
    if (pEnv->pTokenHead == NULL) {
        pEnv->pTokenHead = pToken;
    }
    if (pEnv->pTokenTail != NULL) {
        pEnv->pTokenTail->pNext = pToken;
    }
    pEnv->pTokenTail = pToken;
    return 0;
}

/* configFreeTokenList @0x4368b0 — depth-free the token tree. */
static void configFreeTokenList(ConfigEnv *pEnv, ConfigToken *pToken) /* @0x4368b0 */
{
    while (pToken != NULL) {
        ConfigToken *pNext;
        if (pToken->pChild != NULL) {
            configFreeTokenList(pEnv, pToken->pChild);
        }
        pNext = pToken->pNext;
        configTokenNodeDtor(pToken);
        memFreeDirect((void *)pToken);
        pToken = pNext;
    }
}

/* configBuildTokenTree @0x435b80 — nesting pass over the token list.
 * '{' tokens take their followers as pChild (terminated by the matching
 * '}'); pParent is the enclosing '{' token (stored in nDepth). Returns
 * the terminating '}' token, pEnv->pTokenHead at top level, or NULL on
 * an unterminated nested block. */
static ConfigToken *configBuildTokenTree(ConfigEnv *pEnv, ConfigToken *pParent, ConfigToken *pToken) /* @0x435b80 */
{
    ConfigToken *pLast = NULL;
    while (pToken != NULL) {
        if (mStringEquals(&pToken->text, "{")) {          /* 0x45115c */
            ConfigToken *pEnd;
            if (pToken->pNext == NULL) {
                return NULL;
            }
            pToken->nDepth = (int)(intptr_t)pParent;
            pToken->pChild = pToken->pNext;
            pEnd = configBuildTokenTree(pEnv, pToken, pToken->pNext);
            if (pEnd == NULL) {
                return NULL;
            }
            if (pToken->pNext == pEnd) {
                pToken->pChild = NULL;                    /* empty {} */
            } else {
                pToken->pNext = pEnd;                     /* relink to '}' */
            }
            pLast = pEnd;
            pToken = pEnd->pNext;
        } else if (mStringEquals(&pToken->text, "}")) {   /* 0x451158 */
            if (pLast != NULL) {
                pLast->pNext = NULL;
            }
            return pToken;
        } else {
            pToken->nDepth = (int)(intptr_t)pParent;
            pLast = pToken;
            pToken = pToken->pNext;
        }
    }
    if (pParent != NULL) {
        return NULL;
    }
    return pEnv->pTokenHead;
}

/* configNodeCtorBase @0x4356d0 — zero key/links, base type. */
static void configNodeCtorBase(ConfigNode *pNode) /* @0x4356d0 */
{
    mStringCtorEmpty(&pNode->key);
    pNode->nType = CONFIG_NODE_BLOCK;
    pNode->pParent = NULL;
    pNode->pChild = NULL;
    pNode->pPrev = NULL;
    pNode->pNext = NULL;
}

/* configBlockNodeCtor @0x435780 — 0x2c block node. */
static ConfigBlockNode *configBlockNodeCtor(ConfigBlockNode *pNode) /* @0x435780 */
{
    configNodeCtorBase(&pNode->base);
    pNode->base.nType = CONFIG_NODE_BLOCK;
    pNode->nCount = 0;
    pNode->nIndex = 0;
    pNode->pFirst = NULL;
    pNode->pLast = NULL;
    return pNode;
}

/* configValueNodeCtor @0x4357a0 — 0x28 value node. */
static ConfigValueNode *configValueNodeCtor(ConfigValueNode *pNode) /* @0x4357a0 */
{
    configNodeCtorBase(&pNode->base);
    pNode->base.nType = CONFIG_NODE_VALUE;
    pNode->dValue = 0.0;
    return pNode;
}

/* newStringNode — inline operator_new(0x24) + ctorBase + empty value
 * (original expansion at 0x435cdd/0x435df4). */
static ConfigStringNode *configStringNodeNew(void)
{
    ConfigStringNode *pNode = (ConfigStringNode *)malloc(sizeof(ConfigStringNode));
    if (pNode != NULL) {
        configNodeCtorBase(&pNode->base);
        pNode->base.nType = CONFIG_NODE_STRING;
        mStringCtorEmpty(&pNode->value);
    }
    return pNode;
}

/* linkNode — shared linking of a freshly created node (original
 * expansion at the tail of every creation branch). */
static void configLinkNode(ConfigEnv *pEnv, ConfigNode *pParent, ConfigNode *pNode, ConfigNode **ppLast)
{
    pNode->pParent = pParent;
    pNode->pPrev = *ppLast;
    if (pParent != NULL && pParent->pChild == NULL) {
        pParent->pChild = pNode;
    }
    if (*ppLast != NULL) {
        (*ppLast)->pNext = pNode;
    }
    if (pEnv->pRoot == NULL) {
        pEnv->pRoot = pNode;
    }
    *ppLast = pNode;
}

/* configEnvAddValue @0x436eb0 — allocate a 0x28 value node, set its key and
 * double value, and append it to the end of pParent's child list. Returns
 * the node, or NULL when pParent is NULL or not a block node (the original
 * dispatches the type query through the one-slot vtable). Used by
 * movieFrameUpdate to record one "fb%d"/"lr%d"/"ac%d" value per player. */
ConfigValueNode *configEnvAddValue(ConfigNode *pParent, const char *pKey,
                                   double dValue) /* @0x436eb0 */
{
    ConfigNode *pLast;
    ConfigValueNode *pNode;

    if (pParent == NULL) {                               /* @0x436ecd */
        return NULL;
    }
    if (pParent->nType != CONFIG_NODE_BLOCK) {           /* vtable type query @0x436eea */
        return NULL;
    }
    pLast = pParent->pChild;                             /* @0x436f06 */
    if (pLast != NULL) {
        while (pLast->pNext != NULL) {                   /* @0x436f0d */
            pLast = pLast->pNext;
        }
    }
    pNode = (ConfigValueNode *)malloc(sizeof(ConfigValueNode));
    if (pNode == NULL) {
        pNode = NULL;
    } else {
        configValueNodeCtor(pNode);                      /* @0x436f39 */
    }
    configEnvSetName(&pNode->base.key, pKey);            /* @0x436f54 */
    pNode->dValue = dValue;                              /* @0x436f61 */
    if (pLast == NULL) {                                 /* @0x436f67 */
        pParent->pChild = &pNode->base;                  /* @0x436f6b */
        pNode->base.pParent = pParent;                   /* @0x436f6e */
    } else {
        pNode->base.pParent = pParent;                   /* @0x436f73 */
        pLast->pNext = &pNode->base;                     /* @0x436f76 */
        pNode->base.pPrev = pLast;                       /* @0x436f79 */
    }
    return pNode;
}

/* configBlockNodeNew @0x436d50 — allocate a 0x2c block node, key it (pPsz or
 * "$DFF_BLOCK") and insert it into the env node list: after pTail when given,
 * at the list head otherwise. When the neighbors share the key the original
 * maintains the block-array bookkeeping (pFirst/pLast/nIndex/nCount), so
 * consecutive same-named blocks resolve through configEnvFind's "name[idx]"
 * path. Used by movieFrameUpdate to append one block per recorded frame. */
ConfigBlockNode *configBlockNodeNew(ConfigEnv *pEnv, ConfigNode *pTail,
                                    const char *pPsz) /* @0x436d50 */
{
    ConfigBlockNode *pNode;
    ConfigBlockNode *pChain;
    ConfigNode *pIter;
    int nIndex;

    pNode = (ConfigBlockNode *)malloc(sizeof(ConfigBlockNode));
    if (pNode == NULL) {
        pNode = NULL;
    } else {
        configBlockNodeCtor(pNode);                      /* @0x436d87 */
    }
    configEnvSetName(&pNode->base.key,
                     (pPsz != NULL) ? pPsz : "$DFF_BLOCK"); /* @0x436daf */
    pNode->nIndex = 0;                                   /* @0x436db4 */
    pNode->nCount = 1;                                   /* @0x436dbb */
    pNode->pFirst = &pNode->base;                        /* @0x436dc2 */
    if (pEnv->pRoot != NULL) {                           /* @0x436dc5 */
        if (pTail != NULL) {
            pNode->base.pPrev = pTail;                   /* @0x436ddc */
            pNode->base.pNext = pTail->pNext;            /* @0x436ddf */
            pTail->pNext = &pNode->base;                 /* @0x436de5 */
            if (pNode->base.pNext != NULL) {             /* @0x436de8 */
                pNode->base.pNext->pPrev = &pNode->base; /* @0x436def */
            }
            pChain = NULL;
            pNode->base.pParent = pTail->pParent;        /* @0x436dfa */
            if (pNode->base.pNext != NULL &&
                pNode->base.pNext->nType == CONFIG_NODE_BLOCK &&
                mStringEqualsMString(&pNode->base.key,
                                     &pNode->base.pNext->key) != 0) { /* @0x436e11 */
                pNode->pLast = (ConfigNode *)pNode->base.pNext;   /* @0x436e22 */
                pNode->pFirst = ((ConfigBlockNode *)pNode->base.pNext)->pFirst; /* @0x436e25 */
                pChain = pNode;
            }
            if (pNode->base.pPrev != NULL &&
                pNode->base.pPrev->nType == CONFIG_NODE_BLOCK &&
                mStringEqualsMString(&pNode->base.key,
                                     &pNode->base.pPrev->key) != 0) { /* @0x436e42 */
                pChain = (ConfigBlockNode *)((ConfigBlockNode *)pNode->base.pPrev)->pFirst; /* @0x436e51 */
                ((ConfigBlockNode *)pNode->base.pPrev)->pLast = &pNode->base; /* @0x436e54 */
                pNode->pFirst = (ConfigNode *)pChain;    /* @0x436e57 */
            }
            if (pChain == NULL) {                        /* @0x436e5a */
                return pNode;
            }
            nIndex = 0;                                  /* @0x436e5e */
            pIter = &pChain->base;
            do {
                ((ConfigBlockNode *)pIter)->pFirst = &pChain->base; /* @0x436e62 */
                ((ConfigBlockNode *)pIter)->nIndex = nIndex;        /* @0x436e65 */
                pIter = (ConfigNode *)((ConfigBlockNode *)pIter)->pLast; /* @0x436e68 */
                nIndex++;
            } while (pIter != NULL);
            pChain->nCount = nIndex;                     /* @0x436e70 */
            return pNode;
        }
        pNode->base.pPrev = NULL;                        /* @0x436e75 */
        pNode->base.pNext = pEnv->pRoot;                 /* @0x436e7c */
        pEnv->pRoot->pPrev = &pNode->base;               /* @0x436e85 */
    }
    pEnv->pRoot = &pNode->base;                          /* @0x436e88 */
    return pNode;
}

/* configNodeDtor @0x435700 — recursive child/sibling teardown. */
void configNodeDtor(ConfigNode *pNode) /* @0x435700 */
{
    pNode->nType = CONFIG_NODE_BLOCK;
    if (pNode->pChild != NULL) {
        configNodeDtor(pNode->pChild);
        memFreeDirect(pNode->pChild);
    }
    if (pNode->pNext != NULL) {
        configNodeDtor(pNode->pNext);
        memFreeDirect(pNode->pNext);
    }
    mStringFree(&pNode->key);
}

/* configNodeFree @0x436a60 — unlink pNode (repairing block-array
 * first/last/index bookkeeping) then dtor + free. */
static void configNodeFree(ConfigEnv *pEnv, ConfigNode *pNode) /* @0x436a60 */
{
    if (pNode == NULL) {
        return;
    }
    if (pNode == pEnv->pRoot) {
        pEnv->pRoot = pNode->pNext;
    }
    if (pNode->nType == CONFIG_NODE_BLOCK) {
        ConfigBlockNode *pBlock = (ConfigBlockNode *)pNode;
        if (pBlock->pFirst == NULL && pBlock->nCount > 1) {
            /* first block of an array removed: promote pLast (original
             * walks the pLast chain and re-indexes from 0). */
            ConfigBlockNode *pCur;
            int i = 0;
            pBlock->nIndex = 0;
            pBlock->nCount = 1;
            pBlock->pFirst = pNode;
            for (pCur = pBlock; pCur != NULL; pCur = (ConfigBlockNode *)pCur->pLast) {
                pCur->nIndex = i;
                pCur->pFirst = pNode;
                i++;
            }
        } else if (pBlock->pFirst != NULL && ((ConfigBlockNode *)pBlock->pFirst)->nCount > 1) {
            ConfigBlockNode *pCur;
            ((ConfigBlockNode *)pBlock->pFirst)->nCount--;
            for (pCur = (ConfigBlockNode *)pNode->pNext; pCur != NULL; pCur = (ConfigBlockNode *)pCur->base.pNext) {
                if (pCur->base.nType == CONFIG_NODE_BLOCK) pCur->nIndex--;
            }
        }
    }
    if (pNode->pParent != NULL && pNode->pParent->pChild == pNode) {
        pNode->pParent->pChild = pNode->pNext;
    }
    if (pNode->pPrev != NULL) {
        pNode->pPrev->pNext = pNode->pNext;
    }
    if (pNode->pNext != NULL) {
        pNode->pNext->pPrev = pNode->pPrev;
    }
    pNode->pPrev = NULL;
    pNode->pNext = NULL;
    configNodeDtor(pNode);
    memFreeDirect(pNode);
}

/* configEnvFreeChildren @0x4368a0 — free the parsed tree. */
void configEnvFreeChildren(ConfigEnv *pEnv) /* @0x4368a0 */
{
    if (pEnv->pRoot != NULL) {
        configNodeFree(pEnv, pEnv->pRoot);
    }
}

/* configEnvCtor @0x4357c0 — empty env named "default.dff". */
void configEnvCtor(ConfigEnv *pEnv) /* @0x4357c0 */
{
    mStringCtorEmpty(&pEnv->name);
    configEnvSetName(&pEnv->name, "default.dff");   /* s_default_dff @0x45114c */
    pEnv->pTokenHead = NULL;
    pEnv->pTokenTail = NULL;
    pEnv->pRoot = NULL;
}

/* configEnvSetName @0x4354a0 — replace an MString's buffer with pPsz. */
void configEnvSetName(MString *pStr, const char *pPsz) /* @0x4354a0 */
{
    size_t n;
    memFreeDirect(pStr->pPsz);
    n = strlen(pPsz) + 1;
    pStr->nLen = (int)n;
    pStr->pPsz = (char *)malloc(n);
    memcpy(pStr->pPsz, pPsz, n);
}

/* configParseFile @0x435890 — tokenize pPsz, build the token tree and
 * convert it to the node tree. Returns 0 / -1. */
int configParseFile(ConfigEnv *pEnv, const char *pPsz) /* @0x435890 */
{
    FILE *fp;
    char buf[1024];
    int nResult;

    configEnvSetName(&pEnv->name, pPsz);
    fp = fopen(pPsz, "rb");                 /* streamOpenInputFile (CRT) */
    if (fp == NULL) {
        return -1;
    }
    configEnvFreeChildren(pEnv);
    for (;;) {
        int c = fgetc(fp);                  /* streamPeekByte; consumed chars match the original's reads */
        int bQuoted;
        int bDropped = 0;
        int n = 0;
        if (c == EOF) {
            break;
        }
        if (c == '/') {                     /* comment to end of line */
            if (fgetc(fp) == '\n') {
                c = fgetc(fp);
            } else {
                while ((c = fgetc(fp)) != EOF && c != '\n') {
                }
            }
            continue;
        }
        if ((char)c <= ' ' || c == ',') {   /* skip separators */
            continue;
        }
        bQuoted = (c == '"');
        buf[n++] = (char)c;                 /* opening quote / first char is part of the token */
        if (!bQuoted && (c == '=' || c == '[' || c == ']' || c == '{' || c == '}')) {
            goto emit;                      /* top-level single-char tokens */
        }
        for (;;) {
            int d = fgetc(fp);
            if (d == EOF) {
                bDropped = 1;               /* original drops a token cut by EOF */
                break;
            }
            if (bQuoted) {
                buf[n++] = (char)d;
                if (d == '"') {
                    break;                  /* closing quote consumed */
                }
            } else if (d == ',') {
                break;                      /* ',' consumed, emit token */
            } else if (d == '\t' || d == '\n' || d == '\r' || d == ' ' ||
                       d == '=' || d == '[' || d == ']' || d == '{' || d == '}') {
                ungetc(d, fp);              /* inner separator: emit-keep (jump tables 0x435a80/0x435ae8) */
                break;
            } else {
                buf[n++] = (char)d;
            }
        }
emit:
        if (!bDropped) {
            buf[n] = '\0';
            configParseAddToken(pEnv, buf);
        }
    }
    fclose(fp);
    configBuildTokenTree(pEnv, NULL, pEnv->pTokenHead);
    nResult = configParseTokensToTree(pEnv, NULL, pEnv->pTokenHead);
    configFreeTokenList(pEnv, pEnv->pTokenHead);
    pEnv->pTokenHead = NULL;
    pEnv->pTokenTail = NULL;
    return nResult;
}

/* configParseTokensToTree @0x435c30 — convert the token tree into
 * ConfigNodes. pParent receives the children; string literals become
 * string nodes, "key = value" value/string nodes, "name { }" a
 * single-block array and "name[] { } { }" an indexed block array. */
static int configParseTokensToTree(ConfigEnv *pEnv, ConfigNode *pParent, ConfigToken *pToken) /* @0x435c30 */
{
    MString mstrName;
    ConfigNode *pLast = NULL;        /* last node created at this level */
    ConfigNode *pArrayFirst = NULL;  /* first block of the pending array */
    ConfigNode *pArrayLast = NULL;   /* last block of the pending array */
    int nBlockIdx = 0;               /* running block index (EBP) */
    int nExpect = -1;                /* declared block count, -1 = unlimited */
    int bInArray = 0;

    mStringCtorEmpty(&mstrName);
    while (pToken != NULL) {
        int bMatched = 0;
        if (mStringEquals(&pToken->text, "}")) {                  /* 0x451158 */
            goto error;                                           /* stray close brace */
        }
        if (mStringCharAt(&pToken->text, 0) == '"') {             /* bare string literal */
            ConfigStringNode *pNode;
            if (bInArray) {
                if (nExpect != -1 && nBlockIdx != nExpect) goto error;
                if (pArrayFirst != NULL) ((ConfigBlockNode *)pArrayFirst)->nCount = nBlockIdx;
                bInArray = 0;
                nExpect = -1;
                nBlockIdx = 0;
            }
            pNode = configStringNodeNew();
            if (pNode == NULL) { pLast = NULL; goto error; }
            configEnvSetName(&pNode->base.key, "$DFF_STRING");    /* 0x451178 */
            mStringSubstr(&pToken->text, &pNode->value, 1, mStringLength(&pToken->text) - 2);
            configLinkNode(pEnv, pParent, &pNode->base, &pLast);
            pToken = pToken->pNext;
            continue;
        }
        if (pToken->pNext != NULL && mStringEquals(&pToken->pNext->text, "=")) {  /* 0x451174 */
            /* assignment: key = "string" | number */
            ConfigToken *pValue = pToken->pNext->pNext;
            if (bInArray) {
                if (nExpect != -1 && nBlockIdx != nExpect) goto error;
                if (pArrayFirst != NULL) ((ConfigBlockNode *)pArrayFirst)->nCount = nBlockIdx;
                bInArray = 0;
                nExpect = -1;
                nBlockIdx = 0;
            }
            if (pValue != NULL && mStringCharAt(&pValue->text, 0) == '"') {
                ConfigStringNode *pNode = configStringNodeNew();
                if (pNode == NULL) { pLast = NULL; goto error; }
                mStringAssignCopy(&pNode->base.key, &pToken->text);
                mStringSubstr(&pValue->text, &pNode->value, 1, mStringLength(&pValue->text) - 2);
                configLinkNode(pEnv, pParent, &pNode->base, &pLast);
            } else {
                ConfigValueNode *pNode = (ConfigValueNode *)malloc(sizeof(ConfigValueNode));
                if (pNode == NULL) { pLast = NULL; goto error; }
                configValueNodeCtor(pNode);
                mStringAssignCopy(&pNode->base.key, &pToken->text);
                pNode->dValue = mStringToFloat(&pValue->text);
                configLinkNode(pEnv, pParent, &pNode->base, &pLast);
            }
            pToken = (pValue != NULL) ? pValue->pNext : NULL;
            continue;
        }
        if (mStringEquals(&pToken->text, "{")) {                  /* 0x45115c block of pending array */
            ConfigBlockNode *pNode;
            if (!bInArray) {
                goto error;
            }
            pNode = (ConfigBlockNode *)malloc(sizeof(ConfigBlockNode));
            if (pNode == NULL) { pLast = NULL; goto error; }
            configBlockNodeCtor(pNode);
            mStringAssignCopy(&pNode->base.key, &mstrName);
            configLinkNode(pEnv, pParent, &pNode->base, &pLast);
            if (pArrayFirst == NULL) {
                pArrayFirst = &pNode->base;
                pNode->pFirst = &pNode->base;
            } else {
                pNode->pFirst = pArrayFirst;
            }
            if (pArrayLast != NULL) {
                ((ConfigBlockNode *)pArrayLast)->pLast = &pNode->base;
            }
            pArrayLast = &pNode->base;
            if (pEnv->pRoot == NULL) {
                pEnv->pRoot = &pNode->base;
            }
            if (pToken->pChild != NULL) {
                if (configParseTokensToTree(pEnv, &pNode->base, pToken->pChild) == -1) {
                    goto error;
                }
                pToken = pToken->pNext;
                if (mStringNotEquals(&pToken->text, "}")) {
                    goto error;
                }
            } else {
                pToken = pToken->pNext;
            }
            pNode->nIndex = nBlockIdx;
            nBlockIdx++;
            pToken = pToken->pNext;
            bMatched = 1;
        } else if (pToken->pNext != NULL && mStringEquals(&pToken->pNext->text, "{")) {  /* 0x45115c "name {" */
            if (bInArray) {
                if (nExpect != -1 && nBlockIdx != nExpect) goto error;
                if (pArrayFirst != NULL) ((ConfigBlockNode *)pArrayFirst)->nCount = nBlockIdx;
            }
            mStringAssignCopy(&mstrName, &pToken->text);
            nBlockIdx = 0;
            bInArray = 1;
            nExpect = 1;
            pArrayFirst = NULL;
            pArrayLast = NULL;
            pToken = pToken->pNext;
            bMatched = 1;
        } else if (pToken->pNext != NULL && mStringEquals(&pToken->pNext->text, "[")) {   /* 0x451170 "name [" */
            ConfigToken *pNext;
            if (bInArray) {
                if (nExpect != -1 && nBlockIdx != nExpect) goto error;
                if (pArrayFirst != NULL) ((ConfigBlockNode *)pArrayFirst)->nCount = nBlockIdx;
            }
            mStringAssignCopy(&mstrName, &pToken->text);
            pToken = pToken->pNext;                               /* '[' */
            nBlockIdx = 0;
            if (pToken == NULL) goto error;
            pNext = pToken->pNext;
            if (pNext == NULL) goto error;
            if (mStringEquals(&pNext->text, "]")) {               /* 0x45116c name[] */
                nExpect = -1;
                pToken = pNext->pNext;
            } else {                                              /* name[N] */
                nExpect = mStringToInt(&pNext->text);
                pNext = pNext->pNext;
                pToken = (pNext != NULL) ? pNext->pNext : NULL;
            }
            pArrayFirst = NULL;
            pArrayLast = NULL;
            bInArray = 1;
            bMatched = 1;
        }
        if (!bMatched) {
            /* bare value (number) */
            ConfigValueNode *pNode;
            if (bInArray) {
                if (nExpect != -1 && nBlockIdx != nExpect) goto error;
                if (pArrayFirst != NULL) ((ConfigBlockNode *)pArrayFirst)->nCount = nBlockIdx;
                bInArray = 0;
                nExpect = -1;
                nBlockIdx = 0;
            }
            pNode = (ConfigValueNode *)malloc(sizeof(ConfigValueNode));
            if (pNode == NULL) { pLast = NULL; goto error; }
            configValueNodeCtor(pNode);
            configEnvSetName(&pNode->base.key, "$DFF_VALUE");     /* 0x451160 */
            pNode->dValue = mStringToFloat(&pToken->text);
            configLinkNode(pEnv, pParent, &pNode->base, &pLast);
            pToken = pToken->pNext;
        }
    }
    if (bInArray) {
        if (nExpect != -1 && nBlockIdx != nExpect) goto error;
        if (pArrayFirst != NULL) ((ConfigBlockNode *)pArrayFirst)->nCount = nBlockIdx;
    }
    mStringFree(&mstrName);
    return 0;
error:
    mStringFree(&mstrName);
    return -1;
}


/* configFindNode @0x4367e0 — find a sibling named pKey: start at pNode's
 * parent's first child (or the env root when pNode has no parent). */
ConfigNode *configFindNode(ConfigEnv *pEnv, ConfigNode *pNode, const char *pKey) /* @0x4367e0 */
{
    ConfigNode *pCur;
    if (pNode == NULL || pNode->pParent == NULL) {
        pCur = pEnv->pRoot;
    } else {
        pCur = pNode->pParent->pChild;
    }
    while (pCur != NULL) {
        if (mStringEquals(&pCur->key, pKey)) {
            return pCur;
        }
        pCur = pCur->pNext;
    }
    return NULL;
}

/* configNodeGetId @0x436850 — first child of pNode (the block's id token
 * in the .sol grammar). NULL when pNode is NULL. Used by nloadCmd to
 * descend from a top-level "0001" block into its key/value children. */
ConfigNode *configNodeGetId(ConfigNode *pNode) /* @0x436850 */
{
    if (pNode != NULL) {
        return pNode->pChild;
    }
    return NULL;
}

/* configNodeGetKey @0x436900 — copy a node's key into pOut (empty string
 * when pNode is NULL). Returns pOut. */
MString *configNodeGetKey(MString *pOut, ConfigNode *pNode) /* @0x436900 */
{
    if (pNode == NULL) {
        mStringCtorWithCapacity(pOut, 0);
        return pOut;
    }
    mStringAssign(pOut, &pNode->key);
    return pOut;
}

/* configEnvFind @0x4365c0 — resolve a path ("name", "name[2]",
 * "name[2]/sub/key") from pNode (default env root) to a node. Each
 * component matches name + block index; matching descends into pChild.
 * Returns the resolved node or NULL. */
static ConfigNode *configEnvFind(ConfigEnv *pEnv, ConfigNode *pNode, const char *pKey) /* @0x4365c0 */
{
    MString mstrComp;
    MString mstrIdx;
    MString mstrName;
    int i = 0;
    int bDone = 0;
    ConfigNode *pCur = (pNode != NULL) ? pNode : pEnv->pRoot;

    mStringCtorEmpty(&mstrComp);
    mStringCtorEmpty(&mstrIdx);
    mStringCtorEmpty(&mstrName);
    for (;;) {
        mStringAssignCopy(&mstrComp, &mstrEmpty);           /* reset to "" */
        mStringAssignCopy(&mstrIdx, &mstrEmpty);            /* reset per component @0x436631 */
        for (; pKey[i] != '\0'; i++) {
            if (pKey[i] == '/') {
                break;
            }
            mStringAppendChar(&mstrComp, pKey[i]);
        }
        if (pKey[i] == '\0') {
            bDone = 1;
        }
        if (mStringLength(&mstrComp) > 0) {
            int nIndex = 0;
            if (strstr(mStringCStr(&mstrComp), "[") != NULL) {   /* strFindSubstring @0x43e7e0 */
                int j = 0;
                char ch;
                mStringAssignCopy(&mstrName, &mstrEmpty);
                while ((ch = mStringCharAt(&mstrComp, j)) != '[' && ch != '\0') {
                    mStringAppendChar(&mstrName, ch);
                    j++;
                }
                j++;                                    /* skip '[' */
                mStringAssignCopy(&mstrIdx, &mstrEmpty);
                while ((ch = mStringCharAt(&mstrComp, j)) != ']' && ch != '\0') {
                    mStringAppendChar(&mstrIdx, ch);
                    j++;
                }
                mStringAssignCopy(&mstrComp, &mstrName);
            }
            nIndex = mStringToInt(&mstrIdx);
            while (pCur != NULL) {
                int nNodeIdx = (pCur->nType == CONFIG_NODE_BLOCK)
                             ? ((ConfigBlockNode *)pCur)->nIndex : 0;
                if (mStringEqualsMString(&pCur->key, &mstrComp) && nIndex == nNodeIdx) {
                    break;
                }
                pCur = pCur->pNext;
                if (pCur == NULL) {
                    mStringFree(&mstrIdx);
                    mStringFree(&mstrName);
                    mStringFree(&mstrComp);
                    return NULL;
                }
            }
            if (pCur->pChild == NULL || bDone) {
                /* matched terminal: no deeper component to consume */
                mStringFree(&mstrIdx);
                mStringFree(&mstrName);
                mStringFree(&mstrComp);
                return pCur;
            }
            pCur = pCur->pChild;                        /* descend for next component */
        }
        if (bDone) {
            mStringFree(&mstrIdx);
            mStringFree(&mstrName);
            mStringFree(&mstrComp);
            return pCur;
        }
        i++;                                            /* skip '/' */
    }
}

/* configEnvGetValue @0x436560 — configEnvFind + first child for blocks. */
ConfigNode *configEnvGetValue(ConfigEnv *pEnv, ConfigNode *pNode, const char *pKey) /* @0x436560 */
{
    ConfigNode *pFound = configEnvFind(pEnv, pNode, pKey);
    if (pFound != NULL && pFound->nType == CONFIG_NODE_BLOCK) {
        return pFound->pChild;
    }
    return NULL;
}

/* configEnvGetValueByIndex @0x436550 — this-call wrapper: configEnvGetValue
 * with a NULL parent node. Used by playerSetupRound for the [master] block. */
ConfigNode *configEnvGetValueByIndex(ConfigEnv *pEnv, const char *pKey) /* @0x436550 */
{
    return configEnvGetValue(pEnv, NULL, pKey);
}

/* configEnvFindValue @0x4365a0 — path lookup (configEnvFind with a NULL
 * start node) returning the resolved node or NULL. Used by movieFrameUpdate
 * to read "%s[%d]" frame records from the movie database. */
ConfigNode *configEnvFindValue(ConfigEnv *pEnv, const char *pKey) /* @0x4365a0 */
{
    return configEnvFind(pEnv, NULL, pKey);
}

/* configEnvGetString @0x436990 — string sibling lookup into pOut. */
void configEnvGetString(MString *pOut, ConfigNode *pNode, const char *pKey) /* @0x436990 */
{
    ConfigNode *pFound = configFindNode(&g_configEnvMaster, pNode, pKey);
    if (pFound != NULL && pFound->nType == CONFIG_NODE_STRING) {
        mStringAssign(pOut, &((ConfigStringNode *)pFound)->value);
        return;
    }
    mStringCtorEmpty(pOut);
}

/* configEnvGetDouble2 @0x436a20 — numeric sibling lookup. */
double configEnvGetDouble2(ConfigEnv *pEnv, ConfigNode *pNode, const char *pKey) /* @0x436a20 */
{
    ConfigNode *pFound = configFindNode(pEnv, pNode, pKey);
    if (pFound != NULL && pFound->nType == CONFIG_NODE_VALUE) {
        return ((ConfigValueNode *)pFound)->dValue;
    }
    return 0.0;
}

/* configEnvGetDouble @0x4369f0 — double value of a node itself (virtual
 * getDouble slot; value nodes return dValue, anything else 0). Used by
 * eloadCmd for the "values v" value nodes. */
double configEnvGetDouble(ConfigNode *pNode) /* @0x4369f0 */
{
    if (pNode != NULL && pNode->nType == CONFIG_NODE_VALUE) {
        return ((ConfigValueNode *)pNode)->dValue;
    }
    return 0.0;
}

/* configNodeGetValue @0x436940 — value of a string node ("" otherwise). */
MString *configNodeGetValue(MString *pOut, ConfigNode *pNode) /* @0x436940 */
{
    if (pNode != NULL && pNode->nType == CONFIG_NODE_STRING) {
        mStringAssign(pOut, &((ConfigStringNode *)pNode)->value);
        return pOut;
    }
    mStringCtorEmpty(pOut);
    return pOut;
}

/* configGetHead @0x436890 — env root accessor. */
static ConfigNode *configGetHead(ConfigEnv *pEnv) /* @0x436890 */
{
    return pEnv->pRoot;
}

/* configNextNode @0x436870 — sibling walk (NULL starts at the root). */
ConfigNode *configNextNode(ConfigEnv *pEnv, ConfigNode *pNode) /* @0x436870 */
{
    if (pNode != NULL) {
        return pNode->pNext;
    }
    return configGetHead(pEnv);
}

/* configGetValue @0x408c60 — console "get <key>" query used for the
 * driver path; returns the dispatch result (NULL when unresolved). */
unsigned char *configGetValue(const char *pKey) /* @0x408c60 */
{
    char buf[256];
    fmtSprintf(buf, "get %s", pKey);
    return commandDispatch(0, buf);
}

/* configMasterLoad @0x410350 — read the [master] block into globals. */
void configMasterLoad(void) /* @0x410350 */
{
    ConfigNode *pMaster = configEnvGetValue(&g_configEnvMaster, NULL, "master");
    MString tmp;
    if (pMaster == NULL) {
        fatalError("'master' block not found in cfg.");
    }
    g_nConsoleLogMaxLevel = (int)configEnvGetDouble2(&g_configEnvMaster, pMaster, "log_max_level");
    g_nConsoleLogOn = (int)configEnvGetDouble2(&g_configEnvMaster, pMaster, "log_to_file");
    configEnvGetString(&tmp, pMaster, "log_file_name");
    mStringAssignCopy(&g_mstrLogFileName, &tmp);
    mStringFree(&tmp);
    if (g_nConsoleLogOn != 0) {
        nopDebugStub();
    }
    g_nObjUpdateTime = (int)configEnvGetDouble2(&g_configEnvMaster, pMaster, "obj_update_time");
    g_configMapX = (float)configEnvGetDouble2(&g_configEnvMaster, pMaster, "mapX");
    g_configMapY = (float)configEnvGetDouble2(&g_configEnvMaster, pMaster, "mapY");
    g_configMapZoom = (float)configEnvGetDouble2(&g_configEnvMaster, pMaster, "mapZoom");
    configEnvGetDouble2(&g_configEnvMaster, pMaster, "ai_mode");
    g_bAiEnabled = 1;
}

/* configStringDtor @0x407140 — free the parsed tree at pEnv+0x10
 * (configNodeDtor then memFreeDirect) followed by the env name MString
 * at pEnv+0x00. Also used by the temporary-env unwind handlers. */
void configStringDtor(ConfigEnv *pEnv) /* @0x407140 */
{
    if (pEnv->pRoot != NULL) {
        configNodeDtor(pEnv->pRoot);
        memFreeDirect(pEnv->pRoot);
    }
    mStringFree(&pEnv->name);
}

/* configMasterEnvDtor @0x409bb0 — thiscall thunk -> configStringDtor
 * @0x407140 with this = &g_configEnvMaster. */
static void configMasterEnvDtor(void) /* @0x409bb0 */
{
    configStringDtor(&g_configEnvMaster);
}

/* configMasterEnvCtor @0x409b90 — thiscall thunk -> configEnvCtor
 * @0x4357c0 with this = &g_configEnvMaster. */
void configMasterEnvCtor(void) /* @0x409b90 */
{
    configEnvCtor(&g_configEnvMaster);
}

/* configMasterEnvAtexit @0x409ba0 — atexit(configMasterEnvDtor)
 * registration wrapper. */
void configMasterEnvAtexit(void) /* @0x409ba0 */
{
    atexit(configMasterEnvDtor);
}

/* configMasterEnvInit @0x409b80 — MSVC dynamic initializer for
 * g_configEnvMaster: construct the env then register its atexit teardown.
 * The original runs this from the CRT init table (out of scope), so the
 * rebuild keeps the function and its exact call pair but does not call it
 * from gameInit (which does not call it in the original either). */
void configMasterEnvInit(void) /* @0x409b80 */
{
    configMasterEnvCtor();
    configMasterEnvAtexit();
}

/* movieFrameUpdate @0x40af80 — per-frame demo (movie) record/playback,
 * called from gameWorldUpdate @0x40b3d0 between playerUpdateDispatch and
 * roundLogicUpdate.
 *
 * PLAYBACK (g_nMovieRecord == 0 && g_nMoviePlay != 0): advance g_nMovieFrame
 * and resolve the frame block "g_movieName[g_nMovieFrame]" from g_pMovieDb
 * (configEnvFindValue). A missing block disables playback (g_nMoviePlay = 0)
 * and bails. Each child of the block is a per-player key: the first char
 * selects the field — 'l' -> flInputTurn (+0x2e0), 'f' -> flInputAccel
 * (+0x2e4), 'a' -> nActionSubstate (+0x2e8, clamped to 0 outside 0..7) — and
 * the digits after it are the player index (fmtAtoi). The numeric value is
 * the node's double.
 *
 * RECORD (g_nMovieRecord != 0): append a new frame block to g_pMovieDb
 * (configBlockNodeNew after the previous frame node) and, for each player
 * whose flInputAccel/flInputTurn/nActionSubstate is nonzero, add the
 * "fb%d"/"lr%d"/"ac%d" value to it.
 *
 * NOTE: the original comment claimed an X/Z swap between the 'l'/'f' names;
 * the disassembly (0x40b035 lr<-+0x2e0, 0x40affb fb<-+0x2e4) shows record and
 * playback use the same fields, so no swap is reproduced here. */
void movieFrameUpdate(void) /* @0x40af80 */
{
    char szKey[256];
    int i;

    if (g_nMovieRecord == 0) {                           /* @0x40afa9 */
        ConfigNode *pNode;

        if (g_nMoviePlay == 0) {                         /* @0x40b0bf */
            return;
        }
        g_nMovieFrame++;                                 /* @0x455e88 @0x40b0d5 */
        fmtSprintf(szKey, "%s[%d]", mStringCStr(&g_movieName), g_nMovieFrame); /* @0x44f480 */
        pNode = configEnvFindValue(&g_pMovieDb, szKey);  /* @0x4365a0 */
        if (pNode == NULL) {                             /* @0x40b104 */
            g_nMoviePlay = 0;
            nopDebugStub();
            return;
        }
        for (pNode = configNodeGetId(pNode); pNode != NULL; /* @0x40b13b */
             pNode = configNextNode(&g_pMovieDb, pNode)) {
            MString mstrKey;
            char szName[256];
            char cField;
            int nPlayer;

            configNodeGetKey(&mstrKey, pNode);           /* @0x436900 */
            strcpy(szName, mStringCStr(&mstrKey));
            mStringFree(&mstrKey);                       /* @0x435430 */
            cField = szName[0];
            if (cField == 'l') {                         /* @0x40b1a0 */
                float flValue = (float)configEnvGetDouble(pNode); /* @0x4369f0 */
                nPlayer = fmtAtoi(&szName[2]);           /* @0x43e75c */
                g_playerRecords[nPlayer].flInputTurn = flValue;
            } else if (cField == 'f') {                  /* @0x40b1dc */
                float flValue = (float)configEnvGetDouble(pNode);
                nPlayer = fmtAtoi(&szName[2]);
                g_playerRecords[nPlayer].flInputAccel = flValue;
            } else if (cField == 'a') {                  /* @0x40b218 */
                int nSub = (int)configEnvGetDouble(pNode); /* __ftol @0x43dd10 */
                nPlayer = fmtAtoi(&szName[2]);
                g_playerRecords[nPlayer].nActionSubstate =
                    ((unsigned int)nSub <= 7) ? nSub : 0;
            }
        }
        return;
    }
    /* record */
    g_pMovieFrameNode = (ConfigNode *)configBlockNodeNew(&g_pMovieDb, /* @0x436d50 @0x40afc2 */
                            g_pMovieFrameNode, mStringCStr(&g_movieName));
    for (i = 0; i < g_nPlayerCount; i++) {               /* @0x40afd8 */
        PlayerRecord *pRec = &g_playerRecords[i];

        if (pRec->flInputAccel != 0.0f) {                /* @0x44b244 @0x40afeb */
            fmtSprintf(szKey, "fb%d", i);                /* @0x44f498 */
            configEnvAddValue(g_pMovieFrameNode, szKey,
                              (double)pRec->flInputAccel);
        }
        if (pRec->flInputTurn != 0.0f) {                 /* @0x40b026 */
            fmtSprintf(szKey, "lr%d", i);                /* @0x44f490 */
            configEnvAddValue(g_pMovieFrameNode, szKey,
                              (double)pRec->flInputTurn);
        }
        if (pRec->nActionSubstate != 0) {                /* @0x40b05f */
            fmtSprintf(szKey, "ac%d", i);                /* @0x44f488 */
            configEnvAddValue(g_pMovieFrameNode, szKey,
                              (double)pRec->nActionSubstate);
        }
    }
}
