void
fibonacci(Monitor *mon, int s) {
	unsigned int i, n, cx, cy, cw, ch, nw, nh;
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
			if((i % 4) == 0)
				cx += WIDTH_G(c);
			else if((i % 4) == 1)
				cy += HEIGHT_G(c);
			else if((i % 4) == 2) {
				if(s)
					cx += WIDTH_G(c);
				else
					cx -= WIDTH_G(c);
			}
			else if((i % 4) == 3) {
				if(s)
					cy += HEIGHT_G(c);
				else
					cy -= HEIGHT_G(c);
			}

			if (i % 2)
				ch -= HEIGHT_G(c);
			else
				cw -= WIDTH_G(c);

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

