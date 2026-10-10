// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the cdp command
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <command.h>
#include <dm.h>
#include <env.h>
#include <net.h>
#include <time.h>
#include <asm/eth.h>
#include <test/cmd.h>
#include <test/test.h>
#include <test/ut.h>

/* Ethernet device used by the tests */
#define CDP_ETHACT	"eth@10002000"

/* Multicast address CDP uses, from net_cdp_ethaddr in net/cdp.c */
#define CDP_DEST	{ 0x01, 0x00, 0x0c, 0xcc, 0xcc, 0xcc }

/* Time between triggers in ms, and how many the command sends */
#define CDP_TIMEOUT	250
#define CDP_TRIGGERS	3

/* Protocol version and time-to-live the command sends */
#define CDP_VERSION	2
#define CDP_TTL		180

/* Size of the SNAP header and of the CDP header which follows it */
#define SNAP_HDR_SIZE	8
#define CDP_HDR_SIZE	4

/* Length of a trigger, which carries no TLVs */
#define CDP_TRIGGER_LEN	(ETHER_HDR_SIZE + SNAP_HDR_SIZE + CDP_HDR_SIZE)

/* TLV numbers the emulated switch sends */
#define CDP_DEVICE_ID_TLV	0x0001
#define CDP_NATIVE_VLAN_TLV	0x000a
#define CDP_APPLIANCE_VLAN_TLV	0x000e

/* Warning the command gives for a version newer than it understands */
#define CDP_WARNING \
	"**WARNING: CDP packet received with a protocol version 3 > 2"

/* Values it offers */
#define CDP_DEVICE_ID		"sw-1a"
#define CDP_NATIVE_VLAN		42
#define CDP_APPLIANCE_TYPE	1
#define CDP_APPLIANCE_VLAN	100

/* SNAP header which marks a CDP packet, from cdp_snap_hdr in net/cdp.c */
static const u8 cdp_snap[SNAP_HDR_SIZE] = {
	0xaa, 0xaa, 0x03, 0x00, 0x00, 0x0c, 0x20, 0x00 };

/* The same header with the protocol changed, so that CDP does not claim it */
static const u8 other_snap[SNAP_HDR_SIZE] = {
	0xaa, 0xaa, 0x03, 0x00, 0x00, 0x0c, 0x01, 0x0b };

/**
 * struct cdp_hdr - header which follows the SNAP header
 *
 * @version: protocol version, which must be at least 2
 * @ttl: time-to-live in seconds, which must not be zero
 * @csum: checksum over this header and the TLVs which follow it
 */
struct cdp_hdr {
	u8 version;
	u8 ttl;
	u16 csum;
};

/**
 * enum cdp_reply - what the emulated switch answers a trigger with
 *
 * @CDP_REPLY_NONE: nothing at all
 * @CDP_REPLY_OK: a well-formed packet offering both VLANs
 * @CDP_REPLY_SHORT: a packet too short to hold a CDP header
 * @CDP_REPLY_SNAP: a SNAP header for some other protocol
 * @CDP_REPLY_CSUM: a well-formed packet whose checksum is wrong
 * @CDP_REPLY_V3: a packet announcing protocol version 3
 * @CDP_REPLY_TLV: a packet holding a TLV whose length is zero
 */
enum cdp_reply {
	CDP_REPLY_NONE,
	CDP_REPLY_OK,
	CDP_REPLY_SHORT,
	CDP_REPLY_SNAP,
	CDP_REPLY_CSUM,
	CDP_REPLY_V3,
	CDP_REPLY_TLV,
};

/**
 * struct cdp_ctx - state shared with the transmit handler
 *
 * @uts: test state, used by the ut_assert macros in the handler
 * @reply: packet to answer the first trigger with
 * @triggers: number of triggers the command has sent
 * @protlen: 802.3 length field of the last trigger
 * @len: length of the last trigger in bytes
 * @version: protocol version of the last trigger
 * @ttl: time-to-live of the last trigger
 * @dest_ok: true if the last trigger went to the CDP multicast address
 * @snap_ok: true if the last trigger carried the CDP SNAP header
 * @csum_ok: true if the checksum of the last trigger is right
 */
struct cdp_ctx {
	struct unit_test_state *uts;
	enum cdp_reply reply;
	int triggers;
	int protlen;
	int len;
	int version;
	int ttl;
	bool dest_ok;
	bool snap_ok;
	bool csum_ok;
};

/**
 * add_tlv() - add a type/length/value field
 *
 * The length written covers the four bytes of the header as well as the data.
 * Everything is stored a byte at a time, since a TLV holding an odd number of
 * bytes leaves the next one unaligned.
 *
 * @p: position in the packet
 * @type: TLV number
 * @data: data to store
 * @size: number of bytes of @data
 * Return: position just after the field
 */
