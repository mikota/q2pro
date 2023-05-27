/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/
// cl_scrn.c -- master for refresh, status bar, console, chat, notify, etc

#include "client.h"
#include "common/math.h"
#include "../refresh/images.h"

#define STAT_PICS       11
#define STAT_MINUS      (STAT_PICS - 1)  // num frame for '-' stats digit

float	r_viewmatrix[16];
typedef struct {
    char name[32];
    size_t name_length;
    qhandle_t image;
} texticon_t;
static struct {
    bool        initialized;        // ready to draw

    qhandle_t   crosshair_pic;
    int         crosshair_width, crosshair_height;
    color_t     crosshair_color;
    int         scope_width, scope_height;

    qhandle_t   pause_pic;
    int         pause_width, pause_height;

    qhandle_t   loading_pic;
    int         loading_width, loading_height;
    bool        draw_loading;

    qhandle_t   sb_pics[2][STAT_PICS];
    qhandle_t   inven_pic;
    qhandle_t   field_pic;

    qhandle_t   backtile_pic;

    qhandle_t   net_pic;
    qhandle_t   font_pic;

	int			hud_x, hud_y;
    int         hud_width, hud_height;
    float       hud_scale;

    texticon_t  texticons[256];
    int         texticon_count;
} scr;

static cvar_t   *scr_viewsize;
static cvar_t   *scr_centertime;
static cvar_t   *scr_showpause;
#if USE_DEBUG
static cvar_t   *scr_showstats;
static cvar_t   *scr_showpmove;
#endif
static cvar_t   *scr_showturtle;

static cvar_t   *scr_draw2d;
static cvar_t   *scr_lag_x;
static cvar_t   *scr_lag_y;
static cvar_t   *scr_lag_draw;
static cvar_t   *scr_lag_min;
static cvar_t   *scr_lag_max;
static cvar_t   *scr_alpha;

static cvar_t   *scr_hudborder_x;
static cvar_t   *scr_hudborder_y;

static cvar_t   *scr_demobar;
static cvar_t   *scr_font;
static cvar_t   *scr_scale;

static cvar_t   *scr_crosshair;

static cvar_t   *scr_chathud;
static cvar_t   *scr_chathud_lines;
static cvar_t   *scr_chathud_time;
static cvar_t   *scr_chathud_x;
static cvar_t   *scr_chathud_y;

static cvar_t   *xhair_dot;
static cvar_t   *xhair_length;
static cvar_t   *xhair_gap;
static cvar_t   *xhair_firing_error;
static cvar_t   *xhair_movement_error;
static cvar_t   *xhair_deployed_weapon_gap;
static cvar_t   *xhair_thickness;
static cvar_t   *xhair_scale;
static cvar_t   *xhair_x;
static cvar_t   *xhair_y;
static cvar_t   *xhair_elasticity;
static cvar_t   *xhair_enabled;

static cvar_t   *scr_draw_icons;

static cvar_t   *r_maxfps;

static cvar_t   *ch_health;
static cvar_t   *ch_red;
static cvar_t   *ch_green;
static cvar_t   *ch_blue;
static cvar_t   *ch_alpha;

static cvar_t   *ch_scale;
static cvar_t   *ch_x;
static cvar_t   *ch_y;

vrect_t     scr_vrect;      // position of render window on screen

static const char *const sb_nums[2][STAT_PICS] = {
    {
        "num_0", "num_1", "num_2", "num_3", "num_4", "num_5",
        "num_6", "num_7", "num_8", "num_9", "num_minus"
    },
    {
        "anum_0", "anum_1", "anum_2", "anum_3", "anum_4", "anum_5",
        "anum_6", "anum_7", "anum_8", "anum_9", "anum_minus"
    }
};

const uint32_t colorTable[8] = {
    U32_BLACK, U32_RED, U32_GREEN, U32_YELLOW,
    U32_BLUE, U32_CYAN, U32_MAGENTA, U32_WHITE
};

/*
===============================================================================

UTILS

===============================================================================
*/

#define SCR_DrawString(x, y, flags, string) \
    SCR_DrawStringEx(x, y, flags, MAX_STRING_CHARS, string, scr.font_pic)

/*
==============
SCR_DrawStringEx
==============
*/
int SCR_DrawStringEx(int x, int y, int flags, size_t maxlen,
                     const char *s, qhandle_t font)
{
    size_t len = strlen(s);

    if (len > maxlen) {
        len = maxlen;
    }

    if ((flags & UI_CENTER) == UI_CENTER) {
        x -= len * CHAR_WIDTH / 2;
    } else if (flags & UI_RIGHT) {
        x -= len * CHAR_WIDTH;
    }

    return R_DrawString(x, y, flags, maxlen, s, font);
}


/*
==============
SCR_DrawStringMulti
==============
*/
void SCR_DrawStringMulti(int x, int y, int flags, size_t maxlen,
                         const char *s, qhandle_t font)
{
    char    *p;
    size_t  len;

    while (*s) {
        p = strchr(s, '\n');
        if (!p) {
            SCR_DrawStringEx(x, y, flags, maxlen, s, font);
            break;
        }

        len = p - s;
        if (len > maxlen) {
            len = maxlen;
        }
        SCR_DrawStringEx(x, y, flags, len, s, font);

        y += CHAR_HEIGHT;
        s = p + 1;
    }
}


/*
=================
SCR_FadeAlpha
=================
*/
float SCR_FadeAlpha(unsigned startTime, unsigned visTime, unsigned fadeTime)
{
    float alpha;
    unsigned timeLeft, delta = cls.realtime - startTime;

    if (delta >= visTime) {
        return 0;
    }

    if (fadeTime > visTime) {
        fadeTime = visTime;
    }

    alpha = 1;
    timeLeft = visTime - delta;
    if (timeLeft < fadeTime) {
        alpha = (float)timeLeft / fadeTime;
    }

    return alpha;
}

bool SCR_ParseColor(const char *s, color_t *color)
{
    int i;
    int c[8];

    // parse generic color
    if (*s == '#') {
        s++;
        for (i = 0; s[i]; i++) {
            if (i == 8) {
                return false;
            }
            c[i] = Q_charhex(s[i]);
            if (c[i] == -1) {
                return false;
            }
        }

        switch (i) {
        case 3:
            color->u8[0] = c[0] | (c[0] << 4);
            color->u8[1] = c[1] | (c[1] << 4);
            color->u8[2] = c[2] | (c[2] << 4);
            color->u8[3] = 255;
            break;
        case 6:
            color->u8[0] = c[1] | (c[0] << 4);
            color->u8[1] = c[3] | (c[2] << 4);
            color->u8[2] = c[5] | (c[4] << 4);
            color->u8[3] = 255;
            break;
        case 8:
            color->u8[0] = c[1] | (c[0] << 4);
            color->u8[1] = c[3] | (c[2] << 4);
            color->u8[2] = c[5] | (c[4] << 4);
            color->u8[3] = c[7] | (c[6] << 4);
            break;
        default:
            return false;
        }

        return true;
    }

    // parse name or index
    i = Com_ParseColor(s);
    if (i >= q_countof(colorTable)) {
        return false;
    }

    color->u32 = colorTable[i];
    return true;
}

/*
===============================================================================

BAR GRAPHS

===============================================================================
*/

static void draw_progress_bar(float progress, bool paused, int framenum)
{
    char buffer[16];
    int x, w, h;
    size_t len;

    w = Q_rint(scr.hud_width * progress);
    h = Q_rint(CHAR_HEIGHT / scr.hud_scale);

    scr.hud_height -= h;

    R_DrawFill8(0, scr.hud_height, w, h, 4);
    R_DrawFill8(w, scr.hud_height, scr.hud_width - w, h, 0);

    R_SetScale(scr.hud_scale);

    w = Q_rint(scr.hud_width * scr.hud_scale);
    h = Q_rint(scr.hud_height * scr.hud_scale);

    len = Q_scnprintf(buffer, sizeof(buffer), "%.f%%", progress * 100);
    x = (w - len * CHAR_WIDTH) / 2;
    R_DrawString(x, h, 0, MAX_STRING_CHARS, buffer, scr.font_pic);

    if (scr_demobar->integer > 1) {
        int sec = framenum / 10;
        int min = sec / 60; sec %= 60;

        Q_scnprintf(buffer, sizeof(buffer), "%d:%02d.%d", min, sec, framenum % 10);
        R_DrawString(0, h, 0, MAX_STRING_CHARS, buffer, scr.font_pic);
    }

    if (paused) {
        SCR_DrawString(w, h, UI_RIGHT, "[PAUSED]");
    }

    R_SetScale(1.0f);
}

static void SCR_DrawDemo(void)
{
#if USE_MVD_CLIENT
    float progress;
    bool paused;
    int framenum;
#endif

    if (!scr_demobar->integer) {
        return;
    }

    if (cls.demo.playback) {
        if (cls.demo.file_size) {
            draw_progress_bar(
                cls.demo.file_progress,
                sv_paused->integer &&
                cl_paused->integer &&
                scr_showpause->integer == 2,
                cls.demo.frames_read);
        }
        return;
    }

#if USE_MVD_CLIENT
    if (sv_running->integer != ss_broadcast) {
        return;
    }

    if (!MVD_GetDemoStatus(&progress, &paused, &framenum)) {
        return;
    }

    if (sv_paused->integer && cl_paused->integer && scr_showpause->integer == 2) {
        paused = true;
    }

    draw_progress_bar(progress, paused, framenum);
#endif
}

/*
===============================================================================

CENTER PRINTING

===============================================================================
*/

static char     scr_centerstring[MAX_STRING_CHARS];
static unsigned scr_centertime_start;   // for slow victory printing
static int      scr_center_lines;

