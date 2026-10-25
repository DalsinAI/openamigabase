/*
 * OpenBase - the GFOffice Suite's database, on AmigaOS 3.2.
 *
 * Phase B2, the program (Design-OpenBase.md, 13), on the core in src/core:
 *
 * - a project is one .oadb file (an SQLite database), made and opened here;
 * - the object browser: the project's tables, and its relationships;
 * - the table designer: a table's fields and their types, required or not,
 *   and Lookup fields that point to another table (a relationship that
 *   refuses an orphan, and a record pointed to can't be deleted);
 * - the datasheet: a table's records, a row each;
 * - the auto form: the record shown in the datasheet, a row a field
 *   (OpenGadTools' sections), changed and saved, new ones added, deleted;
 * - Datatype object fields: any file a datatype reads, kept in the record,
 *   a picture drawn beside the form;
 * - the relationships view: the tables as boxes, a line for each
 *   relationship.
 *
 * SQLite is the soft-float, single-thread build (build-os3.sh) with the
 * unix-none VFS (no file locks on AmigaDOS): one OpenBase on a project at
 * a time is kept by a public Exec semaphore named after the file
 * (Design-OpenBase.md 3.2).
 *
 * Chrome: OpenGadTools' toolbar with the Chrome icon set, list, sections
 * and status bar, in the theme (opengadtools docs/style/README.md). 68040,
 * no FPU needed.
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/semaphores.h>
#include <intuition/intuition.h>
#include <intuition/gadgetclass.h>
#include <libraries/gadtools.h>
#include <libraries/asl.h>
#include <workbench/startup.h>
#include <dos/dos.h>
#include <dos/var.h>
#include <graphics/text.h>
#include <graphics/gfx.h>
#include <datatypes/datatypes.h>
#include <datatypes/datatypesclass.h>
#include <datatypes/pictureclass.h>
#include <cybergraphx/cybergraphics.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/gadtools.h>
#include <proto/asl.h>
#include <proto/datatypes.h>
#include <proto/cybergraphics.h>
#include "ogt_openfont.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <clib/alib_protos.h>
#include <sqlite3.h>

#include "openbase_core.h"
#include "ob_stack.h"
#include "lastdir.h"
#include "ogt_theme.h"
#include "ogt_draw.h"
#include "ogt_icons.h"
#include "ogt_toolbar.h"
#include "ogt_iconset.h"
#include "ogt_font.h"
#include "ogt_list.h"
#include "ogt_status.h"
#include "ogt_sections.h"

struct Library *GadToolsBase = NULL, *AslBase = NULL, *OpenFontTextBase = NULL;
struct Library *DataTypesBase = NULL, *CyberGfxBase = NULL;

#define PROGRAM "OpenBase"
#define VERSION_TEXT "OpenBase 0.2.0 (25.10.2026)"
static const char version[] __attribute__((used)) =
    "$VER: " VERSION_TEXT " MIT, Copyright (c) 2026 AmigaChrome Team";

enum { C_OPEN = 1, C_NEWTABLE, C_RELATIONS, C_NEWREC, C_SAVEREC, C_DELREC, C_PICTURE };
enum { GID_TOOLBAR = 100, GID_BROWSER = 200, GID_SHEET = 201, GID_FORM = 1000, GID_FORMBTN = 2000 };
enum {
    M_NEWPROJ = 1, M_OPENPROJ, M_CLOSEPROJ, M_CHECK, M_ABOUT, M_QUIT,
    M_NEWTABLE, M_DESIGN, M_RELATIONS,
    M_NEWREC, M_SAVEREC, M_DELREC, M_UNDOREC, M_LOADPIC, M_CLEARPIC,
    M_TB_BOTH, M_TB_ICONS, M_TB_TEXT
};

static struct NewMenu menus[] = {
    { NM_TITLE, "Project", NULL, 0, 0, NULL },
    { NM_ITEM, "New Project...", NULL, 0, 0, (APTR)M_NEWPROJ },
    { NM_ITEM, "Open Project...", "O", 0, 0, (APTR)M_OPENPROJ },
    { NM_ITEM, "Close Project", NULL, 0, 0, (APTR)M_CLOSEPROJ },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Check Project", NULL, 0, 0, (APTR)M_CHECK },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "About...", NULL, 0, 0, (APTR)M_ABOUT },
    { NM_ITEM, "Quit", "Q", 0, 0, (APTR)M_QUIT },

    { NM_TITLE, "Table", NULL, 0, 0, NULL },
    { NM_ITEM, "New Table...", "T", 0, 0, (APTR)M_NEWTABLE },
    { NM_ITEM, "Design Table...", NULL, 0, 0, (APTR)M_DESIGN },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Relationships", "R", 0, 0, (APTR)M_RELATIONS },

    { NM_TITLE, "Record", NULL, 0, 0, NULL },
    { NM_ITEM, "New Record", "N", 0, 0, (APTR)M_NEWREC },
    { NM_ITEM, "Save Record", "S", 0, 0, (APTR)M_SAVEREC },
    { NM_ITEM, "Undo Changes", "Z", 0, 0, (APTR)M_UNDOREC },
    { NM_ITEM, "Delete Record...", NULL, 0, 0, (APTR)M_DELREC },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Load Picture...", "L", 0, 0, (APTR)M_LOADPIC },
    { NM_ITEM, "Clear Picture", NULL, 0, 0, (APTR)M_CLEARPIC },

    { NM_TITLE, "View", NULL, 0, 0, NULL },
    { NM_ITEM, "Buttons", NULL, 0, 0, NULL },
    { NM_SUB, "Icons and Text", NULL, CHECKIT, ~1 & 7, (APTR)M_TB_BOTH },
    { NM_SUB, "Icons", NULL, CHECKIT | CHECKED, ~2 & 7, (APTR)M_TB_ICONS },
    { NM_SUB, "Text", NULL, CHECKIT, ~4 & 7, (APTR)M_TB_TEXT },

    { NM_TITLE, "Help", NULL, 0, 0, NULL },
    { NM_ITEM, "About OpenBase...", NULL, 0, 0, (APTR)M_ABOUT },
    { NM_END, NULL, NULL, 0, 0, NULL }
};

/* The Chrome icon set's pictures by name, the vector icons as their fallback (the set has no Delete). */
static const ogt_tool tools[] = {
    { C_OPEN,      "Open...",       OGT_ICON_OPEN,    0, 0, 0, "open" },
    { C_NEWTABLE,  "New Table",     OGT_ICON_FILE,    1, 0, 0, "table" },
    { C_RELATIONS, "Relationships", OGT_ICON_ATTACH,  0, 0, 0, "link" },
    { C_NEWREC,    "New Record",    OGT_ICON_FILE,    1, 0, 0, "new" },
    { C_SAVEREC,   "Save Record",   OGT_ICON_DISK,    0, 0, 0, "save" },
    { C_DELREC,    "Delete Record", OGT_ICON_DELETE,  0, 0, 0, NULL },
    { C_PICTURE,   "Picture...",    OGT_ICON_ATTACH,  1, 0, 0, "picture" }
};

static const char classic_theme[] =
    "name Classic\nversion 1\nfont system\ntitle.align left\npassthrough yes\n[four]\n"
    "pens #aaaaaa #000000 #ffffff #6688bb\ntitle.active.fill 3\ntitle.active.text 1\nfill.text 1\n";

typedef struct box { int x, y, w, h; } box;

static struct Screen *scr;
static APTR vi;
static struct Window *win;
static struct Menu *menu;
static struct Gadget *chain;                 /* the toolbar's buttons */
static struct Gadget *form_list;             /* the form's GadTools gadgets */
static ogt_list *browser, *sheet;
static ogt_status sbar;
static ogt_sections secs;
static struct TextFont *font;
static struct TextAttr font_attr;
static int fh;
static ogt_theme theme;
static ogt_ctx ctx;
static ogt_toolbar tb;
static char theme_name[48] = "Open";
static int theme_mode = OGT_LIGHT;
static int tb_style = OGT_TB_ICONS;
static int quit_now;
static WORD zoom_box[4];

static box area, toolbar_box, browser_box, main_box, header_box, sheet_box, form_box, pic_box, status_box;
static char status[320] = "Make a project, or open one: the Project menu.";

/* The project */
static ob_project *proj;
static char proj_path[512];
static struct SignalSemaphore *proj_sem;
static char **tables;
static int ntables;
static int sel = -1;                         /* the browser's row: a table, or ntables: the relationships */

/* The table shown, its records' keys, and the record in the form */
static ob_table *cur;
static long long *keys;
static int nkeys;
static int row = -1;                         /* the record's row in the datasheet; -1: a new record */
static char **loaded;                        /* its values as read (cur->nfields); NULL: a new record */
static char **typed;                         /* the form's text for each field: loaded, or typed since */
static int colw[OB_FIELDS_MAX];              /* the datasheet's columns */

/* The form: a field for each of its rows (-1: a row that is a button) */
#define FORM_ROWS (OGT_SEC_MAX * OGT_SEC_ROWS)
static int form_field[FORM_ROWS], form_kind[FORM_ROWS], nform, form_left;
static char sec_title[OGT_SEC_MAX][64];

/* The relationships view */
static ob_relation *rels;
static int nrels;
static ob_table **rel_tables;                /* each table's design, as the view draws them */

static void draw_all(void);
static void draw_status(void);
static void drop_form(void);
static void layout(void);
static void relayout(void);

/* ---- Small helpers ---- */

static char *read_file(const char *path)
{
    BPTR f = Open((STRPTR)path, MODE_OLDFILE);
    LONG n, cap = 32768;
    char *buf;
    if (!f) return NULL;
    buf = malloc(cap);
    if (!buf) { Close(f); return NULL; }
    n = Read(f, buf, cap - 1);
    Close(f);
    if (n < 0) { free(buf); return NULL; }
    buf[n] = 0;
    return buf;
}

static void load_theme(void)
{
    char path[160], err[120];
    char *text;
    snprintf(path, sizeof path, "PROGDIR:Themes/%s.theme", theme_name);
    text = read_file(path);
    if (!text) {
        snprintf(path, sizeof path, "SYS:Prefs/Presets/Themes/%s.theme", theme_name);
        text = read_file(path);
    }
    if (theme.text) ogt_theme_free(&theme);
    if (!text || !ogt_theme_parse(&theme, text, err, sizeof err)) {
        ogt_theme_parse(&theme, classic_theme, err, sizeof err);
        strcpy(theme_name, "Classic");
    }
    free(text);
}

static void read_theme_choice(void)
{
    char *s = read_file("ENV:OpenGadTools/Theme");
    if (!s) return;
    {
        char *nl = strchr(s, '\n');
        if (nl) *nl = 0;
        if (*s && strlen(s) < sizeof theme_name) strcpy(theme_name, s);
        theme_mode = nl && !strncmp(nl + 1, "dark", 4) ? OGT_DARK : OGT_LIGHT;
    }
    free(s);
}

/* ---- Messages ----
 * EasyRequest cut its lines short on the lab's RTG screen ("Customer: 99 doesn't match a recor"; OpenCalc
 * saw the same), so OpenBase says things in a window of its own, as wide as its widest line in the font it
 * draws with (up to two thirds of the screen; longer lines wrap at spaces). The buttons are numbered as
 * EasyRequest numbers them: 1, 2, ... from the left, the last 0. Return answers a lone OK, Escape the last
 * button; the main window takes no input meanwhile. */
#define SAY_LINES 16
#define SAY_LINE 160
#define SAY_BUTTONS 3

/* text's paragraphs ('\n' between them) broken to maxw pixels in rp's font. The count. */
static int say_wrap(struct RastPort *rp, const char *p, int maxw, char lines[][SAY_LINE], int maxlines)
{
    int n = 0;
    while (n < maxlines) {
        const char *end = strchr(p, '\n');
        int len = end ? (int)(end - p) : (int)strlen(p);
        for (;;) {
            int fit = len < SAY_LINE - 1 ? len : SAY_LINE - 1, cut;
            while (fit > 1 && TextLength(rp, (STRPTR)p, fit) > maxw) fit--;
            cut = fit;
            if (fit < len) {                     /* back to the last space, when there is one */
                int k = fit;
                while (k > 0 && p[k] != ' ') k--;
                if (k > 0) cut = k;
            }
            memcpy(lines[n], p, (size_t)cut);
            lines[n][cut] = 0;
            n++;
            p += cut;
            len -= cut;
            while (len > 0 && *p == ' ') { p++; len--; }
            if (len <= 0 || n >= maxlines) break;
        }
        if (!end || !end[1]) break;
        p = end + 1;
    }
    return n;
}