static u8 *add_tlv(u8 *p, uint type, const void *data, uint size)
{
	uint tlen = size + 4;

	*p++ = type >> 8;
	*p++ = type & 0xff;
	*p++ = tlen >> 8;
	*p++ = tlen & 0xff;
	memcpy(p, data, size);

	return p + size;
}

/**
 * build_reply() - fill in the packet the emulated switch sends back
 *
 * The reply offers a device ID, which the command ignores, then the native and
 * appliance VLANs, which it reads. The device ID is five bytes long, so that
 * the whole of the checksummed area is an even number of bytes:
 * cdp_compute_csum() sign-extends a trailing odd byte, which
 * compute_ip_checksum() does not, so only an even length can be checksummed
 * here.
 *
 * @ctx: handler state, which says what sort of reply to build
 * @buf: buffer to build the packet in
 * Return: length of the packet in bytes
 */
static int build_reply(struct cdp_ctx *ctx, void *buf)
{
	struct ethernet_hdr *eth = buf;
	static const u8 dest[ARP_HLEN] = CDP_DEST;
	struct cdp_hdr *cdp;
	u8 appliance[3];
	u8 *p, *start;
	u16 nvlan;

	memcpy(eth->et_dest, dest, ARP_HLEN);
	memset(eth->et_src, 0x11, ARP_HLEN);

	p = (u8 *)buf + ETHER_HDR_SIZE;
	memcpy(p, ctx->reply == CDP_REPLY_SNAP ? other_snap : cdp_snap,
	       sizeof(cdp_snap));
	p += sizeof(cdp_snap);

	if (ctx->reply == CDP_REPLY_SHORT) {
		/* Stop short of a CDP header, leaving the packet too short */
		memset(p, 0, 3);
		p += 3;
	} else {
		/* The checksum covers everything from the CDP header onwards */
		start = p;
		cdp = (struct cdp_hdr *)p;
		cdp->version = ctx->reply == CDP_REPLY_V3 ? 3 : CDP_VERSION;
		cdp->ttl = CDP_TTL;
		cdp->csum = 0;
		p += sizeof(*cdp);

		if (ctx->reply == CDP_REPLY_TLV) {
			/* A length of zero, which does not advance the parser */
			*p++ = CDP_NATIVE_VLAN_TLV >> 8;
			*p++ = CDP_NATIVE_VLAN_TLV & 0xff;
			*p++ = 0;
			*p++ = 0;
		} else {
			p = add_tlv(p, CDP_DEVICE_ID_TLV, CDP_DEVICE_ID,
				    strlen(CDP_DEVICE_ID));

			nvlan = htons(CDP_NATIVE_VLAN);
			p = add_tlv(p, CDP_NATIVE_VLAN_TLV, &nvlan,
				    sizeof(nvlan));

			appliance[0] = CDP_APPLIANCE_TYPE;
			appliance[1] = CDP_APPLIANCE_VLAN >> 8;
			appliance[2] = CDP_APPLIANCE_VLAN & 0xff;
			p = add_tlv(p, CDP_APPLIANCE_VLAN_TLV, appliance,
				    sizeof(appliance));
		}

		cdp->csum = compute_ip_checksum(start, p - start);

		/* Invert the checksum, so that the packet is dropped */
		if (ctx->reply == CDP_REPLY_CSUM)
			cdp->csum = ~cdp->csum;
	}

	eth->et_protlen = htons(p - (u8 *)buf - ETHER_HDR_SIZE);

	return p - (u8 *)buf;
}

/**
 * sb_cdp_handler() - act as a CISCO switch answering a CDP trigger
 *
 * The handler records what the trigger looked like, since a test cannot see the
 * packets the command sends, and answers the first one. Only the first, so that
 * a reply which produces a message does not produce it three times.
 *
 * It also moves the sandbox timer on by the time the command waits between
 * triggers, which takes the whole exchange from 750ms to a few milliseconds.
 *
 * @dev: sandbox Ethernet device
 * @packet: packet U-Boot has just sent
 * @len: length of @packet in bytes
 * Return: 0, whether or not a reply was queued
 */