/*
==============
SCR_CenterPrint

Called for important messages that should stay in the center of the screen
for a few moments
==============
*/
void SCR_CenterPrint(const char *str)
{
    const char  *s;

    scr_centertime_start = cls.realtime;
    if (!strcmp(scr_centerstring, str)) {
        return;
    }

    Q_strlcpy(scr_centerstring, str, sizeof(scr_centerstring));

    // count the number of lines for centering
    scr_center_lines = 1;
    s = str;
    while (*s) {
        if (*s == '\n')
            scr_center_lines++;
        s++;
    }

    // echo it to the console
    Com_Printf("%s\n", scr_centerstring);
    Con_ClearNotify_f();
}

static void SCR_DrawCenterString(void)
{
    int y;
    float alpha;

    Cvar_ClampValue(scr_centertime, 0.3f, 10.0f);

    alpha = SCR_FadeAlpha(scr_centertime_start, scr_centertime->value * 1000, 300);
    if (!alpha) {
        return;
    }

    R_SetAlpha(alpha * scr_alpha->value);

    y = scr.hud_y + (scr.hud_height / 4 - scr_center_lines * 8 / 2);

    SCR_DrawStringMulti(scr.hud_x + (scr.hud_width / 2), y, UI_CENTER,
                        MAX_STRING_CHARS, scr_centerstring, scr.font_pic);

    R_SetAlpha(scr_alpha->value);
}

/*
===============================================================================

LAGOMETER

===============================================================================
*/

#define LAG_WIDTH   48
#define LAG_HEIGHT  48

#define LAG_CRIT_BIT    (1U << 31)
#define LAG_WARN_BIT    (1U << 30)

#define LAG_BASE    0xD5
#define LAG_WARN    0xDC
#define LAG_CRIT    0xF2

static struct {
    unsigned samples[LAG_WIDTH];
    unsigned head;
} lag;

void SCR_LagClear(void)
{
    lag.head = 0;
}

void SCR_LagSample(void)
{
    int i = cls.netchan.incoming_acknowledged & CMD_MASK;
    client_history_t *h = &cl.history[i];
    unsigned ping;

    h->rcvd = cls.realtime;
    if (!h->cmdNumber || h->rcvd < h->sent) {
        return;
    }

    ping = h->rcvd - h->sent;
    for (i = 0; i < cls.netchan.dropped; i++) {
        lag.samples[lag.head % LAG_WIDTH] = ping | LAG_CRIT_BIT;
        lag.head++;
    }

    if (cl.frameflags & FF_SUPPRESSED) {
        ping |= LAG_WARN_BIT;
    }
    lag.samples[lag.head % LAG_WIDTH] = ping;
    lag.head++;
}

static void SCR_LagDraw(int x, int y)
{
    int i, j, v, c, v_min, v_max, v_range;

    v_min = Cvar_ClampInteger(scr_lag_min, 0, LAG_HEIGHT * 10);
    v_max = Cvar_ClampInteger(scr_lag_max, 0, LAG_HEIGHT * 10);

    v_range = v_max - v_min;
    if (v_range < 1)
        return;

    for (i = 0; i < LAG_WIDTH; i++) {
        j = lag.head - i - 1;
        if (j < 0) {
            break;
        }

        v = lag.samples[j % LAG_WIDTH];

        if (v & LAG_CRIT_BIT) {
            c = LAG_CRIT;
        } else if (v & LAG_WARN_BIT) {
            c = LAG_WARN;
        } else {
            c = LAG_BASE;
        }

        v &= ~(LAG_WARN_BIT | LAG_CRIT_BIT);
        v = (v - v_min) * LAG_HEIGHT / v_range;
        clamp(v, 0, LAG_HEIGHT);

        R_DrawFill8(x + LAG_WIDTH - i - 1, y + LAG_HEIGHT - v, 1, v, c);
    }
}

static void SCR_DrawNet(void)
{
    int x = scr_lag_x->integer + scr.hud_x;
    int y = scr_lag_y->integer + scr.hud_y;

    if (scr_lag_x->integer < 0) {
        x += scr.hud_width - LAG_WIDTH + 1;
    }
    if (scr_lag_y->integer < 0) {
        y += scr.hud_height - LAG_HEIGHT + 1;
    }

    // draw ping graph
    if (scr_lag_draw->integer) {
        if (scr_lag_draw->integer > 1) {
            R_DrawFill8(x, y, LAG_WIDTH, LAG_HEIGHT, 4);
        }
        SCR_LagDraw(x, y);
    }

    // draw phone jack
    if (cls.netchan.outgoing_sequence - cls.netchan.incoming_acknowledged >= CMD_BACKUP) {
        if ((cls.realtime >> 8) & 3) {
            R_DrawStretchPic(x, y, LAG_WIDTH, LAG_HEIGHT, scr.net_pic);
        }
    }
}


/*
===============================================================================

DRAW OBJECTS

===============================================================================
*/

typedef struct {
    list_t          entry;
    int             x, y;
    cvar_t          *cvar;
    cmd_macro_t     *macro;
    int             flags;
    color_t         color;
} drawobj_t;

#define FOR_EACH_DRAWOBJ(obj) \
    LIST_FOR_EACH(drawobj_t, obj, &scr_objects, entry)
#define FOR_EACH_DRAWOBJ_SAFE(obj, next) \
    LIST_FOR_EACH_SAFE(drawobj_t, obj, next, &scr_objects, entry)

static LIST_DECL(scr_objects);

static void SCR_Color_g(genctx_t *ctx)
{
    int color;

    for (color = 0; color < COLOR_COUNT; color++)
        Prompt_AddMatch(ctx, colorNames[color]);
}

static void SCR_Draw_c(genctx_t *ctx, int argnum)
{
    if (argnum == 1) {
        Cvar_Variable_g(ctx);
        Cmd_Macro_g(ctx);
    } else if (argnum == 4) {
        SCR_Color_g(ctx);
    }
}

// draw cl_fps -1 80
static void SCR_Draw_f(void)
{
    int x, y;
    const char *s, *c;
    drawobj_t *obj;
    cmd_macro_t *macro;
    cvar_t *cvar;
    color_t color;
    int flags;
    int argc = Cmd_Argc();

    if (argc == 1) {
        if (LIST_EMPTY(&scr_objects)) {
            Com_Printf("No draw strings registered.\n");
            return;
        }
        Com_Printf("Name               X    Y\n"
                   "--------------- ---- ----\n");
        FOR_EACH_DRAWOBJ(obj) {
            s = obj->macro ? obj->macro->name : obj->cvar->name;
            Com_Printf("%-15s %4d %4d\n", s, obj->x, obj->y);
        }
        return;
    }

    if (argc < 4) {
        Com_Printf("Usage: %s <name> <x> <y> [color]\n", Cmd_Argv(0));
        return;
    }

    color.u32 = U32_BLACK;
    flags = UI_IGNORECOLOR;

    s = Cmd_Argv(1);
    x = atoi(Cmd_Argv(2));
    y = atoi(Cmd_Argv(3));

    if (x < 0) {
        flags |= UI_RIGHT;
    }

    if (argc > 4) {
        c = Cmd_Argv(4);
        if (!strcmp(c, "alt")) {
            flags |= UI_ALTCOLOR;
        } else if (strcmp(c, "none")) {
            if (!SCR_ParseColor(c, &color)) {
                Com_Printf("Unknown color '%s'\n", c);
                return;
            }
            flags &= ~UI_IGNORECOLOR;
        }
    }

    cvar = NULL;
    macro = Cmd_FindMacro(s);
    if (!macro) {
        cvar = Cvar_WeakGet(s);
    }

    FOR_EACH_DRAWOBJ(obj) {
        if (obj->macro == macro && obj->cvar == cvar) {
            obj->x = x;
            obj->y = y;
            obj->flags = flags;
            obj->color.u32 = color.u32;
            return;
        }
    }

    obj = Z_Malloc(sizeof(*obj));
    obj->x = x;
    obj->y = y;
    obj->cvar = cvar;
    obj->macro = macro;
    obj->flags = flags;
    obj->color.u32 = color.u32;

    List_Append(&scr_objects, &obj->entry);
}

static void SCR_Draw_g(genctx_t *ctx)
{
    drawobj_t *obj;
    const char *s;

    if (LIST_EMPTY(&scr_objects)) {
        return;
    }

    Prompt_AddMatch(ctx, "all");

    FOR_EACH_DRAWOBJ(obj) {
        s = obj->macro ? obj->macro->name : obj->cvar->name;
        Prompt_AddMatch(ctx, s);
    }
}

static void SCR_UnDraw_c(genctx_t *ctx, int argnum)
{
    if (argnum == 1) {
        SCR_Draw_g(ctx);
    }
}

static void SCR_UnDraw_f(void)
{
    char *s;
    drawobj_t *obj, *next;
    cmd_macro_t *macro;
    cvar_t *cvar;

    if (Cmd_Argc() != 2) {
        Com_Printf("Usage: %s <name>\n", Cmd_Argv(0));
        return;
    }

    if (LIST_EMPTY(&scr_objects)) {
        Com_Printf("No draw strings registered.\n");
        return;
    }

    s = Cmd_Argv(1);
    if (!strcmp(s, "all")) {
        FOR_EACH_DRAWOBJ_SAFE(obj, next) {
            Z_Free(obj);
        }
        List_Init(&scr_objects);
        Com_Printf("Deleted all draw strings.\n");
        return;
    }

    cvar = NULL;
    macro = Cmd_FindMacro(s);
    if (!macro) {
        cvar = Cvar_WeakGet(s);
    }

    FOR_EACH_DRAWOBJ_SAFE(obj, next) {
        if (obj->macro == macro && obj->cvar == cvar) {
            List_Remove(&obj->entry);
            Z_Free(obj);
            return;
        }
    }

    Com_Printf("Draw string '%s' not found.\n", s);
}