static void say_draw(struct Window *rw, char lines[][SAY_LINE], int nl, int w, int h)
{
    struct RastPort *rp = rw->RPort;
    int i, lh = fh + 4;
    SetFont(rp, font);
    ogt_fill(&ctx, rp, "window", rw->BorderLeft, rw->BorderTop, w, h);
    for (i = 0; i < nl; ++i)
        ogt_text(rp, ogt_pen(&ctx, "text"), rw->BorderLeft + 16, rw->BorderTop + 14 + i * lh, lines[i], w - 32);
    RefreshGList(rw->FirstGadget, rw, NULL, -1);    /* the buttons, over the fill */
    GT_RefreshWindow(rw, NULL);
}

static int say(const char *title, const char *text, const char *buttons)
{
    static char lines[SAY_LINES][SAY_LINE];
    char labels[SAY_BUTTONS][32];
    struct Gadget *glist = NULL, *g;
    struct NewGadget ng;
    struct Window *rw = NULL;
    struct Requester block;
    struct RastPort trp;
    const char *b = buttons;
    int nb = 0, nl, i, bw = 80, tw = 0, lh = fh + 4, bh = fh + 8, ww, wh, maxw, answer = -1, bl, bt, blocked = 0;
    if (scr && font) {
        while (*b && nb < SAY_BUTTONS) {
            const char *bar = strchr(b, '|');
            int n = bar ? (int)(bar - b) : (int)strlen(b);
            if (n > 31) n = 31;
            memcpy(labels[nb], b, (size_t)n);
            labels[nb++][n] = 0;
            if (!bar) break;
            b = bar + 1;
        }
        if (!nb) { strcpy(labels[0], "OK"); nb = 1; }
        InitRastPort(&trp);
        SetFont(&trp, font);
        maxw = scr->Width * 2 / 3 > 640 ? 640 : scr->Width * 2 / 3;
        nl = say_wrap(&trp, text ? text : "", maxw, lines, SAY_LINES);
        for (i = 0; i < nl; ++i) {
            int w = TextLength(&trp, (STRPTR)lines[i], strlen(lines[i]));
            if (w > tw) tw = w;
        }
        for (i = 0; i < nb; ++i) {
            int w = TextLength(&trp, (STRPTR)labels[i], strlen(labels[i])) + 28;
            if (w > bw) bw = w;
        }
        ww = tw + 32;
        if (ww < nb * (bw + 16) + 16) ww = nb * (bw + 16) + 16;
        if (ww < 260) ww = 260;
        wh = 14 + nl * lh + 14 + bh + 10;
        bl = scr->WBorLeft;
        bt = scr->WBorTop + scr->Font->ta_YSize + 1;
        g = CreateContext(&glist);
        memset(&ng, 0, sizeof ng);
        ng.ng_TextAttr = &font_attr;
        ng.ng_VisualInfo = vi;
        ng.ng_Flags = PLACETEXT_IN;
        ng.ng_Width = bw;
        ng.ng_Height = bh;
        ng.ng_TopEdge = bt + wh - bh - 10;
        for (i = 0; i < nb && g; ++i) {
            /* the first at the left, the last at the right, one between them in the middle */
            ng.ng_LeftEdge = nb == 1 || (i > 0 && i < nb - 1) ? bl + (ww - bw) / 2 : i == 0 ? bl + 16 : bl + ww - 16 - bw;
            ng.ng_GadgetText = (STRPTR)labels[i];
            ng.ng_GadgetID = i == nb - 1 ? 0 : i + 1;
            g = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
        }
        if (g) {
            int ow = ww + bl + scr->WBorRight, oh = wh + bt + scr->WBorBottom;
            int x = win ? win->LeftEdge + (win->Width - ow) / 2 : (scr->Width - ow) / 2;
            int y = win ? win->TopEdge + (win->Height - oh) / 3 : (scr->Height - oh) / 2;
            if (x + ow > scr->Width) x = scr->Width - ow;
            if (y + oh > scr->Height) y = scr->Height - oh;
            if (x < 0) x = 0;
            if (y < 0) y = 0;
            rw = OpenWindowTags(NULL,
                WA_PubScreen, (ULONG)scr, WA_Left, x, WA_Top, y, WA_InnerWidth, ww, WA_InnerHeight, wh,
                WA_Title, (ULONG)title, WA_Gadgets, (ULONG)glist, WA_DragBar, TRUE, WA_DepthGadget, TRUE,
                WA_Activate, TRUE, WA_RMBTrap, TRUE, WA_SimpleRefresh, TRUE,
                WA_IDCMP, IDCMP_GADGETUP | IDCMP_VANILLAKEY | IDCMP_REFRESHWINDOW | BUTTONIDCMP,
                TAG_DONE);
        }
    }
    if (!rw) {                                   /* no room for a window: the system's requester */
        struct EasyStruct es = { sizeof(struct EasyStruct), 0, (UBYTE *)title, (UBYTE *)"%s", (UBYTE *)buttons };
        if (glist) FreeGadgets(glist);
        answer = (int)EasyRequest(win, &es, NULL, (ULONG)text);
        if (win) ActivateWindow(win);
        return answer;
    }
    if (win) {
        InitRequester(&block);
        blocked = Request(&block, win) != 0;
        SetWindowPointer(win, WA_BusyPointer, TRUE, TAG_DONE);
    }
    say_draw(rw, lines, nl, ww, wh);
    while (answer < 0) {
        struct IntuiMessage *m;
        WaitPort(rw->UserPort);
        while (answer < 0 && (m = GT_GetIMsg(rw->UserPort))) {
            ULONG cls = m->Class;
            UWORD code = m->Code;
            struct Gadget *gg = (struct Gadget *)m->IAddress;
            GT_ReplyIMsg(m);
            if (cls == IDCMP_GADGETUP && gg) answer = gg->GadgetID;
            else if (cls == IDCMP_VANILLAKEY && (code == 27 || (code == 13 && nb == 1))) answer = 0;
            else if (cls == IDCMP_REFRESHWINDOW) {
                GT_BeginRefresh(rw);
                say_draw(rw, lines, nl, ww, wh);
                GT_EndRefresh(rw, TRUE);
            }
        }
    }
    CloseWindow(rw);
    FreeGadgets(glist);
    if (win) {
        if (blocked) EndRequest(&block, win);
        SetWindowPointer(win, TAG_DONE);
        ActivateWindow(win);
    }
    return answer;
}

static void tell(const char *title, const char *text)
{
    say(title, text, "OK");
}

static int ask(const char *title, const char *text, const char *buttons)
{
    return say(title, text, buttons);
}

static void set_status(const char *s)
{
    size_t n = s ? strlen(s) : 0;
    if (n >= sizeof status) n = sizeof status - 1;
    if (n) memcpy(status, s, n);
    status[n] = 0;
}

static const char *leaf(const char *path)
{
    const char *s = FilePart((STRPTR)path);
    return s && *s ? s : path;
}

static char *dup_text(const char *s)
{
    size_t n = s ? strlen(s) : 0;
    char *d = malloc(n + 1);
    if (d) {
        if (n) memcpy(d, s, n);
        d[n] = 0;
    }
    return d;
}

static void free_texts(char **v, int n)
{
    int i;
    if (!v) return;
    for (i = 0; i < n; ++i) free(v[i]);
    free(v);
}

static const char *field_label(const ob_field *f)
{
    return f->label && *f->label ? f->label : f->name;
}

static int ask_file(int save, const char *title, const char *initial, const char *pattern, char *out, size_t out_size)
{
    struct FileRequester *fr;
    char last[512];
    int ok = 0;
    /* projects and the files put in records each remember their own drawer */
    const char *keep = pattern ? PROGRAM : PROGRAM "Files";
    lastdir_get(keep, last, sizeof last);
    fr = AllocAslRequestTags(ASL_FileRequest,
        ASLFR_TitleText, (ULONG)title,
        ASLFR_InitialDrawer, (ULONG)last,
        ASLFR_Window, (ULONG)win,
        ASLFR_SleepWindow, TRUE,
        ASLFR_DoSaveMode, save ? TRUE : FALSE,
        ASLFR_InitialFile, (ULONG)(initial ? initial : ""),
        ASLFR_DoPatterns, pattern ? TRUE : FALSE,
        ASLFR_InitialPattern, (ULONG)(pattern ? pattern : "#?"),
        ASLFR_PositiveText, (ULONG)(save ? "Make" : "Open"),
        TAG_DONE);
    if (!fr) return 0;
    if (AslRequestTags(fr, TAG_DONE) && fr->fr_File[0]) {
        snprintf(out, out_size, "%s", fr->fr_Drawer);
        lastdir_put(keep, (const char *)fr->fr_Drawer);
        AddPart((STRPTR)out, fr->fr_File, out_size);
        ok = 1;
    }
    FreeAslRequest(fr);
    if (win) ActivateWindow(win);
    return ok;
}

/* ---- One OpenBase on a project at a time ----
 * The unix-none VFS takes no locks, and two programs writing one SQLite file damage it: a public semaphore
 * named after the project's full path says it is open (Design-OpenBase.md 3.2). */

static int full_path(const char *path, char *out, size_t size)
{
    BPTR l = Lock((STRPTR)path, ACCESS_READ);
    if (l) {
        int ok = NameFromLock(l, (STRPTR)out, (LONG)size) != 0;
        UnLock(l);
        if (ok) return 1;
    }
    /* not there yet: its drawer's full name and the file's */
    {
        char dir[512];
        const char *name = FilePart((STRPTR)path);
        size_t n = (size_t)(name - path);
        if (n >= sizeof dir) return 0;
        memcpy(dir, path, n);
        dir[n] = 0;
        l = Lock((STRPTR)dir, ACCESS_READ);
        if (!l) return 0;
        if (!NameFromLock(l, (STRPTR)out, (LONG)size)) {
            UnLock(l);
            return 0;
        }
        UnLock(l);
        return AddPart((STRPTR)out, (STRPTR)name, (ULONG)size) != 0;
    }
}

static int lock_project(const char *path)
{
    char full[512], name[600];
    struct SignalSemaphore *found, *s;
    size_t i, n;
    if (!full_path(path, full, sizeof full)) snprintf(full, sizeof full, "%s", path);
    snprintf(name, sizeof name, "OpenBase project %s", full);
    n = strlen(name);
    for (i = 0; i < n && name[i]; ++i) name[i] = (char)tolower((unsigned char)name[i]);
    s = AllocVec(sizeof *s + n + 1, MEMF_PUBLIC | MEMF_CLEAR);
    if (!s) return 0;
    memcpy((char *)(s + 1), name, n + 1);
    s->ss_Link.ln_Name = (char *)(s + 1);
    s->ss_Link.ln_Pri = 0;
    Forbid();
    found = FindSemaphore((STRPTR)name);
    if (!found) AddSemaphore(s);
    Permit();
    if (found) {
        FreeVec(s);
        return 0;
    }
    proj_sem = s;
    return 1;
}

static void unlock_project(void)
{
    if (!proj_sem) return;
    RemSemaphore(proj_sem);
    FreeVec(proj_sem);
    proj_sem = NULL;
}

/* ---- Records: the datasheet's rows, read when drawn and kept for a while ---- */

#define CACHE 64
static struct { long long key; char **v; int n; ULONG used; } cache[CACHE];
static ULONG cache_tick;

static void cache_clear(void)
{
    int i;
    for (i = 0; i < CACHE; ++i) {
        if (cache[i].v) ob_record_free(cache[i].v, cache[i].n);
        memset(&cache[i], 0, sizeof cache[i]);
    }
}

static void cache_forget(long long key)
{
    int i;
    for (i = 0; i < CACHE; ++i)
        if (cache[i].v && cache[i].key == key) {
            ob_record_free(cache[i].v, cache[i].n);
            memset(&cache[i], 0, sizeof cache[i]);
        }
}

