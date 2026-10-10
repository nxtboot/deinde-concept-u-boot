// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the bootp command
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <command.h>
#include <dm.h>
#include <env.h>
#include <mapmem.h>
#include <net.h>
#include <asm/eth.h>
#include <test/cmd.h>
#include <test/test.h>
#include <test/ut.h>
#include "../../net/bootp.h"

/* Ethernet device used by the tests */
#define BOOTP_ETHACT	"eth@10002000"

/* Address the emulated server hands out and the one it answers from */
#define BOOTP_CLIENT_IP	"192.0.2.10"
#define BOOTP_SERVER_IP	"192.0.2.2"

/* Address sandbox starts with, which the tests put back afterwards */
#define BOOTP_OWN_IP	"192.0.2.1"

/* Network parameters the emulated server offers */
#define BOOTP_NETMASK	"255.255.255.0"
#define BOOTP_GATEWAY	"192.0.2.3"
#define BOOTP_DNS	"192.0.2.4"
#define BOOTP_HOSTNAME	"sandbox"

/* Boot file the emulated server offers */
#define BOOTP_FILE	"boot.img"

/* Name the command makes up when it has none, from BOOTP_CLIENT_IP */
#define BOOTP_DEFAULT_FILE	"C000020A.img"

/* Address the tests load the file to */
#define BOOTP_LOAD_ADDR	0x1000000

/* Contents of that file, and its length */
#define BOOTP_DATA	"U-Boot bootp test file\n"
#define BOOTP_SIZE	((int)sizeof(BOOTP_DATA) - 1)

/* Well-known TFTP port */
#define TFTP_PORT	69

/* Port the emulated server transfers from, chosen at random */
#define TFTP_TID	21313

/* TFTP operations */
#define TFTP_RRQ	1
#define TFTP_DATA	3
#define TFTP_ACK	4

/* Vendor-extension magic cookie, from RFC 1048 */
#define BOOTP_MAGIC	0x63825363

/* Vendor extensions the emulated server includes */
#define OPT_NETMASK	1
#define OPT_GATEWAY	3
#define OPT_DNS		6
#define OPT_HOSTNAME	12
#define OPT_END		255

struct tftp_hdr {
	u16 opcode;
	u16 block;
};

#define TFTP_HDR_SIZE	sizeof(struct tftp_hdr)

/**
 * struct bootp_ctx - state shared with the transmit handler
 *
 * @uts: test state, used by the ut_assert macros in the handler
 * @fname: name the client asked the TFTP server for
 * @replies: number of BOOTP replies sent
 */
struct bootp_ctx {
	struct unit_test_state *uts;
	char fname[128];
	int replies;
};

/**
 * add_ip_opt() - add a vendor extension holding an address
 *
 * @p: position in the vendor area, updated on return
 * @tag: option number
 * @ip: address to store
 */
static void add_ip_opt(uchar **p, int tag, const char *ip)
{
	struct in_addr addr = string_to_ip(ip);
	uchar *ptr = *p;

	*ptr++ = tag;
	*ptr++ = sizeof(addr.s_addr);
	memcpy(ptr, &addr.s_addr, sizeof(addr.s_addr));
	*p = ptr + sizeof(addr.s_addr);
}

/**
 * add_str_opt() - add a vendor extension holding a string
 *
 * @p: position in the vendor area, updated on return
 * @tag: option number
 * @str: string to store, without its terminator
 */
static void add_str_opt(uchar **p, int tag, const char *str)
{
	uchar *ptr = *p;
	int len = strlen(str);

	*ptr++ = tag;
	*ptr++ = len;
	memcpy(ptr, str, len);
	*p = ptr + len;
}

/**
 * bootp_req_to_reply() - answer a BOOTP request
 *
 * The reply repeats the request, so that the transaction ID and the hardware
 * address match, then fills in the address offered to the client, the server
 * and the boot file, along with the vendor extensions a BOOTP server provides.
 *
 * @dev: sandbox Ethernet device
 * @packet: packet U-Boot has just sent
 * @len: length of @packet in bytes
 * Return: 0 if a reply was queued, -EAGAIN if the packet is not a request
 */
