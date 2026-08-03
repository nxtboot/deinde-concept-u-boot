// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for bootctl
 *
 * For now this is just samples, showing how the different functions can be
 * tested
 *
 * Copyright 2025 Canonical Ltd
 * Written by Simon Glass <simon.glass@canonical.com>
 */

#include <stdbool.h>
#include <bootctl.h>
#include <bootflow.h>
#include <bootmeth.h>
#include <bootstd.h>
#include <dm.h>
#include <expo.h>
#include <menu.h>
#include <mouse.h>
#include <os.h>
#include <tkey.h>
#include "bootctl_common.h"
#include <bootctl/logic.h>
#include <bootctl/measure.h>
#include <bootctl/oslist.h>
#include <bootctl/state.h>
#include <bootctl/ui.h>
#include <dm/device-internal.h>
#include <dm/lists.h>
#include <test/ut.h>
#include <test/video.h>
#include "../bootstd_common.h"
#include "../../../boot/bootflow_internal.h"
#include "../../../boot/scene_internal.h"
#include "../bootstd_common.h"
#include "../expo_common.h"
//
/* test that expected devices are available and can be probed */
static int bootctl_base(struct unit_test_state *uts)
{
	struct udevice *dev;

	ut_assertok(bootctl_get_dev(UCLASS_BOOTCTL_UI, &dev));
	ut_asserteq_str("ui-multi", dev->name);

	ut_assertok(bootctl_get_dev(UCLASS_BOOTCTL_OSLIST, &dev));
	ut_asserteq_str("oslist-extlinux", dev->name);

	ut_assertok(bootctl_get_dev(UCLASS_BOOTCTL_STATE, &dev));
	ut_asserteq_str("state", dev->name);

	return 0;
}
BOOTCTL_TEST(bootctl_base, UTF_DM | UTF_SCAN_FDT);

/* test finding an OS */
static int bootctl_oslist(struct unit_test_state *uts)
{
	struct oslist_iter iter;
	struct osinfo info;
	struct bootflow *bflow = &info.bflow;
	struct udevice *dev;

	ut_assertok(bootctl_get_dev(UCLASS_BOOTCTL_OSLIST, &dev));
	ut_asserteq_str("oslist-extlinux", dev->name);

	/* initially we should only see Fedora */
	bc_oslist_setup_iter(&iter);
	ut_assertok(bc_oslist_next(dev, &iter, &info));
	ut_asserteq_str("mmc1.bootdev.part_1", bflow->name);
	ut_asserteq_strn("Fedora-Workstation", bflow->os_name);

	ut_asserteq(-ENODEV, bc_oslist_next(dev, &iter, &info));

	return 0;
}
BOOTCTL_TEST(bootctl_oslist, UTF_DM | UTF_SCAN_FDT);

/* test finding OSes on mmc and usb */
static int bootctl_oslist_usb(struct unit_test_state *uts)
{
	struct oslist_iter iter;
	struct osinfo info;
	struct bootflow *bflow = &info.bflow;
	struct udevice *dev;

	test_set_skip_delays(true);
	bootstd_reset_usb();

	ut_assertok(bootctl_get_dev(UCLASS_BOOTCTL_OSLIST, &dev));
	ut_asserteq_str("oslist-extlinux", dev->name);

	/* include usb in the bootdev order */
	ut_assertok(bootdev_set_order("mmc usb"));

	bc_oslist_setup_iter(&iter);
	ut_assertok(bc_oslist_next(dev, &iter, &info));
	ut_asserteq_str("mmc1.bootdev.part_1", bflow->name);

	ut_assertok(bc_oslist_next(dev, &iter, &info));
	ut_asserteq_str("hub1.p4.usb_mass_storage.lun0.bootdev.part_1", bflow->name);

	/* second entry from flash3 (Ubuntu has two extlinux labels) */
	ut_assertok(bc_oslist_next(dev, &iter, &info));
	ut_asserteq_str("hub1.p4.usb_mass_storage.lun0.bootdev.part_1", bflow->name);

	ut_asserteq(-ENODEV, bc_oslist_next(dev, &iter, &info));

	return 0;
}
BOOTCTL_TEST(bootctl_oslist_usb, UTF_DM | UTF_SCAN_FDT);