/* A row's values, NULL when it can't be read. */
static char **row_values(int index)
{
    int i, slot = 0;
    char **v = NULL;
    ob_error e;
    if (!proj || !cur || index < 0 || index >= nkeys) return NULL;
    for (i = 0; i < CACHE; ++i)
        if (cache[i].v && cache[i].key == keys[index]) {
            cache[i].used = ++cache_tick;
            return cache[i].v;
        }
    for (i = 0; i < CACHE; ++i) {
        if (!cache[i].v) { slot = i; break; }
        if (cache[i].used < cache[slot].used) slot = i;
    }
    if (ob_record_read(proj, cur, keys[index], &v, &e) != OB_OK) return NULL;
    if (cache[slot].v) ob_record_free(cache[slot].v, cache[slot].n);
    cache[slot].key = keys[index];
    cache[slot].v = v;
    cache[slot].n = cur->nfields;
    cache[slot].used = ++cache_tick;
    return v;
}

static int load_keys(void)
{
    long n = 0;
    int got = 0;
    ob_error e;
    free(keys);
    keys = NULL;
    nkeys = 0;
    if (!proj || !cur) return 1;
    if (ob_record_count(proj, cur->name, &n, &e) != OB_OK) {
        set_status(e.text);
        return 0;
    }
    if (n > 100000) n = 100000;                  /* a datasheet's first hundred thousand */
    if (n <= 0) return 1;
    keys = malloc((size_t)n * sizeof *keys);
    if (!keys) {
        set_status("Not enough memory for the table's records.");
        return 0;
    }
    if (ob_record_keys(proj, cur->name, 0, (int)n, keys, &got, &e) != OB_OK) {
        set_status(e.text);
        return 0;
    }
    nkeys = got;
    return 1;
}

static int row_of_key(long long key)
{
    int i;
    for (i = 0; i < nkeys; ++i)
        if (keys[i] == key) return i;
    return -1;
}

/* ---- A Datatype object field's picture, for the record in the form ---- */

static struct {
    long long key;
    int field, tried;
    UBYTE *argb;
    int w, h;
} pic;

static void pic_forget(void)
{
    free(pic.argb);
    memset(&pic, 0, sizeof pic);
}

/* The table's first Datatype object field, or -1. */
static int object_field(void)
{
    int i;
    for (i = 0; cur && i < cur->nfields; ++i)
        if (cur->fields[i].type == OB_TYPE_OBJECT) return i;
    return -1;
}

/* The picture's pixels at most mw x mh, its shape kept, each the nearest source pixel, a band at a time. */
static int read_scaled(Object *o, UBYTE *out, int sw, int sh, int tw, int th)
{
    int rows = 65536 / (sw * 4) > 0 ? 65536 / (sw * 4) : 1, y0, ty, ok = 1;
    UBYTE *band = (UBYTE *)malloc((size_t)sw * 4 * (rows < sh ? rows : sh));
    if (!band) return 0;
    for (y0 = 0, ty = 0; ok && y0 < sh && ty < th; y0 += rows) {
        int n = sh - y0 < rows ? sh - y0 : rows;
        if (!DoMethod(o, PDTM_READPIXELARRAY, (ULONG)band, PBPAFMT_ARGB, (ULONG)sw * 4, 0, (ULONG)y0, (ULONG)sw,
                      (ULONG)n)) {
            ok = 0;
            break;
        }
        for (; ty < th; ++ty) {
            int sy = (int)((ULONG)ty * sh / th), k;
            const ULONG *src;
            ULONG *dst = (ULONG *)(out + (size_t)ty * tw * 4);
            if (sy >= y0 + n) break;
            src = (const ULONG *)(band + (size_t)(sy - y0) * sw * 4);
            for (k = 0; k < tw; ++k) dst[k] = src[(ULONG)k * sw / tw];
        }
    }
    free(band);
    return ok;
}

/* The record's object as a picture fitted into mw x mh: through T: and datatypes, as OpenWrite reads a
 * picture (Design-OpenBase.md 4.3). */
static void pic_load(int field, int mw, int mh)
{
    void *data = NULL;
    long size = 0;
    char tmp[96];
    BPTR f;
    Object *o = NULL;
    struct BitMapHeader *bmh = NULL;
    ob_error e;
    int ok;
    if (pic.tried && pic.key == keys[row] && pic.field == field) return;
    pic_forget();
    pic.tried = 1;
    pic.key = keys[row];
    pic.field = field;
    if (!DataTypesBase || mw < 8 || mh < 8) return;
    if (ob_object_get(proj, cur->name, cur->fields[field].name, keys[row], &data, &size, NULL, 0, &e) != OB_OK ||
        !data || size <= 0) {
        ob_free(data);
        return;
    }
    snprintf(tmp, sizeof tmp, "T:OpenBase-Object-%lx.tmp", (unsigned long)FindTask(NULL));
    f = Open((STRPTR)tmp, MODE_NEWFILE);
    if (!f) {
        ob_free(data);
        return;
    }
    ok = Write(f, data, size) == size;
    Close(f);
    ob_free(data);
    if (ok) o = NewDTObject((APTR)tmp, DTA_GroupID, GID_PICTURE, PDTA_DestMode, PMODE_V43, PDTA_Remap, FALSE, TAG_DONE);
    if (o) {
        int sw, sh, tw, th;
        GetDTAttrs(o, PDTA_BitMapHeader, (ULONG)&bmh, TAG_DONE);
        DoMethod(o, DTM_PROCLAYOUT, NULL, 1);
        sw = bmh ? bmh->bmh_Width : 0;
        sh = bmh ? bmh->bmh_Height : 0;
        if (sw > 0 && sh > 0) {
            tw = sw;
            th = sh;
            if (tw > mw) { th = (int)((long)th * mw / tw); tw = mw; }
            if (th > mh) { tw = (int)((long)tw * mh / th); th = mh; }
            if (tw < 1) tw = 1;
            if (th < 1) th = 1;
            if ((pic.argb = malloc((size_t)tw * th * 4)) != NULL && read_scaled(o, pic.argb, sw, sh, tw, th)) {
                pic.w = tw;
                pic.h = th;
            } else {
                free(pic.argb);
                pic.argb = NULL;
            }
        }
        DisposeDTObject(o);
    }
    DeleteFile((STRPTR)tmp);
}

/* ---- The project ---- */

static void free_texts_of_record(void)
{
    if (cur) {
        free_texts(typed, cur->nfields);
        if (loaded) ob_record_free(loaded, cur->nfields);
    }
    typed = NULL;
    loaded = NULL;
}

/* The record at row (or a new one) into the form's texts. */
static void load_record(void)
{
    int i;
    ob_error e;
    free_texts_of_record();
    pic_forget();
    if (!cur) return;
    if (row >= nkeys) row = nkeys - 1;
    if (row >= 0 && ob_record_read(proj, cur, keys[row], &loaded, &e) != OB_OK) {
        set_status(e.text);
        loaded = NULL;
        row = -1;
    }
    typed = calloc((size_t)(cur->nfields > 0 ? cur->nfields : 1), sizeof *typed);
    for (i = 0; typed && i < cur->nfields; ++i) typed[i] = dup_text(loaded ? loaded[i] : "");
}

static void free_table_view(void)
{
    int i;
    /* the form's gadgets hold this table's texts: gone first, or the next layout reads them into the next
     * table's record (row 1 of one table showed row 1 of the table before) */
    drop_form();
    free_texts_of_record();
    cache_clear();
    pic_forget();
    free(keys);
    keys = NULL;
    nkeys = 0;
    row = -1;
    if (cur) { ob_table_free(cur); cur = NULL; }
    if (rels) { ob_relations_free(rels, nrels); rels = NULL; nrels = 0; }
    if (rel_tables) {
        for (i = 0; i < ntables; ++i) ob_table_free(rel_tables[i]);
        free(rel_tables);
        rel_tables = NULL;
    }
}

static void load_tables(void)
{
    ob_error e;
    if (tables) ob_names_free(tables, ntables);
    tables = NULL;
    ntables = 0;
    if (proj && ob_table_list(proj, &tables, &ntables, &e) != OB_OK) set_status(e.text);
}

/* What the browser shows: table i, or the relationships (i == ntables). */
static void show_object(int i)
{
    ob_error e;
    free_table_view();
    sel = i;
    if (!proj || i < 0) return;
    if (i < ntables) {
        if (ob_table_read(proj, tables[i], &cur, &e) != OB_OK) {
            set_status(e.text);
            cur = NULL;
            return;
        }
        load_keys();
        row = nkeys ? 0 : -1;
        load_record();
        set_status(nkeys ? "" : "No records yet: Record > New Record adds one.");
    } else {
        int k;
        if (ob_relation_list(proj, &rels, &nrels, &e) != OB_OK) {
            set_status(e.text);
            rels = NULL;
            nrels = 0;
        }
        rel_tables = calloc((size_t)(ntables > 0 ? ntables : 1), sizeof *rel_tables);
        for (k = 0; rel_tables && k < ntables; ++k)
            if (ob_table_read(proj, tables[k], &rel_tables[k], &e) != OB_OK) rel_tables[k] = NULL;
        set_status(nrels ? "The relationships between the tables." : "No relationships yet: a Lookup field makes one.");
    }
}

static int table_index(const char *name)
{
    int i;
    for (i = 0; i < ntables; ++i)
        if (!strcmp(tables[i], name)) return i;
    return -1;
}

static int commit_record(void);

static void close_project(void)
{
    free_table_view();
    if (tables) ob_names_free(tables, ntables);
    tables = NULL;
    ntables = 0;
    sel = -1;
    if (proj) ob_project_close(proj);
    proj = NULL;
    proj_path[0] = 0;
    unlock_project();
}

static void project_config(ob_config *cfg)
{
    ob_config_defaults(cfg);
    cfg->vfs = "unix-none";                      /* no POSIX file locks on AmigaDOS: the semaphore instead */
    cfg->cache_kib = ob_config_cache_for(AvailMem(MEMF_ANY));
}

static void update_window_title(void)
{
    static char title[sizeof proj_path + 32];
    snprintf(title, sizeof title, "%s - " PROGRAM, proj_path[0] ? leaf(proj_path) : "No project");
    if (win) SetWindowTitles(win, (STRPTR)title, (STRPTR)-1);
}

/* Leaves the record in the form: its changes saved, or asked about when they can't be. 1: carry on. */
static int leave_record(void)
{
    if (!proj || !cur) return 1;
    if (commit_record() == OB_OK) return 1;
    return ask(PROGRAM, "The record's changes could not be saved.\n\nLose them?", "Lose Them|Cancel") == 1;
}

static void open_project(const char *path, int make)
{
    ob_config cfg;
    ob_project *p = NULL;
    ob_error e;
    char title[128], msg[600];
    int rc;
    if (!leave_record()) return;
    close_project();
    if (!lock_project(path)) {
        snprintf(msg, sizeof msg, "%.200s is open in another program.\n\nOne program at a time can change a project.",
                 leaf(path));
        tell(PROGRAM, msg);
        relayout();
        return;
    }
    project_config(&cfg);
    if (make) {
        const char *name = leaf(path), *dot = strrchr(name, '.');
        int n = dot && dot != name ? (int)(dot - name) : (int)strlen(name);
        snprintf(title, sizeof title, "%.*s", n > 100 ? 100 : n, name);
        rc = ob_project_create(path, title, &cfg, &p, &e);
    } else {
        rc = ob_project_open(path, 0, &cfg, &p, &e);
    }
    if (rc != OB_OK || !p) {
        snprintf(msg, sizeof msg, PROGRAM " could not %s %.200s.\n\n%.250s", make ? "make" : "open", leaf(path), e.text);
        unlock_project();
        tell(PROGRAM, msg);
        relayout();
        return;
    }
    proj = p;
    snprintf(proj_path, sizeof proj_path, "%s", path);
    load_tables();
    show_object(ntables ? 0 : -1);
    if (make) set_status("A new project: Table > New Table... makes its first table.");
    else if (!ntables) set_status("The project has no tables yet: Table > New Table...");
    update_window_title();
    relayout();
}

