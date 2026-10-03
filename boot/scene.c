// SPDX-License-Identifier: GPL-2.0+
/*
 * Implementation of a scene, a collection of text/image/menu items in an expo
 *
 * Copyright 2022 Google LLC
 * Written by Simon Glass <sjg@chromium.org>
 */

#define LOG_CATEGORY	LOGC_EXPO

#include <alist.h>
#include <dm.h>
#include <env.h>
#include <expo.h>
#include <malloc.h>
#include <mapmem.h>
#include <menu.h>
#include <video.h>
#include <video_console.h>
#include <linux/input.h>
#include "scene_internal.h"

static const char *const scene_flag_names[] = {
	"hide",
	"point",
	"open",
	"size_valid",
	"sync_pos",
	"sync_size",
	"sync_width",
	"sync_bbox",
	"manual",
	"dirty",
};

static const char *const scene_obj_type_names[] = {
	"none",
	"image",
	"text",
	"box",
	"menu",
	"textline",
	"textedit",
};

bool scene_chklog(const char *name)
{
	const char *filter, *end, *p;
	int len;

	if (!IS_ENABLED(CONFIG_EXPO_LOG_FILTER))
		return true;

	filter = env_get("expo_log_filter");
	if (!filter)
		return true;

	/* Check each comma-separated filter */
	while (*filter) {
		end = strchrnul(filter, ',');
		len = end - filter;

		/* Check if this filter segment appears in name */
		for (p = name; *p; p++) {
			if (!strncmp(p, filter, len))
				return true;
		}

		if (!*end)
			break;
		filter = end + 1;
	}

	return false;
}

int scene_new(struct expo *exp, const char *name, uint id, struct scene **scnp)
{
	struct scene *scn;

	scn = calloc(1, sizeof(struct scene));
	if (!scn)
		return log_msg_ret("expo", -ENOMEM);
	scn->name = strdup(name);
	if (!scn->name) {
		free(scn);
		return log_msg_ret("name", -ENOMEM);
	}

	if (!abuf_init_size(&scn->buf, EXPO_MAX_CHARS + 1)) {
		free(scn->name);
		free(scn);
		return log_msg_ret("buf", -ENOMEM);
	}

	INIT_LIST_HEAD(&scn->obj_head);
	scn->id = resolve_id(exp, id);
	scn->expo = exp;
	list_add_tail(&scn->sibling, &exp->scene_head);

	*scnp = scn;

	return scn->id;
}

void scene_obj_destroy(struct scene_obj *obj)
{
	if (obj->type == SCENEOBJT_MENU)
		scene_menu_destroy((struct scene_obj_menu *)obj);
	else if (obj->type == SCENEOBJT_TEXTLINE ||
		 obj->type == SCENEOBJT_TEXTEDIT)
		scene_txtin_destroy(obj->scene, scene_obj_txtin(obj));
	if (obj->type == SCENEOBJT_TEXT)
		alist_uninit(&((struct scene_obj_txt *)obj)->gen.lines);
	free(obj->name);
	free(obj);
}

void scene_destroy(struct scene *scn)
{
	struct scene_obj *obj, *next;

	list_for_each_entry_safe(obj, next, &scn->obj_head, sibling)
		scene_obj_destroy(obj);

	abuf_uninit(&scn->buf);
	free(scn->name);
	free(scn);
}

int scene_obj_count(struct scene *scn)
{
	return list_count_nodes(&scn->obj_head);
}

void *scene_obj_find(const struct scene *scn, uint id, enum scene_obj_t type)
{
	struct scene_obj *obj;

	list_for_each_entry(obj, &scn->obj_head, sibling) {
		if (obj->id == id &&
		    (type == SCENEOBJT_NONE || obj->type == type))
			return obj;
	}

	return NULL;
}

void *scene_obj_find_by_name(struct scene *scn, const char *name)
{
	struct scene_obj *obj;

	list_for_each_entry(obj, &scn->obj_head, sibling) {
		if (!strcmp(name, obj->name))
			return obj;
	}

	return NULL;
}

int scene_obj_add(struct scene *scn, const char *name, uint id,
		  enum scene_obj_t type, uint size, struct scene_obj **objp)
{
	struct scene_obj *obj;

	obj = calloc(1, size);
	if (!obj)
		return log_msg_ret("obj", -ENOMEM);
	obj->name = strdup(name);
	if (!obj->name) {
		free(obj);
		return log_msg_ret("name", -ENOMEM);
	}

	obj->id = resolve_id(scn->expo, id);
	obj->scene = scn;
	obj->type = type;
	list_add_tail(&obj->sibling, &scn->obj_head);
	*objp = obj;

	return obj->id;
}

int scene_img(struct scene *scn, const char *name, uint id, char *data,
	      struct scene_obj_img **imgp)
{
	struct scene_obj_img *img;
	int ret;

	ret = scene_obj_add(scn, name, id, SCENEOBJT_IMAGE,
			    sizeof(struct scene_obj_img),
			    (struct scene_obj **)&img);
	if (ret < 0)
		return log_msg_ret("obj", ret);

	img->data = data;

	if (imgp)
		*imgp = img;

	return img->obj.id;
}

int scene_txt_generic_init(struct expo *exp, struct scene_txt_generic *gen,
			   const char *name, uint str_id, const char *str)
{
	int ret;

	if (str) {
		ret = expo_str(exp, name, str_id, str);
		if (ret < 0)
			return log_msg_ret("str", ret);
		if (str_id && ret != str_id)
			return log_msg_ret("id", -EEXIST);
		str_id = ret;
	} else {
		ret = resolve_id(exp, str_id);
		if (ret < 0)
			return log_msg_ret("nst", ret);
		if (str_id && ret != str_id)
			return log_msg_ret("nid", -EEXIST);
	}

	gen->str_id = str_id;
	alist_init_struct(&gen->lines, struct vidconsole_mline);

	return 0;
}

int scene_txt(struct scene *scn, const char *name, uint id, uint str_id,
	      struct scene_obj_txt **txtp)
{
	struct scene_obj_txt *txt;
	int ret;

	ret = scene_obj_add(scn, name, id, SCENEOBJT_TEXT,
			    sizeof(struct scene_obj_txt),
			    (struct scene_obj **)&txt);
	if (ret < 0)
		return log_msg_ret("obj", ret);

	ret = scene_txt_generic_init(scn->expo, &txt->gen, name, str_id, NULL);
	if (ret)
		return log_msg_ret("stg", ret);
	if (txtp)
		*txtp = txt;

	return txt->obj.id;
}

int scene_txt_str(struct scene *scn, const char *name, uint id, uint str_id,
		  const char *str, struct scene_obj_txt **txtp)
{
	struct scene_obj_txt *txt;
	int ret;

	ret = scene_obj_add(scn, name, id, SCENEOBJT_TEXT,
			    sizeof(struct scene_obj_txt),
			    (struct scene_obj **)&txt);
	if (ret < 0)
		return log_msg_ret("obj", ret);

	ret = scene_txt_generic_init(scn->expo, &txt->gen, name, str_id, str);
	if (ret)
		return log_msg_ret("tsg", ret);
	if (txtp)
		*txtp = txt;

	return txt->obj.id;
}