/* test basic use of state */
static int bootctl_simple_state_base(struct unit_test_state *uts)
{
	struct udevice *dev;
	const char *sval;
	struct abuf buf;
	bool bval;
	long ival;

	ut_assertok(bootctl_get_dev(UCLASS_BOOTCTL_STATE, &dev));
	ut_assertok(bc_state_write_bool(dev, "fred", false));
	ut_assertok(bc_state_write_bool(dev, "mary", true));
	ut_assertok(bc_state_write_int(dev, "alex", 123));
	ut_assertok(bc_state_write_str(dev, "john", "abc"));

	ut_assertok(bc_state_read_bool(dev, "fred", &bval));
	ut_asserteq(false, bval);

	ut_assertok(bc_state_read_bool(dev, "mary", &bval));
	ut_asserteq(true, bval);

	ut_assertok(bc_state_read_int(dev, "alex", &ival));
	ut_asserteq(123, ival);

	ut_assertok(bc_state_read_str(dev, "john", &sval));
	ut_asserteq_str("abc", sval);

	/* check the buffer contents, including the nul terminator */
	ut_assertok(bc_state_save_to_buf(dev, &buf));
	ut_asserteq_str("fred=0\nmary=1\nalex=123\njohn=abc\n", buf.data);
	ut_asserteq(strlen("fred=0\nmary=1\nalex=123\njohn=abc\n") + 1,
		    buf.size);
	ut_asserteq(0, *((char *)buf.data + buf.size - 1));
	abuf_uninit(&buf);

	/* overwrite */
	ut_assertok(bc_state_write_str(dev, "fred", "def"));
	ut_assertok(bc_state_read_str(dev, "fred", &sval));
	ut_asserteq_str("def", sval);

	ut_assertok(bc_state_clear(dev));
	ut_asserteq(-ENOENT, bc_state_read_bool(dev, "fred", &bval));
	ut_asserteq(-ENOENT, bc_state_read_bool(dev, "mary", &bval));
	ut_asserteq(-ENOENT, bc_state_read_bool(dev, "john", &bval));
	ut_asserteq(-ENOENT, bc_state_read_bool(dev, "alex", &bval));

	return 0;
}
BOOTCTL_TEST(bootctl_simple_state_base, UTF_DM | UTF_SCAN_FDT);

/* test loading / saving state */
static int bootctl_simple_state_loadsave(struct unit_test_state *uts)
{
	struct udevice *dev;
	char *buf;
	int size;

	ut_assertok(bootctl_get_dev(UCLASS_BOOTCTL_STATE, &dev));
	ut_assertok(bc_state_write_bool(dev, "fred", false));
	ut_assertok(bc_state_write_bool(dev, "mary", true));
	ut_assertok(bc_state_save(dev));

	/* check the file contents, including the nul terminator */
	ut_assertok(os_read_file("bootctl.ini", (void **)&buf, &size));
	ut_asserteq_str("fred=0\nmary=1\n", buf);
	ut_asserteq(strlen("fred=0\nmary=1\n") + 1, size);
	ut_asserteq(0, buf[size - 1]);
	os_free(buf);

	ut_assertok(bc_state_load(dev));

	return 0;
}
BOOTCTL_TEST(bootctl_simple_state_loadsave, UTF_DM | UTF_SCAN_FDT);

/* test limits */
static int bootctl_simple_state_limits(struct unit_test_state *uts)
{
	struct udevice *dev;
	char long_key[32];	/* avoid using constants from impl */
	struct abuf buf;
	char *data;
	int ch;

	ut_assertok(bootctl_get_dev(UCLASS_BOOTCTL_STATE, &dev));

	/* cannot use NULL as a key or value */
	ut_asserteq(-EINVAL, bc_state_write_bool(dev, NULL, false));
	ut_asserteq(-EINVAL, bc_state_write_str(dev, "key", NULL));

	/* empty key and value */
	ut_asserteq(-EINVAL, bc_state_write_str(dev, "", "val"));
	ut_assertok(bc_state_write_str(dev, "empty", ""));

	/* no spaces allowed in a key */
	ut_asserteq(-EKEYREJECTED, bc_state_write_str(dev, "my key", "val"));

	/* check key characters */
	for (ch = 1; ch < 256; ch++) {
		char key[4] = "key";
		bool ok;

		ok = ch == '_' || (ch >= 'a' && ch <= 'z') ||
			(ch >= '0' && ch <= '9');

		key[1] = ch;
		printf("checking ch %x\n", ch);
		if (ok)
			ut_assertok(bc_state_write_str(dev, key, "val"));
		else
			ut_asserteq(-EKEYREJECTED, bc_state_write_str(dev, key, "val"));
	}

	/* key too long */
	strcpy(long_key, "1234567890123456789012345678901");
	ut_asserteq(-EKEYREJECTED, bc_state_write_str(dev, long_key, "val"));
	long_key[30] = '\0';
	ut_assertok(bc_state_write_str(dev, long_key, "val"));

	/* value too long */
	abuf_init(&buf);
	ut_asserteq(true, abuf_realloc(&buf, 0x1002));
	data = buf.data;
	memset(data, 'x', 0x1001);
	data[0x1001] = '\0';
	ut_asserteq(-E2BIG, bc_state_write_str(dev, "try", data));
	data[0x1000] = '\0';
	ut_assertok(bc_state_write_str(dev, "try", data));
	abuf_uninit(&buf);

	return 0;
}
BOOTCTL_TEST(bootctl_simple_state_limits, UTF_DM | UTF_SCAN_FDT);

