void
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

