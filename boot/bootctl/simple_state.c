// SPDX-License-Identifier: GPL-2.0+
/*
 * Provides a simple name/value pair
 *
 * The file format a ordered series of lines of the form:
 * key=value\n
 *
 * with a nul terminator at the end. Strings are stored without quoting. Ints
 * are stored as decimal, perhaps with leading '-'. Bools are stored as 0 or 1
 *
 * keys consist only of characters a-z, _ and 0-9
 *
 * Copyright 2025 Canonical Ltd
 * Written by Simon Glass <simon.glass@canonical.com>
 */

#define LOG_CATEGORY	UCLASS_BOOTCTL

#include <alist.h>
#include <abuf.h>
#include <bootctl.h>
#include <bootmeth.h>
#include <bootstd.h>
#include <ctype.h>
#include <dm.h>
#include <fs_legacy.h>
#include <log.h>
#include <malloc.h>
#include <vsprintf.h>
#include <bootctl/state.h>
#include <linux/sizes.h>

enum {
	/* maximum length of a key, excluding nul terminator */
	MAX_KEY_LEN	= 30,

	/* maximum length of a value, excluding nul terminator */
	MAX_VAL_LEN	= SZ_4K,
	MAX_LINE_LEN	= MAX_KEY_LEN + MAX_VAL_LEN + 10,
	MAX_FILE_SIZE	= SZ_64K,
};

struct keyval {
	char *key;
	char *val;
};

/**
 * struct sstate_priv - Private data for simple-state
 *
 * @ifname: Interface which stores the state
 * @dev_part: Device and partition number which stores the state
 * @filename: Filename which stores the state
 * @items: List of struct keyval
 */
struct sstate_priv {
	const char *ifname;
	const char *dev_part;
	const char *fname;
	struct alist items;
};

static void clear_vals(struct sstate_priv *priv)
{
	struct keyval *kv;

	log_debug("clearing\n");
	alist_for_each(kv, &priv->items) {
		free(kv->key);
		free(kv->val);
	}

	alist_empty(&priv->items);
}

static struct keyval *find_item(struct sstate_priv *priv, const char *key)
{
	struct keyval *kv;

	log_debug("find %s: ", key);
	alist_for_each(kv, &priv->items) {
		if (!strcmp(kv->key, key)) {
			log_debug("found\n");
			return kv;
		}
	}
	log_debug("not found\n");

	return NULL;
}

static int add_val(struct sstate_priv *priv, const char *key,
		   const char *val)
{
	int keylen, vallen;
	struct keyval kv;
	const unsigned char *p;

	log_content("add %s=%s\n", key, val);
	for (keylen = 0, p = key; keylen <= MAX_KEY_LEN && *p; keylen++, p++) {
		if (!(*p == '_' || isdigit(*p) || islower(*p)) || *p >= 127) {
			log_content("- invalid character %02x\n", *p);
			return log_msg_ret("wvk", -EKEYREJECTED);
		}
	}
	if (!keylen) {
		log_content("- empty key\n");
		return log_msg_ret("wve", -EINVAL);
	}
	if (keylen > MAX_KEY_LEN) {
		log_content("- key too long %d\n", keylen);
		return log_msg_ret("wvl", -EKEYREJECTED);
	}
	vallen = strnlen(val, MAX_VAL_LEN + 1);
	if (vallen > MAX_VAL_LEN) {
		log_content("- val too long\n");
		return log_msg_ret("wvv", -E2BIG);
	}

	kv.key = strndup(key, MAX_KEY_LEN);
	if (!kv.key) {
		return log_msg_ret("avk", -ENOMEM);
	}
	kv.val = strndup(val, MAX_VAL_LEN);
	if (!kv.val) {
		free(kv.key);
		return log_msg_ret("avv", -ENOMEM);
	}

	if (!alist_add(&priv->items, kv)) {
		free(kv.key);
		free(kv.val);
		return log_msg_ret("avl", -ENOMEM);
	}

	return 0;
}

static int write_val(struct sstate_priv *priv, const char *key,
		     const char *val)
{
	struct keyval *kv;
	int ret;

	log_content("write %s=%s\n", key, val);
	if (!key || !val)
		return log_msg_ret("wkn", -EINVAL);
	kv = find_item(priv, key);
	if (kv) {
		int len = strnlen(val, MAX_VAL_LEN + 1);
		char *new;

		if (len > MAX_VAL_LEN) {
			log_content("- val too long\n");
			return log_msg_ret("wvr", -E2BIG);
		}
		log_content("- update\n");
		new = realloc(kv->val, len);
		if (!new)
			return log_msg_ret("wvr", -ENOMEM);
		strlcpy(new, val, len + 1);
		kv->val = new;
	} else {
		ret = add_val(priv, key, val);
		if (ret)
			return log_msg_ret("swB", ret);
	}
	log_content("done\n");

	return 0;
}

