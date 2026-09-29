// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the 2048 command
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <command.h>
#include <console.h>
#include <rand.h>
#include <test/cmd.h>
#include <test/ut.h>

/* drawBoard() homes the cursor, then leaves it one line higher when it ends */
#define HOME		"\x1b[H"
#define UP		"\x1b[A"

/* the game hides the cursor and clears the screen before the first board */
#define START		"\x1b[?25l\x1b[2J" HOME

/* the game shows the cursor again on the way out */
#define FINISH		"\x1b[?25h"

/*
 * The start of the row holding a tile showing 4, which getColor() draws as
 * white on colour 4. Only the start can be checked, since the empty squares
 * which follow are drawn with a middle dot: console_record_readline() ends a
 * line at any byte below its minimum, and char is signed, so a UTF-8 byte
 * splits the recorded line in two.
 */
#define TILE_4		"\x1b[38;5;255;48;5;2m   4   "

/*
 * seed_board() - Put the random-number generator back to its starting point
 *
 * The two tiles a new game starts with are placed with rand(), as is the tile
 * added after each move. Seeding with the value rand() begins life with gives
 * the same board on every run, whatever else has called rand() beforehand.
 */
static void seed_board(void)
{
	srand(1);
}

/* Test the self-test of the sliding and merging logic */
static int cmd_test_2048_base(struct unit_test_state *uts)
{
	ut_assertok(run_command("2048 test", 0));
	ut_assert_nextline("All 13 tests executed successfully");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_2048_base, UTF_CONSOLE);

/* Test starting a game and quitting it without making a move */
static int cmd_test_2048_quit(struct unit_test_state *uts)
{
	seed_board();
	ut_asserteq(1, console_in_puts("q"));
	ut_assertok(run_command("2048", 0));

	/* the score starts at zero, whatever a previous game left it at */
	ut_assert_nextline(START HOME "2048.c %17d pts", 0);
	ut_assert_nextline_empty();

	/* the deal puts a 4 in the first column of the second row of tiles */
	ut_assert_skip_to_linen(TILE_4);

	ut_assert_skip_to_line(UP "            QUIT            ");
	ut_assert_nextline(FINISH);
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_2048_quit, UTF_CONSOLE);

/* Test that an arrow key moves the tiles and that a merge scores */
static int cmd_test_2048_move(struct unit_test_state *uts)
{
	seed_board();

	/* the escape sequence a terminal sends for the down arrow, twice */
	ut_asserteq(7, console_in_puts("\x1b[B\x1b[Bq"));
	ut_assertok(run_command("2048", 0));

	/* the board as the game deals it, with nothing yet merged */
	ut_assert_nextline(START HOME "2048.c %17d pts", 0);

	/*
	 * A move which changes the board draws it twice, once for the slide
	 * and once after the new tile appears. The first move only slides the
	 * two tiles of the deal to the bottom, so the score stays at zero.
	 */
	ut_assert_skip_to_line(UP HOME "2048.c %17d pts", 0);
	ut_assert_skip_to_line(UP HOME "2048.c %17d pts", 0);

	/* the second move brings two 2s together, scoring their sum */
	ut_assert_skip_to_line(UP HOME "2048.c %17d pts", 4);
	ut_assert_skip_to_line(UP HOME "2048.c %17d pts", 4);

	ut_assert_skip_to_line(UP "            QUIT            ");
	ut_assert_nextline(FINISH);
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_2048_move, UTF_CONSOLE);

/* Test that the command refuses an option, rather than dealing a board */
static int cmd_test_2048_option(struct unit_test_state *uts)
{
	ut_asserteq(1, run_command("2048 -x", 0));
	ut_assert_nextline("2048 - The 2048 game");
	ut_assert_nextline_empty();
	ut_assert_nextline("Usage:");
	ut_assert_nextlinen("2048 Use your arrow keys to move the tiles.");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_2048_option, UTF_CONSOLE);

/* Test that the command refuses more arguments than it has room for */
static int cmd_test_2048_usage(struct unit_test_state *uts)
{
	ut_asserteq(1, run_command("2048 test extra", 0));
	ut_assert_nextline("2048 - The 2048 game");
	ut_assert_nextline_empty();
	ut_assert_nextline("Usage:");
	ut_assert_nextlinen("2048 Use your arrow keys to move the tiles.");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_2048_usage, UTF_CONSOLE);
