#ifndef DWM_H
#define DWM_H

#include <X11/Xft/Xft.h>

/* macros */
#define BUTTONMASK              (ButtonPressMask|ButtonReleaseMask)
#define CLEANMASK(mask)         (mask & ~(numlockmask|LockMask) & (ShiftMask|ControlMask|Mod1Mask|Mod2Mask|Mod3Mask|Mod4Mask|Mod5Mask))
#define INTERSECT(x,y,w,h,m)    (MAX(0, MIN((x)+(w),(m)->wx+(m)->ww) - MAX((x),(m)->wx)) \
                               * MAX(0, MIN((y)+(h),(m)->wy+(m)->wh) - MAX((y),(m)->wy)))
#define ISVISIBLE(C, M)         ((C->tags & M->tagset[M->seltags]))
#define MOUSEMASK               (BUTTONMASK|PointerMotionMask)
#define WIDTH(X)                ((X)->w + 2 * (X)->bw + 2*gappx)
#define WIDTH_G(X)              ((X)->goalw + 2 * (X)->bw + 2*gappx)
#define HEIGHT(X)               ((X)->h + 2 * (X)->bw + 2*gappx)
#define HEIGHT_G(X)             ((X)->goalh + 2 * (X)->bw + 2*gappx)
#define TAGMASK                 ((1 << NUMTAGS) - 1)
#define TAGSLENGTH              (NUMTAGS)
#define XRDB_LOAD_COLOR(R,V)    if (XrmGetResource(xrdb, R, NULL, &type, &value) == True) { \
                                  if (value.addr != NULL && strnlen(value.addr, 8) == 7 && value.addr[0] == '#') { \
                                    int i = 1; \
                                    for (; i <= 6; i++) { \
                                      if (value.addr[i] < 48) break; \
                                      if (value.addr[i] > 57 && value.addr[i] < 65) break; \
                                      if (value.addr[i] > 70 && value.addr[i] < 97) break; \
                                      if (value.addr[i] > 102) break; \
                                    } \
                                    if (i == 7) { \
                                      strncpy(V, value.addr, 7); \
                                      V[7] = '\0'; \
                                    } \
                                  } \
                                }

/* enums */
enum { CurNormal, CurResize, CurMove, CurLast }; /* cursor */
enum { SchemeNorm, SchemeSel }; /* color schemes */
enum { NetSupported, NetWMName, NetWMState, NetWMCheck,
       NetWMFullscreen, NetWMMaxVert, NetWMMaxHorz, NetActiveWindow, NetWMWindowType,
       NetWMWindowTypeDialog, NetClientList, NetDesktopNames, NetDesktopViewport, NetNumberOfDesktops, NetCurrentDesktop, NetLast }; /* EWMH atoms */
enum { WMProtocols, WMDelete, WMState, WMTakeFocus, WMLast }; /* default atoms */
enum { ClkClientWin, ClkRootWin, ClkLast }; /* clicks */

typedef union {
	int i;
	unsigned int ui;
	float f;
	const void *v;
} Arg;

typedef struct {
	unsigned int click;
	unsigned int mask;
	unsigned int button;
	void (*func)(const Arg *arg);
	const Arg arg;
} Button;

typedef struct Monitor Monitor;
typedef struct Client Client;
struct Client {
	char name[256];
	float mina, maxa;
	int x, y, w, h;
	int oldx, oldy, oldw, oldh;
	int goalx, goaly, goalw, goalh;
	int basew, baseh, incw, inch, maxw, maxh, minw, minh;
	int bw, oldbw;
	unsigned int tags;
	int isfixed, isfloating, isurgent, neverfocus, oldstate, isfullscreen,
	    isterminal, noswallow, useresizehints, animate, hasroundcorners,
	    animateresize;
	pid_t pid;
	Client *next;
	Client *snext;
	Client *swallowing;
	Monitor *mon;
	Window win;
};

typedef struct {
	unsigned int mod;
	KeySym keysym;
	void (*func)(const Arg *);
	const Arg arg;
} Key;

typedef struct {
	const char * sig;
	void (*func)(const Arg *);
} Signal;

typedef struct {
	const char * name;
	int (*func)(char *, char *);
} QuerySignal;

typedef struct {
	const char *symbol;
	void (*arrange)(Monitor *);
} Layout;

typedef struct Clientlist Clientlist;
typedef struct Pertag Pertag;
struct Monitor {
	char ltsymbol[16];
	float mfact;
	int nmaster;
	int num;
	int mx, my, mw, mh;   /* screen size */
	int wx, wy, ww, wh;   /* window area  */
	unsigned int seltags;
	unsigned int sellt;
	unsigned int tagset[2];
	Clientlist *cl;
	Client *sel;	      /* Focused Client */
	Monitor *next;
	Window barwin;
	const Layout *lt[2];
	Pertag *pertag;
};

typedef struct {
	const char *class;
	const char *instance;
	const char *title;
	unsigned int tags;
	int isfloating;
	int isterminal;
	int noswallow;
	int monitor;
	const Layout *lt;
	int overrideresizehints;
	int noroundcorners;
	int noanimatemove;
	int noanimateresize;
} Rule;

