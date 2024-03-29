/* See LICENSE file for copyright and license details. */

#include "config.h"
#include "dwm.h"
#include "layouts.h"
#include "util.h"

/* appearance */
const unsigned int borderpx  = 1;        /* border pixel of windows */
const int cornerradius       = 4;        /* Radius of window corners; 0 disables this feature completely. */
const unsigned int gappxdf   = 4;        /* default gaps between windows */
const unsigned int snap      = 16;       /* snap pixel */
const int swallowfloating    = 0;        /* 1 means swallow floating windows by default */
const int useanimation       = 0;        /* 1 means animate window movements */
const int animationframes    = 30;       /* Amount of frames the animations should take per window. */
const int framereduction     = 0;        /* Amount of frames, the animation should be reduced by, per new Client. */
const int frreducstart       = -1;       /* After how many Clients should the animation time be decreased? */
const int framedur           = 15000 / 30; /* Duration of a single animation frame in microseconds */
const int extrareservedspace = 30;       /* Space at barpos, where no window can be drawn */
#define BLACK "#282c34"
#define WHITE "#dcdfe4"
#define CYAN "#519fdf"
#define GREEN "#88b369"
#define OPAQUE 0xffU
char col_brd_sel[]      = CYAN;
char col_brd_norm[]     = GREEN;
const unsigned int borderalpha = OPAQUE;
// TODO: Change the color logic
char *colors[][3]      = {
	/*               fg         bg         border   */
	[SchemeNorm] = { col_brd_norm, col_brd_norm, col_brd_norm },
	[SchemeSel]  = { col_brd_sel, col_brd_sel, col_brd_sel },
};
const unsigned int alphas[][3]      = {
	/*               fg      bg        border     */
	[SchemeNorm] = { borderalpha, borderalpha, borderalpha },
	[SchemeSel]  = { borderalpha, borderalpha, borderalpha },
};

/* layout(s) */
const float mfact     = 0.5; /* factor of master area size [0.05..0.95] */
const int nmaster     = 1;    /* number of clients in master area */
const int resizehints = 0;    /* 1 means respect size hints in tiled resizals */

/* === Layouts === */
const Layout layouts[] = {
	/* symbol     arrange function */
	{ "[]=",      tile },    /* first entry is default */
	{ "><>",      NULL },    /* no layout function means floating behavior */
	{ "[M]",      monocle },
	{ "###",      horizgrid },
	{ "\\\\\\",   dwindle },
	{ "TTT",      bstack },
	{ "|||",      tcl }
};
CALC_SIZE(layouts);

const Layout * overviewlayout = &layouts[3]; /* The layout used for the overviewmode. Set to null, to not change layout. */

const Rule rules[] = {
	/* xprop(1):
	 *	WM_CLASS(STRING) = instance, class
	 *	WM_NAME(STRING) = title
	 */
	/* class      instance    title       tags mask  isfloating  isterminal  noswallow  monitor  layout       resizehints  noroundcorners  noanimatemove  noanimateresize */
	{ "zoom",     NULL,       "zoom",     1 << 8,    1,          0,          0,         -1,      NULL,        0,           0,              0,             0  },
	{ "zoom",     NULL,       NULL,       1 << 8,    0,          0,          0,         -1,      NULL,        0,           0,              0,             0  },
	{ "VirtualBox Machine", NULL, NULL,   0,         0,          0,          0,         -1,      &layouts[2], 0,           0,              0,             0  },
	{ "Foxit Reader", NULL,   NULL,       0,         0,          0,          0,         -1,      &layouts[2], 0,           0,              0,             0  },
	{ "Foxit Reader", NULL,   "Form",     0,         1,          0,          1,         -1,      NULL,        0,           1,              0,             0  }, /* prevent bug, when applying pixmap */
	{ "Evince",   NULL,       NULL,       0,         0,          0,          0,         -1,      &layouts[2], 0,           0,              0,             0  },
	{ "st",       NULL,       NULL,       0,         0,          1,          0,         -1,      NULL,        0,           0,              0,             1  },
	{ NULL,       NULL,       "Event Tester",0,      1,          0,          1,         -1,      NULL,        0,           0,              0,             0  }, /* xev */
	{ "MPlayer",  NULL,       NULL,       0,         0,          0,          0,         -1,      NULL,        1,           0,              0,             0  }, /* for webcam */
	{ "firefox",  NULL,       "Picture-in-Picture", 0, 0,        0,          0,         -1,      NULL,        1,           0,              0,             0  },
	{ "Mailspring", NULL,     NULL,       0,         0,          0,          0,         -1,      NULL,        0,           1,              0,             0  },
	{ "Nwg-drawer", NULL,     NULL,       0,         0,          0,          1,         -1,      NULL,        0,           0,              0,             0  },
	{ "Nwg-bar",  NULL,       NULL,       0,         1,          0,          0,         -1,      NULL,        0,           0,              0,             0  },
};
CALC_SIZE(rules);

/* Command to be executed, when swapping the tag.
 * tagswap_cmd_number will be replaced by the number of the selected tag. */
// static char tagswap_cmd_number[2] = "1";
// static const char * tagswap_cmd[] = { "swapbg", tagswap_cmd_number /* Will be later replaced */, "-q", NULL };