int scene_box(struct scene *scn, const char *name, uint id, uint width,
	      bool fill, struct scene_obj_box **boxp)
{
	struct scene_obj_box *box;
	int ret;

	ret = scene_obj_add(scn, name, id, SCENEOBJT_BOX,
			    sizeof(struct scene_obj_box),
			    (struct scene_obj **)&box);
	if (ret < 0)
		return log_msg_ret("obj", ret);

	box->width = width;
	box->fill = fill;

	if (boxp)
		*boxp = box;

	return box->obj.id;
}

int scene_box_set_fill(struct scene *scn, uint id, bool fill)
{
	struct scene_obj_box *box;

	box = scene_obj_find(scn, id, SCENEOBJT_BOX);
	if (!box)
		return log_msg_ret("find", -ENOENT);

	box->fill = fill;

	return 0;
}

int scene_txt_set_font(struct scene *scn, uint id, const char *font_name,
		       uint font_size)
{
	struct scene_obj_txt *txt;

	txt = scene_obj_find(scn, id, SCENEOBJT_TEXT);
	if (!txt)
		return log_msg_ret("find", -ENOENT);
	txt->gen.font_name = font_name;
	txt->gen.font_size = font_size;

	return 0;
}

int scene_obj_set_pos(struct scene *scn, uint id, int x, int y)
{
	struct scene_obj *obj;
	int w, h;

	obj = scene_obj_find(scn, id, SCENEOBJT_NONE);
	if (!obj)
		return log_msg_ret("find", -ENOENT);
	w = obj->req_bbox.x1 - obj->req_bbox.x0;
	h = obj->req_bbox.y1 - obj->req_bbox.y0;
	obj->req_bbox.x0 = x;
	obj->req_bbox.y0 = y;
	obj->req_bbox.x1 = obj->req_bbox.x0 + w;
	obj->req_bbox.y1 = obj->req_bbox.y0 + h;
	obj->flags |= SCENEOF_SYNC_POS;

	return 0;
}

int scene_obj_set_size(struct scene *scn, uint id, int w, int h)
{
	struct scene_obj *obj;

	obj = scene_obj_find(scn, id, SCENEOBJT_NONE);
	if (!obj)
		return log_msg_ret("find", -ENOENT);
	log_debug("obj %s w %d h %d\n", obj->name, w, h);
	obj->req_bbox.x1 = obj->req_bbox.x0 + w;
	obj->req_bbox.y1 = obj->req_bbox.y0 + h;
	obj->flags |= SCENEOF_SIZE_VALID | SCENEOF_SYNC_SIZE;

	return 0;
}

int scene_obj_set_width_flags(struct scene *scn, uint id, int w, uint flags)
{
	struct scene_obj *obj;

	obj = scene_obj_find(scn, id, SCENEOBJT_NONE);
	if (!obj)
		return log_msg_ret("find", -ENOENT);
	obj->req_bbox.x1 = obj->req_bbox.x0 + w;
	obj->flags |= flags;

	return 0;
}

int scene_obj_set_width(struct scene *scn, uint id, int w)
{
	return scene_obj_set_width_flags(scn, id, w, SCENEOF_SYNC_WIDTH);
}

int scene_obj_set_bbox(struct scene *scn, uint id, int x0, int y0, int x1,
		       int y1)
{
	struct scene_obj *obj;

	obj = scene_obj_find(scn, id, SCENEOBJT_NONE);
	if (!obj)
		return log_msg_ret("find", -ENOENT);
	obj->req_bbox.x0 = x0;
	obj->req_bbox.y0 = y0;
	obj->req_bbox.x1 = x1;
	obj->req_bbox.y1 = y1;
	obj->flags |= SCENEOF_SIZE_VALID | SCENEOF_SYNC_BBOX;

	return 0;
}

int scene_obj_set_halign(struct scene *scn, uint id, enum scene_obj_align aln)
{
	struct scene_obj *obj;

	obj = scene_obj_find(scn, id, SCENEOBJT_NONE);
	if (!obj)
		return log_msg_ret("osh", -ENOENT);
	obj->horiz = aln;

	return 0;
}

int scene_obj_set_valign(struct scene *scn, uint id, enum scene_obj_align aln)
{
	struct scene_obj *obj;

	obj = scene_obj_find(scn, id, SCENEOBJT_NONE);
	if (!obj)
		return log_msg_ret("osv", -ENOENT);
	obj->vert = aln;

	return 0;
}

int scene_obj_set_hide(struct scene *scn, uint id, bool hide)
{
	int ret;

	ret = scene_obj_flag_clrset(scn, id, SCENEOF_HIDE,
				    hide ? SCENEOF_HIDE : 0);
	if (ret)
		return log_msg_ret("flg", ret);

	return 0;
}

int scene_obj_set_manual(struct scene *scn, uint id, bool manual)
{
	int ret;

	ret = scene_obj_flag_clrset(scn, id, SCENEOF_MANUAL,
				    manual ? SCENEOF_MANUAL : 0);
	if (ret)
		return log_msg_ret("fla", ret);

	return 0;
}

int scene_obj_flag_clrset(struct scene *scn, uint id, uint clr, uint set)
{
	struct scene_obj *obj;

	obj = scene_obj_find(scn, id, SCENEOBJT_NONE);
	if (!obj)
		return log_msg_ret("find", -ENOENT);
	obj->flags &= ~clr;
	obj->flags |= set;

	return 0;
}

static void handle_alignment(enum scene_obj_align horiz,
			     enum scene_obj_align vert,
			     struct vid_bbox *bbox,
			     struct scene_obj_dims *dims,
			     int xsize, int ysize,
			     struct scene_obj_offset *offset)
{
	int width, height;

	if (bbox->x1 == SCENEOB_DISPLAY_MAX)
		bbox->x1 = xsize ?: 1280;
	if (bbox->y1 == SCENEOB_DISPLAY_MAX)
		bbox->y1 = ysize ?: 1024;

	width = bbox->x1 - bbox->x0;
	height = bbox->y1 - bbox->y0;

	switch (horiz) {
	case SCENEOA_CENTRE:
		offset->xofs = (width - dims->x) / 2;
		break;
	case SCENEOA_RIGHT:
		offset->xofs = width - dims->x;
		break;
	case SCENEOA_LEFT:
		offset->xofs = 0;
		break;
	}

	switch (vert) {
	case SCENEOA_CENTRE:
		offset->yofs = (height - dims->y) / 2;
		break;
	case SCENEOA_BOTTOM:
		offset->yofs = height - dims->y;
		break;
	case SCENEOA_TOP:
	default:
		offset->yofs = 0;
		break;
	}
}