/* test integers */
static int bootctl_simple_state_int(struct unit_test_state *uts)
{
	struct udevice *dev;
	long ival;

	ut_assertok(bootctl_get_dev(UCLASS_BOOTCTL_STATE, &dev));

	/* basic integers */
	ut_assertok(bc_state_write_int(dev, "val", 0));
	ut_assertok(bc_state_read_int(dev, "val", &ival));
	ut_asserteq(0, ival);

	ut_assertok(bc_state_write_int(dev, "val", 1));
	ut_assertok(bc_state_read_int(dev, "val", &ival));
	ut_asserteq(1, ival);

	ut_assertok(bc_state_write_int(dev, "val", -1));
	ut_assertok(bc_state_read_int(dev, "val", &ival));
	ut_asserteq(-1, ival);

	/* large ints */
	ut_assertok(bc_state_write_int(dev, "val", 0xffffffffl));
	ut_assertok(bc_state_read_int(dev, "val", &ival));
	ut_asserteq(0xffffffffl, ival);

	ut_assertok(bc_state_write_int(dev, "val", -0xffffffffl));
	ut_assertok(bc_state_read_int(dev, "val", &ival));
	ut_asserteq_64(-0xffffffffl, ival);

	ut_assertok(bc_state_write_int(dev, "val", 0x7fffffffffffffffll));
	ut_assertok(bc_state_read_int(dev, "val", &ival));
	ut_asserteq_64(0x7fffffffffffffffll, ival);

	ut_assertok(bc_state_write_int(dev, "val", -0x7fffffffffffffffll));
	ut_assertok(bc_state_read_int(dev, "val", &ival));
	ut_asserteq_64(-0x7fffffffffffffffll, ival);

	return 0;
}
BOOTCTL_TEST(bootctl_simple_state_int, UTF_DM | UTF_SCAN_FDT);

/* test measurement */
static int bootctl_simple_measure(struct unit_test_state *uts)
{
	struct bootflow_img *img[3];
	struct osinfo osinfo;
	struct bootflow *bflow = &osinfo.bflow;
	const struct measure_info *info;
	struct udevice *dev;
	struct alist result;

	ut_assertok(bootctl_get_dev(UCLASS_BOOTCTL_MEASURE, &dev));

	ut_assertok(bc_measure_start(dev));

	/* set up some data */
	memset(&osinfo, '\0', sizeof(struct osinfo));
	alist_init_struct(&bflow->images, struct bootflow_img);

	/* add a few images */
	img[0] = bootflow_img_add(bflow, "kernel",
				  (enum bootflow_img_t)IH_TYPE_KERNEL, 0,
				  0x100);
	ut_assertnonnull(img);
	img[1] = bootflow_img_add(bflow, "initrd",
				  (enum bootflow_img_t)IH_TYPE_RAMDISK, 0x100,
				  0x200);
	ut_assertnonnull(img);

	/* the fdt is missing so this should fail */
	ut_asserteq(-ENOENT, bc_measure_process(dev, &osinfo, &result));
	if (IS_ENABLED(CONFIG_LOGF_FUNC))
		ut_assert_nextline("      simple_process() Missing image 'flat_dt'");
	else
		ut_assert_nextline("Missing image 'flat_dt'");
	ut_assert_console_end();

	alist_uninit(&result);

	img[2] = bootflow_img_add(bflow, "fdt",
				  (enum bootflow_img_t)IH_TYPE_FLATDT, 0x300,
				  0x30);
	ut_assertok(bc_measure_process(dev, &osinfo, &result));

	/* check the result */
	ut_asserteq(3, result.count);
	info = alist_get(&result, 0, struct measure_info);
	ut_asserteq_ptr(img[0], info[0].img);
	ut_asserteq_ptr(img[1], info[1].img);
	ut_asserteq_ptr(img[2], info[2].img);

	/* TODO: We should also a) read out the TPM log and b) check TPM PCRs */

	ut_assertnonnull(img);

	return 0;
}
BOOTCTL_TEST(bootctl_simple_measure, UTF_DM | UTF_SCAN_FDT | UTF_CONSOLE);

/**
 * check_passphrase() - Test passphrase functionality for an encrypted item
 *
 * @uts: Test state
 * @ui_dev: UI device to test
 * @seq: Sequence number of the encrypted bootflow item
 * Return: 0 if OK, -ve on error
 */