static int bootp_req_to_reply(struct udevice *dev, void *packet,
			      unsigned int len)
{
	struct eth_sandbox_priv *priv = dev_get_priv(dev);
	struct bootp_ctx *ctx = priv->priv;
	struct ethernet_hdr *eth = packet, *eth_recv;
	struct ip_udp_hdr *ip = packet + ETHER_HDR_SIZE, *ip_recv;
	struct bootp_hdr *bp, *bp_recv;
	uchar *p;

	if (ntohs(eth->et_protlen) != PROT_IP || ip->ip_p != IPPROTO_UDP ||
	    ntohs(ip->udp_dst) != PORT_BOOTPS)
		return -EAGAIN;

	bp = (void *)ip + IP_UDP_HDR_SIZE;
	if (bp->bp_op != OP_BOOTREQUEST)
		return -EAGAIN;

	/* Don't allow the buffer to overrun */
	if (priv->recv_packets >= PKTBUFSRX)
		return 0;

	eth_recv = (void *)priv->recv_packet_buffer[priv->recv_packets];
	memcpy(eth_recv, packet, len);
	memcpy(eth_recv->et_dest, eth->et_src, ARP_HLEN);
	memcpy(eth_recv->et_src, priv->fake_host_hwaddr, ARP_HLEN);

	ip_recv = (void *)eth_recv + ETHER_HDR_SIZE;
	ip_recv->ip_src = string_to_ip(BOOTP_SERVER_IP);
	ip_recv->ip_sum = 0;
	ip_recv->ip_sum = compute_ip_checksum(ip_recv, IP_HDR_SIZE);
	ip_recv->udp_src = ip->udp_dst;
	ip_recv->udp_dst = ip->udp_src;
	ip_recv->udp_xsum = 0;

	bp_recv = (void *)ip_recv + IP_UDP_HDR_SIZE;
	bp_recv->bp_op = OP_BOOTREPLY;
	bp_recv->bp_yiaddr = string_to_ip(BOOTP_CLIENT_IP);
	bp_recv->bp_siaddr = string_to_ip(BOOTP_SERVER_IP);
	copy_filename(bp_recv->bp_file, BOOTP_FILE, sizeof(bp_recv->bp_file));

	memset(bp_recv->bp_vend, 0, sizeof(bp_recv->bp_vend));
	p = (uchar *)bp_recv->bp_vend;
	*(u32 *)p = htonl(BOOTP_MAGIC);
	p += sizeof(u32);
	add_ip_opt(&p, OPT_NETMASK, BOOTP_NETMASK);
	add_ip_opt(&p, OPT_GATEWAY, BOOTP_GATEWAY);
	add_ip_opt(&p, OPT_DNS, BOOTP_DNS);
	add_str_opt(&p, OPT_HOSTNAME, BOOTP_HOSTNAME);
	*p++ = OPT_END;

	priv->recv_packet_length[priv->recv_packets] = len;
	++priv->recv_packets;
	++ctx->replies;

	return 0;
}

/**
 * tftp_req_to_reply() - serve the boot file to the client
 *
 * The options in the request are ignored, so the block size is the default of
 * 512 bytes and the whole file goes out in the first block. The ACK which comes
 * back ends the transfer and needs no answer.
 *
 * @dev: sandbox Ethernet device
 * @packet: packet U-Boot has just sent
 * @len: length of @packet in bytes
 * Return: 0 if a reply was queued, -EAGAIN if the packet is not a request
 */
static int tftp_req_to_reply(struct udevice *dev, void *packet,
			     unsigned int len)
{
	struct eth_sandbox_priv *priv = dev_get_priv(dev);
	struct bootp_ctx *ctx = priv->priv;
	struct ethernet_hdr *eth = packet, *eth_recv;
	struct ip_udp_hdr *ip = packet + ETHER_HDR_SIZE, *ip_recv;
	struct tftp_hdr *tftp, *tftp_recv;
	int size;

	if (ntohs(eth->et_protlen) != PROT_IP || ip->ip_p != IPPROTO_UDP)
		return -EAGAIN;

	tftp = (void *)ip + IP_UDP_HDR_SIZE;
	if (ntohs(ip->udp_dst) == TFTP_PORT) {
		if (ntohs(tftp->opcode) != TFTP_RRQ)
			return -EAGAIN;
		strlcpy(ctx->fname, (char *)tftp + sizeof(u16),
			sizeof(ctx->fname));
	} else if (ntohs(ip->udp_dst) == TFTP_TID) {
		if (ntohs(tftp->opcode) != TFTP_ACK)
			return -EAGAIN;

		/* The transfer is over, since the file fits in one block */
		return 0;
	} else {
		return -EAGAIN;
	}

	/* Don't allow the buffer to overrun */
	if (priv->recv_packets >= PKTBUFSRX)
		return 0;

	size = BOOTP_SIZE;
	eth_recv = (void *)priv->recv_packet_buffer[priv->recv_packets];
	memcpy(eth_recv->et_dest, eth->et_src, ARP_HLEN);
	memcpy(eth_recv->et_src, priv->fake_host_hwaddr, ARP_HLEN);
	eth_recv->et_protlen = htons(PROT_IP);

	ip_recv = (void *)eth_recv + ETHER_HDR_SIZE;
	ip_recv->ip_hl_v = 0x45;
	ip_recv->ip_tos = 0;
	ip_recv->ip_len = htons(IP_UDP_HDR_SIZE + TFTP_HDR_SIZE + size);
	ip_recv->ip_id = htons(1);
	ip_recv->ip_off = htons(IP_FLAGS_DFRAG);
	ip_recv->ip_ttl = 64;
	ip_recv->ip_p = IPPROTO_UDP;
	ip_recv->ip_sum = 0;
	ip_recv->ip_src = ip->ip_dst;
	ip_recv->ip_dst = ip->ip_src;
	ip_recv->ip_sum = compute_ip_checksum(ip_recv, IP_HDR_SIZE);
	ip_recv->udp_src = htons(TFTP_TID);
	ip_recv->udp_dst = ip->udp_src;
	ip_recv->udp_len = htons(UDP_HDR_SIZE + TFTP_HDR_SIZE + size);
	ip_recv->udp_xsum = 0;

	tftp_recv = (void *)ip_recv + IP_UDP_HDR_SIZE;
	tftp_recv->opcode = htons(TFTP_DATA);
	tftp_recv->block = htons(1);
	memcpy((void *)tftp_recv + TFTP_HDR_SIZE, BOOTP_DATA, size);

	priv->recv_packet_length[priv->recv_packets] =
		ETHER_HDR_SIZE + IP_UDP_HDR_SIZE + TFTP_HDR_SIZE + size;
	++priv->recv_packets;

	return 0;
}