int scene_obj_get_hw(struct scene *scn, uint id, int *widthp)
{
	struct scene_obj *obj;

	obj = scene_obj_find(scn, id, SCENEOBJT_NONE);
	if (!obj)
		return log_msg_ret("find", -ENOENT);

	switch (obj->type) {
	case SCENEOBJT_NONE:
	case SCENEOBJT_MENU:
	case SCENEOBJT_TEXTLINE:
	case SCENEOBJT_BOX:
	case SCENEOBJT_TEXTEDIT:
		break;
	case SCENEOBJT_IMAGE: {
		struct scene_obj_img *img = (struct scene_obj_img *)obj;
		ulong width, height;
		uint bpix;

		video_bmp_get_info(img->data, &width, &height, &bpix);
		if (widthp)
			*widthp = width;
		return height;
	}
	case SCENEOBJT_TEXT: {
		struct scene_txt_generic *gen;
		struct expo *exp = scn->expo;
		struct vidconsole_bbox bbox;
		int len, ret, limit;
		const char *str;

		gen = &((struct scene_obj_txt *)obj)->gen;

		str = expo_get_str(exp, gen->str_id);
		if (!str)
			return log_msg_ret("str", -ENOENT);
		len = strlen(str);

		/* if there is no console, make it up */
		if (!exp->cons) {
			if (widthp)
				*widthp = 8 * len;
			return 16;
		}

		limit = obj->flags & SCENEOF_SIZE_VALID ?
			obj->req_bbox.x1 - obj->req_bbox.x0 : -1;

		ret = vidconsole_measure(scn->expo->cons, gen->font_name,
					 gen->font_size, str, -1, limit, &bbox,
					 &gen->lines);
		if (ret)
			return log_msg_ret("mea", ret);
		if (scene_chklog(obj->name)) {
			log_debug("obj %s limit %d: %d lines, width %d height %d\n",
				  obj->name, limit, gen->lines.count,
				  bbox.x1, bbox.y1);
			for (int i = 0; i < gen->lines.count; i++) {
				const struct vidconsole_mline *mline;

				mline = alist_get(&gen->lines, i,
						  struct vidconsole_mline);
				log_content("line %d: %d,%d %d,%d '%.*s'\n", i,
					    mline->bbox.x0, mline->bbox.y0,
					    mline->bbox.x1, mline->bbox.y1,
					    mline->len, str + mline->start);
			}
		}
		if (widthp)
			*widthp = bbox.x1;

		return bbox.y1;
	}
	}

	return 0;
}

/**
 * scene_render_background() - Render the background for an object
 *
 * @obj: Object to render
 * @box_only: true to show a box around the object, but keep the normal
 * background colour inside
 * @cur_item: true to render the background only for the current menu item
 */
static void scene_render_background(struct scene_obj *obj, bool box_only,
				    bool cur_item)
{
	struct vidconsole_bbox bbox[SCENEBB_count], *sel;
	struct expo *exp = obj->scene->expo;
	const struct expo_theme *theme = &exp->theme;
	struct udevice *dev = exp->display;
	struct video_priv *vid_priv;
	struct udevice *cons = exp->cons;
	struct vidconsole_colour old;
	enum colour_idx fore, back;
	uint inset = theme->menu_inset;

	vid_priv = dev_get_uclass_priv(dev);
	/* draw a background for the object */
	if (vid_priv->white_on_black) {
		fore = VID_DARK_GREY;
		back = VID_WHITE;
	} else {
		fore = VID_LIGHT_GRAY;
		back = VID_BLACK;
	}

	/* see if this object wants to render a background */
	if (scene_obj_calc_bbox(obj, bbox))
		return;

	sel = cur_item ? &bbox[SCENEBB_curitem] : &bbox[SCENEBB_label];
	if (!sel->valid)
		return;

	vidconsole_push_colour(cons, fore, back, &old);
	video_fill_part(dev, sel->x0 - inset, sel->y0 - inset,
			sel->x1 + inset, sel->y1 + inset,
			vid_priv->colour_fg);
	vidconsole_pop_colour(cons, &old);
	if (box_only) {
		video_fill_part(dev, sel->x0, sel->y0, sel->x1, sel->y1,
				vid_priv->colour_bg);
	}
}

/**
 * draw_string() - Draw a string to the vidconsole
 *
 * @cons: Vidconsole device to draw to
 * @ctx: Vidconsole context, or NULL to use default
 * @str: String to draw
 * @len: Length of string to draw
 * @password: true to draw asterisks instead of actual characters
 */
static void draw_string(struct udevice *cons, void *ctx, const char *str,
			int len, bool password)
{
	if (password) {
		int i;

		for (i = 0; i < len; i++)
			vidconsole_put_char(cons, ctx, '*');
	} else {
		vidconsole_put_stringn(cons, ctx, str, len);
	}
}

/**
 * scene_txt_render() - Render text to the vidconsole
 *
 * @exp: Expo to use
 * @dev: Video device
 * @cons: Vidconsole device
 * @ctx: Vidconsole context, or NULL to use default
 * @obj: Object to render
 * @gen: Generic text info
 * @x: X position in pixels
 * @y: Y position in pixels
 * @menu_inset: Inset for menu items
 * Return: 0 if OK, -ve on error
 */
static int scene_txt_render(struct expo *exp, struct udevice *dev,
			    struct udevice *cons, void *ctx,
			    struct scene_obj *obj,
			    struct scene_txt_generic *gen, int x, int y,
			    int menu_inset)
{
	const struct vidconsole_mline *mline, *last;
	struct video_priv *vid_priv;
	struct vidconsole_colour old;
	enum colour_idx fore, back;
	struct scene_obj_dims dims;
	struct vid_bbox bbox;
	const char *str;
	int ret;

	if (!cons)
		return -ENOTSUPP;

	if (gen->font_name || gen->font_size) {
		ret = vidconsole_select_font(cons, ctx, gen->font_name,
					     gen->font_size);
	} else {
		ret = vidconsole_select_font(cons, ctx, NULL, 0);
	}
	if (ret && ret != -ENOSYS)
		return log_msg_ret("font", ret);
	str = expo_get_str(exp, gen->str_id);
	if (!str)
		return 0;

	vid_priv = dev_get_uclass_priv(dev);
	if (vid_priv->white_on_black) {
		fore = VID_BLACK;
		back = VID_WHITE;
	} else {
		fore = VID_LIGHT_GRAY;
		back = VID_BLACK;
	}

	if (obj->flags & SCENEOF_POINT) {
		int inset;

		inset = exp->popup ? menu_inset : 0;
		vidconsole_push_colour(cons, fore, back, &old);
		video_fill_part(dev, x - inset, y,
				obj->bbox.x1 + inset, obj->bbox.y1,
				vid_priv->colour_bg);
	}

	mline = alist_get(&gen->lines, 0, typeof(*mline));
	last = alist_get(&gen->lines, gen->lines.count - 1, typeof(*mline));
	if (mline)
		dims.y = last->bbox.y1 - mline->bbox.y0;
	bbox.y0 = obj->bbox.y0;
	bbox.y1 = obj->bbox.y1;

	if (!mline) {
		vidconsole_set_cursor_pos(cons, ctx, x, y);
		draw_string(cons, ctx, str, strlen(str),
			    obj->flags & SCENEOF_PASSWORD);
	}

	alist_for_each(mline, &gen->lines) {
		struct scene_obj_offset offset;

		bbox.x0 = obj->bbox.x0;
		bbox.x1 = obj->bbox.x1;
		dims.x = mline->bbox.x1 - mline->bbox.x0;
		handle_alignment(obj->horiz, obj->vert, &bbox, &dims,
				 obj->bbox.x1 - obj->bbox.x0,
				 obj->bbox.y1 - obj->bbox.y0, &offset);

		x = obj->bbox.x0 + offset.xofs;
		y = obj->bbox.y0 + offset.yofs + mline->bbox.y0;
		if (y > bbox.y1)
			break;	/* clip this line and any following */
		vidconsole_set_cursor_pos(cons, ctx, x, y);
		draw_string(cons, ctx, str + mline->start, mline->len,
			    obj->flags & SCENEOF_PASSWORD);
	}
	if (obj->flags & SCENEOF_POINT)
		vidconsole_pop_colour(cons, &old);

	return 0;
}

