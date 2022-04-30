#include "external_cmds.h"
#include "sockdef.h"
#include "dwm.h"
#include "config.h"
#include "util.h"

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
Signal signals[] = {
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
CALC_SIZE(signals);

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

QuerySignal query_funcs[] = {
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
CALC_SIZE(query_funcs);

