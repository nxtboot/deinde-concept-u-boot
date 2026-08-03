// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2022 Google LLC
 * Written by Simon Glass <sjg@chromium.org>
 */

#include <command.h>
#include <env.h>
#include <dm.h>
#include <expo.h>
#include <expo_test.h>
#include <membuf.h>
#include <menu.h>
#include <video.h>
#include <video_console.h>
#include <linux/input.h>
#include <test/cedit-test.h>
#include <test/ut.h>
#include <test/video.h>
#include "bootstd_common.h"
#include "expo_common.h"
#include "../../boot/scene_internal.h"

enum {
	/* scenes */
	SCENE1		= 7,
	SCENE2,

	/* objects */
	OBJ_LOGO,
	OBJ_TEXT,
	OBJ_TEXT2,
	OBJ_TEXT3,
	OBJ_MENU,
	OBJ_MENU_TITLE,
	OBJ_BOX,
	OBJ_BOX2,
	OBJ_TEXTLINE,
	OBJ_TEXTED,
	OBJ_OVERLAP,

	/* strings */
	STR_SCENE_TITLE,

	STR_TEXT,
	STR_TEXT2,
	STR_TEXT3,
	STR_TEXTED,
	STR_MENU_TITLE,
	STR_POINTER_TEXT,

	STR_ITEM1_LABEL,
	STR_ITEM1_DESC,
	STR_ITEM1_KEY,
	STR_ITEM1_PREVIEW,

	STR_ITEM2_LABEL,
	STR_ITEM2_DESC,
	STR_ITEM2_KEY,
	STR_ITEM2_PREVIEW,

	STR_OVERLAP,

	/* menu items */
	ITEM1,
	ITEM1_LABEL,
	ITEM1_DESC,
	ITEM1_KEY,
	ITEM1_PREVIEW,

	ITEM2,
	ITEM2_LABEL,
	ITEM2_DESC,
	ITEM2_KEY,
	ITEM2_PREVIEW,

	/* pointer to current item */
	POINTER_TEXT,
};

#define BAD_POINTER	((void *)1)

/* names for various things */
#define EXPO_NAME	"my menus"
#define SCENE_NAME1	"main"
#define SCENE_NAME2	"second"
#define SCENE_TITLE	"Main Menu"
#define LOGO_NAME	"logo"

/* Check base expo support */
static int expo_base(struct unit_test_state *uts)
{
	struct udevice *dev;
	struct expo *exp;
	ulong start_mem;
	char name[100];
	int i;

	ut_assertok(uclass_first_device_err(UCLASS_VIDEO, &dev));

	start_mem = ut_check_free();

	exp = NULL;
	strcpy(name, EXPO_NAME);
	ut_assertok(expo_new(name, NULL, &exp));
	*name = '\0';
	ut_assertnonnull(exp);
	ut_asserteq(0, exp->scene_id);
	ut_asserteq(EXPOID_BASE_ID, exp->next_id);

	/* Make sure the name was allocated */
	ut_assertnonnull(exp->name);
	ut_asserteq_str(EXPO_NAME, exp->name);

	ut_assertok(expo_set_display(exp, dev));
	expo_destroy(exp);
	ut_assertok(ut_check_delta(start_mem));

	/* test handling out-of-memory conditions */
	for (i = 0; i < 2; i++) {
		struct expo *exp2;

		malloc_enable_testing(i);
		exp2 = BAD_POINTER;
		ut_asserteq(-ENOMEM, expo_new(EXPO_NAME, NULL, &exp2));
		ut_asserteq_ptr(BAD_POINTER, exp2);
		malloc_disable_testing();
	}

	return 0;
}
BOOTSTD_TEST(expo_base, UTF_DM | UTF_SCAN_FDT);

/* Check creating a scene */
static int expo_scene(struct unit_test_state *uts)
{
	struct scene *scn;
	struct expo *exp;
	ulong start_mem;
	char name[100];
	int id, title_id;

	start_mem = ut_check_free();

	ut_assertok(expo_new(EXPO_NAME, NULL, &exp));

	scn = NULL;
	ut_asserteq(EXPOID_BASE_ID, exp->next_id);
	strcpy(name, SCENE_NAME1);
	id = scene_new(exp, name, SCENE1, &scn);
	*name = '\0';
	ut_assertnonnull(scn);
	ut_asserteq(SCENE1, id);
	ut_asserteq(SCENE1 + 1, exp->next_id);
	ut_asserteq_ptr(exp, scn->expo);

	/* Make sure the name was allocated */
	ut_assertnonnull(scn->name);
	ut_asserteq_str(SCENE_NAME1, scn->name);

	/* Set the title */
	title_id = expo_str(exp, "title", STR_SCENE_TITLE, SCENE_TITLE);
	ut_assert(title_id >= 0);

	/* Use an allocated ID - this will be allocated after the title str */
	scn = NULL;
	id = scene_new(exp, SCENE_NAME2, 0, &scn);
	ut_assertnonnull(scn);
	scn->title_id = title_id;
	ut_asserteq(STR_SCENE_TITLE + 1, id);
	ut_asserteq(STR_SCENE_TITLE + 2, exp->next_id);
	ut_asserteq_ptr(exp, scn->expo);

	ut_asserteq_str(SCENE_NAME2, scn->name);
	ut_asserteq(title_id, scn->title_id);

	expo_destroy(exp);

	ut_assertok(ut_check_delta(start_mem));

	return 0;
}
BOOTSTD_TEST(expo_scene, UTF_DM | UTF_SCAN_FDT);

/* Check creating a scene with no ID */
static int expo_scene_no_id(struct unit_test_state *uts)
{
	struct scene *scn;
	struct expo *exp;
	char name[100];
	int id;

	ut_assertok(expo_new(EXPO_NAME, NULL, &exp));
	ut_asserteq(EXPOID_BASE_ID, exp->next_id);

	strcpy(name, SCENE_NAME1);
	id = scene_new(exp, SCENE_NAME1, 0, &scn);
	ut_asserteq(EXPOID_BASE_ID, scn->id);

	return 0;
}
BOOTSTD_TEST(expo_scene_no_id, UTF_DM | UTF_SCAN_FDT);

/* Check creating a scene with objects */
static int expo_object(struct unit_test_state *uts)
{
	struct scene_obj_img *img;
	struct scene_obj_txt *txt;
	struct scene *scn;
	struct expo *exp;
	ulong start_mem;
	char name[100];
	char *data;
	int id;

	start_mem = ut_check_free();

	ut_assertok(expo_new(EXPO_NAME, NULL, &exp));
	id = scene_new(exp, SCENE_NAME1, SCENE1, &scn);
	ut_assert(id > 0);

	ut_asserteq(0, scene_obj_count(scn));

	data = NULL;
	strcpy(name, LOGO_NAME);
	id = scene_img(scn, name, OBJ_LOGO, data, &img);
	ut_assert(id > 0);
	*name = '\0';
	ut_assertnonnull(img);
	ut_asserteq(OBJ_LOGO, id);
	ut_asserteq(OBJ_LOGO + 1, exp->next_id);
	ut_asserteq_ptr(scn, img->obj.scene);
	ut_asserteq(SCENEOBJT_IMAGE, img->obj.type);

	ut_asserteq_ptr(data, img->data);

	/* Make sure the name was allocated */
	ut_assertnonnull(scn->name);
	ut_asserteq_str(SCENE_NAME1, scn->name);

	ut_asserteq(1, scene_obj_count(scn));

	id = scene_txt_str(scn, "text", OBJ_TEXT, STR_TEXT, "my string", &txt);
	ut_assert(id > 0);
	ut_assertnonnull(txt);
	ut_asserteq(OBJ_TEXT, id);
	ut_asserteq(SCENEOBJT_TEXT, txt->obj.type);
	ut_asserteq(2, scene_obj_count(scn));

	/* Check passing NULL as the final parameter */
	id = scene_txt_str(scn, "text2", OBJ_TEXT2, STR_TEXT2, "another string",
			   NULL);
	ut_assert(id > 0);
	ut_asserteq(3, scene_obj_count(scn));

	expo_destroy(exp);

	ut_assertok(ut_check_delta(start_mem));

	return 0;
}
BOOTSTD_TEST(expo_object, UTF_DM | UTF_SCAN_FDT);

