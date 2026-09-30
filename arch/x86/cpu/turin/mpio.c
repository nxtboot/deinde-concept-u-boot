// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 *
 * PCIe/SATA link setup on AMD EPYC Turin through the MPIO firmware
 *
 * The processor's I/O lanes are driven by a microcontroller, the MPIO, which
 * trains the links once the BIOS tells it how the board wires them up. The
 * description is an array of 'ASK' entries in DRAM, one per link, which the
 * MPIO copies in; the BIOS then steps it through mapping the lanes to
 * controllers, programming them, releasing PERST# and training. This
 * follows openSIL's MpioEarlyInitV1(), with the defaults this platform uses
 * and without the optional tuning (straps, proxy writes, hotplug), none of
 * which is needed to bring the links up.
 */

#define LOG_CATEGORY LOGC_ARCH

#include <dm.h>
#include <log.h>
#include <time.h>
#include <asm/io.h>
#include <asm/pci.h>
#include <asm/arch/cpu.h>
#include <dm/ofnode.h>
#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/string.h>

/* SMN index/data pair in the root complex's config space */
#define SMN_INDEX		0xb8
#define SMN_DATA		0xbc

/* MPIO mailbox (C2PMSG registers) on SMN */
#define MPIO_RESP		0x0c9109c8
#define MPIO_ARG0		0x0c9109cc
#define MPIO_DOORBELL		0x0c910954
#define MPIO_RESP_READY		BIT(31)
#define MPIO_RESP_STATUS	0xff
#define MPIO_RESULT_OK		1
#define MPIO_NUM_ARGS		6

/* Messages */
#define MPIO_GET_STATUS		0x01
#define MPIO_SET_GLOBAL_CONFIG	0x02
#define MPIO_GET_ASK_RESULT	0x03
#define MPIO_SETUP_LINK		0x04
#define MPIO_TRANSFER_ASK	0x07
#define MPIO_POSTED		(3 << 8)	/* reply at once, work later */

/* MPIO_SETUP_LINK phases */
#define SETUP_MAP		BIT(0)
#define SETUP_CONFIGURE		BIT(1)
#define SETUP_RECONFIGURE	BIT(2)
#define SETUP_PERST		BIT(3)
#define SETUP_TRAIN		BIT(4)
#define SETUP_ENUMERATE		BIT(5)

/* MPIO_TRANSFER_ASK flags */
#define ASK_SELECTED_LINKS	BIT(0)
#define ASK_TO_MPIO		BIT(1)

/*
 * One ASK entry: the link description followed by its status, which the
 * MPIO fills in. Only the fields used here are named
 */
struct mpio_ask {
	u32 desc;		/* lanes, reversal, type */
	u32 gpio;		/* PERST# handle, channel type, ancillary data */
	u32 devfn;		/* port device and function on the root bus */
	u8 present;		/* bit 0: port present */
	u8 attr[19];
	u32 status;
	u32 reserved[4];
} __packed;

static_assert(sizeof(struct mpio_ask) == 0x34);

#define ASK_DESC_LANES_SHIFT	16
#define ASK_DESC_REVERSED	BIT(22)
#define ASK_DESC_TYPE_SHIFT	28
#define ASK_STATUS_STATE	0xf
#define ASK_STATE_TRAINED	6

#define MPIO_MAX_LINKS		64

/* Board link flags in the devicetree */
#define LINK_FLAG_REVERSED	BIT(0)
#define LINK_FLAG_TYPE_SHIFT	4
#define LINK_FLAG_TYPE_MASK	0xf

/* The slot resets (PCIE_RST0/1_L) are FCH GPIOs, left asserted by the ABL */
#define ACPIMMIO_BASE		0xfed80000
#define GPIO_PCIE_RST1		(ACPIMMIO_BASE + 0x1500 + 26 * 4)
#define RMTGPIO_PCIE_RST0	(ACPIMMIO_BASE + 0x1200 + 0x28)
#define GPIO_OUT_HIGH		0xc40000

