/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright Canonical Ltd
 * Written by Simon Glass <sjg@chromium.org>
 */

#ifndef __EXPO_TEST_H
#define __EXPO_TEST_H

struct expo;

/* Number of seconds to average FPS over in test mode */
#define EXPO_FPS_AVG_SECONDS	5

/**
 * struct expo_test_mode - Test mode information for expo
 *
 * @enabled: true if test mode is enabled
 * @start_time_ms: Time when expo_enter_mode() was called (milliseconds)
 * @render_count: Number of calls to expo_render() since expo_enter_mode()
 * @fps_timestamps_ms: Timestamps for FPS calculation (milliseconds)
 * @fps_frame_counts: Frame counts at each timestamp
 * @fps_index: Current index in the FPS tracking arrays
 * @fps_last: Last calculated FPS value
 * @last_update: Time of last FPS update (milliseconds)
 * @base_time_us: Base time in microseconds for delta calculations
 * @render_delta_us: Time between update and render in microseconds
 * @sync_delta_us: Time taken by video_manual_sync() in microseconds
 * @poll_delta_us: Time taken by expo_poll() in microseconds
 * @render_total_us: Cumulative render time in current second (us)
 * @sync_total_us: Cumulative sync time in current second (us)
 * @poll_total_us: Cumulative poll time in current second (us)
 * @frame_count_last_sec: Number of frames in current measurement second
 * @render_avg_us: Average render time over last second (microseconds)
 * @sync_avg_us: Average sync time over last second (microseconds)
 * @poll_avg_us: Average poll time over last second (microseconds)
 */
struct expo_test_mode {
	bool enabled;
	ulong start_time_ms;
	int render_count;
	ulong fps_timestamps_ms[EXPO_FPS_AVG_SECONDS];
	int fps_frame_counts[EXPO_FPS_AVG_SECONDS];
	int fps_index;
	int fps_last;
	ulong last_update;
	ulong base_time_us;
	ulong render_delta_us;
	ulong sync_delta_us;
	ulong poll_delta_us;
	ulong render_total_us;
	ulong sync_total_us;
	ulong poll_total_us;
	int frame_count_last_sec;
	ulong render_avg_us;
	ulong sync_avg_us;
	ulong poll_avg_us;
};

#if IS_ENABLED(CONFIG_EXPO_TEST)

/**
 * expo_test_init() - Initialize test mode for an expo
 *
 * @exp: Expo to initialize test mode for
 * Return: 0 if OK, -ve on error
 */
int expo_test_init(struct expo *exp);

/**
 * expo_test_uninit() - Uninitialize test mode for an expo
 *
 * @exp: Expo to uninitialize test mode for
 */
void expo_test_uninit(struct expo *exp);

/**
 * expo_test_checkenv() - Check environment and reset test mode
 *
 * @exp: Expo to update test mode for
 *
 * Checks the expotest environment variable and updates the enabled flag
 * accordingly. Also resets the render count to 0.
 */
void expo_test_checkenv(struct expo *exp);

/**
 * expo_test_mark() - Mark the current time for delta calculations
 *
 * @exp: Expo to update test mode for
 *
 * Records the current time in microseconds as the base time for subsequent
 * delta calculations
 */
void expo_test_mark(struct expo *exp);

/**
 * expo_test_update() - Update test mode counters
 *
 * @exp: Expo to update test mode for
 */
void expo_test_update(struct expo *exp);

/**
 * expo_test_poll() - Calculate poll delta time
 *
 * @exp: Expo to update test mode for
 *
 * Calculates the time taken by expo_poll() based on the base time
 */
void expo_test_poll(struct expo *exp);

/**
 * expo_test_sync() - Calculate sync delta time
 *
 * @exp: Expo to update test mode for
 *
 * Calculates the time taken by video_manual_sync() based on the base time
 */
void expo_test_sync(struct expo *exp);

/**
 * expo_test_render() - Render test mode information
 *
 * @exp: Expo to render test info for
 * Return: 0 if OK, -ve on error
 */
int expo_test_render(struct expo *exp);

/**
 * expo_calc_fps() - Calculate FPS based on recent frame history
 *
 * @test: Test mode data containing frame history
 * Return: Calculated FPS value, or 0 if insufficient data
 */
int expo_calc_fps(struct expo_test_mode *test);

#else

static inline int expo_test_init(struct expo *exp)
{
	return 0;
}

static inline void expo_test_uninit(struct expo *exp)
{
}

static inline void expo_test_checkenv(struct expo *exp)
{
}

static inline void expo_test_mark(struct expo *exp)
{
}

static inline void expo_test_update(struct expo *exp)
{
}

static inline void expo_test_poll(struct expo *exp)
{
}

static inline void expo_test_sync(struct expo *exp)
{
}

static inline int expo_test_render(struct expo *exp)
{
	return 0;
}

static inline int expo_calc_fps(struct expo_test_mode *test)
{
	return 0;
}

#endif /* EXPO_TEST */

#endif /* __EXPO_TEST_H */