/* Check setting object attributes and using themes */
static int expo_object_attr(struct unit_test_state *uts)
{
	struct scene_obj_menu *menu;
	struct scene_obj_img *img;
	struct scene_obj_txt *txt;
	struct scene *scn;
	struct expo *exp;
	ulong start_mem;
	char name[100];
	char image[10];
	ofnode node;
	char *data;
	int id;

	start_mem = ut_check_free();

	ut_assertok(expo_new(EXPO_NAME, NULL, &exp));
	id = scene_new(exp, SCENE_NAME1, SCENE1, &scn);
	ut_assert(id > 0);

	data = NULL;
	id = scene_img(scn, LOGO_NAME, OBJ_LOGO, data, &img);
	ut_assert(id > 0);

	ut_assertok(scene_obj_set_pos(scn, OBJ_LOGO, 123, 456));
	ut_asserteq(123, img->obj.req_bbox.x0);
	ut_asserteq(456, img->obj.req_bbox.y0);

	ut_asserteq(-ENOENT, scene_obj_set_pos(scn, OBJ_TEXT2, 0, 0));

	id = scene_txt_str(scn, "text", OBJ_TEXT, STR_TEXT, "my string", &txt);
	ut_assert(id > 0);

	strcpy(name, "font2");
	ut_assertok(scene_txt_set_font(scn, OBJ_TEXT, name, 42));
	ut_asserteq_ptr(name, txt->gen.font_name);
	ut_asserteq(42, txt->gen.font_size);

	ut_asserteq(-ENOENT, scene_txt_set_font(scn, OBJ_TEXT2, name, 42));

	id = scene_menu(scn, "main", OBJ_MENU, &menu);
	ut_assert(id > 0);

	ut_assertok(scene_menu_set_title(scn, OBJ_MENU, OBJ_TEXT));

	ut_asserteq(-ENOENT, scene_menu_set_title(scn, OBJ_TEXT2, OBJ_TEXT));
	ut_asserteq(-EINVAL, scene_menu_set_title(scn, OBJ_MENU, OBJ_TEXT2));

	node = ofnode_path("/bootstd/theme");
	ut_assert(ofnode_valid(node));
	ut_assertok(expo_setup_theme(exp, node));
	ut_asserteq(30, txt->gen.font_size);

	/* try setting up a fake image */
	strcpy(image, "wibble");
	ut_assertok(scene_img_set_data(scn, OBJ_LOGO, image, sizeof(image)));
	ut_asserteq_str("wibble", img->data);

	expo_destroy(exp);

	ut_assertok(ut_check_delta(start_mem));

	return 0;
}
BOOTSTD_TEST(expo_object_attr, UTF_DM | UTF_SCAN_FDT);

/**
 * struct test_iter_priv - private data for expo-iterator test
 *
 * @count: number of scene objects
 * @menu_count: number of menus
 * @fail_at: item ID at which to return an error
 */
struct test_iter_priv {
	int count;
	int menu_count;
	int fail_at;
};

int h_test_iter(struct scene_obj *obj, void *vpriv)
{
	struct test_iter_priv *priv = vpriv;

	if (priv->fail_at == obj->id)
		return -EINVAL;

	priv->count++;
	if (obj->type == SCENEOBJT_MENU)
		priv->menu_count++;

	return 0;
}

/* Check creating a scene with a menu */
static int expo_object_menu(struct unit_test_state *uts)
{
	struct scene_obj_menu *menu;
	struct scene_menitem *item;
	int id, label_id, desc_id, key_id, pointer_id, preview_id;
	struct scene_obj_txt *ptr, *name1, *desc1, *key1, *tit, *prev1;
	struct test_iter_priv priv;
	struct scene *scn;
	struct expo *exp;
	ulong start_mem;

	start_mem = ut_check_free();

	ut_assertok(expo_new(EXPO_NAME, NULL, &exp));
	id = scene_new(exp, SCENE_NAME1, SCENE1, &scn);
	ut_assert(id > 0);

	id = scene_menu(scn, "main", OBJ_MENU, &menu);
	ut_assert(id > 0);
	ut_assertnonnull(menu);
	ut_asserteq(OBJ_MENU, id);
	ut_asserteq(SCENEOBJT_MENU, menu->obj.type);
	ut_asserteq(0, menu->title_id);
	ut_asserteq(0, menu->pointer_id);

	ut_assertok(scene_obj_set_pos(scn, OBJ_MENU, 50, 400));
	ut_asserteq(50, menu->obj.req_bbox.x0);
	ut_asserteq(400, menu->obj.req_bbox.y0);

	id = scene_txt_str(scn, "title", OBJ_MENU_TITLE, STR_MENU_TITLE,
			   "Main Menu", &tit);
	ut_assert(id > 0);
	ut_assertok(scene_menu_set_title(scn, OBJ_MENU, OBJ_MENU_TITLE));
	ut_asserteq(OBJ_MENU_TITLE, menu->title_id);

	pointer_id = scene_txt_str(scn, "cur_item", POINTER_TEXT,
				   STR_POINTER_TEXT, ">", &ptr);
	ut_assert(pointer_id > 0);

	ut_assertok(scene_menu_set_pointer(scn, OBJ_MENU, POINTER_TEXT));
	ut_asserteq(POINTER_TEXT, menu->pointer_id);

	label_id = scene_txt_str(scn, "label1", ITEM1_LABEL, STR_ITEM1_LABEL,
				 "Play", &name1);
	ut_assert(label_id > 0);

	desc_id = scene_txt_str(scn, "desc1", ITEM1_DESC, STR_ITEM1_DESC,
				"Lord Melchett", &desc1);
	ut_assert(desc_id > 0);

	key_id = scene_txt_str(scn, "item1-key", ITEM1_KEY, STR_ITEM1_KEY, "1",
			       &key1);
	ut_assert(key_id > 0);

	preview_id = scene_txt_str(scn, "item1-preview", ITEM1_PREVIEW,
				   STR_ITEM1_PREVIEW, "(preview1)", &prev1);
	ut_assert(preview_id > 0);

	id = scene_menuitem(scn, OBJ_MENU, "linux", ITEM1, ITEM1_KEY,
			    ITEM1_LABEL, ITEM1_DESC, ITEM1_PREVIEW, 0, &item);
	ut_asserteq(ITEM1, id);
	ut_asserteq(id, item->id);
	ut_asserteq(key_id, item->key_id);
	ut_asserteq(label_id, item->label_id);
	ut_asserteq(desc_id, item->desc_id);
	ut_asserteq(preview_id, item->preview_id);

	ut_assertok(scene_arrange(scn));

	/* arranging the scene should cause the first item to become current */
	ut_asserteq(id, menu->cur_item_id);

	/* the title should be at the top */
	ut_asserteq(menu->obj.bbox.x0, tit->obj.bbox.x0);
	ut_asserteq(menu->obj.bbox.y0, tit->obj.bbox.y0);

	/* the first item should be next */
	ut_asserteq(menu->obj.bbox.x0, name1->obj.bbox.x0);
	ut_asserteq(menu->obj.bbox.y0 + 32, name1->obj.bbox.y0);

	ut_asserteq(menu->obj.bbox.x0 + 100, ptr->obj.bbox.x0);
	ut_asserteq(menu->obj.bbox.y0 + 32, ptr->obj.bbox.y0);

	ut_asserteq(menu->obj.bbox.x0 + 129, key1->obj.bbox.x0);
	ut_asserteq(menu->obj.bbox.y0 + 32, key1->obj.bbox.y0);

	ut_asserteq(menu->obj.bbox.x0 + 179, desc1->obj.bbox.x0);
	ut_asserteq(menu->obj.bbox.y0 + 32, desc1->obj.bbox.y0);

	ut_asserteq(-84, prev1->obj.bbox.x0);
	ut_asserteq(menu->obj.bbox.y0 + 32, prev1->obj.bbox.y0);
	ut_asserteq(true, prev1->obj.flags & SCENEOF_HIDE);

	/* check iterating through scene items */
	memset(&priv, '\0', sizeof(priv));
	ut_assertok(expo_iter_scene_objs(exp, h_test_iter, &priv));
	ut_asserteq(7, priv.count);
	ut_asserteq(1, priv.menu_count);

	/* check the iterator failing part way through iteration */
	memset(&priv, '\0', sizeof(priv));
	priv.fail_at = key_id;
	ut_asserteq(-EINVAL, expo_iter_scene_objs(exp, h_test_iter, &priv));

	/* 2 items (preview_id and the menuitem) are after key_id, 7 - 2 = 5 */
	ut_asserteq(5, priv.count);

	/* menu is first, so is still processed */
	ut_asserteq(1, priv.menu_count);

	expo_destroy(exp);

	ut_assertok(ut_check_delta(start_mem));

	return 0;
}
BOOTSTD_TEST(expo_object_menu, UTF_DM | UTF_SCAN_FDT);

/**
 * create_test_expo() - Create a test expo with menu items for testing
 *
 * @uts: Unit test state
 * @expp: Returns pointer to expo
 * @scnp: Returns pointer to scene
 * @menup: Returns pointer to menu
 * @bufp: Returns pointer to buf (caller must uninit)
 * @logo_copyp: Returns pointer to logo_copy (caller must uninit)
 * Returns: 0 if OK, -ve on error
 */
static int create_test_expo(struct unit_test_state *uts, struct expo **expp,
			    struct scene **scnp, struct scene_obj_menu **menup,
			    struct abuf *bufp, struct abuf *logo_copyp)
{
	struct scene_obj_menu *menu;
	struct abuf buf, logo_copy;
	struct scene *scn;
	struct udevice *dev;
	struct expo *exp;
	int id, size;
	void *logo;

	ut_assertok(uclass_first_device_err(UCLASS_VIDEO, &dev));

	ut_assertok(expo_new(EXPO_NAME, NULL, &exp));
	id = scene_new(exp, SCENE_NAME1, SCENE1, &scn);
	ut_assert(id > 0);
	ut_assertok(expo_set_display(exp, dev));

	id = scene_img(scn, "logo", OBJ_LOGO, video_get_u_boot_logo(NULL),
		       NULL);
	ut_assert(id > 0);
	ut_assertok(scene_obj_set_pos(scn, OBJ_LOGO, 50, 20));

