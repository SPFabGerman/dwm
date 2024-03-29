/* See LICENSE file for copyright and license details.

 *
 * dynamic window manager is designed like any other X client as well. It is
 * driven through handling X events. In contrast to other X clients, a window
 * manager selects for SubstructureRedirectMask on the root window, to receive
 * events about window (dis-)appearance. Only one X connection at a time is
 * allowed to select for this event mask.
 *
 * The event handlers of dwm are organized in an array which is accessed
 * whenever a new event has been fetched. This allows event dispatching
 * in O(1) time.
 *
 * Each child of the root window is called a client, except windows which have
 * set the override_redirect flag. Clients are organized in a linked client
 * list on each monitor, the focus history is remembered through a stack list
 * on each monitor. Each client contains a bit array to indicate the tags of a
 * client.
 *
 * Keys and tagging rules are organized as arrays and defined in config.h.
 *
 * To understand everything else, start reading main().
 */
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/Xresource.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <errno.h>
#include <locale.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef XINERAMA
#include <X11/extensions/Xinerama.h>
#endif /* XINERAMA */
#include <X11/Xft/Xft.h>
#include <X11/Xlib-xcb.h>
#include <X11/extensions/shape.h>
#include <xcb/res.h>

#include "drw.h"
#include "dwm.h"
#include "config.h"
#include "layouts.h"
#include "external_cmds.h"
#include "util.h"

/* variables */
static const char broken[] = "broken";
static int scanner;
static int screen;
static int sw, sh; /* X display screen geometry width, height */
static int (*xerrorxlib)(Display *, XErrorEvent *);
static unsigned int numlockmask = 0;
static void (*handler[LASTEvent])(XEvent *) = {
    [ButtonPress] = buttonpress,
    [ClientMessage] = clientmessage,
    [ConfigureRequest] = configurerequest,
    [ConfigureNotify] = configurenotify,
    [DestroyNotify] = destroynotify,
    [EnterNotify] = enternotify,
    [FocusIn] = focusin,
    [KeyPress] = keypress,
    [MappingNotify] = mappingnotify,
    [MapRequest] = maprequest,
    [MotionNotify] = motionnotify,
    [PropertyNotify] = propertynotify,
    [UnmapNotify] = unmapnotify};
static Atom wmatom[WMLast], netatom[NetLast];
static int restart = 0;
static int running = 1;
static int startupdone = 0;
static int tempdisableanimation = 0;
static Cur *cursor[CurLast];
static Clr **scheme;
static Display *dpy;
static Drw *drw;
Monitor *mons, *selmon;
static Window root, wmcheckwin;

static int useargb = 0;
static Visual *visual;
static int depth;
static Colormap cmap;

Clientlist *cl;
static Pertag *pertagglist;

static AnimateThreadArg *animatequeue;
static pthread_mutex_t animatemutex;

static int querysocket;
static pthread_t querysocket_thread;

unsigned int gappx;
static xcb_connection_t *xcon;

static int overviewmode;

/* configuration, allows nested code to access above variables */
#include "sockdef.h"

/* Needs to be here, because tags is only defined in config.h. */
struct Pertag {
  unsigned int curtag, prevtag;     /* current and previous tag */
  int nmasters[NUMTAGS + 1];        /* number of windows in master area */
  float mfacts[NUMTAGS + 1];        /* mfacts per tag */
  unsigned int sellts[NUMTAGS + 1]; /* selected layouts */
  const Layout
      *ltidxs[NUMTAGS + 1][2]; /* matrix of tags and layouts indexes  */
};

void animateclient(Client *c, int x, int y, int w, int h) {
  Client *ct;
  int oldx = c->x;
  int oldy = c->y;
  int oldw = c->w;
  int oldh = c->h;
  int frame, n;
  int maxframes = animationframes;

  for (n = 0, ct = nexttiled(c->mon->cl->clients, c->mon); ct;
       ct = nexttiled(ct->next, ct->mon), n++)
    ;
  if (frreducstart >= 0 && n >= frreducstart)
    maxframes = animationframes - framereduction * (n - frreducstart + 1);

  for (frame = 0; frame < maxframes; frame++) {
    double ratio = (double)frame / maxframes;
    pthread_mutex_lock(&animatemutex);
    resizeclient(c, oldx + ratio * (x - oldx), oldy + ratio * (y - oldy),
		 c->animateresize ? oldw + ratio * (w - oldw) : w,
		 c->animateresize ? oldh + ratio * (h - oldh) : h);
    pthread_mutex_unlock(&animatemutex);
    usleep(framedur);
  }

  pthread_mutex_lock(&animatemutex);
  resizeclient(c, x, y, w, h);
  pthread_mutex_unlock(&animatemutex);
}

void *animateclient_thread(void *arg1) {
  AnimateThreadArg *arg2 = (AnimateThreadArg *)arg1;
  animateclient(arg2->c, arg2->x, arg2->y, arg2->w, arg2->h);
  return NULL;
}

void animateclient_start(Client *c, int x, int y, int w, int h) {
  AnimateThreadArg *new = malloc(sizeof(AnimateThreadArg));
  new->c = c;
  new->x = x;
  new->y = y;
  new->h = h;
  new->w = w;
  if (pthread_create(&(new->thread), NULL, animateclient_thread, new) != 0) {
    fprintf(stderr, "animateclient: could not create new thread!\n");
    resizeclient(c, x, y, w, h);
    free(new);
    return;
  }
  new->next = animatequeue;
  animatequeue = new;
}

void animateclient_endall() {
  while (animatequeue != NULL) {
    if (pthread_join(animatequeue->thread, NULL) != 0) {
      fprintf(stderr, "Could not join Animation Thread.\n");
    }
    AnimateThreadArg *next = animatequeue->next;
    free(animatequeue);
    animatequeue = next;
  }
}

/* function implementations */
void applyrules(Client *c) {
  const char *class, *instance;
  unsigned int i;
  const Rule *r;
  Monitor *m;
  XClassHint ch = {NULL, NULL};
  const Layout *newLayout = NULL;

  /* setting defaults */
  c->tags = 0;
  c->isfloating = 0;
  c->noswallow = -1;
  c->isterminal = 0;
  c->useresizehints = resizehints;
  c->hasroundcorners = 1;
  c->animate = 1;
  c->animateresize = 1;

  XGetClassHint(dpy, c->win, &ch);
  class = ch.res_class ? ch.res_class : broken;
  instance = ch.res_name ? ch.res_name : broken;

  for (i = 0; i < rules_size; i++) {
    r = &rules[i];
    if ((!r->title || strstr(c->name, r->title)) &&
	(!r->class || strstr(class, r->class)) &&
	(!r->instance || strstr(instance, r->instance))) {
      if (r->isterminal)
	c->isterminal = 1;
      if (r->noswallow)
	c->noswallow = 1;
      if (r->isfloating)
	c->isfloating = 1;
      if (r->noroundcorners)
	c->hasroundcorners = 0;
      if (r->noanimatemove)
	c->animate = 0;
      if (r->noanimateresize)
	c->animateresize = 0;
      if (r->overrideresizehints != 0)
	c->useresizehints = r->overrideresizehints == -1 ? 0 : 1;

      c->tags |= r->tags;
      for (m = mons; m && (m->tagset[m->seltags] & c->tags) == 0; m = m->next)
	;
      if (m)
	c->mon = m;
      if (r->lt)
	newLayout = r->lt;
    }
  }
  if (ch.res_class)
    XFree(ch.res_class);
  if (ch.res_name)
    XFree(ch.res_name);

  if (c->tags & TAGMASK) {
    c->tags = c->tags & TAGMASK;
  } else if (c->mon->tagset[c->mon->seltags]) {
    c->tags = c->mon->tagset[c->mon->seltags];
  } else if (pertagglist->curtag > 0) {
    c->tags = 1 << (pertagglist->curtag - 1);
  } else {
    c->tags = TAGMASK;
  }

  /* Change Layout on all tags and update the Monitor. */
  if (newLayout) {
    for (i = 0; i <= NUMTAGS; i++) {
      if (c->tags & 1 << i) {
	// Loop over the tags of the client
	pertagglist->sellts[(i + 1) % (NUMTAGS + 1)] ^= 1;
	pertagglist->ltidxs[(i + 1) % (NUMTAGS + 1)]
			   [pertagglist->sellts[(i + 1) % (NUMTAGS + 1)]] =
	    newLayout;
      }
    }
    if (c->mon->tagset[c->mon->seltags] & c->tags) {
      Arg a = {.v = newLayout};
      setlayoutcustommonitor(&a, c->mon); /* TODO: Rausoptimieren, um Aufruf der
					     Updatebar CMD zu vermeiden */
    }
  }
}

int applysizehints(Client *c, int *x, int *y, int *w, int *h, int interact) {
  int baseismin;
  Monitor *m = c->mon;

  /* set minimum possible */
  *w = MAX(1, *w);
  *h = MAX(1, *h);
  if (interact) {
    if (*x > sw)
      *x = sw - WIDTH(c);
    if (*y > sh)
      *y = sh - HEIGHT(c);
    if (*x + *w + 2 * c->bw < 0)
      *x = 0;
    if (*y + *h + 2 * c->bw < 0)
      *y = 0;
  } else {
    if (*x >= m->wx + m->ww)
      *x = m->wx + m->ww - WIDTH(c);
    if (*y >= m->wy + m->wh)
      *y = m->wy + m->wh - HEIGHT(c);
    if (*x + *w + 2 * c->bw <= m->wx)
      *x = m->wx;
    if (*y + *h + 2 * c->bw <= m->wy)
      *y = m->wy;
  }
  if (*h < 1)
    *h = 1;
  if (*w < 1)
    *w = 1;
  if (c->useresizehints || c->isfloating ||
      !c->mon->lt[c->mon->sellt]->arrange) {
    /* see last two sentences in ICCCM 4.1.2.3 */
    baseismin = c->basew == c->minw && c->baseh == c->minh;
    if (!baseismin) { /* temporarily remove base dimensions */
      *w -= c->basew;
      *h -= c->baseh;
    }
    /* adjust for aspect limits */
    if (c->mina > 0 && c->maxa > 0) {
      if (c->maxa < (float)*w / *h)
	*w = *h * c->maxa + 0.5;
      else if (c->mina < (float)*h / *w)
	*h = *w * c->mina + 0.5;
    }
    if (baseismin) { /* increment calculation requires this */
      *w -= c->basew;
      *h -= c->baseh;
    }
    /* adjust for increment value */
    if (c->incw)
      *w -= *w % c->incw;
    if (c->inch)
      *h -= *h % c->inch;
    /* restore base dimensions */
    *w = MAX(*w + c->basew, c->minw);
    *h = MAX(*h + c->baseh, c->minh);
    if (c->maxw)
      *w = MIN(*w, c->maxw);
    if (c->maxh)
      *h = MIN(*h, c->maxh);
  }
  return *x != c->x || *y != c->y || *w != c->w || *h != c->h;
}

void arrange(Monitor *m) {
  if (m)
    showhide(m->cl->stack);
  else
    for (m = mons; m; m = m->next)
      showhide(m->cl->stack);
  if (m) {
    arrangemon(m);
    restack(m);
  } else
    for (m = mons; m; m = m->next)
      arrangemon(m);
}

void arrangemon(Monitor *m) {
  strncpy(m->ltsymbol, m->lt[m->sellt]->symbol, sizeof m->ltsymbol);
  if (m->lt[m->sellt]->arrange) {
    m->lt[m->sellt]->arrange(m);
    animateclient_endall();
  }
}

void attach(Client *c) {
  c->next = c->mon->cl->clients;
  c->mon->cl->clients = c;
}

void attachstack(Client *c) {
  c->snext = c->mon->cl->stack;
  c->mon->cl->stack = c;
}

