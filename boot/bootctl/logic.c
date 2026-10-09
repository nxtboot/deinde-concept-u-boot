// SPDX-License-Identifier: GPL-2.0+
/*
 * Implementation of the logic to perform a boot
 *
 * Copyright 2025 Canonical Ltd
 * Written by Simon Glass <simon.glass@canonical.com>
 */

#define LOG_CATEGORY	UCLASS_BOOTCTL

#include <bootctl.h>
#include <dm.h>
#include <hexdump.h>
#include <log.h>
#include <luks.h>
#include <part.h>
#include <time.h>
#include <tkey.h>
#include <version.h>
#include <bootctl/logic.h>
#include <bootctl/measure.h>
#include <bootctl/oslist.h>
#include <bootctl/state.h>
#include <bootctl/ui.h>
#include <bootctl/util.h>
#include <dm/device-internal.h>
#include <dm/uclass-internal.h>
#include <u-boot/sha256.h>

enum {
	COUNTDOWN_INTERVAL_MS	= 1000,	/* interval between autoboot updates */
	ERROR_TIMEOUT_MS	= 5000,	/* timeout for error message display */
};

static int logic_bind(struct udevice *dev)
{
	struct bootctl_uc_plat *ucp = dev_get_uclass_plat(dev);

	ucp->desc = "Controls the boot process";

	return 0;
}

static int logic_prepare(struct udevice *dev)
{
	struct logic_priv *priv = dev_get_priv(dev);
	int ret;

	/* figure out the UI to use */
	ret = bootctl_get_dev(UCLASS_BOOTCTL_UI, &priv->ui);
	if (ret) {
		log_err("UI required but failed (err=%dE)\n", ret);
		return log_msg_ret("bgd", ret);
	}

	/* figure out the measurement to use */
	if (priv->opt_measure) {
		ret = bootctl_get_dev(UCLASS_BOOTCTL_MEASURE,
					    &priv->meas);
		if (ret) {
			log_err("Measurement required but failed (err=%dE)\n",
				ret);
			return log_msg_ret("bgs", ret);
		}
	}

	/* figure out at least one oslist driver to use */
	ret = uclass_first_device_err(UCLASS_BOOTCTL_OSLIST, &priv->oslist);
	if (ret)
		return log_msg_ret("bgo", ret);

	/* figure out the state to use */
	ret = bootctl_get_dev(UCLASS_BOOTCTL_STATE, &priv->state);
	if (ret)
		return log_msg_ret("bgs", ret);

	if (priv->opt_labels) {
		ret = bootdev_set_order(priv->opt_labels);
		if (ret)
			return log_msg_ret("blo", ret);
	}

	/* Find TKey device if enabled (test can override this) */
	if (priv->opt_tkey) {
		ret = uclass_find_first_device(UCLASS_TKEY, &priv->tkey);
		if (ret || !priv->tkey) {
			log_debug("TKey not found at startup\n");
		} else {
			log_debug("TKey '%s'\n", priv->tkey->name);
			/* Device found but not probed yet - not present */
			priv->tkey_present = false;
		}
	}

	return 0;
}

static int logic_start(struct udevice *dev)
{
	struct logic_priv *priv = dev_get_priv(dev);
	int ret;

	if (priv->opt_persist_state) {
		/* read in our state */
		ret = bc_state_load(priv->state);
		if (ret)
			log_warning("Cannot load state, starting fresh (err=%dE)\n", ret);
		else
			priv->state_loaded = true;
	}

	ret = bc_ui_show(priv->ui);
	if (ret) {
		log_err("Cannot show display (err=%dE)\n", ret);
		return log_msg_ret("bds", ret);
	}

	priv->start_time = get_timer(0);
	if (priv->opt_autoboot) {
		priv->next_countdown = COUNTDOWN_INTERVAL_MS;
		priv->autoboot_remain_s = priv->opt_timeout;
		priv->autoboot_active = true;
	}

	if (priv->opt_default_os)
		bc_state_read_str(priv->state, "default", &priv->default_os);

	if (priv->opt_measure) {
		ret = bc_measure_start(priv->meas);
		if (ret)
			return log_msg_ret("pme", ret);
	}

	/* start scanning for OSes */
	bc_oslist_setup_iter(&priv->iter);
	priv->scanning = true;

	return 0;
}