	id = scene_txt_str(scn, "text", OBJ_TEXT, STR_TEXT, "my string", NULL);
	ut_assert(id > 0);
	ut_assertok(scene_txt_set_font(scn, OBJ_TEXT, "cantoraone_regular",
				       40));
	ut_assertok(scene_obj_set_pos(scn, OBJ_TEXT, 400, 100));

	id = scene_txt_str(scn, "text", OBJ_TEXT2, STR_TEXT2, "another string",
			   NULL);
	ut_assert(id > 0);
	ut_assertok(scene_txt_set_font(scn, OBJ_TEXT2, "nimbus_sans_l_regular",
				       60));
	ut_assertok(scene_obj_set_pos(scn, OBJ_TEXT2, 200, 600));

	/* this string is clipped as it extends beyond its bottom bound */
	id = scene_txt_str(scn, "text", OBJ_TEXT3, STR_TEXT3,
			   "this is yet\nanother string, with word-wrap and it goes on for quite a while",
			   NULL);
	ut_assert(id > 0);
	ut_assertok(scene_txt_set_font(scn, OBJ_TEXT3, "nimbus_sans_l_regular",
				       60));
	ut_assertok(scene_obj_set_bbox(scn, OBJ_TEXT3, 500, 200, 1000, 350));

	id = scene_menu(scn, "main", OBJ_MENU, &menu);
	ut_assert(id > 0);

	id = scene_txt_str(scn, "title", OBJ_MENU_TITLE, STR_MENU_TITLE,
			   "Main Menu", NULL);
	ut_assert(id > 0);
	ut_assertok(scene_menu_set_title(scn, OBJ_MENU, OBJ_MENU_TITLE));

	id = scene_txt_str(scn, "cur_item", POINTER_TEXT, STR_POINTER_TEXT, ">",
			   NULL);
	ut_assert(id > 0);
	ut_assertok(scene_menu_set_pointer(scn, OBJ_MENU, POINTER_TEXT));

	id = scene_txt_str(scn, "label1", ITEM1_LABEL, STR_ITEM1_LABEL, "Play",
			   NULL);
	ut_assert(id > 0);
	id = scene_txt_str(scn, "item1-txt", ITEM1_DESC, STR_ITEM1_DESC,
			   "Lord Melchett", NULL);
	ut_assert(id > 0);
	id = scene_txt_str(scn, "item1-key", ITEM1_KEY, STR_ITEM1_KEY, "1",
			   NULL);
	ut_assert(id > 0);

	/*
	 * hack the logo to change the palette and use that for item2 so we can
	 * tell them apart
	 */
	logo = video_get_u_boot_logo(&size);
	abuf_init_const(&buf, logo, size);
	ut_assert(abuf_copy(&buf, &logo_copy));
	memset(logo_copy.data + 0x70, '\x45', 0x20);

	id = scene_img(scn, "item1-preview", ITEM1_PREVIEW, logo_copy.data,
		       NULL);
	id = scene_menuitem(scn, OBJ_MENU, "item1", ITEM1, ITEM1_KEY,
			    ITEM1_LABEL, ITEM1_DESC, ITEM1_PREVIEW, 0, NULL);
	ut_assert(id > 0);

	id = scene_txt_str(scn, "label2", ITEM2_LABEL, STR_ITEM2_LABEL, "Now",
			   NULL);
	ut_assert(id > 0);
	id = scene_txt_str(scn, "item2-txt", ITEM2_DESC, STR_ITEM2_DESC,
			   "Lord Percy", NULL);
	ut_assert(id > 0);
	id = scene_txt_str(scn, "item2-key", ITEM2_KEY, STR_ITEM2_KEY, "2",
			   NULL);
	ut_assert(id > 0);
	id = scene_img(scn, "item2-preview", ITEM2_PREVIEW,
		       video_get_u_boot_logo(NULL), NULL);
	ut_assert(id > 0);

	id = scene_menuitem(scn, OBJ_MENU, "item2", ITEM2, ITEM2_KEY,
			    ITEM2_LABEL, ITEM2_DESC, ITEM2_PREVIEW, 0, NULL);
	ut_assert(id > 0);

	ut_assertok(scene_obj_set_pos(scn, OBJ_MENU, 50, 400));

	id = scene_box(scn, "box", OBJ_BOX, 3, false, NULL);
	ut_assert(id > 0);
	ut_assertok(scene_obj_set_bbox(scn, OBJ_BOX, 40, 390, 1000, 510));

	id = scene_box(scn, "box2", OBJ_BOX2, 1, false, NULL);
	ut_assert(id > 0);
	ut_assertok(scene_obj_set_bbox(scn, OBJ_BOX, 500, 200, 1000, 350));

	/*
	 * Add an extra text object that overlaps with OBJ_TEXT to test reverse
	 * search order. OBJ_TEXT is at (400, 100), so let's add one nearby
	 */
	ut_assert(scene_txt_str(scn, "overlap", OBJ_OVERLAP, STR_OVERLAP,
				"overlap text", NULL) > 0);
	ut_assertok(scene_obj_set_pos(scn, OBJ_OVERLAP, 405, 105));

	*expp = exp;
	*scnp = scn;
	*menup = menu;
	*bufp = buf;
	*logo_copyp = logo_copy;

	return 0;
}

