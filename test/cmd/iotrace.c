// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the iotrace command
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

/*
 * mapmem.h comes first since it pulls in asm/io.h, which is where iotrace.h
 * expects to be included from
 */
#include <mapmem.h>
#include <command.h>
#include <console.h>
#include <dm.h>
#include <iotrace.h>
#include <malloc.h>
#include <asm/test.h>
#include <test/cmd.h>
#include <test/ut.h>

/* size of the trace buffer the tests use */
#define BUF_SIZE	0x400

/* size of the block of memory the tests trace accesses to */
#define REGS_SIZE	0x100

/* value written to the traced memory, distinctive enough to spot in a dump */
#define TEST_VALUE	0x12345678

/* space one record takes up, which differs between 32- and 64-bit sandbox */
#define REC_SIZE	((ulong)sizeof(struct iotrace_record))

/* the last line of the help text, which ends the usage message */
#define USAGE_LAST \
	"iotrace dump                         - dump iotrace buffer"

/**
 * struct iotrace_test - buffers a test traces into and accesses
 *
 * @buf: Trace buffer
 * @buf_addr: Address of @buf, as the command wants it
 * @regs: Block of memory standing in for a device's registers
 * @regs_addr: Address of @regs, as it appears in the trace
 */
struct iotrace_test {
	void *buf;
	ulong buf_addr;
	void *regs;
	ulong regs_addr;
};

/**
 * setup_trace() - Set up a trace of a block of memory
 *
 * Allocates the two buffers, points the tracer at one and limits tracing to
 * the other. The limit is what makes the record count predictable: without it
 * any I/O another part of U-Boot happens to do lands in the buffer too.
 *
 * @uts: Test state
 * @trc: Returns the buffers in use
 * Return: 0 if OK, other value on error
 */
static int setup_trace(struct unit_test_state *uts, struct iotrace_test *trc)
{
	trc->buf = malloc(BUF_SIZE);
	ut_assertnonnull(trc->buf);
	trc->regs = calloc(1, REGS_SIZE);
	ut_assertnonnull(trc->regs);
	trc->buf_addr = map_to_sysmem(trc->buf);
	trc->regs_addr = map_to_sysmem(trc->regs);

	/* readl() and writel() do nothing on sandbox until this is on */
	sandbox_set_enable_memio(true);

	ut_assertok(run_commandf("iotrace buffer %lx %x", trc->buf_addr,
				 BUF_SIZE));
	ut_assertok(run_commandf("iotrace limit %lx %x", trc->regs_addr,
				 REGS_SIZE));

	return 0;
}

/**
 * finish_trace() - Drop the trace and free the buffers
 *
 * The buffer must be dropped before it is freed, or a later access would
 * write a record into memory which is no longer ours.
 *
 * @uts: Test state
 * @trc: Buffers in use
 * Return: 0 if OK, other value on error
 */
static int finish_trace(struct unit_test_state *uts, struct iotrace_test *trc)
{
	ut_assertok(run_command("iotrace pause", 0));
	ut_assertok(run_command("iotrace buffer", 0));
	ut_assertok(run_command("iotrace limit", 0));
	sandbox_set_enable_memio(false);
	free(trc->regs);
	free(trc->buf);
	ut_assert_console_end();

	return 0;
}

/**
 * check_stats() - Check the output of 'iotrace stats'
 *
 * The checksum covers the timestamp of each record, so it differs from run to
 * run; only the label is checked here.
 *
 * @uts: Test state
 * @enabled: true if tracing is expected to be on
 * @start: Expected buffer address
 * @size: Expected buffer size
 * @needed: Expected size needed to hold everything traced so far
 * @region_start: Expected start of the traced region
 * @region_size: Expected size of the traced region
 * @count: Expected number of records in the buffer
 * Return: 0 if OK, other value on error
 */
static int check_stats(struct unit_test_state *uts, bool enabled, ulong start,
		       ulong size, ulong needed, ulong region_start,
		       ulong region_size, ulong count)
{
	ut_assertok(run_command("iotrace stats", 0));
	ut_assert_nextline("iotrace is %sabled", enabled ? "en" : "dis");
	ut_assert_nextline("Start:  %08lx", start);
	ut_assert_nextline("Actual Size:   %08lx", size);
	ut_assert_nextline("Needed Size:   %08lx", needed);
	ut_assert_nextline("Region: %08lx", region_start);
	ut_assert_nextline("Size:   %08lx", region_size);
	ut_assert_nextline("Offset: %08lx", count * REC_SIZE);
	ut_assert_nextline("Output: %08lx", start + count * REC_SIZE);
	ut_assert_nextline("Count:  %08lx", count);
	ut_assert_nextlinen("CRC32:  ");
	ut_assert_console_end();

	return 0;
}

