/* $Id: screen-redraw.c,v 1.47 2009-09-11 14:13:52 tcunha Exp $ */

/*
 * Copyright (c) 2007 Nicholas Marriott <nicm@users.sourceforge.net>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER
 * IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>

#include <string.h>

#include "tmux.h"

int	screen_redraw_cell_border(struct client *, u_int, u_int);
int	screen_redraw_check_cell(struct client *, u_int, u_int);
void	screen_redraw_draw_number(struct client *, struct window_pane *);

#define CELL_INSIDE 0
#define CELL_LEFTRIGHT 1
#define CELL_TOPBOTTOM 2
#define CELL_TOPLEFT 3
#define CELL_TOPRIGHT 4
#define CELL_BOTTOMLEFT 5
#define CELL_BOTTOMRIGHT 6
#define CELL_TOPJOIN 7
#define CELL_BOTTOMJOIN 8
#define CELL_LEFTJOIN 9
#define CELL_RIGHTJOIN 10
#define CELL_JOIN 11
#define CELL_OUTSIDE 12

/* Check if a cell is on the pane border. */
int
screen_redraw_cell_border(struct client *c, u_int px, u_int py)
{
	struct window		*w = c->session->curw->window;
	struct window_pane	*wp;

	/* Check all the panes. */
	TAILQ_FOREACH(wp, &w->panes, entry) {
		if (!window_pane_visible(wp))
			continue;

		/* Inside pane. */
		if (px >= wp->xoff && px < wp->xoff + wp->sx &&
		    py >= wp->yoff && py < wp->yoff + wp->sy)
			return (0);

		/* Left/right borders. */
		if ((wp->yoff == 0 || py >= wp->yoff - 1) &&
		    py <= wp->yoff + wp->sy) {
			if (wp->xoff != 0 && px == wp->xoff - 1)
				return (1);
			if (px == wp->xoff + wp->sx)
				return (1);
		}

		/* Top/bottom borders. */
		if ((wp->xoff == 0 || px >= wp->xoff - 1) && 
		    px <= wp->xoff + wp->sx) {
			if (wp->yoff != 0 && py == wp->yoff - 1)
				return (1);
			if (py == wp->yoff + wp->sy)
				return (1);
		}
	}

	return (0);
}

/* Check if cell inside a pane. */
int
screen_redraw_check_cell(struct client *c, u_int px, u_int py)
{
	struct window		*w = c->session->curw->window;
	struct window_pane	*wp;
	int			 borders;

	if (px > w->sx || py > w->sy)
		return (CELL_OUTSIDE);

	TAILQ_FOREACH(wp, &w->panes, entry) {
		if (!window_pane_visible(wp))
			continue;

		/* If outside the pane and its border, skip it. */
		if ((wp->xoff != 0 && px < wp->xoff - 1) ||
		    px > wp->xoff + wp->sx ||
		    (wp->yoff != 0 && py < wp->yoff - 1) ||
		    py > wp->yoff + wp->sy)
			continue;

		/* If definitely inside, return so. */
		if (!screen_redraw_cell_border(c, px, py))
			return (CELL_INSIDE);

		/* 
		 * Construct a bitmask of whether the cells to the left (bit
		 * 4), right, top, and bottom (bit 1) of this cell are borders.
		 */
		borders = 0;
		if (px == 0 || screen_redraw_cell_border(c, px - 1, py))
			borders |= 8;
		if (px <= w->sx && screen_redraw_cell_border(c, px + 1, py))
			borders |= 4;
		if (py == 0 || screen_redraw_cell_border(c, px, py - 1))
			borders |= 2;
		if (py <= w->sy && screen_redraw_cell_border(c, px, py + 1))
			borders |= 1;

		/* 
		 * Figure out what kind of border this cell is. Only one bit
		 * set doesn't make sense (can't have a border cell with no
		 * others connected).
		 */
		switch (borders) {
		case 15:	/* 1111, left right top bottom */
			return (CELL_JOIN);
		case 14:	/* 1110, left right top */
			return (CELL_BOTTOMJOIN);
		case 13:	/* 1101, left right bottom */
			return (CELL_TOPJOIN);
		case 12:	/* 1100, left right */
			return (CELL_TOPBOTTOM);
		case 11:	/* 1011, left top bottom */
			return (CELL_RIGHTJOIN);
		case 10:	/* 1010, left top */
			return (CELL_BOTTOMRIGHT);
		case 9:		/* 1001, left bottom */
			return (CELL_TOPRIGHT);
		case 7:		/* 0111, right top bottom */
			return (CELL_LEFTJOIN);
		case 6:		/* 0110, right top */
			return (CELL_BOTTOMLEFT);
		case 5:		/* 0101, right bottom */
			return (CELL_TOPLEFT);
		case 3:		/* 0011, top bottom */
			return (CELL_LEFTRIGHT);
		}
	}

	return (CELL_OUTSIDE);
}