void attachclients(Monitor *m) {
  /* attach clients to the specified monitor */
  Monitor *tm;
  Client *c;
  unsigned int utags = 0;
  Bool rmons = False;
  if (!m)
    return;

  /* collect information about the tags in use */
  for (tm = mons; tm; tm = tm->next)
    if (tm != m)
      utags |= tm->tagset[tm->seltags];

  for (c = m->cl->clients; c; c = c->next)
    if (ISVISIBLE(c, m)) {
      /* if client is also visible on other tags that are displayed on
       * other monitors, remove these tags */
      if (c->tags & utags) {
	c->tags = c->tags & m->tagset[m->seltags];
	rmons = True;
      }
      unfocus(c, True);
      c->mon = m;
    }

  if (rmons)
    for (tm = mons; tm; tm = tm->next)
      if (tm != m)
	arrange(tm);
}

void swallow(Client *p, Client *c) {
  Client *s;

  if (c->noswallow > 0 || c->isterminal)
    return;
  if (c->noswallow < 0 && !swallowfloating && c->isfloating)
    return;

  detach(c);
  detachstack(c);

  setclientstate(c, WithdrawnState);
  XUnmapWindow(dpy, p->win);

  p->swallowing = c;
  c->mon = p->mon;

  Window w = p->win;
  p->win = c->win;
  c->win = w;

  XChangeProperty(dpy, c->win, netatom[NetClientList], XA_WINDOW, 32,
		  PropModeReplace, (unsigned char *)&(p->win), 1);

  updatetitle(p);
  s = scanner ? c : p;
  resizeclient(p, s->x, s->y, s->w, s->h);
  updateclientlist();
}

void unswallow(Client *c) {
  c->win = c->swallowing->win;

  free(c->swallowing);
  c->swallowing = NULL;

  XDeleteProperty(dpy, c->win, netatom[NetClientList]);

  /* unfullscreen the client */
  setfullscreen(c, 0);
  updatetitle(c);
  // arrange(c->mon); # Why is this needed?
  XMapWindow(dpy, c->win);
  resizeclient(c, c->x, c->y, c->w, c->h);
  setclientstate(c, NormalState);
  focus(NULL);
  arrange(c->mon); /* Update for sizehints etc. */
}

void buttonpress(XEvent *e) {
  unsigned int i, click;
  Client *c;
  Monitor *m;
  XButtonPressedEvent *ev = &e->xbutton;

  click = ClkRootWin;
  /* focus monitor if necessary */
  if ((m = wintomon(ev->window)) && m != selmon) {
    unfocus(selmon->sel, 1);
    selmon = m;
    focus(NULL);
  }
  if ((c = wintoclient(ev->window))) {
    focus(c);
    restack(selmon);
    XAllowEvents(dpy, ReplayPointer, CurrentTime);
    click = ClkClientWin;
  }
  for (i = 0; i < buttons_size; i++)
    if (click == buttons[i].click && buttons[i].func &&
	buttons[i].button == ev->button &&
	CLEANMASK(buttons[i].mask) == CLEANMASK(ev->state)) {
      tempdisableanimation = 1; /* Disable animations on all button events to
				   make them more responsive */
      buttons[i].func(&buttons[i].arg);
      tempdisableanimation = 0;
    }
}

void checkotherwm(void) {
  xerrorxlib = XSetErrorHandler(xerrorstart);
  /* this causes an error if some other window manager is running */
  XSelectInput(dpy, DefaultRootWindow(dpy), SubstructureRedirectMask);
  XSync(dpy, False);
  XSetErrorHandler(xerror);
  XSync(dpy, False);
}

void cleanup(void) {
  Arg a = {.ui = ~0};
  Layout foo = {"", NULL};
  Monitor *m;
  size_t i;

  view(&a);
  selmon->lt[selmon->sellt] = &foo;
  for (m = mons; m; m = m->next)
    while (m->cl->stack)
      unmanage(m->cl->stack, 0);
  XUngrabKey(dpy, AnyKey, AnyModifier, root);
  while (mons)
    cleanupmon(mons);
  for (i = 0; i < CurLast; i++)
    drw_cur_free(drw, cursor[i]);
  for (i = 0; i < LENGTH(colors); i++)
    free(scheme[i]);
  XDestroyWindow(dpy, wmcheckwin);
  drw_free(drw);
  XSync(dpy, False);
  XSetInputFocus(dpy, PointerRoot, RevertToPointerRoot, CurrentTime);
  XDeleteProperty(dpy, root, netatom[NetActiveWindow]);

  /* Close and remove socket.
   * Ignore Errors, since we are quitting dwm soon. */
  if (pthread_cancel(querysocket_thread) != 0)
    fprintf(stderr, "Could not Cancel Query Thread.\n");
  if (pthread_join(querysocket_thread, NULL) != 0)
    fprintf(stderr, "Could not Cancel Query Thread.\n");
  if (shutdown(querysocket, SHUT_RDWR) != 0)
    fprintf(stderr, "Could not shutdown socket.\n");
  if (close(querysocket) != 0)
    fprintf(stderr, "Could not close socket.\n");
  if (unlink(SOCKET_PATH) != 0)
    fprintf(stderr, "Could not remove socket.\n");
}

void cleanupmon(Monitor *mon) {
  Monitor *m;

  if (mon == mons)
    mons = mons->next;
  else {
    for (m = mons; m && m->next != mon; m = m->next)
      ;
    m->next = mon->next;
  }
  XUnmapWindow(dpy, mon->barwin);
  XDestroyWindow(dpy, mon->barwin);
  free(mon);
}

void clientmessage(XEvent *e) {
  XClientMessageEvent *cme = &e->xclient;
  Client *c = wintoclient(cme->window);

  if (!c)
    return;
  if (cme->message_type == netatom[NetWMState]) {
    if (cme->data.l[1] == netatom[NetWMFullscreen] ||
	cme->data.l[2] == netatom[NetWMFullscreen])
      setfullscreen(c, (cme->data.l[0] == 1 /* _NET_WM_STATE_ADD    */
			|| (cme->data.l[0] == 2 /* _NET_WM_STATE_TOGGLE */ &&
			    !c->isfullscreen)));
  } else if (cme->message_type == netatom[NetActiveWindow]) {
    if (c != selmon->sel && !c->isurgent) {
      // seturgent(c, 1);
      Arg a = {.ui = c->tags};
      selmon = c->mon;
      view(&a);
      focus(c);
      restack(selmon);
    }
  }
}

void configure(Client *c) {
  XConfigureEvent ce;

  ce.type = ConfigureNotify;
  ce.display = dpy;
  ce.event = c->win;
  ce.window = c->win;
  ce.x = c->x;
  ce.y = c->y;
  ce.width = c->w;
  ce.height = c->h;
  ce.border_width = c->bw;
  ce.above = None;
  ce.override_redirect = False;
  XSendEvent(dpy, c->win, False, StructureNotifyMask, (XEvent *)&ce);
}

void configurenotify(XEvent *e) {
  Monitor *m;
  Client *c;
  XConfigureEvent *ev = &e->xconfigure;
  int dirty;

  /* TODO: updategeom handling sucks, needs to be simplified */
  if (ev->window == root) {
    dirty = (sw != ev->width || sh != ev->height);
    sw = ev->width;
    sh = ev->height;
    if (updategeom() || dirty) {
      // drw_resize(drw, sw, 0); // Can probably be removed
      updatebars();
      for (m = mons; m; m = m->next) {
	for (c = m->cl->clients; c; c = c->next)
	  if (c->isfullscreen)
	    resizeclient(c, m->mx, m->my, m->mw, m->mh);
      }
      focus(NULL);
      arrange(NULL);
    }
  }
}

void configurerequest(XEvent *e) {
  Client *c;
  Monitor *m;
  XConfigureRequestEvent *ev = &e->xconfigurerequest;
  XWindowChanges wc;

  if ((c = wintoclient(ev->window))) {
    if (ev->value_mask & CWBorderWidth)
      c->bw = ev->border_width;
    else if (c->isfloating || !selmon->lt[selmon->sellt]->arrange) {
      m = c->mon;
      if (ev->value_mask & CWX) {
	c->oldx = c->x;
	c->x = m->mx + ev->x;
      }
      if (ev->value_mask & CWY) {
	c->oldy = c->y;
	c->y = m->my + ev->y;
      }
      if (ev->value_mask & CWWidth) {
	c->oldw = c->w;
	c->w = ev->width;
      }
      if (ev->value_mask & CWHeight) {
	c->oldh = c->h;
	c->h = ev->height;
      }
      if ((c->x + c->w) > m->mx + m->mw && c->isfloating)
	c->x = m->mx + (m->mw / 2 - WIDTH(c) / 2); /* center in x direction */
      if ((c->y + c->h) > m->my + m->mh && c->isfloating)
	c->y = m->my + (m->mh / 2 - HEIGHT(c) / 2); /* center in y direction */
      if ((ev->value_mask & (CWX | CWY)) &&
	  !(ev->value_mask & (CWWidth | CWHeight)))
	configure(c);
      if (ISVISIBLE(c, m))
	XMoveResizeWindow(dpy, c->win, c->x, c->y, c->w, c->h);
    } else
      configure(c);
  } else {
    wc.x = ev->x;
    wc.y = ev->y;
    wc.width = ev->width;
    wc.height = ev->height;
    wc.border_width = ev->border_width;
    wc.sibling = ev->above;
    wc.stack_mode = ev->detail;
    XConfigureWindow(dpy, ev->window, ev->value_mask, &wc);
  }
  XSync(dpy, False);
}

Monitor *createmon(void) {
  Monitor *m, *tm;
  int i;

  /* bail out if the number of monitors exceeds the number of tags */
  for (i = 1, tm = mons; tm; i++, tm = tm->next)
    ;
  if (i > NUMTAGS) {
    fprintf(stderr, "dwm: failed to add monitor, number of tags exceeded\n");
    return NULL;
  }
  /* find the first tag that isn't in use */
  for (i = 0; i < NUMTAGS; i++) {
    for (tm = mons; tm && !(tm->tagset[tm->seltags] & (1 << i)); tm = tm->next)
      ;
    if (!tm)
      break;
  }
  /* reassign all tags to monitors since there's currently no free tag for the
   * new monitor */
  if (i >= NUMTAGS)
    for (i = 0, tm = mons; tm; tm = tm->next, i++) {
      tm->seltags ^= 1;
      tm->tagset[tm->seltags] = (1 << i) & TAGMASK;
    }

  m = ecalloc(1, sizeof(Monitor));
  m->cl = cl;
  m->tagset[0] = m->tagset[1] = (1 << i) & TAGMASK;
  m->mfact = mfact;
  m->nmaster = nmaster;
  m->lt[0] = &layouts[0];
  m->lt[1] = &layouts[1 % layouts_size];
  strncpy(m->ltsymbol, layouts[0].symbol, sizeof m->ltsymbol);
  m->pertag = pertagglist;

  return m;
}

void destroynotify(XEvent *e) {
  Client *c;
  XDestroyWindowEvent *ev = &e->xdestroywindow;

  if ((c = wintoclient(ev->window)))
    unmanage(c, 1);

  else if ((c = swallowingclient(ev->window)))
    unmanage(c->swallowing, 1);
}

void detach(Client *c) {
  Client **tc;

  for (tc = &c->mon->cl->clients; *tc && *tc != c; tc = &(*tc)->next)
    ;
  *tc = c->next;
}

void detachstack(Client *c) {
  Client **tc, *t;

  for (tc = &c->mon->cl->stack; *tc && *tc != c; tc = &(*tc)->snext)
    ;
  *tc = c->snext;

  if (c == c->mon->sel) {
    for (t = c->mon->cl->stack; t && !ISVISIBLE(t, c->mon); t = t->snext)
      ;
    c->mon->sel = t;
  }
}

Monitor *dirtomon(int dir) {
  Monitor *m = NULL;

  if (dir > 0) {
    if (!(m = selmon->next))
      m = mons;
  } else if (selmon == mons)
    for (m = mons; m->next; m = m->next)
      ;
  else
    for (m = mons; m->next != selmon; m = m->next)
      ;
  return m;
}

void enternotify(XEvent *e) {
  Client *c;
  Monitor *m;
  XCrossingEvent *ev = &e->xcrossing;

  if ((ev->mode != NotifyNormal || ev->detail == NotifyInferior) &&
      ev->window != root)
    return;
  c = wintoclient(ev->window);
  m = c ? c->mon : wintomon(ev->window);
  if (m != selmon) {
    unfocus(selmon->sel, 1);
    selmon = m;
  } else if (!c || c == selmon->sel)
    return;
  // Don't change focus if we are in Monocle view and "focus" a different window
  // (This can sometimes happen with GTK window borders which pass the pointer through)
  else if (selmon->lt[selmon->sellt]->arrange == monocle && !selmon->sel->isfloating && !c->isfloating)
    return;
  focus(c);
  restack_nowarp(selmon);
}