/**
 * check_record() - Check one line of 'iotrace dump'
 *
 * @uts: Test state
 * @write: true for a write record, false for a read
 * @value: Expected value
 * @addr: Expected address
 * Return: 0 if OK, other value on error
 */
static int check_record(struct unit_test_state *uts, bool write, ulong value,
			ulong addr)
{
	char pat[80];

	/* the timestamp is whatever the clock said, so match any digits */
	snprintf(pat, sizeof(pat), "^[0-9]+: 0x%08lx %s 0x%08lx$", value,
		 write ? "-->" : "<--", addr);
	ut_assert_nextline_regex(pat);

	return 0;
}

/* Test collecting a trace and reading it back */
static int cmd_test_iotrace_base(struct unit_test_state *uts)
{
	struct iotrace_test trc;

	ut_assertok(setup_trace(uts, &trc));

	/* the buffer is set but nothing is recorded until tracing is resumed */
	ut_assertok(check_stats(uts, false, trc.buf_addr, BUF_SIZE, 0,
				trc.regs_addr, REGS_SIZE, 0));
	ut_asserteq(0, iotrace_get_checksum());

	ut_assertok(run_command("iotrace resume", 0));
	writel(TEST_VALUE, trc.regs + 4);
	ut_asserteq(TEST_VALUE, readl(trc.regs + 4));
	ut_assertok(run_command("iotrace pause", 0));

	ut_assertok(check_stats(uts, false, trc.buf_addr, BUF_SIZE,
				2 * REC_SIZE, trc.regs_addr, REGS_SIZE, 2));
	ut_assert(iotrace_get_checksum());

	/* the write comes first, then the read of the same address */
	ut_assertok(run_command("iotrace dump", 0));
	ut_assert_nextline("Timestamp  Value          Address");
	ut_assertok(check_record(uts, true, TEST_VALUE, trc.regs_addr + 4));
	ut_assertok(check_record(uts, false, TEST_VALUE, trc.regs_addr + 4));
	ut_assert_console_end();

	ut_assertok(finish_trace(uts, &trc));

	/* with no buffer there is nothing to dump */
	ut_assertok(run_command("iotrace dump", 0));
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_iotrace_base, UTF_CONSOLE);

/* Test that tracing can be paused and resumed */
static int cmd_test_iotrace_pause(struct unit_test_state *uts)
{
	struct iotrace_test trc;

	ut_assertok(setup_trace(uts, &trc));

	/* nothing is recorded while tracing is off */
	writel(TEST_VALUE, trc.regs);
	ut_assertok(check_stats(uts, false, trc.buf_addr, BUF_SIZE, 0,
				trc.regs_addr, REGS_SIZE, 0));

	ut_assertok(run_command("iotrace resume", 0));
	ut_assertok(check_stats(uts, true, trc.buf_addr, BUF_SIZE, 0,
				trc.regs_addr, REGS_SIZE, 0));
	writel(TEST_VALUE, trc.regs);

	/* and nothing more once it is paused again */
	ut_assertok(run_command("iotrace pause", 0));
	writel(TEST_VALUE, trc.regs);
	ut_assertok(check_stats(uts, false, trc.buf_addr, BUF_SIZE, REC_SIZE,
				trc.regs_addr, REGS_SIZE, 1));

	ut_assertok(finish_trace(uts, &trc));

	return 0;
}
CMD_TEST(cmd_test_iotrace_pause, UTF_CONSOLE);