static int check_passphrase(struct unit_test_state *uts,
			    struct udevice *ui_dev, int seq)
{
	struct bc_ui_priv *uc_priv = dev_get_uclass_priv(ui_dev);
	const char *retrieved_passphrase = NULL;
	struct scene_obj *label_obj, *edit_obj;
	struct scene_obj_textline *tline;
	struct scene *scn = uc_priv->scn;
	bool selected;
	int seq_out;

	/* Show passphrase for the specified item (this also opens it) */
	ut_assertok(bc_ui_show_pass(ui_dev, seq, true));
	ut_assertok(bc_ui_render(ui_dev));

	/* Verify passphrase textline and its child objects are now visible */
	tline = scene_obj_find(scn, ITEM_PASS + seq, SCENEOBJT_TEXTLINE);
	ut_assertnonnull(tline);
	ut_asserteq(false, tline->obj.flags & SCENEOF_HIDE);
	ut_assert(tline->obj.flags & SCENEOF_OPEN);

	/* Verify the scene's highlight is set to the passphrase textline */
	ut_asserteq(ITEM_PASS + seq, scn->highlight_id);

	label_obj = scene_obj_find(scn, ITEM_PASS_LABEL + seq, SCENEOBJT_NONE);
	ut_assertnonnull(label_obj);
	ut_asserteq(false, label_obj->flags & SCENEOF_HIDE);

	edit_obj = scene_obj_find(scn, ITEM_PASS_EDIT + seq, SCENEOBJT_NONE);
	ut_assertnonnull(edit_obj);
	ut_asserteq(false, edit_obj->flags & SCENEOF_HIDE);

	/* Type 't', 'e', 's', 't' - each poll processes one character */
	ut_asserteq(4, console_in_puts("test"));
	ut_assertok(bc_ui_poll(ui_dev, &seq_out, &selected));
	ut_asserteq_str("t", abuf_data(&tline->tin.buf));
	ut_assertok(bc_ui_poll(ui_dev, &seq_out, &selected));
	ut_asserteq_str("te", abuf_data(&tline->tin.buf));
	ut_assertok(bc_ui_poll(ui_dev, &seq_out, &selected));
	ut_asserteq_str("tes", abuf_data(&tline->tin.buf));
	ut_assertok(bc_ui_poll(ui_dev, &seq_out, &selected));
	ut_asserteq_str("test", abuf_data(&tline->tin.buf));

	/* Send backspace to remove one character */
	ut_asserteq(1, console_in_puts("\b"));
	ut_assertok(bc_ui_poll(ui_dev, &seq_out, &selected));
	ut_asserteq_str("tes", abuf_data(&tline->tin.buf));

	/* Re-add the 't' and verify */
	ut_asserteq(1, console_in_puts("t"));
	ut_assertok(bc_ui_poll(ui_dev, &seq_out, &selected));
	ut_asserteq_str("test", abuf_data(&tline->tin.buf));

	/* Send return key to submit - should close textline and select */
	ut_asserteq(1, console_in_puts("\n"));
	ut_assertok(bc_ui_poll(ui_dev, &seq_out, &selected));
	ut_assert(selected);
	ut_asserteq(seq, seq_out);

	/* Verify we can retrieve the passphrase */
	ut_assertok(bc_ui_get_pass(ui_dev, seq, &retrieved_passphrase));
	ut_assertnonnull(retrieved_passphrase);
	ut_asserteq_str("test", retrieved_passphrase);

	/*
	 * Verify the LUKS partition unlock would be attempted. In a real
	 * scenario, this would call luks_unlock(), but for the test we just
	 * verify the passphrase was correctly captured and the UI state
	 * indicates selection was made (which triggers the unlock logic)
	 */

	/* Test hiding the passphrase field */
	ut_assertok(bc_ui_show_pass(ui_dev, seq, false));
	ut_assertok(bc_ui_render(ui_dev));

	/* Verify all three objects are now hidden */
	ut_asserteq(true, tline->obj.flags & SCENEOF_HIDE);
	ut_asserteq(true, label_obj->flags & SCENEOF_HIDE);
	ut_asserteq(true, edit_obj->flags & SCENEOF_HIDE);

	return 0;
}

static int check_multiboot_ui(struct unit_test_state *uts,
			      struct bootstd_priv *std)
{
	struct udevice *oslist_dev, *ui_dev, *vid_dev;
	struct membuf buf1, buf2, buf3, buf4;
	char *data1, *data2, *data3, *data4;
	struct bc_ui_priv *uc_priv;
	struct udevice *logic_dev;
	struct logic_priv *lpriv;
	struct oslist_iter iter;
	struct osinfo info[2];
	int len;

	test_set_skip_delays(true);
	bootstd_reset_usb();

	/* get the oslist device and find two OSes */
	ut_assertok(bootctl_get_dev(UCLASS_BOOTCTL_OSLIST, &oslist_dev));
	ut_asserteq_str("oslist-extlinux", oslist_dev->name);