void focus(Client *c) {
  if (!c || !ISVISIBLE(c, selmon))
    for (c = selmon->cl->stack; c && !ISVISIBLE(c, selmon); c = c->snext)
      ;
  if (selmon->sel && selmon->sel != c)
    unfocus(selmon->sel, 0);
  if (c) {
    if (c->mon != selmon)
      selmon = c->mon;
    if (c->isurgent)
      seturgent(c, 0);
    detachstack(c);
    attachstack(c);
    grabbuttons(c, 1);
    XSetWindowBorder(dpy, c->win, scheme[SchemeSel][ColBorder].pixel);
    setfocus(c);
  } else {
    XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
    XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
  }
  selmon->sel = c;
}

/* there are some broken focus acquiring clients needing extra handling */
void focusin(XEvent *e) {
  XFocusChangeEvent *ev = &e->xfocus;

  if (selmon->sel && ev->window != selmon->sel->win)
    setfocus(selmon->sel);
}

void focusmon(const Arg *arg) {
  Monitor *m;

  if (!mons->next)
    return;
  if ((m = dirtomon(arg->i)) == selmon)
    return;
  unfocus(selmon->sel, 0);
  selmon = m;
  focus(NULL);
  warp(selmon->sel);
}

void focusstack(const Arg *arg) {
  Client *c = NULL, *i;

  if (!selmon->sel)
    return;
  if (arg->i > 0) {
    for (c = selmon->sel->next; c && !ISVISIBLE(c, selmon); c = c->next)
      ;
    if (!c)
      for (c = selmon->cl->clients; c && !ISVISIBLE(c, selmon); c = c->next)
	;
  } else {
    for (i = selmon->cl->clients; i != selmon->sel; i = i->next)
      if (ISVISIBLE(i, selmon))
	c = i;
    if (!c)
      for (; i; i = i->next)
	if (ISVISIBLE(i, selmon))
	  c = i;
  }
  if (c) {
    focus(c);
    restack(selmon);
  }
}

Atom getatomprop(Client *c, Atom prop, int num) {
  int di;
  unsigned long dl1, dl2;
  unsigned char *p = NULL;
  Atom da, atom = None;

  if (XGetWindowProperty(dpy, c->win, prop, 0L, sizeof atom, False, XA_ATOM,
			 &da, &di, &dl1, &dl2, &p) == Success &&
      p) {
    if (num < dl1) {
      atom = ((Atom *)p)[num];
    }
    XFree(p);
  }
  return atom;
}

int getrootptr(int *x, int *y) {
  int di;
  unsigned int dui;
  Window dummy;

  return XQueryPointer(dpy, root, &dummy, &dummy, x, y, &di, &di, &dui);
}

long getstate(Window w) {
  int format;
  long result = -1;
  unsigned char *p = NULL;
  unsigned long n, extra;
  Atom real;

  if (XGetWindowProperty(dpy, w, wmatom[WMState], 0L, 2L, False,
			 wmatom[WMState], &real, &format, &n, &extra,
			 (unsigned char **)&p) != Success)
    return -1;
  if (n != 0)
    result = *p;
  XFree(p);
  return result;
}

int gettextprop(Window w, Atom atom, char *text, unsigned int size) {
  char **list = NULL;
  int n;
  XTextProperty name;

  if (!text || size == 0)
    return 0;
  text[0] = '\0';
  if (!XGetTextProperty(dpy, w, &name, atom) || !name.nitems)
    return 0;
  if (name.encoding == XA_STRING)
    strncpy(text, (char *)name.value, size - 1);
  else {
    if (XmbTextPropertyToTextList(dpy, &name, &list, &n) >= Success && n > 0 &&
	*list) {
      strncpy(text, *list, size - 1);
      XFreeStringList(list);
    }
  }
  text[size - 1] = '\0';
  XFree(name.value);
  return 1;
}

void grabbuttons(Client *c, int focused) {
  updatenumlockmask();
  {
    unsigned int i, j;
    unsigned int modifiers[] = {0, LockMask, numlockmask,
				numlockmask | LockMask};
    XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
    if (!focused)
      XGrabButton(dpy, AnyButton, AnyModifier, c->win, False, BUTTONMASK,
		  GrabModeSync, GrabModeSync, None, None);
    for (i = 0; i < buttons_size; i++)
      if (buttons[i].click == ClkClientWin)
	for (j = 0; j < LENGTH(modifiers); j++)
	  XGrabButton(dpy, buttons[i].button, buttons[i].mask | modifiers[j],
		      c->win, False, BUTTONMASK, GrabModeAsync, GrabModeSync,
		      None, None);
  }
}

void grabkeys(void) {
  updatenumlockmask();
  {
    unsigned int i, j;
    unsigned int modifiers[] = {0, LockMask, numlockmask,
				numlockmask | LockMask};
    KeyCode code;

    XUngrabKey(dpy, AnyKey, AnyModifier, root);
    for (i = 0; i < keys_size; i++)
      if ((code = XKeysymToKeycode(dpy, keys[i].keysym)))
	for (j = 0; j < LENGTH(modifiers); j++)
	  XGrabKey(dpy, code, keys[i].mod | modifiers[j], root, True,
		   GrabModeAsync, GrabModeAsync);
  }
}

void incnmaster(const Arg *arg) {
  unsigned int i;
  int n;
  Client *c;

  for (n = 0, c = nexttiled(selmon->cl->clients, selmon); c;
       c = nexttiled(c->next, selmon), n++)
    ;

  if (arg->i == 0)
    selmon->nmaster = nmaster;
  else if (selmon->nmaster + arg->i > n)
    selmon->nmaster = n;
  else if (selmon->nmaster + arg->i < 0)
    selmon->nmaster = 0;
  else
    selmon->nmaster += arg->i;

  for (i = 0; i <= NUMTAGS; ++i)
    if (selmon->tagset[selmon->seltags] & 1 << i)
      selmon->pertag->nmasters[(i + 1) % (NUMTAGS + 1)] = selmon->nmaster;
  arrange(selmon);
}

#ifdef XINERAMA
static int isuniquegeom(XineramaScreenInfo *unique, size_t n,
			XineramaScreenInfo *info) {
  while (n--)
    if (unique[n].x_org == info->x_org && unique[n].y_org == info->y_org &&
	unique[n].width == info->width && unique[n].height == info->height)
      return 0;
  return 1;
}
#endif /* XINERAMA */

void keypress(XEvent *e) {
  unsigned int i;
  KeySym keysym;
  XKeyEvent *ev;

  ev = &e->xkey;
  keysym = XKeycodeToKeysym(dpy, (KeyCode)ev->keycode, 0);
  for (i = 0; i < keys_size; i++)
    if (keysym == keys[i].keysym &&
	CLEANMASK(keys[i].mod) == CLEANMASK(ev->state) && keys[i].func)
      keys[i].func(&(keys[i].arg));
}

int fake_signal(void) {
  char fsignal[256];
  char indicator[9] = "fsignal:";
  char str_sig[50];
  char param[16];
  int i, len_str_sig, n, paramn;
  size_t len_fsignal, len_indicator = strlen(indicator);
  Arg arg;

  // Get root name property
  if (gettextprop(root, XA_WM_NAME, fsignal, sizeof(fsignal))) {
    len_fsignal = strlen(fsignal);

    // Check if this is indeed a fake signal
    if (len_indicator > len_fsignal
	    ? 0
	    : strncmp(indicator, fsignal, len_indicator) == 0) {
      paramn = sscanf(fsignal + len_indicator, "%s%n%s%n", str_sig,
		      &len_str_sig, param, &n);

      if (paramn == 1)
	arg = (Arg){0};
      else if (paramn > 2)
	return 1;
      else if (strncmp(param, "i", n - len_str_sig) == 0)
	sscanf(fsignal + len_indicator + n, "%i", &(arg.i));
      else if (strncmp(param, "ui", n - len_str_sig) == 0)
	sscanf(fsignal + len_indicator + n, "%u", &(arg.ui));
      else if (strncmp(param, "f", n - len_str_sig) == 0)
	sscanf(fsignal + len_indicator + n, "%f", &(arg.f));
      else
	return 1;

      // Check if a signal was found, and if so handle it
      for (i = 0; i < signals_size; i++)
	if (strncmp(str_sig, signals[i].sig, len_str_sig) == 0 &&
	    signals[i].func) {
	  signals[i].func(&(arg));
	  return 1;
	}

      // A fake signal was sent
      return 1;
    }
  }

  // No fake signal was sent, so proceed with update
  return 0;
}

void killclient(const Arg *arg) {
  if (!selmon->sel)
    return;
  if (!sendevent(selmon->sel, wmatom[WMDelete])) {
    XGrabServer(dpy);
    XSetErrorHandler(xerrordummy);
    XSetCloseDownMode(dpy, DestroyAll);
    XKillClient(dpy, selmon->sel->win);
    XSync(dpy, False);
    XSetErrorHandler(xerror);
    XUngrabServer(dpy);
  }
}

void loadxrdb() {
  Display *display;
  char *resm;
  XrmDatabase xrdb;
  char *type;
  XrmValue value;

  display = XOpenDisplay(NULL);

  if (display != NULL) {
    resm = XResourceManagerString(display);

    if (resm != NULL) {
      xrdb = XrmGetStringDatabase(resm);

      if (xrdb != NULL) {
	XRDB_LOAD_COLOR("dwm.color4", col_brd_sel);
	XRDB_LOAD_COLOR("dwm.color2", col_brd_norm);
      }
    }
  }

  XCloseDisplay(display);
}

void manage(Window w, XWindowAttributes *wa) {
  Client *c, *t = NULL, *term = NULL;
  Window trans = None;
  XWindowChanges wc;

  c = ecalloc(1, sizeof(Client));
  c->win = w;
  c->pid = winpid(w);
  /* geometry */
  c->x = c->oldx = wa->x;
  c->y = c->oldy = wa->y;
  c->w = c->oldw = wa->width;
  c->h = c->oldh = wa->height;
  c->oldbw = wa->border_width;

  updatetitle(c);
  if (XGetTransientForHint(dpy, w, &trans) && (t = wintoclient(trans))) {
    c->mon = t->mon;
    c->tags = t->tags;
  } else {
    c->mon = selmon;
    applyrules(c);
    term = termforwin(c);
  }

  if (c->x == c->mon->mx && c->y == c->mon->my) {
    // if window is spawned in upper left corner (when no coordinates are set explicitly), center it
    c->x = c->mon->mx + c->mon->mw/2 - (WIDTH(c) - 2*gappx)/2;
    c->y = c->mon->my + c->mon->mh/2 - (HEIGHT(c) - 2*gappx)/2;
  }
  
  // Move window, if parts are offscreen 
  if (c->x + WIDTH(c) > c->mon->mx + c->mon->mw)
    c->x = c->mon->mx + c->mon->mw - WIDTH(c);
  if (c->y + HEIGHT(c) > c->mon->my + c->mon->mh)
    c->y = c->mon->my + c->mon->mh - HEIGHT(c);
  c->x = MAX(c->x, c->mon->mx);
  c->y = MAX(c->y, c->mon->my);
  c->bw = borderpx;

  wc.border_width = c->bw;
  XConfigureWindow(dpy, w, CWBorderWidth, &wc);
  XSetWindowBorder(dpy, w, scheme[SchemeNorm][ColBorder].pixel);
  configure(c); /* propagates border_width, if size doesn't change */
  updatewindowtype(c);
  updatesizehints(c);
  updatewmhints(c);
  XSelectInput(dpy, w,
	       EnterWindowMask | FocusChangeMask | PropertyChangeMask |
		   StructureNotifyMask);
  grabbuttons(c, 0);
  if (!c->isfloating)
    c->isfloating = c->oldstate = trans != None || c->isfixed;
  if (c->isfloating)
    XRaiseWindow(dpy, c->win);
  attach(c);
  attachstack(c);
  XChangeProperty(dpy, root, netatom[NetClientList], XA_WINDOW, 32,
		  PropModeAppend, (unsigned char *)&(c->win), 1);
  XMoveResizeWindow(dpy, c->win, c->x + 2 * sw, c->y, c->w,
		    c->h); /* some windows require this */
  setclientstate(c, NormalState);
  if (c->mon == selmon)
    unfocus(selmon->sel, 0);
  c->mon->sel = c;

  /* Save window, to keep it save from changes made in swallow. */
  Window tw = c->win;
  if (term)
    swallow(term, c);

  arrange(c->mon);

  /* Needed here, when the window is floating etc. */
  roundcornersclient(c);

  XMapWindow(dpy, tw); /* Make the window visible */
  focus(NULL);
  spawnbarupdate();
}