/**
 * scene_obj_render() - Render an object
 *
 * @obj: Object to render
 * @ctx: Vidconsole context, or NULL to use default
 * @text_mode: true to use text mode
 * Return: 0 if OK, -ve on error
 */
static int scene_obj_render(struct scene_obj *obj, void *ctx, bool text_mode)
{
	struct scene *scn = obj->scene;
	struct expo *exp = scn->expo;
	const struct expo_theme *theme = &exp->theme;
	struct udevice *dev = exp->display;
	struct udevice *cons = text_mode ? NULL : exp->cons;
	struct video_priv *vid_priv;
	int x, y, ret;

	y = obj->bbox.y0;
	x = obj->bbox.x0 + obj->ofs.xofs;
	vid_priv = dev_get_uclass_priv(dev);

	switch (obj->type) {
	case SCENEOBJT_NONE:
		break;
	case SCENEOBJT_IMAGE: {
		struct scene_obj_img *img = (struct scene_obj_img *)obj;

		if (!cons)
			return -ENOTSUPP;
		ret = video_bmp_display(dev, map_to_sysmem(img->data), x, y,
					true);
		if (ret < 0)
			return log_msg_ret("img", ret);
		break;
	}
	case SCENEOBJT_TEXT: {
		struct scene_obj_txt *txt = (struct scene_obj_txt *)obj;

		ret = scene_txt_render(exp, dev, cons, ctx, obj, &txt->gen,
				       x, y, theme->menu_inset);
		break;
	}
	case SCENEOBJT_MENU: {
		struct scene_obj_menu *menu = (struct scene_obj_menu *)obj;

		if (exp->popup) {
			if (obj->flags & SCENEOF_OPEN) {
				if (!cons)
					return -ENOTSUPP;

				/* draw a background behind the menu items */
				scene_render_background(obj, false, false);
			}
		} else if (exp->show_highlight) {
			/* do nothing */
		}

		/*
		 * With a vidconsole, the text and item pointer are rendered as
		 * normal objects so we don't need to do anything here. The menu
		 * simply controls where they are positioned.
		 */
		if (cons)
			return -ENOTSUPP;

		ret = scene_menu_display(menu);
		if (ret < 0)
			return log_msg_ret("img", ret);

		break;
	}
	case SCENEOBJT_TEXTLINE:
	case SCENEOBJT_TEXTEDIT:
		if (obj->flags & SCENEOF_OPEN)
			scene_render_background(obj, true, false);
		break;
	case SCENEOBJT_BOX: {
		struct scene_obj_box *box = (struct scene_obj_box *)obj;

		video_draw_box(dev, obj->bbox.x0, obj->bbox.y0, obj->bbox.x1,
			       obj->bbox.y1, box->width, vid_priv->colour_fg, box->fill);
		break;
	}
	}

	return 0;
}

int scene_calc_arrange(struct scene *scn, struct expo_arrange_info *arr)
{
	struct scene_obj *obj;

	arr->label_width = 0;
	list_for_each_entry(obj, &scn->obj_head, sibling) {
		uint label_id = 0;
		int width;

		switch (obj->type) {
		case SCENEOBJT_NONE:
		case SCENEOBJT_IMAGE:
		case SCENEOBJT_TEXT:
		case SCENEOBJT_BOX:
			break;
		case SCENEOBJT_MENU: {
			struct scene_obj_menu *menu;

			menu = (struct scene_obj_menu *)obj,
			label_id = menu->title_id;
			break;
		}
		case SCENEOBJT_TEXTLINE:
		case SCENEOBJT_TEXTEDIT:
			label_id = scene_obj_txtin(obj)->label_id;
			break;
		}

		if (label_id) {
			int ret;

			ret = scene_obj_get_hw(scn, label_id, &width);
			if (ret < 0)
				return log_msg_ret("hei", ret);
			arr->label_width = max(arr->label_width, width);
		}
	}

	return 0;
}

/**
 * scene_set_default_bbox() - Set a default for each object's size
 *
 * If there is not already a size, use the dims property to set one
 */
static int scene_set_default_bbox(struct scene *scn)
{
	struct scene_obj *obj;

	list_for_each_entry(obj, &scn->obj_head, sibling) {
		switch (obj->type) {
		case SCENEOBJT_IMAGE:
		case SCENEOBJT_TEXT:
			if (obj->flags & SCENEOF_SIZE_VALID)
				break;
			obj->req_bbox.x1 = obj->req_bbox.x0 + obj->dims.x;
			obj->req_bbox.y1 = obj->req_bbox.y0 + obj->dims.y;
			obj->flags |= SCENEOF_SYNC_SIZE;
			break;
		default:
			break;
		}
	}

	return 0;
}