/**
 * prepare_for_boot() - Get ready to boot an OS
 *
 * Intended to include at least:
 *   - A/B/recovery logic
 *   - persist the state
 *   - devicetree fix-up
 *   - measure images
 *
 * @dev: Bootctrl logic device
 * @osinfo: OS to boot
 * Return: 0 if OK, -ve on error
 */
static int prepare_for_boot(struct udevice *dev, struct osinfo *osinfo)
{
	struct logic_priv *priv = dev_get_priv(dev);
	int ret;

	if (priv->opt_track_success) {
		ret = bc_state_write_bool(priv->state, "recordfail", true);
		if (ret)
			log_warning("Cannot set up recordfail (err=%dE)\n",
				    ret);
	}

	if (priv->opt_persist_state) {
		ret = bc_state_save(priv->state);
		if (ret)
			log_warning("Cannot save state (err=%dE)\n", ret);
		else
			priv->state_saved = true;
	}

	/* devicetree fix-ups go here */

	/* measure loaded images */
	if (priv->opt_measure) {
		struct alist result;

		ret = bc_measure_process(priv->meas, osinfo, &result);
		if (ret)
			return log_msg_ret("pbp", ret);
		show_measures(&result);

		/* TODO: pass these measurements on to OS */
	}

	return 0;
}

/**
 * read_images() - Read all the images needed to boot an OS
 *
 * @dev: Bootctrl logic device
 * @osinfo: OS we intend to boot
 * Return: 0 if OK, -ve on error
 */
static int read_images(struct udevice *dev, struct osinfo *osinfo)
{
	struct bootflow *bflow = &osinfo->bflow;
	int ret;

	ret = bootflow_read_all(bflow);
	if (ret)
		return log_msg_ret("rea", ret);
	log_debug("Images read: %d\n", bflow->images.count);

	return 0;
}

/**
 * show_unlock_error() - Display error message and update UI state
 *
 * Helper function to show an error message, display error state,
 * and hide the pass prompt.
 *
 * @priv: Logic private data
 * @seq: Sequence number of the selected OS
 * @msg: Error message to display
 * Return: 0 if OK, -ve on error
 */
static int show_unlock_error(struct logic_priv *priv, int seq, const char *msg)
{
	int ret;

	ret = bc_ui_set_pass_msg(priv->ui, seq, msg);
	if (ret && ret != -ENOSYS)
		return log_msg_ret("sem", ret);
	ret = bc_ui_show_pass_msg(priv->ui, seq, true);
	if (ret && ret != -ENOSYS)
		return log_msg_ret("see", ret);
	ret = bc_ui_show_pass(priv->ui, seq, false);
	if (ret)
		return log_msg_ret("hsp", ret);

	return 0;
}

/**
 * start_tkey_load() - Start TKey app loading
 *
 * Starts the app loading process. Expects TKey device to be already probed
 * and present.
 *
 * @dev: Logic device
 * @pass: User passphrase to use as USS
 * Return: 0 on success or if needs replug (state set appropriately),
 *         -ve on error
 */
static int start_tkey_load(struct udevice *dev, const char *pass)
{
	struct logic_priv *priv = dev_get_priv(dev);
	int ret;

	ret = tkey_load_start(&priv->tkey_load_ctx, priv->tkey,
			      (const u8 *)__signer_1_0_0_begin,
			      TKEY_SIGNER_SIZE, (const u8 *)pass, strlen(pass));
	if (ret)
		return ret;
	log_debug("Started TKey app loading (%zx bytes)\n", TKEY_SIGNER_SIZE);

	return 0;
}

/**
 * derive_tkey_disk_key() - Derive disk encryption key from TKey public key
 *
 * Gets the public key from the TKey and derives the disk encryption key
 * using SHA256. Must match the Python tkey-fde-key.py implementation.
 *
 * @dev: Logic device
 * Return: 0 on success, -ve on error
 */