void mappingnotify(XEvent *e) {
  XMappingEvent *ev = &e->xmapping;

  XRefreshKeyboardMapping(ev);
  if (ev->request == MappingKeyboard)
    grabkeys();
}

void maprequest(XEvent *e) {
  static XWindowAttributes wa;
  XMapRequestEvent *ev = &e->xmaprequest;

  if (!XGetWindowAttributes(dpy, ev->window, &wa))
    return;
  if (wa.override_redirect)
    return;
  if (!wintoclient(ev->window))
    manage(ev->window, &wa);
}

void motionnotify(XEvent *e) {
  static Monitor *mon = NULL;
  Monitor *m;
  XMotionEvent *ev = &e->xmotion;

  if (ev->window != root)
    return;
  if ((m = recttomon(ev->x_root, ev->y_root, 1, 1)) != mon && mon) {
    unfocus(selmon->sel, 1);
    selmon = m;
    focus(NULL);
  }
  mon = m;
}

void movemouse(const Arg *arg) {
  int x, y, ocx, ocy, nx, ny;
  Client *c;
  Monitor *m;
  XEvent ev;
  Time lasttime = 0;

  if (!(c = selmon->sel))
    return;
  if (c->isfullscreen) /* no support moving fullscreen windows by mouse */
    return;
  restack(selmon);
  ocx = c->x;
  ocy = c->y;
  if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
		   None, cursor[CurMove]->cursor, CurrentTime) != GrabSuccess)
    return;
  if (!getrootptr(&x, &y))
    return;
  do {
    XMaskEvent(dpy, MOUSEMASK | ExposureMask | SubstructureRedirectMask, &ev);
    switch (ev.type) {
    case ConfigureRequest:
    case MapRequest:
      handler[ev.type](&ev);
      break;
    case MotionNotify:
      if ((ev.xmotion.time - lasttime) <= (1000 / 60))
	continue;
      lasttime = ev.xmotion.time;

      nx = ocx + (ev.xmotion.x - x);
      ny = ocy + (ev.xmotion.y - y);
      if (abs(selmon->wx - nx) < snap)
	nx = selmon->wx;
      else if (abs((selmon->wx + selmon->ww) - (nx + WIDTH(c) - 2 * gappx)) <
	       snap)
	nx = selmon->wx + selmon->ww - WIDTH(c) + 2 * gappx;
      if (abs(selmon->wy - ny) < snap)
	ny = selmon->wy;
      else if (abs((selmon->wy + selmon->wh) - (ny + HEIGHT(c) - 2 * gappx)) <
	       snap)
	ny = selmon->wy + selmon->wh - HEIGHT(c) + 2 * gappx;
      if (!c->isfloating && selmon->lt[selmon->sellt]->arrange &&
	  (abs(nx - c->x) > snap || abs(ny - c->y) > snap))
	togglefloating(NULL);
      if (!selmon->lt[selmon->sellt]->arrange || c->isfloating)
	resize(c, nx, ny, c->w, c->h, 1, 0);
      break;
    }
  } while (ev.type != ButtonRelease);
  XUngrabPointer(dpy, CurrentTime);
  if ((m = recttomon(c->x, c->y, c->w, c->h)) != selmon) {
    sendmon(c, m);
    selmon = m;
    focus(NULL);
  }
}

Client *nexttiled(Client *c, Monitor *m) {
  for (; c && (c->isfloating || !ISVISIBLE(c, m)); c = c->next)
    ;
  return c;
}

void overview(const Arg *arg) {
  if (overviewmode == 0 && selmon->tagset[selmon->seltags] != TAGMASK) {
    if (overviewlayout)
      pertagglist->ltidxs[0][pertagglist->sellts[0]] = overviewlayout;
    const Arg viewarg = {.ui = ~0};
    view(&viewarg);
    overviewmode = 1;
  } else {
    viewselected(NULL);
    overviewmode = 0;
  }
}

void pop(Client *c) {
  detach(c);
  attach(c);
  focus(c);
  arrange(c->mon);
}

void propertynotify(XEvent *e) {
  Client *c;
  Window trans;
  XPropertyEvent *ev = &e->xproperty;

  if ((ev->window == root) && (ev->atom == XA_WM_NAME)) {
    fake_signal();
  } else if (ev->state == PropertyDelete)
    return; /* ignore */
  else if ((c = wintoclient(ev->window))) {
    switch (ev->atom) {
    default:
      break;
    case XA_WM_TRANSIENT_FOR:
      if (!c->isfloating && (XGetTransientForHint(dpy, c->win, &trans)) &&
	  (c->isfloating = (wintoclient(trans)) != NULL))
	arrange(c->mon);
      break;
    case XA_WM_NORMAL_HINTS:
      updatesizehints(c);
      break;
    case XA_WM_HINTS:
      updatewmhints(c);
      break;
    }
    if (ev->atom == XA_WM_NAME || ev->atom == netatom[NetWMName]) {
      updatetitle(c);
    }
    if (ev->atom == netatom[NetWMWindowType])
      updatewindowtype(c);
  }
}

void quit(const Arg *arg) {
    if (arg->i)
        restart = 1;
    running = 0;
}

void *querysocket_listen(void *arg) {
  pthread_t thread;
  int socketfd;
  int *socketfd_arg;

  while (running) { /* Wait for socket connections and handle them. */
    if ((socketfd = accept(querysocket, NULL, NULL)) == -1) {
      fprintf(stderr, "Could not connect to new socket. Errno: %d\n", errno);
      continue;
    }

    /* Save File Descriptor on Heap, to savely bring it to the new thread. */
    if ((socketfd_arg = malloc(sizeof(socketfd_arg))) == NULL) {
      fprintf(stderr, "Could not malloc Query Argument. Errno: %d\n", errno);
      continue;
    }
    *socketfd_arg = socketfd;

    if (pthread_create(&thread, NULL, querysocket_execute, socketfd_arg) != 0) {
      fprintf(stderr, "Could not create Query Execution Thread. Errno: %d\n",
	      errno);
      close(socketfd);
      free(socketfd_arg);
      continue;
    }
    pthread_detach(thread);
  }

  return NULL;
}

void *querysocket_execute(void *arg) {
  int fd = *((int *)arg);
  free(arg);

  int i, used;
  int res = 1;
  int (*qfunc)(char *, char *) = NULL;
  char inputBuf[MAXBUFF_SOCKET];
  char outputBuf[MAXBUFF_SOCKET];
  char funcname[MAXBUFF_SOCKET];

  memset(outputBuf, '\0', MAXBUFF_SOCKET);

  if (recv(fd, inputBuf, sizeof(inputBuf), 0) <= 0) {
    strncpy(outputBuf, "Did not recieve any bytes.", MAXBUFF_SOCKET);
    goto QueryEnd;
  }
  inputBuf[MAXBUFF_SOCKET - 1] = '\0'; /* Make sure the String ends. */

  if (sscanf(inputBuf, "%s %n", funcname, &used) != 1) {
    strncpy(outputBuf, "Could not read function name.", MAXBUFF_SOCKET);
    goto QueryEnd;
  }

  for (i = 0; i < query_funcs_size; i++) {
    if (strncmp(funcname, query_funcs[i].name, used) == 0) {
      qfunc = query_funcs[i].func;
      break;
    }
  }
  if (qfunc == NULL) {
    strncpy(outputBuf, "Could not find function.", MAXBUFF_SOCKET);
    goto QueryEnd;
  }

  res = qfunc(&inputBuf[used], outputBuf);
  outputBuf[MAXBUFF_SOCKET - 1] = '\0';

QueryEnd:
  send(fd, &res, sizeof(res), 0);
  send(fd, outputBuf, sizeof(outputBuf), 0);

  close(fd);
  return NULL;
}

Monitor *recttomon(int x, int y, int w, int h) {
  Monitor *m, *r = selmon;
  int a, area = 0;

  for (m = mons; m; m = m->next)
    if ((a = INTERSECT(x, y, w, h, m)) > area) {
      area = a;
      r = m;
    }
  return r;
}

void resize(Client *c, int x, int y, int w, int h, int interact, int animate) {
  unsigned int currgap, n;
  Client *nbc;
  Monitor *m = c->mon;

  /* Get number of clients for the selected monitor */
  for (n = 0, nbc = nexttiled(m->cl->clients, m); nbc;
       nbc = nexttiled(nbc->next, m), n++)
    ;

  /* Calculate Gaps */
  if (c->isfloating || interact || m->lt[m->sellt]->arrange == NULL) {
    currgap = 0;
  } else {
    if (m->lt[m->sellt]->arrange == monocle || n == 1) {
      /* This code is executed, when we have the monocle layout or just one
       * client. */
      currgap = gappx;
      // c.bw = 0;
    } else {
      currgap = gappx;
    }
  }

  x += currgap;
  y += currgap;
  w -= currgap * 2;
  h -= currgap * 2;

  if (applysizehints(c, &x, &y, &w, &h, interact)) {
    if (animate && c->animate && useanimation && !interact && startupdone &&
	running && !tempdisableanimation) {
      animateclient_start(c, x, y, w, h);
    } else {
      resizeclient(c, x, y, w, h);
    }
  }

  c->goalx = x;
  c->goaly = y;
  c->goalh = h;
  c->goalw = w;
}

void resizeclient(Client *c, int x, int y, int w, int h) {
  XWindowChanges wc;
  wc.border_width = c->bw;

  c->oldx = c->x;
  c->x = wc.x = x;
  c->oldy = c->y;
  c->y = wc.y = y;
  c->oldw = c->w;
  c->w = wc.width = w;
  c->oldh = c->h;
  c->h = wc.height = h;

  XConfigureWindow(dpy, c->win, CWX | CWY | CWWidth | CWHeight | CWBorderWidth,
		   &wc);
  configure(c);
  XSync(dpy, False);

  roundcornersclient(c);
}