int scene_arrange(struct scene *scn)
{
	struct expo_arrange_info arr;
	int xsize = 0, ysize = 0;
	struct scene_obj *obj;
	struct udevice *dev;
	int ret;

	dev = scn->expo->display;
	if (dev) {
		struct video_priv *priv = dev_get_uclass_priv(dev);

		xsize = priv->xsize;
		ysize = priv->ysize;
	}

	ret = scene_calc_dims(scn);
	if (ret)
		return log_msg_ret("scd", ret);
	ret = scene_set_default_bbox(scn);
	if (ret)
		return log_msg_ret("sce", ret);
	ret = scene_calc_arrange(scn, &arr);
	if (ret < 0)
		return log_msg_ret("arr", ret);

	list_for_each_entry(obj, &scn->obj_head, sibling) {
		handle_alignment(obj->horiz, obj->vert, &obj->bbox, &obj->dims,
				 xsize, ysize, &obj->ofs);

		if (obj->flags & SCENEOF_MANUAL)
			continue;
		switch (obj->type) {
		case SCENEOBJT_NONE:
		case SCENEOBJT_IMAGE:
		case SCENEOBJT_TEXT:
		case SCENEOBJT_BOX:
			break;
		case SCENEOBJT_MENU: {
			struct scene_obj_menu *menu;

			menu = (struct scene_obj_menu *)obj,
			ret = scene_menu_arrange(scn, &arr, menu);
			if (ret)
				return log_msg_ret("arr", ret);
			break;
		}
		case SCENEOBJT_TEXTLINE: {
			struct scene_obj_textline *tline;

			tline = (struct scene_obj_textline *)obj,
			ret = scene_textline_arrange(scn, &arr, tline);
			if (ret)
				return log_msg_ret("arr", ret);
			break;
		}
		case SCENEOBJT_TEXTEDIT: {
			struct scene_obj_txtedit *ted;

			ted = (struct scene_obj_txtedit *)obj,
			ret = scene_txted_arrange(scn, &arr, ted);
			if (ret)
				return log_msg_ret("arr", ret);
			break;
		}
		}
	}
	ret = scene_sync_bbox(scn);
	if (ret)
		return log_msg_ret("saf", ret);

	return 0;
}

int scene_render_obj(struct scene *scn, uint id, void *ctx)
{
	struct scene_obj *obj;
	int ret;

	obj = scene_obj_find(scn, id, SCENEOBJT_NONE);
	if (!obj)
		return log_msg_ret("obj", -ENOENT);

	if (!(obj->flags & SCENEOF_HIDE)) {
		ret = scene_obj_render(obj, ctx, false);
		if (ret && ret != -ENOTSUPP)
			return log_msg_ret("ren", ret);
	}

	return 0;
}

int scene_render_deps(struct scene *scn, uint id)
{
	struct scene_obj *obj;
	int ret;

	if (!id)
		return 0;
	obj = scene_obj_find(scn, id, SCENEOBJT_NONE);
	if (!obj)
		return log_msg_ret("obj", -ENOENT);

	if (!(obj->flags & SCENEOF_HIDE)) {
		ret = scene_obj_render(obj, NULL, false);
		if (ret && ret != -ENOTSUPP)
			return log_msg_ret("ren", ret);

		switch (obj->type) {
		case SCENEOBJT_NONE:
		case SCENEOBJT_IMAGE:
		case SCENEOBJT_TEXT:
		case SCENEOBJT_BOX:
			break;
		case SCENEOBJT_MENU:
			scene_menu_render_deps(scn,
					       (struct scene_obj_menu *)obj);
			break;
		case SCENEOBJT_TEXTLINE:
		case SCENEOBJT_TEXTEDIT:
			scene_txtin_render_deps(scn, obj, scene_obj_txtin(obj));
			break;
		}
	}

	return 0;
}

/**
 * scene_get_dirty_bbox() - Get bounding box of all dirty objects in a scene
 *
 * @scn: Scene to scan
 * @bbox: Returns bounding box of all dirty objects
 * Return: 0 if dirty objects found, -ENOENT if no dirty objects
 */
static int scene_get_dirty_bbox(struct scene *scn, struct vid_bbox *bbox)
{
	struct scene_obj *obj;
	bool found_dirty = false;

	list_for_each_entry(obj, &scn->obj_head, sibling) {
		if (obj->flags & SCENEOF_DIRTY) {
			if (!found_dirty) {
				/* First dirty object - initialize bbox */
				*bbox = obj->bbox;
				found_dirty = true;
			} else {
				/* Expand bbox to include this object */
				if (obj->bbox.x0 < bbox->x0)
					bbox->x0 = obj->bbox.x0;
				if (obj->bbox.y0 < bbox->y0)
					bbox->y0 = obj->bbox.y0;
				if (obj->bbox.x1 > bbox->x1)
					bbox->x1 = obj->bbox.x1;
				if (obj->bbox.y1 > bbox->y1)
					bbox->y1 = obj->bbox.y1;
			}
		}
	}

	return found_dirty ? 0 : -ENOENT;
}

/**
 * bbox_intersects() - Check if two bounding boxes intersect
 *
 * @bbox1: First bounding box
 * @bbox2: Second bounding box
 * Return: true if bounding boxes intersect, false otherwise
 */
static bool bbox_intersects(const struct vid_bbox *bbox1,
			    const struct vid_bbox *bbox2)
{
	return !(bbox1->x1 <= bbox2->x0 || bbox2->x1 <= bbox1->x0 ||
		 bbox1->y1 <= bbox2->y0 || bbox2->y1 <= bbox1->y0);
}

int scene_render(struct scene *scn, bool dirty_only)
{
	struct expo *exp = scn->expo;
	struct scene_obj *obj;
	struct vid_bbox dirty_bbox;
	int ret;

	/* Get bounding box of dirty objects and add to expo damage */
	ret = scene_get_dirty_bbox(scn, &dirty_bbox);
	if (!ret)
		expo_damage_add(exp, &dirty_bbox);

	list_for_each_entry(obj, &scn->obj_head, sibling) {
		bool render = true;

		if (obj->flags & SCENEOF_HIDE)
			continue;

		/* render objects that intersect with dirty bbox */
		if (dirty_only && !ret)
			render = bbox_intersects(&obj->bbox, &dirty_bbox);

		if (render) {
			ret = scene_obj_render(obj, NULL, exp->text_mode);
			if (ret && ret != -ENOTSUPP)
				return log_msg_ret("ren", ret);
		}
	}

	/* render any highlighted object on top of the others */
	if (scn->highlight_id && !exp->text_mode) {
		ret = scene_render_deps(scn, scn->highlight_id);
		if (ret && ret != -ENOTSUPP)
			return log_msg_ret("dep", ret);
	}

	return 0;
}

/**
 * send_key_obj() - Handle a keypress for moving between objects
 *
 * @scn: Scene to receive the key
 * @obj: Object to receive the key
 * @key: Key to send (KEYCODE_UP)
 * @event: Returns resulting event from this keypress
 * Returns: 0 if OK, -ve on error
 */
