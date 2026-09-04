// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the dns command
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
#define DNS_ETHACT	"eth@10002000"

/* Address of the emulated name server */
#define DNS_SERVER	"192.0.2.2"

/* Address the emulated name server hands back for any query */
#define DNS_ANSWER	"192.0.2.99"

/* Port a name server listens on */
#define DNS_PORT	53

/* Resource-record type and class used by the query and the answer */
#define DNS_TYPE_A	1
#define DNS_CLASS_IN	1

/* Offset of the question section within a DNS message */
#define DNS_QUESTION_OFFSET	12

/**
 * struct dns_ctx - state shared with the transmit handler
 *
 * @uts: test state, used by the ut_assert macros in the handler
 * @empty: true to answer with a message holding no records
 */
struct dns_ctx {
	struct unit_test_state *uts;
	bool empty;
};

/**
 * sb_dns_handler() - act as a name server for the sandbox Ethernet device
 *
 * This answers the ARP request for the server and then any DNS query sent to
 * it, always with the same address.
 *
 * @dev: sandbox Ethernet device
 * @packet: packet U-Boot has just sent
 * @len: length of @packet in bytes
 * Return: 0 if a reply was queued, -ve on error
 */
static int sb_dns_handler(struct udevice *dev, void *packet, unsigned int len)
{
	struct eth_sandbox_priv *priv = dev_get_priv(dev);
	struct dns_ctx *ctx = priv->priv;
	struct ethernet_hdr *eth = packet, *eth_recv;
	struct ip_udp_hdr *ip = packet + ETHER_HDR_SIZE, *ip_recv;
	struct in_addr answer;
	uchar *dns, *p;
	int qlen, dlen;

	if (ntohs(eth->et_protlen) == PROT_ARP)
		return sandbox_eth_arp_req_to_reply(dev, packet, len);

	if (ntohs(eth->et_protlen) != PROT_IP || ip->ip_p != IPPROTO_UDP ||
	    ntohs(ip->udp_dst) != DNS_PORT)
		return -EPROTONOSUPPORT;

	/* Don't allow the buffer to overrun */
	if (priv->recv_packets >= PKTBUFSRX)
		return 0;

	eth_recv = (void *)priv->recv_packet_buffer[priv->recv_packets];
	memcpy(eth_recv->et_dest, eth->et_src, ARP_HLEN);
	memcpy(eth_recv->et_src, priv->fake_host_hwaddr, ARP_HLEN);
	eth_recv->et_protlen = htons(PROT_IP);

	/* Repeat the question, so that the client can skip over the name */
	ip_recv = (void *)eth_recv + ETHER_HDR_SIZE;
	dns = (uchar *)ip_recv + IP_UDP_HDR_SIZE;
	qlen = ntohs(ip->udp_len) - UDP_HDR_SIZE;
	memcpy(dns, (uchar *)ip + IP_UDP_HDR_SIZE, qlen);

	/* Turn the query into a response */
	dns[2] = 0x81;
	dns[3] = 0x80;
	dns[6] = 0;
	dns[7] = ctx->empty ? 0 : 1;

	p = dns + qlen;
	if (!ctx->empty) {
		/* Point at the name in the question rather than repeating it */
		*p++ = 0xc0;
		*p++ = DNS_QUESTION_OFFSET;

		*p++ = 0;
		*p++ = DNS_TYPE_A;
		*p++ = 0;
		*p++ = DNS_CLASS_IN;

		/* Time to live, one minute */
		*p++ = 0;
		*p++ = 0;
		*p++ = 0;
		*p++ = 60;

		/* An address is four bytes long */
		*p++ = 0;
		*p++ = 4;
		answer = string_to_ip(DNS_ANSWER);
		memcpy(p, &answer.s_addr, sizeof(answer.s_addr));
		p += sizeof(answer.s_addr);
	}
	dlen = p - dns;

	ip_recv->ip_hl_v = 0x45;
	ip_recv->ip_tos = 0;
	ip_recv->ip_len = htons(IP_UDP_HDR_SIZE + dlen);
	ip_recv->ip_id = htons(1);
	ip_recv->ip_off = htons(IP_FLAGS_DFRAG);
	ip_recv->ip_ttl = 64;
	ip_recv->ip_p = IPPROTO_UDP;
	ip_recv->ip_sum = 0;
	ip_recv->ip_src = ip->ip_dst;
	ip_recv->ip_dst = ip->ip_src;
	ip_recv->ip_sum = compute_ip_checksum(ip_recv, IP_HDR_SIZE);
	ip_recv->udp_src = ip->udp_dst;
	ip_recv->udp_dst = ip->udp_src;
	ip_recv->udp_len = htons(UDP_HDR_SIZE + dlen);
	ip_recv->udp_xsum = 0;

	priv->recv_packet_length[priv->recv_packets] =
		ETHER_HDR_SIZE + IP_UDP_HDR_SIZE + dlen;
	++priv->recv_packets;

	return 0;
}