static struct mpio_ask ask_buf[MPIO_MAX_LINKS] __aligned(64);

static u32 smn_read(u32 reg)
{
	ulong val;

	pci_x86_write_config(PCI_BDF(0, 0, 0), SMN_INDEX, reg, PCI_SIZE_32);
	pci_x86_read_config(PCI_BDF(0, 0, 0), SMN_DATA, &val, PCI_SIZE_32);

	return val;
}

static void smn_write(u32 reg, u32 val)
{
	pci_x86_write_config(PCI_BDF(0, 0, 0), SMN_INDEX, reg, PCI_SIZE_32);
	pci_x86_write_config(PCI_BDF(0, 0, 0), SMN_DATA, val, PCI_SIZE_32);
}

static int mpio_wait_ready(ulong timeout_ms, u32 *respp)
{
	ulong start = get_timer(0);
	u32 resp;

	do {
		resp = smn_read(MPIO_RESP);
		if (resp & MPIO_RESP_READY) {
			*respp = resp;
			return 0;
		}
	} while (get_timer(start) < timeout_ms);

	return -ETIMEDOUT;
}

/**
 * mpio_request() - Send a message to the MPIO and wait for its reply
 *
 * @msg: Message, with MPIO_POSTED if the MPIO should reply at once
 * @args: Six argument words, updated with the reply
 * Return: 0 if OK, -EIO if the MPIO reports an error, -ETIMEDOUT if it does
 *	not reply
 */
static int mpio_request(u32 msg, u32 args[MPIO_NUM_ARGS])
{
	u32 resp;
	int ret, i;

	ret = mpio_wait_ready(1000, &resp);
	if (ret)
		return log_msg_ret("rdy", ret);
	for (i = 0; i < MPIO_NUM_ARGS; i++)
		smn_write(MPIO_ARG0 + 4 * i, args[i]);
	smn_write(MPIO_RESP, (msg & 0xfff) << 8);
	smn_write(MPIO_DOORBELL, ~0U);
	/* even a posted request can take a few hundred milliseconds */
	ret = mpio_wait_ready(5000, &resp);
	if (ret)
		return log_msg_ret("rsp", ret);
	for (i = 0; i < MPIO_NUM_ARGS; i++)
		args[i] = smn_read(MPIO_ARG0 + 4 * i);
	if ((resp & MPIO_RESP_STATUS) != MPIO_RESULT_OK) {
		log_debug("msg %x: status %x err %x\n", msg, resp, args[0]);
		return log_msg_ret("sts", -EIO);
	}

	return 0;
}

/* Wait for the MPIO to finish the work of a posted message */
static int mpio_wait_idle(void)
{
	ulong start = get_timer(0);
	u32 args[MPIO_NUM_ARGS];
	int ret;

	do {
		memset(args, '\0', sizeof(args));
		ret = mpio_request(MPIO_GET_STATUS, args);
		if (ret)
			return ret;
		if (!args[0])
			return 0;
	} while (get_timer(start) < 5000);

	return log_msg_ret("idl", -ETIMEDOUT);
}

static int mpio_setup_link(uint phases)
{
	u32 args[MPIO_NUM_ARGS] = { phases };
	ulong start = get_timer(0);
	int ret;

	ret = mpio_request(MPIO_SETUP_LINK | MPIO_POSTED, args);
	if (!ret)
		ret = mpio_wait_idle();
	log_debug("setup %x: %d, %lu ms\n", phases, ret, get_timer(start));

	return ret;
}

static int mpio_read_asks(void)
{
	u32 args[MPIO_NUM_ARGS] = { 0, (ulong)ask_buf };

	return mpio_request(MPIO_GET_ASK_RESULT, args);
}

