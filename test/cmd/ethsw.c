// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the ethsw command
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <command.h>
#include <console.h>
#include <errno.h>
#include <ethsw.h>
#include <test/cmd.h>
#include <test/ut.h>

/* name the stub switch gives itself, which the command prints */
#define ETHSW_TEST_NAME		"test-switch"

/* start of the last line of the help text, which ends the usage message */
#define ETHSW_USAGE_LAST	"ethsw [port <port_no>] aggr"

/* the command line as the parser understood it */
static struct ethsw_command_def parsed;

/* name of the member of struct ethsw_command_func which the parser chose */
static const char *called;

/**
 * ETHSW_STUB() - Define a handler which records what the parser produced
 *
 * A switch driver provides a function for each sub-command. These stubs stand
 * in for them, so a test can see which one the parser picks and what it hands
 * over, which is the whole of what the command itself does.
 *
 * @_name: Member of struct ethsw_command_func to stand in for
 */
#define ETHSW_STUB(_name)						\
	static int stub_ ## _name(struct ethsw_command_def *parsed_cmd)	\
	{								\
		parsed = *parsed_cmd;					\
		called = #_name;					\
									\
		return CMD_RET_SUCCESS;					\
	}

ETHSW_STUB(port_enable)
ETHSW_STUB(port_disable)
ETHSW_STUB(port_show)
ETHSW_STUB(port_stats)
ETHSW_STUB(port_learn)
ETHSW_STUB(port_learn_show)
ETHSW_STUB(fdb_show)
ETHSW_STUB(fdb_flush)
ETHSW_STUB(fdb_entry_add)
ETHSW_STUB(fdb_entry_del)
ETHSW_STUB(pvid_show)
ETHSW_STUB(pvid_set)
ETHSW_STUB(vlan_show)
ETHSW_STUB(vlan_set)
ETHSW_STUB(port_untag_show)
ETHSW_STUB(port_untag_set)
ETHSW_STUB(port_egr_vlan_show)
ETHSW_STUB(port_egr_vlan_set)
ETHSW_STUB(vlan_learn_show)
ETHSW_STUB(vlan_learn_set)
ETHSW_STUB(port_ingr_filt_show)
ETHSW_STUB(port_ingr_filt_set)
ETHSW_STUB(port_aggr_show)
ETHSW_STUB(port_aggr_set)

/*
 * port_stats_clear is left out on purpose, since a driver need not provide
 * every function; cmd_test_ethsw_misc() covers what the command does then
 */
static const struct ethsw_command_func stub_func = {
	.ethsw_name		= ETHSW_TEST_NAME,
	.port_enable		= stub_port_enable,
	.port_disable		= stub_port_disable,
	.port_show		= stub_port_show,
	.port_stats		= stub_port_stats,
	.port_learn		= stub_port_learn,
	.port_learn_show	= stub_port_learn_show,
	.fdb_show		= stub_fdb_show,
	.fdb_flush		= stub_fdb_flush,
	.fdb_entry_add		= stub_fdb_entry_add,
	.fdb_entry_del		= stub_fdb_entry_del,
	.pvid_show		= stub_pvid_show,
	.pvid_set		= stub_pvid_set,
	.vlan_show		= stub_vlan_show,
	.vlan_set		= stub_vlan_set,
	.port_untag_show	= stub_port_untag_show,
	.port_untag_set		= stub_port_untag_set,
	.port_egr_vlan_show	= stub_port_egr_vlan_show,
	.port_egr_vlan_set	= stub_port_egr_vlan_set,
	.vlan_learn_show	= stub_vlan_learn_show,
	.vlan_learn_set		= stub_vlan_learn_set,
	.port_ingr_filt_show	= stub_port_ingr_filt_show,
	.port_ingr_filt_set	= stub_port_ingr_filt_set,
	.port_aggr_show		= stub_port_aggr_show,
	.port_aggr_set		= stub_port_aggr_set,
};

/**
 * setup_switch() - Attach the stub handlers to the command
 *
 * The registration fills in a table held by the command and skips an entry
 * which is already set, so it takes effect only the first time and every test
 * shares the same stubs. A board whose driver has registered first therefore
 * never sees them, which a probe command detects.
 *
 * Return: 0 if the stubs are in place, -EAGAIN to skip the test if a switch
 * driver has claimed the command, or -ve error
 */
static int setup_switch(void)
{
	int ret;

	ret = ethsw_define_functions(&stub_func);
	if (ret)
		return ret;

	called = NULL;
	run_command("ethsw port 1 show", 0);
	console_record_reset();

	return called ? 0 : -EAGAIN;
}

/**
 * run_ethsw() - Run a command line which must reach one of the stubs
 *
 * @uts: Test state
 * @cmd: Command line to run
 * Return: 0 if OK, 1 on failure
 */
static int run_ethsw(struct unit_test_state *uts, const char *cmd)
{
	called = NULL;
	ut_assertok(run_command(cmd, 0));
	ut_assert_console_end();
	ut_assertnonnull(called);

	return 0;
}