/**
 * dns_setup() - point the dns command at the emulated name server
 *
 * The dnsip variable is set with the setenv command rather than env_set(),
 * since on_dnsip() ignores a programmatic write and so net_dns_server would
 * keep its old value.
 *
 * @uts: test state
 * @ctx: handler state, which must live until dns_restore() is called
 * Return: 0 if OK, 1 on failure
 */
static int dns_setup(struct unit_test_state *uts, struct dns_ctx *ctx)
{
	sandbox_eth_set_tx_handler(0, sb_dns_handler);
	sandbox_eth_set_priv(0, ctx);
	env_set("ethact", DNS_ETHACT);
	ut_assertok(run_command("setenv dnsip " DNS_SERVER, 0));

	return 0;
}

/**
 * dns_restore() - put back the environment and the transmit handler
 *
 * @uts: test state
 * Return: 0 if OK, 1 on failure
 */
static int dns_restore(struct unit_test_state *uts)
{
	sandbox_eth_set_tx_handler(0, NULL);
	env_set("ethact", NULL);
	ut_assertok(run_command("setenv dnsip", 0));

	return 0;
}

/* Look up a name and check the address which comes back */
static int cmd_test_dns_base(struct unit_test_state *uts)
{
	struct dns_ctx ctx = { .uts = uts };

	ut_assertok(dns_setup(uts, &ctx));
	ut_assertok(run_command("dns u-boot.example.com", 0));
	ut_assert_nextline(DNS_ANSWER);
	ut_assert_console_end();
	ut_assertok(dns_restore(uts));

	return 0;
}
CMD_TEST(cmd_test_dns_base, UTF_CONSOLE);

/* Check that the address is stored in the environment variable given */
static int cmd_test_dns_var(struct unit_test_state *uts)
{
	struct dns_ctx ctx = { .uts = uts };

	env_set("hostip", NULL);
	ut_assertok(dns_setup(uts, &ctx));
	ut_assertok(run_command("dns u-boot.example.com hostip", 0));
	ut_assert_nextline(DNS_ANSWER);
	ut_assert_console_end();
	ut_asserteq_str(DNS_ANSWER, env_get("hostip"));
	env_set("hostip", NULL);
	ut_assertok(dns_restore(uts));

	return 0;
}
CMD_TEST(cmd_test_dns_var, UTF_CONSOLE);

/* A name which does not resolve is reported, but is not an error */
static int cmd_test_dns_unknown(struct unit_test_state *uts)
{
	struct dns_ctx ctx = { .uts = uts, .empty = true };

	env_set("hostip", NULL);
	ut_assertok(dns_setup(uts, &ctx));
	ut_assertok(run_command("dns u-boot.example.com hostip", 0));
	ut_assert_nextline("DNS: host not found");
	ut_assert_console_end();
	ut_assertnull(env_get("hostip"));
	ut_assertok(dns_restore(uts));

	return 0;
}
CMD_TEST(cmd_test_dns_unknown, UTF_CONSOLE);

/* The command needs a name server to ask */
static int cmd_test_dns_noserver(struct unit_test_state *uts)
{
	struct dns_ctx ctx = { .uts = uts };

	ut_assertok(dns_setup(uts, &ctx));
	ut_assertok(run_command("setenv dnsip", 0));
	ut_asserteq(1, run_command("dns u-boot.example.com", 0));
	ut_assert_nextline("*** ERROR: DNS server address not given");
	ut_assert_nextline("dns lookup of u-boot.example.com failed, check setup");
	ut_assert_console_end();
	ut_assertok(dns_restore(uts));

	return 0;
}
CMD_TEST(cmd_test_dns_noserver, UTF_CONSOLE);

/* A name is required and must be short enough to fit in a query */
static int cmd_test_dns_badname(struct unit_test_state *uts)
{
	char name[300];

	memset(name, 'a', sizeof(name) - 1);
	name[sizeof(name) - 1] = '\0';

	ut_asserteq(1, run_command("dns", 0));
	ut_assert_nextline("dns - lookup the IP of a hostname");
	ut_assert_skip_to_line("Usage:");
	ut_assert_nextline("dns hostname [envvar]");
	ut_assert_console_end();

	ut_asserteq(1, run_commandf("dns %s", name));
	ut_assert_nextline("dns error: hostname too long");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_dns_badname, UTF_CONSOLE);