static void do_new_project(void)
{
    char path[512];
    size_t n;
    if (!ask_file(1, "Make a project", "Untitled.oadb", "#?.oadb", path, sizeof path)) return;
    n = strlen(path);
    if (n < 5 || strcmp(path + n - 5, ".oadb"))
        if (n + 5 < sizeof path) strcat(path, ".oadb");
    open_project(path, 1);
}

static void do_open_project(void)
{
    char path[512];
    if (!ask_file(0, "Open a project", NULL, "#?.oadb", path, sizeof path)) return;
    open_project(path, 0);
}

/* ---- The auto form (Design-OpenBase.md 6.1): a row a field, OpenGadTools' sections ---- */

enum { FK_STRING = 1, FK_CHECK, FK_TEXT, FK_BUTTON };

static int form_editable(const ob_field *f)
{
    return f->type != OB_TYPE_COUNTER && f->type != OB_TYPE_OBJECT;
}

/* The form's texts from its gadgets (what was typed). */
static void read_form(void)
{
    int k;
    if (!win || !form_list || !cur || !typed) return;
    for (k = 0; k < nform; ++k) {
        int f = form_field[k];
        struct Gadget *g = ogt_sections_gadget(&secs, GID_FORM + k);
        if (f < 0 || !g) continue;
        if (form_kind[k] == FK_STRING) {
            STRPTR s = NULL;
            GT_GetGadgetAttrs(g, win, NULL, GTST_String, (ULONG)&s, TAG_DONE);
            if (s && strcmp((char *)s, typed[f])) {
                free(typed[f]);
                typed[f] = dup_text((char *)s);
            }
        } else if (form_kind[k] == FK_CHECK) {
            ULONG on = 0;
            const ob_formats *fm = ob_project_formats(proj);
            const char *was = loaded ? loaded[f] : "", *now;
            GT_GetGadgetAttrs(g, win, NULL, GTCB_Checked, (ULONG)&on, TAG_DONE);
            /* unticked and empty before: still empty (its default, or nothing) */
            now = on ? fm->yes : (*was || *typed[f]) ? fm->no : "";
            if (strcmp(now, typed[f])) {
                free(typed[f]);
                typed[f] = dup_text(now);
            }
        }
    }
}

/* The form's gadgets taken away; what they hold is not read (drop_form), or read first (free_form). */
static void drop_form(void)
{
    if (!win || !form_list) return;
    RemoveGList(win, form_list, -1);
    FreeGadgets(form_list);
    form_list = NULL;
    nform = 0;
}

static void free_form(void)
{
    if (!win || !form_list) return;
    read_form();
    drop_form();
}

/* The form's gadgets for the record, in the form's box: as many fields as fit (form_left: those that don't). */
static void build_form(void)
{
    struct Gadget *g;
    ogt_row rows[OGT_SEC_ROWS];
    int lh = fh + 6, gap = 4, title_h = fh + 10, sec_gap = 10;
    int i, nrows = 0, nsec = 0, y, col_w, label_w = 0, limit;
    struct RastPort *rp = win->RPort;
    nform = 0;
    form_left = 0;
    if (!cur || form_box.w < 120 || form_box.h < lh * 2) return;
    if (!(g = CreateContext(&form_list))) return;
    SetFont(rp, font);
    for (i = 0; i < cur->nfields; ++i) {
        int w = ogt_text_width(rp, field_label(&cur->fields[i]));
        if (w > label_w) label_w = w;
    }
    col_w = pic_box.w > 0 ? pic_box.x - form_box.x - 24 : form_box.w - 24;
    if (col_w > 560) col_w = 560;
    label_w += 12;
    if (label_w > col_w / 2) label_w = col_w / 2;
    ogt_sections_init(&secs, form_box.x + 12, form_box.y + 6, col_w, label_w, lh, &font_attr, vi);
    if (row >= 0) snprintf(sec_title[0], sizeof sec_title[0], "Record %d of %d", row + 1, nkeys);
    else snprintf(sec_title[0], sizeof sec_title[0], "New record");
    for (i = 1; i < OGT_SEC_MAX; ++i) snprintf(sec_title[i], sizeof sec_title[0], "More of the record");
    y = form_box.y + 6 + title_h;
    limit = form_box.y + form_box.h - 4;
    for (i = 0; i < cur->nfields && nform < FORM_ROWS - 1; ++i) {
        const ob_field *f = &cur->fields[i];
        int need = f->type == OB_TYPE_OBJECT ? 2 : 1, k;
        if (nrows + need > OGT_SEC_ROWS) {
            /* this section is full: the next one under its own title */
            if (nsec + 1 >= OGT_SEC_MAX) break;
            ogt_sections_add(&secs, sec_title[nsec], rows, nrows);
            nsec++;
            nrows = 0;
            y += sec_gap + title_h;
        }
        if (y + need * (lh + gap) > limit) break;
        for (k = 0; k < need; ++k) {
            ogt_row *r = &rows[nrows];
            memset(r, 0, sizeof *r);
            r->id = GID_FORM + nform;
            form_field[nform] = i;
            if (f->type == OB_TYPE_OBJECT && k == 1) {
                r->kind = OGT_ROW_BUTTON;
                r->id = GID_FORMBTN + i;
                r->label = "";
                r->text = "Load Picture...";
                r->disabled = !proj || ob_project_readonly(proj);
                form_field[nform] = -1;
                form_kind[nform] = FK_BUTTON;
            } else if (!form_editable(f) || strlen(typed[i]) >= 120) {
                r->kind = OGT_ROW_TEXT;
                r->label = field_label(f);
                r->text = f->type == OB_TYPE_COUNTER && row < 0 ? "(new)" :
                          f->type == OB_TYPE_OBJECT && !*typed[i] ? "(none)" : typed[i];
                form_kind[nform] = FK_TEXT;
            } else if (f->type == OB_TYPE_YESNO) {
                r->kind = OGT_ROW_CHECKBOX;
                r->label = field_label(f);
                r->value = *typed[i] && !strcmp(typed[i], ob_project_formats(proj)->yes);
                form_kind[nform] = FK_CHECK;
            } else {
                r->kind = OGT_ROW_STRING;
                r->label = field_label(f);
                r->text = typed[i];
                form_kind[nform] = FK_STRING;
            }
            nrows++;
            nform++;
            y += lh + gap;
        }
    }
    form_left = 0;
    for (; i < cur->nfields; ++i) form_left++;
    if (nrows) ogt_sections_add(&secs, sec_title[nsec], rows, nrows);
    if (!ogt_sections_gadgets(&secs, g)) {
        FreeGadgets(form_list);
        form_list = NULL;
        nform = 0;
        set_status("The form's gadgets could not be made.");
        return;
    }
    AddGList(win, form_list, ~0, -1, NULL);
    RefreshGList(form_list, win, NULL, -1);
    GT_RefreshWindow(win, NULL);
}

/* The record's changes written: a new record added (when anything was typed), an old one changed (what
 * changed). OB_OK, or the error's code after saying it. */
static int commit_record(void)
{
    const char **texts;
    long long key;
    int i, any = 0, rc;
    ob_error e;
    if (!proj || !cur || !typed) return OB_OK;
    read_form();
    texts = calloc((size_t)(cur->nfields > 0 ? cur->nfields : 1), sizeof *texts);
    if (!texts) return OB_ERR_NOMEM;
    for (i = 0; i < cur->nfields; ++i) {
        if (!form_editable(&cur->fields[i])) continue;
        if (loaded ? strcmp(typed[i], loaded[i]) != 0 : *typed[i] != 0) {
            texts[i] = typed[i];
            any = 1;
        }
    }
    if (!any) {
        free(texts);
        return OB_OK;
    }
    key = row >= 0 ? keys[row] : 0;
    rc = ob_record_write(proj, cur, &key, texts, &e);
    free(texts);
    if (rc != OB_OK) {
        tell("Save Record", e.text);
        set_status(e.text);
        return rc;
    }
    cache_forget(key);
    if (row < 0) {
        load_keys();
        row = row_of_key(key);
    }
    load_record();
    set_status(row >= 0 && loaded ? "Record saved." : "Saved.");
    return OB_OK;
}

/* ---- Layout ---- */

static void compute_columns(void)
{
    struct RastPort *rp = win->RPort;
    int i, total = 0, avail = sheet_box.w - 18;
    if (!cur) return;
    SetFont(rp, font);
    for (i = 0; i < cur->nfields; ++i) {
        /* as wide as its name, and as what the type holds */
        const char *sample;
        int w = ogt_text_width(rp, field_label(&cur->fields[i])) + 16, min;
        switch (cur->fields[i].type) {
        case OB_TYPE_COUNTER: case OB_TYPE_LOOKUP: sample = "00000"; break;
        case OB_TYPE_YESNO: sample = "Yes"; break;
        case OB_TYPE_INTEGER: sample = "0000000"; break;
        case OB_TYPE_NUMBER: case OB_TYPE_CURRENCY: sample = "1234567.89"; break;
        case OB_TYPE_DATE: sample = "2026-10-25"; break;
        case OB_TYPE_TIME: sample = "12:00:00"; break;
        case OB_TYPE_DATETIME: sample = "2026-10-25 12:00:00"; break;
        case OB_TYPE_OBJECT: sample = "picture-name.png"; break;
        default: sample = "A name of some length"; break;
        }
        min = ogt_text_width(rp, sample) + 16;
        if (w < min) w = min;
        if (w > 240) w = 240;
        colw[i] = w;
        total += w;
    }
    /* room to spare: to the text columns, up to 320 pixels each, then to all of them */
    if (total < avail && cur->nfields) {
        int extra = avail - total, n = 0;
        for (i = 0; i < cur->nfields; ++i)
            if (ob_type_is_text(cur->fields[i].type)) n++;
        for (i = 0; n && i < cur->nfields; ++i)
            if (ob_type_is_text(cur->fields[i].type)) {
                int add = extra / n;
                if (colw[i] + add > 320) add = colw[i] < 320 ? 320 - colw[i] : 0;
                colw[i] += add;
                total += add;
            }
        if (total < avail) {                     /* the rest to all but the key and the yes/no columns */
            int m = 0;
            extra = avail - total;
            for (i = 0; i < cur->nfields; ++i)
                if (cur->fields[i].type != OB_TYPE_COUNTER && cur->fields[i].type != OB_TYPE_YESNO) m++;
            for (i = 0; m && i < cur->nfields; ++i)
                if (cur->fields[i].type != OB_TYPE_COUNTER && cur->fields[i].type != OB_TYPE_YESNO) colw[i] += extra / m;
        }
    }
}

static void free_gadgets(void)
{
    if (!win) return;
    free_form();
    if (chain) { RemoveGList(win, chain, -1); chain = NULL; }
    if (browser) { ogt_list_free(browser); browser = NULL; }
    if (sheet) { ogt_list_free(sheet); sheet = NULL; }
}

static int browser_height(void *user, int index)
{
    (void)user;
    return fh + (index == ntables ? 14 : 8);
}

static void browser_draw(void *user, int index, struct RastPort *rp, int x, int y, int w, int h, int selected)
{
    LONG pen;
    (void)user;
    SetFont(rp, font);
    if (selected) ogt_fill(&ctx, rp, "accent", x, y, w, h);
    pen = ogt_pen(&ctx, selected ? "accent.text" : "text");
    if (index == ntables) {
        ogt_hline(rp, ogt_pen(&ctx, "group.line"), x + 4, y + 2, w - 8);
        ogt_bold(rp, 1);
        ogt_text(rp, pen, x + 8, y + 8, "Relationships", w - 12);
        ogt_bold(rp, 0);
        return;
    }
    if (index >= 0 && index < ntables) ogt_text(rp, pen, x + 8, y + (h - fh) / 2, tables[index], w - 12);
}

static int sheet_height(void *user, int index)
{
    (void)user;
    (void)index;
    return fh + 6;
}