static int sb_cdp_handler(struct udevice *dev, void *packet, unsigned int len)
{
	struct eth_sandbox_priv *priv = dev_get_priv(dev);
	struct cdp_ctx *ctx = priv->priv;
	static const u8 dest[ARP_HLEN] = CDP_DEST;
	struct ethernet_hdr *eth = packet;
	u8 *snap = (u8 *)packet + ETHER_HDR_SIZE;
	struct cdp_hdr *cdp = (struct cdp_hdr *)(snap + SNAP_HDR_SIZE);
	int size = (int)len - ETHER_HDR_SIZE - SNAP_HDR_SIZE;

	ctx->triggers++;
	ctx->len = len;
	ctx->protlen = ntohs(eth->et_protlen);
	ctx->dest_ok = !memcmp(eth->et_dest, dest, ARP_HLEN);
	ctx->snap_ok = !memcmp(snap, cdp_snap, sizeof(cdp_snap));
	ctx->version = cdp->version;
	ctx->ttl = cdp->ttl;
	ctx->csum_ok = !compute_ip_checksum(cdp, size);

	timer_test_add_offset(CDP_TIMEOUT);

	if (ctx->reply == CDP_REPLY_NONE || ctx->triggers > 1)
		return 0;

	/* Don't allow the buffer to overrun */
	if (priv->recv_packets >= PKTBUFSRX)
		return 0;

	priv->recv_packet_length[priv->recv_packets] =
		build_reply(ctx, priv->recv_packet_buffer[priv->recv_packets]);
	++priv->recv_packets;

	return 0;
}

/**
 * cdp_setup() - point the cdp command at the emulated switch
 *
 * @uts: test state
 * @ctx: handler state, which must live until cdp_restore() is called
 * @reply: packet the switch should answer the first trigger with
 * Return: 0 if OK, 1 on failure
 */
static int cdp_setup(struct unit_test_state *uts, struct cdp_ctx *ctx,
		     enum cdp_reply reply)
{
	ctx->uts = uts;
	ctx->reply = reply;
	sandbox_eth_set_tx_handler(0, sb_cdp_handler);
	sandbox_eth_set_priv(0, ctx);
	ut_assertok(env_set("ethact", CDP_ETHACT));

	return 0;
}

/**
 * cdp_restore() - put back the transmit handler and the VLAN settings
 *
 * The setenv command is used rather than env_set(), since on_vlan() and
 * on_nvlan() ignore a programmatic write; the two globals are put back by hand
 * for the same reason.
 *
 * @uts: test state
 * Return: 0 if OK, 1 on failure
 */
static int cdp_restore(struct unit_test_state *uts)
{
	sandbox_eth_set_tx_handler(0, NULL);
	sandbox_eth_set_priv(0, NULL);
	ut_assertok(env_set("ethact", NULL));
	ut_assertok(run_command("setenv vlan", 0));
	ut_assertok(run_command("setenv nvlan", 0));
	net_our_vlan = 0xffff;
	net_native_vlan = 0xffff;

	return 0;
}

/**
 * check_trigger() - check the packet the command sends
 *
 * @uts: test state
 * @ctx: handler state, filled in by the handler
 * Return: 0 if OK, 1 on failure
 */
static int check_trigger(struct unit_test_state *uts, struct cdp_ctx *ctx)
{
	/* An Ethernet header, the SNAP header and a CDP header with no TLVs */
	ut_asserteq(CDP_TRIGGER_LEN, ctx->len);
	ut_asserteq(ctx->len - ETHER_HDR_SIZE, ctx->protlen);
	ut_assert(ctx->dest_ok);
	ut_assert(ctx->snap_ok);
	ut_asserteq(CDP_VERSION, ctx->version);
	ut_asserteq(CDP_TTL, ctx->ttl);
	ut_assert(ctx->csum_ok);

	return 0;
}

/* Obtain the VLAN settings the switch offers */
static int cmd_test_cdp_base(struct unit_test_state *uts)
{
	struct cdp_ctx ctx = { };
	char nvlan[8], vlan[8];

	ut_assertok(cdp_setup(uts, &ctx, CDP_REPLY_OK));
	ut_assertok(run_command("cdp", 0));
	ut_assert_nextline("Using %s device", CDP_ETHACT);
	ut_assert_nextline("CDP offered appliance VLAN %d", CDP_APPLIANCE_VLAN);
	ut_assert_nextline("CDP offered native VLAN %d", CDP_NATIVE_VLAN);
	ut_assert_console_end();

	/*
	 * The command sends all three triggers whatever the switch answers,
	 * since it reports what it has only when the last one times out
	 */
	ut_asserteq(CDP_TRIGGERS, ctx.triggers);
	ut_assertok(check_trigger(uts, &ctx));

	/* Both VLANs reach the environment and the network settings */
	sprintf(vlan, "%d", CDP_APPLIANCE_VLAN);
	sprintf(nvlan, "%d", CDP_NATIVE_VLAN);
	ut_asserteq_str(vlan, env_get("vlan"));
	ut_asserteq_str(nvlan, env_get("nvlan"));
	ut_asserteq(CDP_APPLIANCE_VLAN, ntohs(net_our_vlan));
	ut_asserteq(CDP_NATIVE_VLAN, ntohs(net_native_vlan));

	ut_assertok(cdp_restore(uts));

	return 0;
}
CMD_TEST(cmd_test_cdp_base, UTF_CONSOLE);