static int sstate_clear(struct udevice *dev)
{
	struct sstate_priv *priv = dev_get_priv(dev);

	clear_vals(priv);

	return 0;
}

static int sstate_load(struct udevice *dev)
{
	struct sstate_priv *priv = dev_get_priv(dev);
	struct membuf inf;
	struct abuf buf;
	char line[MAX_LINE_LEN];
	bool ok;
	int len;
	int ret;

	log_debug("loading\n");
	clear_vals(priv);
	log_debug("read file ifname '%s' dev_part '%s' fname '%s'\n",
		  priv->ifname, priv->dev_part, priv->fname);
	ret = fs_load_alloc(priv->ifname, priv->dev_part, priv->fname,
				MAX_FILE_SIZE, 0, &buf);
	if (ret)
		return log_msg_ret("ssa", ret);

	log_debug("parsing\n");
	membuf_init_with_data(&inf, buf.data, buf.size);
	for (ok = true;
	     len = membuf_readline(&inf, line, sizeof(line), ' ', true),
	     len && ok;) {
		char *key = strtok(line, "=");
		char *val = strtok(NULL, "=");

		if (key && val)
			ok = !add_val(priv, key, val);
	}

	abuf_uninit(&buf);

	if (!ok) {
		clear_vals(priv);
		return log_msg_ret("ssr", -ENOMEM);
	}

	return 0;
}

static int sstate_save_to_buf(struct udevice *dev, struct abuf *buf)
{
	struct sstate_priv *priv = dev_get_priv(dev);
	struct membuf inf;
	struct keyval *kv;
	char *data;
	int size;

	log_debug("saving\n");
	abuf_init(buf);
	if (!abuf_realloc(buf, MAX_FILE_SIZE))
		return log_msg_ret("ssa", -ENOMEM);

	membuf_init(&inf, buf->data, buf->size);

	alist_for_each(kv, &priv->items) {
		int keylen = strnlen(kv->key, MAX_KEY_LEN + 1);
		int vallen = strnlen(kv->val, MAX_VAL_LEN + 1);

		if (keylen > MAX_KEY_LEN || vallen > MAX_VAL_LEN)
			return log_msg_ret("ssp", -E2BIG);

		log_content("save %s=%s\n", kv->key, kv->val);
		if (membuf_put(&inf, kv->key, keylen) != keylen ||
		    membuf_put(&inf, "=", 1) != 1 ||
		    membuf_put(&inf, kv->val, vallen) != vallen ||
		    membuf_put(&inf, "\n", 1) != 1)
			return log_msg_ret("ssp", -ENOSPC);
	}
	if (membuf_put(&inf, "", 1) != 1)
		return log_msg_ret("ssp", -ENOSPC);

	size = membuf_getraw(&inf, MAX_FILE_SIZE, true, &data);
	if (data != buf->data)
		return log_msg_ret("ssp", -EFAULT);
	buf->size = size;

	return 0;
}

static int sstate_save(struct udevice *dev)
{
	struct sstate_priv *priv = dev_get_priv(dev);
	loff_t actwrite;
	struct abuf buf;
	int ret;

	ret = sstate_save_to_buf(dev, &buf);
	if (ret)
		return log_msg_ret("sss", ret);

	log_debug("set dest ifname '%s' dev_part '%s'\n", priv->ifname,
		  priv->dev_part);
	ret = fs_set_blk_dev(priv->ifname, priv->dev_part, FS_TYPE_ANY);
	if (ret) {
		ret = log_msg_ret("sss", ret);
	} else {
		log_debug("write fname '%s' size %zx\n", priv->fname, buf.size);
		ret = fs_write(priv->fname, abuf_addr(&buf), 0, buf.size,
			       &actwrite);
		if (ret)
			ret = log_msg_ret("ssw", ret);
	}

	abuf_uninit(&buf);
	if (ret)
		return ret;

	return 0;
}

