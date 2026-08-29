#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "config.h"
#include "pool.h"
#include "stubs.h"
#include "gameplay.h"
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

/* configNodeDtor @0x435700 — recursive child/sibling teardown. */
static void configNodeDtor(ConfigNode *pNode) /* @0x435700 */
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
static ConfigNode *configFindNode(ConfigEnv *pEnv, ConfigNode *pNode, const char *pKey) /* @0x4367e0 */
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

/* configMasterEnvDtor @0x409bb0 — atexit teardown (configStringDtor
 * @0x407140): free the tree then the env name. */
static void configMasterEnvDtor(void) /* @0x409bb0 */
{
    if (g_configEnvMaster.pRoot != NULL) {
        configNodeDtor(g_configEnvMaster.pRoot);
        memFreeDirect(g_configEnvMaster.pRoot);
        g_configEnvMaster.pRoot = NULL;
    }
    mStringFree(&g_configEnvMaster.name);
}

/* configMasterEnvInit @0x409b80 — MSVC dynamic initializer for
 * g_configEnvMaster; the rebuild calls it explicitly from gameInit. */
void configMasterEnvInit(void) /* @0x409b80 */
{
    configEnvCtor(&g_configEnvMaster);
    atexit(configMasterEnvDtor);
}
