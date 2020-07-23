void
tcl(Monitor * m)
{
	int x, y, h, w, mw, sw, bdw;
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
				n == m->nmaster ? m->ww - bdw : mw - bdw,
				(h - y) / (nn - i) - bdw,
				False, 1);
		y += HEIGHT_G(c);
	}

	n -= nn;
	if (n == 0)
		return;


	if (nn == 0)
		c = nexttiled(m->cl->clients, m);
	w = (m->ww - mw) / ((n > 1) + 1);

	if (n > 1)
	{
		x = m->wx + ((n > 1) ? mw + sw : mw);
		y = m->wy;
		h = m->wh / (n / 2);

		if (h < bh)
			h = m->wh;

		for (i = 0; c && i < n / 2; c = nexttiled(c->next, m), i++)
		{
			bdw = (2 * c->bw);
			resize(c,
			       x,
			       y,
			       w - bdw,
			       (i + 1 == n / 2) ? m->wy + m->wh - y - bdw : h - bdw,
			       False, 1);

			if (h != m->wh)
				y += HEIGHT_G(c);
		}
	}

	x = (n + 1 / 2) == 1 ? mw + m->wx : m->wx;
	y = m->wy;
	h = m->wh / ((n + 1) / 2);

	if (h < bh)
		h = m->wh;

	for (i = 0; c; c = nexttiled(c->next, m), i++)
	{
		bdw = (2 * c->bw);
		resize(c,
		       x,
		       y,
		       (i + 1 == (n + 1) / 2) ? w - bdw : w - bdw,
		       (i + 1 == (n + 1) / 2) ? m->wy + m->wh - y - bdw : h - bdw,
		       False, 1);

		if (h != m->wh)
			y += HEIGHT_G(c);
	}
}