void resizemouse(const Arg *arg) {
  int ocx, ocy, nw, nh;
  int ocx2, ocy2, nx, ny;
  Client *c;
  Monitor *m;
  XEvent ev;
  int horizcorner, vertcorner;
  int di;
  unsigned int dui;
  Window dummy;
  Time lasttime = 0;

  if (!(c = selmon->sel))
    return;
  if (c->isfullscreen) /* no support resizing fullscreen windows by mouse */
    return;
  restack(selmon);
  ocx = c->x;
  ocy = c->y;
  ocx2 = c->x + c->w;
  ocy2 = c->y + c->h;
  if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
		   None, cursor[CurResize]->cursor, CurrentTime) != GrabSuccess)
    return;
  if (!XQueryPointer(dpy, c->win, &dummy, &dummy, &di, &di, &nx, &ny, &dui))
    return;
  horizcorner = nx < c->w / 2;
  vertcorner = ny < c->h / 2;
  XWarpPointer(dpy, None, c->win, 0, 0, 0, 0,
	       horizcorner ? (-c->bw) : (c->w + c->bw - 1),
	       vertcorner ? (-c->bw) : (c->h + c->bw - 1));

  do {
    XMaskEvent(dpy, MOUSEMASK | ExposureMask | SubstructureRedirectMask, &ev);
    switch (ev.type) {
    case ConfigureRequest:
    case MapRequest:
      handler[ev.type](&ev);
      break;
    case MotionNotify:
      if ((ev.xmotion.time - lasttime) <= (1000 / 60))
	continue;
      lasttime = ev.xmotion.time;

      nw = MAX(ev.xmotion.x - ocx - 2 * c->bw + 1, 1);
      nh = MAX(ev.xmotion.y - ocy - 2 * c->bw + 1, 1);
      nx = horizcorner ? ev.xmotion.x : c->x;
      ny = vertcorner ? ev.xmotion.y : c->y;
      nw = MAX(horizcorner ? (ocx2 - nx) : (ev.xmotion.x - ocx - 2 * c->bw + 1),
	       1);
      nh = MAX(vertcorner ? (ocy2 - ny) : (ev.xmotion.y - ocy - 2 * c->bw + 1),
	       1);

      if (c->mon->wx + nw >= selmon->wx &&
	  c->mon->wx + nw <= selmon->wx + selmon->ww &&
	  c->mon->wy + nh >= selmon->wy &&
	  c->mon->wy + nh <= selmon->wy + selmon->wh) {
	if (!c->isfloating && selmon->lt[selmon->sellt]->arrange &&
	    (abs(nw - c->w) > snap || abs(nh - c->h) > snap))
	  togglefloating(NULL);
      }
      if (!selmon->lt[selmon->sellt]->arrange || c->isfloating)
	resize(c, nx, ny, nw, nh, 1, 0);
      break;
    }
  } while (ev.type != ButtonRelease);
  XWarpPointer(dpy, None, c->win, 0, 0, 0, 0,
	       horizcorner ? (-c->bw) : (c->w + c->bw - 1),
	       vertcorner ? (-c->bw) : (c->h + c->bw - 1));
  XUngrabPointer(dpy, CurrentTime);
  while (XCheckMaskEvent(dpy, EnterWindowMask, &ev))
    ;
  if ((m = recttomon(c->x, c->y, c->w, c->h)) != selmon) {
    sendmon(c, m);
    selmon = m;
    focus(NULL);
  }
}

void restack(Monitor *m) {
  restack_nowarp(m);
  if (m && m == selmon && m->sel && /* TODO: Optimize the rest of the line */
      (m->tagset[m->seltags] & m->sel->tags) &&
      selmon->lt[selmon->sellt] != &layouts[2] /* Monocle */)
    warp(m->sel);
}

void restack_nowarp(Monitor *m) {
  Client *c;
  XEvent ev;
  XWindowChanges wc;

  if (!m->sel)
    return;
  if (m->sel->isfloating || !m->lt[m->sellt]->arrange)
    XRaiseWindow(dpy, m->sel->win);
  if (m->lt[m->sellt]->arrange) {
    wc.stack_mode = Below;
    wc.sibling = m->barwin;
    for (c = m->cl->stack; c; c = c->snext)
      if (!c->isfloating && ISVISIBLE(c, m)) {
	XConfigureWindow(dpy, c->win, CWSibling | CWStackMode, &wc);
	wc.sibling = c->win;
      }
  }
  XSync(dpy, False);
  while (XCheckMaskEvent(dpy, EnterWindowMask, &ev))
    ;
}

void roundcornersclient(Client *c) {
  Pixmap mask;
  GC shapegc;

  if (!cornerradius) {
    return;
  }

  // if (!c || c->isfullscreen)
  if (!c || !(c->win) || c->hasroundcorners == 0)
    return;

  // TODO: Cleanup
  XGrabServer(dpy);
  // XSync(dpy, False);
  // XSetErrorhandler(xerrordummy);

  /* Create Border Mask */
  if (createroundcornermask(&mask, &shapegc, c->win, c->w + 2 * c->bw,
			    c->h + 2 * c->bw,
			    c->isfullscreen ? 0 : cornerradius + c->bw) == 0) {
    XShapeCombineMask(dpy, c->win, ShapeBounding, -c->bw, -c->bw, mask,
		      ShapeSet);
    XFreePixmap(dpy, mask);
    XFreeGC(dpy, shapegc);
  }

  /* Create Clip Mask */
  if (createroundcornermask(&mask, &shapegc, c->win, c->w, c->h,
			    c->isfullscreen ? 0 : cornerradius) == 0) {
    XShapeCombineMask(dpy, c->win, ShapeClip, 0, 0, mask, ShapeSet);
    XFreePixmap(dpy, mask);
    XFreeGC(dpy, shapegc);
  }

  XSync(dpy, False);
  // XSetErrorhandler(xerror);
  XUngrabServer(dpy);
}

int createroundcornermask(Pixmap *maskP, GC *shapegcP, Window win, int w, int h,
			  int radius) {
  Pixmap mask;
  GC shapegc;
  XWindowAttributes xwa;
  int diam = 2 * radius;

  if (radius < 0)
    return 1;

  // Test and return immediatly, if the window does not exist anymore.
  if (!(XGetWindowAttributes(dpy, win, &xwa)))
    return 1;

  if (!(mask = XCreatePixmap(dpy, win, w, h, 1)))
    return 1;

  if (!(shapegc = XCreateGC(dpy, mask, 0, NULL))) {
    XFreePixmap(dpy, mask);
    return 1;
  }

  *maskP = mask;
  *shapegcP = shapegc;

  XFillRectangle(dpy, mask, shapegc, 0, 0, w, h);
  XSetForeground(dpy, shapegc, 1);

  if (radius == 0 || w < diam || h < diam) {
    XFillRectangle(dpy, mask, shapegc, 0, 0, w, h);
    return 0;
  }
  /* topleft, topright, bottomleft, bottomright
   * man XArc - positive is counterclockwise
   */
  XFillArc(dpy, mask, shapegc, 0, 0, diam, diam, 90 * 64, 90 * 64);
  XFillArc(dpy, mask, shapegc, w - diam - 1, 0, diam, diam, 0 * 64, 90 * 64);
  XFillArc(dpy, mask, shapegc, 0, h - diam - 1, diam, diam, -90 * 64, -90 * 64);
  XFillArc(dpy, mask, shapegc, w - diam - 1, h - diam - 1, diam, diam, 0 * 64,
	   -90 * 64);

  XFillRectangle(dpy, mask, shapegc, radius, 0, w - diam, h);
  XFillRectangle(dpy, mask, shapegc, 0, radius, w, h - diam);

  return 0;
}

void run(void) {
  XEvent ev;
  /* main event loop */
  XSync(dpy, False);
  while (running && !XNextEvent(dpy, &ev))
    if (handler[ev.type])
      handler[ev.type](&ev); /* call handler */
}

void scan(void) {
  scanner = 1;
  unsigned int i, num;
  char swin[256];
  Window d1, d2, *wins = NULL;
  XWindowAttributes wa;

  if (XQueryTree(dpy, root, &d1, &d2, &wins, &num)) {
    for (i = 0; i < num; i++) {
      if (!XGetWindowAttributes(dpy, wins[i], &wa) || wa.override_redirect ||
	  XGetTransientForHint(dpy, wins[i], &d1))
	continue;
      if (wa.map_state == IsViewable || getstate(wins[i]) == IconicState)
	manage(wins[i], &wa);
      else if (gettextprop(wins[i], netatom[NetClientList], swin, sizeof swin))
	manage(wins[i], &wa);
    }
    for (i = 0; i < num; i++) { /* now the transients */
      if (!XGetWindowAttributes(dpy, wins[i], &wa))
	continue;
      if (XGetTransientForHint(dpy, wins[i], &d1) &&
	  (wa.map_state == IsViewable || getstate(wins[i]) == IconicState))
	manage(wins[i], &wa);
    }
    if (wins)
      XFree(wins);
  }
  scanner = 0;
}

void sendmon(Client *c, Monitor *m) {
  int i;
  Monitor *tm;
  if (c->mon == m)
    return;
  unfocus(c, 1);
  detachstack(c);
  c->mon = m;
  if (!m->tagset[m->seltags]) { /* If Monitor has no tag, than give it one. */
    /* find the first tag that isn't in use */
    for (i = 0; i < NUMTAGS; i++) {
      for (tm = mons; tm && !(tm->tagset[tm->seltags] & (1 << i));
	   tm = tm->next)
	;
      if (!tm)
	break;
    }
    if (i >= NUMTAGS) {
      /* reassign all tags to monitors since there's currently no free tag for
       * the new monitor */
      for (i = 0, tm = mons; tm; tm = tm->next, i++) {
	tm->seltags ^= 1;
	tm->tagset[tm->seltags] = (1 << i) & TAGMASK;
      }
    } else {
      m->tagset[m->seltags] = (1 << i) & TAGMASK;
    }
  }

  c->tags = m->tagset[m->seltags]; /* assign tags of target monitor */

  attachstack(c);
  focus(NULL);
  arrange(NULL);
}

void setclientstate(Client *c, long state) {
  long data[] = {state, None};

  XChangeProperty(dpy, c->win, wmatom[WMState], wmatom[WMState], 32,
		  PropModeReplace, (unsigned char *)data, 2);
}
void setcurrentdesktop(void) {
  long data[] = {0};
  XChangeProperty(dpy, root, netatom[NetCurrentDesktop], XA_CARDINAL, 32,
		  PropModeReplace, (unsigned char *)data, 1);
}
void setdesktopnames(void) {
  // TODO: Generate Tag Names
  /* XTextProperty text; */
  /* Xutf8TextListToTextProperty(dpy, (char **) tags, TAGSLENGTH,
   * XUTF8StringStyle, &text); */
  /* XSetTextProperty(dpy, root, &text, netatom[NetDesktopNames]); */
}

int sendevent(Client *c, Atom proto) {
  int n;
  Atom *protocols;
  int exists = 0;
  XEvent ev;

  if (XGetWMProtocols(dpy, c->win, &protocols, &n)) {
    while (!exists && n--)
      exists = protocols[n] == proto;
    XFree(protocols);
  }
  if (exists) {
    ev.type = ClientMessage;
    ev.xclient.window = c->win;
    ev.xclient.message_type = wmatom[WMProtocols];
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = proto;
    ev.xclient.data.l[1] = CurrentTime;
    XSendEvent(dpy, c->win, False, NoEventMask, &ev);
  }
  return exists;
}

void setnumdesktops(void) {
  long data[] = {TAGSLENGTH};
  XChangeProperty(dpy, root, netatom[NetNumberOfDesktops], XA_CARDINAL, 32,
		  PropModeReplace, (unsigned char *)data, 1);
}

void setfocus(Client *c) {
  if (!c->neverfocus) {
    XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime);
    XChangeProperty(dpy, root, netatom[NetActiveWindow], XA_WINDOW, 32,
		    PropModeReplace, (unsigned char *)&(c->win), 1);
  }
  sendevent(c, wmatom[WMTakeFocus]);
}

void setfullscreen(Client *c, int fullscreen) {
  if (fullscreen && !c->isfullscreen) {
    XChangeProperty(dpy, c->win, netatom[NetWMState], XA_ATOM, 32,
		    PropModeReplace, (unsigned char *)&netatom[NetWMFullscreen],
		    1);
    c->isfullscreen = 1;
    c->oldstate = c->isfloating;
    c->oldbw = c->bw;
    c->bw = 0;
    c->isfloating = 1;
    resizeclient(c, c->mon->mx, c->mon->my, c->mon->mw, c->mon->mh);
    XRaiseWindow(dpy, c->win);
  } else if (!fullscreen && c->isfullscreen) {
    XChangeProperty(dpy, c->win, netatom[NetWMState], XA_ATOM, 32,
		    PropModeReplace, (unsigned char *)0, 0);
    c->isfullscreen = 0;
    c->isfloating = c->oldstate;
    c->bw = c->oldbw;
    c->x = c->oldx;
    c->y = c->oldy;
    c->w = c->oldw;
    c->h = c->oldh;
    resizeclient(c, c->x, c->y, c->w, c->h);
    arrange(c->mon);
  }
}

void setlayout(const Arg *arg) { setlayoutcustommonitor(arg, selmon); }

