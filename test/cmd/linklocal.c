// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the linklocal command
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
#define LL_ETHACT	"eth@10002000"

/*
 * Address the command settles on when it picks one for itself. The choice is
 * seeded from the MAC address of the interface, so it is the same every time.
 */
#define LL_PICKED	"169.254.195.225"

/* Address to ask for through llipaddr */
#define LL_WANTED	"169.254.7.7"

/* Enough for an address in dotted-quad form */
#define LL_ADDR_LEN	16

/**
 * struct ll_env - environment variables which the command overwrites
 *
 * @ipaddr: value of ipaddr before the test
 * @netmask: value of netmask before the test
 * @gatewayip: value of gatewayip before the test
 * @llipaddr: value of llipaddr before the test
 */
struct ll_env {
	char ipaddr[LL_ADDR_LEN];
	char netmask[LL_ADDR_LEN];
	char gatewayip[LL_ADDR_LEN];
	char llipaddr[LL_ADDR_LEN];
};

/**
 * sb_ll_handler() - swallow the probes the command sends
 *
 * Nothing answers, so every address the command tries is free. The clock is
 * moved on as each packet goes out, so that the protocol's timeouts expire at
 * once rather than taking the ten seconds they take on a real network.
 *
 * @dev: sandbox Ethernet device
 * @packet: packet U-Boot has just sent
 * @len: length of @packet in bytes
 * Return: 0 always
 */
static int sb_ll_handler(struct udevice *dev, void *packet, unsigned int len)
{
	sandbox_eth_skip_timeout();

	return 0;
}

/** ll_get() - read an environment variable into a buffer, empty if unset */
static void ll_get(char *buf, int size, const char *name)
{
	const char *val = env_get(name);

	snprintf(buf, size, "%s", val ? val : "");
}

/**
 * ll_setup() - remember the environment and take over the Ethernet device
 *
 * @uts: test state
 * @env: place to store the variables the command overwrites
 * Return: 0 if OK, 1 on failure
 */
static int ll_setup(struct unit_test_state *uts, struct ll_env *env)
{
	ll_get(env->ipaddr, sizeof(env->ipaddr), "ipaddr");
	ll_get(env->netmask, sizeof(env->netmask), "netmask");
	ll_get(env->gatewayip, sizeof(env->gatewayip), "gatewayip");
	ll_get(env->llipaddr, sizeof(env->llipaddr), "llipaddr");

	sandbox_eth_set_tx_handler(0, sb_ll_handler);
	env_set("ethact", LL_ETHACT);
	ut_assertok(run_command("setenv llipaddr", 0));

	return 0;
}

/**
 * ll_restore() - put back the environment and the transmit handler
 *
 * The variables are restored with the setenv command, since the callbacks
 * which keep net_ip and net_netmask in step ignore a programmatic write.
 *
 * @uts: test state
 * @env: variables saved by ll_setup()
 * Return: 0 if OK, 1 on failure
 */
static int ll_restore(struct unit_test_state *uts, struct ll_env *env)
{
	ut_assertok(run_commandf("setenv ipaddr %s", env->ipaddr));
	ut_assertok(run_commandf("setenv netmask %s", env->netmask));
	ut_assertok(run_commandf("setenv gatewayip %s", env->gatewayip));
	ut_assertok(run_commandf("setenv llipaddr %s", env->llipaddr));
	env_set("ethact", NULL);
	sandbox_eth_set_tx_handler(0, NULL);

	return 0;
}

/* Claim an address and check what is left in the environment */
static int cmd_test_linklocal_base(struct unit_test_state *uts)
{
	struct ll_env env;

	ut_assertok(ll_setup(uts, &env));
	ut_assertok(run_command("linklocal", 0));
	ut_assert_nextline("Successfully assigned " LL_PICKED);
	ut_assert_console_end();

	ut_asserteq_str(LL_PICKED, env_get("ipaddr"));
	ut_asserteq_str(LL_PICKED, env_get("llipaddr"));
	ut_asserteq_str("255.255.0.0", env_get("netmask"));
	ut_asserteq_str("0.0.0.0", env_get("gatewayip"));
	ut_asserteq(string_to_ip(LL_PICKED).s_addr, net_ip.s_addr);

	ut_assertok(ll_restore(uts, &env));

	return 0;
}
CMD_TEST(cmd_test_linklocal_base, UTF_CONSOLE);

/* An address already in llipaddr is claimed again, rather than a new one */
static int cmd_test_linklocal_reuse(struct unit_test_state *uts)
{
	struct ll_env env;

	ut_assertok(ll_setup(uts, &env));
	ut_assertok(run_command("setenv llipaddr " LL_WANTED, 0));
	ut_assertok(run_command("linklocal", 0));
	ut_assert_nextline("Successfully assigned " LL_WANTED);
	ut_assert_console_end();

	ut_asserteq_str(LL_WANTED, env_get("ipaddr"));
	ut_asserteq_str(LL_WANTED, env_get("llipaddr"));

	ut_assertok(ll_restore(uts, &env));

	return 0;
}
CMD_TEST(cmd_test_linklocal_reuse, UTF_CONSOLE);

/* An address outside 169.254.0.0/16 is refused */
static int cmd_test_linklocal_notlocal(struct unit_test_state *uts)
{
	struct ll_env env;

	ut_assertok(ll_setup(uts, &env));
	ut_assertok(run_command("setenv llipaddr 192.0.2.7", 0));
	ut_asserteq(1, run_command("linklocal", 0));
	ut_assert_nextline("invalid link address");
	ut_assert_console_end();

	/* The address which was rejected is left alone, as is ipaddr */
	ut_asserteq_str("192.0.2.7", env_get("llipaddr"));
	ut_asserteq_str(env.ipaddr, env_get("ipaddr"));

	ut_assertok(ll_restore(uts, &env));

	return 0;
}
CMD_TEST(cmd_test_linklocal_notlocal, UTF_CONSOLE);
