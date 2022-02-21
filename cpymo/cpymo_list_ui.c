#include "cpymo_list_ui.h"
#include "cpymo_engine.h"
#include <assert.h>

static inline float cpymo_list_ui_get_y(const cpymo_engine *e, int relative_to_current)
{
	const cpymo_list_ui *ui = (cpymo_list_ui *)cpymo_ui_data_const(e);

	return ui->from_bottom_to_top ?
		(float)e->gameconfig.imagesize_h - (1 + relative_to_current) * ui->node_height - ui->current_y :
		ui->current_y + relative_to_current * ui->node_height;
}

static int cpymo_list_ui_get_selection_relative_to_cur_by_mouse(const cpymo_engine *e)
{
	if (!e->input.mouse_position_useable) return INT_MAX;
	const float mouse_y = e->input.mouse_y;

	const cpymo_list_ui *ui = (cpymo_list_ui *)cpymo_ui_data_const(e);

	float y = ui->from_bottom_to_top ?
		(float)e->gameconfig.imagesize_h - ui->node_height - ui->current_y :
		ui->current_y;

	void *prev = ui->get_prev(e, cpymo_list_ui_data_const(e), ui->current_node);
	if (prev) {
		float top = y - (ui->from_bottom_to_top ? -ui->node_height : ui->node_height);
		float bottom = top + ui->node_height;

		if (mouse_y >= top && mouse_y < bottom) return -1;
	}

	int cur = 0;

	void *node = ui->current_node;
	while (ui->from_bottom_to_top ?
		y > -ui->node_height :
		y < (float)e->gameconfig.imagesize_h) {

		float bottom = y + ui->node_height;
		if (mouse_y >= y && mouse_y < bottom) return cur;

		y += ui->from_bottom_to_top ? -ui->node_height : ui->node_height;
		node = ui->get_next(e, cpymo_list_ui_data_const(e), node);
		if (node == NULL) break;

		cur++;
	}

	return INT_MAX;
}

static error_t cpymo_list_ui_update(cpymo_engine *e, void *ui_data, float d)
{
	if (e->input.cancel) {
		cpymo_list_ui_exit(e); 
		return CPYMO_ERR_SUCC;
	}

	cpymo_list_ui *ui = (cpymo_list_ui *)ui_data;
	
	if (e->input.mouse_button || e->input.mouse_wheel_delta) {
		float delta_y = e->input.mouse_y - e->prev_input.mouse_y;

		if (fabs(e->input.mouse_wheel_delta) > fabs(delta_y)) {
			delta_y = e->input.mouse_wheel_delta * ui->node_height * 0.5f;
		}

		ui->current_y += ui->from_bottom_to_top ? -delta_y : delta_y;

		bool a = delta_y > 0;
		bool b = delta_y < 0;

		if (ui->from_bottom_to_top) {
			bool tmp = b;
			b = a;
			a = tmp;
		}

		if (a) {
			void *p = ui->get_prev(e, ui + 1, ui->current_node);
			if (p == NULL) ui->current_y = 0;
		}
		else if (b) {
			void *p = ui->get_next(e, ui + 1, ui->current_node);
			if (p == NULL) ui->current_y = 0;
		}

		while (ui->current_y >= ui->node_height) {
			ui->current_y -= ui->node_height;
			void *p = ui->get_prev(e, ui + 1, ui->current_node);
			if (p) {
				ui->current_node = p;
				ui->selection_relative_to_cur++;
			}
		}

		while (ui->current_y <= -ui->node_height) {
			ui->current_y += ui->node_height;
			void *p = ui->get_next(e, ui + 1, ui->current_node);
			if (p) {
				ui->current_node = p;
				ui->selection_relative_to_cur--;
			}
		}

		cpymo_engine_request_redraw(e);
	}

	bool just_press_up = CPYMO_INPUT_JUST_PRESSED(e, up);
	bool just_press_down = CPYMO_INPUT_JUST_PRESSED(e, down);

	if (ui->from_bottom_to_top) {
		bool tmp = just_press_up;
		just_press_up = just_press_down;
		just_press_down = tmp;
	}

	if (just_press_up) {
		ui->selection_relative_to_cur--;
		// scroll follow
		cpymo_engine_request_redraw(e);
	}

	if (just_press_down) {
		ui->selection_relative_to_cur++;
		// scroll follow
		cpymo_engine_request_redraw(e);
	}

	if (cpymo_input_mouse_moved(e)) {
		int s = cpymo_list_ui_get_selection_relative_to_cur_by_mouse(e);
		if (s != ui->selection_relative_to_cur && s != INT_MAX) {
			ui->selection_relative_to_cur = s;
			cpymo_engine_request_redraw(e);
		}
	}

	// OK!

	return CPYMO_ERR_SUCC;
}