static int derive_tkey_disk_key(struct udevice *dev)
{
	struct logic_priv *priv = dev_get_priv(dev);
	u8 pubkey[TKEY_PUBKEY_SIZE];
	char pubkey_hex[TKEY_PUBKEY_SIZE * 2 + 1];
	sha256_context ctx;
	int ret;

	/* Get public key from the loaded app */
	ret = tkey_get_pubkey(priv->tkey, pubkey);
	if (ret) {
		log_warning("Failed to get TKey public key (err=%dE)\n", ret);
		return ret;
	}

	/*
	 * Derive disk encryption key from public key using SHA256
	 * Must match Python tkey-fde-key.py implementation which does:
	 * hashlib.sha256(pubkey.encode()).digest()
	 *
	 * This converts the binary public key to hex string,
	 * then hashes the string bytes.
	 */
	bin2hex(pubkey_hex, pubkey, TKEY_PUBKEY_SIZE);

	sha256_starts(&ctx);
	sha256_update(&ctx, (const u8 *)pubkey_hex, TKEY_PUBKEY_SIZE * 2);
	sha256_finish(&ctx, priv->tkey_disk_key);

	log_info("TKey disk key derived successfully\n");

	return 0;
}

/**
 * perform_tkey_unlock() - Perform TKey-based LUKS unlock
 *
 * This function performs LUKS unlock using the TKey-derived key as binary
 * passphrase material. Expects TKey to be already loaded and key derived.
 *
 * @dev: Logic device
 * @os: OS information containing the encrypted bootflow
 * @seq: Sequence number of the selected OS
 * @master_key: Buffer to store the unlocked master key
 * @key_size: Pointer to key size (input: buffer size, output: actual size)
 * Return: 0 if unlock succeeded, -ENOENT if there is no encrypted partition,
 * other -ve on other error
 */
static int perform_tkey_unlock(struct udevice *dev, struct osinfo *os, int seq,
			       u8 *master_key, u32 *key_sizep)
{
	struct logic_priv *priv = dev_get_priv(dev);
	struct disk_partition pinfo;
	int ret;

	/* TKey key should already be derived at this point */
	assert(priv->ustate == UNS_TKEY_UNLOCK);

	/* Get partition info for the encrypted partition (next after boot) */
	ret = part_get_info(dev_get_uclass_plat(os->bflow.blk),
			    os->bflow.part + 1, &pinfo);
	if (ret) {
		log_warning("Failed to get partition info (err=%dE)\n", ret);
		return -ENOENT;
	}

	/*
	 * Use TKey-derived key as binary passphrase input to LUKS KDF
	 * The key is treated as binary passphrase material that will be
	 * processed by PBKDF2/Argon2 just like a text passphrase would be.
	 * This matches how cryptsetup --key-file works.
	 */
	log_info("Using LUKS unlock with binary passphrase\n");
	ret = luks_unlock(os->bflow.blk, &pinfo, priv->tkey_disk_key,
			  TKEY_DISK_KEY_SIZE, false, master_key, key_sizep);
	if (ret)
		return log_msg_ret("htu", ret);

	return 0;
}

/**
 * handle_idle() - Set up the passphrase prompt UI
 *
 * This handles the UNS_IDLE state, showing the passphrase prompt and
 * transitioning to UNS_WAITING_PASS.
 *
 * @dev: Logic device
 * @seq: Sequence number of the selected OS
 * Return: 0 on success, -ve on error
 */
static int handle_idle(struct udevice *dev, int seq)
{
	struct logic_priv *priv = dev_get_priv(dev);
	int ret;

	/* show passphrase prompt and hide any error */
	ret = bc_ui_show_pass_msg(priv->ui, seq, false);
	if (ret && ret != -ENOSYS)
		return log_msg_ret("hse", ret);
	ret = bc_ui_show_pass(priv->ui, seq, true);
	if (ret)
		return log_msg_ret("lsp", ret);
	priv->ustate = UNS_WAITING_PASS;
	priv->selected_seq = seq;
	priv->refresh = true;

	return 0;
}

/**
 * handle_waiting_pass() - Handle waiting for passphrase entry
 *
 * This handles the UNS_WAITING_PASS state, getting the passphrase and
 * showing the "Unlocking..." message, then transitioning to either
 * UNS_UNLOCK_NORMAL or UNS_TKEY_START based on configuration.
 *
 * @dev: Logic device
 * @seq: Sequence number of the selected OS
 * Return: 0 on success, -ve on error
 */
