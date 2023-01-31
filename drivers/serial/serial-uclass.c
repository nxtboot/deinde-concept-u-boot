// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2014 The Chromium OS Authors.
 */

#define LOG_CATEGORY UCLASS_SERIAL

#include <config.h>
#include <dm.h>
#include <env_internal.h>
#include <errno.h>
#include <malloc.h>
#include <os.h>
#include <serial.h>
#include <stdio_dev.h>
#include <time.h>
#include <watchdog.h>
#include <asm/global_data.h>
#include <dm/lists.h>
#include <dm/device-internal.h>
#ifdef CONFIG_SANDBOX
#include <asm/state.h>
#endif
#include <dm/of_access.h>
#include <linux/build_bug.h>
#include <linux/delay.h>

DECLARE_GLOBAL_DATA_PTR;

#define ESC "\x1b"

/*
 * Table with supported baudrates (defined in config_xyz.h)
 */
static const unsigned long baudrate_table[] = CFG_SYS_BAUDRATE_TABLE;

#if IS_ENABLED(CONFIG_SERIAL_PRESENT)
static int serial_check_stdout(const void *blob, struct udevice **devp)
{
	int node = -1;
	const char *str, *p;
	int namelen;

	/* Check for a chosen console */
	str = fdtdec_get_chosen_prop(blob, "stdout-path");
	if (str) {
		p = strchr(str, ':');
		namelen = p ? p - str : strlen(str);
		/*
		 * This also deals with things like
		 *
		 *	stdout-path = "serial0:115200n8";
		 *
		 * since fdt_path_offset_namelen() treats a str not
		 * beginning with '/' as an alias and thus applies
		 * fdt_get_alias_namelen() to it.
		 */
		node = fdt_path_offset_namelen(blob, str, namelen);
	}

	if (node < 0)
		node = fdt_path_offset(blob, "console");
	if (!uclass_get_device_by_of_offset(UCLASS_SERIAL, node, devp))
		return 0;

	/*
	 * If the console is not marked to be bound before relocation, bind it
	 * anyway.
	 */
	if (node > 0 && !lists_bind_fdt(gd->dm_root, offset_to_ofnode(node),
					devp, NULL, false)) {
		if (device_get_uclass_id(*devp) == UCLASS_SERIAL &&
		    !device_probe(*devp))
			return 0;
	}

	return -ENODEV;
}

static void serial_find_console_or_panic(void)
{
	const void *blob = gd->fdt_blob;
	struct udevice *dev;
#ifdef CONFIG_SERIAL_SEARCH_ALL
	int ret;
#endif

	if (IS_ENABLED(CONFIG_OF_PLATDATA)) {
		uclass_first_device(UCLASS_SERIAL, &dev);
		if (dev) {
			gd->cur_serial_dev = dev;
			return;
		}
	} else if (IS_ENABLED(CONFIG_OF_CONTROL) && blob) {
		/* Live tree has support for stdout */
		if (of_live_active()) {
			struct device_node *np = of_get_stdout();

			if (np && !uclass_get_device_by_ofnode(UCLASS_SERIAL,
					np_to_ofnode(np), &dev)) {
				gd->cur_serial_dev = dev;
				return;
			}
		} else {
			if (!serial_check_stdout(blob, &dev)) {
				gd->cur_serial_dev = dev;
				return;
			}
		}
	}
	if (!IS_ENABLED(CONFIG_XPL_BUILD) || !IS_ENABLED(CONFIG_OF_CONTROL) ||
	    !blob) {
		/*
		 * Try to use CONFIG_CONS_INDEX if available (it is numbered
		 * from 1!).
		 *
		 * Failing that, get the device with sequence number 0, or in
		 * extremis just the first working serial device we can find.
		 * But we insist on having a console (even if it is silent).
		 */
#ifdef CONFIG_CONS_INDEX
#define INDEX (CONFIG_CONS_INDEX - 1)
#else
#define INDEX 0
#endif

#ifdef CONFIG_SERIAL_SEARCH_ALL
		if (!uclass_get_device_by_seq(UCLASS_SERIAL, INDEX, &dev) ||
		    !uclass_get_device(UCLASS_SERIAL, INDEX, &dev)) {
			if (dev_get_flags(dev) & DM_FLAG_ACTIVATED) {
				gd->cur_serial_dev = dev;
				return;
			}
		}

		/* Search for any working device */
		for (ret = uclass_first_device_check(UCLASS_SERIAL, &dev);
		     dev;
		     ret = uclass_next_device_check(&dev)) {
			if (!ret) {
				/* Device did succeed probing */
				gd->cur_serial_dev = dev;
				return;
			}
		}
#else
		if (!uclass_get_device_by_seq(UCLASS_SERIAL, INDEX, &dev) ||
		    !uclass_get_device(UCLASS_SERIAL, INDEX, &dev) ||
		    !uclass_first_device_err(UCLASS_SERIAL, &dev)) {
			gd->cur_serial_dev = dev;
			return;
		}
#endif

#undef INDEX
	}

#ifdef CONFIG_REQUIRE_SERIAL_CONSOLE
	panic_str("No serial driver found");
#endif
	gd->cur_serial_dev = NULL;
}
#endif /* CONFIG_SERIAL_PRESENT */