/* Redraw entire screen. */
void
screen_redraw_screen(struct client *c, int status_only)
{
	struct window		*w = c->session->curw->window;
	struct tty		*tty = &c->tty;
	struct window_pane	*wp;
	u_int		 	 i, j, type;
	int		 	 status;
	const u_char		*base, *ptr;
	u_char		       	 ch, border[20];

	/* Get status line, er, status. */
	if (c->message_string != NULL || c->prompt_string != NULL)
		status = 1;
	else
		status = options_get_number(&c->session->options, "status");

	/* If only drawing status and it is present, don't need the rest. */
	if (status_only && status) {
		tty_draw_line(tty, &c->status, 0, 0, tty->sy - 1);
		tty_reset(tty);
		return;
	}

	/* Draw background and borders. */
	tty_reset(tty);
	strlcpy(border, " |-....--||+.", sizeof border);
	if (tty_term_has(tty->term, TTYC_ACSC)) {
		base = " xqlkmjwvtun~";
		for (ptr = base; *ptr != '\0'; ptr++) {
			if ((ch = tty_get_acs(tty, *ptr)) != '\0')
				border[ptr - base] = ch;
		}
		tty_putcode(tty, TTYC_SMACS);
	}
	for (j = 0; j < tty->sy - status; j++) {
		if (status_only && j != tty->sy - 1)
			continue;
		for (i = 0; i < tty->sx; i++) {
			type = screen_redraw_check_cell(c, i, j);
			if (type != CELL_INSIDE) {
				tty_cursor(tty, i, j, 0, 0);
				tty_putc(tty, border[type]);
			}
		}
	}
	tty_putcode(tty, TTYC_RMACS);

	/* Draw the panes. */
	TAILQ_FOREACH(wp, &w->panes, entry) {
		if (!window_pane_visible(wp))
			continue;
		for (i = 0; i < wp->sy; i++) {
			if (status_only && wp->yoff + i != tty->sy - 1)
				continue;
			tty_draw_line(tty, wp->screen, i, wp->xoff, wp->yoff);
		}
		if (c->flags & CLIENT_IDENTIFY)
			screen_redraw_draw_number(c, wp);
	}

	/* Draw the status line. */
	if (status)
		tty_draw_line(tty, &c->status, 0, 0, tty->sy - 1);
	tty_reset(tty);
}

/* Draw a single pane. */
void
screen_redraw_pane(struct client *c, struct window_pane *wp)
{
	u_int	i;

	for (i = 0; i < wp->sy; i++)
		tty_draw_line(&c->tty, wp->screen, i, wp->xoff, wp->yoff);
	tty_reset(&c->tty);
}

/* Draw number on a pane. */
void
screen_redraw_draw_number(struct client *c, struct window_pane *wp)
{
	struct tty		*tty = &c->tty;
	struct session		*s = c->session;
	struct grid_cell	 gc;
	u_int			 idx, px, py, i, j;
	int			 colour;
	char			 buf[16], *ptr;
	size_t			 len;

	idx = window_pane_index(wp->window, wp);
	len = xsnprintf(buf, sizeof buf, "%u", idx);

	if (wp->sx < len)
		return;
	colour = options_get_number(&s->options, "display-panes-colour");

	px = wp->sx / 2;
	py = wp->sy / 2;
	if (wp->sx < len * 6 || wp->sy < 5) {
		tty_cursor(tty, px - len / 2, py, wp->xoff, wp->yoff);
		memcpy(&gc, &grid_default_cell, sizeof gc);
		colour_set_fg(&gc, colour);
		tty_attributes(tty, &gc);
		tty_puts(tty, buf);
		return;
	}
	
	px -= len * 3;
	py -= 2;

	memcpy(&gc, &grid_default_cell, sizeof gc);
	colour_set_bg(&gc, colour);
	tty_attributes(tty, &gc);
	for (ptr = buf; *ptr != '\0'; ptr++) {
		if (*ptr < '0' || *ptr > '9')
			continue;
		idx = *ptr - '0';
		
		for (j = 0; j < 5; j++) {
			for (i = px; i < px + 5; i++) {
				tty_cursor(tty, i, py + j, wp->xoff, wp->yoff);
				if (clock_table[idx][j][i - px])
					tty_putc(tty, ' ');
			}
		}
		px += 6;
	}
}
