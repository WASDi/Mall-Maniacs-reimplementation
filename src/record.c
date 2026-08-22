#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "gx.h"
#include "font.h"
#include "menu.h"
#include "options.h"
#include "record.h"
#include "sound.h"
#include "custom_helpers.h"
#include "util.h"

/* =====================================================================
 * Rekord / high-score table subsystem — reimplementation of
 * stateHighScoreTable @0x41dfd0. Split from menu.c per file-size.
 *
 * Original draws the fshi/vahi table for current level: commandDispatch
 * "get %s%dname%d"/"get %s%dtime%d"/"get %s%dface%d"/"get %s%ddiff%d"
 * with prefixes fshi @0x4508b0 / vahi @0x450a58 (stored XOR 0x55 in
 * config.mm, decoded on load like the original's commandDispatch path).
 * Time formatted "%02d:%02d:%02d" @0x4508bc. Face polygon via
 * g_hMenuTexChar @0x45a6c0, difficulty bar via g_hMenuTexGfx @0x45a6bc,
 * level header via g_hMenuTexLevel @0x45a6b8 @0x4505fc. Headers:
 * "Fr\xe5gesport" @0x4508a4 and "Vagnrace" @0x450498 drawn at 0x3c/0x19a,
 * y 0x50 with msfnt/mfnt. Level quad @0x41ed64: V=g_nResultsLevel*0x3300.
 * Animated quads @0x41ee8c/0x41eec6: sin(g_flHighScoreAnimTime)*5.
 * Keys: 0 Right/1 Left cycle g_nResultsLevel (0..min(4,g_nLevelCount));
 * 2 Up/3 Down toggle g_nRecordsRow (0..1); 6 Enter -> menuUpdate;
 * 7 Esc -> menuUpdate. Frame anim: g_flHighScoreAnimTime += g_flFrameDelta*0.3 .
 * ===================================================================== */

int g_nLevelCount = 4;           /* @0x45a6f4 */
int g_nResultsLevel = 0;         /* @0x45d46c */
int g_nRecordsRow = 0;           /* @0x45d47c */
int g_nScoreTableRow = 0;        /* @0x45d464 */
int g_nScoreTableTick = 0;       /* @0x45d43c */
float g_flHighScoreAnimTime = 0; /* @0x45a714 */
void *g_hMenuTexLevel = NULL;    /* @0x45a6b8 */
void *g_hMenuTexChar = NULL;     /* @0x45a6c0 */

/* Real high-score storage loaded from config.mm (XOR 0x55) like the
 * original's commandDispatch path. Two prefixes: fshi/vahi, level 0..4,
 * slot 0..4. Stored as file-scope data, not a helper function. */
static char g_recName[2][5][5][32];
static int  g_recTime[2][5][5];
static int  g_recFace[2][5][5];
static int  g_recDiff[2][5][5];
static int  g_recLoaded = 0;