static int handle_waiting_pass(struct udevice *dev, int seq)
{
	struct logic_priv *priv = dev_get_priv(dev);
	const char *pass;
	int ret;

	/* Get pass and show "Unlocking..." message */
	ret = bc_ui_get_pass(priv->ui, seq, &pass);
	if (ret) {
		log_warning("Failed to get pass (err=%dE)\n", ret);
		priv->ustate = UNS_IDLE;
		return -EAGAIN;  /* Return to menu */
	}

	/* Show "Unlocking..." message */
	ret = bc_ui_set_pass_msg(priv->ui, seq, "Unlocking...");
	if (ret && ret != -ENOSYS)
		return log_msg_ret("spu", ret);
	ret = bc_ui_show_pass_msg(priv->ui, seq, true);
	if (ret && ret != -ENOSYS)
		return log_msg_ret("ssu", ret);
	ret = bc_ui_show_pass(priv->ui, seq, false);
	if (ret)
		return log_msg_ret("hsp", ret);

	/* Select unlock path based on TKey option */
	if (priv->opt_tkey)
		priv->ustate = UNS_TKEY_START;
	else
		priv->ustate = UNS_UNLOCK_NORMAL;
	priv->refresh = true;

	return 0;
}

/**
 * handle_unlock_normal() - Perform normal LUKS unlock with direct passphrase
 *
 * This handles the UNS_UNLOCK_NORMAL state, performing LUKS unlock
 * with direct passphrase.
 *
 * @dev: Logic device
 * @os: OS information
 * @seq: Sequence number of the selected OS
 * Return: 0 on normal operation, -ve on error
 */
static int handle_unlock_normal(struct udevice *dev, struct osinfo *os, int seq)
{
	struct logic_priv *priv = dev_get_priv(dev);
	struct disk_partition pinfo;
	u8 master_key[128];
	u32 key_size = sizeof(master_key);
	const char *pass;
	int ret;

	/* Get pass for direct LUKS unlock */
	ret = bc_ui_get_pass(priv->ui, seq, &pass);
	if (ret) {
		log_warning("Failed to get pass (err=%dE)\n", ret);
		priv->ustate = UNS_IDLE;
		return 0;
	}

	/* Get partition info for the encrypted partition (next after boot) */
	ret = part_get_info(dev_get_uclass_plat(os->bflow.blk),
			    os->bflow.part + 1, &pinfo);
	if (ret) {
		log_warning("Failed to get partition info (err=%dE)\n", ret);
		priv->ustate = UNS_IDLE;
		return 0;
	}

	/* Try to unlock with the pass */
	ret = luks_unlock(os->bflow.blk, &pinfo, (const u8 *)pass, strlen(pass),
			  false, master_key, &key_size);

	/* Store result and transition to result handling state */
	priv->unlock_result = ret;
	priv->ustate = UNS_UNLOCK_RESULT;
	priv->refresh = true;

	return 0;
}

/**
 * handle_tkey_wait_remove() - Handle TKey removal wait state
 *
 * This handles the UNS_TKEY_WAIT_REMOVE state, waiting for TKey to be
 * physically removed and then transitioning to UNS_TKEY_WAIT_INSERT.
 *
 * @dev: Logic device
 * @seq: Sequence number of the selected OS
 * Return: 0 on normal operation, -ve on error
 */
static int handle_tkey_wait_remove(struct udevice *dev, int seq)
{
	struct logic_priv *priv = dev_get_priv(dev);
	int ret;

	/* Check if TKey is still present by checking app mode */
	ret = tkey_in_app_mode(priv->tkey);
	if (ret < 0) {
		/* TKey removed (error accessing device) */
		log_debug("TKey removed, cleaning up device\n");
		device_remove(priv->tkey, DM_REMOVE_NORMAL);
		priv->tkey_present = false;
		log_debug("TKey removed, ready for next attempt\n");

		/* Show replug message */
		ret = show_unlock_error(priv, seq, "Please insert TKey");
		if (ret)
			return log_msg_ret("rpe", ret);
		priv->refresh = true;
		priv->ustate = UNS_TKEY_WAIT_INSERT;

		return 0;
	}

	return 0;
}