/**
 * sb_bootp_handler() - act as a BOOTP and TFTP server
 *
 * @dev: sandbox Ethernet device
 * @packet: packet U-Boot has just sent
 * @len: length of @packet in bytes
 * Return: 0, whether or not a reply was queued
 */
static int sb_bootp_handler(struct udevice *dev, void *packet,
			    unsigned int len)
{
	if (!sandbox_eth_arp_req_to_reply(dev, packet, len))
		return 0;
	if (!bootp_req_to_reply(dev, packet, len))
		return 0;
	if (!tftp_req_to_reply(dev, packet, len))
		return 0;

	return 0;
}

/**
 * bootp_setup() - point the bootp command at the emulated server
 *
 * @uts: test state
 * @ctx: handler state, which must live until bootp_restore() is called
 * Return: 0 if OK, 1 on failure
 */
static int bootp_setup(struct unit_test_state *uts, struct bootp_ctx *ctx)
{
	sandbox_eth_set_tx_handler(0, sb_bootp_handler);
	sandbox_eth_set_priv(0, ctx);
	ut_assertok(env_set("ethact", BOOTP_ETHACT));

	/*
	 * With CONFIG_BOOTP_SERVERIP the server address comes from the
	 * environment rather than from the reply. The setenv command is used
	 * rather than env_set(), since on_serverip() ignores a programmatic
	 * write and so net_server_ip would keep its old value.
	 */
	ut_assertok(run_command("setenv serverip " BOOTP_SERVER_IP, 0));

	return 0;
}

/**
 * bootp_restore() - put back the environment and the transmit handler
 *
 * @uts: test state
 * Return: 0 if OK, 1 on failure
 */
static int bootp_restore(struct unit_test_state *uts)
{
	sandbox_eth_set_tx_handler(0, NULL);
	sandbox_eth_set_priv(0, NULL);
	ut_assertok(env_set("ethact", NULL));
	ut_assertok(run_command("setenv serverip", 0));
	ut_assertok(run_command("setenv ipaddr " BOOTP_OWN_IP, 0));
	ut_assertok(env_set("netmask", NULL));
	ut_assertok(env_set("gatewayip", NULL));
	ut_assertok(env_set("dnsip", NULL));
	ut_assertok(env_set("hostname", NULL));
	ut_assertok(env_set("bootfile", NULL));
	ut_assertok(env_set("autoload", NULL));
	ut_assertok(env_set("filesize", NULL));

	return 0;
}

/**
 * check_bound() - check the line which reports the address obtained
 *
 * The message comes from the DHCP state machine, which handles BOOTP replies
 * too when CONFIG_CMD_DHCP is enabled, and includes the time taken.
 *
 * @uts: test state
 * Return: 0 if OK, 1 on failure
 */
static int check_bound(struct unit_test_state *uts)
{
	if (IS_ENABLED(CONFIG_CMD_DHCP))
		ut_assert_nextlinen("DHCP client bound to address "
				    BOOTP_CLIENT_IP " (");

	return 0;
}