static void sheet_draw(void *user, int index, struct RastPort *rp, int x, int y, int w, int h, int selected)
{
    char **v = row_values(index);
    int i, cx = x + 4;
    LONG pen;
    (void)user;
    SetFont(rp, font);
    if (selected) ogt_fill(&ctx, rp, "accent", x, y, w, h);
    else if (index & 1) ogt_fill(&ctx, rp, "list.alternate", x, y, w, h);
    pen = ogt_pen(&ctx, selected ? "accent.text" : "text");
    if (!v) {
        ogt_text(rp, ogt_pen(&ctx, "muted"), cx, y + 3, "(not read)", w - 8);
        return;
    }
    for (i = 0; cur && i < cur->nfields && cx < x + w; ++i) {
        int cw = colw[i];
        if (cx + cw > x + w) cw = x + w - cx;
        ogt_text(rp, pen, cx, y + 3, v[i], cw - 8);
        cx += colw[i];
    }
}

static void layout(void)
{
    struct RastPort *rp = win->RPort;
    int tbh, bw, body_y, body_h;
    free_gadgets();
    area.x = win->BorderLeft;
    area.y = win->BorderTop;
    area.w = win->Width - win->BorderLeft - win->BorderRight;
    area.h = win->Height - win->BorderTop - win->BorderBottom;
    SetFont(rp, font);
    ogt_toolbar_set(&tb, tools, sizeof tools / sizeof tools[0], area.w < 600 ? OGT_TB_ICONS : tb_style);
    ogt_toolbar_enable(&tb, C_NEWTABLE, proj != NULL);
    ogt_toolbar_enable(&tb, C_RELATIONS, proj != NULL);
    ogt_toolbar_enable(&tb, C_NEWREC, cur != NULL);
    ogt_toolbar_enable(&tb, C_SAVEREC, cur != NULL);
    ogt_toolbar_enable(&tb, C_DELREC, cur != NULL && row >= 0);
    ogt_toolbar_enable(&tb, C_PICTURE, cur != NULL && object_field() >= 0);
    tbh = ogt_toolbar_layout(&tb, rp, area.x + 5, area.y + 3, area.w - 10);
    toolbar_box = (box){ area.x, area.y, area.w, tbh + 7 };
    status_box = (box){ area.x, area.y + area.h - fh - 10, area.w, fh + 10 };
    body_y = toolbar_box.y + toolbar_box.h;
    body_h = status_box.y - body_y;
    bw = area.w >= 640 ? 180 : 120;
    browser_box = (box){ area.x, body_y, bw, body_h };
    main_box = (box){ area.x + bw + 1, body_y, area.w - bw - 1, body_h };
    header_box = sheet_box = form_box = pic_box = (box){ 0, 0, 0, 0 };
    if (cur) {
        int sheet_h = main_box.h * 2 / 5;
        if (sheet_h < (fh + 6) * 4 + fh + 12) sheet_h = (fh + 6) * 4 + fh + 12;
        header_box = (box){ main_box.x, main_box.y, main_box.w, fh + 8 };
        sheet_box = (box){ main_box.x, main_box.y + header_box.h, main_box.w, sheet_h - header_box.h };
        form_box = (box){ main_box.x, main_box.y + sheet_h + 1, main_box.w, main_box.h - sheet_h - 1 };
        /* a picture beside the form, when the table has a Datatype object field and there is room */
        if (object_field() >= 0 && form_box.w >= 520) {
            int pw = form_box.w * 2 / 5;
            pic_box = (box){ form_box.x + form_box.w - pw - 12, form_box.y + 12, pw, form_box.h - 24 };
        }
        compute_columns();
    }
    if ((chain = ogt_toolbar_gadgets(&tb, GID_TOOLBAR)))
        AddGList(win, chain, ~0, -1, NULL);
    if ((browser = ogt_list_new(win, &ctx, GID_BROWSER, browser_height, browser_draw, NULL, NULL, "list"))) {
        ogt_list_layout(browser, browser_box.x + 4, browser_box.y + 4, browser_box.w - 8, browser_box.h - 8);
        ogt_list_set_count(browser, proj ? ntables + 1 : 0);
        ogt_list_select(browser, sel);
    }
    if (cur && sheet_box.h > fh && (sheet = ogt_list_new(win, &ctx, GID_SHEET, sheet_height, sheet_draw, NULL, NULL, "list"))) {
        ogt_list_layout(sheet, sheet_box.x, sheet_box.y, sheet_box.w, sheet_box.h);
        ogt_list_set_count(sheet, nkeys);
        ogt_list_select(sheet, row);
    }
    build_form();
    ogt_status_init(&sbar, win, &ctx, 4);
    ogt_status_layout(&sbar, status_box.x, status_box.y, status_box.w, status_box.h);
}

/* ---- Drawing ---- */

static void draw_status(void)
{
    char s[96], t[96];
    if (!win) return;
    snprintf(s, sizeof s, "%.60s", proj_path[0] ? leaf(proj_path) : "No project");
    if (cur) snprintf(t, sizeof t, "%.40s: %d record%s", cur->name, nkeys, nkeys == 1 ? "" : "s");
    else if (proj && sel == ntables) snprintf(t, sizeof t, "%d relationship%s", nrels, nrels == 1 ? "" : "s");
    else snprintf(t, sizeof t, "%d table%s", ntables, ntables == 1 ? "" : "s");
    ogt_status_set(&sbar, 0, s);
    ogt_status_set(&sbar, 1, proj ? t : "");
    ogt_status_set(&sbar, 2, status);
    ogt_status_set(&sbar, 3, proj && ob_project_readonly(proj) ? "Read only" : "SQLite");
}

static void draw_header(void)
{
    struct RastPort *rp = win->RPort;
    int i, cx = header_box.x + 4;
    LONG pen = ogt_pen(&ctx, "text");
    if (!cur || header_box.w <= 0) return;
    ogt_fill(&ctx, rp, "list.header", header_box.x, header_box.y, header_box.w, header_box.h);
    SetFont(rp, font);
    ogt_bold(rp, 1);
    for (i = 0; i < cur->nfields && cx < header_box.x + header_box.w; ++i) {
        int cw = colw[i];
        if (cx + cw > header_box.x + header_box.w) cw = header_box.x + header_box.w - cx;
        ogt_text(rp, pen, cx, header_box.y + 4, field_label(&cur->fields[i]), cw - 8);
        cx += colw[i];
        if (cx - 2 < header_box.x + header_box.w)
            ogt_vline(rp, ogt_pen(&ctx, "group.line"), cx - 3, header_box.y + 2, header_box.h - 4);
    }
    ogt_bold(rp, 0);
    ogt_hline(rp, ogt_pen(&ctx, "group.line"), header_box.x, header_box.y + header_box.h - 1, header_box.w);
}

/* The picture of the record's Datatype object, beside the form. */
static void draw_picture(void)
{
    struct RastPort *rp = win->RPort;
    int field = object_field();
    const char *say = NULL;
    char msg[160];
    if (pic_box.w <= 0 || field < 0) return;
    ogt_fill(&ctx, rp, "list", pic_box.x, pic_box.y, pic_box.w, pic_box.h);
    ogt_frame(rp, ogt_pen(&ctx, "group.line"), pic_box.x - 1, pic_box.y - 1, pic_box.w + 2, pic_box.h + 2);
    SetFont(rp, font);
    if (row < 0 || !typed || !*typed[field]) {
        snprintf(msg, sizeof msg, "%s: none. Record > Load Picture...", field_label(&cur->fields[field]));
        say = msg;
    } else {
        pic_load(field, pic_box.w - 8, pic_box.h - 8 - fh - 6);
        if (pic.argb && CyberGfxBase) {
            int x = pic_box.x + (pic_box.w - pic.w) / 2, y = pic_box.y + 4;
            WritePixelArray(pic.argb, 0, 0, (UWORD)(pic.w * 4), rp, (UWORD)x, (UWORD)y, (UWORD)pic.w, (UWORD)pic.h,
                            RECTFMT_ARGB);
        } else {
            say = "Not a picture a datatype reads: it is kept, not drawn here.";
        }
        ogt_text(rp, ogt_pen(&ctx, "muted"), pic_box.x + 6, pic_box.y + pic_box.h - fh - 4, typed[field], pic_box.w - 12);
    }
    if (say) ogt_text(rp, ogt_pen(&ctx, "muted"), pic_box.x + 8, pic_box.y + 8, say, pic_box.w - 16);
}

static void draw_form(void)
{
    struct RastPort *rp = win->RPort;
    if (!cur || form_box.w <= 0) return;
    ogt_fill(&ctx, rp, "window", form_box.x, form_box.y, form_box.w, form_box.h);
    if (form_list) {
        RefreshGList(form_list, win, NULL, -1);
        GT_RefreshWindow(win, NULL);
        SetFont(rp, font);
        ogt_sections_draw(&secs, win, &ctx);
    }
    if (form_left) {
        char s[96];
        snprintf(s, sizeof s, "%d more field%s: make the window taller to see %s.", form_left, form_left == 1 ? "" : "s",
                 form_left == 1 ? "it" : "them");
        SetFont(rp, font);
        ogt_text(rp, ogt_pen(&ctx, "muted"), form_box.x + 12, form_box.y + form_box.h - fh - 4, s, form_box.w / 2);
    }
    draw_picture();
}

/* The relationships view: the tables as boxes, a line from each Lookup field to the table it points to. */
static void draw_relations(void)
{
    struct RastPort *rp = win->RPort;
    int i, k, bw = 190, gapx = 80, gapy = 30, cols, lh = fh + 4, maxrows = 8, ybase;
    static int bx[256], by[256], bh[256];
    LONG line = ogt_pen(&ctx, "group.line"), text = ogt_pen(&ctx, "text"), accent = ogt_pen(&ctx, "accent");
    ogt_fill(&ctx, rp, "window", main_box.x, main_box.y, main_box.w, main_box.h);
    SetFont(rp, font);
    if (!ntables) {
        ogt_text(rp, ogt_pen(&ctx, "muted"), main_box.x + 16, main_box.y + 16, "No tables yet.", main_box.w - 32);
        return;
    }
    cols = (main_box.w - 40) / (bw + gapx);
    if (cols < 1) cols = 1;
    ybase = main_box.y + 20;
    for (i = 0; i < ntables && i < 256; ++i) {
        const ob_table *t = rel_tables ? rel_tables[i] : NULL;
        int n = t ? (t->nfields < maxrows ? t->nfields : maxrows + 1) : 0, r;
        bx[i] = main_box.x + 20 + (i % cols) * (bw + gapx);
        by[i] = ybase + (i / cols) * ((maxrows + 2) * lh + gapy);
        bh[i] = (n + 1) * lh + 6;
        ogt_fill(&ctx, rp, "list", bx[i], by[i], bw, bh[i]);
        ogt_fill(&ctx, rp, "list.header", bx[i], by[i], bw, lh + 2);
        ogt_frame(rp, line, bx[i], by[i], bw, bh[i]);
        ogt_bold(rp, 1);
        ogt_text(rp, text, bx[i] + 6, by[i] + 3, tables[i], bw - 12);
        ogt_bold(rp, 0);
        for (r = 0; t && r < n; ++r) {
            const char *name = r < maxrows ? t->fields[r].name : "...";
            ogt_text(rp, r < maxrows && t->fields[r].type == OB_TYPE_LOOKUP ? accent : text,
                     bx[i] + 10, by[i] + lh + 5 + r * lh, name, bw - 16);
        }
    }
    /* the lines: from the field's row to the other table's key (or its title) */
    for (k = 0; k < nrels; ++k) {
        int c = table_index(rels[k].child), p = table_index(rels[k].parent), fr = 0, kr, x1, y1, x2, y2;
        const ob_table *t, *pt;
        if (c < 0 || p < 0 || c >= 256 || p >= 256) continue;
        t = rel_tables ? rel_tables[c] : NULL;
        pt = rel_tables ? rel_tables[p] : NULL;
        for (fr = 0; t && fr < t->nfields && fr < maxrows; ++fr)
            if (!strcmp(t->fields[fr].name, rels[k].child_fields[0])) break;
        for (kr = 0; pt && kr < pt->nfields && kr < maxrows; ++kr)
            if (pt->fields[kr].type == OB_TYPE_COUNTER) break;
        y1 = by[c] + lh + 5 + fr * lh + fh / 2;
        y2 = pt && kr < pt->nfields && kr < maxrows ? by[p] + lh + 5 + kr * lh + fh / 2 : by[p] + lh / 2 + 1;
        if (bx[p] >= bx[c] + bw) { x1 = bx[c] + bw; x2 = bx[p]; }
        else if (bx[p] + bw <= bx[c]) { x1 = bx[c]; x2 = bx[p] + bw; }
        else { x1 = bx[c] + bw; x2 = bx[p] + bw; }
        ogt_set_apen(rp, accent);
        Move(rp, x1, y1);
        if (x1 == x2 || (bx[p] < bx[c] + bw && bx[p] + bw > bx[c])) {
            /* one above the other: out to the right and back */
            Draw(rp, x1 + 16, y1);
            Draw(rp, x1 + 16, y2);
            Draw(rp, x2, y2);
        } else {
            Draw(rp, (x1 + x2) / 2, y1);
            Draw(rp, (x1 + x2) / 2, y2);
            Draw(rp, x2, y2);
        }
        ogt_box(rp, accent, x2 + (x2 > x1 ? 0 : -4), y2 - 2, 4, 5);
    }
    /* the same in words, under the boxes */
    {
        int rowsdown = (ntables + cols - 1) / cols, y = ybase + rowsdown * ((maxrows + 2) * lh + gapy);
        for (k = 0; k < nrels && y + fh < main_box.y + main_box.h; ++k, y += fh + 4) {
            char s[600];
            snprintf(s, sizeof s, "%.60s.%.60s points to %.60s: each %.60s record needs its %.60s record, and a "
                     "%.60s record that %.60s records point to can't be deleted.", rels[k].child,
                     rels[k].child_fields[0], rels[k].parent, rels[k].child, rels[k].parent, rels[k].parent,
                     rels[k].child);
            ogt_text(rp, text, main_box.x + 20, y, s, main_box.w - 40);
        }
    }
}

