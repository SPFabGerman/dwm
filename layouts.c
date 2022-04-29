#include "layouts.h"
#include "dwm.h"
#include "util.h"

void
tile(Monitor *m)
{
	unsigned int i, n, h, mw, my, ty;
	Client *c;

	for (n = 0, c = nexttiled(m->cl->clients, m); c; c = nexttiled(c->next, m), n++);
	if (n == 0)
		return;

	if (n > m->nmaster)
		mw = m->nmaster ? m->ww * m->mfact : 0;
	else
		mw = m->ww;
	for (i = my = ty = 0, c = nexttiled(m->cl->clients, m); c; c = nexttiled(c->next, m), i++)
		if (i < m->nmaster) {
			h = (m->wh - my) / (MIN(n, m->nmaster) - i);
			resize(c, m->wx, m->wy + my, mw - (2*c->bw), h - (2*c->bw), 0, 1);
			if (my + HEIGHT_G(c) < m->wh)
				my += HEIGHT_G(c);
		} else {
			h = (m->wh - ty) / (n - i);
			resize(c, m->wx + mw, m->wy + ty, m->ww - mw - (2*c->bw), h - (2*c->bw), 0, 1);
			if (ty + HEIGHT_G(c) < m->wh)
				ty += HEIGHT_G(c);
		}
}

void
monocle(Monitor *m)
{
	unsigned int n = 0;
	Client *c;

	for (c = m->cl->clients; c; c = c->next)
		if (ISVISIBLE(c, m))
			n++;
	/* override layout symbol */
	snprintf(m->ltsymbol, sizeof m->ltsymbol, "[%d]", n);
	for (c = nexttiled(m->cl->clients, m); c; c = nexttiled(c->next, m))
		resize(c, m->wx, m->wy, m->ww - 2 * c->bw, m->wh - 2 * c->bw, 0, 1);
}

void
gaplessgrid(Monitor *m) {
	unsigned int n, cols, rows, cn, rn, i, cx, cy, cw, ch;
	Client *c;

	for(n = 0, c = nexttiled(m->cl->clients, m); c; c = nexttiled(c->next, m), n++) ;
	if(n == 0)
		return;

	/* grid dimensions */
	for(cols = 0; cols <= n/2; cols++)
		if(cols*cols >= n)
			break;
	if(n == 5) /* set layout against the general calculation: not 1:2:2, but 2:3 */
		cols = 2;
	rows = n/cols;

	/* window geometries */
	cw = cols ? m->ww / cols : m->ww;
	cn = 0; /* current column number */
	rn = 0; /* current row number */
	for(i = 0, c = nexttiled(m->cl->clients, m); c; i++, c = nexttiled(c->next, m)) {
		if(i/rows + 1 > cols - n%cols)
			rows = n/cols + 1;
		ch = rows ? m->wh / rows : m->wh;
		cx = m->wx + cn*cw;
		cy = m->wy + rn*ch;
		resize(c, cx, cy, cw - 2 * c->bw, ch - 2 * c->bw, False, 1);
		rn++;
		if(rn >= rows) {
			rn = 0;
			cn++;
		}
	}
}

