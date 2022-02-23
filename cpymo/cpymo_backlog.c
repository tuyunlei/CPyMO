#include "cpymo_backlog.h"
#include "cpymo_engine.h"
#include "cpymo_list_ui.h"
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

void cpymo_backlog_init(cpymo_backlog *b)
{
	b->next_record_to_write = 0;
	b->pending_vo_filename[0] = '\0';
	b->owning_name = false;
	b->pending_name = NULL;
	for (size_t i = 0; i < CPYMO_BACKLOG_MAX_RECORDS; i++) {
		b->records[i].vo_filename[0] = '\0';
		b->records[i].owning_name = false;
		b->records[i].name = NULL;
		b->records[i].lines = NULL;
	}
}

static void cpymo_backlog_record_clean(cpymo_backlog_record *rec)
{
	if (rec->owning_name && rec->name)
		cpymo_backend_text_free(rec->name);

	rec->owning_name = false;
	rec->name = NULL;

	if (rec->lines) {
		for (size_t i = 0; i < rec->max_lines; ++i) {
			if (rec->lines[i]) cpymo_backend_text_free(rec->lines[i]);
		}

		free(rec->lines);
		rec->lines = NULL;
	}

	rec->max_lines = 0;
	rec->vo_filename[0] = '\0';
}

void cpymo_backlog_free(cpymo_backlog *b)
{
	if (b->owning_name && b->pending_name) 
		cpymo_backend_text_free(b->pending_name);

	for (size_t i = 0; i < CPYMO_BACKLOG_MAX_RECORDS; i++) {
		cpymo_backlog_record *rec = &b->records[i];
		cpymo_backlog_record_clean(rec);
	}
}

void cpymo_backlog_record_write_vo(cpymo_backlog *b, cpymo_parser_stream_span vo)
{
	cpymo_parser_stream_span_copy(
		b->pending_vo_filename, sizeof(b->pending_vo_filename), vo);
}

void cpymo_backlog_record_write_name(cpymo_backlog *b, cpymo_backend_text name)
{
	if (b->owning_name && b->pending_name)	
		cpymo_backend_text_free(b->pending_name);

	b->owning_name = name == NULL ? false : true;
	b->pending_name = name;
}

error_t cpymo_backlog_record_write_text(cpymo_backlog *b, cpymo_backend_text **textlines_moveinto, size_t max_lines)
{
	cpymo_backlog_record *rec = &b->records[b->next_record_to_write];

	cpymo_backlog_record_clean(rec);

	rec->name = b->pending_name;
	rec->owning_name = b->owning_name;
	b->owning_name = false;
	rec->lines = *textlines_moveinto;
	*textlines_moveinto = NULL;
	rec->max_lines = max_lines;
	
	strcpy(rec->vo_filename, b->pending_vo_filename);
	b->pending_vo_filename[0] = '\0';

	b->next_record_to_write = (b->next_record_to_write + 1) % CPYMO_BACKLOG_MAX_RECORDS;
	return CPYMO_ERR_SUCC;
}

#define ENC(INDEX) CPYMO_LIST_UI_ENCODE_UINT_NODE_ENC(INDEX)
#define DEC(PTR) CPYMO_LIST_UI_ENCODE_UINT_NODE_DEC(PTR)

static void cpymo_backlog_ui_draw_node(const cpymo_engine *e, const void *node_to_draw, float y)
{
	const cpymo_backlog_record *rec = &e->backlog.records[DEC(node_to_draw)];

	const float font_size = cpymo_gameconfig_font_size(&e->gameconfig);
	y += font_size;
	if (rec->name) {
		// Why not draw?
		cpymo_backend_text_draw(
			rec->name, 0, y, cpymo_color_white,
			1.0, cpymo_backend_image_draw_type_uielement);

		y += font_size;
	}

	for (size_t i = 0; i < rec->max_lines; ++i) {
		if (rec->lines[i]) {
			cpymo_backend_text_draw(
				rec->lines[i], 0, y, cpymo_color_white,
				1.0f, cpymo_backend_image_draw_type_uielement);
			y += font_size;
		}
	}
}

static error_t cpymo_backlog_ui_ok(struct cpymo_engine *e, void *selected)
{
	const cpymo_backlog_record *rec = &e->backlog.records[DEC(selected)];
	if (rec->vo_filename[0]) {
		return cpymo_audio_vo_play(e, cpymo_parser_stream_span_pure(rec->vo_filename));
	}

	return CPYMO_ERR_SUCC;
}

static void cpymo_backlog_ui_deleter(cpymo_engine *e, void *ui_)
{
}

static void *cpymo_backlog_ui_get_next(const cpymo_engine *e, const void *ui_data, const void *cur)
{
	size_t index = (size_t)DEC(cur);
	if (index) index--;
	else index = CPYMO_BACKLOG_MAX_RECORDS - 1;

	if (index == e->backlog.next_record_to_write) return NULL;
	if (e->backlog.records[index].lines == NULL) return NULL;

	return ENC(index);
}

static void *cpymo_backlog_ui_get_prev(const cpymo_engine *e, const void *ui_data, const void *cur)
{
	size_t index = (size_t)DEC(cur);
	if (index >= CPYMO_BACKLOG_MAX_RECORDS - 1) index = 0;
	else index++;

	if (index == e->backlog.next_record_to_write) return NULL;
	if (e->backlog.records[index].lines == NULL) return NULL;

	return ENC(index);
}

error_t cpymo_backlog_ui_enter(cpymo_engine *e)
{
	void *out_data = NULL;

	size_t first = e->backlog.next_record_to_write;
	if (first) first--;
	else first = CPYMO_BACKLOG_MAX_RECORDS - 1;

	return cpymo_list_ui_enter(
		e,
		&out_data,
		0,
		&cpymo_backlog_ui_draw_node,
		&cpymo_backlog_ui_ok,
		&cpymo_backlog_ui_deleter,
		ENC(first),
		&cpymo_backlog_ui_get_next,
		&cpymo_backlog_ui_get_prev,
		true,
		3);
}