/**
 * check_usage() - Check the usage message, which a bad command line produces
 *
 * @uts: Test state
 * Return: 0 if OK, 1 on failure
 */
static int check_usage(struct unit_test_state *uts)
{
	ut_assert_nextline("ethsw - Ethernet l2 switch commands");
	ut_assert_nextline_empty();
	ut_assert_nextline("Usage:");
	ut_assert_skip_to_linen(ETHSW_USAGE_LAST);
	ut_assert_nextline_empty();
	ut_assert_console_end();

	return 0;
}

/* Test the sub-commands which act on a port as a whole */
static int cmd_test_ethsw_base(struct unit_test_state *uts)
{
	int ret;

	ret = setup_switch();
	if (ret)
		return ret;

	/* without the port keyword the sub-command covers every port */
	ut_assertok(run_ethsw(uts, "ethsw enable"));
	ut_asserteq_str("port_enable", called);
	ut_asserteq(ETHSW_CMD_PORT_ALL, parsed.port);
	ut_asserteq(ETHSW_CMD_VLAN_ALL, parsed.vid);
	ut_asserteq(ETHSW_CMD_AGGR_GRP_NONE, parsed.aggr_grp);

	/* with it, the port number reaches the handler */
	ut_assertok(run_ethsw(uts, "ethsw port 3 enable"));
	ut_asserteq_str("port_enable", called);
	ut_asserteq(3, parsed.port);

	ut_assertok(run_ethsw(uts, "ethsw port 0 disable"));
	ut_asserteq_str("port_disable", called);
	ut_asserteq(0, parsed.port);

	ut_assertok(run_ethsw(uts, "ethsw show"));
	ut_asserteq_str("port_show", called);
	ut_asserteq(ETHSW_CMD_PORT_ALL, parsed.port);

	ut_assertok(run_ethsw(uts, "ethsw port 2 show"));
	ut_asserteq_str("port_show", called);
	ut_asserteq(2, parsed.port);

	return 0;
}
CMD_TEST(cmd_test_ethsw_base, UTF_CONSOLE);

/* Test the filtering-database sub-commands and the MAC address they take */
static int cmd_test_ethsw_fdb(struct unit_test_state *uts)
{
	const u8 mac[] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 };
	int ret;

	ret = setup_switch();
	if (ret)
		return ret;

	ut_assertok(run_ethsw(uts, "ethsw fdb show"));
	ut_asserteq_str("fdb_show", called);

	ut_assertok(run_ethsw(uts, "ethsw fdb flush"));
	ut_asserteq_str("fdb_flush", called);

	/* an address is read into the parsed command */
	ut_assertok(run_ethsw(uts, "ethsw fdb add 00:11:22:33:44:55"));
	ut_asserteq_str("fdb_entry_add", called);
	ut_asserteq_mem(mac, parsed.ethaddr, sizeof(mac));

	/* the optional keywords narrow the entry to a port and a VLAN */
	ut_assertok(run_ethsw(uts,
			      "ethsw port 2 vlan 7 fdb del 00:11:22:33:44:55"));
	ut_asserteq_str("fdb_entry_del", called);
	ut_asserteq(2, parsed.port);
	ut_asserteq(7, parsed.vid);
	ut_asserteq_mem(mac, parsed.ethaddr, sizeof(mac));

	/* the family prints its own help when nothing follows it */
	ut_assertok(run_command("ethsw fdb", 0));
	ut_assert_nextlinen("ethsw [port <port_no>] [vlan <vid>] fdb");
	ut_assert_console_end();

	/* an address which cannot be read is named, then the usage follows */
	ut_asserteq(1, run_command("ethsw fdb add zz", 0));
	ut_assert_nextline("Invalid MAC address: zz");
	ut_assertok(check_usage(uts));

	/* the broadcast address is what the parser uses to mean 'none' */
	ut_asserteq(1, run_command("ethsw fdb add ff:ff:ff:ff:ff:ff", 0));
	ut_assertok(check_usage(uts));

	return 0;
}
CMD_TEST(cmd_test_ethsw_fdb, UTF_CONSOLE);