static void
fibonacci(Monitor *mon, int s) {
	int i, n, cx, cy, cw, ch, nw, nh;
	Client *c;

	for(n = 0, c = nexttiled(mon->cl->clients, mon); c; c = nexttiled(c->next, mon), n++);
	if(n == 0)
		return;
	
	cx = mon->wx;
	cy = mon->wy;
	cw = mon->ww;
	ch = mon->wh;
	
	for(i = 0, c = nexttiled(mon->cl->clients, mon); c; c = nexttiled(c->next, mon)) {
		if((i % 2 && ch * mon->mfact > 2 * c->bw)
		   || (!(i % 2) && cw * mon->mfact > 2 * c->bw)) {
			nw = cw;
			nh = ch;
			if(i < n - 1) {
				if(i % 2)
					nh *= mon->mfact;
				else
					nw *= mon->mfact;
			}

			resize(c, cx, cy, nw - 2 * c->bw, nh - 2 * c->bw, False, 1);
			if((i % 4) == 0) {
				if ((cx - mon->wx) + WIDTH_G(c) < mon->ww)
					cx += WIDTH_G(c);
			} else if((i % 4) == 1) {
				if ((cy - mon->wy) + HEIGHT_G(c) < mon->wh)
					cy += HEIGHT_G(c);
			} else if((i % 4) == 2) {
				if(s) {
					if ((cx - mon->wx) + WIDTH_G(c) < mon->ww)
						cx += WIDTH_G(c);
				} else {
					cx -= WIDTH_G(c);
				}
			}
			else if((i % 4) == 3) {
				if(s) {
					if ((cy - mon->wy) + HEIGHT_G(c) < mon->wh)
						cy += HEIGHT_G(c);
				} else {
					cy -= HEIGHT_G(c);
				}
			}

			if (i % 2) {
				if (ch - HEIGHT_G(c) >= 0)
					ch -= HEIGHT_G(c);
			} else {
				if (cw - WIDTH_G(c) >= 0)
					cw -= WIDTH_G(c);
			}

			i++;
		} else {
			resize(c, cx, cy, cw - 2 * c->bw, ch - 2 * c->bw, False, 1);
		}
	}
}

void
dwindle(Monitor *mon) {
	fibonacci(mon, 1);
}

void
spiral(Monitor *mon) {
	fibonacci(mon, 0);
}

void
horizgrid(Monitor *m) {
	Client *c;
	unsigned int n, i;
	int w = 0;
	int ntop, nbottom = 0;

	/* Count windows */
	for(n = 0, c = nexttiled(m->cl->clients, m); c; c = nexttiled(c->next, m), n++);

	if(n == 0)
		return;
	else if(n == 1) { /* Just fill the whole screen */
		c = nexttiled(m->cl->clients, m);
		resize(c, m->wx, m->wy, m->ww - (2*c->bw), m->wh - (2*c->bw), False, 1);
	} else if(n == 2) { /* Split vertically */
		w = m->ww / 2;
		c = nexttiled(m->cl->clients, m);
		resize(c, m->wx, m->wy, w - (2*c->bw), m->wh - (2*c->bw), False, 1);
		c = nexttiled(c->next, m);
		resize(c, m->wx + w, m->wy, w - (2*c->bw), m->wh - (2*c->bw), False, 1);
	} else {
		ntop = n / 2;
		nbottom = n - ntop;
		for(i = 0, c = nexttiled(m->cl->clients, m); c; c = nexttiled(c->next, m), i++) {
			if(i < ntop)
				resize(c, m->wx + i * m->ww / ntop, m->wy, m->ww / ntop - (2*c->bw), m->wh / 2 - (2*c->bw), False, 1);
			else
				resize(c, m->wx + (i - ntop) * m->ww / nbottom, m->wy + m->wh / 2, m->ww / nbottom - (2*c->bw), m->wh / 2 - (2*c->bw), False, 1);
		}
	}
}


void
bstack(Monitor *m) {
	int w, h, mh, mx, tx, ty, tw;
	unsigned int i, n;
	Client *c;

	for (n = 0, c = nexttiled(m->cl->clients, m); c; c = nexttiled(c->next, m), n++);
	if (n == 0)
		return;
	if (n > m->nmaster) {
		mh = m->nmaster ? m->mfact * m->wh : 0;
		tw = m->ww / (n - m->nmaster);
		ty = m->wy + mh;
	} else {
		mh = m->wh;
		tw = m->ww;
		ty = m->wy;
	}
	for (i = mx = 0, tx = m->wx, c = nexttiled(m->cl->clients, m); c; c = nexttiled(c->next, m), i++) {
		if (i < m->nmaster) {
			w = (m->ww - mx) / (MIN(n, m->nmaster) - i);
			resize(c, m->wx + mx, m->wy, w - (2 * c->bw), mh - (2 * c->bw), 0, 1);
			if (mx + WIDTH_G(c) < m->ww)
				mx += WIDTH_G(c);
		} else {
			h = m->wh - mh;
			resize(c, tx, ty, tw - (2 * c->bw), h - (2 * c->bw), 0, 1);
			if (tx + WIDTH_G(c) < m->ww)
				tx += WIDTH_G(c);
		}
	}
}

