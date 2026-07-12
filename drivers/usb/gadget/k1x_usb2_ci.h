/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2023, Spacemit Inc.
 */
#ifndef __GADGET__MV_UDC_H__
#define __GADGET__MV_UDC_H__

#ifdef CONFIG_SPL_BUILD
#define NUM_ENDPOINTS		2
#else
#define NUM_ENDPOINTS		6
#endif

struct mv_udc {
	u32 pad0[16];
#define MICRO_8FRAME	0x8
#define USBCMD_ITC(x)	((((x) > 0xff) ? 0xff : x) << 16)
#define USBCMD_FS2	BIT(15)
#define USBCMD_SETUP_TRIPWIRE_SET	BIT(13)
#define USBCMD_RST	BIT(1)
#define USBCMD_RUN	(1)
	u32 usbcmd;		/* 0x140 */
#define STS_SLI		BIT(8)
#define STS_URI		BIT(6)
#define STS_SEI		BIT(4)
#define STS_PCI		BIT(2)
#define STS_UEI		BIT(1)
#define STS_UI		BIT(0)
	u32 usbsts;		/* 0x144 */
	u32 usbintr;		/* 0x148 */
	u32 frindex;		/* 0x14c */
	u32 pad1[1];
	u32 devaddr;		/* 0x154 */
	u32 epinitaddr;		/* 0x158 */
	u32 pad2[10];
#define PTS_ENABLE	2
#define PTS(x)		(((x) & 0x3U) << 30)
#define PFSC		BIT(24)
	u32 portsc;		/* 0x184 */
	u32 pad3[8];
#define USBMODE_CM_MASK	3
#define USBMODE_DEVICE	2
	u32 usbmode;		/* 0x1a8 */
	u32 epsetupstat;		/* 0x1ac */
#define EPT_TX(x)	(1 << (((x) & 0xffff) + 16))
#define EPT_RX(x)	(1 << ((x) & 0xffff))
	u32 epprime;		/* 0x1b0 */
	u32 epflush;		/* 0x1b4 */
	u32 pad4;
	u32 epcomp;		/* 0x1bc */
#define CTRL_TXE	BIT(23)
#define CTRL_TXR	BIT(22)
#define CTRL_RXE	BIT(7)
#define CTRL_RXR	BIT(6)
#define CTRL_TXT_BULK	(2 << 18)
#define CTRL_RXT_BULK	(2 << 2)
	u32 epctrl[16];		/* 0x1c0 */
#define EPCTRL_TX_DATA_TOGGLE_RST       (0x00400000)
#define EPCTRL_TX_EP_STALL          (0x00010000)
#define EPCTRL_RX_EP_STALL          (0x00000001)
#define EPCTRL_RX_DATA_TOGGLE_RST       (0x00000040)
};

struct mv_req {
	struct usb_request	req;
	struct list_head	queue;
	/* Bounce buffer allocated if needed to align the transfer */
	u8 *b_buf;
	u32 b_len;
	/* Buffer for the current transfer. Either req.buf/len or b_buf/len */
	u8 *hw_buf;
	u32 hw_len;
};

struct mv_ep {
	struct usb_ep ep;
	struct list_head queue;
	bool req_primed;
	const struct usb_endpoint_descriptor *desc;
};

struct mv_drv {
	struct usb_gadget		gadget;
	struct mv_req			*ep0_req;
	struct usb_gadget_driver	*driver;
	struct ehci_ctrl		*ctrl;
	struct ept_queue_head		*epts;
	struct ept_queue_item		*items[2 * NUM_ENDPOINTS];
	u8				*items_mem;
	struct mv_ep			ep[2 * NUM_ENDPOINTS];
};

struct ept_queue_head {
	unsigned int config;
	unsigned int current;	/* read-only */

	unsigned int next;
	unsigned int info;
	unsigned int page0;
	unsigned int page1;
	unsigned int page2;
	unsigned int page3;
	unsigned int page4;
	unsigned int reserved_0;

	unsigned char setup_data[8];

	unsigned int reserved_1;
	unsigned int reserved_2;
	unsigned int reserved_3;
	unsigned int reserved_4;
};

#define CFG_MAX_PKT(n)		((n) << 16)
#define CFG_ZLT			BIT(29)	/* stop on zero-len xfer */
#define CFG_IOS			BIT(15)	/* IRQ on setup */

struct ept_queue_item {
	unsigned int next;
	unsigned int info;
	unsigned int page0;
	unsigned int page1;
	unsigned int page2;
	unsigned int page3;
	unsigned int page4;
	unsigned int reserved;
};

#define TERMINATE 1
#define INFO_BYTES(n)		((n) << 16)
#define INFO_IOC		BIT(15)
#define INFO_ACTIVE		BIT(7)
#define INFO_HALTED		BIT(6)
#define INFO_BUFFER_ERROR	BIT(5)
#define INFO_TX_ERROR		BIT(3)

#define K1X_APMU_BASE		0xd4282800
#define PMUA_USB_CLK_RES_CTRL			(K1X_APMU_BASE + 0x5c)
#define PMUA_USB_CLK_RES_CTRL_USB_AXICLK_EN	(0x1 << 1)
#define PMUA_USB_CLK_RES_CTRL_USB_AXI_RST	(0x1 << 0)

#define K1X_USB_BASE		0xC0900000

#define USB2_PHY_REG_BASE   0xC0940000
#define USB2_PHY_REG01      (USB2_PHY_REG_BASE + 0x04)
#define USB2_PHY_REG04      (USB2_PHY_REG_BASE + 0x10)
#define USB2_PHY_REG06      (USB2_PHY_REG_BASE + 0x18)
#define USB2_PHY_REG08      (USB2_PHY_REG_BASE + 0x20)
#define USB2_PHY_REG0D      (USB2_PHY_REG_BASE + 0x34)
#define USB2_PHY_REG26      (USB2_PHY_REG_BASE + 0x98)
#define USB2_PHY_REG22      (USB2_PHY_REG_BASE + 0x88)

#define USB2_PLL_PLLREADY   BIT(0)
#define USB2_CFG_FORCE_CDRCLK   BIT(6)
#define USB2_CFG_HS_SRC_SEL	BIT(0)

static inline uint32_t reg32_read(uintptr_t reg)
{
	return (*(volatile uint32_t *)(reg));
}

static inline void reg32_write(uintptr_t reg, uint32_t value)
{
	(*(volatile uint32_t *)(reg)) = value;
}

static inline void reg32_modify(uintptr_t reg, uint32_t clear_mask, uint32_t set_mask)
{
	reg32_write(reg, (reg32_read(reg) & ~clear_mask) | set_mask);
}

#endif