static int sstate_read_bool(struct udevice *dev, const char *prop, bool *valp)
{
	struct sstate_priv *priv = dev_get_priv(dev);
	const struct keyval *kv;

	log_debug("read_bool\n");
	kv = find_item(priv, prop);
	if (!kv)
		return log_msg_ret("srb", -ENOENT);

	*valp = !strcmp(kv->val, "1") ? true : false;
	log_debug("- val %s: %d\n", kv->val, *valp);

	return 0;
}

static int sstate_write_bool(struct udevice *dev, const char *prop, bool val)
{
	struct sstate_priv *priv = dev_get_priv(dev);
	int ret;

	ret = write_val(priv, prop, simple_itoa(val));
	if (ret)
		return log_msg_ret("swb", ret);

	return 0;
}

static int sstate_read_int(struct udevice *dev, const char *prop, long *valp)
{
	struct sstate_priv *priv = dev_get_priv(dev);
	const struct keyval *kv;

	log_debug("read_bool\n");
	kv = find_item(priv, prop);
	if (!kv)
		return log_msg_ret("srb", -ENOENT);

	*valp = simple_strtol(kv->val, NULL, 10);
	log_debug("- val %s: %ld\n", kv->val, *valp);

	return 0;
}

static int sstate_write_int(struct udevice *dev, const char *prop, long val)
{
	struct sstate_priv *priv = dev_get_priv(dev);
	int ret;

	log_debug("write_int %ld\n", val);
	ret = write_val(priv, prop, simple_itoa(val));
	if (ret)
		return log_msg_ret("swb", ret);

	return 0;
}

static int sstate_read_str(struct udevice *dev, const char *prop,
			   const char **valp)
{
	struct sstate_priv *priv = dev_get_priv(dev);
	const struct keyval *kv;

	log_debug("read_bool\n");
	kv = find_item(priv, prop);
	if (!kv)
		return log_msg_ret("srb", -ENOENT);

	*valp = kv->val;
	log_debug("- val %s: %s\n", kv->val, *valp);

	return 0;
}

static int sstate_write_str(struct udevice *dev, const char *prop,
			    const char *str)
{
	struct sstate_priv *priv = dev_get_priv(dev);
	int ret;

	ret = write_val(priv, prop, str);
	if (ret)
		return log_msg_ret("swb", ret);

	return 0;
}

static int sstate_probe(struct udevice *dev)
{
	struct sstate_priv *priv = dev_get_priv(dev);

	alist_init_struct(&priv->items, struct keyval);

	return 0;
}

static int sstate_of_to_plat(struct udevice *dev)
{
	struct sstate_priv *priv = dev_get_priv(dev);
	int ret;

	ret = dev_read_string_index(dev, "location", 0, &priv->ifname);
	if (ret)
		return log_msg_ret("ssi", ret);

	ret = dev_read_string_index(dev, "location", 1, &priv->dev_part);
	if (ret)
		return log_msg_ret("ssd", ret);

	priv->fname = dev_read_string(dev, "filename");
	if (!priv->ifname || !priv->dev_part || !priv->fname)
		return log_msg_ret("ssp", -EINVAL);

	return 0;
}

static int sstate_bind(struct udevice *dev)
{
	struct bootctl_uc_plat *ucp = dev_get_uclass_plat(dev);

	ucp->desc = "Stores state information about booting";

	return 0;
}

static struct bc_state_ops ops = {
	.load	= sstate_load,
	.save	= sstate_save,
	.save_to_buf	 = sstate_save_to_buf,
	.clear	= sstate_clear,
	.read_bool = sstate_read_bool,
	.write_bool = sstate_write_bool,
	.read_int  = sstate_read_int,
	.write_int = sstate_write_int,
	.read_str  = sstate_read_str,
	.write_str = sstate_write_str,
};

static const struct udevice_id sstate_ids[] = {
	{ .compatible = "bootctl,simple-state" },
	{ .compatible = "bootctl,state" },
	{ }
};

static int sstate_remove(struct udevice *dev)
{
	struct sstate_priv *priv = dev_get_priv(dev);

	clear_vals(priv);
	alist_uninit(&priv->items);

	return 0;
}

U_BOOT_DRIVER(simple_state) = {
	.name		= "simple_state",
	.id		= UCLASS_BOOTCTL_STATE,
	.of_match	= sstate_ids,
	.ops		= &ops,
	.bind		= sstate_bind,
	.probe		= sstate_probe,
	.remove		= sstate_remove,
	.of_to_plat	= sstate_of_to_plat,
	.priv_auto	= sizeof(struct sstate_priv),
};