void
bstackhoriz(Monitor *m) {
	int w, mh, mx, tx, ty, th;
	unsigned int i, n;
	Client *c;

	for (n = 0, c = nexttiled(m->cl->clients, m); c; c = nexttiled(c->next, m), n++);
	if (n == 0)
		return;
	if (n > m->nmaster) {
		mh = m->nmaster ? m->mfact * m->wh : 0;
		th = (m->wh - mh) / (n - m->nmaster);
		ty = m->wy + mh;
	} else {
		th = mh = m->wh;
		ty = m->wy;
	}
	for (i = mx = 0, tx = m->wx, c = nexttiled(m->cl->clients, m); c; c = nexttiled(c->next, m), i++) {
		if (i < m->nmaster) {
			w = (m->ww - mx) / (MIN(n, m->nmaster) - i);
			resize(c, m->wx + mx, m->wy, w - (2 * c->bw), mh - (2 * c->bw), 0, 1);
			mx += WIDTH_G(c);
		} else {
			resize(c, tx, ty, m->ww - (2 * c->bw), th - (2 * c->bw), 0, 1);
			if (th != m->wh)
				ty += HEIGHT_G(c);
		}
	}
}

void
tcl(Monitor * m)
{
	int x, y, h, w, mw, sw, bdw, yl, yr;
	unsigned int i, n, nn;
	Client * c;

	for (n = 0, c = nexttiled(m->cl->clients, m); c;
	        c = nexttiled(c->next, m), n++);

	if (n == 0)
		return;

	nn = MIN(m->nmaster, n);
	if (nn != 0)
		mw = m->mfact * m->ww;
	else
		mw = 0;
	sw = (m->ww - mw) / 2;
	y = 0;
	h = m->wh;
	for (i = 0, c = nexttiled(m->cl->clients, m); i < nn; i++, c = nexttiled(c->next, m)) {
		bdw = (2 * c->bw);
		resize(c,
				n <= m->nmaster + 1 ? m->wx : m->wx + sw,
				m->wy + y,
				n <= m->nmaster ? m->ww - bdw : mw - bdw,
				(h - y) / (nn - i) - bdw,
				False, 1);
		if (y + HEIGHT_G(c) < m->wh)
			y += HEIGHT_G(c);
	}

	n -= nn;
	if (n == 0)
		return;

	if (nn == 0)
		c = nexttiled(m->cl->clients, m);

	if (n == 1) {
		bdw = (2 * c->bw);
		resize(c,
			m->wx + mw,
			m->wy,
			m->ww - mw - bdw,
			m->wh - bdw,
			False, 1);
		return;
	}

	w = sw;
	yl = yr = 0;
	for (i = 0; c && i < n; i++, c = nexttiled(c->next, m)) {
		bdw = (2 * c->bw);
		if (i % 2 == 0) {
			x = m->wx;
			y = yl;
		} else {
			x = m->wx + sw + mw;
			y = yr;
		}
		h = (m->wh - y) / ((n - i + 1) / 2);

		resize(c, x, m->wy + y, w-bdw, h-bdw, False, 1);

		if (i % 2 == 0) {
			if (yl + HEIGHT_G(c) < m->wh)
				yl += HEIGHT_G(c);
		} else {
			if (yr + HEIGHT_G(c) < m->wh)
				yr += HEIGHT_G(c);
		}
	}
}