void setlayoutcustommonitor(const Arg *arg, Monitor *m) {
  unsigned int i;
  if (!m) {
    m = selmon;
  }

  if (!arg || !arg->v || arg->v != m->lt[m->sellt])
    m->sellt ^= 1;
  if (arg && arg->v)
    m->lt[m->sellt] = (Layout *)arg->v;
  strncpy(m->ltsymbol, m->lt[m->sellt]->symbol, sizeof m->ltsymbol);

  if (m->lt[m->sellt]->arrange == monocle) {
    strncpy(selmon->ltsymbol, "[0]", sizeof selmon->ltsymbol);
  }

  for (i = 0; i <= NUMTAGS; ++i)
    if (m->tagset[m->seltags] & 1 << i) {
      // Loop over all selected Tags
      m->pertag->ltidxs[(i + 1) % (NUMTAGS + 1)][m->sellt] = m->lt[m->sellt];
      m->pertag->sellts[(i + 1) % (NUMTAGS + 1)] = m->sellt;
    }

  if (m->pertag->curtag == 0) {
    m->pertag->sellts[0] ^= 1;
    m->pertag->ltidxs[0][selmon->pertag->sellts[0]] = m->lt[m->sellt];
  }

  if (m->sel)
    arrange(m);
  spawnbarupdate();
}

/* arg > 1.0 will set mfact absolutely */
void setmfact(const Arg *arg) {
  float f;
  unsigned int i;

  if (!arg || !selmon->lt[selmon->sellt]->arrange)
    return;
  f = arg->f < 1.0 ? arg->f + selmon->mfact : arg->f - 1.0;
  if (arg->f == 0.0)
    f = mfact;
  if (f < 0.05 || f > 0.95)
    return;
  selmon->mfact = f;
  for (i = 0; i <= NUMTAGS; ++i)
    if (selmon->tagset[selmon->seltags] & 1 << i)
      selmon->pertag->mfacts[(i + 1) % (NUMTAGS + 1)] = f;
  arrange(selmon);
}

void setgap(const Arg *arg) {
  int i = arg->i;

  if (i != 0) {
    if (gappx > -i)
      gappx += i;
    else
      gappx = 0;
  } else {
    gappx = gappx ? 0 : gappxdf;
  }

  arrange(NULL);
}

