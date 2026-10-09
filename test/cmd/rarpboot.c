// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the rarpboot command
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <command.h>
#include <dm.h>
#include <env.h>
#include <net.h>
#include <asm/eth.h>
#include <test/cmd.h>
#include <test/test.h>
#include <test/ut.h>

/* Ethernet device used by the tests */
#define RARP_ETHACT	"eth@10002000"

/* Address the emulated server hands out and the one it answers from */
#define RARP_CLIENT_IP	"192.0.2.10"
#define RARP_SERVER_IP	"192.0.2.2"

/* Address sandbox starts with, which the tests put back afterwards */
#define RARP_OWN_IP	"192.0.2.1"

/**
 * struct rarp_ctx - state shared with the transmit handler
 *
 * @uts: test state, used by the ut_assert macros in the handler
 * @bad_first: true to send a reply with an invalid header before the good one
 * @replies: number of replies sent, the invalid one included
 */
struct rarp_ctx {
	struct unit_test_state *uts;
	bool bad_first;
	int replies;
};

/**
 * queue_reply() - queue a RARP reply for the client
 *
 * @dev: sandbox Ethernet device
 * @packet: request U-Boot has just sent
 * @op: operation to claim, RARPOP_REPLY for a reply the client accepts
 * Return: 0 if the reply was queued, -EOVERFLOW if there is no room
 */
static int queue_reply(struct udevice *dev, void *packet, int op)
{
	struct eth_sandbox_priv *priv = dev_get_priv(dev);
	struct ethernet_hdr *eth = packet, *eth_recv;
	struct arp_hdr *arp = packet + ETHER_HDR_SIZE, *arp_recv;
	struct in_addr addr;

	/* Don't allow the buffer to overrun */
	if (priv->recv_packets >= PKTBUFSRX)
		return -EOVERFLOW;

	eth_recv = (void *)priv->recv_packet_buffer[priv->recv_packets];
	memcpy(eth_recv->et_dest, eth->et_src, ARP_HLEN);
	memcpy(eth_recv->et_src, priv->fake_host_hwaddr, ARP_HLEN);
	eth_recv->et_protlen = htons(PROT_RARP);

	arp_recv = (void *)eth_recv + ETHER_HDR_SIZE;
	arp_recv->ar_hrd = htons(ARP_ETHER);
	arp_recv->ar_pro = htons(PROT_IP);
	arp_recv->ar_hln = ARP_HLEN;
	arp_recv->ar_pln = ARP_PLEN;
	arp_recv->ar_op = htons(op);

	/* The server describes itself, then the client it is answering */
	memcpy(&arp_recv->ar_sha, priv->fake_host_hwaddr, ARP_HLEN);
	addr = string_to_ip(RARP_SERVER_IP);
	memcpy(&arp_recv->ar_spa, &addr.s_addr, ARP_PLEN);
	memcpy(&arp_recv->ar_tha, &arp->ar_sha, ARP_HLEN);
	addr = string_to_ip(RARP_CLIENT_IP);
	memcpy(&arp_recv->ar_tpa, &addr.s_addr, ARP_PLEN);

	priv->recv_packet_length[priv->recv_packets] =
		ETHER_HDR_SIZE + ARP_HDR_SIZE;
	++priv->recv_packets;

	return 0;
}

/**
 * sb_rarp_handler() - act as a RARP server for the sandbox Ethernet device
 *
 * An invalid reply is queued ahead of the good one when the test asks for it,
 * so that both arrive from the same request and the error path costs no retry
 * interval.
 *
 * @dev: sandbox Ethernet device
 * @packet: packet U-Boot has just sent
 * @len: length of @packet in bytes
 * Return: 0, whether or not a reply was queued
 */
