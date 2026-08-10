// SPDX-License-Identifier: GPL-2.0+
/*
 * Environment stored in a host file, for sandbox
 *
 * Copyright 2026 Google LLC
 * Written by Simon Glass <sjg@chromium.org>
 */

#include <command.h>
#include <env.h>
#include <env_internal.h>
#include <errno.h>
#include <os.h>
#include <asm/global_data.h>
#include <asm/state.h>
#include <linux/stddef.h>

DECLARE_GLOBAL_DATA_PTR;

/**
 * env_sandbox_fname() - Get the file holding the environment
 *
 * Return: filename given with -E, or NULL if the option was not used
 */
static const char *env_sandbox_fname(void)
{
	return state_get_current()->env_fname;
}

static int env_sandbox_init(void)
{
	/*
	 * Without -E there is nowhere to keep the environment, so report the
	 * location as unavailable. Returning -ENOENT would mark it as
	 * initialised and let env_save() try to use it.
	 */
	if (!env_sandbox_fname())
		return -ENODEV;

	gd->env_valid = ENV_VALID;

	return 0;
}

static int env_sandbox_load(void)
{
	const char *fname = env_sandbox_fname();
	env_t *env;
	int size, ret;
	void *buf;
	int fd;

	if (!fname)
		return -ENODEV;

	/*
	 * Check for the file first, since os_read_file() complains when it is
	 * missing and that is the normal state before the first save.
	 */
	fd = os_open(fname, OS_O_RDONLY);
	if (fd < 0) {
		/*
		 * The file appears when the environment is first saved, so
		 * until then start from the default. Report success even so,
		 * to keep this location selected: otherwise the next one is
		 * chosen and 'saveenv' has nowhere to write.
		 */
		env_set_default(NULL, 0);

		return 0;
	}
	os_close(fd);

	ret = os_read_file(fname, &buf, &size);
	if (ret)
		return ret;

	if (size != sizeof(env_t)) {
		printf("Environment file '%s' has size %d, expected %d\n",
		       fname, size, (int)sizeof(env_t));
		os_free(buf);
		env_set_default(NULL, 0);

		return -ENOMSG;
	}

	env = buf;
	ret = env_import((char *)env, 1, H_EXTERNAL);
	os_free(buf);

	return ret;
}

static int env_sandbox_save(void)
{
	const char *fname = env_sandbox_fname();
	env_t env;
	int ret;

	if (!fname)
		return -ENODEV;

	ret = env_export(&env);
	if (ret)
		return ret;

	if (os_write_file(fname, &env, sizeof(env)))
		return -EIO;

	return 0;
}

static int env_sandbox_erase(void)
{
	const char *fname = env_sandbox_fname();

	if (!fname)
		return -ENODEV;

	/* removing the file is how this location is emptied */
	if (os_unlink(fname))
		return -EIO;

	return 0;
}

U_BOOT_ENV_LOCATION(sandbox) = {
	.location	= ENVL_SANDBOX,
	.init		= env_sandbox_init,
	.load		= env_sandbox_load,
	.save		= env_sandbox_save,
	.erase		= env_sandbox_erase,
	ENV_NAME("sandbox")
};