static void send_key_obj(struct scene *scn, struct scene_obj *obj, int key,
			 struct expo_action *event)
{
	switch (key) {
	case BKEY_UP:
		while (obj != list_first_entry(&scn->obj_head, struct scene_obj,
					       sibling)) {
			obj = list_entry(obj->sibling.prev,
					 struct scene_obj, sibling);
			if (scene_obj_can_highlight(obj)) {
				event->type = EXPOACT_POINT_OBJ;
				event->select.id = obj->id;
				log_debug("up to obj %d\n", event->select.id);
				break;
			}
		}
		break;
	case BKEY_DOWN:
		while (!list_is_last(&obj->sibling, &scn->obj_head)) {
			obj = list_entry(obj->sibling.next, struct scene_obj,
					 sibling);
			if (scene_obj_can_highlight(obj)) {
				event->type = EXPOACT_POINT_OBJ;
				event->select.id = obj->id;
				log_debug("down to obj %d\n", event->select.id);
				break;
			}
		}
		break;
	case BKEY_SELECT:
		if (scene_obj_can_highlight(obj)) {
			event->type = EXPOACT_OPEN;
			event->select.id = obj->id;
			log_debug("open obj %d\n", event->select.id);
		}
		break;
	case BKEY_QUIT:
		event->type = EXPOACT_QUIT;
		log_debug("obj quit\n");
		break;
	}
}

int scene_send_key(struct scene *scn, int key, struct expo_action *event)
{
	struct scene_obj *cur, *obj;
	int ret;

	event->type = EXPOACT_NONE;

	/*
	 * In 'popup' mode, arrow keys move betwen objects, unless a menu or
	 * textline is opened
	 */
	cur = NULL;
	if (scn->highlight_id)
		cur = scene_obj_find(scn, scn->highlight_id, SCENEOBJT_NONE);
	if (scn->expo->popup) {
		if (!cur)
			return 0;

		if (!(cur->flags & SCENEOF_OPEN)) {
			send_key_obj(scn, cur, key, event);
			return 0;
		}

		switch (cur->type) {
		case SCENEOBJT_NONE:
		case SCENEOBJT_IMAGE:
		case SCENEOBJT_TEXT:
		case SCENEOBJT_BOX:
			break;
		case SCENEOBJT_MENU: {
			struct scene_obj_menu *menu;

			menu = (struct scene_obj_menu *)cur,
			ret = scene_menu_send_key(scn, menu, key, event);
			if (ret)
				return log_msg_ret("key", ret);
			break;
		}
		case SCENEOBJT_TEXTLINE:
		case SCENEOBJT_TEXTEDIT:
			ret = scene_txtin_send_key(cur, scene_obj_txtin(cur),
						   key, event);
			if (ret)
				return log_msg_ret("key", ret);
			break;
		}
		return 0;
	}

	if (cur && (cur->type == SCENEOBJT_TEXTLINE ||
		    cur->type == SCENEOBJT_TEXTEDIT)) {
		ret = scene_txtin_send_key(cur, scene_obj_txtin(cur),
					   key, event);
		if (ret)
			return log_msg_ret("key", ret);
		return 0;
	}

	list_for_each_entry(obj, &scn->obj_head, sibling) {
		if (obj->type == SCENEOBJT_MENU) {
			struct scene_obj_menu *menu;

			menu = (struct scene_obj_menu *)obj,
			ret = scene_menu_send_key(scn, menu, key, event);
			if (ret)
				return log_msg_ret("key", ret);
			break;
		}
	}

	return 0;
}

/**
 * is_within() - check if a point is considered within an object ID
 *
 * @scn: Scene to check
 * @obj: object to check
 * @x: X coordinate of the point
 * @y: Y coordinate of the point
 * Return: true if the point is considered within the object, false if not
 */
static bool is_within(const struct scene_obj *obj, int x, int y)
{
	/* Check if point (x, y) is within object's bounding box */
	return (x >= obj->bbox.x0 && x <= obj->bbox.x1 &&
		y >= obj->bbox.y0 && y <= obj->bbox.y1);
}

bool scene_within(const struct scene *scn, uint id, int x, int y)
{
	struct scene_obj *obj;

	obj = scene_obj_find(scn, id, SCENEOBJT_NONE);
	if (!obj) {
		log_debug("Cannot find id %d\n", id);
		return false;
	}
	log_debug("- id %d: '%s' bbox x0 %d y0 %d x1 %d y1 %d\n", id, obj->name,
		  obj->bbox.x0, obj->bbox.y0, obj->bbox.x1, obj->bbox.x1);

	return is_within(obj, x, y);
}

bool scene_obj_within(const struct scene *scn, struct scene_obj *obj, int x,
		      int y)
{
	bool within = false;

	switch (obj->type) {
	case SCENEOBJT_NONE:
		break;
	case SCENEOBJT_IMAGE:
	case SCENEOBJT_TEXT:
	case SCENEOBJT_BOX:
		within = is_within(obj, x, y);
		break;
	case SCENEOBJT_MENU: {
		struct scene_obj_menu *menu;

		menu = (struct scene_obj_menu *)obj,
		within = scene_menu_within(scn, menu, x, y);
		break;
	}
	case SCENEOBJT_TEXTLINE: {
		struct scene_obj_textline *tline;

		tline = (struct scene_obj_textline *)obj,
		within = scene_textline_within(scn, tline, x, y);
		break;
	}
	case SCENEOBJT_TEXTEDIT:
		/* TODO(sjg@chromium.org): Implement this */
		break;
	}

	return within;
}

struct scene_obj *scene_find_obj_within(const struct scene *scn, int x, int y,
					bool reverse, bool allow_any)
{
	struct scene_obj *obj;

	log_debug("within: x %d y %d reverse %d allow_any %d\n", x, y, reverse,
		  allow_any);
	if (reverse) {
		list_for_each_entry_reverse(obj, &scn->obj_head, sibling) {
			log_debug(" - obj %d '%s' can_highlight %d within %d\n",
				  obj->id, obj->name,
				  scene_obj_can_highlight(obj),
				  scene_obj_within(scn, obj, x, y));
			if ((allow_any || scene_obj_can_highlight(obj)) &&
			    scene_obj_within(scn, obj, x, y)) {
				log_debug("- returning obj %d '%s'\n", obj->id,
					  obj->name);
				return obj;
			}
		}
	} else {
		list_for_each_entry(obj, &scn->obj_head, sibling) {
			log_debug(" - obj %d '%s' can_highlight %d within %d\n",
				  obj->id, obj->name,
				  scene_obj_can_highlight(obj),
				  scene_obj_within(scn, obj, x, y));
			if ((allow_any || scene_obj_can_highlight(obj)) &&
			    scene_obj_within(scn, obj, x, y)) {
				log_debug("- returning obj %d '%s'\n", obj->id,
					  obj->name);
				return obj;
			}
		}
	}
	log_debug("- no object\n");

	return NULL;
}

/**
 * send_click_obj() - Handle a click for moving between objects
 *
 * On entry, scn->highlight_id is set to a menu/textline, but the object is not
 * open.
 *
 * @scn: Scene to receive the click
 * @obj: Object to receive the click
 * @x: X coordinate of the click
 * @y: Y coordinate of the click
 * @event: Returns resulting event from this keypress
 * Returns: 0 if OK, -ve on error
 */