struct Clientlist {
	Client *clients;
	Client *stack;
};

typedef struct AnimateThreadArg {
	int x, y, w, h;
	Client * c;
	pthread_t thread;
	struct AnimateThreadArg * next;
} AnimateThreadArg;

/* function declarations */
void animateclient(Client *c, int x, int y, int w, int h);
void * animateclient_thread(void * arg);
void animateclient_start(Client * c, int x, int y, int w, int h);
void animateclient_endall();
void applyrules(Client *c);
int applysizehints(Client *c, int *x, int *y, int *w, int *h, int interact);
void arrange(Monitor *m);
void arrangemon(Monitor *m);
void attach(Client *c);
void attachclients(Monitor *m);
void attachstack(Client *c);
int fake_signal(void);
void buttonpress(XEvent *e);
void checkotherwm(void);
void cleanup(void);
void cleanupmon(Monitor *mon);
void clientmessage(XEvent *e);
void configure(Client *c);
void configurenotify(XEvent *e);
void configurerequest(XEvent *e);
Monitor *createmon(void);
void destroynotify(XEvent *e);
void detach(Client *c);
void detachstack(Client *c);
Monitor *dirtomon(int dir);
void enternotify(XEvent *e);
void focus(Client *c);
void focusin(XEvent *e);
void focusmon(const Arg *arg);
void focusstack(const Arg *arg);
Atom getatomprop(Client *c, Atom prop, int num);
int getrootptr(int *x, int *y);
long getstate(Window w);
int gettextprop(Window w, Atom atom, char *text, unsigned int size);
void grabbuttons(Client *c, int focused);
void grabkeys(void);
void incnmaster(const Arg *arg);
void keypress(XEvent *e);
void killclient(const Arg *arg);
void loadxrdb(void);
void manage(Window w, XWindowAttributes *wa);
void mappingnotify(XEvent *e);
void maprequest(XEvent *e);
void motionnotify(XEvent *e);
void movemouse(const Arg *arg);
Client *nexttiled(Client *c, Monitor *m);
void overview(const Arg *arg);
void pop(Client *);
void propertynotify(XEvent *e);
void quit(const Arg *arg);
void * querysocket_listen(void * arg);
void * querysocket_execute(void * arg);
Monitor *recttomon(int x, int y, int w, int h);
void resize(Client *c, int x, int y, int w, int h, int interact, int animate);
void resizeclient(Client *c, int x, int y, int w, int h);
void resizemouse(const Arg *arg);
void restack(Monitor *m);
void restack_nowarp(Monitor *m);
void run(void);
void scan(void);
int sendevent(Client *c, Atom proto);
void sendmon(Client *c, Monitor *m);
void setclientstate(Client *c, long state);
void setcurrentdesktop(void);
void setdesktopnames(void);
void setfocus(Client *c);
void setfullscreen(Client *c, int fullscreen);
void setlayout(const Arg *arg);
void setlayoutcustommonitor(const Arg *arg, Monitor *m);
void setmfact(const Arg *arg);
void setgap(const Arg *arg);
void setnumdesktops(void);
void setup(void);
void setviewport(void);
void seturgent(Client *c, int urg);
void showhide(Client *c);
void sigchld(int unused);
void spawn(const Arg *arg);
void spawnbarupdate();
void tag(const Arg *arg);
void tagmon(const Arg *arg);
void togglefloating(const Arg *arg);
void toggletag(const Arg *arg);
void toggleview(const Arg *arg);
void unfocus(Client *c, int setfocus);
void unmanage(Client *c, int destroyed);
void unmapnotify(XEvent *e);
void updatecurrentdesktop(void);
void updatebars(void);
void updateclientlist(void);
int updategeom(void);
void updatenumlockmask(void);
void updatesizehints(Client *c);
void updatetitle(Client *c);
void updatewindowtype(Client *c);
void updatewmhints(Client *c);
void view(const Arg *arg);
void viewselected(const Arg *arg);
void warp(const Client *c);
Client *wintoclient(Window w);
Monitor *wintomon(Window w);
int xerror(Display *dpy, XErrorEvent *ee);
int xerrordummy(Display *dpy, XErrorEvent *ee);
int xerrorstart(Display *dpy, XErrorEvent *ee);
void xinitvisual();
void xrdb(const Arg *arg);
void zoom(const Arg *arg);
void bstack(Monitor *m);
void bstackhoriz(Monitor *m);
void roundcornersclient(Client *c);
int createroundcornermask(Pixmap * maskP, GC * shapegcP, Window win, int w, int h, int radius);
pid_t getparentprocess(pid_t p);
int isdescprocess(pid_t p, pid_t c);
Client *swallowingclient(Window w);
Client *termforwin(const Client *c);
pid_t winpid(Window w);

extern Clientlist *cl;
extern Monitor *mons, *selmon;
extern unsigned int gappx;

#endif /* DWM_H */