/* Check rendering a scene */
static int expo_render_image(struct unit_test_state *uts)
{
	struct scene_obj_menu *menu;
	struct abuf buf, logo_copy;
	struct scene *scn, *scn2;
	struct expo_action act;
	struct scene_obj *obj;
	struct udevice *dev;
	struct expo *exp;

	ut_assertok(create_test_expo(uts, &exp, &scn, &menu, &buf, &logo_copy));
	dev = exp->display;

	scn2 = expo_lookup_scene_id(exp, SCENE1);
	ut_asserteq_ptr(scn, scn2);
	scn2 = expo_lookup_scene_id(exp, SCENE2);
	ut_assertnull(scn2);

	/* render without a scene */
	ut_asserteq(-ECHILD, expo_render(exp));

	ut_assertok(scene_arrange(scn));

	/* check dimensions of text */
	obj = scene_obj_find(scn, OBJ_TEXT, SCENEOBJT_NONE);
	ut_assertnonnull(obj);
	ut_asserteq(400, obj->bbox.x0);
	ut_asserteq(100, obj->bbox.y0);
	ut_asserteq(400 + 126, obj->bbox.x1);
	ut_asserteq(100 + 40, obj->bbox.y1);

	/* check dimensions of image */
	obj = scene_obj_find(scn, OBJ_LOGO, SCENEOBJT_NONE);
	ut_assertnonnull(obj);
	ut_asserteq(50, obj->bbox.x0);
	ut_asserteq(20, obj->bbox.y0);
	ut_asserteq(50 + 160, obj->bbox.x1);
	ut_asserteq(20 + 160, obj->bbox.y1);

	/* check dimensions of menu labels - both should be the same width */
	obj = scene_obj_find(scn, ITEM1_LABEL, SCENEOBJT_NONE);
	ut_assertnonnull(obj);
	ut_asserteq(50, obj->bbox.x0);
	ut_asserteq(436, obj->bbox.y0);
	ut_asserteq(50 + 29, obj->bbox.x1);
	ut_asserteq(436 + 18, obj->bbox.y1);

	obj = scene_obj_find(scn, ITEM2_LABEL, SCENEOBJT_NONE);
	ut_assertnonnull(obj);
	ut_asserteq(50, obj->bbox.x0);
	ut_asserteq(454, obj->bbox.y0);
	ut_asserteq(50 + 29, obj->bbox.x1);
	ut_asserteq(454 + 18, obj->bbox.y1);

	/* same for the key */
	obj = scene_obj_find(scn, ITEM1_KEY, SCENEOBJT_NONE);
	ut_assertnonnull(obj);
	ut_asserteq(177, obj->bbox.x0);
	ut_asserteq(436, obj->bbox.y0);
	ut_asserteq(177 + 9, obj->bbox.x1);
	ut_asserteq(436 + 18, obj->bbox.y1);

	obj = scene_obj_find(scn, ITEM2_KEY, SCENEOBJT_NONE);
	ut_assertnonnull(obj);
	ut_asserteq(177, obj->bbox.x0);
	ut_asserteq(454, obj->bbox.y0);
	ut_asserteq(177 + 9, obj->bbox.x1);
	ut_asserteq(454 + 18, obj->bbox.y1);

	/* and the description */
	obj = scene_obj_find(scn, ITEM1_DESC, SCENEOBJT_NONE);
	ut_assertnonnull(obj);
	ut_asserteq(227, obj->bbox.x0);
	ut_asserteq(436, obj->bbox.y0);
	ut_asserteq(227 + 89, obj->bbox.x1);
	ut_asserteq(436 + 18, obj->bbox.y1);

	obj = scene_obj_find(scn, ITEM2_DESC, SCENEOBJT_NONE);
	ut_assertnonnull(obj);
	ut_asserteq(227, obj->bbox.x0);
	ut_asserteq(454, obj->bbox.y0);
	ut_asserteq(227 + 89, obj->bbox.x1);
	ut_asserteq(454 + 18, obj->bbox.y1);

	/* check dimensions of menu */
	obj = scene_obj_find(scn, OBJ_MENU, SCENEOBJT_NONE);
	ut_assertnonnull(obj);
	ut_asserteq(50, obj->bbox.x0);
	ut_asserteq(400, obj->bbox.y0);
	ut_asserteq(50 + 160, obj->bbox.x1);
	ut_asserteq(400 + 160, obj->bbox.y1);

	scene_obj_set_width(scn, OBJ_MENU, 170);
	ut_asserteq(50 + 170, obj->req_bbox.x1);
	scene_obj_set_bbox(scn, OBJ_MENU, 60, 410, 50 + 160, 400 + 160);
	ut_asserteq(60, obj->req_bbox.x0);
	ut_asserteq(410, obj->req_bbox.y0);
	ut_asserteq(50 + 160, obj->req_bbox.x1);
	ut_asserteq(400 + 160, obj->req_bbox.y1);

	/* reset back to normal */
	scene_obj_set_bbox(scn, OBJ_MENU, 50, 400, 50 + 160, 400 + 160);

	/* make sure the preview for the first item is not shown */
	obj = scene_obj_find(scn, ITEM1_PREVIEW, SCENEOBJT_NONE);
	ut_assertnonnull(obj);
	ut_assert(obj->flags & SCENEOF_HIDE);

	/* render it */
	expo_set_scene_id(exp, SCENE1);
	ut_assertok(expo_render(exp));
	ut_asserteq(14714, video_compress_fb(uts, dev, false));

	ut_asserteq(0, scn->highlight_id);
	ut_assertok(scene_arrange(scn));
	ut_asserteq(0, scn->highlight_id);
	ut_assertok(expo_render(exp));
	ut_asserteq(15533, video_compress_fb(uts, dev, false));

	ut_assertok(scene_arrange(scn));
	ut_assertok(expo_render(exp));
	ut_asserteq(15533, video_compress_fb(uts, dev, false));

	scene_set_highlight_id(scn, OBJ_MENU);
	ut_asserteq(OBJ_MENU, scn->highlight_id);
	ut_assertok(scene_menu_select_item(scn, OBJ_MENU, ITEM1));
	ut_assertok(scene_arrange(scn));
	ut_asserteq(OBJ_MENU, scn->highlight_id);

	/* make sure the preview for the first item is now shown */
	ut_assert(!(obj->flags & SCENEOF_HIDE));

	ut_assertok(expo_render(exp));
	ut_asserteq(15533, video_compress_fb(uts, dev, false));

	/* move down */
	ut_assertok(expo_send_key(exp, BKEY_DOWN));

	ut_assertok(expo_action_get(exp, &act));

	ut_asserteq(EXPOACT_POINT_ITEM, act.type);
	ut_asserteq(ITEM2, act.select.id);
	ut_assertok(scene_menu_select_item(scn, OBJ_MENU, act.select.id));
	ut_asserteq(ITEM2, scene_menu_get_cur_item(scn, OBJ_MENU));
	ut_assertok(scene_arrange(scn));
	ut_assertok(expo_render(exp));
	ut_asserteq(15363, video_compress_fb(uts, dev, false));
	ut_assertok(video_check_copy_fb(uts, dev));

	/* do some alignment checks */
	ut_assertok(scene_obj_set_halign(scn, OBJ_TEXT3, SCENEOA_CENTRE));
	ut_assertok(expo_render(exp));
	ut_asserteq(15400, video_compress_fb(uts, dev, false));
	ut_assertok(scene_obj_set_halign(scn, OBJ_TEXT3, SCENEOA_RIGHT));
	ut_assertok(expo_render(exp));
	ut_asserteq(15369, video_compress_fb(uts, dev, false));

	ut_assertok(scene_obj_set_halign(scn, OBJ_TEXT3, SCENEOA_LEFT));
	ut_assertok(scene_obj_set_valign(scn, OBJ_TEXT3, SCENEOA_CENTRE));
	ut_assertok(expo_render(exp));
	ut_asserteq(17821, video_compress_fb(uts, dev, false));
	ut_assertok(scene_obj_set_valign(scn, OBJ_TEXT3, SCENEOA_BOTTOM));
	ut_assertok(expo_render(exp));
	ut_asserteq(17802, video_compress_fb(uts, dev, false));

	/* make sure only the preview for the second item is shown */
	obj = scene_obj_find(scn, ITEM1_PREVIEW, SCENEOBJT_NONE);
	ut_asserteq(true, obj->flags & SCENEOF_HIDE);

	obj = scene_obj_find(scn, ITEM2_PREVIEW, SCENEOBJT_NONE);
	ut_asserteq(false, obj->flags & SCENEOF_HIDE);

	/* select it */
	ut_assertok(expo_send_key(exp, BKEY_SELECT));

	ut_assertok(expo_action_get(exp, &act));
	ut_asserteq(EXPOACT_SELECT, act.type);
	ut_asserteq(ITEM2, act.select.id);

	/* make sure the action doesn't come again */
	ut_asserteq(-EAGAIN, expo_action_get(exp, &act));

	/* make sure there was no console output */
	ut_assert_console_end();

	/* now try with the highlight */
	exp->show_highlight = true;
	ut_assertok(scene_arrange(scn));
	ut_assertok(expo_render(exp));
	ut_asserteq(17948, video_compress_fb(uts, dev, false));

	/* now try in text mode */
	expo_set_text_mode(exp, true);
	ut_assertok(expo_render(exp));

	ut_assert_nextline("U-Boot    :    Boot Menu");
	ut_assert_nextline("%s", "");
	ut_assert_nextline("Main Menu");
	ut_assert_nextline("%s", "");
	ut_assert_nextline("       1  Play        Lord Melchett");
	ut_assert_nextline("  >    2  Now         Lord Percy");

	/* Move back up to the first item */
	ut_assertok(expo_send_key(exp, BKEY_UP));

	ut_assertok(expo_action_get(exp, &act));

	ut_asserteq(EXPOACT_POINT_ITEM, act.type);
	ut_asserteq(ITEM1, act.select.id);
	ut_assertok(scene_menu_select_item(scn, OBJ_MENU, act.select.id));

	ut_assertok(expo_render(exp));
	ut_assert_nextline("U-Boot    :    Boot Menu");
	ut_assert_nextline("%s", "");
	ut_assert_nextline("Main Menu");
	ut_assert_nextline("%s", "");
	ut_assert_nextline("  >    1  Play        Lord Melchett");
	ut_assert_nextline("       2  Now         Lord Percy");

	ut_assert_console_end();

	abuf_uninit(&buf);
	abuf_uninit(&logo_copy);

	expo_destroy(exp);

	return 0;
}
BOOTSTD_TEST(expo_render_image, UTF_DM | UTF_SCAN_FDT | UTF_CONSOLE);