	bc_oslist_setup_iter(&iter);
	ut_assertok(bc_oslist_next(oslist_dev, &iter, &info[0]));
	ut_asserteq_str("mmc11.bootdev.part_1", info[0].bflow.name);

	/* skip the second mmc11 entry (Ubuntu has two extlinux labels) */
	ut_assertok(bc_oslist_next(oslist_dev, &iter, &info[1]));
	ut_asserteq_str("mmc11.bootdev.part_1", info[1].bflow.name);

	ut_assertok(bc_oslist_next(oslist_dev, &iter, &info[1]));
	ut_asserteq_str("hub1.p4.usb_mass_storage.lun0.bootdev.part_1",
			info[1].bflow.name);

	test_set_skip_delays(false);

	/* first use simple_ui as baseline */
	ut_assertok(uclass_get_device_by_name(UCLASS_BOOTCTL_UI, "ui-simple",
					      &ui_dev));
	ut_assertok(bc_ui_show(ui_dev));
	ut_assertok(bc_ui_add(ui_dev, &info[0]));
	ut_assertok(bc_ui_add(ui_dev, &info[1]));
	ut_assertok(bc_ui_render(ui_dev));
	ut_assertok(uclass_first_device_err(UCLASS_VIDEO, &vid_dev));
	ut_asserteq(21748, video_compress_fb(uts, vid_dev, false));

	/* dump the simple_ui expo - buf1 is golden for simple_ui */
	uc_priv = dev_get_uclass_priv(ui_dev);
	ut_assertok(membuf_new(&buf1, 4096));
	expo_dump(uc_priv->expo, &buf1);
	len = membuf_getraw(&buf1, -1, false, &data1);
	ut_assert(len > 0);
	if (_DEBUG)
		ut_assertok(os_write_file("simple_ui.txt", data1, len));

	/* clear out osinfo and bootflows before using ui2 */
	ut_assertok(bootctl_get_dev(UCLASS_BOOTCTL, &logic_dev));
	lpriv = dev_get_priv(logic_dev);
	alist_empty(&lpriv->osinfo);

	alist_empty(&std->bootflows);

	/* now use multiboot_ui - this is the initial multiboot state */
	ut_assertok(uclass_get_device_by_name(UCLASS_BOOTCTL_UI, "ui-multi",
					      &ui_dev));
	ut_assertok(bc_ui_show(ui_dev));
	ut_assertok(bc_ui_add(ui_dev, &info[0]));
	ut_assertok(bc_ui_add(ui_dev, &info[1]));
	ut_assertok(bc_ui_render(ui_dev));
	ut_asserteq(16324, video_compress_fb(uts, vid_dev, false));

	/* dump after render - buf2 is golden for multiboot_ui */
	uc_priv = dev_get_uclass_priv(ui_dev);
	ut_assertok(membuf_new(&buf2, 4096));
	expo_dump(uc_priv->expo, &buf2);
	len = membuf_getraw(&buf2, -1, false, &data2);
	ut_assert(len > 0);
	if (_DEBUG)
		ut_assertok(os_write_file("multiboot_ui.txt", data2, len));

	/* switch to simple_ui layout and check against buf1 */
	ut_assertok(bc_ui_switch_layout(ui_dev));
	ut_assertok(bc_ui_render(ui_dev));
	ut_asserteq(21748, video_compress_fb(uts, vid_dev, false));

	/* dump after switch to simple_ui - buf3 should match buf1 */
	ut_assertok(membuf_new(&buf3, 4096));
	expo_dump(uc_priv->expo, &buf3);
	len = membuf_getraw(&buf3, -1, false, &data3);
	ut_assert(len > 0);
	if (_DEBUG)
		ut_assertok(os_write_file("multiboot_ui_switched.txt", data3,
					  len));

	/* compare buf3 against buf1 (simple_ui golden) */
	if (strcmp(data1, data3)) {
		printf("Expo dumps differ after switch to simple_ui!\n");
		if (_DEBUG) {
			puts("simple_ui:\n");
			puts(data1);
			puts("multiboot_ui_switched:\n");
			puts(data3);
		}
	}

	/* switch back to multiboot UI style and check against buf2 */
	ut_assertok(bc_ui_switch_layout(ui_dev));
	ut_assertok(bc_ui_render(ui_dev));
	ut_asserteq(16324, video_compress_fb(uts, vid_dev, false));

	/* dump after switch back to multiboot - buf4 should match buf2 */
	ut_assertok(membuf_new(&buf4, 4096));
	expo_dump(uc_priv->expo, &buf4);
	len = membuf_getraw(&buf4, -1, false, &data4);
	ut_assert(len > 0);
	if (_DEBUG)
		ut_assertok(os_write_file("multiboot_ui_switched_back.txt",
					  data4, len));