/**
 * check_valid_baudrate() - Check whether baudrate is valid or not
 *
 * @baud: baud rate to check
 * Return: 0 if OK, -ve on error
 */
static int check_valid_baudrate(int baud)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(baudrate_table); ++i) {
		if (baud == baudrate_table[i])
			return 0;
	}

	return -EINVAL;
}

int fetch_baud_from_dtb(void)
{
	int baud_value, ret;

	baud_value = ofnode_read_baud();
	ret = check_valid_baudrate(baud_value);
	if (ret)
		return ret;

	return baud_value;
}

/* Called prior to relocation */
int serial_init(void)
{
#if IS_ENABLED(CONFIG_SERIAL_PRESENT)
	/*
	 * Skip serial device probe before relocation if debug UART is enabled
	 * and early DM is skipped. Debug UART will handle console output during
	 * early boot. Full serial initialization happens in serial_initialize()
	 * after relocation.
	 */
	if ((IS_ENABLED(CONFIG_DEBUG_UART) && IS_ENABLED(CONFIG_SKIP_EARLY_DM)) &&
	    !(gd->flags & GD_FLG_RELOC))
		return 0;

	serial_find_console_or_panic();
	if (gd->cur_serial_dev)
		gd->flags |= GD_FLG_SERIAL_READY;

	if (IS_ENABLED(CONFIG_OF_SERIAL_BAUD)) {
		int ret = 0;
		char *ptr = (char*)&default_environment[0];

		/*
		 * Fetch the baudrate from the dtb and update the value in the
		 * default environment.
		 */
		ret = fetch_baud_from_dtb();
		if (ret != -EINVAL && ret != -EFAULT) {
			gd->baudrate = ret;

			while (*ptr != '\0' && *(ptr + 1) != '\0')
				ptr++;
			ptr += 2;
			sprintf(ptr, "baudrate=%d", gd->baudrate);
		}
	}
	serial_setbrg();
#endif

	return 0;
}

/* Called after relocation */
int serial_initialize(void)
{
	/* Scanning uclass to probe devices */
	if (IS_ENABLED(CONFIG_SERIAL_PROBE_ALL)) {
		int ret;

		ret  = uclass_probe_all(UCLASS_SERIAL);
		if (ret)
			return ret;
	}

	return serial_init();
}

static void _serial_flush(struct udevice *dev)
{
	struct dm_serial_ops *ops = serial_get_ops(dev);

	if (!ops->pending)
		return;
	while (ops->pending(dev, false) > 0)
		;
}

static void _serial_putc(struct udevice *dev, char ch)
{
	struct dm_serial_ops *ops = serial_get_ops(dev);
	int err;

	if (ch == '\n')
		_serial_putc(dev, '\r');

	do {
		err = ops->putc(dev, ch);
	} while (err == -EAGAIN);

	if (IS_ENABLED(CONFIG_CONSOLE_FLUSH_ON_NEWLINE) && ch == '\n')
		_serial_flush(dev);
}

static int __serial_puts(struct udevice *dev, const char *str, size_t len)
{
	struct dm_serial_ops *ops = serial_get_ops(dev);

	do {
		ssize_t written = ops->puts(dev, str, len);

		if (written == -EAGAIN)
			continue;
		if (written < 0)
			return written;
		str += written;
		len -= written;
	} while (len);

	return 0;
}

static void _serial_puts(struct udevice *dev, const char *str)
{
	struct dm_serial_ops *ops = serial_get_ops(dev);

	if (!IS_ENABLED(CONFIG_SERIAL_PUTS) || !ops->puts) {
		while (*str)
			_serial_putc(dev, *str++);
		return;
	}

	do {
		const char *newline = strchrnul(str, '\n');
		size_t len = newline - str;

		if (__serial_puts(dev, str, len))
			return;

		if (*newline && __serial_puts(dev, "\r\n", 2))
			return;

		if (IS_ENABLED(CONFIG_CONSOLE_FLUSH_ON_NEWLINE) && *newline)
			_serial_flush(dev);

		str += len + !!*newline;
	} while (*str);
}