/* Check building an expo from a devicetree description */
static int expo_test_build(struct unit_test_state *uts)
{
	struct scene_obj_menu *menu;
	struct scene_menitem *item;
	struct scene_obj_txt *txt;
	struct abuf orig, *copy;
	struct scene_obj *obj;
	struct scene *scn;
	struct expo *exp;
	int count;
	ofnode node;

	node = ofnode_path("/cedit");
	ut_assert(ofnode_valid(node));
	ut_assertok(expo_build(node, &exp));

	ut_asserteq_str("name", exp->name);
	ut_asserteq(0, exp->scene_id);
	ut_asserteq(ID_DYNAMIC_START + 23, exp->next_id);
	ut_asserteq(false, exp->popup);

	/* check the scene */
	scn = expo_lookup_scene_id(exp, ID_SCENE1);
	ut_assertnonnull(scn);
	ut_asserteq_str("main", scn->name);
	ut_asserteq(ID_SCENE1, scn->id);
	ut_asserteq(ID_DYNAMIC_START, scn->title_id);
	ut_asserteq(0, scn->highlight_id);

	/* check the title */
	txt = scene_obj_find(scn, scn->title_id, SCENEOBJT_NONE);
	ut_assertnonnull(txt);
	obj = &txt->obj;
	ut_asserteq_ptr(scn, obj->scene);
	ut_asserteq_str("main.title", obj->name);
	ut_asserteq(scn->title_id, obj->id);
	ut_asserteq(SCENEOBJT_TEXT, obj->type);
	ut_asserteq(0, obj->flags);
	ut_asserteq_str("Test Configuration",
			expo_get_str(exp, txt->gen.str_id));

	/* check the menu */
	menu = scene_obj_find(scn, ID_CPU_SPEED, SCENEOBJT_NONE);
	obj = &menu->obj;
	ut_asserteq_ptr(scn, obj->scene);
	ut_asserteq_str("cpu-speed", obj->name);
	ut_asserteq(ID_CPU_SPEED, obj->id);
	ut_asserteq(SCENEOBJT_MENU, obj->type);
	ut_asserteq(0, obj->flags);

	txt = scene_obj_find(scn, menu->title_id, SCENEOBJT_NONE);
	ut_asserteq_str("CPU speed", expo_get_str(exp, txt->gen.str_id));

	ut_asserteq(0, menu->cur_item_id);
	ut_asserteq(0, menu->pointer_id);

	/* check the items */
	item = list_first_entry(&menu->item_head, struct scene_menitem,
				sibling);
	ut_asserteq_str("cpu-speed.item-0", item->name);
	ut_asserteq(ID_CPU_SPEED_1, item->id);
	ut_asserteq(0, item->key_id);
	ut_asserteq(0, item->desc_id);
	ut_asserteq(0, item->preview_id);
	ut_asserteq(0, item->flags);
	ut_asserteq(0, item->value);

	txt = scene_obj_find(scn, item->label_id, SCENEOBJT_NONE);
	ut_asserteq_str("2 GHz", expo_get_str(exp, txt->gen.str_id));

	count = list_count_nodes(&menu->item_head);
	ut_asserteq(3, count);

	/* check the box */
	struct scene_obj_box *box = scene_obj_find(scn, ID_TEST_BOX, SCENEOBJT_NONE);
	ut_assertnonnull(box);
	obj = &box->obj;
	ut_asserteq_ptr(scn, obj->scene);
	ut_asserteq_str("test-box", obj->name);
	ut_asserteq(ID_TEST_BOX, obj->id);
	ut_asserteq(SCENEOBJT_BOX, obj->type);
	ut_asserteq(0, obj->flags);
	ut_asserteq(5, box->width);
	ut_asserteq(false, box->fill);

	/* check the filled box */
	struct scene_obj_box *filled_box = scene_obj_find(scn, ID_TEST_BOX_FILLED, SCENEOBJT_NONE);
	ut_assertnonnull(filled_box);
	obj = &filled_box->obj;
	ut_asserteq_ptr(scn, obj->scene);
	ut_asserteq_str("test-box-filled", obj->name);
	ut_asserteq(ID_TEST_BOX_FILLED, obj->id);
	ut_asserteq(SCENEOBJT_BOX, obj->type);
	ut_asserteq(0, obj->flags);
	ut_asserteq(3, filled_box->width);
	ut_asserteq(true, filled_box->fill);

	/* test scene_box_set_fill() function */
	ut_assertok(scene_box_set_fill(scn, ID_TEST_BOX, true));
	ut_asserteq(true, box->fill);
	ut_assertok(scene_box_set_fill(scn, ID_TEST_BOX_FILLED, false));
	ut_asserteq(false, filled_box->fill);

	/* test error case */
	ut_asserteq(-ENOENT, scene_box_set_fill(scn, 9999, true));

	/* try editing some text */
	ut_assertok(expo_edit_str(exp, txt->gen.str_id, &orig, &copy));
	ut_asserteq_str("2 GHz", orig.data);
	ut_asserteq_str("2 GHz", copy->data);

	/* change it and check that things look right */
	abuf_printf(copy, "atlantic %d", 123);
	ut_asserteq_str("2 GHz", orig.data);
	ut_asserteq_str("atlantic 123", copy->data);

	expo_destroy(exp);

	return 0;
}
BOOTSTD_TEST(expo_test_build, UTF_DM);

/* test scene object within functions */
static int expo_within_funcs(struct unit_test_state *uts)
{
	struct scene_obj_textline *tline;
	struct scene_obj_menu *menu;
	struct scene_menitem *item;
	struct scene_obj *obj;
	struct udevice *dev;
	struct scene *scn;
	struct expo *exp;
	ofnode node;

	node = ofnode_path("/cedit");
	ut_assert(ofnode_valid(node));
	ut_assertok(expo_build(node, &exp));

	scn = expo_lookup_scene_id(exp, ID_SCENE1);
	ut_assertnonnull(scn);

	/* test scene_menu_within() */
	menu = scene_obj_find(scn, ID_CPU_SPEED, SCENEOBJT_NONE);
	ut_assertnonnull(menu);

	ut_assertok(uclass_first_device_err(UCLASS_VIDEO, &dev));
	ut_assertok(expo_set_display(exp, dev));

	ut_assertok(scene_arrange(scn));

	/* get first menu item and test with its coordinates */
	item = list_first_entry(&menu->item_head, struct scene_menitem,
				sibling);
	ut_assertnonnull(item);

	/* get the label object to find coordinates */
	obj = scene_obj_find(scn, item->label_id, SCENEOBJT_NONE);
	ut_assertnonnull(obj);
	ut_asserteq_ptr(item, scene_menu_within(scn, menu, obj->bbox.x0 + 1,
						obj->bbox.y0 + 1));

	/* test point outside menu bounds */
	ut_assertnull(scene_menu_within(scn, menu, -1, -1));

	/* test point far outside menu bounds */
	ut_assertnull(scene_menu_within(scn, menu, 9999, 9999));

	/* test scene_textline_within() */
	tline = scene_obj_find(scn, ID_MACHINE_NAME, SCENEOBJT_NONE);
	ut_assertnonnull(tline);
	obj = scene_obj_find(scn, tline->tin.edit_id, SCENEOBJT_NONE);
	ut_assertnonnull(obj);

	/* positive test: point within textline bounds */
	ut_assert(scene_textline_within(scn, tline, obj->bbox.x0 + 1,
					obj->bbox.y0 + 1));

	/* test point outside textline bounds */
	ut_assert(!scene_textline_within(scn, tline, -1, -1));

	/* test point far outside textline bounds */
	ut_assert(!scene_textline_within(scn, tline, 9999, 9999));

	expo_destroy(exp);

	return 0;
}
BOOTSTD_TEST(expo_within_funcs, UTF_DM | UTF_SCAN_FDT);

/* test expo_set_mouse_enable() */
static int expo_mouse_enable(struct unit_test_state *uts)
{
	struct udevice *dev;
	struct expo *exp;

	ut_assertok(uclass_first_device_err(UCLASS_VIDEO, &dev));
	ut_assertok(expo_new(EXPO_NAME, NULL, &exp));
	ut_assertok(expo_set_display(exp, dev));

	ut_asserteq(false, exp->mouse_enabled);

	ut_assertok(expo_set_mouse_enable(exp, true));
	ut_assertnonnull(exp->mouse);
	ut_asserteq(UCLASS_MOUSE, device_get_uclass_id(exp->mouse));

	return 0;
}
BOOTSTD_TEST(expo_mouse_enable, UTF_DM | UTF_SCAN_FDT);

/* Check mouse click functionality */
static int expo_mouse_click(struct unit_test_state *uts)
{
	struct scene_obj_menu *menu;
	struct abuf buf, logo_copy;
	struct expo_action act;
	struct scene *scn;
	struct expo *exp;

	ut_assertok(create_test_expo(uts, &exp, &scn, &menu, &buf, &logo_copy));

	/* set the scene */
	ut_assertok(expo_set_scene_id(exp, SCENE1));

	/* arrange the scene so objects have proper bounding boxes */
	ut_assertok(scene_arrange(scn));

	/* enable mouse input */
	ut_assertok(expo_set_mouse_enable(exp, true));

	/* click on the first menu-item label */
	ut_assertok(click_check(uts, scn, ITEM1_LABEL, EXPOACT_SELECT, &act));
	ut_asserteq(EXPOACT_SELECT, act.type);
	ut_asserteq(ITEM1, act.select.id);

	/* click on the second menu-item label */
	ut_assertok(click_check(uts, scn, ITEM2_LABEL, EXPOACT_SELECT, &act));
	ut_asserteq(EXPOACT_SELECT, act.type);
	ut_asserteq(ITEM2, act.select.id);

	/* click on the second menu-item description */
	ut_assertok(click_check(uts, scn, ITEM2_DESC, EXPOACT_SELECT, &act));
	ut_asserteq(EXPOACT_SELECT, act.type);
	ut_asserteq(ITEM2, act.select.id);

	/* click in empty space */
	ut_assertok(scene_send_click(scn, 10, 10, &act));
	ut_asserteq(EXPOACT_NONE, act.type);

	abuf_uninit(&buf);
	abuf_uninit(&logo_copy);

	expo_destroy(exp);

	return 0;
}
BOOTSTD_TEST(expo_mouse_click, UTF_DM | UTF_SCAN_FDT);

static int expo_test_mode(struct unit_test_state *uts)
{
	struct scene_obj_menu *menu;
	struct abuf buf, logo_copy;
	struct udevice *dev;
	struct scene *scn;
	struct expo *exp;

	ut_assertok(create_test_expo(uts, &exp, &scn, &menu, &buf, &logo_copy));
	dev = exp->display;

	/* Check test mode is initially off */
	ut_asserteq(false, exp->test->enabled);

	/* Entering expo mode without expotest env var keeps it off */
	expo_enter_mode(exp);
	ut_asserteq(false, exp->test->enabled);
	expo_exit_mode(exp);

	/* Enable test mode */
	ut_assertok(env_set("expotest", "1"));
	expo_enter_mode(exp);
	ut_asserteq(true, exp->test->enabled);

	/* Check initial render count */
	ut_asserteq(0, exp->test->render_count);

	/* Render and check count increments */
	ut_assertok(expo_set_scene_id(exp, scn->id));
	ut_assertok(scene_arrange(scn));
	ut_assertok(expo_render(exp));
	ut_asserteq(1, exp->test->render_count);

	ut_assertok(expo_render(exp));
	ut_asserteq(2, exp->test->render_count);

	/* Test that expo_enter_mode() resets the counter */
	expo_exit_mode(exp);
	expo_enter_mode(exp);
	ut_asserteq(0, exp->test->render_count);
	ut_assertok(expo_render(exp));
	ut_asserteq(1, exp->test->render_count);
	expo_exit_mode(exp);

	/* Disable test mode */
	ut_assertok(env_set("expotest", "0"));
	expo_enter_mode(exp);
	ut_asserteq(false, exp->test->enabled);
	expo_exit_mode(exp);

	/* Check test mode is off when env var is unset */
	ut_assertok(env_set("expotest", NULL));
	expo_enter_mode(exp);
	ut_asserteq(false, exp->test->enabled);
	expo_exit_mode(exp);

	ut_assertok(env_set("expotest", NULL));
	abuf_uninit(&buf);
	abuf_uninit(&logo_copy);
	expo_destroy(exp);

	return 0;
}
BOOTSTD_TEST(expo_test_mode, UTF_DM | UTF_SCAN_FDT | UTF_CONSOLE);