static void cpymo_list_ui_draw(const cpymo_engine *e, const void *ui_data)
{
	cpymo_bg_draw(e);
	float xywh[] = {0, 0, (float)e->gameconfig.imagesize_w, (float)e->gameconfig.imagesize_h };
	cpymo_backend_image_fill_rects(xywh, 1, cpymo_color_black, 0.5f, cpymo_backend_image_draw_type_bg);

	const cpymo_list_ui *ui = (cpymo_list_ui *)ui_data;
	xywh[1] = cpymo_list_ui_get_y(e, ui->selection_relative_to_cur);
	xywh[3] = ui->node_height;
	cpymo_backend_image_fill_rects(xywh, 1, cpymo_color_white, 0.5f, cpymo_backend_image_draw_type_bg);

	cpymo_color red;
	red.r = 255;
	red.g = 0;
	red.b = 0;
	xywh[1] = cpymo_list_ui_get_y(e, 0);
	xywh[3] = ui->node_height;
	cpymo_backend_image_fill_rects(xywh, 1, red, 0.5f, cpymo_backend_image_draw_type_bg);

	float y = ui->from_bottom_to_top ? 
		(float)e->gameconfig.imagesize_h - ui->node_height - ui->current_y : 
		ui->current_y;

	void *prev = ui->get_prev(e, cpymo_list_ui_data_const(e), ui->current_node);
	if (prev) {
		ui->draw_node(e, prev, y - (ui->from_bottom_to_top ? -ui->node_height : ui->node_height));	
	}

	void *node = ui->current_node;
	while (ui->from_bottom_to_top ? 
			y > -ui->node_height : 
			y < (float)e->gameconfig.imagesize_h) {
		ui->draw_node(e, node, y);

		y += ui->from_bottom_to_top ? -ui->node_height : ui->node_height;
		node = ui->get_next(e, cpymo_list_ui_data_const(e), node);
		if (node == NULL) break;
	}
}

static void cpymo_list_ui_delete(cpymo_engine *e, void *ui)
{
	cpymo_list_ui *list_ui = (cpymo_list_ui *)ui;
	list_ui->ui_data_deleter(e, cpymo_list_ui_data(e));
}

error_t cpymo_list_ui_enter(
	cpymo_engine *e,
	void **out_ui_data,
	size_t ui_data_size,
	cpymo_list_ui_draw_node draw_node,
	cpymo_ui_deleter ui_data_deleter,
	void *current,
	cpymo_list_ui_get_node get_next,
	cpymo_list_ui_get_node get_prev,
	bool from_bottom_to_top,
	size_t nodes_per_screen)
{
	cpymo_list_ui *data = NULL;
	error_t err = cpymo_ui_enter(
		(void **)&data, e, sizeof(cpymo_list_ui) + ui_data_size,
		&cpymo_list_ui_update,
		&cpymo_list_ui_draw,
		&cpymo_list_ui_delete);
	CPYMO_THROW(err);

	data->ui_data_deleter = ui_data_deleter;
	data->draw_node = draw_node;
	data->current_node = current;
	data->get_next = get_next;
	data->get_prev = get_prev;
	data->from_bottom_to_top = from_bottom_to_top;
	data->node_height = (float)e->gameconfig.imagesize_h / nodes_per_screen;
	data->current_y = 0;
	data->selection_relative_to_cur = 0;

	assert(*out_ui_data == NULL);
	*out_ui_data = cpymo_list_ui_data(e);

	return err;
}