static void _serial_putsn(struct udevice *dev, const char *str, size_t len)
{
	struct dm_serial_ops *ops = serial_get_ops(dev);

	if (!IS_ENABLED(CONFIG_SERIAL_PUTS) || !ops->puts) {
		while (len--)
			_serial_putc(dev, *str++);
		return;
	}

	while (len) {
		const char *newline = memchr(str, '\n', len);
		size_t seg_len = newline ? newline - str : len;

		if (__serial_puts(dev, str, seg_len))
			return;

		if (newline && __serial_puts(dev, "\r\n", 2))
			return;

		if (IS_ENABLED(CONFIG_CONSOLE_FLUSH_ON_NEWLINE) && newline)
			_serial_flush(dev);

		str += seg_len + !!newline;
		len -= seg_len + !!newline;
	}
}

static int __serial_getc(struct udevice *dev)
{
	struct dm_serial_ops *ops = serial_get_ops(dev);
	int err;

	do {
		err = ops->getc(dev);
		if (err == -EAGAIN)
			schedule();
	} while (err == -EAGAIN);

	return err >= 0 ? err : 0;
}

static int __serial_tstc(struct udevice *dev)
{
	struct dm_serial_ops *ops = serial_get_ops(dev);

	if (ops->pending)
		return ops->pending(dev, true);

	return 1;
}

#if IS_ENABLED(CONFIG_SERIAL_RX_BUFFER)
static int _serial_tstc(struct udevice *dev)
{
	struct serial_dev_priv *upriv = dev_get_uclass_priv(dev);
	uint wr, avail;

	BUILD_BUG_ON_NOT_POWER_OF_2(CONFIG_SERIAL_RX_BUFFER_SIZE);

	/* Read all available chars into the RX buffer while there's room */
	avail = CONFIG_SERIAL_RX_BUFFER_SIZE - (upriv->wr_ptr - upriv->rd_ptr);
	while (avail-- && __serial_tstc(dev)) {
		wr = upriv->wr_ptr++ % CONFIG_SERIAL_RX_BUFFER_SIZE;
		upriv->buf[wr] = __serial_getc(dev);
	}

	return upriv->rd_ptr != upriv->wr_ptr ? 1 : 0;
}

static int _serial_getc(struct udevice *dev)
{
	struct serial_dev_priv *upriv = dev_get_uclass_priv(dev);
	char val;
	uint rd;

	if (upriv->rd_ptr == upriv->wr_ptr)
		return __serial_getc(dev);

	rd = upriv->rd_ptr++ % CONFIG_SERIAL_RX_BUFFER_SIZE;
	val = upriv->buf[rd];

	return val;
}

#else /* IS_ENABLED(CONFIG_SERIAL_RX_BUFFER) */

static int _serial_getc(struct udevice *dev)
{
	return __serial_getc(dev);
}

static int _serial_tstc(struct udevice *dev)
{
	return __serial_tstc(dev);
}
#endif /* IS_ENABLED(CONFIG_SERIAL_RX_BUFFER) */

void serial_putc(char ch)
{
	if (gd->cur_serial_dev)
		_serial_putc(gd->cur_serial_dev, ch);
}

void serial_puts(const char *str)
{
	if (gd->cur_serial_dev)
		_serial_puts(gd->cur_serial_dev, str);
}

void serial_putsn(const char *str, int len)
{
	if (gd->cur_serial_dev)
		_serial_putsn(gd->cur_serial_dev, str, len);
}

#ifdef CONFIG_CONSOLE_FLUSH_SUPPORT
void serial_flush(void)
{
	if (!gd->cur_serial_dev)
		return;

	_serial_flush(gd->cur_serial_dev);
}
#endif

int serial_getc(void)
{
	if (!gd->cur_serial_dev)
		return 0;

	return _serial_getc(gd->cur_serial_dev);
}

int serial_tstc(void)
{
	if (!gd->cur_serial_dev)
		return 0;

	return _serial_tstc(gd->cur_serial_dev);
}

void serial_setbrg(void)
{
	struct dm_serial_ops *ops;

	if (!gd->cur_serial_dev)
		return;

	ops = serial_get_ops(gd->cur_serial_dev);
	if (ops->setbrg)
		ops->setbrg(gd->cur_serial_dev, gd->baudrate);
}

