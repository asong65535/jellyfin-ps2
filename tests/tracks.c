/* Run the real screen callbacks; drawing stubs capture visible episode rows. */
#include "screens.h"
#include "ui.h"
#include "poster.h"
#include "jf.h"
#include <libpad.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

JfTrack jf_tracks[JF_TRACK_MAX];
int jf_track_count, jf_album_year;
char jf_album_name[112], jf_album_artist[80], jf_album_desc[512];
JfTab jf_tab = JF_TAB_SERIES;
static const Layout layout = {.w = 640};
const Layout *lay = &layout;
static int rows, first, last, opened, jobs;
static ScreenId destination;
static char count_text[32];

int jf_load_detail(const char *id)
{
	assert(sscanf(id, "id-%d", &opened) == 1);
	return 0;
}
const char *jf_err_title(void) { return ""; }
const char *jf_err_body(void) { return ""; }
void app_fail(const char *a, const char *b) { (void)a; (void)b; assert(0); }
void scr_detail_reset(void) {}
void app_goto(ScreenId id) { destination = id; }
void app_run_job(JobFn fn, const char *s) { (void)s; jobs++; fn(); }
void scr_player_open(int track) { opened = track; }
void scr_player_start(void) {}
int poster_ready(int slot) { (void)slot; return 0; }
void poster_draw(GSGLOBAL *gs, int slot, float x, float y, float w, float h)
{ (void)gs; (void)slot; (void)x; (void)y; (void)w; (void)h; }
GSGLOBAL *ui_gs(void) { return NULL; }
void ui_waves(void) {}
float ui_text_w(float scale, const char *s) { (void)scale; return strlen(s) * 8; }
void ui_text(float x, float y, float scale, u64 c, const char *s)
{
	(void)x; (void)y; (void)scale; (void)c;
	int episode;
	if (sscanf(s, "Episode %d", &episode) == 1) {
		if (!rows) first = episode;
		last = episode;
		rows++;
	}
	if (strstr(s, "episodes")) snprintf(count_text, sizeof(count_text), "%s", s);
}
void ui_text_c(float x, float y, float scale, u64 c, const char *s)
{ ui_text(x, y, scale, c, s); }
void ui_box(float x, float y, float w, float h, u64 c)
{ (void)x; (void)y; (void)w; (void)h; (void)c; }
void ui_round(float x, float y, float w, float h, float r, u64 c)
{ (void)r; ui_box(x, y, w, h, c); }
int ui_wrap_lines(float scale, const char *s, float w)
{ (void)scale; (void)s; (void)w; return 0; }
int ui_wrap(float x, float y, float lh, float scale, u64 c, const char *s, float w, int n)
{ (void)x; (void)y; (void)lh; (void)scale; (void)c; (void)s; (void)w; (void)n; return 0; }
int ui_wrap_from(float x, float y, float lh, float scale, u64 c, const char *s, float w, int n, int first_line)
{ (void)first_line; return ui_wrap(x, y, lh, scale, c, s, w, n); }
void ui_scrollbar(float x, float y, float h, int total, int vis, int top)
{ (void)x; (void)y; (void)h; (void)total; (void)vis; (void)top; }
void ui_hint_row(const BtnGlyph *b, const char **labels, int n)
{ (void)b; (void)labels; (void)n; }

static void check_visible(int selected)
{
	rows = 0;
	scr_tracks.draw();
	assert(rows > 0 && rows <= 8);
	assert(first <= selected && last >= selected);
	assert(strcmp(count_text, "256 episodes") == 0);
}

int main(void)
{
	jf_track_count = 256;
	for (int i = 0; i < jf_track_count; i++) {
		snprintf(jf_tracks[i].id, sizeof(jf_tracks[i].id), "id-%d", i);
		snprintf(jf_tracks[i].name, sizeof(jf_tracks[i].name), "Episode %d", i);
	}
	scr_tracks_reset();
	scr_tracks.enter();
	for (int i = 0; i < 256; i++) {
		check_visible(i);
		scr_tracks.input(PAD_CROSS);
		assert(destination == SCR_DETAIL && opened == i);
		scr_tracks.enter(); /* Back from detail preserves the visible selection. */
		check_visible(i);
		scr_tracks.input(PAD_DOWN);
	}
	scr_tracks.input(PAD_CROSS);
	assert(opened == 255);
	for (int i = 255; i >= 0; i--) {
		check_visible(i);
		scr_tracks.input(PAD_UP);
	}
	scr_tracks.input(PAD_CROSS);
	assert(opened == 0);
	jf_track_count = 0;
	scr_tracks.enter();
	int previous_jobs = jobs;
	scr_tracks.input(PAD_UP | PAD_DOWN | PAD_CROSS);
	assert(jobs == previous_jobs);
	rows = 0; scr_tracks.draw(); assert(rows == 0);
	jf_tab = JF_TAB_MUSIC;
	jf_track_count = 256;
	scr_tracks_reset(); scr_tracks.enter();
	for (int i = 0; i < 255; i++) scr_tracks.input(PAD_DOWN);
	scr_tracks.input(PAD_CROSS);
	assert(opened == 255 && destination == SCR_PLAYER);
	puts("track screen navigation tests passed (256 entries, detail return, empty season, music)");
	return 0;
}