	/* compare buf4 against buf2 (multiboot_ui golden) */
	if (strcmp(data2, data4)) {
		printf("Expo dumps differ after switch back to multiboot!\n");
		if (_DEBUG) {
			puts("multiboot_ui:\n");
			puts(data2);
			puts("multiboot_ui_switched_back:\n");
			puts(data4);
		}
	}

	/*
	 * Test passphrase functionality for mmc11 (item 0, which is encrypted)
	 */
	ut_assertok(check_passphrase(uts, ui_dev, 0));

	membuf_dispose(&buf1);
	membuf_dispose(&buf2);
	membuf_dispose(&buf3);
	membuf_dispose(&buf4);

	return 0;
}

/* test creating multiboot_ui with two OSes */
static int bootctl_multiboot_ui(struct unit_test_state *uts)
{
	static const char *order[3];
	struct bootstd_priv *std;
	const char **old_order;
	struct udevice *dev;
	ofnode root, node;
	int ret;

	order[0] = "mmc11";
	order[1] = "usb3";
	order[2] = NULL;

	/* Enable the requested mmc node since we need a second bootflow */
	root = oftree_root(oftree_default());
	node = ofnode_find_subnode(root, "mmc11");
	ut_assert(ofnode_valid(node));
	ut_assertok(lists_bind_fdt(gd->dm_root, node, &dev, NULL, false));

	/* Change the order to include the device */
	ut_assertok(bootstd_get_priv(&std));
	old_order = std->bootdev_order;
	std->bootdev_order = order;

	ret = check_multiboot_ui(uts, std);

	std->bootdev_order = old_order;
	ut_assertok(ret);

	return 0;
}
BOOTCTL_TEST(bootctl_multiboot_ui, UTF_DM | UTF_SCAN_FDT | UTF_CONSOLE);

/**
 * click_os() - Click on an OS in the bootctl UI
 *
 * @uts: Unit test state
 * @lpriv: Logic private data
 * @seq: Sequence number of the OS to click
 * Return: 0 if OK, -ve on error
 */
static int click_os(struct unit_test_state *uts, struct logic_priv *lpriv,
		    int seq)
{
	struct bc_ui_priv *uc_priv;
	struct scene_obj *obj;
	struct scene *scn;
	struct expo *exp;

	uc_priv = dev_get_uclass_priv(lpriv->ui);
	scn = uc_priv->scn;
	exp = uc_priv->expo;

	/* Get the position of ITEM_DESC + seq and queue a click there */
	obj = scene_obj_find(scn, ITEM_DESC + seq, SCENEOBJT_NONE);
	ut_assertnonnull(obj);
	/* Click halfway along the object, 5 pixels from the top */
	ut_assertok(mouse_queue_click_for_test(exp->mouse,
					       obj->bbox.x0 + (obj->bbox.x1 -
						       obj->bbox.x0) / 2,
					       obj->bbox.y0 + 5));

	return 0;
}

/**
 * prepare_tkey_test() - Prepare bootctl logic for TKey unlock testing
 *
 * This helper sets up the complete test environment including:
 * - Preparing the logic and finding bootflows
 * - Configuring TKey emulator with test pubkey
 * - Setting TKey to app mode to test replugging
 * - Starting the logic and polling to find OSes
 * - Verifying encrypted bootflows were found
 *
 * @uts: Unit test state
 * @logic: Bootctl logic device
 * @emul_out: Returns the TKey emulator device
 * @test_pubkey: Public key to configure in emulator
 * Return: 0 on success, -ve on error
 */