/**
 * handle_tkey_start() - Handle TKey unlock start
 *
 * Checks if TKey device is available and transitions to wait for insert state.
 *
 * @dev: Logic device
 * @seq: Sequence number of the selected OS
 * Return: 0 on normal operation, -ve on error
 */
static int handle_tkey_start(struct udevice *dev, int seq)
{
	struct logic_priv *priv = dev_get_priv(dev);

	/* Check if TKey device is available */
	if (!priv->tkey) {
		log_err("TKey device not found\n");
		show_unlock_error(priv, seq, "TKey not found");
		return -ENODEV;
	}
	priv->ustate = UNS_TKEY_WAIT_INSERT;

	return 0;
}

/**
 * handle_tkey_wait_insert() - Handle TKey insertion wait state
 *
 * This handles the UNS_TKEY_WAIT_INSERT state, probing for TKey device
 * and transitioning to UNS_TKEY_INSERTED when found.
 *
 * @dev: Logic device
 * @seq: Sequence number of the selected OS
 * Return: -EAGAIN to continue waiting or transition to next state
 */
static int handle_tkey_wait_insert(struct udevice *dev, int seq)
{
	struct logic_priv *priv = dev_get_priv(dev);
	int ret;

	log_debug("Probing TKey device\n");
	ret = device_probe(priv->tkey);
	if (ret) {
		/* Probe failed - device not yet inserted */
		log_debug("TKey not inserted yet, waiting\n");
		priv->ustate = UNS_TKEY_WAIT_INSERT;
		return 0;
	}
	/* Probe succeeded - device is present */
	log_debug("TKey probed successfully\n");
	priv->tkey_present = true;
	priv->ustate = UNS_TKEY_INSERTED;

	return 0;
}

/**
 * handle_tkey_inserted() - Handle TKey inserted state
 *
 * This handles the UNS_TKEY_INSERTED state, starting the TKey app loading
 * process with the user's passphrase.
 *
 * @dev: Logic device
 * @seq: Sequence number of the selected OS
 * Return: 0 on normal operation, -ve on error
 */
static int handle_tkey_inserted(struct udevice *dev, int seq)
{
	struct logic_priv *priv = dev_get_priv(dev);
	const char *pass;
	int ret;

	/* Get passphrase for TKey derivation */
	ret = bc_ui_get_pass(priv->ui, seq, &pass);
	if (ret) {
		log_warning("Failed to get pass (err=%dE)\n", ret);
		priv->ustate = UNS_IDLE;
		return 0;  /* Return to menu */
	}

	/* Start loading TKey app with USS */
	ret = start_tkey_load(dev, pass);
	if (ret == -ENOTSUPP) {
		/* TKey in app mode, needs to be replugged */
		log_debug("TKey not in firmware mode, needs replug\n");
		priv->ustate = UNS_TKEY_WAIT_REMOVE;

		/* Show replug message */
		ret = show_unlock_error(priv, seq, "Please remove TKey");
		if (ret)
			return log_msg_ret("rpe", ret);
		priv->refresh = true;
		return 0;
	} else if (ret) {
		log_warning("Failed to start TKey app load (err=%dE)\n", ret);
		return ret;
	}

	priv->ustate = UNS_TKEY_LOADING;

	return 0;
}

/**
 * handle_tkey_loading() - Handle TKey app loading state
 *
 * This handles the UNS_TKEY_LOADING state, sending blocks of the TKey app
 * and transitioning to UNS_TKEY_READY when complete.
 *
 * @dev: Logic device
 * @seq: Sequence number of the selected OS
 * Return: 0 on normal operation, -ve on error
 */