/* stateHighScoreTable @0x41dfd0 */
int stateHighScoreTable(int nType, int nKey, int nKeyType)
{
    /* Inline real-record load (original did this via commandDispatch
     * reading the already-loaded config tree; rebuild does the XOR 0x55
     * config.mm decode here, once, without inventing a helper function). */
    if (!g_recLoaded) {
        g_recLoaded = 1;
        for(int p=0;p<2;p++) for(int l=0;l<5;l++) for(int s=0;s<5;s++){
            g_recName[p][l][s][0]='\0';
            snprintf(g_recName[p][l][s], sizeof(g_recName[p][l][s]), "-");
            g_recTime[p][l][s]=0; g_recFace[p][l][s]=0; g_recDiff[p][l][s]=0;
        }
        g_nLevelCount = 4;
        const char *candidates[] = {"config.mm", "/home/wasd/MallManiacsUnmodified/config.mm", NULL};
        FILE *f=NULL; char *buf=NULL; long len=0;
        for(int i=0;candidates[i];i++){ f=fopen(candidates[i],"rb"); if(f) break; }
        if (f) {
            fseek(f,0,SEEK_END); len=ftell(f); fseek(f,0,SEEK_SET);
            buf=(char*)malloc(len+1);
            if(buf){
                fread(buf,1,len,f);
                for(long i=0;i<len;i++) buf[i]^=0x55;
                buf[len]='\0';
                char *line=buf;
                while(line && *line){
                    char *eol=strchr(line,'\n');
                    if(eol) *eol='\0';
                    char *eq=strchr(line,'=');
                    if(eq){
                        *eq='\0';
                        char key[64]={0}, val[64]={0};
                        strncpy(key,line,63); strncpy(val,eq+1,63);
                        /* inline trim */
                        { char *p=key; while(*p==' '||*p=='\t'||*p=='\r'||*p=='\n') p++; if(p!=key) memmove(key,p,strlen(p)+1); size_t n=strlen(key); while(n>0&&(key[n-1]==' '||key[n-1]=='\t'||key[n-1]=='\r'||key[n-1]=='\n')) key[--n]='\0'; }
                        { char *p=val; while(*p==' '||*p=='\t'||*p=='\r'||*p=='\n') p++; if(p!=val) memmove(val,p,strlen(p)+1); size_t n=strlen(val); while(n>0&&(val[n-1]==' '||val[n-1]=='\t'||val[n-1]=='\r'||val[n-1]=='\n')) val[--n]='\0'; }
                        if(val[0]=='"'){ memmove(val,val+1,strlen(val)); size_t vl=strlen(val); if(vl>0&&val[vl-1]=='"') val[vl-1]='\0'; { char *p=val; while(*p==' '||*p=='\t'||*p=='\r'||*p=='\n') p++; if(p!=val) memmove(val,p,strlen(p)+1); size_t n=strlen(val); while(n>0&&(val[n-1]==' '||val[n-1]=='\t'||val[n-1]=='\r'||val[n-1]=='\n')) val[--n]='\0'; } }
                        if(strcmp(key,"toplevel")==0){
                            int tl=atoi(val); if(tl>=0&&tl<=10) g_nLevelCount=tl;
                        } else if(strncmp(key,"fshi",4)==0 || strncmp(key,"vahi",4)==0){
                            int isVahi=(key[0]=='v');
                            int lvl=key[4]-'0';
                            if(lvl>=0&&lvl<=4){
                                const char *rest=key+5;
                                int slot=rest[strlen(rest)-1]-'0';
                                if(slot>=0&&slot<=4){
                                    char field[16]={0}; size_t rlen=strlen(rest);
                                    if(rlen>1){ memcpy(field,rest,rlen-1); field[rlen-1]='\0'; }
                                    int p=isVahi?1:0;
                                    if(strcmp(field,"name")==0) snprintf(g_recName[p][lvl][slot],32,"%s",val);
                                    else if(strcmp(field,"time")==0) g_recTime[p][lvl][slot]=atoi(val);
                                    else if(strcmp(field,"face")==0) g_recFace[p][lvl][slot]=atoi(val);
                                    else if(strcmp(field,"diff")==0) g_recDiff[p][lvl][slot]=atoi(val);
                                }
                            }
                        }
                    }
                    if(!eol) break;
                    line=eol+1;
                }
                free(buf);
            }
            fclose(f);
            appLog("[record] loaded real fshi/vahi from config.mm (toplevel=%d)", g_nLevelCount);
        }
    }

    if (nType == 0) {
        if (g_hMenuTexLevel == NULL) g_hMenuTexLevel = (void*)(uintptr_t)gxLoadTpgFile("menu\\level00.tpg");
        if (g_hMenuTexChar == NULL) g_hMenuTexChar = (void*)(uintptr_t)gxLoadTpgFile("menu\\char00.tpg");
        if (g_hMenuTexGfx == NULL) g_hMenuTexGfx = (void*)(uintptr_t)gxLoadTpgFile("menu\\gfx00.tpg");

        g_nScoreTableRow = 0;
        do {
            if (1) {
                int yBase = 0x8200;
                int textY = 0x94;
                int col = g_nScoreTableRow;
                int prefix = col; /* 0=fshi,1=vahi @0x4508b0/@0x450a58 */
                do {
                    int idx = (yBase - 0x8200)/0x4600; /* 0..4 */
                    char name[32]; int tm, face, diff;
                    /* inline recordFetch from g_rec* (no helper) */
                    {
                        int p=prefix; if(p<0)p=0; if(p>1)p=1;
                        int lvl=g_nResultsLevel; if(lvl<0)lvl=0; if(lvl>4)lvl=4;
                        int slot=idx; if(slot<0)slot=0; if(slot>4)slot=4;
                        strncpy(name, g_recName[p][lvl][slot], 32); name[31]='\0'; if(name[0]=='\0') strcpy(name,"-");
                        tm=g_recTime[p][lvl][slot]; face=g_recFace[p][lvl][slot]; diff=g_recDiff[p][lvl][slot];
                    }
                    {
                        GxVert v0,v1,v2,v3; GxColorUv uv;
                        uv.pTexture=g_hMenuTexGfx; uv.pParam5=NULL; uv.pad=0;
                        uv.U=0; uv.V=0; uv.V2=0; uv.gwU=0xff00; uv.gwU2=0xff00;
                        uv.hV=0x3200; uv.hV2=0x3200; uv.U2=0;
                        v0.x=(col*0x140+0x32)*0x100; v1.x=(col*0x140+0x131)*0x100;
                        v2.x=v1.x; v3.x=v0.x;
                        v0.y=yBase; v1.y=yBase; v2.y=yBase+0x3300; v3.y=yBase+0x3300;
                        setSignVerts(&v0,&v1,&v2,&v3);
                        gxDrawPolygon(&v0,&v1,&v2,&v3,0x2004,&uv);
                    }
                    {
                        int ss = tm % 100;
                        int t2 = tm / 100;
                        int mm = t2 % 60;
                        int hh = t2 / 60;
                        char combined[96];
                        snprintf(combined, sizeof(combined), "%s   %02d:%02d:%02d", name, hh, mm, ss);
                        if (g_hMenuFontTiny){
                            int w=textWidth(g_hMenuFontTiny, combined);
                            textDraw(g_hMenuFontTiny,0x2004, col*0x140 - w/2 +0xb2, textY, combined);
                        }
                    }
                    g_nScoreTableTick++;
                    {
                        int u = (face & 3) * 0x40;
                        int v = (face >> 2) * 0x40;
                        GxVert v0,v1,v2,v3; GxColorUv uv;
                        uv.pTexture=g_hMenuTexChar; uv.pParam5=NULL; uv.pad=0;
                        uv.U=(unsigned short)(u*0x100); uv.V=(unsigned short)(v*0x100);
                        uv.U2=uv.U; uv.V2=uv.V;
                        uv.gwU=(unsigned short)((u+0x3f)*0x100); uv.gwU2=uv.gwU;
                        uv.hV=(unsigned short)((v+0x3f)*0x100); uv.hV2=uv.hV;
                        v0.x=(col*0x140+10)*0x100; v1.x=(col*0x140+0x49)*0x100;
                        v2.x=v1.x; v3.x=v0.x;
                        v0.y=yBase; v1.y=yBase; v2.y=yBase+0x3f00; v3.y=yBase+0x3f00;
                        setSignVerts(&v0,&v1,&v2,&v3);
                        gxDrawPolygon(&v0,&v1,&v2,&v3,0x2004,&uv);
                    }
                    {
                        int off = diff *0x1a;
                        GxVert v0,v1,v2,v3; GxColorUv uv;
                        uv.pTexture=g_hMenuTexGfx; uv.pParam5=NULL; uv.pad=0;
                        uv.V=32000; uv.V2=32000; uv.hV=0x9600; uv.hV2=0x9600;
                        uv.U=(unsigned short)((off+0x65)*0x100); uv.U2=uv.U;
                        uv.gwU=(unsigned short)((off+0x7e)*0x100); uv.gwU2=uv.gwU;
                        v0.x=(col*0x140+0x118)*0x100; v1.x=(col*0x140+0x132)*0x100;
                        v2.x=v1.x; v3.x=v0.x;
                        v0.y=yBase+0x1400; v1.y=yBase+0x1400; v2.y=yBase+0x2e00; v3.y=yBase+0x2e00;
                        setSignVerts(&v0,&v1,&v2,&v3);
                        gxDrawPolygon(&v0,&v1,&v2,&v3,0x2004,&uv);
                    }
                    yBase+=0x4600; textY+=0x46;
                } while (yBase < 0x1e000);
            }
            g_nScoreTableRow++;
        } while (g_nScoreTableRow < 2);

        g_flHighScoreAnimTime += g_flFrameDelta * 0.3f;

        /* Headers — original draws "Fr\xe5gesport" at 0x3c,0x50 and Vagnrace at 0x19a
         * inline two-font split, not via helper. */
        {
            const char *s="Fr\xe5gesport"; int x=0x3c, y=0x50;
            gxFont *hiSmall=g_hMenuMsfnt, *hiBig=g_hMenuMfnt;
            if(hiSmall&&hiBig){
                const char *p=s; int cx=x;
                while(*p){ unsigned int u=(unsigned char)*p; const char *q=p; char tok[128]; int n;
                    if((u>0x60&&u<0x7b)||u==0xe5||u==0xe4||u==0xf6){ while(*q){u=(unsigned char)*q; if(!((u>0x60&&u<0x7b)||u==0xe5||u==0xe4||u==0xf6))break; q++;} n=(int)(q-p); if(n>0){memcpy(tok,p,n);tok[n]=0;textDraw(hiSmall,0x2004,cx,y,tok);cx+=textWidth(hiSmall,tok);} }
                    else { while(*q){u=(unsigned char)*q; if((u>0x60&&u<0x7b)||u==0xe5||u==0xe4||u==0xf6)break; q++;} n=(int)(q-p); if(n>0){memcpy(tok,p,n);tok[n]=0;textDraw(hiBig,0x2004,cx,y,tok);cx+=textWidth(hiBig,tok);} }
                    p=q;
                }
            } else if(g_hMenuFont) textDraw(g_hMenuFont,0x2004,x,y,(char*)s);
        }
        {
            const char *s="Vagnrace"; int x=0x19a, y=0x50;
            gxFont *hiSmall=g_hMenuMsfnt, *hiBig=g_hMenuMfnt;
            if(hiSmall&&hiBig){
                const char *p=s; int cx=x;
                while(*p){ unsigned int u=(unsigned char)*p; const char *q=p; char tok[128]; int n;
                    if((u>0x60&&u<0x7b)||u==0xe5||u==0xe4||u==0xf6){ while(*q){u=(unsigned char)*q; if(!((u>0x60&&u<0x7b)||u==0xe5||u==0xe4||u==0xf6))break; q++;} n=(int)(q-p); if(n>0){memcpy(tok,p,n);tok[n]=0;textDraw(hiSmall,0x2004,cx,y,tok);cx+=textWidth(hiSmall,tok);} }
                    else { while(*q){u=(unsigned char)*q; if((u>0x60&&u<0x7b)||u==0xe5||u==0xe4||u==0xf6)break; q++;} n=(int)(q-p); if(n>0){memcpy(tok,p,n);tok[n]=0;textDraw(hiBig,0x2004,cx,y,tok);cx+=textWidth(hiBig,tok);} }
                    p=q;
                }
            } else if(g_hMenuFont) textDraw(g_hMenuFont,0x2004,x,y,(char*)s);
        }

        {
            GxVert v0,v1,v2,v3; GxColorUv uv;
            uv.pTexture=g_hMenuTexLevel; uv.pParam5=NULL; uv.pad=0;
            uv.U=0; uv.U2=0; uv.gwU=0xff00; uv.gwU2=0xff00;
            uv.V=(unsigned short)(g_nResultsLevel*0x3300);
            uv.V2=uv.V;
            uv.hV=(unsigned short)((g_nResultsLevel+1)*0x3300);
            uv.hV2=uv.hV;
            v0.x=0xe800; v1.x=0x1e700; v2.x=0x1e700; v3.x=0xe800;
            v0.y=0xa00; v1.y=0xa00; v2.y=0x3d00; v3.y=0x3d00;
            setSignVerts(&v0,&v1,&v2,&v3);
            gxDrawPolygon(&v0,&v1,&v2,&v3,0x2004,&uv);
        }
        gxDrawQuadColor(g_hMenuTexGfx,0x98,0x10,0xd7,0x37,0xb0,0x33,0xef,0x5b);
        {
            int off = (int)(sinf(g_flHighScoreAnimTime)*5.0 + 100.0);
            gxDrawQuadColor(g_hMenuTexGfx, off-8,0xf, off+0x23,0x3a,0x58,0x33,0x83,0x5e);
        }
        {
            int off = (int)(sinf(g_flHighScoreAnimTime)*5.0 - 496.0);
            gxDrawQuadColor(g_hMenuTexGfx, 8-off,0xf, 0x33-off,0x3a,0x84,0x33,0xaf,0x5e);
        }
        g_nMenuFadeTarget = 0;
        return 0;
    } else if (nType == 1 && nKeyType == 2) {
        if (nKey == 0) {
            if (g_nLevelCount != 0) {
                g_nResultsLevel++;
                int mx = g_nLevelCount >4 ? 4 : g_nLevelCount;
                if (g_nResultsLevel > mx) g_nResultsLevel = 0;
                sndPlaySfx(0,1,1,0xffff,0,0x400);
                appLog("[record] level %d", g_nResultsLevel);
            }
            return 0;
        } else if (nKey == 1) {
            if (g_nLevelCount != 0) {
                g_nResultsLevel--;
                if (g_nResultsLevel < 0) {
                    int mx = g_nLevelCount >4 ? 4 : g_nLevelCount;
                    g_nResultsLevel = mx;
                }
                sndPlaySfx(0,1,1,0xffff,0,0x400);
                appLog("[record] level %d", g_nResultsLevel);
            }
            return 0;
        } else if (nKey == 2) {
            if (g_nRecordsRow > 0) {
                sndPlaySfx(0,1,1,0xffff,0,0x400);
                g_nRecordsRow--;
            } else {
                sndPlaySfx(0,1,1,0xffff,0,0x400);
                g_nRecordsRow = 1;
            }
            return 0;
        } else if (nKey == 3) {
            if (g_nRecordsRow < 1) {
                sndPlaySfx(0,1,1,0xffff,0,0x400);
                g_nRecordsRow++;
            } else {
                sndPlaySfx(0,1,1,0xffff,0,0x400);
                g_nRecordsRow = 0;
            }
            return 0;
        } else if (nKey == 6) {
            sndPlaySfx(0,1,3,0xffff,0,0x400);
            appLog("[record] Enter row %d -> menu", g_nRecordsRow);
            g_pStateFunc = menuUpdate;
            return 0;
        } else if (nKey == 7) {
            sndPlaySfx(0,1,4,0xffff,0,0x400);
            appLog("[record] Esc -> menu");
            g_pStateFunc = menuUpdate;
            return 0;
        } else {
            return 0;
        }
    }
    if (nType != 0) return 0;
    return 0;
}