/**
 * mpio_fill_asks() - Build the ASK array from the board's link list
 *
 * Each devicetree entry is <first-lane lane-count flags devfn>, with the
 * lanes numbered as the MPIO numbers its PHY lanes
 *
 * Return: number of links, or -ve on error
 */
static int mpio_fill_asks(ofnode node)
{
	u32 cells[MPIO_MAX_LINKS * 4];
	int count, i;

	count = ofnode_read_size(node, "amd,links");
	if (count <= 0 || count % 16 || count > sizeof(cells))
		return log_msg_ret("lnk", -EINVAL);
	count /= 16;
	if (ofnode_read_u32_array(node, "amd,links", cells, count * 4))
		return log_msg_ret("rd", -EINVAL);

	memset(ask_buf, '\0', sizeof(ask_buf));
	for (i = 0; i < count; i++) {
		struct mpio_ask *ask = &ask_buf[i];
		u32 *cell = &cells[i * 4];
		u32 flags = cell[2];

		ask->desc = cell[0] | cell[1] << ASK_DESC_LANES_SHIFT |
			((flags >> LINK_FLAG_TYPE_SHIFT) & LINK_FLAG_TYPE_MASK) <<
			ASK_DESC_TYPE_SHIFT;
		if (flags & LINK_FLAG_REVERSED)
			ask->desc |= ASK_DESC_REVERSED;
		ask->devfn = cell[3];
		ask->present = 1;
	}

	return count;
}

int turin_mpio_init(void)
{
	/*
	 * openSIL's global configuration for this platform: skip vetting,
	 * use the PHY SRAM with valid PHY firmware, enable the workaround for
	 * non-compliant devices and set a TX FIFO read-pointer offset of 0x76,
	 * plus two top bits which openSIL sets but its headers do not name
	 */
	u32 args[MPIO_NUM_ARGS] = { 0x800008c1, 0, 0, 0x76000000,
				    0x80000000, 0 };
	ulong start = get_timer(0);
	int count, trained, ret, i;
	ofnode node;

	node = ofnode_path("/mpio");
	if (!ofnode_valid(node))
		return 0;
	count = mpio_fill_asks(node);
	if (count < 0)
		return count;

	ret = mpio_request(MPIO_SET_GLOBAL_CONFIG, args);
	if (ret)
		return log_msg_ret("glb", ret);

	ret = mpio_wait_idle();
	if (ret)
		return ret;
	memset(args, '\0', sizeof(args));
	args[1] = (ulong)ask_buf;
	args[2] = ASK_SELECTED_LINKS | ASK_TO_MPIO;
	args[4] = count;
	ret = mpio_request(MPIO_TRANSFER_ASK, args);
	if (ret)
		return log_msg_ret("ask", ret);

	ret = mpio_setup_link(SETUP_MAP);
	if (!ret)
		ret = mpio_setup_link(SETUP_CONFIGURE | SETUP_RECONFIGURE);
	if (!ret)
		ret = mpio_setup_link(SETUP_PERST);
	if (ret)
		return log_msg_ret("stp", ret);

	/* Release the slot resets, then train */
	writel(GPIO_OUT_HIGH, GPIO_PCIE_RST1);
	writel(GPIO_OUT_HIGH, RMTGPIO_PCIE_RST0);
	ret = mpio_setup_link(SETUP_TRAIN | SETUP_ENUMERATE);
	if (ret)
		return log_msg_ret("trn", ret);

	ret = mpio_read_asks();
	if (ret)
		return log_msg_ret("res", ret);
	for (i = 0, trained = 0; i < count; i++) {
		u32 state = ask_buf[i].status & ASK_STATUS_STATE;

		log_debug("link %2d: lanes %3u state %u\n", i,
			  ask_buf[i].desc & 0xffff, state);
		if (state == ASK_STATE_TRAINED)
			trained++;
	}
	log_info("MPIO: %d of %d links up (%lu ms)\n", trained, count,
		 get_timer(start));

	return 0;
}
