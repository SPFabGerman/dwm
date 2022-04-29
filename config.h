/* See LICENSE file for copyright and license details. */

#include "dwm.h"
#include "layouts.h"

/* appearance */
static const unsigned int borderpx  = 2;        /* border pixel of windows */
static const int cornerradius       = 4;        /* Radius of window corners; 0 disables this feature completely. */
/* TODO: Make per Client */
static const unsigned int gappxdf   = 4;        /* default gaps between windows */
static const unsigned int snap      = 16;       /* snap pixel */
static const int swallowfloating    = 0;        /* 1 means swallow floating windows by default */
static const int useanimation       = 1;        /* 1 means animate window movements */
static const int animationframes    = 30;       /* Amount of frames the animations should take per window. */
static const int framereduction     = 0;        /* Amount of frames, the animation should be reduced by, per new Client. */
static const int frreducstart       = -1;       /* After how many Clients should the animation time be decreased? */
static const int framedur           = 15000 / 30; /* Duration of a single animation frame in microseconds */
static const int extrareservedspace = 30;       /* Space at barpos, where no window can be drawn */
#define BLACK "#282c34"
#define WHITE "#dcdfe4"
#define CYAN "#519fdf"
#define GREEN "#88b369"
#define OPAQUE 0xffU
static char col_brd_sel[]      = CYAN;
static char col_brd_norm[]     = GREEN;
static const unsigned int borderalpha = OPAQUE;
// TODO: Change the color logic
static char *colors[][3]      = {
	/*               fg         bg         border   */
	[SchemeNorm] = { col_brd_norm, col_brd_norm, col_brd_norm },
	[SchemeSel]  = { col_brd_sel, col_brd_sel, col_brd_sel },
};
static const unsigned int alphas[][3]      = {
	/*               fg      bg        border     */
	[SchemeNorm] = { borderalpha, borderalpha, borderalpha },
	[SchemeSel]  = { borderalpha, borderalpha, borderalpha },
};

#define NUMTAGS 9

/* layout(s) */
static const float mfact     = 0.5; /* factor of master area size [0.05..0.95] */
static const int nmaster     = 1;    /* number of clients in master area */
static const int resizehints = 0;    /* 1 means respect size hints in tiled resizals */

/* === Layouts === */
static const Layout layouts[] = {
	/* symbol     arrange function */
	{ "[]=",      tile },    /* first entry is default */
	{ "><>",      NULL },    /* no layout function means floating behavior */
	{ "[M]",      monocle },
	{ "###",      horizgrid },
	{ "\\\\\\",   dwindle },
	{ "TTT",      bstack },
	{ "|||",      tcl },
};

static const Layout * overviewlayout = &layouts[3]; /* The layout used for the overviewmode. Set to null, to not change layout. */

static const Rule rules[] = {
	/* xprop(1):
	 *	WM_CLASS(STRING) = instance, class
	 *	WM_NAME(STRING) = title
	 */
	/* class      instance    title       tags mask  isfloating  isterminal  noswallow  monitor  layout       resizehints  noroundcorners  noanimatemove  noanimateresize */
	{ "zoom",     NULL,       NULL,       1 << 8,    0,          0,          0,         -1,      &layouts[1], 0,           0,              0,             0  },
	{ "Foxit Reader", NULL,   NULL,       0,         0,          0,          0,         -1,      &layouts[2], 0,           0,              0,             0  },
	{ "Foxit Reader", NULL,   "Form",     0,         1,          0,          1,         -1,      NULL,        0,           1,              0,             0  }, /* prevent bug, when applying pixmap */
	{ "st",       NULL,       NULL,       0,         0,          1,          0,         -1,      NULL,        0,           0,              0,             1  },
	{ NULL,       NULL,       "Event Tester",0,      1,          0,          1,         -1,      NULL,        0,           0,              0,             0  }, /* xev */
	{ "MPlayer",  NULL,       NULL,       0,         0,          0,          0,         -1,      NULL,        1,           0,              0,             0  }, /* for webcam */
	{ "firefox",  NULL,       "Picture-in-Picture", 0, 0,        0,          0,         -1,      NULL,        1,           0,              0,             0  },
	{ "Mailspring", NULL,     NULL,       0,         0,          0,          0,         -1,      NULL,        0,           1,              0,             0  },
	// { "Nwg-drawer", NULL,     NULL,       0,         1,          0,          0,         -1,      NULL,        0,           0,              0,             0  },
	{ "Nwg-bar",  NULL,       NULL,       0,         1,          0,          0,         -1,      NULL,        0,           0,              0,             0  },
};