static void draw_main(void)
{
    struct RastPort *rp = win->RPort;
    if (!proj) {
        ogt_fill(&ctx, rp, "window", main_box.x, main_box.y, main_box.w, main_box.h);
        SetFont(rp, font);
        ogt_text(rp, ogt_pen(&ctx, "muted"), main_box.x + 16, main_box.y + 16,
                 "Project > New Project... makes one; Open Project... opens one.", main_box.w - 32);
        return;
    }
    if (sel == ntables) {
        draw_relations();
        return;
    }
    if (!cur) {
        ogt_fill(&ctx, rp, "window", main_box.x, main_box.y, main_box.w, main_box.h);
        SetFont(rp, font);
        ogt_text(rp, ogt_pen(&ctx, "muted"), main_box.x + 16, main_box.y + 16,
                 ntables ? "Choose a table on the left." : "No tables yet: Table > New Table...", main_box.w - 32);
        return;
    }
    draw_header();
    if (sheet) ogt_list_draw(sheet);
    ogt_hline(rp, ogt_pen(&ctx, "group.line"), form_box.x, form_box.y - 1, form_box.w);
    draw_form();
}

static void draw_all(void)
{
    struct RastPort *rp;
    if (!win) return;
    rp = win->RPort;
    SetFont(rp, font);
    ogt_fill(&ctx, rp, "window", area.x, area.y, area.w, area.h);
    ogt_toolbar_draw(&tb, &ctx, rp, "window");
    ogt_hline(rp, ogt_pen(&ctx, "group.line"), toolbar_box.x, toolbar_box.y + toolbar_box.h - 1, toolbar_box.w);
    if (browser) ogt_list_draw(browser);
    ogt_vline(rp, ogt_pen(&ctx, "group.line"), browser_box.x + browser_box.w, browser_box.y, browser_box.h);
    draw_main();
    draw_status();
    ogt_status_draw(&sbar);                      /* unchanged texts aren't drawn again by ogt_status_set */
}

static void relayout(void)
{
    if (!win) return;
    layout();
    draw_all();
}

/* After the form's record changed: the form again, and the datasheet's selection. */
static void reform(void)
{
    if (!win) return;
    free_form();
    load_record();
    ogt_toolbar_enable(&tb, C_DELREC, cur != NULL && row >= 0);
    build_form();
    if (sheet) {
        ogt_list_set_count(sheet, nkeys);
        ogt_list_select(sheet, row);
        ogt_list_draw(sheet);
    }
    ogt_toolbar_draw(&tb, &ctx, win->RPort, "window");
    draw_form();
    draw_status();
}

/* ---- The table designer (Design-OpenBase.md 4): a table's fields, their types, required or not, and Lookup
 * fields that point to another table, making a relationship that refuses an orphan ---- */

static const char *const type_labels[] = {
    "Text", "Memo", "Whole number", "Number", "Currency", "Date", "Time", "Date and time", "Yes/No",
    "Lookup", "Datatype object", NULL
};
static const ob_type type_values[] = {
    OB_TYPE_TEXT, OB_TYPE_MEMO, OB_TYPE_INTEGER, OB_TYPE_NUMBER, OB_TYPE_CURRENCY, OB_TYPE_DATE, OB_TYPE_TIME,
    OB_TYPE_DATETIME, OB_TYPE_YESNO, OB_TYPE_LOOKUP, OB_TYPE_OBJECT
};
#define NTYPES 11
#define DESIGN_MAX 60

typedef struct {
    char name[OB_NAME_MAX + 1];
    int type;                    /* into type_values */
    int required;
    int parent;                  /* into tables, for a Lookup; -1: none */
    int old;                     /* a field the table has: its index in cur; -1: new */
    struct Node node;
    char line[OB_NAME_MAX + 120];
} dfield;

static int same_name(const char *a, const char *b)
{
    while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) { ++a; ++b; }
    return *a == *b;
}

static int type_index(ob_type t)
{
    int i;
    for (i = 0; i < NTYPES; ++i)
        if (type_values[i] == t) return i;
    return 0;
}

static void dfield_line(dfield *d)
{
    snprintf(d->line, sizeof d->line, "%s: %s%s%s%s", d->name, type_labels[d->type], d->required ? ", required" : "",
             d->parent >= 0 ? ", points to " : "", d->parent >= 0 ? tables[d->parent] : "");
    d->node.ln_Name = d->line;
}

enum { D_NAME = 1, D_LIST, D_FIELD, D_TYPE, D_REQUIRED, D_PARENT, D_ADD, D_REMOVE, D_MAKE, D_CANCEL };

