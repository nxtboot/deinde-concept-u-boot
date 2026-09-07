// SPDX-License-Identifier: GPL-2.0+
/*
 * Debug output through a Dediprog EM100Pro SPI-flash emulator
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 *
 * The EM100Pro can watch the SPI bus for a command which is not a flash
 * access and treat what follows as a message for the host, which the
 * 'em100 --terminal' tool then prints. This provides a way of getting debug
 * output from boards which boot from an emulated flash chip but have no
 * usable UART.
 *
 * Each message consists of a reserved byte, a command telling the EM100 to
 * put what follows in its uFIFO, a signature, a type and a length, followed
 * by the data. The command used is 0x11 by default; it must match the value
 * the host has told the EM100 to look for.
 */

#include <debug_dev.h>
#include <dm.h>
#include <em100.h>
#include <log.h>
#include <spi.h>
#include <spi-mem.h>
#include <linux/errno.h>

/**
 * struct em100_priv - private data for the EM100 debug device
 * @cmd: SPI command which the EM100 treats as carrying a message
 */
struct em100_priv {
	u8 cmd;
};

static int em100_puts(struct udevice *dev, const char *str, size_t len)
{
	struct em100_priv *priv = dev_get_priv(dev);
	struct spi_slave *slave = dev_get_parent_priv(dev);
	u8 buf[EM100_HDR_LEN + EM100_MAX_DATA];
	struct spi_mem_op op;
	int ret;

	if (!len)
		return 0;
	if (len > EM100_MAX_DATA)
		len = EM100_MAX_DATA;

	/*
	 * Send the message as a command with no address, using the spi-mem
	 * interface rather than a plain transfer, since flash controllers
	 * which build a transaction from an opcode, an address and data cannot
	 * do the latter. The Intel controller found on x86 is one of these,
	 * and it is the usual place to want this.
	 */
	op = (struct spi_mem_op)SPI_MEM_OP(SPI_MEM_OP_CMD(priv->cmd, 1),
					   SPI_MEM_OP_NO_ADDR,
					   SPI_MEM_OP_NO_DUMMY,
					   SPI_MEM_OP_DATA_OUT(EM100_HDR_LEN +
							       len, buf, 1));

	/*
	 * The controller may not manage the whole message in one go; the Intel
	 * one is typically limited to 64 bytes. Send as much as it will take
	 * and let the caller come back for the rest.
	 */
	ret = spi_mem_adjust_op_size(slave, &op);
	if (ret)
		return ret;
	if (op.data.nbytes <= EM100_HDR_LEN)
		return -ENOSPC;

	len = op.data.nbytes - EM100_HDR_LEN;
	em100_put_header(buf, len);
	memcpy(&buf[EM100_HDR_LEN], str, len);
	op.data.nbytes = EM100_HDR_LEN + len;

	ret = spi_mem_exec_op(slave, &op);
	if (ret)
		return ret;

	return len;
}

static int em100_of_to_plat(struct udevice *dev)
{
	struct em100_priv *priv = dev_get_priv(dev);

	priv->cmd = dev_read_u32_default(dev, "dediprog,command",
					 EM100_DEFAULT_CMD);

	return 0;
}

static const struct dm_debug_ops em100_ops = {
	.puts	= em100_puts,
};

static const struct udevice_id em100_ids[] = {
	{ .compatible = "dediprog,em100-debug" },
	{ }
};

U_BOOT_DRIVER(em100_debug) = {
	.name	= "em100_debug",
	.id	= UCLASS_DEBUG,
	.of_match = em100_ids,
	.of_to_plat = em100_of_to_plat,
	.priv_auto = sizeof(struct em100_priv),
	.ops	= &em100_ops,
};
