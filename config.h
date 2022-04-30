/* See LICENSE file for copyright and license details. */

#ifndef CONFIG_H
#define CONFIG_H

#include "dwm.h"

#define NUMTAGS 9

/* appearance */
extern const unsigned int borderpx;
extern const int cornerradius;
extern const unsigned int gappxdf;
extern const unsigned int snap;
extern const int swallowfloating;
extern const int useanimation;
extern const int animationframes;
extern const int framereduction;
extern const int frreducstart;
extern const int framedur;
extern const int extrareservedspace;
extern char col_brd_sel[8];
extern char col_brd_norm[8];
extern const unsigned int borderalpha;
extern char *colors[2][3];
extern const unsigned int alphas[2][3];

/* layout(s) */
extern const float mfact;
extern const int nmaster;
extern const int resizehints;

/* === Layouts === */
extern const Layout layouts[];
extern const size_t layouts_size;

extern const Layout * overviewlayout;

extern const Rule rules[];
extern const size_t rules_size;

extern const char * barupdate_cmd[];

extern Key keys[];
extern const size_t keys_size;
extern Button buttons[];
extern const size_t buttons_size;

#endif /* CONFIG_H */

