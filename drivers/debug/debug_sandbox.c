// SPDX-License-Identifier: GPL-2.0+
/*
 * Sandbox debug device, which records what is written to it
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <debug_dev.h>
#include <dm.h>
#include <asm/test.h>
#include <linux/errno.h>

/**
 * struct sandbox_debug_priv - private data for the sandbox debug device
 * @buf: characters written so far, nul-terminated
 * @len: number of characters written so far
 */
struct sandbox_debug_priv {
	char buf[SANDBOX_DEBUG_SIZE];
	int len;
};

const char *sandbox_debug_get(struct udevice *dev)
{
	struct sandbox_debug_priv *priv = dev_get_priv(dev);

	return priv->buf;
}

void sandbox_debug_clear(struct udevice *dev)
{
	struct sandbox_debug_priv *priv = dev_get_priv(dev);

	priv->len = 0;
	priv->buf[0] = '\0';
}

static int sandbox_debug_puts(struct udevice *dev, const char *str, size_t len)
{
	struct sandbox_debug_priv *priv = dev_get_priv(dev);
	int space = SANDBOX_DEBUG_SIZE - 1 - priv->len;

	if (space <= 0)
		return -ENOSPC;

	/*
	 * Write only part of the string if there is not room for all of it, so
	 * that the uclass loop is exercised
	 */
	if (len > (size_t)space)
		len = space;

	memcpy(priv->buf + priv->len, str, len);
	priv->len += len;
	priv->buf[priv->len] = '\0';

	return len;
}

static const struct dm_debug_ops sandbox_debug_ops = {
	.puts	= sandbox_debug_puts,
};

static const struct udevice_id sandbox_debug_ids[] = {
	{ .compatible = "sandbox,debug" },
	{ }
};

U_BOOT_DRIVER(sandbox_debug) = {
	.name	= "sandbox_debug",
	.id	= UCLASS_DEBUG,
	.of_match = sandbox_debug_ids,
	.priv_auto = sizeof(struct sandbox_debug_priv),
	.ops	= &sandbox_debug_ops,
};