static int expo_test_calc_fps(struct unit_test_state *uts)
{
	struct expo_test_mode test;
	int fps;

	memset(&test, 0, sizeof(test));

	/* No data - should return 0 */
	fps = expo_calc_fps(&test);
	ut_asserteq(0, fps);

	/* Single data point - should return 0 */
	test.fps_index = 0;
	test.fps_timestamps_ms[0] = 0;
	test.fps_frame_counts[0] = 0;
	fps = expo_calc_fps(&test);
	ut_asserteq(0, fps);

	/* Two data points: 100 frames in 1000ms = 100 FPS */
	test.fps_index = 1;
	test.fps_timestamps_ms[0] = 0;
	test.fps_frame_counts[0] = 0;
	test.fps_timestamps_ms[1] = 1000;
	test.fps_frame_counts[1] = 100;
	fps = expo_calc_fps(&test);
	ut_asserteq(100, fps);

	/* Three data points spanning 2 seconds: 240 frames in 2000ms = 120 FPS */
	test.fps_index = 2;
	test.fps_timestamps_ms[0] = 0;
	test.fps_frame_counts[0] = 0;
	test.fps_timestamps_ms[1] = 1000;
	test.fps_frame_counts[1] = 100;
	test.fps_timestamps_ms[2] = 2000;
	test.fps_frame_counts[2] = 240;
	fps = expo_calc_fps(&test);
	ut_asserteq(120, fps);

	/* Test wraparound: index at 1, with data at indices 2,3,4,0,1 */
	test.fps_index = 1;
	test.fps_timestamps_ms[2] = 0;
	test.fps_frame_counts[2] = 0;
	test.fps_timestamps_ms[3] = 1000;
	test.fps_frame_counts[3] = 60;
	test.fps_timestamps_ms[4] = 2000;
	test.fps_frame_counts[4] = 120;
	test.fps_timestamps_ms[0] = 3000;
	test.fps_frame_counts[0] = 180;
	test.fps_timestamps_ms[1] = 4000;
	test.fps_frame_counts[1] = 240;
	fps = expo_calc_fps(&test);
	ut_asserteq(60, fps);  /* 240 frames in 4000ms = 60 FPS */

	return 0;
}
BOOTSTD_TEST(expo_test_calc_fps, 0);

/* Test scene_flag_name() */
static int expo_scene_flag_name(struct unit_test_state *uts)
{
	/* Test valid flags */
	ut_asserteq_str("hide", scene_flag_name(SCENEOF_HIDE));
	ut_asserteq_str("point", scene_flag_name(SCENEOF_POINT));
	ut_asserteq_str("open", scene_flag_name(SCENEOF_OPEN));
	ut_asserteq_str("manual", scene_flag_name(SCENEOF_MANUAL));

	/* Test invalid flag (0) */
	ut_asserteq_str("(none)", scene_flag_name(0));

	/* Test invalid flag (out of range) */
	ut_asserteq_str("(none)", scene_flag_name(BIT(20)));

	return 0;
}
BOOTSTD_TEST(expo_scene_flag_name, 0);

/* Test scene_obj_type_name() */
static int expo_scene_obj_type_name(struct unit_test_state *uts)
{
	/* Test all valid object types */
	ut_asserteq_str("none", scene_obj_type_name(SCENEOBJT_NONE));
	ut_asserteq_str("image", scene_obj_type_name(SCENEOBJT_IMAGE));
	ut_asserteq_str("text", scene_obj_type_name(SCENEOBJT_TEXT));
	ut_asserteq_str("box", scene_obj_type_name(SCENEOBJT_BOX));
	ut_asserteq_str("menu", scene_obj_type_name(SCENEOBJT_MENU));
	ut_asserteq_str("textline", scene_obj_type_name(SCENEOBJT_TEXTLINE));
	ut_asserteq_str("textedit", scene_obj_type_name(SCENEOBJT_TEXTEDIT));

	/* Test invalid type (out of range) */
	ut_asserteq_str("unknown", scene_obj_type_name(SCENEOBJT_TEXTEDIT + 1));

	return 0;
}
BOOTSTD_TEST(expo_scene_obj_type_name, 0);

/* Test scene_chklog() */
static int expo_scene_chklog(struct unit_test_state *uts)
{
	/* Without filter, all objects should be logged */
	env_set("expo_log_filter", NULL);
	ut_assert(scene_chklog("my-menu"));
	ut_assert(scene_chklog("textline"));

	/* With a single filter, only matching objects should be logged */
	env_set("expo_log_filter", "menu");
	ut_assert(scene_chklog("my-menu"));
	ut_assert(scene_chklog("menu-item"));
	ut_assert(!scene_chklog("textline"));
	ut_assert(!scene_chklog("other"));

	/* With comma-separated filters, any match should pass */
	env_set("expo_log_filter", "menu,text");
	ut_assert(scene_chklog("my-menu"));
	ut_assert(scene_chklog("textline"));
	ut_assert(scene_chklog("textedit"));
	ut_assert(!scene_chklog("other"));
	ut_assert(!scene_chklog("image"));

	/* Test with three filters */
	env_set("expo_log_filter", "menu,text,img");
	ut_assert(scene_chklog("my-menu"));
	ut_assert(scene_chklog("textline"));
	ut_assert(scene_chklog("img-logo"));
	ut_assert(!scene_chklog("other"));

	/* Clear the filter */
	env_set("expo_log_filter", NULL);

	return 0;
}
BOOTSTD_TEST(expo_scene_chklog, 0);

/* Test scene_find_obj_within() */
static int expo_find_obj_within(struct unit_test_state *uts)
{
	struct scene_obj_menu *menu;
	struct abuf buf, logo_copy;
	struct scene_obj *obj;
	struct scene *scn;
	struct expo *exp;

	ut_assertok(create_test_expo(uts, &exp, &scn, &menu, &buf, &logo_copy));

	/* Arrange the scene so objects have proper bounding boxes */
	ut_assertok(scene_arrange(scn));

	/*
	 * Check finding a menu by 'clicking' on a menu item label - menu items
	 * are at (50,436) for ITEM1 and (50,454) for ITEM2
	 */
	obj = scene_find_obj_within(scn, 60, 440, false, false);
	ut_assertnonnull(obj);
	ut_asserteq(OBJ_MENU, obj->id);

	/*
	 * Check with allow_any=false for non-highlightable objects - logo and
	 * text are not highlightable, so they should not be found
	 */
	ut_assertnull(scene_find_obj_within(scn, 60, 30, false, false));
	ut_assertnull(scene_find_obj_within(scn, 410, 110, false, false));

	/* Test with allow_any=true for non-highlightable objects */
	obj = scene_find_obj_within(scn, 60, 30, false, true);
	ut_assertnonnull(obj);
	ut_asserteq(OBJ_LOGO, obj->id);

	/*
	 * Check reversing search order with allow_any=true at the overlapping
	 * position. OBJ_TEXT was created first at (400, 100), and the
	 * "overlap" text object was created second at (405, 105). They
	 * overlap at position (410, 110).
	 *
	 * With reverse=false, we search from start of list (bottom to top) and
	 * find OBJ_TEXT first.
	 */
	obj = scene_find_obj_within(scn, 410, 110, false, true);
	ut_assertnonnull(obj);
	ut_asserteq(OBJ_TEXT, obj->id);

	/*
	 * With reverse=true, we search from end of list (top to bottom) and
	 * find the OBJ_OVERLAP_TEST object first.
	 */
	obj = scene_find_obj_within(scn, 410, 110, true, true);
	ut_assertnonnull(obj);
	ut_asserteq(OBJ_OVERLAP, obj->id);

	/*
	 * Test reverse=true with a non-overlapping object - should get same
	 * result as reverse=false
	 */
	obj = scene_find_obj_within(scn, 60, 30, true, true);
	ut_assertnonnull(obj);
	ut_asserteq(OBJ_LOGO, obj->id);

	/* empty space */
	ut_assertnull(scene_find_obj_within(scn, 10, 10, false, false));

	/* way outside bounds */
	ut_assertnull(scene_find_obj_within(scn, 9999, 9999, false, false));

	abuf_uninit(&buf);
	abuf_uninit(&logo_copy);
	expo_destroy(exp);

	return 0;
}
BOOTSTD_TEST(expo_find_obj_within, UTF_DM | UTF_SCAN_FDT);

