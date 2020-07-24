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