/* With nothing answering the command gives up */
static int cmd_test_cdp_quiet(struct unit_test_state *uts)
{
	struct cdp_ctx ctx = { };

	ut_assertok(cdp_setup(uts, &ctx, CDP_REPLY_NONE));
	ut_asserteq(1, run_command("cdp", 0));
	ut_assert_nextline("Using %s device", CDP_ETHACT);
	ut_assert_nextline("cdp failed; perhaps not a CISCO switch?");
	ut_assert_console_end();

	ut_asserteq(CDP_TRIGGERS, ctx.triggers);
	ut_assertok(check_trigger(uts, &ctx));

	/* Nothing is offered, so the environment is left alone */
	ut_assertnull(env_get("vlan"));
	ut_assertnull(env_get("nvlan"));

	ut_assertok(cdp_restore(uts));

	return 0;
}
CMD_TEST(cmd_test_cdp_quiet, UTF_CONSOLE);

/* A packet which is not CDP, or whose checksum is wrong, is ignored */
static int cmd_test_cdp_ignore(struct unit_test_state *uts)
{
	struct cdp_ctx ctx = { };

	ut_assertok(cdp_setup(uts, &ctx, CDP_REPLY_SNAP));
	ut_asserteq(1, run_command("cdp", 0));
	ut_assert_nextline("Using %s device", CDP_ETHACT);
	ut_assert_nextline("cdp failed; perhaps not a CISCO switch?");
	ut_assert_console_end();

	ctx.triggers = 0;
	ctx.reply = CDP_REPLY_CSUM;
	ut_asserteq(1, run_command("cdp", 0));
	ut_assert_nextline("Using %s device", CDP_ETHACT);
	ut_assert_nextline("cdp failed; perhaps not a CISCO switch?");
	ut_assert_console_end();

	ut_asserteq(CDP_TRIGGERS, ctx.triggers);
	ut_assertnull(env_get("vlan"));

	ut_assertok(cdp_restore(uts));

	return 0;
}
CMD_TEST(cmd_test_cdp_ignore, UTF_CONSOLE);

/* A packet with no room for a CDP header is reported */
static int cmd_test_cdp_short(struct unit_test_state *uts)
{
	struct cdp_ctx ctx = { };

	ut_assertok(cdp_setup(uts, &ctx, CDP_REPLY_SHORT));
	ut_asserteq(1, run_command("cdp", 0));
	ut_assert_nextline("Using %s device", CDP_ETHACT);
	ut_assert_nextline("** CDP packet is too short");
	ut_assert_nextline("cdp failed; perhaps not a CISCO switch?");
	ut_assert_console_end();

	/* So is a TLV whose length leaves the parser where it started */
	ctx.triggers = 0;
	ctx.reply = CDP_REPLY_TLV;
	ut_asserteq(1, run_command("cdp", 0));
	ut_assert_nextline("Using %s device", CDP_ETHACT);
	ut_assert_nextline("** CDP packet is too short");
	ut_assert_nextline("cdp failed; perhaps not a CISCO switch?");
	ut_assert_console_end();

	ut_assertok(cdp_restore(uts));

	return 0;
}
CMD_TEST(cmd_test_cdp_short, UTF_CONSOLE);

/* A newer protocol version is warned about and then used anyway */
static int cmd_test_cdp_version(struct unit_test_state *uts)
{
	struct cdp_ctx ctx = { };

	ut_assertok(cdp_setup(uts, &ctx, CDP_REPLY_V3));
	ut_assertok(run_command("cdp", 0));
	ut_assert_nextline("Using %s device", CDP_ETHACT);
	ut_assert_nextline(CDP_WARNING);
	ut_assert_nextline("CDP offered appliance VLAN %d", CDP_APPLIANCE_VLAN);
	ut_assert_nextline("CDP offered native VLAN %d", CDP_NATIVE_VLAN);
	ut_assert_console_end();

	ut_assertok(cdp_restore(uts));

	return 0;
}
CMD_TEST(cmd_test_cdp_version, UTF_CONSOLE);

/* The command takes no arguments */
static int cmd_test_cdp_usage(struct unit_test_state *uts)
{
	ut_asserteq(1, run_command("cdp extra", 0));
	ut_assert_nextline("cdp - Perform CDP network configuration");
	ut_assert_nextline_empty();
	ut_assert_nextline("Usage:");

	/* The trailing space and the blank line come from a help text of "\n" */
	ut_assert_nextline("cdp ");
	ut_assert_nextline_empty();
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_cdp_usage, UTF_CONSOLE);