/* existing: change the table shown; else make a new one. 1 when the project changed. */
static int design_table(int existing)
{
    static dfield fields[DESIGN_MAX];
    static const char *parents[OB_TABLES_MAX + 2];
    struct List list;
    struct Gadget *glist = NULL, *g, *gname, *glist_g, *gfield, *gtype, *greq, *gparent;
    struct NewGadget ng;
    struct Window *rw = NULL;
    int nf = 0, i, done = 0, made = 0, picked = -1, cur_type = 0, cur_req = 0, cur_parent = 0, np = 0;
    /* gadgets are placed from the window's corner: inside its borders and title bar */
    int bl = scr->WBorLeft, bt = scr->WBorTop + scr->Font->ta_YSize + 1;
    int ww = 600, wh, lh = fh + 6, x0 = bl + 110, fw = ww - 110 - 16;
    char tname[OB_NAME_MAX + 1] = "";
    ob_error e;
    if (!proj || (existing && !cur)) return 0;
    if (existing) {
        snprintf(tname, sizeof tname, "%s", cur->name);
        for (i = 0; i < cur->nfields && nf < DESIGN_MAX; ++i) {
            const ob_field *f = &cur->fields[i];
            if (f->type == OB_TYPE_COUNTER) continue;
            memset(&fields[nf], 0, sizeof fields[nf]);
            snprintf(fields[nf].name, sizeof fields[nf].name, "%s", f->name);
            fields[nf].type = type_index(f->type);
            fields[nf].required = f->required;
            fields[nf].parent = f->type == OB_TYPE_LOOKUP && f->lookup_table ? table_index(f->lookup_table) : -1;
            fields[nf].old = i;
            nf++;
        }
    }
    /* the tables a Lookup can point to */
    parents[np++] = "(choose a table)";
    for (i = 0; i < ntables && np < OB_TABLES_MAX + 1; ++i) parents[np++] = tables[i];
    parents[np] = NULL;
    NewList(&list);
    for (i = 0; i < nf; ++i) {
        dfield_line(&fields[i]);
        AddTail(&list, &fields[i].node);
    }

    memset(&ng, 0, sizeof ng);
    ng.ng_TextAttr = &font_attr;
    ng.ng_VisualInfo = vi;
    g = CreateContext(&glist);
    ng.ng_LeftEdge = x0; ng.ng_TopEdge = bt + 8; ng.ng_Width = fw; ng.ng_Height = lh;
    ng.ng_GadgetText = (STRPTR)"Table name"; ng.ng_GadgetID = D_NAME; ng.ng_Flags = PLACETEXT_LEFT;
    g = gname = CreateGadget(STRING_KIND, g, &ng, GTST_String, (ULONG)tname, GTST_MaxChars, OB_NAME_MAX,
                             GA_Disabled, existing, TAG_DONE);
    ng.ng_TopEdge += lh + 10; ng.ng_Height = lh * 8; ng.ng_GadgetText = (STRPTR)"Fields"; ng.ng_GadgetID = D_LIST;
    ng.ng_Flags = PLACETEXT_LEFT;
    g = glist_g = CreateGadget(LISTVIEW_KIND, g, &ng, GTLV_Labels, (ULONG)&list, GTLV_ShowSelected, 0, TAG_DONE);
    ng.ng_TopEdge += lh * 8 + 10; ng.ng_Height = lh; ng.ng_GadgetText = (STRPTR)"Field"; ng.ng_GadgetID = D_FIELD;
    g = gfield = CreateGadget(STRING_KIND, g, &ng, GTST_String, (ULONG)"", GTST_MaxChars, OB_NAME_MAX, TAG_DONE);
    ng.ng_TopEdge += lh + 4; ng.ng_Width = 200; ng.ng_GadgetText = (STRPTR)"Type"; ng.ng_GadgetID = D_TYPE;
    g = gtype = CreateGadget(CYCLE_KIND, g, &ng, GTCY_Labels, (ULONG)type_labels, GTCY_Active, 0, TAG_DONE);
    ng.ng_LeftEdge = x0 + 300; ng.ng_Width = 26; ng.ng_GadgetText = (STRPTR)"Required"; ng.ng_GadgetID = D_REQUIRED;
    g = greq = CreateGadget(CHECKBOX_KIND, g, &ng, GTCB_Scaled, TRUE, TAG_DONE);
    ng.ng_LeftEdge = x0; ng.ng_TopEdge += lh + 4; ng.ng_Width = fw; ng.ng_GadgetText = (STRPTR)"Points to";
    ng.ng_GadgetID = D_PARENT;
    g = gparent = CreateGadget(CYCLE_KIND, g, &ng, GTCY_Labels, (ULONG)parents, GTCY_Active, 0, GA_Disabled, TRUE,
                               TAG_DONE);
    ng.ng_TopEdge += lh + 8; ng.ng_Width = 140; ng.ng_Flags = PLACETEXT_IN;
    ng.ng_GadgetText = (STRPTR)"Add Field"; ng.ng_GadgetID = D_ADD;
    g = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
    ng.ng_LeftEdge = x0 + 150; ng.ng_GadgetText = (STRPTR)"Remove Field"; ng.ng_GadgetID = D_REMOVE;
    g = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
    ng.ng_TopEdge += lh + 18; ng.ng_LeftEdge = bl + 16; ng.ng_Width = 160;
    ng.ng_GadgetText = (STRPTR)(existing ? "Change Table" : "Make Table"); ng.ng_GadgetID = D_MAKE;
    g = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
    ng.ng_LeftEdge = bl + ww - 16 - 120; ng.ng_Width = 120; ng.ng_GadgetText = (STRPTR)"Cancel"; ng.ng_GadgetID = D_CANCEL;
    g = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
    wh = ng.ng_TopEdge + lh + 12 - bt;
    if (!g) goto out;
    rw = OpenWindowTags(NULL,
        WA_Title, (ULONG)(existing ? "Design Table" : "New Table"), WA_PubScreen, (ULONG)scr,
        WA_InnerWidth, ww, WA_InnerHeight, wh,
        WA_Left, (scr->Width - ww) / 2, WA_Top, (scr->Height - wh) / 2,
        WA_Gadgets, (ULONG)glist, WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE,
        WA_Activate, TRUE, WA_SimpleRefresh, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | STRINGIDCMP | BUTTONIDCMP | CYCLEIDCMP | LISTVIEWIDCMP |
                  CHECKBOXIDCMP,
        TAG_DONE);
    if (!rw) goto out;
    GT_RefreshWindow(rw, NULL);
    ActivateGadget(existing ? gfield : gname, rw, NULL);
    while (!done) {
        struct IntuiMessage *m;
        Wait(1UL << rw->UserPort->mp_SigBit);
        while (!done && (m = GT_GetIMsg(rw->UserPort))) {
            ULONG cls = m->Class;
            UWORD code = m->Code;
            UWORD id = (cls == IDCMP_GADGETUP || cls == IDCMP_GADGETDOWN) && m->IAddress
                       ? ((struct Gadget *)m->IAddress)->GadgetID : 0;
            GT_ReplyIMsg(m);
            if (cls == IDCMP_CLOSEWINDOW) { done = 1; break; }
            if (cls == IDCMP_REFRESHWINDOW) { GT_BeginRefresh(rw); GT_EndRefresh(rw, TRUE); continue; }
            if (cls != IDCMP_GADGETUP && cls != IDCMP_GADGETDOWN) continue;
            switch (id) {
            case D_TYPE:
                cur_type = code;
                GT_SetGadgetAttrs(gparent, rw, NULL, GA_Disabled, type_values[cur_type] != OB_TYPE_LOOKUP, TAG_DONE);
                break;
            case D_REQUIRED: cur_req = (greq->Flags & GFLG_SELECTED) != 0; break;
            case D_PARENT: cur_parent = code; break;
            case D_LIST: picked = code; break;
            case D_FIELD:                                /* Return in the field's name: as Add Field */
            case D_ADD: {
                STRPTR s = NULL;
                GT_GetGadgetAttrs(gfield, rw, NULL, GTST_String, (ULONG)&s, TAG_DONE);
                if (!s || !*s) {
                    if (id == D_ADD) DisplayBeep(scr);
                    break;
                }
                if (ob_name_check((const char *)s, "A field", &e) != OB_OK) { tell("Add Field", e.text); break; }
                for (i = 0; i < nf; ++i)
                    if (same_name(fields[i].name, (const char *)s)) break;
                if (i < nf || same_name((const char *)s, "id")) { tell("Add Field", "The table has a field of that name."); break; }
                if (type_values[cur_type] == OB_TYPE_LOOKUP && cur_parent <= 0) {
                    tell("Add Field", "A Lookup field points to another table: choose it in Points to.");
                    break;
                }
                if (nf >= DESIGN_MAX) { tell("Add Field", "A table designed here has 60 fields at most."); break; }
                GT_SetGadgetAttrs(glist_g, rw, NULL, GTLV_Labels, ~0UL, TAG_DONE);
                memset(&fields[nf], 0, sizeof fields[nf]);
                snprintf(fields[nf].name, sizeof fields[nf].name, "%s", (const char *)s);
                fields[nf].type = cur_type;
                fields[nf].required = cur_req;
                fields[nf].parent = type_values[cur_type] == OB_TYPE_LOOKUP ? cur_parent - 1 : -1;
                fields[nf].old = -1;
                dfield_line(&fields[nf]);
                AddTail(&list, &fields[nf].node);
                nf++;
                GT_SetGadgetAttrs(glist_g, rw, NULL, GTLV_Labels, (ULONG)&list, TAG_DONE);
                GT_SetGadgetAttrs(gfield, rw, NULL, GTST_String, (ULONG)"", TAG_DONE);
                ActivateGadget(gfield, rw, NULL);
                break;
            }
            case D_REMOVE:
                if (picked < 0 || picked >= nf) { DisplayBeep(scr); break; }
                GT_SetGadgetAttrs(glist_g, rw, NULL, GTLV_Labels, ~0UL, TAG_DONE);
                NewList(&list);
                for (i = picked; i + 1 < nf; ++i) fields[i] = fields[i + 1];
                nf--;
                for (i = 0; i < nf; ++i) {
                    dfield_line(&fields[i]);
                    AddTail(&list, &fields[i].node);
                }
                GT_SetGadgetAttrs(glist_g, rw, NULL, GTLV_Labels, (ULONG)&list, TAG_DONE);
                picked = -1;
                break;
            case D_CANCEL: done = 1; break;
            case D_MAKE: {
                STRPTR s = NULL;
                ob_table *t = NULL;
                int rc = OB_OK;
                GT_GetGadgetAttrs(gname, rw, NULL, GTST_String, (ULONG)&s, TAG_DONE);
                if (!existing) {
                    if (!s || !*s) { tell("New Table", "The table needs a name."); break; }
                    snprintf(tname, sizeof tname, "%s", (const char *)s);
                    t = ob_table_new(tname, 1, &e);
                } else {
                    t = ob_table_copy(cur, &e);
                    /* the fields kept say what they were; those taken out go */
                    if (t) {
                        for (i = t->nfields - 1; i >= 0; --i) {
                            int k, keep = t->fields[i].type == OB_TYPE_COUNTER;
                            for (k = 0; !keep && k < nf; ++k)
                                if (fields[k].old == i) keep = 1;
                            if (!keep) ob_table_remove_field(t, t->fields[i].name, &e);
                            else ob_strset(&t->fields[i].was, t->fields[i].name, &e);
                        }
                    }
                }
                if (!t) { tell("Table", e.text); break; }
                for (i = 0; i < nf && rc == OB_OK; ++i) {
                    ob_field *f;
                    if (fields[i].old >= 0) continue;
                    f = ob_table_add_field(t, fields[i].name, type_values[fields[i].type], &e);
                    if (!f) { rc = e.code ? e.code : OB_ERR_INVALID; break; }
                    f->required = fields[i].required;
                    if (fields[i].parent >= 0) {
                        ob_strset(&f->lookup_table, tables[fields[i].parent], &e);
                        if (!ob_table_add_relation(t, fields[i].name, tables[fields[i].parent], OB_REL_REFUSE, &e))
                            rc = e.code ? e.code : OB_ERR_INVALID;
                    }
                }
                if (rc == OB_OK) {
                    if (existing) {
                        ob_change_result res;
                        rc = ob_table_change(proj, cur->name, t, 0, &res, &e);
                    } else {
                        rc = ob_table_create(proj, t, &e);
                    }
                }
                ob_table_free(t);
                if (rc != OB_OK) { tell(existing ? "Design Table" : "New Table", e.text); break; }
                made = 1;
                done = 1;
                break;
            }
            }
        }
    }
out:
    if (rw) CloseWindow(rw);
    if (glist) FreeGadgets(glist);
    if (win) ActivateWindow(win);
    if (made) {
        load_tables();
        show_object(table_index(tname));
        set_status(existing ? "Table changed." : "Table made: Record > New Record adds its first record.");
        relayout();
    }
    return made;
}

/* ---- Records: new, save, delete, undo; a picture in ---- */

static void pick_row(int index)
{
    if (!cur || index == row) return;
    if (commit_record() != OB_OK) {
        if (sheet) ogt_list_select(sheet, row);
        return;
    }
    row = index;
    reform();
}

static void do_new_record(void)
{
    if (!cur) return;
    if (commit_record() != OB_OK) return;
    row = -1;
    set_status("A new record: type it in, then Save Record (Amiga-S).");
    reform();
    {
        int k;
        for (k = 0; k < nform; ++k)
            if (form_kind[k] == FK_STRING) {
                struct Gadget *g = ogt_sections_gadget(&secs, GID_FORM + k);
                if (g) ActivateGadget(g, win, NULL);
                break;
            }
    }
}

static void do_save_record(void)
{
    if (!cur) return;
    if (commit_record() == OB_OK) reform();
}

static void do_undo_record(void)
{
    if (!cur) return;
    free_form();
    load_record();                               /* the record as it is in the project */
    build_form();
    draw_form();
    set_status("The record's changes are undone.");
    draw_status();
}

static void do_delete_record(void)
{
    ob_error e;
    char msg[200];
    if (!cur || row < 0) return;
    snprintf(msg, sizeof msg, "Delete record %d of %.60s?", row + 1, cur->name);
    if (ask("Delete Record", msg, "Delete|Cancel") != 1) return;
    if (ob_record_delete(proj, cur->name, keys[row], &e) != OB_OK) {
        tell("Delete Record", e.text);
        set_status(e.text);
        draw_status();
        return;
    }
    cache_forget(keys[row]);
    free_texts_of_record();                      /* nothing left of it to save */
    load_keys();
    if (row >= nkeys) row = nkeys - 1;
    set_status("Record deleted.");
    reform();
}

static const char *kind_of(const char *path)
{
    BPTR l = Lock((STRPTR)path, ACCESS_READ);
    struct DataType *dt;
    const char *kind = "file";
    if (!l) return kind;
    if (DataTypesBase && (dt = ObtainDataTypeA(DTST_FILE, (APTR)l, NULL)) != NULL) {
        switch (dt->dtn_Header->dth_GroupID) {
        case GID_PICTURE: kind = "picture"; break;
        case GID_SOUND: kind = "sound"; break;
        case GID_TEXT: kind = "text"; break;
        case GID_ANIMATION: kind = "animation"; break;
        case GID_MOVIE: kind = "movie"; break;
        case GID_DOCUMENT: kind = "document"; break;
        case GID_MUSIC: kind = "music"; break;
        default: break;
        }
        ReleaseDataType(dt);
    }
    UnLock(l);
    return kind;
}

/* A file into the record's Datatype object field (field: -1 the first one). */
static void do_load_picture(int field)
{
    char path[512], msg[300];
    BPTR f;
    struct FileInfoBlock *fib;
    long size = -1, limit;
    void *data;
    ob_error e;
    if (!cur) return;
    if (field < 0) field = object_field();
    if (field < 0) {
        tell("Load Picture", "The table has no Datatype object field: Table > Design Table... adds one.");
        return;
    }
    if (commit_record() != OB_OK) return;
    if (row < 0) {
        tell("Load Picture", "Type the record in and save it first: the picture goes into a saved record.");
        return;
    }
    snprintf(msg, sizeof msg, "A file for %s", field_label(&cur->fields[field]));
    if (!ask_file(0, msg, NULL, NULL, path, sizeof path)) return;
    if ((f = Open((STRPTR)path, MODE_OLDFILE)) != 0) {
        if ((fib = AllocDosObject(DOS_FIB, NULL)) != NULL) {
            if (ExamineFH(f, fib)) size = fib->fib_Size;
            FreeDosObject(DOS_FIB, fib);
        }
    }
    limit = ob_project_config(proj)->max_object_kib;
    if (!f || size < 0) {
        if (f) Close(f);
        tell("Load Picture", "The file can't be read.");
        return;
    }
    if (limit > 0 && size / 1024 > limit) {
        Close(f);
        snprintf(msg, sizeof msg, "That file is %ld KB: a Datatype object may be %ld KB at most.", size / 1024, limit);
        tell("Load Picture", msg);
        return;
    }
    data = malloc((size_t)(size > 0 ? size : 1));
    if (!data || Read(f, data, size) != size) {
        Close(f);
        free(data);
        tell("Load Picture", data ? "The file can't be read." : "Not enough memory for the file.");
        return;
    }
    Close(f);
    if (ob_object_set(proj, cur->name, cur->fields[field].name, keys[row], data, size, leaf(path), kind_of(path), &e)
        != OB_OK) {
        free(data);
        tell("Load Picture", e.text);
        return;
    }
    free(data);
    cache_forget(keys[row]);
    snprintf(msg, sizeof msg, "%.100s kept in the record (%ld bytes).", leaf(path), size);
    set_status(msg);
    reform();
}

static void do_clear_picture(void)
{
    int field = object_field();
    ob_error e;
    if (!cur || row < 0 || field < 0) return;
    if (ob_object_set(proj, cur->name, cur->fields[field].name, keys[row], NULL, 0, NULL, NULL, &e) != OB_OK) {
        tell("Clear Picture", e.text);
        return;
    }
    cache_forget(keys[row]);
    set_status("Picture cleared.");
    reform();
}