/* Test expo_dump() */
static int expo_dump_test(struct unit_test_state *uts)
{
	struct scene_obj_menu *menu;
	struct abuf buf, logo_copy;
	struct scene *scn;
	struct expo *exp;
	struct membuf mb;
	char mb_buf[4096];
	char *data;
	int len;

	membuf_init(&mb, mb_buf, sizeof(mb_buf));

	ut_assertok(create_test_expo(uts, &exp, &scn, &menu, &buf, &logo_copy));

	/* Arrange the scene so objects have proper dimensions */
	ut_assertok(scene_arrange(scn));

	/* Dump the expo */
	expo_dump(exp, &mb);

	/* Get the dumped data */
	len = membuf_getraw(&mb, sizeof(mb_buf), false, &data);
	ut_assert(len > 0);
	ut_assertnonnull(data);

	/* Nul-terminate for strstr to work */
	if (len < sizeof(mb_buf))
		data[len] = '\0';

	/* Check for a few elements in the output */
	ut_assert(strstr(data, "Expo: name"));
	ut_assert(strstr(data, "my menus"));
	ut_assert(strstr(data, "Scene"));
	ut_assert(strstr(data, "main"));

	abuf_uninit(&buf);
	abuf_uninit(&logo_copy);
	expo_destroy(exp);

	return 0;
}
BOOTSTD_TEST(expo_dump_test, UTF_DM | UTF_SCAN_FDT);

/* Check behaviour of textlines in a non-popup expo */
static int expo_render_textline(struct unit_test_state *uts)
{
	struct scene_obj_textline *tline;
	struct scene_obj_menu *menu;
	struct abuf buf, logo_copy;
	struct expo_action act;
	struct scene *scn;
	struct udevice *dev;
	struct expo *exp;
	int id;

	ut_assertok(create_test_expo(uts, &exp, &scn, &menu, &buf, &logo_copy));
	dev = exp->display;
	expo_enter_mode(exp);

	id = scene_textline(scn, "textline", OBJ_TEXTLINE, 20, &tline);
	ut_assert(id > 0);
	ut_assertok(scene_obj_set_pos(scn, OBJ_TEXTLINE, 500, 500));
	strcpy(abuf_data(&tline->tin.buf), "sample hopwind");

	/* create the label text object */
	id = scene_txt_str(scn, "tline-label", 0, 0, "Label:", NULL);
	ut_assert(id > 0);
	tline->tin.label_id = id;

	/* create the edit text object pointing to the textline buffer */
	id = scene_txt_str(scn, "tline-edit", 0, 0, abuf_data(&tline->tin.buf),
			   NULL);
	ut_assert(id > 0);
	tline->tin.edit_id = id;
	ut_assertok(scene_txt_set_font(scn, tline->tin.edit_id,
				       "nimbus_sans_l_regular", 40));

	expo_set_scene_id(exp, SCENE1);
	ut_assertok(scene_arrange(scn));
	ut_assertok(expo_render(exp));
	ut_asserteq(16751, video_compress_fb(uts, dev, false));

	/* highlight the textline and re-render */
	scene_set_highlight_id(scn, OBJ_TEXTLINE);
	ut_assertok(scene_arrange(scn));
	ut_assertok(expo_render(exp));
	ut_asserteq(17560, video_compress_fb(uts, dev, false));

	/* open the textline and re-render */
	ut_assertok(scene_set_open(scn, OBJ_TEXTLINE, true));
	ut_assertok(scene_arrange(scn));
	ut_assertok(expo_render(exp));

	/* the cursor should be at the end */
	ut_asserteq(17556, video_compress_fb(uts, dev, false));

	/* send a keypress to add a character */
	ut_assertok(expo_send_key(exp, 'a'));
	ut_assertok(scene_arrange(scn));
	ut_assertok(expo_render(exp));
	ut_asserteq(17725, video_compress_fb(uts, dev, false));

	/* move cursor left 3 times */
	ut_assertok(expo_send_key(exp, CTL_CH('b')));

	/* check cursor moved back one position, before 'a' */
	ut_asserteq(14, tline->tin.cls.num);
	ut_asserteq(15, tline->tin.cls.eol_num);
	ut_asserteq_str("sample hopwinda", abuf_data(&tline->tin.buf));
	ut_assertok(scene_arrange(scn));
	ut_assertok(expo_render(exp));
	ut_asserteq(17809, video_compress_fb(uts, dev, false));

	ut_assertok(expo_send_key(exp, CTL_CH('b')));
	ut_assertok(expo_send_key(exp, CTL_CH('b')));
	ut_assertok(expo_send_key(exp, CTL_CH('b')));

	/* check cursor moved back three more positions, before 'i' */
	ut_asserteq(11, tline->tin.cls.num);
	ut_asserteq(15, tline->tin.cls.eol_num);
	ut_asserteq_str("sample hopwinda", abuf_data(&tline->tin.buf));
	ut_assertok(scene_arrange(scn));
	ut_assertok(expo_render(exp));
	ut_asserteq(17809, video_compress_fb(uts, dev, false));

	/* delete a character at the cursor */
	ut_assertok(expo_send_key(exp, CTL_CH('d')));

	/* check character deleted at cursor position */
	ut_asserteq(11, tline->tin.cls.num);
	ut_asserteq(14, tline->tin.cls.eol_num);
	ut_asserteq_str("sample hopwnda", abuf_data(&tline->tin.buf));
	ut_assertok(scene_arrange(scn));
	ut_assertok(expo_render(exp));
	ut_asserteq(17756, video_compress_fb(uts, dev, false));

	/* close the textline with Enter (BKEY_SELECT) */
	ut_assertok(expo_send_key(exp, BKEY_SELECT));
	ut_assertok(expo_action_get(exp, &act));
	ut_asserteq(EXPOACT_CLOSE, act.type);
	ut_asserteq(OBJ_TEXTLINE, act.select.id);
	ut_assertok(scene_set_open(scn, act.select.id, false));

	/* check the textline is closed and text was saved */
	ut_asserteq(0, tline->obj.flags & SCENEOF_OPEN);
	ut_asserteq_str("sample hopwnda", abuf_data(&tline->tin.buf));
	ut_assertok(scene_arrange(scn));
	ut_assertok(expo_render(exp));
	ut_asserteq(17757, video_compress_fb(uts, dev, false));

	abuf_uninit(&buf);
	abuf_uninit(&logo_copy);

	expo_exit_mode(exp);
	expo_destroy(exp);

	return 0;
}
BOOTSTD_TEST(expo_render_textline, UTF_DM | UTF_SCAN_FDT | UTF_NO_SILENT);

