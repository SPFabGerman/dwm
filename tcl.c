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