/* ---- Commands ---- */

static void about(void)
{
    char text[400];
    snprintf(text, sizeof text,
             VERSION_TEXT "\nThe GFOffice Suite's database.\n\nTables, records, relationships and Datatype objects\n"
             "in one .oadb project (an SQLite file).\n\n%s, SQLite %s.\nMIT, Copyright (c) 2026 AmigaChrome Team.",
             OB_VERSION_TEXT, sqlite3_libversion());
    tell("About " PROGRAM, text);
}

static void do_check(void)
{
    ob_error e;
    if (!proj) return;
    if (ob_project_check(proj, 1, &e) == OB_OK) tell("Check Project", "The project is sound.");
    else tell("Check Project", e.text);
}

static void select_object(int i)
{
    if (!proj || i == sel) return;
    if (!leave_record()) {
        if (browser) ogt_list_select(browser, sel);
        return;
    }
    show_object(i);
    relayout();
}

static void menu_action(ULONG id)
{
    switch (id) {
    case M_NEWPROJ: if (leave_record()) do_new_project(); break;
    case M_OPENPROJ: if (leave_record()) do_open_project(); break;
    case M_CLOSEPROJ:
        if (leave_record()) {
            close_project();
            set_status("Project closed.");
            update_window_title();
            relayout();
        }
        break;
    case M_CHECK: do_check(); break;
    case M_ABOUT: about(); break;
    case M_QUIT: if (leave_record()) quit_now = 1; break;
    case M_NEWTABLE: if (proj && leave_record()) design_table(0); break;
    case M_DESIGN: if (cur && leave_record()) design_table(1); break;
    case M_RELATIONS: select_object(ntables); break;
    case M_NEWREC: do_new_record(); break;
    case M_SAVEREC: do_save_record(); break;
    case M_UNDOREC: do_undo_record(); break;
    case M_DELREC: do_delete_record(); break;
    case M_LOADPIC: do_load_picture(-1); break;
    case M_CLEARPIC: do_clear_picture(); break;
    case M_TB_BOTH: tb_style = OGT_TB_ICONS_TEXT; relayout(); break;
    case M_TB_ICONS: tb_style = OGT_TB_ICONS; relayout(); break;
    case M_TB_TEXT: tb_style = OGT_TB_TEXT; relayout(); break;
    }
}

static void tool_command(int id)
{
    switch (id) {
    case C_OPEN: menu_action(M_OPENPROJ); break;
    case C_NEWTABLE: menu_action(M_NEWTABLE); break;
    case C_RELATIONS: menu_action(M_RELATIONS); break;
    case C_NEWREC: menu_action(M_NEWREC); break;
    case C_SAVEREC: menu_action(M_SAVEREC); break;
    case C_DELREC: menu_action(M_DELREC); break;
    case C_PICTURE: menu_action(M_LOADPIC); break;
    }
}

static void toolbar_event(ULONG class, struct Gadget *g)
{
    int i = ogt_toolbar_index(&tb, g->GadgetID);
    if (i < 0) return;
    if (class == IDCMP_GADGETDOWN) {
        tb.pressed = i;
        ogt_toolbar_draw_one(&tb, &ctx, win->RPort, i, 1, "window");
        return;
    }
    ogt_toolbar_draw_one(&tb, &ctx, win->RPort, i, 0, "window");
    tb.pressed = -1;
    if (!tb.tool[i].disabled) tool_command(tb.tool[i].id);
}

static void events(void)
{
    struct IntuiMessage *im;
    while (win && (im = GT_GetIMsg(win->UserPort))) {
        ULONG class = im->Class, secs_ = im->Seconds, micros = im->Micros;
        UWORD code = im->Code;
        APTR ia = im->IAddress;
        int mx = im->MouseX, my = im->MouseY, index = -1, r, handled = 0;
        struct Gadget *g = (struct Gadget *)ia;
        if (browser) {
            r = ogt_list_event(browser, class, code, ia, mx, my, secs_, micros, &index);
            if (r == OGT_LIST_PICKED || r == OGT_LIST_OPENED) handled = 1;
            else if (r == OGT_LIST_SCROLLED) handled = 2;
            if (handled == 1) {
                GT_ReplyIMsg(im);
                select_object(index);
                continue;
            }
        }
        if (!handled && sheet) {
            r = ogt_list_event(sheet, class, code, ia, mx, my, secs_, micros, &index);
            if (r == OGT_LIST_PICKED || r == OGT_LIST_OPENED) {
                GT_ReplyIMsg(im);
                pick_row(index);
                continue;
            }
            if (r == OGT_LIST_SCROLLED) handled = 2;
        }
        GT_ReplyIMsg(im);
        if (handled) continue;
        switch (class) {
        case IDCMP_CLOSEWINDOW:
            if (leave_record()) quit_now = 1;
            break;
        case IDCMP_NEWSIZE:
            relayout();
            break;
        case IDCMP_REFRESHWINDOW:
            GT_BeginRefresh(win);
            draw_all();
            GT_EndRefresh(win, TRUE);
            break;
        case IDCMP_GADGETDOWN:
        case IDCMP_GADGETUP:
            if (!g) break;
            if (g->GadgetID >= GID_TOOLBAR && g->GadgetID < GID_BROWSER) toolbar_event(class, g);
            else if (class == IDCMP_GADGETUP && cur && g->GadgetID >= GID_FORMBTN &&
                     g->GadgetID < GID_FORMBTN + cur->nfields)
                do_load_picture(g->GadgetID - GID_FORMBTN);
            break;
        case IDCMP_MOUSEBUTTONS:
            if (code == SELECTUP && tb.pressed >= 0) {
                ogt_toolbar_draw_one(&tb, &ctx, win->RPort, tb.pressed, 0, "window");
                tb.pressed = -1;
            }
            break;
        case IDCMP_RAWKEY:
            /* up and down through the records, when no field is being typed in */
            if (sheet && (code == 0x4c || code == 0x4d)) {
                int to = row + (code == 0x4c ? -1 : 1);
                if (to >= 0 && to < nkeys) pick_row(to);
            }
            break;
        case IDCMP_MENUPICK:
            while (code != MENUNULL && win) {
                struct MenuItem *item = ItemAddress(menu, code);
                if (!item) break;
                menu_action((ULONG)GTMENUITEM_USERDATA(item));
                code = item->NextSelect;
            }
            break;
        }
    }
}

/* ---- Opening and closing ---- */

static void free_area(int *l, int *t, int *w, int *h)
{
    char buf[48];
    int a, b, c, d;
    LONG got = GetVar((STRPTR)"OpenDock/Free", (STRPTR)buf, sizeof buf, GVF_GLOBAL_ONLY);
    if (got > 0 && sscanf(buf, "%d %d %d %d", &a, &b, &c, &d) == 4 && c >= 400 && d >= 200 && a >= 0 && b >= 0 &&
        a + c <= scr->Width && b + d <= scr->Height) {
        *l = a;
        *t = b < scr->BarHeight + 1 ? scr->BarHeight + 1 : b;
        *w = c;
        *h = b + d - *t;
        return;
    }
    *l = 0;
    *t = scr->BarHeight + 1;
    *w = scr->Width;
    *h = scr->Height - *t;
}

static int open_window(void)
{
    int l, t, w, h, aw, ah;
    if (!(scr = LockPubScreen(NULL))) return 0;
    if (!(vi = GetVisualInfo(scr, TAG_DONE))) return 0;
    load_theme();
    if (!(font = ogt_open_font(&theme, scr, &font_attr))) return 0;
    fh = font->tf_YSize;
    ogt_ctx_init(&ctx, scr, &theme, theme_mode);
    menu = CreateMenus(menus, TAG_DONE);
    if (menu) LayoutMenus(menu, vi, GTMN_NewLookMenus, TRUE, TAG_DONE);
    free_area(&l, &t, &aw, &ah);
    /* 800 x 600 to start (the Open apps' rule), centred; the zoom gadget: the whole free area */
    w = aw < 800 ? aw : 800;
    h = ah < 600 ? ah : 600;
    zoom_box[0] = (WORD)l;
    zoom_box[1] = (WORD)t;
    zoom_box[2] = (WORD)aw;
    zoom_box[3] = (WORD)ah;
    l += (aw - w) / 2;
    t += (ah - h) / 2;
    win = OpenWindowTags(NULL,
        WA_Title, (ULONG)PROGRAM,
        WA_ScreenTitle, (ULONG)VERSION_TEXT,
        WA_PubScreen, (ULONG)scr,
        WA_Left, l, WA_Top, t, WA_Width, w, WA_Height, h,
        WA_MinWidth, w < 480 ? w : 480, WA_MinHeight, h < 320 ? h : 320, WA_Zoom, (ULONG)zoom_box,
        WA_MaxWidth, ~0, WA_MaxHeight, ~0,
        WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE,
        WA_SizeGadget, TRUE, WA_SizeBBottom, TRUE,
        WA_Activate, TRUE, WA_SmartRefresh, TRUE, WA_ReportMouse, TRUE,
        WA_NewLookMenus, TRUE, WA_AutoAdjust, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_GADGETDOWN | IDCMP_MENUPICK | IDCMP_NEWSIZE |
                  IDCMP_REFRESHWINDOW | IDCMP_VANILLAKEY | IDCMP_RAWKEY | IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE |
                  IDCMP_IDCMPUPDATE,
        TAG_DONE);
    if (!win) return 0;
    if (menu) SetMenuStrip(win, menu);
    update_window_title();
    relayout();
    return 1;
}

static void close_window(void)
{
    if (win) {
        ClearMenuStrip(win);
        free_gadgets();
        CloseWindow(win);
        win = NULL;
    }
    ogt_ctx_free(&ctx);
    if (menu) { FreeMenus(menu); menu = NULL; }
    if (font) { CloseFont(font); font = NULL; }
    if (vi) { FreeVisualInfo(vi); vi = NULL; }
    if (scr) { UnlockPubScreen(NULL, scr); scr = NULL; }
}

static int window_main(int argc, char **argv)
{
    int rc = 0;
    char first[512] = "";
    if (!(GadToolsBase = OpenLibrary((STRPTR)"gadtools.library", 39)) ||
        !(AslBase = OpenLibrary((STRPTR)"asl.library", 38))) {
        if (argc) PutStr((STRPTR)PROGRAM " needs AmigaOS 3.0 or later\n");
        rc = 20;
        goto out;
    }
    OpenFontTextBase = ogt_openfont_open();
    DataTypesBase = OpenLibrary((STRPTR)"datatypes.library", 39);
    CyberGfxBase = OpenLibrary((STRPTR)"cybergraphics.library", 0);
    ogt_iconset_attach();   /* the toolbar's pictures from the Chrome set (Images/Toolbar/) */
    read_theme_choice();
    if (argc > 1) snprintf(first, sizeof first, "%s", argv[1]);
    else if (argc == 0) {
        struct WBStartup *w = (struct WBStartup *)argv;
        if (w->sm_NumArgs > 1 && NameFromLock(w->sm_ArgList[1].wa_Lock, (STRPTR)first, sizeof first))
            AddPart((STRPTR)first, w->sm_ArgList[1].wa_Name, sizeof first);
    }
    if (!open_window()) {
        if (argc) PutStr((STRPTR)PROGRAM ": the window could not open\n");
        rc = 20;
        goto out;
    }
    if (first[0]) open_project(first, 0);
    while (!quit_now) {
        ULONG sig = 1UL << win->UserPort->mp_SigBit;
        ULONG got = Wait(sig | SIGBREAKF_CTRL_C);
        if ((got & SIGBREAKF_CTRL_C) && leave_record()) quit_now = 1;
        if (got & sig) events();
    }
out:
    close_project();
    close_window();
    if (theme.text) ogt_theme_free(&theme);
    if (OpenFontTextBase) CloseLibrary(OpenFontTextBase);
    if (CyberGfxBase) CloseLibrary(CyberGfxBase);
    if (DataTypesBase) CloseLibrary(DataTypesBase);
    if (AslBase) CloseLibrary(AslBase);
    if (GadToolsBase) CloseLibrary(GadToolsBase);
    return rc;
}

int main(int argc, char **argv)
{
    return ob_run_with_stack(131072, window_main, argc, argv);
}