static void SCR_DrawObjects(void)
{
    char buffer[MAX_QPATH];
    int x, y;
    drawobj_t *obj;

    FOR_EACH_DRAWOBJ(obj) {
        x = obj->x + scr.hud_x;
        y = obj->y + scr.hud_y;
        if (obj->x < 0) {
            x += scr.hud_width + 1;
        }
        if (obj->y < 0) {
            y += scr.hud_height - CHAR_HEIGHT + 1;
        }
        if (!(obj->flags & UI_IGNORECOLOR)) {
            R_SetColor(obj->color.u32);
        }
        if (obj->macro) {
            obj->macro->function(buffer, sizeof(buffer));
            SCR_DrawString(x, y, obj->flags, buffer);
        } else {
            SCR_DrawString(x, y, obj->flags, obj->cvar->string);
        }
        if (!(obj->flags & UI_IGNORECOLOR)) {
            R_ClearColor();
            R_SetAlpha(scr_alpha->value);
        }
    }
}

/*
===============================================================================

CHAT HUD

===============================================================================
*/

#define MAX_CHAT_TEXT       150
#define MAX_CHAT_LINES      32
#define CHAT_LINE_MASK      (MAX_CHAT_LINES - 1)

typedef struct {
    char        text[MAX_CHAT_TEXT];
    unsigned    time;
} chatline_t;

static chatline_t   scr_chatlines[MAX_CHAT_LINES];
static unsigned     scr_chathead;

void SCR_ClearChatHUD_f(void)
{
    memset(scr_chatlines, 0, sizeof(scr_chatlines));
    scr_chathead = 0;
}

void SCR_AddToChatHUD(const char *text)
{
    chatline_t *line;
    char *p;

    line = &scr_chatlines[scr_chathead++ & CHAT_LINE_MASK];
    Q_strlcpy(line->text, text, sizeof(line->text));
    line->time = cls.realtime;

    p = strrchr(line->text, '\n');
    if (p)
        *p = 0;
}
static void Icons_Init(void) {
    char** list;
    int num = 0;
    if (!(list = (char**)FS_ListFiles(
        NULL, "pics/icons/*.png", FS_SEARCH_BYFILTER | FS_SEARCH_STRIPEXT, &num))) {
     //   return;
    }
    scr.texticon_count = 0;
    for (int i = 0; i < num; i++) {
        char* name = list[i];
        //name will be in format "name", need to make it ":name:"
        scr.texticons[i].name[0] = ':';
        strcpy(scr.texticons[i].name + 1, name);
        scr.texticons[i].name[strlen(name) + 1] = ':';
        scr.texticons[i].name[strlen(name) + 2] = 0;
        scr.texticons[i].name_length = strlen(name)+2;
        char name_with_path[256];
        strcpy(name_with_path, "icons/");
        strcat(name_with_path, name);
        scr.texticons[i].image = R_RegisterPic(name);
        scr.texticon_count++;
    }
    //print out all the icons in console
    for (int i = 0; i < scr.texticon_count; i++) {
        Com_Printf("%s\n", scr.texticons[i].name);
    }
}
static int* Icons_ParseString(const char* s) {
    int* result = (int*)malloc((strlen(s) + 1)*sizeof(int));
    int* p = result;
    //Icons_Init();
    while (*s) {
        int icon_index;
        int icon_found = 0;
        for (icon_index = 0; icon_index < scr.texticon_count; icon_index++) {
            texticon_t* icon = &scr.texticons[icon_index];
            if (strncmp(s, icon->name, icon->name_length) == 0) {
                s += icon->name_length;
                *p++ = 256+icon_index;
                icon_found = 1;
                break;
            }
        }
        if (!icon_found) {
            *p++ = *s++;
        }
    }
    *p = 0;
    return result;
}
static void SCR_DrawChatLine(int x, int y, int flags, const char* text) {
    if (scr_draw_icons->integer) {
        image_t* font_image = IMG_ForHandle(scr.font_pic);
        int* parsed = Icons_ParseString(text);
        int* s = parsed;
        
        while (*s) {
            if (*s < 256) {
                byte c = *s++;
                R_DrawChar(x,y,flags,c,scr.font_pic);
                x += CHAR_WIDTH;
            } else {
                int icon_index = *s++ - 256;
                texticon_t* icon = &scr.texticons[icon_index];
         //        image_t* icon_image = IMG_ForHandle(icon->image);
         //       int height = CHAR_HEIGHT;
         //      int width = icon_image->aspect*CHAR_WIDTH;
                R_DrawStretchPic(x, y, 32, 16,icon->image);

                x += 32;
            }
            
           
        }
        free(parsed);
        
    } else {
        SCR_DrawString(x, y, flags, text);
    }
}
static void SCR_DrawChatHUD(void)
{

    int x, y, i, lines, flags, step;
    float alpha;
    chatline_t *line;

    if (scr_chathud->integer == 0)
        return;

    x = scr_chathud_x->integer + scr.hud_x;
    y = scr_chathud_y->integer + scr.hud_y;

    if (scr_chathud->integer == 2)
        flags = UI_ALTCOLOR;
    else
        flags = 0;

    if (scr_chathud_x->integer < 0) {
        x += scr.hud_width + 1;
        flags |= UI_RIGHT;
    } else {
        flags |= UI_LEFT;
    }

    if (scr_chathud_y->integer < 0) {
        y += scr.hud_height - CHAR_HEIGHT + 1;
        step = -CHAR_HEIGHT;
    } else {
        step = CHAR_HEIGHT;
    }

    lines = scr_chathud_lines->integer;
    if (lines > scr_chathead)
        lines = scr_chathead;

    for (i = 0; i < lines; i++) {
        line = &scr_chatlines[(scr_chathead - i - 1) & CHAT_LINE_MASK];

        if (scr_chathud_time->integer) {
            alpha = SCR_FadeAlpha(line->time, scr_chathud_time->integer, 1000);
            if (!alpha)
                break;

            R_SetAlpha(alpha * scr_alpha->value);
            SCR_DrawChatLine(x, y, flags, line->text);
            R_SetAlpha(scr_alpha->value);
        } else {
            SCR_DrawChatLine(x, y, flags, line->text);
        }

        y += step;
    }
}

/*
===============================================================================

DEBUG STUFF

===============================================================================
*/