/* Check rendering a textedit */
static int expo_render_textedit(struct unit_test_state *uts)
{
	struct scene_obj_txtedit *ted;
	struct scene_obj_menu *menu;
	struct vidconsole_ctx *ctx;
	struct abuf buf, logo_copy;
	struct expo_action act;
	struct scene *scn;
	struct udevice *dev;
	struct expo *exp;
	int id;

	ut_assertok(create_test_expo(uts, &exp, &scn, &menu, &buf, &logo_copy));
	dev = exp->display;
	expo_enter_mode(exp);

	id = scene_texted(scn, "texted", OBJ_TEXTED, 40, &ted);
	ut_assert(id > 0);
	ut_assertok(scene_obj_set_bbox(scn, OBJ_TEXTED, 100, 200, 400, 300));

	/* create the label text object */
	id = scene_txt_str(scn, "ted-label", 0, 0, "Editor:", NULL);
	ut_assert(id > 0);
	ted->tin.label_id = id;

	/* create the edit text object pointing to the textedit buffer */
	abuf_printf(&ted->tin.buf, "This\nis the initial contents of the text "
		"editor but it is quite likely that more will be added later");
	id = scene_txt_str(scn, "ted-edit", STR_TEXTED, 0,
			   abuf_data(&ted->tin.buf), NULL);
	ut_assert(id > 0);
	ted->tin.edit_id = id;
	ut_assertok(scene_txt_set_font(scn, ted->tin.edit_id,
				       "nimbus_sans_l_regular", 20));

	expo_set_scene_id(exp, SCENE1);
	ut_assertok(scene_arrange(scn));
	ut_assertok(expo_render(exp));
	ut_asserteq(18932, video_compress_fb(uts, dev, false));

	/* highlight the textedit and re-render */
	scene_set_highlight_id(scn, OBJ_TEXTED);
	ut_assertok(scene_arrange(scn));
	ut_assertok(expo_render(exp));
	ut_asserteq(19851, video_compress_fb(uts, dev, false));

	/* open the textedit and re-render */
	ut_assertok(scene_set_open(scn, OBJ_TEXTED, true));
	ut_assertok(scene_arrange(scn));
	ut_assertok(expo_render(exp));

	/* the cursor should be at the end */
	ut_asserteq(100, ted->tin.cls.num);
	ut_asserteq(100, ted->tin.cls.eol_num);
	ctx = ted->tin.ctx;
	ut_asserteq(343, VID_TO_PIXEL(ctx->xcur_frac));
	ut_asserteq(260, ctx->ycur);
	ut_asserteq(19713, video_compress_fb(uts, dev, false));

	/* send a keypress to add a character */
	ut_assertok(expo_send_key(exp, 'X'));
	ut_asserteq(101, ted->tin.cls.num);
	ut_asserteq(101, ted->tin.cls.eol_num);
	ut_asserteq(353, VID_TO_PIXEL(ctx->xcur_frac));
	ut_asserteq(260, ctx->ycur);
	ut_assertok(scene_arrange(scn));
	ut_assertok(expo_render(exp));
	ut_asserteq(19776, video_compress_fb(uts, dev, false));

	ut_assertok(expo_send_key(exp, CTL_CH('b')));
	ut_assertok(expo_send_key(exp, CTL_CH('b')));
	ut_assertok(expo_send_key(exp, CTL_CH('b')));

	/* check cursor moved back three positions, before 'e' */
	ut_asserteq(98, ted->tin.cls.num);
	ut_asserteq(101, ted->tin.cls.eol_num);
	ut_assertok(scene_arrange(scn));
	ut_assertok(expo_render(exp));
	/* check cursor position after render (render_deps corrects it) */
	ut_asserteq(329, VID_TO_PIXEL(ctx->xcur_frac));
	ut_asserteq(260, ctx->ycur);
	ut_asserteq(19811, video_compress_fb(uts, dev, false));

	/* delete a character at the cursor (removes 'e') */
	ut_assertok(expo_send_key(exp, CTL_CH('d')));

	/* check character deleted at cursor position */
	ut_asserteq(98, ted->tin.cls.num);
	ut_asserteq(100, ted->tin.cls.eol_num);
	ut_assertok(scene_arrange(scn));
	ut_assertok(expo_render(exp));
	/* check cursor position after render (render_deps corrects it) */
	ut_asserteq(329, VID_TO_PIXEL(ctx->xcur_frac));
	ut_asserteq(260, ctx->ycur);
	ut_asserteq(19742, video_compress_fb(uts, dev, false));

	/* move cursor to previous visual line at same x position */
	ut_assertok(expo_send_key(exp, CTL_CH('p')));
	ut_asserteq(67, ted->tin.cls.num);
	ut_asserteq(100, ted->tin.cls.eol_num);
	ut_asserteq(328, VID_TO_PIXEL(ctx->xcur_frac));
	ut_asserteq(240, ctx->ycur);
	ut_assertok(scene_arrange(scn));
	ut_assertok(expo_render(exp));
	ut_asserteq(327, VID_TO_PIXEL(ctx->xcur_frac));
	ut_asserteq(240, ctx->ycur);
	ut_asserteq(19764, video_compress_fb(uts, dev, false));

	/* delete the word before cursor (deletes "lik" from "likely") */
	ut_assertok(expo_send_key(exp, CTL_CH('w')));
	ut_asserteq(64, ted->tin.cls.num);
	ut_asserteq(97, ted->tin.cls.eol_num);
	ut_assertok(scene_arrange(scn));
	ut_assertok(expo_render(exp));
	ut_asserteq(19673, video_compress_fb(uts, dev, false));

	/* delete another word (deletes "quite ") */
	ut_assertok(expo_send_key(exp, CTL_CH('w')));
	ut_asserteq(58, ted->tin.cls.num);
	ut_asserteq(91, ted->tin.cls.eol_num);
	ut_assertok(scene_arrange(scn));
	ut_assertok(expo_render(exp));
	ut_asserteq(19419, video_compress_fb(uts, dev, false));

	/* go back 4 characters */
	ut_assertok(expo_send_key(exp, CTL_CH('b')));
	ut_assertok(expo_send_key(exp, CTL_CH('b')));
	ut_assertok(expo_send_key(exp, CTL_CH('b')));
	ut_assertok(expo_send_key(exp, CTL_CH('b')));
	ut_asserteq(54, ted->tin.cls.num);
	ut_asserteq(91, ted->tin.cls.eol_num);
	ut_assertok(scene_arrange(scn));
	ut_assertok(expo_render(exp));
	ut_asserteq(19406, video_compress_fb(uts, dev, false));

	/* move cursor to next visual line at same x position */
	ut_assertok(expo_send_key(exp, CTL_CH('n')));
	ut_asserteq(87, ted->tin.cls.num);
	ut_asserteq(91, ted->tin.cls.eol_num);
	ut_assertok(scene_arrange(scn));
	ut_assertok(expo_render(exp));
	ut_asserteq(19427, video_compress_fb(uts, dev, false));

	/* go to start of line (multiline Home goes to start of current line) */
	ut_assertok(expo_send_key(exp, CTL_CH('a')));
	ut_asserteq(5, ted->tin.cls.num);
	ut_asserteq(91, ted->tin.cls.eol_num);
	ut_assertok(expo_send_key(exp, CTL_CH('d')));
	ut_asserteq(5, ted->tin.cls.num);
	ut_asserteq(90, ted->tin.cls.eol_num);
	ut_assertok(scene_arrange(scn));
	ut_assertok(expo_render(exp));
	ut_asserteq(19374, video_compress_fb(uts, dev, false));

	/* go to end of buffer and backspace */
	ut_assertok(expo_send_key(exp, CTL_CH('e')));
	ut_asserteq(90, ted->tin.cls.num);
	ut_asserteq(90, ted->tin.cls.eol_num);
	ut_assertok(expo_send_key(exp, '\x7f'));
	ut_asserteq(89, ted->tin.cls.num);
	ut_asserteq(89, ted->tin.cls.eol_num);
	ut_assertok(scene_arrange(scn));
	ut_assertok(expo_render(exp));
	ut_asserteq(19270, video_compress_fb(uts, dev, false));

	/* set multiline mode and check Enter inserts newline */
	ted->obj.flags |= SCENEOF_MULTILINE;
	ut_assertok(expo_send_key(exp, BKEY_SELECT));
	ut_asserteq(90, ted->tin.cls.num);
	ut_asserteq(90, ted->tin.cls.eol_num);
	ut_assert(ted->obj.flags & SCENEOF_OPEN);
	ut_asserteq('\n', ((char *)abuf_data(&ted->tin.buf))[89]);
	ut_assertok(scene_arrange(scn));
	ut_assertok(expo_render(exp));
	ut_asserteq(19318, video_compress_fb(uts, dev, false));

	/* go back 5 characters (before the newline) and use Ctrl+K */
	ut_assertok(expo_send_key(exp, CTL_CH('b')));
	ut_assertok(expo_send_key(exp, CTL_CH('b')));
	ut_assertok(expo_send_key(exp, CTL_CH('b')));
	ut_assertok(expo_send_key(exp, CTL_CH('b')));
	ut_assertok(expo_send_key(exp, CTL_CH('b')));
	ut_asserteq(85, ted->tin.cls.num);
	ut_asserteq(90, ted->tin.cls.eol_num);

	/* Ctrl+K in multiline mode should only delete to the newline */
	ut_assertok(expo_send_key(exp, CTL_CH('k')));
	ut_asserteq(85, ted->tin.cls.num);
	ut_asserteq(86, ted->tin.cls.eol_num);
	ut_asserteq('\n', ((char *)abuf_data(&ted->tin.buf))[85]);

	/* Ctrl+Y yanks back the killed text "latr" */
	ut_assertok(expo_send_key(exp, CTL_CH('y')));
	ut_asserteq(89, ted->tin.cls.num);
	ut_asserteq(90, ted->tin.cls.eol_num);
	ut_asserteq('\n', ((char *)abuf_data(&ted->tin.buf))[89]);

	/* Ctrl+Z undoes the yank */
	ut_assertok(expo_send_key(exp, CTL_CH('z')));
	ut_asserteq(85, ted->tin.cls.num);
	ut_asserteq(86, ted->tin.cls.eol_num);

	/* Ctrl+Shift+Z (internal code 'g') redoes the yank */
	ut_assertok(expo_send_key(exp, CTL_CH('g')));
	ut_asserteq(89, ted->tin.cls.num);
	ut_asserteq(90, ted->tin.cls.eol_num);

	/* clear multiline mode, close the textedit with Enter (BKEY_SELECT) */
	ted->obj.flags &= ~SCENEOF_MULTILINE;
	ut_assertok(expo_send_key(exp, BKEY_SELECT));
	ut_assertok(expo_action_get(exp, &act));
	ut_asserteq(EXPOACT_CLOSE, act.type);
	ut_asserteq(OBJ_TEXTED, act.select.id);
	ut_assertok(scene_set_open(scn, act.select.id, false));

	/* check the textedit is closed and text is changed (redo restored latr) */
	ut_asserteq(0, ted->obj.flags & SCENEOF_OPEN);
	ut_asserteq_str("This\ns the initial contents of the text "
		"editor but it is ely that more will be added latr\n",
		abuf_data(&ted->tin.buf));
	ut_assertok(scene_arrange(scn));
	ut_assertok(expo_render(exp));
	ut_asserteq(19428, video_compress_fb(uts, dev, false));

	abuf_uninit(&buf);
	abuf_uninit(&logo_copy);

	expo_exit_mode(exp);
	expo_destroy(exp);

	return 0;
}
BOOTSTD_TEST(expo_render_textedit, UTF_DM | UTF_SCAN_FDT | UTF_NO_SILENT);