static int sb_rarp_handler(struct udevice *dev, void *packet, unsigned int len)
{
	struct eth_sandbox_priv *priv = dev_get_priv(dev);
	struct rarp_ctx *ctx = priv->priv;
	struct ethernet_hdr *eth = packet;
	struct arp_hdr *arp = packet + ETHER_HDR_SIZE;

	if (ntohs(eth->et_protlen) != PROT_RARP ||
	    ntohs(arp->ar_op) != RARPOP_REQUEST)
		return 0;

	if (ctx->bad_first && !queue_reply(dev, packet, RARPOP_REQUEST))
		++ctx->replies;
	if (!queue_reply(dev, packet, RARPOP_REPLY))
		++ctx->replies;

	return 0;
}

/**
 * rarp_setup() - point the rarpboot command at the emulated server
 *
 * Autoload is turned off, since a RARP reply carries no boot file name and
 * the download which would otherwise follow has no server to answer it.
 *
 * @uts: test state
 * @ctx: handler state, which must live until rarp_restore() is called
 * Return: 0 if OK, 1 on failure
 */
static int rarp_setup(struct unit_test_state *uts, struct rarp_ctx *ctx)
{
	sandbox_eth_set_tx_handler(0, sb_rarp_handler);
	sandbox_eth_set_priv(0, ctx);
	ut_assertok(env_set("ethact", RARP_ETHACT));
	ut_assertok(env_set("autoload", "no"));

	return 0;
}

/**
 * rarp_restore() - put back the environment and the transmit handler
 *
 * The ipaddr variable is set with the setenv command rather than env_set(),
 * since on_ipaddr() ignores a programmatic write and so net_ip would keep the
 * address the server handed out.
 *
 * @uts: test state
 * Return: 0 if OK, 1 on failure
 */
static int rarp_restore(struct unit_test_state *uts)
{
	sandbox_eth_set_tx_handler(0, NULL);
	sandbox_eth_set_priv(0, NULL);
	ut_assertok(env_set("ethact", NULL));
	ut_assertok(env_set("autoload", NULL));
	ut_assertok(run_command("setenv ipaddr " RARP_OWN_IP, 0));

	return 0;
}

/* Obtain the address of the board from the emulated RARP server */
static int cmd_test_rarpboot_base(struct unit_test_state *uts)
{
	struct rarp_ctx ctx = { .uts = uts };

	ut_assertok(rarp_setup(uts, &ctx));
	ut_assertok(run_command("rarpboot", 0));
	ut_assert_nextline("RARP broadcast 1");
	ut_assert_console_end();

	ut_asserteq(1, ctx.replies);
	ut_asserteq_str(RARP_CLIENT_IP, env_get("ipaddr"));
	ut_assertok(rarp_restore(uts));

	return 0;
}
CMD_TEST(cmd_test_rarpboot_base, UTF_CONSOLE);

/* A reply which is not one is reported and the next reply is used */
static int cmd_test_rarpboot_bad(struct unit_test_state *uts)
{
	struct rarp_ctx ctx = { .uts = uts, .bad_first = true };

	ut_assertok(rarp_setup(uts, &ctx));
	ut_assertok(run_command("rarpboot", 0));
	ut_assert_nextline("RARP broadcast 1");
	ut_assert_nextline("invalid RARP header");
	ut_assert_console_end();

	ut_asserteq(2, ctx.replies);
	ut_asserteq_str(RARP_CLIENT_IP, env_get("ipaddr"));
	ut_assertok(rarp_restore(uts));

	return 0;
}
CMD_TEST(cmd_test_rarpboot_bad, UTF_CONSOLE);

/* The command takes at most two arguments */
static int cmd_test_rarpboot_usage(struct unit_test_state *uts)
{
	ut_asserteq(1, run_command("rarpboot 1000 boot.img extra", 0));
	ut_assert_nextline("rarpboot - boot image via network using RARP/TFTP protocol");
	ut_assert_nextline_empty();
	ut_assert_nextline("Usage:");
	ut_assert_nextline("rarpboot [loadAddress] [[hostIPaddr:]bootfilename]");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_rarpboot_usage, UTF_CONSOLE);