static void SCR_DrawTurtle(void)
{
    int x, y;

    if (scr_showturtle->integer <= 0)
        return;

    if (!cl.frameflags)
        return;

    x = CHAR_WIDTH;
    y = scr.hud_height - 11 * CHAR_HEIGHT;

#define DF(f) \
    if (cl.frameflags & FF_##f) { \
        SCR_DrawString(x, y, UI_ALTCOLOR, #f); \
        y += CHAR_HEIGHT; \
    }

    if (scr_showturtle->integer > 1) {
        DF(SUPPRESSED)
    }
    DF(CLIENTPRED)
    if (scr_showturtle->integer > 1) {
        DF(CLIENTDROP)
        DF(SERVERDROP)
    }
    DF(BADFRAME)
    DF(OLDFRAME)
    DF(OLDENT)
    DF(NODELTA)

#undef DF
}

#if USE_DEBUG

static void SCR_DrawDebugStats(void)
{
    char buffer[MAX_QPATH];
    int i, j;
    int x, y;

    j = scr_showstats->integer;
    if (j <= 0)
        return;

    if (j > MAX_STATS)
        j = MAX_STATS;

    x = CHAR_WIDTH;
    y = (scr.hud_height - j * CHAR_HEIGHT) / 2;
    for (i = 0; i < j; i++) {
        Q_snprintf(buffer, sizeof(buffer), "%2d: %d", i, cl.frame.ps.stats[i]);
        if (cl.oldframe.ps.stats[i] != cl.frame.ps.stats[i]) {
            R_SetColor(U32_RED);
        }
        R_DrawString(x, y, 0, MAX_STRING_CHARS, buffer, scr.font_pic);
        R_ClearColor();
        y += CHAR_HEIGHT;
    }
}

static void SCR_DrawDebugPmove(void)
{
    static const char * const types[] = {
        "NORMAL", "SPECTATOR", "DEAD", "GIB", "FREEZE"
    };
    static const char * const flags[] = {
        "DUCKED", "JUMP_HELD", "ON_GROUND",
        "TIME_WATERJUMP", "TIME_LAND", "TIME_TELEPORT",
        "NO_PREDICTION", "TELEPORT_BIT"
    };
    unsigned i, j;
    int x, y;

    if (!scr_showpmove->integer)
        return;

    x = CHAR_WIDTH;
    y = (scr.hud_height - 2 * CHAR_HEIGHT) / 2;

    i = cl.frame.ps.pmove.pm_type;
    if (i > PM_FREEZE)
        i = PM_FREEZE;

    R_DrawString(x, y, 0, MAX_STRING_CHARS, types[i], scr.font_pic);
    y += CHAR_HEIGHT;

    j = cl.frame.ps.pmove.pm_flags;
    for (i = 0; i < 8; i++) {
        if (j & (1 << i)) {
            x = R_DrawString(x, y, 0, MAX_STRING_CHARS, flags[i], scr.font_pic);
            x += CHAR_WIDTH;
        }
    }
}

#endif

//============================================================================

// Sets scr_vrect, the coordinates of the rendered window
static void SCR_CalcVrect(void)
{
    int     size;

    // bound viewsize
    size = Cvar_ClampInteger(scr_viewsize, 40, 100);

    scr_vrect.width = scr.hud_width * size / 100;
    scr_vrect.height = scr.hud_height * size / 100;

    scr_vrect.x = (scr.hud_width - scr_vrect.width) / 2;
    scr_vrect.y = (scr.hud_height - scr_vrect.height) / 2;
}

/*
=================
SCR_SizeUp_f

Keybinding command
=================
*/
static void SCR_SizeUp_f(void)
{
    Cvar_SetInteger(scr_viewsize, scr_viewsize->integer + 10, FROM_CONSOLE);
}

/*
=================
SCR_SizeDown_f

Keybinding command
=================
*/
static void SCR_SizeDown_f(void)
{
    Cvar_SetInteger(scr_viewsize, scr_viewsize->integer - 10, FROM_CONSOLE);
}

/*
=================
SCR_Sky_f

Set a specific sky and rotation speed. If empty sky name is provided, falls
back to server defaults.
=================
*/
static void SCR_Sky_f(void)
{
    char    *name;
    float   rotate;
    vec3_t  axis;
    int     argc = Cmd_Argc();

    if (argc < 2) {
        Com_Printf("Usage: sky <basename> [rotate] [axis x y z]\n");
        return;
    }

    if (cls.state != ca_active) {
        Com_Printf("No map loaded.\n");
        return;
    }

    name = Cmd_Argv(1);
    if (!*name) {
        CL_SetSky();
        return;
    }

    if (argc > 2)
        rotate = atof(Cmd_Argv(2));
    else
        rotate = 0;

    if (argc == 6) {
        axis[0] = atof(Cmd_Argv(3));
        axis[1] = atof(Cmd_Argv(4));
        axis[2] = atof(Cmd_Argv(5));
    } else
        VectorSet(axis, 0, 0, 1);

    R_SetSky(name, rotate, axis);
}

/*
================
SCR_TimeRefresh_f
================
*/
static void SCR_TimeRefresh_f(void)
{
    int     i;
    unsigned    start, stop;
    float       time;

    if (cls.state != ca_active) {
        Com_Printf("No map loaded.\n");
        return;
    }

    start = Sys_Milliseconds();

    if (Cmd_Argc() == 2) {
        // run without page flipping
        R_BeginFrame();
        for (i = 0; i < 128; i++) {
            cl.refdef.viewangles[1] = i / 128.0f * 360.0f;
            R_RenderFrame(&cl.refdef);
        }
        R_EndFrame();
    } else {
        for (i = 0; i < 128; i++) {
            cl.refdef.viewangles[1] = i / 128.0f * 360.0f;

            R_BeginFrame();
            R_RenderFrame(&cl.refdef);
            R_EndFrame();
        }
    }

    stop = Sys_Milliseconds();
    time = (stop - start) * 0.001f;
    Com_Printf("%f seconds (%f fps)\n", time, 128.0f / time);
}


//============================================================================

static void scr_crosshair_changed(cvar_t *self)
{
    char buffer[16];
    int w, h;
    float scale;
    qhandle_t scope_pic;

    if (scr_crosshair->integer > 0) {
        Q_snprintf(buffer, sizeof(buffer), "ch%i", scr_crosshair->integer);
        scr.crosshair_pic = R_RegisterPic(buffer);
        R_GetPicSize(&w, &h, scr.crosshair_pic);

        // prescale
        scale = Cvar_ClampValue(ch_scale, 0.1f, 9.0f) * scr.hud_scale;
        scr.crosshair_width = w * scale;
        scr.crosshair_height = h * scale;
        if (scr.crosshair_width < 1)
            scr.crosshair_width = 1;
        if (scr.crosshair_height < 1)
            scr.crosshair_height = 1;

        // action mod scope scaling
        scope_pic = R_RegisterPic("scope2x");;
        if (scope_pic) {
            R_GetPicSize(&w, &h, scope_pic);
            scr.scope_width = w * scale;
            scr.scope_height = h * scale;
            if (scr.scope_width < 1)
                scr.scope_width = 1;
            if (scr.scope_height < 1)
                scr.scope_height = 1;
        }

        if (ch_health->integer) {
            SCR_SetCrosshairColor();
        } else {
            scr.crosshair_color.u8[0] = Cvar_ClampValue(ch_red, 0, 1) * 255;
            scr.crosshair_color.u8[1] = Cvar_ClampValue(ch_green, 0, 1) * 255;
            scr.crosshair_color.u8[2] = Cvar_ClampValue(ch_blue, 0, 1) * 255;
        }
        scr.crosshair_color.u8[3] = Cvar_ClampValue(ch_alpha, 0, 1) * 255;
    } else {
        scr.crosshair_pic = 0;
    }
}

void SCR_SetCrosshairColor(void)
{
    int health;

    if (!ch_health->integer) {
        return;
    }

    health = cl.frame.ps.stats[STAT_HEALTH];
    if (health <= 0) {
        VectorSet(scr.crosshair_color.u8, 0, 0, 0);
        return;
    }

    // red
    scr.crosshair_color.u8[0] = 255;

    // green
    if (health >= 66) {
        scr.crosshair_color.u8[1] = 255;
    } else if (health < 33) {
        scr.crosshair_color.u8[1] = 0;
    } else {
        scr.crosshair_color.u8[1] = (255 * (health - 33)) / 33;
    }

    // blue
    if (health >= 99) {
        scr.crosshair_color.u8[2] = 255;
    } else if (health < 66) {
        scr.crosshair_color.u8[2] = 0;
    } else {
        scr.crosshair_color.u8[2] = (255 * (health - 66)) / 33;
    }
}

void SCR_ModeChanged(void)
{
    IN_Activate();
    Con_CheckResize();
    UI_ModeChanged();
    cls.disable_screen = 0;
    if (scr.initialized)
        scr.hud_scale = R_ClampScale(scr_scale);
}

/*
==================
SCR_RegisterMedia
==================
*/
void SCR_RegisterMedia(void)
{
    int     i, j;

    for (i = 0; i < 2; i++)
        for (j = 0; j < STAT_PICS; j++)
            scr.sb_pics[i][j] = R_RegisterPic(sb_nums[i][j]);

    scr.inven_pic = R_RegisterPic("inventory");
    scr.field_pic = R_RegisterPic("field_3");

    scr.backtile_pic = R_RegisterImage("backtile", IT_PIC, IF_PERMANENT | IF_REPEAT);

    scr.pause_pic = R_RegisterPic("pause");
    R_GetPicSize(&scr.pause_width, &scr.pause_height, scr.pause_pic);

    scr.loading_pic = R_RegisterPic("loading");
    R_GetPicSize(&scr.loading_width, &scr.loading_height, scr.loading_pic);

    scr.net_pic = R_RegisterPic("net");
    scr.font_pic = R_RegisterFont(scr_font->string);

    scr_crosshair_changed(scr_crosshair);
}

static void scr_font_changed(cvar_t *self)
{
    scr.font_pic = R_RegisterFont(self->string);
}

static void scr_scale_changed(cvar_t *self)
{
    scr.hud_scale = R_ClampScale(self);

    scr_crosshair_changed(scr_crosshair);
}

static const cmdreg_t scr_cmds[] = {
    { "timerefresh", SCR_TimeRefresh_f },
    { "sizeup", SCR_SizeUp_f },
    { "sizedown", SCR_SizeDown_f },
    { "sky", SCR_Sky_f },
    { "draw", SCR_Draw_f, SCR_Draw_c },
    { "undraw", SCR_UnDraw_f, SCR_UnDraw_c },
    { "clearchathud", SCR_ClearChatHUD_f },
    { NULL }
};

/*
==================
SCR_Init
==================
*/
void SCR_Init(void)
{
    scr_viewsize = Cvar_Get("viewsize", "100", CVAR_ARCHIVE);
    scr_showpause = Cvar_Get("scr_showpause", "1", 0);
    scr_centertime = Cvar_Get("scr_centertime", "2.5", 0);
    scr_demobar = Cvar_Get("scr_demobar", "1", 0);
    scr_font = Cvar_Get("scr_font", "conchars", 0);
    scr_font->changed = scr_font_changed;
    scr_scale = Cvar_Get("scr_scale", "0", 0);
    scr_scale->changed = scr_scale_changed;
    scr_crosshair = Cvar_Get("crosshair", "0", CVAR_ARCHIVE);
    scr_crosshair->changed = scr_crosshair_changed;

    scr_chathud = Cvar_Get("scr_chathud", "0", 0);
    scr_chathud_lines = Cvar_Get("scr_chathud_lines", "4", 0);
    scr_chathud_time = Cvar_Get("scr_chathud_time", "0", 0);
    scr_chathud_time->changed = cl_timeout_changed;
    scr_chathud_time->changed(scr_chathud_time);
    scr_chathud_x = Cvar_Get("scr_chathud_x", "8", 0);
    scr_chathud_y = Cvar_Get("scr_chathud_y", "-64", 0);

    xhair_dot = Cvar_Get("xhair_dot", "1",0);
    xhair_length = Cvar_Get("xhair_length","4",0);
    xhair_gap = Cvar_Get("xhair_gap", "10",0);
    xhair_firing_error = Cvar_Get("xhair_firing_error","1",0);
    xhair_movement_error = Cvar_Get("xhair_movement_error","1",0);
    xhair_deployed_weapon_gap = Cvar_Get("xhair_deployed_weapon_gap","1",0);
    xhair_thickness = Cvar_Get("xhair_thickness","1",0);
    xhair_scale = Cvar_Get("xhair_scale","1",0);
    xhair_x = Cvar_Get("xhair_x","0",0);
    xhair_y = Cvar_Get("xhair_y","0",0);
    xhair_elasticity = Cvar_Get("xhair_elasticity","1",0);
    xhair_enabled = Cvar_Get("xhair_enabled","0",0);

    scr_draw_icons = Cvar_Get("scr_draw_icons", "1", 0);

    r_maxfps = Cvar_Get("r_maxfps","0",0);

    ch_health = Cvar_Get("ch_health", "0", 0);
    ch_health->changed = scr_crosshair_changed;
    ch_red = Cvar_Get("ch_red", "1", 0);
    ch_red->changed = scr_crosshair_changed;
    ch_green = Cvar_Get("ch_green", "1", 0);
    ch_green->changed = scr_crosshair_changed;
    ch_blue = Cvar_Get("ch_blue", "1", 0);
    ch_blue->changed = scr_crosshair_changed;
    ch_alpha = Cvar_Get("ch_alpha", "1", 0);
    ch_alpha->changed = scr_crosshair_changed;

    ch_scale = Cvar_Get("ch_scale", "1", 0);
    ch_scale->changed = scr_crosshair_changed;
    ch_x = Cvar_Get("ch_x", "0", 0);
    ch_y = Cvar_Get("ch_y", "0", 0);

    scr_draw2d = Cvar_Get("scr_draw2d", "2", 0);
    scr_showturtle = Cvar_Get("scr_showturtle", "1", 0);
    scr_lag_x = Cvar_Get("scr_lag_x", "-1", 0);
    scr_lag_y = Cvar_Get("scr_lag_y", "-1", 0);
    scr_lag_draw = Cvar_Get("scr_lag_draw", "0", 0);
    scr_lag_min = Cvar_Get("scr_lag_min", "0", 0);
    scr_lag_max = Cvar_Get("scr_lag_max", "200", 0);
    scr_alpha = Cvar_Get("scr_alpha", "1", 0);

	scr_hudborder_x = Cvar_Get("scr_hudborder_x", "0", 0);
	scr_hudborder_y = Cvar_Get("scr_hudborder_y", "0", 0);
#if USE_DEBUG
    scr_showstats = Cvar_Get("scr_showstats", "0", 0);
    scr_showpmove = Cvar_Get("scr_showpmove", "0", 0);
#endif

    Cmd_Register(scr_cmds);

    scr_scale_changed(scr_scale);
    scr_crosshair_changed(scr_crosshair);

    Icons_Init();

    scr.initialized = true;
}

void SCR_Shutdown(void)
{
    Cmd_Deregister(scr_cmds);
    scr.initialized = false;
}

//=============================================================================

/*
================
SCR_BeginLoadingPlaque
================
*/
void SCR_BeginLoadingPlaque(void)
{
    if (!cls.state) {
        return;
    }

    S_StopAllSounds();
    OGG_Stop();

    if (cls.disable_screen) {
        return;
    }

#if USE_DEBUG
    if (developer->integer) {
        return;
    }
#endif

    // if at console or menu, don't bring up the plaque
    if (cls.key_dest & (KEY_CONSOLE | KEY_MENU)) {
        return;
    }

    scr.draw_loading = true;
    SCR_UpdateScreen();

    cls.disable_screen = Sys_Milliseconds();
}

/*
================
SCR_EndLoadingPlaque
================
*/
void SCR_EndLoadingPlaque(void)
{
    if (!cls.state) {
        return;
    }
    cls.disable_screen = 0;
    Con_ClearNotify_f();
}

// Clear any parts of the tiled background that were drawn on last frame
static void SCR_TileClear(void)
{
    int top, bottom, left, right;

    //if (con.currentHeight == 1)
    //  return;     // full screen console

    if (scr_viewsize->integer == 100)
        return;     // full screen rendering

    top = scr_vrect.y;
    bottom = top + scr_vrect.height;
    left = scr_vrect.x;
    right = left + scr_vrect.width;

    // clear above view screen
    R_TileClear(0, 0, scr.hud_width, top, scr.backtile_pic);

    // clear below view screen
    R_TileClear(0, bottom, scr.hud_width,
                scr.hud_height - bottom, scr.backtile_pic);

    // clear left of view screen
    R_TileClear(0, top, left, scr_vrect.height, scr.backtile_pic);

    // clear right of view screen
    R_TileClear(right, top, scr.hud_width - right,
                scr_vrect.height, scr.backtile_pic);
}

/*
===============================================================================

STAT PROGRAMS

===============================================================================
*/

#define ICON_WIDTH  24
#define ICON_HEIGHT 24
#define DIGIT_WIDTH 16
#define ICON_SPACE  8

#define HUD_DrawString(x, y, string) \
    R_DrawString(x, y, 0, MAX_STRING_CHARS, string, scr.font_pic)

#define HUD_DrawAltString(x, y, string) \
    R_DrawString(x, y, UI_XORCOLOR, MAX_STRING_CHARS, string, scr.font_pic)

#define HUD_DrawCenterString(x, y, string) \
    SCR_DrawStringMulti(x, y, UI_CENTER, MAX_STRING_CHARS, string, scr.font_pic)

#define HUD_DrawAltCenterString(x, y, string) \
    SCR_DrawStringMulti(x, y, UI_CENTER | UI_XORCOLOR, MAX_STRING_CHARS, string, scr.font_pic)

static void HUD_DrawNumber(int x, int y, int color, int width, int value)
{
    char    num[16], *ptr;
    int     l;
    int     frame;

    if (width < 1)
        return;

    // draw number string
    if (width > 5)
        width = 5;

    color &= 1;

    l = Q_scnprintf(num, sizeof(num), "%i", value);
    if (l > width)
        l = width;
    x += 2 + DIGIT_WIDTH * (width - l);

    ptr = num;
    while (*ptr && l) {
        if (*ptr == '-')
            frame = STAT_MINUS;
        else
            frame = *ptr - '0';

        R_DrawPic(x, y, scr.sb_pics[color][frame]);
        x += DIGIT_WIDTH;
        ptr++;
        l--;
    }
}

#define DISPLAY_ITEMS   17

static void SCR_DrawInventory(void)
{
    int     i;
    int     num, selected_num, item;
    int     index[MAX_ITEMS];
    char    string[MAX_STRING_CHARS];
    int     x, y;
    const char  *bind;
    int     selected;
    int     top;

    if (!(cl.frame.ps.stats[STAT_LAYOUTS] & 2))
        return;

    selected = cl.frame.ps.stats[STAT_SELECTED_ITEM];

    num = 0;
    selected_num = 0;
    for (i = 0; i < MAX_ITEMS; i++) {
        if (i == selected) {
            selected_num = num;
        }
        if (cl.inventory[i]) {
            index[num++] = i;
        }
    }

    // determine scroll point
    top = selected_num - DISPLAY_ITEMS / 2;
    if (top > num - DISPLAY_ITEMS) {
        top = num - DISPLAY_ITEMS;
    }
    if (top < 0) {
        top = 0;
    }

    x = scr.hud_x + ((scr.hud_width - 256) / 2);
    y = scr.hud_y + ((scr.hud_height - 240) / 2);

    R_DrawPic(x, y + 8, scr.inven_pic);
    y += 24;
    x += 24;

    HUD_DrawString(x, y, "hotkey ### item");
    y += CHAR_HEIGHT;

    HUD_DrawString(x, y, "------ --- ----");
    y += CHAR_HEIGHT;

    for (i = top; i < num && i < top + DISPLAY_ITEMS; i++) {
        item = index[i];
        // search for a binding
        Q_concat(string, sizeof(string), "use ", cl.configstrings[CS_ITEMS + item]);
        bind = Key_GetBinding(string);

        Q_snprintf(string, sizeof(string), "%6s %3i %s",
                   bind, cl.inventory[item], cl.configstrings[CS_ITEMS + item]);

        if (item != selected) {
            HUD_DrawAltString(x, y, string);
        } else {    // draw a blinky cursor by the selected item
            HUD_DrawString(x, y, string);
            if ((cls.realtime >> 8) & 1) {
                R_DrawChar(x - CHAR_WIDTH, y, 0, 15, scr.font_pic);
            }
        }

        y += CHAR_HEIGHT;
    }
}

static void SCR_ExecuteLayoutString(const char *s)
{
    char    buffer[MAX_QPATH];
    int     x, y;
    int     value;
    char    *token;
    int     width;
    int     index;
    clientinfo_t    *ci;

    if (!s[0])
        return;

    x = scr.hud_x;
    y = scr.hud_y;

    while (s) {
        token = COM_Parse(&s);
        if (token[2] == 0) {
            if (token[0] == 'x') {
                if (token[1] == 'l') {
                    token = COM_Parse(&s);
                    x = scr.hud_x + atoi(token);
                    continue;
                }

                if (token[1] == 'r') {
                    token = COM_Parse(&s);
                    x = scr.hud_x + scr.hud_width + atoi(token);
                    continue;
                }

                if (token[1] == 'v') {
                    token = COM_Parse(&s);
                    x = scr.hud_x + scr.hud_width / 2 - 160 + atoi(token);
                    continue;
                }
            }

            if (token[0] == 'y') {
                if (token[1] == 't') {
                    token = COM_Parse(&s);
                    y = scr.hud_y + atoi(token);
                    continue;
                }

                if (token[1] == 'b') {
                    token = COM_Parse(&s);
                    y = scr.hud_y + scr.hud_height + atoi(token);
                    continue;
                }

                if (token[1] == 'v') {
                    token = COM_Parse(&s);
                    y = scr.hud_y + scr.hud_height / 2 - 120 + atoi(token);
                    continue;
                }
            }
        }

        if (!strcmp(token, "pic")) {
            // draw a pic from a stat number
            token = COM_Parse(&s);
            value = atoi(token);
            if (value < 0 || value >= MAX_STATS) {
                Com_Error(ERR_DROP, "%s: invalid stat index", __func__);
            }
            value = cl.frame.ps.stats[value];
            if (value < 0 || value >= MAX_IMAGES) {
                Com_Error(ERR_DROP, "%s: invalid pic index", __func__);
            }
            token = cl.configstrings[CS_IMAGES + value];
            if (token[0]) {
                qhandle_t pic = cl.image_precache[value];
                // hack for action mod scope scaling
                if (Com_WildCmp("scope?x", token)) {
                    int x = scr.hud_x + (scr.hud_width - scr.scope_width) / 2;
                    int y = scr.hud_y + (scr.hud_height - scr.scope_height) / 2;

                    int w = scr.scope_width;
                    int h = scr.scope_height;
                    R_DrawStretchPic(x + ch_x->integer,
                                     y + ch_y->integer,
                                     w, h, pic);
                } else {
                    R_DrawPic(x, y, pic);
                }
            }
            continue;
        }

        if (!strcmp(token, "client")) {
            // draw a deathmatch client block
            int     score, ping, time;

            token = COM_Parse(&s);
            x = scr.hud_x + scr.hud_width / 2 - 160 + atoi(token);
            token = COM_Parse(&s);
            y = scr.hud_y + scr.hud_height / 2 - 120 + atoi(token);

            token = COM_Parse(&s);
            value = atoi(token);
            if (value < 0 || value >= MAX_CLIENTS) {
                Com_Error(ERR_DROP, "%s: invalid client index", __func__);
            }
            ci = &cl.clientinfo[value];

            token = COM_Parse(&s);
            score = atoi(token);

            token = COM_Parse(&s);
            ping = atoi(token);

            token = COM_Parse(&s);
            time = atoi(token);

            HUD_DrawAltString(x + 32, y, ci->name);
            HUD_DrawString(x + 32, y + CHAR_HEIGHT, "Score: ");
            Q_snprintf(buffer, sizeof(buffer), "%i", score);
            HUD_DrawAltString(x + 32 + 7 * CHAR_WIDTH, y + CHAR_HEIGHT, buffer);
            Q_snprintf(buffer, sizeof(buffer), "Ping:  %i", ping);
            HUD_DrawString(x + 32, y + 2 * CHAR_HEIGHT, buffer);
            Q_snprintf(buffer, sizeof(buffer), "Time:  %i", time);
            HUD_DrawString(x + 32, y + 3 * CHAR_HEIGHT, buffer);

            if (!ci->icon) {
                ci = &cl.baseclientinfo;
            }
            R_DrawPic(x, y, ci->icon);
            continue;
        }

        if (!strcmp(token, "ctf")) {
            // draw a ctf client block
            int     score, ping;

            token = COM_Parse(&s);
            x = scr.hud_x + scr.hud_width / 2 - 160 + atoi(token);
            token = COM_Parse(&s);
            y = scr.hud_y + scr.hud_height / 2 - 120 + atoi(token);

            token = COM_Parse(&s);
            value = atoi(token);
            if (value < 0 || value >= MAX_CLIENTS) {
                Com_Error(ERR_DROP, "%s: invalid client index", __func__);
            }
            ci = &cl.clientinfo[value];

            token = COM_Parse(&s);
            score = atoi(token);

            token = COM_Parse(&s);
            ping = atoi(token);
            if (ping > 999)
                ping = 999;

            Q_snprintf(buffer, sizeof(buffer), "%3d %3d %-12.12s",
                       score, ping, ci->name);
            if (value == cl.frame.clientNum) {
                HUD_DrawAltString(x, y, buffer);
            } else {
                HUD_DrawString(x, y, buffer);
            }
            continue;
        }

        if (!strcmp(token, "picn")) {
            // draw a pic from a name
            token = COM_Parse(&s);
            R_DrawPic(x, y, R_RegisterPic2(token));
            continue;
        }

        if (!strcmp(token, "num")) {
            // draw a number
            token = COM_Parse(&s);
            width = atoi(token);
            token = COM_Parse(&s);
            value = atoi(token);
            if (value < 0 || value >= MAX_STATS) {
                Com_Error(ERR_DROP, "%s: invalid stat index", __func__);
            }
            value = cl.frame.ps.stats[value];
            HUD_DrawNumber(x, y, 0, width, value);
            continue;
        }

        if (!strcmp(token, "hnum")) {
            // health number
            int     color;

            width = 3;
            value = cl.frame.ps.stats[STAT_HEALTH];
            if (value > 25)
                color = 0;  // green
            else if (value > 0)
                color = ((cl.frame.number / CL_FRAMEDIV) >> 2) & 1;     // flash
            else
                color = 1;

            if (cl.frame.ps.stats[STAT_FLASHES] & 1)
                R_DrawPic(x, y, scr.field_pic);

            HUD_DrawNumber(x, y, color, width, value);
            continue;
        }

        if (!strcmp(token, "anum")) {
            // ammo number
            int     color;

            width = 3;
            value = cl.frame.ps.stats[STAT_AMMO];
            if (value > 5)
                color = 0;  // green
            else if (value >= 0)
                color = ((cl.frame.number / CL_FRAMEDIV) >> 2) & 1;     // flash
            else
                continue;   // negative number = don't show

            if (cl.frame.ps.stats[STAT_FLASHES] & 4)
                R_DrawPic(x, y, scr.field_pic);

            HUD_DrawNumber(x, y, color, width, value);
            continue;
        }

        if (!strcmp(token, "rnum")) {
            // armor number
            int     color;

            width = 3;
            value = cl.frame.ps.stats[STAT_ARMOR];
            if (value < 1)
                continue;

            color = 0;  // green

            if (cl.frame.ps.stats[STAT_FLASHES] & 2)
                R_DrawPic(x, y, scr.field_pic);

            HUD_DrawNumber(x, y, color, width, value);
            continue;
        }

        if (!strcmp(token, "stat_string")) {
            token = COM_Parse(&s);
            index = atoi(token);
            if (index < 0 || index >= MAX_STATS) {
                Com_Error(ERR_DROP, "%s: invalid stat index", __func__);
            }
            index = cl.frame.ps.stats[index];
            if (index < 0 || index >= MAX_CONFIGSTRINGS) {
                Com_Error(ERR_DROP, "%s: invalid string index", __func__);
            }
            HUD_DrawString(x, y, cl.configstrings[index]);
            continue;
        }

        if (!strcmp(token, "cstring")) {
            token = COM_Parse(&s);
            HUD_DrawCenterString(x + 320 / 2, y, token);
            continue;
        }

        if (!strcmp(token, "cstring2")) {
            token = COM_Parse(&s);
            HUD_DrawAltCenterString(x + 320 / 2, y, token);
            continue;
        }

        if (!strcmp(token, "string")) {
            token = COM_Parse(&s);
            HUD_DrawString(x, y, token);
            continue;
        }

        if (!strcmp(token, "string2")) {
            token = COM_Parse(&s);
            HUD_DrawAltString(x, y, token);
            continue;
        }

        if (!strcmp(token, "if")) {
            token = COM_Parse(&s);
            value = atoi(token);
            if (value < 0 || value >= MAX_STATS) {
                Com_Error(ERR_DROP, "%s: invalid stat index", __func__);
            }
            value = cl.frame.ps.stats[value];
            if (!value) {   // skip to endif
                while (strcmp(token, "endif")) {
                    token = COM_Parse(&s);
                    if (!s) {
                        break;
                    }
                }
            }
            continue;
        }

        // Q2PRO extension
        if (!strcmp(token, "color")) {
            color_t     color;

            token = COM_Parse(&s);
            if (SCR_ParseColor(token, &color)) {
                color.u8[3] *= scr_alpha->value;
                R_SetColor(color.u32);
            }
            continue;
        }
    }

    R_ClearColor();
    R_SetAlpha(scr_alpha->value);
}

//=============================================================================

static void SCR_DrawPause(void)
{
    int x, y;

    if (!sv_paused->integer)
        return;
    if (!cl_paused->integer)
        return;
    if (scr_showpause->integer != 1)
        return;

    x = scr.hud_x + (scr.hud_width - scr.pause_width) / 2;
    y = scr.hud_y + (scr.hud_height - scr.pause_height) / 2;

    R_DrawPic(x, y, scr.pause_pic);
}

static void SCR_DrawLoading(void)
{
    int x, y;

    if (!scr.draw_loading)
        return;

    scr.draw_loading = false;

    R_SetScale(scr.hud_scale);

    x = (r_config.width * scr.hud_scale - scr.loading_width) / 2;
    y = (r_config.height * scr.hud_scale - scr.loading_height) / 2;

    R_DrawPic(x, y, scr.loading_pic);

    R_SetScale(1.0f);
}

typedef struct {
    char wepname[64];
    int gap;
    float moving_inacc;
    float duck_acc;
    int firing_frame;
    int firing_frame_2;
    float firing_scale;
} xhair_weapon_cfg_t;

typedef struct {
    int gap, length;
} xhair_state_t;

#define XHAIR_MAX_GAP 1024
#define XHAIR_MAX_LENGTH 648
static float deltatime_factor = 0.001;

static xhair_weapon_cfg_t xhair_weapon_cfgs[9] = {
    {"models/weapons/v_m4/tris.md2", 5, 2.35, 0.6, 12, 11, 1.1},
    {"models/weapons/v_blast/tris.md2", -2, 3, 0.7, 11, 12, 2.25},
    {"models/weapons/v_machn/tris.md2", 1, 2.35, 0.6, 12, 11, 1.25},
    {"models/weapons/v_knife/tris.md2", XHAIR_MAX_GAP, 1, 1, -1, -1, 1},
    {"models/weapons/v_sniper/tris.md2", XHAIR_MAX_GAP, 1, 1, -1, -1, 1},
    {"models/weapons/v_shotg/tris.md2", 50, 1, 1, 9, -1, 1},
    {"models/weapons/v_cannon/tris.md2", 180, 1, 1, 8, -1, 1},
    {"models/weapons/v_dual/tris.md2", 3, 2.25, 0.7, 11, 12, 1},
    {"models/weapons/v_handgr/tris.md2", XHAIR_MAX_GAP, 1, 1, -1, -1, 1}
};
static int XHAIR_GetWeaponIndex(void) {
    char* wepname = cl.configstrings[CS_MODELS + cl.frame.ps.gunindex];
    for(int i=0; i<9; i++) {
        if (Q_stricmp(wepname, xhair_weapon_cfgs[i].wepname) == 0) {
            return i;
        }
    }
    return 3; //fallback gives {XHAIR_MAX_GAP,1,1}
}
static xhair_state_t XHAIR_ApplyWeaponGap(xhair_state_t xh, int wep_index) {
    xh.gap += xhair_weapon_cfgs[wep_index].gap
        *deltatime_factor*1000;
    if (xhair_weapon_cfgs[wep_index].gap == XHAIR_MAX_GAP) {
        xh.length = XHAIR_MAX_LENGTH;
    }
    return xh; 
}

static xhair_state_t XHAIR_ApplyMovingInaccuracy(xhair_state_t xh, int wep_index) {
    byte pm_flags = cl.frame.ps.pmove.pm_flags;
    pmove_state_t* pm = &cl.frame.ps.pmove;
    static const short velo_boundary = 20;
    if (pm_flags & PMF_DUCKED) {
        xh.gap *= 1000*xhair_weapon_cfgs[wep_index].duck_acc*deltatime_factor;
    } else {
        if (!(pm_flags & PMF_ON_GROUND)) {
           //doesn't actually do anything to weapon accuracy 
        }
        if (fabs(pm->velocity[0]) > velo_boundary 
            || fabs(pm->velocity[1]) > velo_boundary) {
            xh.gap += 5000*xhair_weapon_cfgs[wep_index].moving_inacc
                    *xhair_movement_error->value*deltatime_factor;
        }
    }
    return xh;
}

static int XHAIR_WeaponJustFired(int wep_index) {
    return (cl.frame.ps.gunframe == xhair_weapon_cfgs[wep_index].firing_frame);
}

static int XHAIR_WeaponIsFiring(int wep_index) {
    return (cl.frame.ps.gunframe == xhair_weapon_cfgs[wep_index].firing_frame
        ||  cl.frame.ps.gunframe == xhair_weapon_cfgs[wep_index].firing_frame_2);
}

static xhair_state_t XHAIR_ApplyFiringInaccuracy(xhair_state_t xh, int wep_index) {
    static int spray = 0;
    if (spray > 0) {
        spray -= 1800*deltatime_factor;
    }
    if (XHAIR_WeaponJustFired(wep_index)) {
        xh.gap += xhair_weapon_cfgs[wep_index].firing_scale*
            2850*spray/425*xhair_firing_error->value * deltatime_factor;
        xh.gap *= xhair_weapon_cfgs[wep_index].firing_scale*
            1300*xhair_firing_error->value * deltatime_factor;
        xh.length *= 750*spray/60*xhair_firing_error->value * deltatime_factor;
        spray += 4000*deltatime_factor;
    }
    
    //Con_Printf("%d\n",cl.frame.ps.gunframe);
    return xh;
}
static void SCR_DrawXhair(void) {
    //Con_Printf("deltatime_factor: %g\n",deltatime_factor);
    R_SetColor(scr.crosshair_color.u32);
    //Con_Printf("%f",scr.hud_scale);
    R_SetScale(1.0f); 
    static int last_rtime = 0;
    deltatime_factor = (float)((cls.realtime-last_rtime)/2 + 4)*0.001;
    //deltatime_factor = 0.001;
    deltatime_factor /= 4;
    int deltatime_ms = (cls.realtime - last_rtime);
    if (deltatime_ms > 8) deltatime_ms = 8;
   // Con_Printf("%d ",deltatime_ms);
    last_rtime = cls.realtime;
    int xh_center_x = scr.hud_width/2 - xhair_thickness->integer/2;
    int xh_center_y = scr.hud_height/2 - xhair_thickness->integer/2;
    if (xhair_dot->integer) {
        R_DrawFill32(xh_center_x+xhair_x->integer,
            xh_center_y+xhair_y->integer,
            xhair_thickness->integer,
            xhair_thickness->integer,
            scr.crosshair_color.u32);
    }
    static float gap = 5;
    static float length = 4;
    static float xh_elasticity = 0.04;
    xhair_state_t xh;
    xh.gap = xhair_gap->integer;
    xh.length = xhair_length->integer;
    int wep_index = XHAIR_GetWeaponIndex();

    //fix for alt-tab glitch
    if (fabs(gap) > XHAIR_MAX_GAP*1.5
        || fabs(length) > XHAIR_MAX_LENGTH*2
        || fabs(xh_elasticity) > 1 ) {
        xh_elasticity = .75;
        gap = XHAIR_MAX_GAP;
        length = XHAIR_MAX_LENGTH; //I think this is a cool effect :P
    }

    if (xhair_deployed_weapon_gap->integer) 
        xh = XHAIR_ApplyWeaponGap(xh,wep_index);
    if (xhair_firing_error->value)
        xh = XHAIR_ApplyFiringInaccuracy(xh,wep_index);
    if (xhair_movement_error->value)
        xh = XHAIR_ApplyMovingInaccuracy(xh,wep_index);        
    
    if (XHAIR_WeaponIsFiring(wep_index) && xhair_firing_error->value) {
        xh_elasticity += (0.04*xhair_elasticity->value - xh_elasticity) * deltatime_factor*500;
    } else {
        xh_elasticity += (0.02*xhair_elasticity->value - xh_elasticity) * deltatime_factor*500;
    }
    int repeat = 1;
    do { //framerate-independence...not ideal, but it works
        gap += (xh.gap - gap) * xh_elasticity * deltatime_factor*500;
        length += (xh.length - length) * xh_elasticity * deltatime_factor*500;
        repeat *= 2;
    } while(repeat<deltatime_ms);

    int rgap = (int)(round(gap));
    int rlength = (int)(round(length));
    if (xhair_weapon_cfgs[wep_index].gap != XHAIR_MAX_GAP) {
        /*
        i       rem     quot    x       y       w       h 
        0       0       0       0       gap     1       length
        1       1       0       gap     0       length  1
        2       0       1       0      -gap-len 1       length
        3       1       1      -gap-len 0       length  1
        */
        for (int i=0; i<4; i++) {
            int xh_x = xh_center_x + xhair_x->integer;
            int xh_y = xh_center_y + xhair_y->integer;
            int xh_w;
            int xh_h;
            int quot = i / 2;
            int rem  = i % 2;
            if (quot) {
                xh_x -= (rgap+rlength)*rem;
                xh_y -= (rgap+rlength)*(1-rem);
                xh_w = rem ? rlength : xhair_thickness->integer;
                xh_h = rem ? xhair_thickness->integer : rlength;
            } else {
                xh_x += (1+rgap+xhair_thickness->integer-1)*(1-rem);
                xh_y += (1+rgap+xhair_thickness->integer-1)*rem;
                xh_h = rem ? rlength : xhair_thickness->integer;
                xh_w = rem ? xhair_thickness->integer : rlength;
            }
            R_DrawFill32(xh_x,xh_y,
                xh_w,xh_h,scr.crosshair_color.u32);
        }
    } 
}
static void SCR_DrawClassicCrosshair(void) {
    R_SetColor(scr.crosshair_color.u32);
    int x, y;

    if (!scr_crosshair->integer)
        return;

    x = scr.hud_x + (scr.hud_width - scr.crosshair_width) / 2;
    y = scr.hud_y + (scr.hud_height - scr.crosshair_height) / 2;
    
    R_DrawStretchPic(x + ch_x->integer,
                     y + ch_y->integer,
                     scr.crosshair_width,
                     scr.crosshair_height,
                     scr.crosshair_pic);
}
#ifdef AQTION_EXTENSION
void CL_Clear3DGhudQueue(void)
{
	ghud_3delement_t *link;
	ghud_3delement_t *hold;
	for (link = cl.ghud_3dlist; link != NULL; hold = link, link = link->next, free(hold));
}


static void SCR_DrawGhudElement(ghud_element_t *element, float alpha_base, color_t color_base, int x, int y, int sizex, int sizey)
{
	byte alpha = element->color[3];
	if (element->flags & GHF_BLINK)
		alpha = min((element->color[3] * 0.85) + (element->color[3] * 0.25 * sin((float)cls.realtime / 125)), 255);

	color_base.u8[0] = element->color[0] * (color_base.u8[2] / 0xFF);
	color_base.u8[1] = element->color[1] * (color_base.u8[2] / 0xFF);
	color_base.u8[2] = element->color[2] * (color_base.u8[2] / 0xFF);
	color_base.u8[3] = (alpha_base * alpha);
	R_SetColor(color_base.u32);

	switch (element->type)
	{
	case GHT_TEXT:;
		int length = strlen(element->text);
		int uiflags = element->size[0] | (element->size[1] << 16);
		if ((uiflags & UI_CENTER) == UI_CENTER)
			x -= (length * CHAR_WIDTH * 0.5);
		else if (uiflags & UI_RIGHT)
			x -= (length * CHAR_WIDTH);

		if ((uiflags & UI_MIDDLE) == UI_MIDDLE)
			y -= (length * CHAR_HEIGHT * 0.5);
		else if (uiflags & UI_BOTTOM)
			y -= (length * CHAR_HEIGHT);

		uiflags &= ~(UI_LEFT | UI_RIGHT | UI_TOP | UI_BOTTOM);

		R_DrawString(x, y, uiflags, MAX_STRING_CHARS, element->text, scr.font_pic);
		break;
	case GHT_IMG:
		if (!element->val)
			break;

		R_DrawStretchPic(x, y, sizex, sizey, cl.image_precache[element->val]);
		break;
	case GHT_NUM:;
		int numsize = element->size[0];
		if (numsize <= 0)
		{
			double val = element->val;
			if (val <= 0)
				val = 0;
			else
				val = log10(val);

			numsize = val + 1;
		}

		HUD_DrawNumber(x, y, 0, numsize, element->val);
		break;
    case GHT_FILL:;
		R_DrawFill32(x, y, element->size[0], element->size[1], color_base.u32);
	}
}


static void SCR_DrawGhud(void)
{
	int x, y;
	int i;

	float alpha_base = Cvar_ClampValue(scr_alpha, 0, 1);
	color_t color_base;
	color_base.u32 = 0xFFFFFFFF;


	if (cl.ghud_3dlist)
	{
		/*build view and projection matricies*/
		float modelview[16];
		float proj[16];

		Matrix4x4_CM_ModelViewMatrix(modelview, cl.refdef.viewangles, cl.refdef.vieworg);
		Matrix4x4_CM_Projection2(proj, cl.refdef.fov_x, cl.refdef.fov_y, 4);

		/*build the vp matrix*/
		Matrix4_Multiply(proj, modelview, r_viewmatrix);
		
		ghud_element_t *element;
		ghud_3delement_t *link;
		ghud_3delement_t *hold;
		for (link = cl.ghud_3dlist; link; hold = link, link = link->next, free(hold))
		{
			element = link->element;
			element->color[3] = 200;

			float v[4], tempv[4], out[4];

			// get position
			v[0] = element->pos[0];
			v[1] = element->pos[1];
			v[2] = element->pos[2];
			v[3] = 1;

			Matrix4x4_CM_Transform4(r_viewmatrix, v, tempv);

			if (tempv[3] < 0) // the element is behind us
				continue;

			tempv[0] /= tempv[3];
			tempv[1] /= tempv[3];
			tempv[2] /= tempv[3];

			out[0] = (1 + tempv[0]) / 2;
			out[1] = 1 - (1 + tempv[1]) / 2;
			out[2] = tempv[2];

			x = scr.hud_x + out[0] * scr.hud_width;
			y = scr.hud_y + out[1] * scr.hud_height;
			//

			float mult = 300 / link->distance;
			clamp(mult, 0.25, 5);

			int sizex = element->size[0] * mult;
			int sizey = element->size[1] * mult;

			x -= (sizex / 2);
			y -= (sizey / 2);

			float alpha_mult = 1;
			alpha_mult = min(1 / mult, 1);

			
			vec3_t pos, xhair;
			pos[0] = x;
			pos[1] = y;
			pos[2] = 0;
			xhair[0] = scr.hud_width / 2;
			xhair[1] = scr.hud_height / 2;
			xhair[2] = 0;

			float scale_dimension = min(scr.hud_width, scr.hud_height) / 6;
			VectorSubtract(pos, xhair, pos);
			float len = VectorLength(pos);
			if (len < scale_dimension)
			{
				alpha_mult *= len / scale_dimension;
			}

			SCR_DrawGhudElement(element, alpha_base * alpha_mult, color_base, x, y, sizex, sizey);
		}

		cl.ghud_3dlist = NULL;
	}

	for (i = 0; i < MAX_GHUDS; i++)
	{
		ghud_element_t *element = &(cl.ghud[i]);
		if (!(element->flags & GHF_INUSE) || (element->flags & GHF_HIDE))
			continue;

		if (element->color[3] <= 0) // totally transparent
			continue;

		if (element->flags & GHF_3DPOS)
		{
			ghud_3delement_t *link = malloc(sizeof(ghud_3delement_t));
			link->element = element;

			vec3_t org;
			org[0] = element->pos[0];
			org[1] = element->pos[1];
			org[2] = element->pos[2];
			VectorSubtract(org, cl.refdef.vieworg, org);
			link->distance = VectorLength(org);
			link->next = NULL;

			///*
			if (cl.ghud_3dlist == NULL)
				cl.ghud_3dlist = link;
			else if (cl.ghud_3dlist->distance < link->distance)
			{
				link->next = cl.ghud_3dlist;
				cl.ghud_3dlist = link;
			}
			else
			{
				ghud_3delement_t *hold, *list;
				list = cl.ghud_3dlist;
				hold = list;
				while (list && list->distance >= link->distance)
				{
					hold = list;
					list = list->next;
				}

				link->next = hold->next;
				hold->next = link;
			}
			//*/

			continue;
		}
		else
		{
			x = scr.hud_x + element->pos[0] + (scr.hud_width * element->anchor[0]);
			y = scr.hud_y + element->pos[1] + (scr.hud_height * element->anchor[1]);
		}

		SCR_DrawGhudElement(element, alpha_base, color_base, x, y, element->size[0], element->size[1]);
	}
}
#endif

// The status bar is a small layout program that is based on the stats array
static void SCR_DrawStats(void)
{
    if (scr_draw2d->integer <= 1)
        return;

    SCR_ExecuteLayoutString(cl.configstrings[CS_STATUSBAR]);
}

static void SCR_DrawLayout(void)
{
    if (scr_draw2d->integer == 3 && !Key_IsDown(K_F1))
        return;     // turn off for GTV

    if (cls.demo.playback && Key_IsDown(K_F1))
        goto draw;

    if (!(cl.frame.ps.stats[STAT_LAYOUTS] & 1))
        return;

draw:
    SCR_ExecuteLayoutString(cl.layout);
}

static void SCR_Draw2D(void)
{
    if (scr_draw2d->integer <= 0)
        return;     // turn off for screenshots

    if (cls.key_dest & KEY_MENU)
        return;

    if (xhair_enabled->integer)
        SCR_DrawXhair();

    R_SetScale(scr.hud_scale);


	scr.hud_x = Q_rint(scr_hudborder_x->integer);
	scr.hud_y = Q_rint(scr_hudborder_y->integer);
    scr.hud_width = Q_rint((scr.hud_width - scr.hud_x) * scr.hud_scale);
	scr.hud_height = Q_rint((scr.hud_height - scr.hud_y) * scr.hud_scale);
	scr.hud_x *= scr.hud_scale / 2;
	scr.hud_y *= scr.hud_scale / 2;

    if (!xhair_enabled->integer) {
        SCR_DrawClassicCrosshair();
    }    
    
    // the rest of 2D elements share common alpha
    R_ClearColor();
    R_SetAlpha(Cvar_ClampValue(scr_alpha, 0, 1));

    SCR_DrawStats();

    SCR_DrawLayout();

#ifdef AQTION_EXTENSION
	// Draw game defined hud elements
	SCR_DrawGhud();

	// gotta redo the colors because the ghud messes with them, sadly.
	R_ClearColor();
	R_SetAlpha(Cvar_ClampValue(scr_alpha, 0, 1));
#endif

    SCR_DrawInventory();

    SCR_DrawCenterString();

    SCR_DrawNet();

    SCR_DrawObjects();

    SCR_DrawChatHUD();

    SCR_DrawTurtle();

	SCR_DrawPause();

    // debug stats have no alpha
    R_ClearColor();

#if USE_DEBUG
    SCR_DrawDebugStats();
    SCR_DrawDebugPmove();
#endif
    R_ClearColor();

    R_SetScale(1.0f);
}

static void SCR_DrawActive(void)
{
    // if full screen menu is up, do nothing at all
    if (!UI_IsTransparent())
        return;

    // draw black background if not active
    if (cls.state < ca_active) {
        R_DrawFill8(0, 0, r_config.width, r_config.height, 0);
        return;
    }

    if (cls.state == ca_cinematic) {
        SCR_DrawCinematic();
        return;
    }

    // start with full screen HUD
    scr.hud_height = r_config.height;
    scr.hud_width = r_config.width;

    SCR_DrawDemo();

    SCR_CalcVrect();

    // clear any dirty part of the background
    SCR_TileClear();

    // draw 3D game view
    V_RenderView();

    // draw all 2D elements
    SCR_Draw2D();
}

//=======================================================

/*
==================
SCR_UpdateScreen

This is called every frame, and can also be called explicitly to flush
text to the screen.
==================
*/
void SCR_UpdateScreen(void)
{
    static int recursive;

    if (!scr.initialized) {
        return;             // not initialized yet
    }

    // if the screen is disabled (loading plaque is up), do nothing at all
    if (cls.disable_screen) {
        unsigned delta = Sys_Milliseconds() - cls.disable_screen;

        if (delta < 120 * 1000) {
            return;
        }

        cls.disable_screen = 0;
        Com_Printf("Loading plaque timed out.\n");
    }

    if (recursive > 1) {
        Com_Error(ERR_FATAL, "%s: recursively called", __func__);
    }

    recursive++;

    R_BeginFrame();

    // do 3D refresh drawing
    SCR_DrawActive();

    // draw main menu
    UI_Draw(cls.realtime);

    // draw console
    Con_DrawConsole();

    // draw loading plaque
    SCR_DrawLoading();

    R_EndFrame();

    recursive--;
}