/* Cmd to update the external bar */
const char * barupdate_cmd[] = { "polybar-msg", "hook", "dwmtags", "1", NULL };

/* key definitions */
#define MODKEY Mod4Mask
#define TAGKEYS(KEY,TAG) \
	{ MODKEY,                       KEY,      view,           {.ui = 1 << TAG} }, \
	{ MODKEY|ControlMask,           KEY,      toggleview,     {.ui = 1 << TAG} }, \
	{ MODKEY|ShiftMask,             KEY,      tag,            {.ui = 1 << TAG} }, \
	{ MODKEY|ControlMask|ShiftMask, KEY,      toggletag,      {.ui = 1 << TAG} },

/* commands */
Key keys[] = {
	/* modifier                     key        function        argument */

	{ MODKEY,                       XK_Down,      focusstack,     {.i = +1 } },
	{ MODKEY,                       XK_Up,        focusstack,     {.i = -1 } },
	{ MODKEY,                       XK_plus,      incnmaster,     {.i = +1 } },
	{ MODKEY,                       XK_minus,     incnmaster,     {.i = -1 } },
	/* { MODKEY,                       XK_equal,      incnmaster,     {.i = 0 } }, */ // Conflicts with Shift-0 (Tag All)
	{ MODKEY,                       XK_less,      setmfact,       {.f = -0.05} },
	{ MODKEY|ShiftMask,             XK_less,      setmfact,       {.f = +0.05} },
	{ MODKEY|Mod5Mask,              XK_less,      setmfact,       {.f = mfact+1.0} },
	{ MODKEY,                       XK_Return,    zoom,           {0} }, // Focus Selected
	{ MODKEY,                       XK_Tab,       view,           {0} }, // Tab betwenn recent Tags
	{ MODKEY,                       XK_c,         killclient,     {0} },

	// Change Layouts
	{ MODKEY|ShiftMask,                       XK_t,      setlayout,      {.v = &layouts[0]} },
	{ MODKEY|ShiftMask|ControlMask,           XK_f,      setlayout,      {.v = &layouts[1]} },
	{ MODKEY|ShiftMask,                       XK_m,      setlayout,      {.v = &layouts[2]} },
	{ MODKEY|ShiftMask,                       XK_g,      setlayout,      {.v = &layouts[3]} },
	{ MODKEY|ShiftMask,                       XK_d,      setlayout,      {.v = &layouts[4]} },
	{ MODKEY|ShiftMask,                       XK_b,      setlayout,      {.v = &layouts[5]} },
	{ MODKEY|ShiftMask,                       XK_c,      setlayout,      {.v = &layouts[6]} },
	{ MODKEY|ShiftMask,                       XK_Tab,    setlayout,      {0} },
	{ MODKEY|ShiftMask,                       XK_f,  togglefloating, {0} },

	// { MODKEY,                       XK_F5,     xrdb,           {.v = NULL } },

	// Monitor Setup
	{ MODKEY|ControlMask,                       XK_Right,  focusmon,       {.i = -1 } },
	{ MODKEY|ControlMask,                       XK_Left, focusmon,       {.i = +1 } },
	// { MODKEY|ShiftMask,             XK_comma,  tagmon,         {.i = -1 } },
	// { MODKEY|ShiftMask,             XK_period, tagmon,         {.i = +1 } },

	// Tag Keys
	{ MODKEY,                       XK_0,      view,           {.ui = ~0 } },
	{ MODKEY|ShiftMask,             XK_0,      tag,            {.ui = ~0 } },

	TAGKEYS(                        XK_1,                      0)
	TAGKEYS(                        XK_2,                      1)
	TAGKEYS(                        XK_3,                      2)
	TAGKEYS(                        XK_4,                      3)
	TAGKEYS(                        XK_5,                      4)
	TAGKEYS(                        XK_6,                      5)
	TAGKEYS(                        XK_7,                      6)
	TAGKEYS(                        XK_8,                      7)
	TAGKEYS(                        XK_9,                      8)

	{ MODKEY,                       XK_o,      overview,        {0} },

	// Stop DWM
	// { MODKEY,                       XK_r,      quit,           {0} },
};
CALC_SIZE(keys);

/* button definitions */
/* click can be ClkClientWin or ClkRootWin */
Button buttons[] = {
	/* click                event mask      button          function        argument */
	{ ClkClientWin,         MODKEY,         Button1,        movemouse,      {0} },
	{ ClkClientWin,         MODKEY,         Button2,        togglefloating, {0} },
	{ ClkClientWin,         MODKEY,         Button3,        resizemouse,    {0} },
	{ ClkClientWin,         MODKEY|ControlMask,         Button1,        resizemouse,    {0} },
	{ ClkClientWin,         MODKEY,         Button4,        setmfact,       { .f = +0.025 } },
	{ ClkClientWin,         MODKEY,         Button5,        setmfact,       { .f = -0.025 } },
	{ ClkRootWin,           MODKEY,         Button4,        setmfact,       { .f = +0.025 } },
	{ ClkRootWin,           MODKEY,         Button5,        setmfact,       { .f = -0.025 } },
};
CALC_SIZE(buttons);