static int handle_tkey_loading(struct udevice *dev, int seq)
{
	struct logic_priv *priv = dev_get_priv(dev);
	int ret;

	/* Send 1 block per poll to keep the UI responsive */
	ret = tkey_load_next(&priv->tkey_load_ctx, 1);
	if (ret == -EAGAIN) {
		char msg[64];
		int percent;

		/* More blocks to send */
		log_debug("TKey loading: %u/%u bytes\n",
			  priv->tkey_load_ctx.offset,
			  priv->tkey_load_ctx.app_size);

		/* Show loading progress - round up so 100% will show */
		percent = ((priv->tkey_load_ctx.offset + 1) * 100 +
				priv->tkey_load_ctx.app_size - 1) /
				priv->tkey_load_ctx.app_size;
		if (percent > 100)
			percent = 100;
		snprintf(msg, sizeof(msg), "Preparing TKey... %d%%", percent);
		bc_ui_set_pass_msg(priv->ui, seq, msg);
		priv->refresh = true;
		return 0;
	}

	if (ret) {
		log_warning("Failed to load TKey app (err=%dE)\n", ret);
		priv->ustate = UNS_TKEY_START;
		return ret;
	}

	/* Loading complete, now derive disk key */
	log_info("TKey app loaded successfully, deriving disk key\n");
	priv->ustate = UNS_TKEY_READY;

	return 0;
}

/**
 * handle_tkey_ready() - Handle TKey ready state
 *
 * This handles the UNS_TKEY_READY state, deriving the disk encryption key
 * from the TKey public key and transitioning to IN_PROGRESS state.
 *
 * @dev: Logic device
 * @seq: Sequence number of the selected OS
 * Return: 0 on normal operation, -ve on error
 */
static int handle_tkey_ready(struct udevice *dev, int seq)
{
	struct logic_priv *priv = dev_get_priv(dev);
	int ret;

	/* Derive disk encryption key from TKey public key */
	ret = derive_tkey_disk_key(dev);
	if (ret)
		return ret;
	bc_ui_set_pass_msg(priv->ui, seq, "Unlocking...");
	priv->refresh = true;

	/* Key derived, start unlock */
	priv->ustate = UNS_TKEY_UNLOCK;

	return 0;
}

/**
 * handle_tkey_unlock() - Handle TKey-based LUKS unlock state
 *
 * This handles the UNS_TKEY_UNLOCK state, performing LUKS unlock with
 * TKey-derived key and transitioning to UNS_UNLOCK_RESULT.
 *
 * @dev: Logic device
 * @os: OS information
 * @seq: Sequence number of the selected OS
 * Return: 0 on normal operation, -ve on error
 */
static int handle_tkey_unlock(struct udevice *dev, struct osinfo *os, int seq)
{
	struct logic_priv *priv = dev_get_priv(dev);
	u8 master_key[128];
	u32 key_size = sizeof(master_key);
	int ret;

	ret = perform_tkey_unlock(dev, os, seq, master_key, &key_size);

	/* Store result and transition to result handling state */
	priv->unlock_result = ret;
	priv->ustate = UNS_UNLOCK_RESULT;
	priv->refresh = true;

	return 0;
}

/**
 * handle_unlock_result() - Handle the result of unlock operation
 *
 * Processes unlock result, showing either error or success message.
 * On error, shows "Incorrect passphrase" and transitions to UNS_BAD_PASS.
 * On success, shows "Unlock successful" and transitions to UNS_OK.
 *
 * @priv: Logic private data
 * @seq: Sequence number of the selected OS
 * @unlock_ret: Return value from unlock operation
 * Return: -EAGAIN always (to wait for next poll)
 */
static int handle_unlock_result(struct logic_priv *priv, int seq,
				int unlock_ret)
{
	int ret;

	if (unlock_ret) {
		log_warning("Failed to unlock LUKS partition (err=%dE)\n",
			    unlock_ret);

		/* Set and show error message, hide pass prompt */
		ret = show_unlock_error(priv, seq, "Incorrect passphrase");
		if (ret)
			return log_msg_ret("ipe", ret);
		/* Display error for 5 seconds before allowing retry */
		priv->time_error = get_timer(0);
		priv->ustate = UNS_BAD_PASS;
		priv->refresh = true;
		return 0;
	}

	log_info("LUKS partition unlocked successfully\n");
	/* Set and show success message briefly, hide pass prompt */
	ret = show_unlock_error(priv, seq, "Unlock successful");
	if (ret)
		return log_msg_ret("suc", ret);
	/* Show success message for one poll cycle, then boot */
	priv->ustate = UNS_OK;
	priv->refresh = true;
	/* TODO: Create blkmap device for decrypted access */

	return 0;
}