/* Command to be executed, when swapping the tag.
 * tagswap_cmd_number will be replaced by the number of the selected tag. */
// static char tagswap_cmd_number[2] = "1";
// static const char * tagswap_cmd[] = { "swapbg", tagswap_cmd_number /* Will be later replaced */, "-q", NULL };

/* Cmd to update the external bar */
static const char * barupdate_cmd[] = { "polybar-msg", "hook", "dwmtags", "1", NULL };

/* key definitions */
#define MODKEY Mod4Mask
#define TAGKEYS(KEY,TAG) \
	{ MODKEY,                       KEY,      view,           {.ui = 1 << TAG} }, \
	{ MODKEY|ControlMask,           KEY,      toggleview,     {.ui = 1 << TAG} }, \
	{ MODKEY|ShiftMask,             KEY,      tag,            {.ui = 1 << TAG} }, \
	{ MODKEY|ControlMask|ShiftMask, KEY,      toggletag,      {.ui = 1 << TAG} },

/* helper for spawning shell commands in the pre dwm-5.0 fashion */
#define SHCMD(cmd) { .v = (const char*[]){ "/bin/sh", "-c", cmd, NULL } }

/* commands */
static Key keys[] = {
	/* modifier                     key        function        argument */

	{ MODKEY,                       XK_Down,      focusstack,     {.i = +1 } },
	{ MODKEY,                       XK_Up,        focusstack,     {.i = -1 } },
	{ MODKEY,                       XK_plus,      incnmaster,     {.i = +1 } },
	{ MODKEY,                       XK_minus,     incnmaster,     {.i = -1 } },
	/* { MODKEY,                       XK_equal,      incnmaster,     {.i = 0 } }, */ // Conflicts with Shift-0 (Tag All)
	{ MODKEY,                       XK_less,      setmfact,       {.f = -0.05} },
	{ MODKEY|ShiftMask,             XK_less,      setmfact,       {.f = +0.05} },
	{ MODKEY|Mod5Mask,              XK_less,      setmfact,       {.f = mfact+1.0} },
	{ MODKEY,                       XK_g,         setgap,         {.i = 0 } },
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

/* button definitions */
/* click can be ClkTagBar, ClkLtSymbol, ClkStatusText, ClkWinTitle, ClkClientWin, or ClkRootWin */
static Button buttons[] = {
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

// === DWMC Extra Functions ===

void
setlayoutex(const Arg *arg)
{
	setlayout(&((Arg) { .v = &layouts[arg->i] }));
}

void
viewex(const Arg *arg)
{
	view(&((Arg) { .ui = 1 << arg->ui }));
}

void
viewall(const Arg *arg)
{
	view(&((Arg){.ui = ~0}));
}

void
toggleviewex(const Arg *arg)
{
	toggleview(&((Arg) { .ui = 1 << arg->ui }));
}

void
tagex(const Arg *arg)
{
	tag(&((Arg) { .ui = 1 << arg->ui }));
}

void
toggletagex(const Arg *arg)
{
	toggletag(&((Arg) { .ui = 1 << arg->ui }));
}

void
tagall(const Arg *arg)
{
	tag(&((Arg){.ui = ~0}));
}

/* signal definitions */
/* signum must be greater than 0 */
/* trigger signals using `xsetroot -name "fsignal:<signame> [<type> <value>]"` */
static Signal signals[] = {
	/* signum           function */
	{ "focusstack",     focusstack },
	{ "setmfact",       setmfact },
	{ "incnmaster",     incnmaster },
	{ "togglefloating", togglefloating },
	{ "focusmon",       focusmon },
	{ "tagmon",         tagmon },
	{ "zoom",           zoom },
	{ "view",           view },
	{ "viewall",        viewall },
	{ "viewex",         viewex },
	{ "toggleview",     view },
	{ "toggleviewex",   toggleviewex },
	{ "tag",            tag },
	{ "tagall",         tagall },
	{ "tagex",          tagex },
	{ "toggletag",      tag },
	{ "toggletagex",    toggletagex },
	{ "killclient",     killclient },
	{ "quit",           quit },
	{ "setlayout",      setlayout },
	{ "setlayoutex",    setlayoutex },
	{ "xrdb",           xrdb },
};

// === DWMQ Extra Functions ===

Monitor * getMonFromIndex(int i) {
	int j;
	Monitor * m;
	for (j = 0, m = mons; m && m->next && j < i; m = m->next, j++);
	return m;
}

void cpyTags(Monitor * m, char * output) {
	int i;
	for (i = 0; i < TAGSLENGTH; i++) {
		if (m->tagset[m->seltags] & (1 << i))
			output[i] = '1';
		else
			output[i] = '0';
	}
}

int queryTags(char * input, char * output) {
	cpyTags(selmon, output);
	return 0;
}

int queryTagsMon(char * input, char * output) {
	int i;
	if (sscanf(input, "%d", &i) < 1) {
		return 1;
	}
	cpyTags(getMonFromIndex(i), output);
	return 0;
}

int queryOccTags(char * input, char * output) {
	unsigned int occ = 0;
	int i;
	Client * c;
	for (c = cl->clients; c; c = c->next) {
		occ |= c->tags;
	}
	for (i = 0; i < TAGSLENGTH; i++) {
		if (occ & (1 << i))
			output[i] = '1';
		else
			output[i] = '0';
	}
	return 0;
}

int queryUrgTags(char * input, char * output) {
	unsigned int urg = 0;
	int i;
	Client * c;
	for (c = cl->clients; c; c = c->next) {
		if (c->isurgent)
			urg |= c->tags;
	}
	for (i = 0; i < TAGSLENGTH; i++) {
		if (urg & (1 << i))
			output[i] = '1';
		else
			output[i] = '0';
	}
	return 0;

}

int queryNumMon(char * input, char * output) {
	int i;
	Monitor * m;
	for (i = 0, m = mons; m; m=m->next, i++);
	output[0] = '0' + i;
	return 0;
}

int querySelmon(char * input, char * output) {
	int i;
	Monitor * m;
	for(i = 0, m = mons; m != selmon; m=m->next, i++);
	output[0] = '0' + i;
	return 0;
}

int queryGeomToMon(char * input, char * output) {
	unsigned int i, x = 0, y = 0, w = 1, h = 1;
	Monitor * m;
	Monitor * tm;
	if (sscanf(input, "%ux%u+%u+%u", &w, &h, &x, &y) < 4) {
		return 1;
	}
	tm = recttomon(x, y, w, h);
	for(i = 0, m = mons; m != tm; m=m->next, i++);
	output[0] = '0' + i;
	return 0;
}

int queryLayout(char * input, char * output) {
	strcpy(output, selmon->ltsymbol);
	return 0;
}

int queryLayoutMon(char * input, char * output) {
	int i;
	if (sscanf(input, "%d", &i) < 1) {
		return 1;
	}
	strcpy(output, getMonFromIndex(i)->ltsymbol);
	return 0;
}

int querySelWin(char * input, char * output) {
	int r,tag;
	unsigned int tagmask;
	Client * c;
	
	r = sscanf(input, "%d", &tag);
	if (r <= 0) {
		tagmask = selmon->tagset[selmon->seltags];
	} else {
		tagmask = 1 << tag;
	}

	for (c = selmon->cl->stack; c; c = c->snext) {
		if (c->tags & tagmask) {
			/* strncpy(output, c->name, MAXBUFF_SOCKET); */
			snprintf(output, MAXBUFF_SOCKET, "0x%lx", c->win);
			return 0;
		}
	}

	return 1;
}

int queryMasterWin(char * input, char * output) {
	int r,tag;
	unsigned int tagmask;
	Client * c;
	
	r = sscanf(input, "%d", &tag);
	if (r <= 0) {
		tagmask = selmon->tagset[selmon->seltags];
	} else {
		tagmask = 1 << tag;
	}

	for (c = selmon->cl->clients; c; c = c->next) {
		if (c->tags & tagmask && !c->isfloating) {
			/* strncpy(output, c->name, MAXBUFF_SOCKET); */
			snprintf(output, MAXBUFF_SOCKET, "0x%lx", c->win);
			return 0;
		}
	}

	return 1;
}

static QuerySignal query_funcs[] = {
	{ "nummons", queryNumMon },
	{ "selmon", querySelmon },
	{ "mon", queryGeomToMon },

	{ "tags", queryTags },
	{ "montags", queryTagsMon },

	{ "occ", queryOccTags },
	{ "urg", queryUrgTags },

	{ "layout", queryLayout },
	{ "monlayout", queryLayoutMon },
	{ "selwin", querySelWin },
	{ "masterwin", queryMasterWin }
};

