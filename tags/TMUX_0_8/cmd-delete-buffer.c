/* $Id: cmd-delete-buffer.c,v 1.4 2009-01-19 18:23:40 nicm Exp $ */

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

#include <stdlib.h>

#include "tmux.h"

/*
 * Delete a paste buffer.
 */

int	cmd_delete_buffer_exec(struct cmd *, struct cmd_ctx *);

const struct cmd_entry cmd_delete_buffer_entry = {
	"delete-buffer", "deleteb",
	CMD_BUFFER_SESSION_USAGE,
	0,
	cmd_buffer_init,
	cmd_buffer_parse,
	cmd_delete_buffer_exec,
	cmd_buffer_send,
	cmd_buffer_recv,
	cmd_buffer_free,
	cmd_buffer_print
};

int
cmd_delete_buffer_exec(struct cmd *self, struct cmd_ctx *ctx)
{
	struct cmd_buffer_data	*data = self->data;
	struct session		*s;

	if ((s = cmd_find_session(ctx, data->target)) == NULL)
		return (-1);

	if (data->buffer == -1)
		paste_free_top(&s->buffers);
	else if (paste_free_index(&s->buffers, data->buffer) != 0) {
		ctx->error(ctx, "no buffer %d", data->buffer);
		return (-1);
	}
	
	return (0);
}