static int prepare_tkey_test(struct unit_test_state *uts,
			     struct udevice *logic,
			     struct udevice **emul_out,
			     const u8 *test_pubkey)
{
	struct logic_priv *lpriv = dev_get_priv(logic);
	struct udevice *emul;

	/*
	 * Prepare the logic. TKey device will be found automatically in
	 * tkey_poll() when needed (uses first device, which is tkey-emul)
	 */
	ut_assertok(bc_logic_prepare(logic));
	ut_assertnonnull(lpriv->ui);
	ut_assertnonnull(lpriv->oslist);

	/*
	 * Configure the emulator to return a pubkey that matches the test
	 * LUKS image. The test image was created with this specific TKey.
	 * Get the emulator device to configure it.
	 */
	ut_assertok(uclass_get_device_by_name(UCLASS_TKEY, "tkey-emul",
					      &emul));
	ut_assertok(tkey_emul_set_pubkey_for_test(emul, test_pubkey));

	/*
	 * Put TKey into app mode. This will force the unlock logic to
	 * request replugging the TKey.
	 */
	ut_assertok(tkey_emul_set_app_mode_for_test(emul, true));

	/* Start the logic */
	ut_assertok(bc_logic_start(logic));

	/*
	 * Override the TKey device to use the emulator. logic_start() finds
	 * the first device, but we want to use tkey-emul for testing.
	 */
	lpriv->tkey = emul;

	/* Poll twice to find both OSes (no delays, so completes quickly) */
	ut_assertok(bc_logic_poll(logic));
	ut_assertok(bc_logic_poll(logic));

	/* Verify both OSes were found */
	ut_asserteq(2, lpriv->osinfo.count);

	/* First OS should be mmc13 and should be marked as encrypted */
	ut_asserteq_str("mmc13.bootdev.part_1",
			alist_getw(&lpriv->osinfo, 0,
				   struct osinfo)->bflow.name);
	ut_assert(alist_getw(&lpriv->osinfo, 0, struct osinfo)->bflow.flags &
		  BOOTFLOWF_ENCRYPTED);

	/* Verify TKey is enabled (device will be found later in tkey_poll) */
	ut_assert(lpriv->opt_tkey);

	*emul_out = emul;
	return 0;
}

/**
 * try_tkey_unlock() - Try to unlock with TKey using a passphrase
 *
 * @uts: Unit test state
 * @logic: Logic device
 * @emul: TKey emulator device
 * @test_pubkey: Expected public key (or NULL to keep wrong key for failure
 * test)
 * @passphrase: Passphrase to enter
 * @load_iterations_out: Pointer to store load iteration count
 * Return: 0 if OK, -ve on error
 */
static int try_tkey_unlock(struct unit_test_state *uts, struct udevice *logic,
			   struct udevice *emul, const u8 *test_pubkey,
			   const char *passphrase, int *load_iterations_out)
{
	struct logic_priv *lpriv = dev_get_priv(logic);
	int load_iterations;
	int i;

	/* Verify passphrase is being requested */
	ut_asserteq(UNS_WAITING_PASS, lpriv->ustate);
	ut_asserteq(0, lpriv->selected_seq);

	/* Type the passphrase - each poll processes one character */
	ut_asserteq(strlen(passphrase), console_in_puts(passphrase));
	for (i = 0; i < strlen(passphrase); i++)
		ut_assertok(bc_logic_poll(logic));

	/* Press return to submit the passphrase */
	ut_asserteq(1, console_in_puts("\n"));

	/* Poll to process return - should transition to UNS_TKEY_START */
	ut_assertok(bc_logic_poll(logic));
	ut_asserteq(UNS_TKEY_START, lpriv->ustate);

	/* Poll - should transition to UNS_TKEY_WAIT_INSERT */
	ut_assertok(bc_logic_poll(logic));
	ut_asserteq(UNS_TKEY_WAIT_INSERT, lpriv->ustate);

	/* Poll - TKey should be detected, transition to UNS_TKEY_INSERTED */
	ut_assertok(bc_logic_poll(logic));
	ut_asserteq(UNS_TKEY_INSERTED, lpriv->ustate);

	/*
	 * Poll - TKey is in app mode, should request removal
	 * Transition to UNS_TKEY_WAIT_REMOVE
	 */
	ut_assertok(bc_logic_poll(logic));
	ut_asserteq(UNS_TKEY_WAIT_REMOVE, lpriv->ustate);

	/* Simulate TKey removal by disconnecting the emulator */
	ut_assertok(tkey_emul_set_connected_for_test(emul, false));

	/* Poll - should detect removal, transition to UNS_TKEY_WAIT_INSERT */
	ut_assertok(bc_logic_poll(logic));
	ut_asserteq(UNS_TKEY_WAIT_INSERT, lpriv->ustate);

	/* Simulate TKey reinsertion (reconnect the device) */
	ut_assertok(tkey_emul_set_connected_for_test(emul, true));

	/*
	 * Poll - TKey should be detected again, transition to
	 * UNS_TKEY_INSERTED
	 */
	ut_assertok(bc_logic_poll(logic));
	ut_asserteq(UNS_TKEY_INSERTED, lpriv->ustate);

	/*
	 * After reprobe, the emulator gets new priv data.
	 * Set the pubkey if provided (for success), or skip it (for failure)
	 */
	if (test_pubkey)
		ut_assertok(tkey_emul_set_pubkey_for_test(emul, test_pubkey));

	/* Poll - should start loading, transition to UNS_TKEY_LOADING */
	ut_assertok(bc_logic_poll(logic));
	ut_asserteq(UNS_TKEY_LOADING, lpriv->ustate);

	/* Poll while TKey app is loading */
	load_iterations = 0;
	while (lpriv->ustate == UNS_TKEY_LOADING) {
		ut_assertok(bc_logic_poll(logic));
		load_iterations++;
		/* Exact count: 28KB / 127 bytes */
		ut_assert(load_iterations <= 221);
	}

	/* Verify loading completed - should be in UNS_TKEY_READY */
	ut_asserteq(UNS_TKEY_READY, lpriv->ustate);
	ut_asserteq(221, load_iterations);

	if (load_iterations_out)
		*load_iterations_out = load_iterations;

	/* Poll - should derive key and transition to UNS_TKEY_UNLOCK */
	ut_assertok(bc_logic_poll(logic));
	ut_asserteq(UNS_TKEY_UNLOCK, lpriv->ustate);

	/* Poll - should perform unlock and transition to UNS_UNLOCK_RESULT */
	ut_assertok(bc_logic_poll(logic));
	ut_asserteq(UNS_UNLOCK_RESULT, lpriv->ustate);

	/* Poll - should process result */
	ut_assertok(bc_logic_poll(logic));

	return 0;
}