/* Test the sub-commands which work on VLANs and on tagging */
static int cmd_test_ethsw_vlan(struct unit_test_state *uts)
{
	int ret;

	ret = setup_switch();
	if (ret)
		return ret;

	ut_assertok(run_ethsw(uts, "ethsw vlan show"));
	ut_asserteq_str("vlan_show", called);

	/* the VID follows add or del rather than the vlan keyword */
	ut_assertok(run_ethsw(uts, "ethsw vlan add 100"));
	ut_asserteq_str("vlan_set", called);
	ut_asserteq(100, parsed.vid);

	ut_assertok(run_ethsw(uts, "ethsw port 1 vlan del 100"));
	ut_asserteq_str("vlan_set", called);
	ut_asserteq(1, parsed.port);
	ut_asserteq(100, parsed.vid);

	/* a PVID is reported in the same field */
	ut_assertok(run_ethsw(uts, "ethsw pvid show"));
	ut_asserteq_str("pvid_show", called);

	ut_assertok(run_ethsw(uts, "ethsw port 2 pvid 5"));
	ut_asserteq_str("pvid_set", called);
	ut_asserteq(2, parsed.port);
	ut_asserteq(5, parsed.vid);

	ut_assertok(run_ethsw(uts, "ethsw untagged show"));
	ut_asserteq_str("port_untag_show", called);

	ut_assertok(run_ethsw(uts, "ethsw port 4 untagged all"));
	ut_asserteq_str("port_untag_set", called);
	ut_asserteq(4, parsed.port);

	ut_assertok(run_ethsw(uts, "ethsw egress tag show"));
	ut_asserteq_str("port_egr_vlan_show", called);

	ut_assertok(run_ethsw(uts, "ethsw egress tag classified"));
	ut_asserteq_str("port_egr_vlan_set", called);

	/* 'vlan fdb' is about learning, not about the entries themselves */
	ut_assertok(run_ethsw(uts, "ethsw vlan fdb show"));
	ut_asserteq_str("vlan_learn_show", called);

	ut_assertok(run_ethsw(uts, "ethsw vlan fdb shared"));
	ut_asserteq_str("vlan_learn_set", called);

	ut_assertok(run_ethsw(uts, "ethsw port 1 ingress filtering enable"));
	ut_asserteq_str("port_ingr_filt_set", called);
	ut_asserteq(1, parsed.port);

	return 0;
}
CMD_TEST(cmd_test_ethsw_vlan, UTF_CONSOLE);

/* Test statistics, learning and aggregation, and a missing handler */
static int cmd_test_ethsw_misc(struct unit_test_state *uts)
{
	int ret;

	ret = setup_switch();
	if (ret)
		return ret;

	ut_assertok(run_ethsw(uts, "ethsw port 1 statistics"));
	ut_asserteq_str("port_stats", called);
	ut_asserteq(1, parsed.port);

	ut_assertok(run_ethsw(uts, "ethsw learning show"));
	ut_asserteq_str("port_learn_show", called);

	/* both auto and disable reach the same handler */
	ut_assertok(run_ethsw(uts, "ethsw learning auto"));
	ut_asserteq_str("port_learn", called);

	ut_assertok(run_ethsw(uts, "ethsw port 2 learning disable"));
	ut_asserteq_str("port_learn", called);
	ut_asserteq(2, parsed.port);

	ut_assertok(run_ethsw(uts, "ethsw aggr show"));
	ut_asserteq_str("port_aggr_show", called);

	ut_assertok(run_ethsw(uts, "ethsw port 1 aggr 3"));
	ut_asserteq_str("port_aggr_set", called);
	ut_asserteq(1, parsed.port);
	ut_asserteq(3, parsed.aggr_grp);

	/* a family prints its own help rather than the whole usage message */
	ut_assertok(run_command("ethsw learning help", 0));
	ut_assert_nextlinen("ethsw [port <port_no>] learning");
	ut_assert_console_end();

	/*
	 * The stubs leave port_stats_clear out, so the parser finds the
	 * sub-command but has nothing to call and reports the switch by name
	 */
	called = NULL;
	ut_asserteq(1, run_command("ethsw statistics clear", 0));
	ut_assert_nextline("Command not available for: " ETHSW_TEST_NAME);
	ut_assert_console_end();
	ut_assertnull(called);

	return 0;
}
CMD_TEST(cmd_test_ethsw_misc, UTF_CONSOLE);

/* Test the command lines which the parser refuses */
static int cmd_test_ethsw_usage(struct unit_test_state *uts)
{
	int ret;

	ret = setup_switch();
	if (ret)
		return ret;

	/* a sub-command is needed */
	ut_asserteq(1, run_command("ethsw", 0));
	ut_assertok(check_usage(uts));

	/* help belongs to a family and is not a sub-command of its own */
	ut_asserteq(1, run_command("ethsw help", 0));
	ut_assertok(check_usage(uts));

	/* a word which is not a keyword */
	ut_asserteq(1, run_command("ethsw bogus", 0));
	ut_assertok(check_usage(uts));

	/* the optional keywords say which port to act on, but not what to do */
	ut_asserteq(1, run_command("ethsw port 1", 0));
	ut_assertok(check_usage(uts));

	/* and the port keyword needs a number after it */
	ut_asserteq(1, run_command("ethsw port enable", 0));
	ut_assertok(check_usage(uts));

	/* the keywords of a family do not mix with those of another */
	ut_asserteq(1, run_command("ethsw learning flush", 0));
	ut_assertok(check_usage(uts));

	return 0;
}
CMD_TEST(cmd_test_ethsw_usage, UTF_CONSOLE);