void setup(void) {
  int i;
  XSetWindowAttributes wa;
  Atom utf8string;
  struct sockaddr_un sockaddr;

  /* clean up any zombies immediately */
  sigchld(0);

  signal(SIGHUP, sighup);
  signal(SIGTERM, sigterm);

  /* init screen */
  screen = DefaultScreen(dpy);
  sw = DisplayWidth(dpy, screen);
  sh = DisplayHeight(dpy, screen);
  if (!(cl = (Clientlist *)calloc(1, sizeof(Clientlist))))
    die("fatal: could not malloc() %u bytes\n", sizeof(Clientlist));

  if (!(pertagglist = calloc(1, sizeof(Pertag))))
    die("fatal: could not malloc() %u bytes\n", sizeof(Pertag));
  pertagglist->curtag = pertagglist->prevtag = 1;

  for (i = 0; i <= NUMTAGS; i++) {
    pertagglist->nmasters[i] = nmaster;
    pertagglist->mfacts[i] = mfact;

    pertagglist->ltidxs[i][0] = &layouts[0];
    pertagglist->ltidxs[i][1] = &layouts[1 % layouts_size];
    pertagglist->sellts[i] = 0;
  }

  animatequeue = NULL;
  if (pthread_mutex_init(&animatemutex, NULL) != 0) {
    die("Could not create animation mutex.\n");
  }

  /* Setup Socket */
  if ((querysocket = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
    die("Could not create socket.\n");
  sockaddr.sun_family = AF_UNIX;
  strncpy(sockaddr.sun_path, SOCKET_PATH, sizeof(sockaddr.sun_path) - 1);
  if (unlink(SOCKET_PATH) != 0 && errno != ENOENT)
    die("Could not delete old socket.\n");
  if (bind(querysocket, (struct sockaddr *)&sockaddr, sizeof(sockaddr)) != 0)
    die("Could not bind socket.\n");
  if (listen(querysocket, BACKLOG) != 0)
    die("Unable to listen on socket.\n");
  /* Create new Thread to listen for incomming requests. */
  if (pthread_create(&querysocket_thread, NULL, querysocket_listen, NULL) != 0)
    die("Unable to create listening Thread.\n");

  gappx = gappxdf;

  root = RootWindow(dpy, screen);
  xinitvisual();
  drw = drw_create(dpy, screen, root, sw, sh, visual, depth, cmap);
  updategeom();
  /* init atoms */
  utf8string = XInternAtom(dpy, "UTF8_STRING", False);
  wmatom[WMProtocols] = XInternAtom(dpy, "WM_PROTOCOLS", False);
  wmatom[WMDelete] = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
  wmatom[WMState] = XInternAtom(dpy, "WM_STATE", False);
  wmatom[WMTakeFocus] = XInternAtom(dpy, "WM_TAKE_FOCUS", False);
  netatom[NetActiveWindow] = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
  netatom[NetSupported] = XInternAtom(dpy, "_NET_SUPPORTED", False);
  netatom[NetWMName] = XInternAtom(dpy, "_NET_WM_NAME", False);
  netatom[NetWMState] = XInternAtom(dpy, "_NET_WM_STATE", False);
  netatom[NetWMCheck] = XInternAtom(dpy, "_NET_SUPPORTING_WM_CHECK", False);
  netatom[NetWMFullscreen] =
      XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
  netatom[NetWMMaxVert] =
      XInternAtom(dpy, "_NET_WM_STATE_MAXIMIZED_VERT", False);
  netatom[NetWMMaxHorz] =
      XInternAtom(dpy, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
  netatom[NetWMWindowType] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
  netatom[NetWMWindowTypeDialog] =
      XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
  netatom[NetClientList] = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
  netatom[NetDesktopViewport] =
      XInternAtom(dpy, "_NET_DESKTOP_VIEWPORT", False);
  netatom[NetNumberOfDesktops] =
      XInternAtom(dpy, "_NET_NUMBER_OF_DESKTOPS", False);
  netatom[NetCurrentDesktop] = XInternAtom(dpy, "_NET_CURRENT_DESKTOP", False);
  netatom[NetDesktopNames] = XInternAtom(dpy, "_NET_DESKTOP_NAMES", False);
  /* init cursors */
  cursor[CurNormal] = drw_cur_create(drw, XC_left_ptr);
  cursor[CurResize] =
      drw_cur_create(drw, XC_sizing); // TODO: Change to better cursor
  cursor[CurMove] = drw_cur_create(drw, XC_fleur);
  /* init appearance */
  scheme = ecalloc(LENGTH(colors), sizeof(Clr *));
  for (i = 0; i < LENGTH(colors); i++)
    scheme[i] = drw_scm_create(drw, colors[i], alphas[i], 3);
  /* init bars */
  updatebars();
  /* supporting window for NetWMCheck */
  wmcheckwin = XCreateSimpleWindow(dpy, root, 0, 0, 1, 1, 0, 0, 0);
  XChangeProperty(dpy, wmcheckwin, netatom[NetWMCheck], XA_WINDOW, 32,
		  PropModeReplace, (unsigned char *)&wmcheckwin, 1);
  XChangeProperty(dpy, wmcheckwin, netatom[NetWMName], utf8string, 8,
		  PropModeReplace, (unsigned char *)"dwm", 3);
  XChangeProperty(dpy, root, netatom[NetWMCheck], XA_WINDOW, 32,
		  PropModeReplace, (unsigned char *)&wmcheckwin, 1);
  /* EWMH support per view */
  XChangeProperty(dpy, root, netatom[NetSupported], XA_ATOM, 32,
		  PropModeReplace, (unsigned char *)netatom, NetLast);
  setnumdesktops();
  setcurrentdesktop();
  setdesktopnames();
  setviewport();
  XDeleteProperty(dpy, root, netatom[NetClientList]);
  /* select events */
  wa.cursor = cursor[CurNormal]->cursor;
  wa.event_mask = SubstructureRedirectMask | SubstructureNotifyMask |
		  ButtonPressMask | PointerMotionMask | EnterWindowMask |
		  LeaveWindowMask | StructureNotifyMask | PropertyChangeMask;
  XChangeWindowAttributes(dpy, root, CWEventMask | CWCursor, &wa);
  XSelectInput(dpy, root, wa.event_mask);
  grabkeys();
  focus(NULL);
}
void setviewport(void) {
  long data[] = {0, 0};
  XChangeProperty(dpy, root, netatom[NetDesktopViewport], XA_CARDINAL, 32,
		  PropModeReplace, (unsigned char *)data, 2);
}

void seturgent(Client *c, int urg) {
  XWMHints *wmh;

  c->isurgent = urg;
  if (!(wmh = XGetWMHints(dpy, c->win)))
    return;
  wmh->flags = urg ? (wmh->flags | XUrgencyHint) : (wmh->flags & ~XUrgencyHint);
  XSetWMHints(dpy, c->win, wmh);
  XFree(wmh);
}

void showhide(Client *c) {
  if (!c)
    return;
  if (ISVISIBLE(c, c->mon)) {
    /* show clients top down */
    XMoveWindow(dpy, c->win, c->x, c->y);
    if ((!c->mon->lt[c->mon->sellt]->arrange || c->isfloating) &&
	!c->isfullscreen)
      resize(c, c->x, c->y, c->w, c->h, 0, 0);
    showhide(c->snext);
  } else {
    /* hide clients bottom up */
    showhide(c->snext);
    XMoveWindow(dpy, c->win, WIDTH(c) * -2, c->y);
  }
}

void sigchld(int unused) {
  if (signal(SIGCHLD, sigchld) == SIG_ERR)
    die("can't install SIGCHLD handler:");
  while (0 < waitpid(-1, NULL, WNOHANG))
    ;
}

void sighup(int unused) {
  Arg a = {.i = 1};
  quit(&a);
}

void sigterm(int unused) {
  Arg a = {.i = 0};
  quit(&a);
}

void spawn(const Arg *arg) {
  if (fork() == 0) {
    if (dpy)
      close(ConnectionNumber(dpy));
    setsid();
    execvp(((char **)arg->v)[0], (char **)arg->v);
    fprintf(stderr, "dwm: execvp %s", ((char **)arg->v)[0]);
    perror(" failed");
    exit(EXIT_SUCCESS);
  }
}

void spawnbarupdate() {
  const Arg a = {.v = barupdate_cmd};
  spawn(&a);
}

void tag(const Arg *arg) {
  Monitor *m;
  unsigned int newtags;
  if (selmon->sel && arg->ui & TAGMASK) {
    newtags = arg->ui & TAGMASK;
    for (m = mons; m; m = m->next)
      /* if tag is visible on another monitor, move client to the new monitor */
      if (m != selmon && m->tagset[m->seltags] & newtags) {
	/* prevent moving client to all tags (MODKEY-Shift-0) when multiple
	 * monitors are connected */
	if (newtags & selmon->tagset[selmon->seltags])
	  return;
	selmon->sel->tags = newtags;
	selmon->sel->mon = m;
	arrange(m);
	break;
      }
    /* workaround in case just one monitor is connected */
    selmon->sel->tags = arg->ui & TAGMASK;
    focus(NULL);
    arrange(selmon);
    spawnbarupdate();
  }
}

void tagmon(const Arg *arg) {
  if (!selmon->sel || !mons->next)
    return;
  sendmon(selmon->sel, dirtomon(arg->i));
}

void togglefloating(const Arg *arg) {
  if (!selmon->sel)
    return;
  if (selmon->sel->isfullscreen) /* no support for fullscreen windows */
    return;
  selmon->sel->isfloating = !selmon->sel->isfloating || selmon->sel->isfixed;
  if (selmon->sel->isfloating)
    resize(selmon->sel, selmon->sel->x, selmon->sel->y, selmon->sel->w,
	   selmon->sel->h, 0, 0);
  arrange(selmon);
}

void toggletag(const Arg *arg) {
  Monitor *m;
  unsigned int newtags;

  if (!selmon->sel)
    return;
  newtags = selmon->sel->tags ^ (arg->ui & TAGMASK);
  if (newtags) {
    /* prevent adding tags that are in use on other monitors */
    for (m = mons; m; m = m->next)
      if (m != selmon && newtags & m->tagset[m->seltags])
	return;
    selmon->sel->tags = newtags;
    focus(NULL);
    arrange(selmon);
    updatecurrentdesktop();
    spawnbarupdate();
  }
}

void toggleview(const Arg *arg) {
  Monitor *m;
  unsigned int newtagset =
      selmon->tagset[selmon->seltags] ^ (arg->ui & TAGMASK);
  int i;

  overviewmode = 0;

  /* prevent displaying the same tags on multiple monitors */
  for (m = mons; m; m = m->next)
    if (m != selmon && newtagset & m->tagset[m->seltags])
      return;

  selmon->tagset[selmon->seltags] = newtagset;

  if (newtagset) { /* Update Pertag List, when at least one tag is visible */
    if (newtagset == ~0) {
      selmon->pertag->prevtag = selmon->pertag->curtag;
      selmon->pertag->curtag = 0;
    }

    /* test if the user did not select the same tag */
    if (!(newtagset & 1 << (selmon->pertag->curtag - 1))) {
      selmon->pertag->prevtag = selmon->pertag->curtag;
      for (i = 0; !(newtagset & 1 << i); i++)
	;
      selmon->pertag->curtag = i + 1;
    }

    /* apply settings for this view */
    selmon->nmaster = selmon->pertag->nmasters[selmon->pertag->curtag];
    selmon->mfact = selmon->pertag->mfacts[selmon->pertag->curtag];
    selmon->sellt = selmon->pertag->sellts[selmon->pertag->curtag];
    selmon->lt[selmon->sellt] =
	selmon->pertag->ltidxs[selmon->pertag->curtag][selmon->sellt];
    selmon->lt[selmon->sellt ^ 1] =
	selmon->pertag->ltidxs[selmon->pertag->curtag][selmon->sellt ^ 1];
  }

  attachclients(selmon);
  focus(NULL);
  arrange(selmon);
  updatecurrentdesktop();
  spawnbarupdate();
}

void unfocus(Client *c, int setfocus) {
  if (!c)
    return;
  grabbuttons(c, 0);
  XSetWindowBorder(dpy, c->win, scheme[SchemeNorm][ColBorder].pixel);
  if (setfocus) {
    XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
    XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
  }
}

void unmanage(Client *c, int destroyed) {
  Monitor *m = c->mon;
  XWindowChanges wc;

  if (c->swallowing) {
    unswallow(c);
    return;
  }

  Client *s = swallowingclient(c->win);
  if (s) {
    free(s->swallowing);
    s->swallowing = NULL;
    arrange(m);
    focus(NULL);
    return;
  }

  detach(c);
  detachstack(c);
  if (!destroyed) {
    wc.border_width = c->oldbw;
    XGrabServer(dpy); /* avoid race conditions */
    XSetErrorHandler(xerrordummy);
    XConfigureWindow(dpy, c->win, CWBorderWidth, &wc); /* restore border */
    XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
    setclientstate(c, WithdrawnState);
    XSync(dpy, False);
    XSetErrorHandler(xerror);
    XUngrabServer(dpy);
  }
  free(c);

  if (!s) {
    arrange(m);
    focus(NULL);
    updateclientlist();
  }

  spawnbarupdate();
}

void unmapnotify(XEvent *e) {
  Client *c;
  XUnmapEvent *ev = &e->xunmap;

  if ((c = wintoclient(ev->window))) {
    if (ev->send_event)
      setclientstate(c, WithdrawnState);
    else
      unmanage(c, 0);
  }
}

void updatebars(void) {
  Monitor *m;
  XSetWindowAttributes wa = {.override_redirect = True,
			     .background_pixel = 0,
			     .border_pixel = 0,
			     .colormap = cmap,
			     .event_mask = ButtonPressMask | ExposureMask};
  XClassHint ch = {"dwm", "dwm"};
  for (m = mons; m; m = m->next) {
    if (m->barwin)
      continue;
    m->barwin = XCreateWindow(dpy, root, m->wx, 0, m->ww, 1, 0, depth,
			      InputOutput, visual,
			      CWOverrideRedirect | CWBackPixel | CWBorderPixel |
				  CWColormap | CWEventMask,
			      &wa);
    XMapRaised(dpy, m->barwin);
    XSetClassHint(dpy, m->barwin, &ch);
  }
}

void updateclientlist() {
  Client *c;
  Monitor *m;

  XDeleteProperty(dpy, root, netatom[NetClientList]);
  for (m = mons; m; m = m->next)
    for (c = m->cl->clients; c; c = c->next)
      XChangeProperty(dpy, root, netatom[NetClientList], XA_WINDOW, 32,
		      PropModeAppend, (unsigned char *)&(c->win), 1);
}

void updatecurrentdesktop(void) {
  long rawdata[] = {selmon->tagset[selmon->seltags]};
  int i = 0;
  while (*rawdata >> (i + 1)) {
    i++;
  }
  long data[] = {i};
  XChangeProperty(dpy, root, netatom[NetCurrentDesktop], XA_CARDINAL, 32,
		  PropModeReplace, (unsigned char *)data, 1);
}

int updategeom(void) {
  int dirty = 0;

#ifdef XINERAMA
  if (XineramaIsActive(dpy)) {
    int i, j, n, nn;
    Client *c;
    Monitor *m;
    XineramaScreenInfo *info = XineramaQueryScreens(dpy, &nn);
    XineramaScreenInfo *unique = NULL;

    for (n = 0, m = mons; m; m = m->next, n++)
      ;
    /* only consider unique geometries as separate screens */
    unique = ecalloc(nn, sizeof(XineramaScreenInfo));
    for (i = 0, j = 0; i < nn; i++)
      if (isuniquegeom(unique, j, &info[i]))
	memcpy(&unique[j++], &info[i], sizeof(XineramaScreenInfo));
    XFree(info);
    nn = j;
    if (n <= nn) { /* new monitors available */
      for (i = 0; i < (nn - n); i++) {
	for (m = mons; m && m->next; m = m->next)
	  ;
	if (m) {
	  m->next = createmon();
	  attachclients(m->next);
	} else
	  mons = createmon();
      }
      for (i = 0, m = mons; i < nn && m; m = m->next, i++)
	if (i >= n || unique[i].x_org != m->mx || unique[i].y_org != m->my ||
	    unique[i].width != m->mw || unique[i].height != m->mh) {
	  dirty = 1;
	  m->num = i;
	  m->mx = m->wx = unique[i].x_org;
	  m->my = m->wy = unique[i].y_org;
	  m->mw = m->ww = unique[i].width;
	  m->mh = m->wh = unique[i].height;
	  m->wy = m->my + extrareservedspace;
	  m->wh = m->mh - extrareservedspace;
	}
    } else { /* less monitors available nn < n */
      for (i = nn; i < n; i++) {
	for (m = mons; m && m->next; m = m->next)
	  ;
	if (m == selmon)
	  selmon = mons;
	for (c = m->cl->clients; c; c = c->next) {
	  dirty = True;
	  if (c->mon == m)
	    c->mon = selmon;
	}
	cleanupmon(m);
      }
    }
    free(unique);
  } else
#endif /* XINERAMA */
  {    /* default monitor setup */
    if (!mons)
      mons = createmon();
    if (mons->mw != sw || mons->mh != sh) {
      dirty = 1;
      mons->mw = mons->ww = sw;
      mons->mh = mons->wh = sh;
      mons->wy = mons->my + extrareservedspace;
      mons->wh = mons->mh - extrareservedspace;
    }
  }
  if (dirty) {
    selmon = mons;
    selmon = wintomon(root);
  }
  return dirty;
}

void updatenumlockmask(void) {
  unsigned int i, j;
  XModifierKeymap *modmap;

  numlockmask = 0;
  modmap = XGetModifierMapping(dpy);
  for (i = 0; i < 8; i++)
    for (j = 0; j < modmap->max_keypermod; j++)
      if (modmap->modifiermap[i * modmap->max_keypermod + j] ==
	  XKeysymToKeycode(dpy, XK_Num_Lock))
	numlockmask = (1 << i);
  XFreeModifiermap(modmap);
}

void updatesizehints(Client *c) {
  long msize;
  XSizeHints size;

  if (!XGetWMNormalHints(dpy, c->win, &size, &msize))
    /* size is uninitialized, ensure that size.flags aren't used */
    size.flags = PSize;
  if (size.flags & PBaseSize) {
    c->basew = size.base_width;
    c->baseh = size.base_height;
  } else if (size.flags & PMinSize) {
    c->basew = size.min_width;
    c->baseh = size.min_height;
  } else
    c->basew = c->baseh = 0;
  if (size.flags & PResizeInc) {
    c->incw = size.width_inc;
    c->inch = size.height_inc;
  } else
    c->incw = c->inch = 0;
  if (size.flags & PMaxSize) {
    c->maxw = size.max_width;
    c->maxh = size.max_height;
  } else
    c->maxw = c->maxh = 0;
  if (size.flags & PMinSize) {
    c->minw = size.min_width;
    c->minh = size.min_height;
  } else if (size.flags & PBaseSize) {
    c->minw = size.base_width;
    c->minh = size.base_height;
  } else
    c->minw = c->minh = 0;
  if (size.flags & PAspect) {
    c->mina = (float)size.min_aspect.y / size.min_aspect.x;
    c->maxa = (float)size.max_aspect.x / size.max_aspect.y;
  } else
    c->maxa = c->mina = 0.0;
  c->isfixed = (c->maxw && c->maxh && c->maxw == c->minw && c->maxh == c->minh);
}

void updatetitle(Client *c) {
  if (!gettextprop(c->win, netatom[NetWMName], c->name, sizeof c->name))
    gettextprop(c->win, XA_WM_NAME, c->name, sizeof c->name);
  if (c->name[0] == '\0') /* hack to mark broken clients */
    strcpy(c->name, broken);
}

void updatewindowtype(Client *c) {
  Atom state = getatomprop(c, netatom[NetWMState], 0);
  Atom wtype = getatomprop(c, netatom[NetWMWindowType], 0);

  if (state == netatom[NetWMFullscreen])
    setfullscreen(c, 1);
  else if (state == netatom[NetWMMaxVert]) {
    if (getatomprop(c, netatom[NetWMState], 1) == netatom[NetWMMaxHorz])
      setfullscreen(c, 1);
  } else if (state == netatom[NetWMMaxHorz]) {
    if (getatomprop(c, netatom[NetWMState], 1) == netatom[NetWMMaxVert])
      setfullscreen(c, 1);
  }
  if (wtype == netatom[NetWMWindowTypeDialog])
    c->isfloating = 1;
}

void updatewmhints(Client *c) {
  XWMHints *wmh;

  if ((wmh = XGetWMHints(dpy, c->win))) {
    if (c == selmon->sel && wmh->flags & XUrgencyHint) {
      wmh->flags &= ~XUrgencyHint;
      XSetWMHints(dpy, c->win, wmh);
    } else
      c->isurgent = (wmh->flags & XUrgencyHint) ? 1 : 0;
    if (wmh->flags & InputHint)
      c->neverfocus = !wmh->input;
    else
      c->neverfocus = 0;
    XFree(wmh);
  }
}

void view(const Arg *arg) {
  Monitor *m;
  Client *c;
  unsigned int newtagset = selmon->tagset[selmon->seltags ^ 1];
  int i, n, tagcount, ltag;
  unsigned int tmptag;

  /* Reset Overviewmode */
  overviewmode = 0;

  if (selmon->tagset[selmon->seltags] == (arg->ui & TAGMASK)) {
    /* If the same tags where selected, than toggle all of them off. */
    toggleview(arg);
    return;
  }

  /* Get Lowest Selected Tag */
  if (arg->ui & TAGMASK) {
    for (ltag = 0; !(arg->ui & (1 << ltag)) && ltag < TAGSLENGTH; ltag++)
      ;
    /* Change Command Number */
    // tagswap_cmd_number[0] = '1' + ltag;
  } else {
    // tagswap_cmd_number[0] = '0';
  }
  /* Spawn the actual Command */
  // const Arg a = { .v = tagswap_cmd };
  if (running) {
    // spawn(&a);
  }

  /* swap tags when trying to display a tag from another monitor */
  if (arg->ui & TAGMASK)
    newtagset = arg->ui & TAGMASK;
  for (m = mons; m; m = m->next)
    if (m != selmon && newtagset & m->tagset[m->seltags]) {
      /* prevent displaying all tags (MODKEY-0) when multiple monitors
       * are connected */
      if (newtagset & selmon->tagset[selmon->seltags] ||
	  selmon->tagset[selmon->seltags] == 0) {
	/* When the new tagset is currently distributes over each monitor:
	 * remove the overlapping tags. */
	// TODO: Optimize the criteria for all of this stuff.
	newtagset &= ~m->tagset[m->seltags];

	continue;
      }
      m->sel = selmon->sel;
      m->seltags ^= 1;
      m->tagset[m->seltags] = selmon->tagset[selmon->seltags];

      /* Find lowest Tag to swap. */
      for (i = 0; !(m->tagset[m->seltags] & 1 << i); i++)
	;
      ltag = i + 1;

      /* Apply all changes, based on this lowest tag. */
      m->nmaster = m->pertag->nmasters[ltag];
      m->mfact = m->pertag->mfacts[ltag];
      m->sellt = m->pertag->sellts[ltag];
      m->lt[m->sellt] = m->pertag->ltidxs[ltag][m->sellt];
      m->lt[m->sellt ^ 1] = m->pertag->ltidxs[ltag][m->sellt ^ 1];

      attachclients(m);
      arrange(m); /* TODO: Do both arrange function calls at the same time */

      /* TODO: Check if this still works for > 2 Monitors. */
      break;
    }

  selmon->seltags ^= 1; /* toggle sel tagset */
  if (arg->ui & TAGMASK) {
    selmon->tagset[selmon->seltags] = newtagset;
    selmon->pertag->prevtag = selmon->pertag->curtag;

    for (tagcount = 0, i = 0; i < TAGSLENGTH; i++) {
      if (newtagset & (1 << i))
	tagcount++;
    }
    if (tagcount > 1) {
      selmon->pertag->curtag = 0;
    } else {
      for (i = 0; !(arg->ui & 1 << i); i++)
	;
      selmon->pertag->curtag = i + 1;
    }
  } else {
    /* Swap to prev tag.
     * (See also seltags ^= 1 instruction above) */
    // TODO: Remove all these tabbed tags, they make things only more complex!
    tmptag = selmon->pertag->prevtag;
    selmon->pertag->prevtag = selmon->pertag->curtag;
    selmon->pertag->curtag = tmptag;
  }

  attachclients(selmon); /* Move clients over to new tag. */

  /* Count number of clients on new tag and reset layout, if no clients are on
   * new tag. */
  for (i = 0; i < TAGSLENGTH; i++) {
    if (1 << i & selmon->tagset[selmon->seltags]) {
      n = 0;
      for (c = nexttiled(selmon->cl->clients, selmon); c;
	   c = nexttiled(c->next, selmon)) {
	if (c->tags & 1 << i) {
	  n++;
	}
      }
      if (n == 0) {
	selmon->pertag->nmasters[i + 1] = 1;
	selmon->pertag->mfacts[i + 1] = mfact;
	selmon->pertag->ltidxs[i + 1][selmon->pertag->sellts[i + 1]] =
	    &layouts[0];
      }
    }
  }

  selmon->nmaster = selmon->pertag->nmasters[selmon->pertag->curtag];
  selmon->mfact = selmon->pertag->mfacts[selmon->pertag->curtag];
  selmon->sellt = selmon->pertag->sellts[selmon->pertag->curtag];
  selmon->lt[selmon->sellt] =
      selmon->pertag->ltidxs[selmon->pertag->curtag][selmon->sellt];
  selmon->lt[selmon->sellt ^ 1] =
      selmon->pertag->ltidxs[selmon->pertag->curtag][selmon->sellt ^ 1];

  focus(NULL); /* Focus last client */
  arrange(selmon);
  updatecurrentdesktop();
  spawnbarupdate();
}

void viewselected(const Arg *a) {
  if (!selmon->sel)
    return;
  const Arg viewarg = {.ui = selmon->sel->tags};
  view(&viewarg);
}

void warp(const Client *c) {
  int x, y;

  if (!c) {
    XWarpPointer(dpy, None, root, 0, 0, 0, 0, selmon->wx + selmon->ww / 2,
		 selmon->wy + selmon->wh / 2);
    return;
  }

  if (!getrootptr(&x, &y) ||
      (x > c->x - c->bw && y > c->y - c->bw && x < c->x + c->w + c->bw * 2 &&
       y < c->y + c->h + c->bw * 2))
    return;

  XWarpPointer(dpy, None, c->win, 0, 0, 0, 0, c->w / 2, c->h / 2);
}

pid_t winpid(Window w) {
  pid_t result = 0;

  xcb_res_client_id_spec_t spec = {0};
  spec.client = w;
  spec.mask = XCB_RES_CLIENT_ID_MASK_LOCAL_CLIENT_PID;

  xcb_generic_error_t *e = NULL;
  xcb_res_query_client_ids_cookie_t c =
      xcb_res_query_client_ids(xcon, 1, &spec);
  xcb_res_query_client_ids_reply_t *r =
      xcb_res_query_client_ids_reply(xcon, c, &e);

  if (!r)
    return (pid_t)0;

  xcb_res_client_id_value_iterator_t i =
      xcb_res_query_client_ids_ids_iterator(r);
  for (; i.rem; xcb_res_client_id_value_next(&i)) {
    spec = i.data->spec;
    if (spec.mask & XCB_RES_CLIENT_ID_MASK_LOCAL_CLIENT_PID) {
      uint32_t *t = xcb_res_client_id_value_value(i.data);
      result = *t;
      break;
    }
  }

  free(r);

  if (result == (pid_t)-1)
    result = 0;
  return result;
}

pid_t getparentprocess(pid_t p) {
  unsigned int v = 0;

#if defined(__linux__)
  FILE *f;
  char buf[256];
  snprintf(buf, sizeof(buf) - 1, "/proc/%u/stat", (unsigned)p);

  if (!(f = fopen(buf, "r")))
    return (pid_t)0;

  if (fscanf(f, "%*u %*s %*c %u", (unsigned *)&v) != 1)
    v = (pid_t)0;
  fclose(f);
#elif defined(__FreeBSD__)
  struct kinfo_proc *proc = kinfo_getproc(p);
  if (!proc)
    return (pid_t)0;

  v = proc->ki_ppid;
  free(proc);
#endif
  return (pid_t)v;
}

int isdescprocess(pid_t p, pid_t c) {
  while (p != c && c != 0)
    c = getparentprocess(c);

  return (int)c;
}

Client *termforwin(const Client *w) {
  Client *c;
  Monitor *m;

  if (!w->pid || w->isterminal)
    return NULL;

  for (m = mons; m; m = m->next) {
    for (c = m->cl->clients; c; c = c->next) {
      if (c->isterminal && !c->swallowing && c->pid &&
          isdescprocess(c->pid, w->pid))
        return c;
    }
  }

  return NULL;
}

Client *swallowingclient(Window w) {
  Client *c;
  Monitor *m;

  for (m = mons; m; m = m->next) {
    for (c = m->cl->clients; c; c = c->next) {
      if (c->swallowing && c->swallowing->win == w)
        return c;
    }
  }

  return NULL;
}

Client *wintoclient(Window w) {
  Client *c;
  Monitor *m;

  for (m = mons; m; m = m->next)
    for (c = m->cl->clients; c; c = c->next)
      if (c->win == w)
        return c;
  return NULL;
}

Monitor *wintomon(Window w) {
  int x, y;
  Client *c;
  Monitor *m;

  if (w == root && getrootptr(&x, &y))
    return recttomon(x, y, 1, 1);
  for (m = mons; m; m = m->next)
    if (w == m->barwin)
      return m;
  if ((c = wintoclient(w)))
    return c->mon;
  return selmon;
}

/* There's no way to check accesses to destroyed windows, thus those cases are
 * ignored (especially on UnmapNotify's). Other types of errors call Xlibs
 * default error handler, which may call exit. */
int xerror(Display *dpy, XErrorEvent *ee) {
  if (ee->error_code == BadWindow ||
      (ee->request_code == X_SetInputFocus && ee->error_code == BadMatch) ||
      (ee->request_code == X_PolyText8 && ee->error_code == BadDrawable) ||
      (ee->request_code == X_PolyFillRectangle &&
       ee->error_code == BadDrawable) ||
      (ee->request_code == X_PolySegment && ee->error_code == BadDrawable) ||
      (ee->request_code == X_ConfigureWindow && ee->error_code == BadMatch) ||
      (ee->request_code == X_GrabButton && ee->error_code == BadAccess) ||
      (ee->request_code == X_GrabKey && ee->error_code == BadAccess) ||
      (ee->request_code == X_CopyArea && ee->error_code == BadDrawable))
    return 0;
  fprintf(stderr, "dwm: fatal error: request code=%d, error code=%d\n",
          ee->request_code, ee->error_code);
  return xerrorxlib(dpy, ee); /* may call exit */
}

int xerrordummy(Display *dpy, XErrorEvent *ee) { return 0; }

/* Startup Error handler to check if another window manager
 * is already running. */
int xerrorstart(Display *dpy, XErrorEvent *ee) {
  die("dwm: another window manager is already running");
  return -1;
}

void xinitvisual() {
  XVisualInfo *infos;
  XRenderPictFormat *fmt;
  int nitems;
  int i;

  XVisualInfo tpl = {.screen = screen, .depth = 32, .class = TrueColor};
  long masks = VisualScreenMask | VisualDepthMask | VisualClassMask;

  infos = XGetVisualInfo(dpy, masks, &tpl, &nitems);
  visual = NULL;
  for (i = 0; i < nitems; i++) {
    fmt = XRenderFindVisualFormat(dpy, infos[i].visual);
    if (fmt->type == PictTypeDirect && fmt->direct.alphaMask) {
      visual = infos[i].visual;
      depth = infos[i].depth;
      cmap = XCreateColormap(dpy, root, visual, AllocNone);
      useargb = 1;
      break;
    }
  }

  XFree(infos);

  if (!visual) {
    visual = DefaultVisual(dpy, screen);
    depth = DefaultDepth(dpy, screen);
    cmap = DefaultColormap(dpy, screen);
  }
}

void xrdb(const Arg *arg) {
  loadxrdb();
  int i;
  Monitor *m;
  Client *c;
  for (i = 0; i < LENGTH(colors); i++)
    scheme[i] = drw_scm_create(drw, colors[i], alphas[i], 3);
  /* Redraw every Border. */
  for (m = mons; m; m = m->next)
    for (c = m->cl->clients; c; c = c->next)
      XSetWindowBorder(dpy, c->win, scheme[SchemeNorm][ColBorder].pixel);
  focus(NULL);
  arrange(NULL);
}

void zoom(const Arg *arg) {
  Client *c = selmon->sel;

  if (!selmon->lt[selmon->sellt]->arrange ||
      (selmon->sel && selmon->sel->isfloating))
    return;
  if (c == nexttiled(selmon->cl->clients, selmon))
    if (!c || !(c = nexttiled(c->next, selmon)))
      return;
  pop(c);
  spawnbarupdate();
}

int main(int argc, char *argv[]) {
  if (argc == 2 && !strcmp("-v", argv[1]))
    die("dwm-" VERSION);
  else if (argc != 1)
    die("usage: dwm [-v]");
  if (!setlocale(LC_CTYPE, "") || !XSupportsLocale())
    fputs("warning: no locale support\n", stderr);
  if (!(dpy = XOpenDisplay(NULL)))
    die("dwm: cannot open display");
  if (!(xcon = XGetXCBConnection(dpy)))
    die("dwm: cannot get xcb connection\n");
  checkotherwm();
  XrmInitialize();
  loadxrdb();
  setup();
#ifdef __OpenBSD__
  if (pledge("stdio rpath proc exec", NULL) == -1)
    die("pledge");
#endif /* __OpenBSD__ */
  scan();
  startupdone = 1;
  run();
  cleanup();
  XCloseDisplay(dpy);
  if(restart)
      execvp(argv[0], argv);
  return EXIT_SUCCESS;
}