/* Obtain the network parameters and load the boot file the server offers */
static int cmd_test_bootp_base(struct unit_test_state *uts)
{
	struct bootp_ctx ctx = { .uts = uts };
	const char *fname = BOOTP_FILE;

	ut_assertok(bootp_setup(uts, &ctx));
	ut_assertok(run_commandf("bootp %x", BOOTP_LOAD_ADDR));
	ut_assert_nextline("BOOTP broadcast 1");
	ut_assertok(check_bound(uts));
	/*
	 * With CONFIG_BOOTP_SERVERIP the whole of the reply except the address
	 * offered is dropped, boot file name included, so the command falls
	 * back to a name made up from that address
	 */
	if (IS_ENABLED(CONFIG_BOOTP_SERVERIP)) {
		fname = BOOTP_DEFAULT_FILE;
		ut_assert_nextline("*** Warning: no boot file name; using '%s'",
				   fname);
	}
	ut_assert_nextline("Using %s device", BOOTP_ETHACT);
	ut_assert_nextline("TFTP from server " BOOTP_SERVER_IP
			   "; our IP address is " BOOTP_CLIENT_IP);
	ut_assert_nextline("Filename '%s'.", fname);
	ut_assert_nextline("Load address: 0x%x", BOOTP_LOAD_ADDR);
	/*
	 * The transfer rate is reported only when the transfer takes a
	 * millisecond or more, which a file this small does not always manage
	 */
	ut_assert_nextlinen("Loading: ");
	ut_assert_skip_to_line("done");
	ut_assert_nextline("Bytes transferred = %d (%x hex)", BOOTP_SIZE,
			   BOOTP_SIZE);
	ut_assert_console_end();

	ut_asserteq(1, ctx.replies);
	ut_asserteq_str(fname, ctx.fname);
	ut_asserteq(BOOTP_SIZE, env_get_hex("filesize", 0));
	ut_asserteq_mem(BOOTP_DATA, map_sysmem(BOOTP_LOAD_ADDR, 0),
			BOOTP_SIZE);

	/* The reply and its vendor extensions reach the environment */
	ut_asserteq_str(BOOTP_CLIENT_IP, env_get("ipaddr"));
	ut_asserteq_str(BOOTP_NETMASK, env_get("netmask"));
	ut_asserteq_str(BOOTP_GATEWAY, env_get("gatewayip"));
	ut_asserteq_str(BOOTP_DNS, env_get("dnsip"));
	ut_asserteq_str(BOOTP_HOSTNAME, env_get("hostname"));
	if (!IS_ENABLED(CONFIG_BOOTP_SERVERIP))
		ut_asserteq_str(BOOTP_FILE, env_get("bootfile"));

	ut_assertok(bootp_restore(uts));

	return 0;
}
CMD_TEST(cmd_test_bootp_base, UTF_CONSOLE);

/* With autoload off the command configures the network and stops there */
static int cmd_test_bootp_noload(struct unit_test_state *uts)
{
	struct bootp_ctx ctx = { .uts = uts };

	ut_assertok(bootp_setup(uts, &ctx));
	ut_assertok(env_set("autoload", "no"));
	ut_assertok(run_command("bootp", 0));
	ut_assert_nextline("BOOTP broadcast 1");
	ut_assertok(check_bound(uts));
	ut_assert_console_end();

	ut_asserteq_str(BOOTP_CLIENT_IP, env_get("ipaddr"));
	ut_asserteq(1, ctx.replies);

	/* Nothing was asked of the TFTP server */
	ut_asserteq_str("", ctx.fname);
	ut_assertok(bootp_restore(uts));

	return 0;
}
CMD_TEST(cmd_test_bootp_noload, UTF_CONSOLE);

/* A file named on the command line is used in place of the offered one */
static int cmd_test_bootp_file(struct unit_test_state *uts)
{
	struct bootp_ctx ctx = { .uts = uts };

	ut_assertok(bootp_setup(uts, &ctx));
	ut_assertok(run_commandf("bootp %x other.img", BOOTP_LOAD_ADDR));
	ut_assert_nextline("BOOTP broadcast 1");
	ut_assertok(check_bound(uts));
	ut_assert_skip_to_line("Filename 'other.img'.");
	ut_assert_nextline("Load address: 0x%x", BOOTP_LOAD_ADDR);
	ut_assert_nextlinen("Loading: ");
	ut_assert_skip_to_line("done");
	ut_assert_nextline("Bytes transferred = %d (%x hex)", BOOTP_SIZE,
			   BOOTP_SIZE);
	ut_assert_console_end();

	ut_asserteq_str("other.img", ctx.fname);
	ut_assertok(bootp_restore(uts));

	return 0;
}
CMD_TEST(cmd_test_bootp_file, UTF_CONSOLE);

/* The command takes at most two arguments */
static int cmd_test_bootp_usage(struct unit_test_state *uts)
{
	ut_asserteq(1, run_command("bootp 1000 boot.img extra", 0));
	ut_assert_nextline("bootp - boot image via network using BOOTP/TFTP protocol");
	ut_assert_nextline_empty();
	ut_assert_nextline("Usage:");
	ut_assert_nextline("bootp [loadAddress] [[hostIPaddr:]bootfilename]");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_bootp_usage, UTF_CONSOLE);