static void send_click_obj(struct scene *scn, struct scene_obj *obj, int x,
			   int y, struct expo_action *event)
{
	if (scene_obj_can_highlight(obj) && scene_obj_within(scn, obj, x, y)) {
		event->type = EXPOACT_OPEN;
		event->select.id = obj->id;
		log_debug("open obj %d\n", event->select.id);
		return;
	}

	log_debug("no object; finding...\n");
	obj = scene_find_obj_within(scn, x, y, false, false);
	if (obj) {
		event->type = EXPOACT_POINT_OPEN;
		event->select.id = obj->id;
	}
}

static int scene_click_popup(struct scene *scn, int x, int y,
			     struct expo_action *event)
{
	struct scene_obj *obj, *chk;

	obj = NULL;
	if (scn->highlight_id) {
		obj = scene_obj_find(scn, scn->highlight_id,
				     SCENEOBJT_NONE);
	}
	if (!obj)
		return 0;

	if (!(obj->flags & SCENEOF_OPEN)) {
		send_click_obj(scn, obj, x, y, event);
		return 0;
	}

	/* check that the click is within our object */
	chk = scene_find_obj_within(scn, x, y, false, false);
	log_debug("chk %d '%s' (obj %d '%s')\n", chk ? chk->id : -1,
		  chk ? chk->name : "(none)", obj->id, obj->name);
	if (!chk) {
		/* click into space */
		event->type = EXPOACT_CLOSE;
		event->select.id = obj->id;
		return 0;
	} else if (chk != obj) {
		send_click_obj(scn, chk, x, y, event);
		if (event->type == EXPOACT_OPEN) {
			event->type = EXPOACT_REPOINT_OPEN;
			event->select.prev_id = obj->id;
		}
		return 0;
	}

	/* click within the open object */
	switch (obj->type) {
	case SCENEOBJT_NONE:
	case SCENEOBJT_IMAGE:
	case SCENEOBJT_TEXT:
	case SCENEOBJT_BOX:
		break;
	case SCENEOBJT_MENU: {
		struct scene_obj_menu *menu;

		menu = (struct scene_obj_menu *)obj,
		scene_menu_send_click(scn, menu, x, y, event);
		break;
	}
	case SCENEOBJT_TEXTLINE: {
		struct scene_obj_textline *tline;

		tline = (struct scene_obj_textline *)obj;
		// ret = scene_textline_send_click(scn, tline, x, y, event);
		// if (ret)
			// return log_msg_ret("key", ret);
		break;
	}
	case SCENEOBJT_TEXTEDIT:
		/* TODO(sjg@chromium.org): Implement this */
		break;
	}

	return 0;
}

int scene_send_click(struct scene *scn, int x, int y, struct expo_action *event)
{
	struct scene_obj *obj;

	event->type = EXPOACT_NONE;

	if (scn->expo->popup) {
		scene_click_popup(scn, x, y, event);
		return 0;
	}

	obj = scene_find_obj_within(scn, x, y, false, false);
	log_debug("non-popup obj %d '%s'\n", obj ? obj->id : -1,
		  obj ? obj->name : "(none)");
	if (!obj) {
		obj = scene_find_obj_within(scn, x, y, true, true);
		log_debug("non-popup any obj %d '%s'\n", obj ? obj->id : -1,
			  obj ? obj->name : "(none)");
		if (!obj)
			return 0;
	}

	switch (obj->type) {
	case SCENEOBJT_NONE:
	case SCENEOBJT_IMAGE:
	case SCENEOBJT_TEXT:
	case SCENEOBJT_BOX:
		event->type = EXPOACT_CLICK;
		event->select.id = obj->id;
		break;
	case SCENEOBJT_MENU: {
		struct scene_obj_menu *menu;

		menu = (struct scene_obj_menu *)obj,
		scene_menu_send_click(scn, menu, x, y, event);
		break;
	}
	case SCENEOBJT_TEXTLINE: {
		/* For now, just highlight the textline */
		scn->highlight_id = obj->id;
		break;
	}
	case SCENEOBJT_TEXTEDIT:
		/* For now, just highlight the textedit */
		scn->highlight_id = obj->id;
		break;
	}

	return 0;
}

int scene_obj_calc_bbox(struct scene_obj *obj, struct vidconsole_bbox bbox[])
{
	switch (obj->type) {
	case SCENEOBJT_NONE:
	case SCENEOBJT_IMAGE:
	case SCENEOBJT_TEXT:
	case SCENEOBJT_BOX:
		return -ENOSYS;
	case SCENEOBJT_MENU: {
		struct scene_obj_menu *menu = (struct scene_obj_menu *)obj;

		scene_menu_calc_bbox(menu, bbox);
		break;
	}
	case SCENEOBJT_TEXTLINE:
	case SCENEOBJT_TEXTEDIT:
		scene_txtin_calc_bbox(obj, scene_obj_txtin(obj),
				      &bbox[SCENEBB_all], &bbox[SCENEBB_label]);
		break;
	}

	return 0;
}

int scene_sync_bbox(struct scene *scn)
{
	struct scene_obj *obj;

	list_for_each_entry(obj, &scn->obj_head, sibling) {
		int req_width = obj->req_bbox.x1 - obj->req_bbox.x0;
		int req_height = obj->req_bbox.y1 - obj->req_bbox.y0;
		struct vid_bbox old_bbox = obj->bbox;

		if (obj->flags & SCENEOF_SYNC_POS) {
			if (obj->flags & SCENEOF_SIZE_VALID) {
				int width = obj->bbox.x1 - obj->bbox.x0;
				int height = obj->bbox.y1 - obj->bbox.y0;

				obj->bbox.x1 = obj->req_bbox.x0 + width;
				obj->bbox.y1 = obj->req_bbox.y0 + height;
			}
			obj->bbox.x0 = obj->req_bbox.x0;
			obj->bbox.y0 = obj->req_bbox.y0;
		}

		if (obj->flags & SCENEOF_SYNC_SIZE) {
			obj->bbox.x1 = obj->bbox.x0 + req_width;
			obj->bbox.y1 = obj->bbox.y0 + req_height;
		}
		if (obj->flags & SCENEOF_SYNC_WIDTH)
			obj->bbox.x1 = obj->bbox.x0 + req_width;
		if (obj->flags & SCENEOF_SYNC_BBOX)
			obj->bbox = obj->req_bbox;

		/* Set dirty flag if bbox changed */
		if (old_bbox.x0 != obj->bbox.x0 || old_bbox.y0 != obj->bbox.y0 ||
		    old_bbox.x1 != obj->bbox.x1 || old_bbox.y1 != obj->bbox.y1)
			obj->flags |= SCENEOF_DIRTY;

		obj->flags &= ~(SCENEOF_SYNC_POS | SCENEOF_SYNC_SIZE |
				SCENEOF_SYNC_WIDTH | SCENEOF_SYNC_BBOX);
	}

	return 0;
}