static int handle_encrypted_tkey(struct udevice *dev, struct osinfo *os,
				 int seq)
{
	struct logic_priv *priv = dev_get_priv(dev);

	if (!IS_ENABLED(CONFIG_TKEY))
		return -ENOSYS;

	switch (priv->ustate) {
	case UNS_TKEY_WAIT_REMOVE:
		return handle_tkey_wait_remove(dev, seq);
	case UNS_TKEY_START:
		return handle_tkey_start(dev, seq);
	case UNS_TKEY_WAIT_INSERT:
		return handle_tkey_wait_insert(dev, seq);
	case UNS_TKEY_INSERTED:
		return handle_tkey_inserted(dev, seq);
	case UNS_TKEY_LOADING:
		return handle_tkey_loading(dev, seq);
	case UNS_TKEY_READY:
		return handle_tkey_ready(dev, seq);
	case UNS_TKEY_UNLOCK:
		return handle_tkey_unlock(dev, os, seq);
	default:
		return -EINVAL;
	}
}

static int handle_encrypted(struct udevice *dev, struct osinfo *os, int seq)
{
	struct logic_priv *priv = dev_get_priv(dev);

	switch (priv->ustate) {
	case UNS_IDLE:
		return handle_idle(dev, seq);
	case UNS_WAITING_PASS:
		return handle_waiting_pass(dev, seq);
	case UNS_UNLOCK_NORMAL:
		return handle_unlock_normal(dev, os, seq);
	case UNS_BAD_PASS:
	case UNS_OK:
		/* These states shouldn't reach here in normal flow */
		return -EINVAL;
	case UNS_UNLOCK_RESULT:
		return handle_unlock_result(priv, seq, priv->unlock_result);
	default:
		return handle_encrypted_tkey(dev, os, seq);
	}
}