/* Test that only accesses inside the region set by 'iotrace limit' are kept */
static int cmd_test_iotrace_limit(struct unit_test_state *uts)
{
	struct iotrace_test trc;

	ut_assertok(setup_trace(uts, &trc));

	/* narrow the region to the first two words of the block */
	ut_assertok(run_commandf("iotrace limit %lx 8", trc.regs_addr));
	ut_assertok(run_command("iotrace resume", 0));

	writel(TEST_VALUE, trc.regs);
	writel(TEST_VALUE, trc.regs + 4);

	/* the word at the end of the region is outside it */
	writel(TEST_VALUE, trc.regs + 8);

	/* as is anything further up the block */
	writel(TEST_VALUE, trc.regs + REGS_SIZE - 4);

	ut_assertok(run_command("iotrace pause", 0));
	ut_assertok(check_stats(uts, false, trc.buf_addr, BUF_SIZE,
				2 * REC_SIZE, trc.regs_addr, 8, 2));

	ut_assertok(run_command("iotrace dump", 0));
	ut_assert_nextline("Timestamp  Value          Address");
	ut_assertok(check_record(uts, true, TEST_VALUE, trc.regs_addr));
	ut_assertok(check_record(uts, true, TEST_VALUE, trc.regs_addr + 4));
	ut_assert_console_end();

	/* dropping the limit brings the rest of the address space back */
	ut_assertok(run_command("iotrace limit", 0));
	ut_assertok(run_commandf("iotrace buffer %lx %x", trc.buf_addr,
				 BUF_SIZE));
	ut_assertok(run_command("iotrace resume", 0));
	writel(TEST_VALUE, trc.regs + 8);
	ut_assertok(run_command("iotrace pause", 0));
	ut_assertok(check_stats(uts, false, trc.buf_addr, BUF_SIZE, REC_SIZE, 0,
				0, 1));

	ut_assertok(finish_trace(uts, &trc));

	return 0;
}
CMD_TEST(cmd_test_iotrace_limit, UTF_CONSOLE);

/* Test what happens when the buffer fills up */
static int cmd_test_iotrace_full(struct unit_test_state *uts)
{
	struct iotrace_test trc;
	int i;

	ut_assertok(setup_trace(uts, &trc));

	/* a buffer with room for one record only */
	ut_assertok(run_commandf("iotrace buffer %lx %lx", trc.buf_addr,
				 2 * REC_SIZE));
	ut_assertok(run_command("iotrace resume", 0));
	for (i = 0; i < 4; i++)
		writel(TEST_VALUE, trc.regs + i * 4);
	ut_assertok(run_command("iotrace pause", 0));

	/*
	 * Throw away the warning about the buffer being exhausted. It comes
	 * from WARN_ONCE(), so it appears only on the first run of the test in
	 * any one U-Boot and cannot be asserted on.
	 */
	console_record_reset();

	/* the needed size counts the records which did not fit */
	ut_assertok(check_stats(uts, false, trc.buf_addr, 2 * REC_SIZE,
				4 * REC_SIZE, trc.regs_addr, REGS_SIZE, 1));

	/* only the record which fitted is in the buffer */
	ut_assertok(run_command("iotrace dump", 0));
	ut_assert_nextline("Timestamp  Value          Address");
	ut_assertok(check_record(uts, true, TEST_VALUE, trc.regs_addr));
	ut_assert_console_end();

	/* setting the buffer again starts a fresh count */
	ut_assertok(run_commandf("iotrace buffer %lx %x", trc.buf_addr,
				 BUF_SIZE));
	ut_assertok(check_stats(uts, false, trc.buf_addr, BUF_SIZE, 0,
				trc.regs_addr, REGS_SIZE, 0));

	ut_assertok(finish_trace(uts, &trc));

	return 0;
}
CMD_TEST(cmd_test_iotrace_full, UTF_CONSOLE);

/* Check the usage message, which every bad command line produces */
static int check_usage(struct unit_test_state *uts)
{
	ut_assert_nextline("iotrace - iotrace utility commands");
	ut_assert_nextline_empty();
	ut_assert_nextline("Usage:");
	ut_assert_skip_to_line(USAGE_LAST);
	ut_assert_console_end();

	return 0;
}

/* Test 'iotrace' with a bad command line */
static int cmd_test_iotrace_usage(struct unit_test_state *uts)
{
	/* start from a known state, whatever an earlier test has left */
	ut_assertok(run_command("iotrace buffer", 0));
	ut_assertok(run_command("iotrace limit", 0));

	/* a sub-command is needed */
	ut_asserteq(1, run_command("iotrace", 0));
	ut_assertok(check_usage(uts));

	/* and must start with the letter of one the command knows */
	ut_asserteq(1, run_command("iotrace zap", 0));
	ut_assertok(check_usage(uts));

	/* buffer and limit take an address and a size, or nothing at all */
	ut_asserteq(1, run_command("iotrace buffer 1000", 0));
	ut_assertok(check_usage(uts));
	ut_asserteq(1, run_command("iotrace limit 1000", 0));
	ut_assertok(check_usage(uts));

	/* only the first letter is looked at, so this sets the buffer */
	ut_assertok(run_command("iotrace bananas 1000000 100", 0));
	ut_assertok(check_stats(uts, false, 0x1000000, 0x100, 0, 0, 0, 0));
	ut_assertok(run_command("iotrace buffer", 0));

	return 0;
}
CMD_TEST(cmd_test_iotrace_usage, UTF_CONSOLE);