int scene_calc_dims(struct scene *scn)
{
	struct scene_obj *obj;
	int ret, i;

	/*
	 * Do the menus last so that all the menus' text objects
	 * are dimensioned. Many objects are referenced by a menu and the size
	 * and position is set by the menu
	 */
	for (i = 0; i < 2; i++) {
		bool do_menus = i;

		list_for_each_entry(obj, &scn->obj_head, sibling) {
			switch (obj->type) {
			case SCENEOBJT_NONE:
			case SCENEOBJT_TEXT:
			case SCENEOBJT_BOX:
			case SCENEOBJT_IMAGE: {
				int width;

				if (!do_menus) {
					ret = scene_obj_get_hw(scn, obj->id,
							       &width);
					if (ret < 0)
						return log_msg_ret("get", ret);
					obj->dims.x = width;
					obj->dims.y = ret;
				}
				break;
			}
			case SCENEOBJT_MENU:
				break;
			case SCENEOBJT_TEXTEDIT: {
				struct scene_obj_txtedit *ted;

				ted = (struct scene_obj_txtedit *)obj;
				if (!scn->expo->cons || do_menus)
					continue;

				ret = scene_txted_calc_dims(ted, scn->expo->cons);
				if (ret)
					return log_msg_ret("ted", ret);

				break;
			}
			case SCENEOBJT_TEXTLINE: {
				struct scene_obj_textline *tline;

				tline = (struct scene_obj_textline *)obj;
				if (!scn->expo->cons)
					continue;

				ret = scene_textline_calc_dims(tline,
							       scn->expo->cons);
				if (ret)
					return log_msg_ret("men", ret);

				break;
			}
			}
		}
	}

	return 0;
}

int scene_apply_theme(struct scene *scn, struct expo_theme *theme)
{
	struct scene_obj *obj;

	/* Avoid error-checking optional items */
	scene_txt_set_font(scn, scn->title_id, NULL, theme->font_size);

	list_for_each_entry(obj, &scn->obj_head, sibling) {
		switch (obj->type) {
		case SCENEOBJT_NONE:
		case SCENEOBJT_IMAGE:
		case SCENEOBJT_MENU:
		case SCENEOBJT_BOX:
		case SCENEOBJT_TEXTLINE:
		case SCENEOBJT_TEXTEDIT:
			break;
		case SCENEOBJT_TEXT:
			scene_txt_set_font(scn, obj->id, NULL,
					   theme->font_size);
			break;
		}
	}

	return 0;
}

void scene_set_highlight_id(struct scene *scn, uint id)
{
	scn->highlight_id = id;
}

void scene_highlight_first(struct scene *scn)
{
	struct scene_obj *obj;

	list_for_each_entry(obj, &scn->obj_head, sibling) {
		if (scene_obj_can_highlight(obj)) {
			scene_set_highlight_id(scn, obj->id);
			return;
		}
	}
}

int scene_img_set_data(struct scene *scn, uint id, const void *data, int size)
{
	struct scene_obj_img *img;

	img = scene_obj_find(scn, id, SCENEOBJT_IMAGE);
	if (!img)
		return log_msg_ret("sid", -ENOENT);
	img->data = data;
	/* TODO: Consider using an abuf for the image */

	return 0;
}

static int scene_obj_open(struct scene *scn, struct scene_obj *obj)
{
	int ret;

	switch (obj->type) {
	case SCENEOBJT_NONE:
	case SCENEOBJT_IMAGE:
	case SCENEOBJT_MENU:
	case SCENEOBJT_TEXT:
	case SCENEOBJT_BOX:
		break;
	case SCENEOBJT_TEXTLINE:
	case SCENEOBJT_TEXTEDIT:
		ret = scene_txtin_open(scn, obj, scene_obj_txtin(obj));
		if (ret)
			return log_msg_ret("op", ret);
		break;
	}

	return 0;
}

int scene_set_open(struct scene *scn, uint id, bool open)
{
	struct scene_obj *obj;
	int ret;

	obj = scene_obj_find(scn, id, SCENEOBJT_NONE);
	if (!obj)
		return log_msg_ret("find", -ENOENT);

	if (open) {
		ret = scene_obj_open(scn, obj);
		if (ret)
			return log_msg_ret("op", ret);
	}

	ret = scene_obj_flag_clrset(scn, id, SCENEOF_OPEN,
				    open ? SCENEOF_OPEN : 0);
	if (ret)
		return log_msg_ret("flg", ret);

	return 0;
}

int scene_iter_objs(struct scene *scn, expo_scene_obj_iterator iter,
		    void *priv)
{
	struct scene_obj *obj;

	list_for_each_entry(obj, &scn->obj_head, sibling) {
		int ret;

		ret = iter(obj, priv);
		if (ret)
			return log_msg_ret("itr", ret);
	}

	return 0;
}

int scene_bbox_join(const struct vidconsole_bbox *src, int inset,
		    struct vidconsole_bbox *dst)
{
	if (dst->valid) {
		dst->x0 = min(dst->x0, src->x0 - inset);
		dst->y0 = min(dst->y0, src->y0);
		dst->x1 = max(dst->x1, src->x1 + inset);
		dst->y1 = max(dst->y1, src->y1);
	} else {
		dst->x0 = src->x0 - inset;
		dst->y0 = src->y0;
		dst->x1 = src->x1 + inset;
		dst->y1 = src->y1;
		dst->valid = true;
	}

	return 0;
}

int scene_bbox_union(struct scene *scn, uint id, int inset,
		     struct vidconsole_bbox *bbox)
{
	struct scene_obj *obj;
	struct vidconsole_bbox local;

	if (!id)
		return 0;
	obj = scene_obj_find(scn, id, SCENEOBJT_NONE);
	if (!obj)
		return log_msg_ret("obj", -ENOENT);
	local.x0 = obj->bbox.x0;
	local.y0 = obj->bbox.y0;
	local.x1 = obj->bbox.x1;
	local.y1 = obj->bbox.y1;
	local.valid = true;
	scene_bbox_join(&local, inset, bbox);

	return 0;
}

void scene_dims_join(struct scene_obj_dims *src, struct scene_obj_dims *dst)
{
	dst->x = max(dst->x, src->x);
	dst->y = max(dst->y, src->y);
}

int scene_dims_union(struct scene *scn, uint id, struct scene_obj_dims *dims)
{
	struct scene_obj *obj;

	if (!id)
		return 0;
	obj = scene_obj_find(scn, id, SCENEOBJT_NONE);
	if (!obj)
		return log_msg_ret("obj", -ENOENT);
	scene_dims_join(&obj->dims, dims);

	return 0;
}

const char *scene_flag_name(uint flag)
{
	int bit;

	bit = ffs(flag) - 1;
	if (bit < 0 || bit >= ARRAY_SIZE(scene_flag_names))
		return "(none)";

	return scene_flag_names[bit];
}

const char *scene_obj_type_name(enum scene_obj_t type)
{
	if (type >= ARRAY_SIZE(scene_obj_type_names))
		return "unknown";

	return scene_obj_type_names[type];
}