/* test TKey unlock with logic device - wrong then correct passphrase */
static int bootctl_logic_tkey(struct unit_test_state *uts)
{
	/* Correct pubkey matching emulator default - produces valid disk key */
	const u8 test_pubkey[32] = {
		0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57,
		0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f,
		0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57,
		0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f
	};
	/*
	 * Wrong pubkey - produces an invalid disk key for testing unlock
	 * failure
	 */
	const u8 wrong_pubkey[32] = {
		0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
		0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
		0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
		0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f
	};
	struct udevice *emul, *logic, *dev;
	struct logic_priv *lpriv;
	ofnode root, node;

	test_set_skip_delays(true);
	bootstd_reset_usb();

	/* Enable mmc13 device which has the TKey-encrypted partition */
	root = oftree_root(oftree_default());
	node = ofnode_find_subnode(root, "mmc13");
	ut_assert(ofnode_valid(node));
	ut_assertok(lists_bind_fdt(gd->dm_root, node, &dev, NULL, false));

	/* Get the logic device */
	ut_assertok(bootctl_get_dev(UCLASS_BOOTCTL, &logic));
	lpriv = dev_get_priv(logic);

	/* Enable TKey support and disable autoboot */
	lpriv->opt_tkey = true;
	lpriv->opt_autoboot = false;

	/* Set boot order to include mmc13 before prepare */
	lpriv->opt_labels = "mmc13 usb3";

	/* Prepare the test environment and verify encrypted bootflows found */
	ut_assertok(prepare_tkey_test(uts, logic, &emul, test_pubkey));

	/* Queue a click on the first OS (seq 0) to select it */
	ut_assertok(click_os(uts, lpriv, 0));

	/* Poll the logic - should process the click and ask for passphrase */
	ut_assertok(bc_logic_poll(logic));

	/*
	 * First, test wrong passphrase to verify UNS_BAD_PASS state.
	 * Use wrong_pubkey to simulate a TKey producing an invalid disk key.
	 */
	ut_assertok(try_tkey_unlock(uts, logic, emul, wrong_pubkey, "wrongpw",
				    NULL));

	/* Unlock should fail, transition to UNS_BAD_PASS */
	ut_asserteq(UNS_BAD_PASS, lpriv->ustate);

	/*
	 * Poll while in error display state - should remain in UNS_BAD_PASS
	 * Error timeout is checked but we skip delays in tests
	 */
	ut_assertok(bc_logic_poll(logic));
	ut_asserteq(UNS_BAD_PASS, lpriv->ustate);

	/*
	 * Advance time past the error timeout (5 seconds) to trigger
	 * transition back to UNS_IDLE
	 */
	timer_test_add_offset(6000);  /* 6 seconds */

	/* Poll - error timeout should expire, transition to UNS_IDLE */
	ut_assertok(bc_logic_poll(logic));
	ut_asserteq(UNS_IDLE, lpriv->ustate);

	/* Click on the OS again to re-select it */
	ut_assertok(click_os(uts, lpriv, 0));

	/* Poll - should process click and ask for passphrase again */
	ut_assertok(bc_logic_poll(logic));

	/*
	 * Now type the correct passphrase. The test image was created with
	 * USS "test" which produces the pubkey configured in the emulator
	 * above.
	 */
	ut_assertok(try_tkey_unlock(uts, logic, emul, test_pubkey, "test",
				    NULL));

	/* Unlock should succeed, transition to UNS_OK */
	ut_asserteq(UNS_OK, lpriv->ustate);

	/* Verify TKey device was found and used */
	ut_assertnonnull(lpriv->tkey);
	ut_assert(lpriv->tkey_present);

	test_set_skip_delays(false);

	return 0;
}
BOOTCTL_TEST(bootctl_logic_tkey, UTF_DM | UTF_SCAN_FDT | UTF_CONSOLE);