int serial_getconfig(struct udevice *dev, uint *config)
{
	struct dm_serial_ops *ops;

	ops = serial_get_ops(dev);
	if (ops->getconfig)
		return ops->getconfig(dev, config);

	return 0;
}

int serial_setconfig(struct udevice *dev, uint config)
{
	struct dm_serial_ops *ops;

	ops = serial_get_ops(dev);
	if (ops->setconfig)
		return ops->setconfig(dev, config);

	return 0;
}

int serial_getinfo(struct udevice *dev, struct serial_device_info *info)
{
	struct dm_serial_ops *ops;

	if (!info)
		return -EINVAL;

	info->baudrate = gd->baudrate;

	ops = serial_get_ops(dev);
	if (ops->getinfo)
		return ops->getinfo(dev, info);

	return -EINVAL;
}

void serial_stdio_init(void)
{
}

#if IS_ENABLED(CONFIG_DM_STDIO)

#if IS_ENABLED(CONFIG_SERIAL_PRESENT)
static void serial_stub_putc(struct stdio_dev *sdev, const char ch)
{
	_serial_putc(sdev->priv, ch);
}

static void serial_stub_puts(struct stdio_dev *sdev, const char *str)
{
	_serial_puts(sdev->priv, str);
}

#ifdef CONFIG_CONSOLE_FLUSH_SUPPORT
static void serial_stub_flush(struct stdio_dev *sdev)
{
	_serial_flush(sdev->priv);
}
#endif

static int serial_stub_getc(struct stdio_dev *sdev)
{
	return _serial_getc(sdev->priv);
}

static int serial_stub_tstc(struct stdio_dev *sdev)
{
	return _serial_tstc(sdev->priv);
}
#endif
#endif

/**
 * on_baudrate() - Update the actual baudrate when the env var changes
 *
 * This will check for a valid baudrate and only apply it if valid.
 */
static int on_baudrate(const char *name, const char *value, enum env_op op,
	int flags)
{
	int i;
	int baudrate;

	switch (op) {
	case env_op_create:
	case env_op_overwrite:
		/*
		 * Switch to new baudrate if new baudrate is supported
		 */
		baudrate = dectoul(value, NULL);

		/* Not actually changing */
		if (gd->baudrate == baudrate)
			return 0;

		for (i = 0; i < ARRAY_SIZE(baudrate_table); ++i) {
			if (baudrate == baudrate_table[i])
				break;
		}
		if (i == ARRAY_SIZE(baudrate_table)) {
			if ((flags & H_FORCE) == 0)
				printf("## Baudrate %d bps not supported\n",
				       baudrate);
			return 1;
		}
		if ((flags & H_INTERACTIVE) != 0) {
			printf("## Switch baudrate to %d bps and press ENTER ...\n",
			       baudrate);
			udelay(50000);
			flush();
		}

		gd->baudrate = baudrate;

		serial_setbrg();

		udelay(50000);

		if ((flags & H_INTERACTIVE) != 0)
			while (1) {
				if (getchar() == '\r')
					break;
			}

		return 0;
	case env_op_delete:
		printf("## Baudrate may not be deleted\n");
		return 1;
	default:
		return 0;
	}
}
U_BOOT_ENV_CALLBACK(baudrate, on_baudrate);

/**
 * term_get_char() - read a character from the console
 *
 * Wait for up to 100 ms to read a character from the console.
 *
 * @c:		pointer to the buffer to receive the character
 * Return:	0 on success, -ETIMEDOUT if timed out
 */
static int term_get_char(s32 *c)
{
	u64 timeout;

	/* Wait up to 100 ms for a character */
	timeout = timer_get_us() + 100000;

	while (!tstc())
		if (timer_get_us() > timeout)
			return -ETIMEDOUT;

	*c = getchar();

	return 0;
}

/**
 * term_read_reply() - receive and parse a reply from the terminal
 *
 * @n:		array of return values
 * @num:	number of return values expected
 * @end_char:	character indicating end of terminal message
 * Return:	non-zero indicates error
 */
static int term_read_reply(int *n, int num, char end_char)
{
	int ret, i = 0;
	s32 c;

	ret = term_get_char(&c);
	if (ret)
		return ret;
	if (c != cESC)
		return -EPROTO;

	ret = term_get_char(&c);
	if (ret)
		return ret;
	if (c != '[')
		return -EPROTO;

	n[0] = 0;
	while (1) {
		int ret;

		ret = term_get_char(&c);
		if (ret)
			return ret;

		if (c == ';') {
			i++;
			if (i >= num)
				return -EPROTO;
			n[i] = 0;
			continue;
		} else if (c == end_char) {
			break;
		} else if (c > '9' || c < '0') {
			return -EPROTO;
		}

		/* Read one more decimal position */
		n[i] *= 10;
		n[i] += c - '0';
	}
	if (i != num - 1)
		return -EPROTO;

	return 0;
}