static int logic_poll(struct udevice *dev)
{
	struct logic_priv *priv = dev_get_priv(dev);
	struct osinfo info;
	bool selected;
	int ret, seq;

	/* scan for the next OS, if any */
	if (priv->scanning) {
		ret = bc_oslist_next(priv->oslist, &priv->iter, &info);
		if (!ret) {
			ret = bc_ui_add(priv->ui, &info);
			if (ret)
				return log_msg_ret("bda", ret);
			priv->refresh = true;
		} else {
			/* No more OSes from this driver, try the next */
			ret = uclass_next_device_err(&priv->oslist);
			if (ret)
				priv->scanning = false;
			else
				memset(&priv->iter, '\0',
				       sizeof(struct oslist_iter));
		}
	}

	/* if unlock succeeded - show a message and boot on the next poll */
	if (priv->ustate == UNS_OK) {
		/* Success message is set, prepare to boot after rendering */
		priv->ustate = UNS_IDLE;
		priv->ready_to_boot = true;
		priv->refresh = true;
	}

	/* Check if error message display timeout has expired */
	if (priv->ustate == UNS_BAD_PASS && priv->time_error &&
	    get_timer(priv->time_error) > ERROR_TIMEOUT_MS) {
		/* Hide error message and allow retry */
		ret = bc_ui_show_pass_msg(priv->ui, priv->selected_seq, false);
		if (ret && ret != -ENOSYS)
			return log_msg_ret("hse", ret);
		priv->time_error = 0;
		priv->ustate = UNS_IDLE;
		priv->refresh = true;
	}

	if (priv->autoboot_active &&
	    get_timer(priv->start_time) > priv->next_countdown) {
		ulong secs = get_timer(priv->start_time) / 1000;

		priv->autoboot_remain_s = secs >= priv->opt_timeout ? 0 :
			max(priv->opt_timeout - secs, 0ul);
		priv->next_countdown += COUNTDOWN_INTERVAL_MS;
		priv->refresh = true;
	}

	if (!priv->opt_slow_refresh || priv->refresh) {
		ret = bc_ui_render(priv->ui);
		if (ret)
			return log_msg_ret("bdr", ret);
		priv->refresh = false;
	}

	ret = bc_ui_poll(priv->ui, &seq, &selected);
	if (ret < 0)
		return log_msg_ret("bdo", ret);
	else if (ret)
		priv->refresh = true;

	/* Ignore menu selection while displaying error message */
	if (selected && priv->ustate == UNS_BAD_PASS)
		selected = false;

	if (!selected && priv->autoboot_active && !priv->autoboot_remain_s &&
	    seq >= 0) {
		log_info("Selecting %d due to timeout\n", seq);
		selected = true;
	}

	/* If ready to unlock, trigger selection to continue unlock process */
	if (!selected && priv->ustate != UNS_WAITING_PASS &&
	    priv->ustate != UNS_IDLE && priv->ustate != UNS_BAD_PASS) {
		seq = priv->selected_seq;
		selected = true;
		log_debug("Continuing unlock for seq %d\n", seq);
	}

	if (selected) {
		struct osinfo *os;

		os = alist_getw(&priv->osinfo, seq, struct osinfo);
		if (!os)
			return log_msg_ret("gos", -ENOENT);

		/* If encrypted, handle pass entry and unlock */
		if (IS_ENABLED(CONFIG_BLK_LUKS) &&
		    (os->bflow.flags & BOOTFLOWF_ENCRYPTED)) {
			ret = handle_encrypted(dev, os, seq);
			if (ret)
				return ret;
		}
		priv->ready_to_boot = false;
		priv->selected_seq = seq;
	}

	if (priv->ready_to_boot) {
		struct osinfo *os;

		seq = priv->selected_seq;
		os = alist_getw(&priv->osinfo, seq, struct osinfo);
		if (!os)
			return log_msg_ret("gbo", -ENOENT);
		log_info("Selected %d: %s\n", seq, os->bflow.os_name);

		priv->ready_to_boot = false;
		/*
		 * try to read the images first; some methods don't support
		 * this
		 */
		ret = read_images(dev, os);
		if (ret && ret != -ENOSYS)
			return log_msg_ret("lri", ret);
		ret = prepare_for_boot(dev, os);
		if (ret)
			return log_msg_ret("lpb", ret);

		/* boot OS */
		ret = bootflow_boot(&os->bflow);
		if (ret)
			log_warning("Boot failed (err=%dE)\n", ret);

		return -ESHUTDOWN;
	}

	return 0;
}

static int logic_of_to_plat(struct udevice *dev)
{
	struct logic_priv *priv = dev_get_priv(dev);

	ofnode node = ofnode_find_subnode(dev_ofnode(dev), "options");

	priv->opt_persist_state = ofnode_read_bool(node, "persist-state");
	priv->opt_default_os = ofnode_read_bool(node, "default-os");
	ofnode_read_u32(node, "timeout", &priv->opt_timeout);
	priv->opt_skip_timeout = ofnode_read_bool(node,
						  "skip-timeout-on-success");
	priv->opt_track_success = ofnode_read_bool(node, "track-success");
	priv->opt_labels = ofnode_read_string(node, "labels");
	priv->opt_autoboot = ofnode_read_bool(node, "autoboot");
	priv->opt_measure = ofnode_read_bool(node, "measure");
	priv->opt_slow_refresh = ofnode_read_bool(node, "slow-refresh");
	priv->opt_tkey = ofnode_read_bool(node, "tkey");

	return 0;
}

static int logic_probe(struct udevice *dev)
{
	struct logic_priv *priv = dev_get_priv(dev);

	alist_init_struct(&priv->osinfo, struct osinfo);

	return 0;
}

static struct bc_logic_ops ops = {
	.prepare	= logic_prepare,
	.start		= logic_start,
	.poll		= logic_poll,
};

static const struct udevice_id logic_ids[] = {
	{ .compatible = "bootctl,ubuntu-desktop" },
	{ .compatible = "bootctl,logic" },
	{ }
};

U_BOOT_DRIVER(bc_logic) = {
	.name		= "bc_logic",
	.id		= UCLASS_BOOTCTL,
	.of_match	= logic_ids,
	.bind		= logic_bind,
	.ops		= &ops,
	.of_to_plat	= logic_of_to_plat,
	.probe		= logic_probe,
	.priv_auto	= sizeof(struct logic_priv),
};