int serial_query_size(int *rowsp, int *colsp)
{
	int ret = 0;
	int n[2];

	if (!IS_ENABLED(CONFIG_SERIAL_TERM_PRESENT))
		return -ENOENT;

#ifdef CONFIG_SANDBOX
	if (IS_ENABLED(CONFIG_SANDBOX)) {
		struct sandbox_state *state = state_get_current();
		if (state->no_term_present)
			return -ENOENT;
	}
#endif

	/* Empty input buffer */
	while (tstc())
		getchar();

	/*
	 * Not all terminals understand CSI [18t for querying the console size.
	 * We should adhere to escape sequences documented in the console_codes
	 * man page and the ECMA-48 standard.
	 *
	 * So here we follow a different approach. We position the cursor to the
	 * bottom right and query its position. Before leaving the function we
	 * restore the original cursor position.
	 */
	puts(ESC "7"		/* Save cursor position */
	     ESC "[r"		/* Set scrolling region to full window */
	     ESC "[999;999H"	/* Move to bottom right corner */
	     ESC "[6n");	/* Query cursor position */

	/* Read {rows,cols} */
	ret = term_read_reply(n, 2, 'R');
	if (!ret) {
		struct serial_priv *priv;
		struct uclass *uc;

		*colsp = n[1];
		*rowsp = n[0];

		/* Store in serial uclass private data if available */
		if (!uclass_get(UCLASS_SERIAL, &uc)) {
			priv = uclass_get_priv(uc);
			priv->rows = n[0];
			priv->cols = n[1];
		}
	}

	printf(ESC "8");	/* Restore cursor position */

	return ret;
}

int serial_get_size(int *rowsp, int *colsp)
{
	struct serial_priv *priv;
	struct uclass *uc;
	int ret;

	ret = uclass_get(UCLASS_SERIAL, &uc);
	if (ret)
		return ret;

	priv = uclass_get_priv(uc);

	/* Check if we have cached values */
	if (priv->rows && priv->cols) {
		*rowsp = priv->rows;
		*colsp = priv->cols;
		return 0;
	}

	/* No cached values, query the terminal */
	ret = serial_query_size(rowsp, colsp);

	return ret;
}

#if IS_ENABLED(CONFIG_SERIAL_PRESENT)
static int serial_post_probe(struct udevice *dev)
{
	struct dm_serial_ops *ops = serial_get_ops(dev);
#if IS_ENABLED(CONFIG_DM_STDIO)
	struct serial_dev_priv *upriv = dev_get_uclass_priv(dev);
	struct stdio_dev sdev;
#endif
	int ret;

	/* Set the baud rate */
	if (ops->setbrg) {
		ret = ops->setbrg(dev, gd->baudrate);
		if (ret)
			return ret;
	}

#if IS_ENABLED(CONFIG_DM_STDIO)
	if (!(gd->flags & GD_FLG_RELOC))
		return 0;
	memset(&sdev, '\0', sizeof(sdev));

	strlcpy(sdev.name, dev->name, sizeof(sdev.name));
	sdev.flags = DEV_FLAGS_OUTPUT | DEV_FLAGS_INPUT | DEV_FLAGS_DM;
	sdev.priv = dev;
	sdev.putc = serial_stub_putc;
	sdev.puts = serial_stub_puts;
	STDIO_DEV_ASSIGN_FLUSH(&sdev, serial_stub_flush);
	sdev.getc = serial_stub_getc;
	sdev.tstc = serial_stub_tstc;

	stdio_register_dev(&sdev, &upriv->sdev);
#endif
	return 0;
}

static int serial_pre_remove(struct udevice *dev)
{
#if IS_ENABLED(CONFIG_SYS_STDIO_DEREGISTER)
	struct serial_dev_priv *upriv = dev_get_uclass_priv(dev);

	if (stdio_deregister_dev(upriv->sdev, true))
		return -EPERM;
#endif

	return 0;
}

UCLASS_DRIVER(serial) = {
	.id		= UCLASS_SERIAL,
	.name		= "serial",
	.flags		= DM_UC_FLAG_SEQ_ALIAS,
	.post_probe	= serial_post_probe,
	.pre_remove	= serial_pre_remove,
	.per_device_auto	= sizeof(struct serial_dev_priv),
	.priv_auto	= sizeof(struct serial_priv),
};
#endif
