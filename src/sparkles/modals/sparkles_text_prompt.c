#include "sparkles_text_prompt.h"
#include "sparkles_theme.h"
#include "input/sparkles_input.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>

static bool s_open = false;
static SparklesTextPromptConfig s_cfg = {0};
static char s_tag[64] = {0};
static char s_prompt[128] = {0};
static char s_submit_label[32] = {0};
static char s_buf[256] = {0};
static int s_len = 0;
static bool s_just_opened = false;

void sparkles_text_prompt_init(void) {
    s_open = false;
    s_len = 0;
    s_buf[0] = '\0';
    s_just_opened = false;
    memset(&s_cfg, 0, sizeof(s_cfg));
}

void sparkles_text_prompt_open(const SparklesTextPromptConfig *cfg) {
    if (!cfg) return;
    s_cfg = *cfg;
    s_open = true;
    s_just_opened = true;

    // Deep copy strings
    if (cfg->tag && cfg->tag[0]) {
        strncpy(s_tag, cfg->tag, sizeof(s_tag) - 1);
        s_tag[sizeof(s_tag) - 1] = '\0';
    } else {
        strncpy(s_tag, "PROMPT", sizeof(s_tag) - 1);
    }

    if (cfg->prompt && cfg->prompt[0]) {
        strncpy(s_prompt, cfg->prompt, sizeof(s_prompt) - 1);
        s_prompt[sizeof(s_prompt) - 1] = '\0';
    } else {
        strncpy(s_prompt, "Enter text", sizeof(s_prompt) - 1);
    }

    if (cfg->submit_label && cfg->submit_label[0]) {
        strncpy(s_submit_label, cfg->submit_label, sizeof(s_submit_label) - 1);
        s_submit_label[sizeof(s_submit_label) - 1] = '\0';
    } else {
        strncpy(s_submit_label, "Confirm", sizeof(s_submit_label) - 1);
    }

    if (cfg->initial_text) {
        strncpy(s_buf, cfg->initial_text, sizeof(s_buf) - 1);
        s_buf[sizeof(s_buf) - 1] = '\0';
    } else {
        s_buf[0] = '\0';
    }
    s_len = (int)strlen(s_buf);

    int cap = (cfg->max_len > 0 && cfg->max_len < (int)sizeof(s_buf)) ? cfg->max_len : (int)sizeof(s_buf) - 1;
    s_cfg.max_len = cap;
}

void sparkles_text_prompt_close(void) {
    if (!s_open) return;
    if (s_cfg.on_cancel) s_cfg.on_cancel(s_cfg.user_data);
    s_open = false;
}

bool sparkles_text_prompt_is_open(void) {
    return s_open;
}

void sparkles_text_prompt_update(void) {
    if (!s_open) return;

    if (s_just_opened) {
        s_just_opened = false;
        sparkles_input_consume();
        return;
    }

    int c = GetCharPressed();
    while (c > 0) {
        if (c >= 32 && c <= 126 && s_len < s_cfg.max_len) {
            s_buf[s_len++] = (char)c;
            s_buf[s_len] = '\0';
        }
        c = GetCharPressed();
    }

    if (IsKeyPressed(KEY_BACKSPACE) && s_len > 0) {
        s_buf[--s_len] = '\0';
    }

    if (IsKeyPressed(KEY_ESCAPE)) {
        sparkles_text_prompt_close();
        return;
    }

    if (IsKeyPressed(KEY_ENTER) && s_len > 0) {
        if (s_cfg.on_submit) s_cfg.on_submit(s_buf, s_cfg.user_data);
        s_open = false;
        return;
    }

    float sw = (float)GetScreenWidth();
    float sh = (float)GetScreenHeight();
    float ui_scale = sparkles_get_ui_scale();
    bool is_mobile = (sh > sw);

    float qw = is_mobile ? (sw - 20.0f * ui_scale) : fminf(480.0f * ui_scale, sw - 40.0f);
    float qh = is_mobile ? (210.0f * ui_scale) : (160.0f * ui_scale);
    float qy = is_mobile ? (sh * 0.06f) : (sh * 0.18f);
    Rectangle box = { (sw - qw) * 0.5f, qy, qw, qh };

    float btn_w_sub = is_mobile ? (76.0f * ui_scale) : 80.0f;
    float btn_w_can = is_mobile ? (68.0f * ui_scale) : 72.0f;
    float btn_h = is_mobile ? (30.0f * ui_scale) : 26.0f;

    Rectangle btn_cancel = { box.x + box.width - btn_w_can - 12.0f * ui_scale, box.y + box.height - btn_h - 12.0f * ui_scale, btn_w_can, btn_h };
    Rectangle btn_submit = { btn_cancel.x - btn_w_sub - 8.0f * ui_scale, btn_cancel.y, btn_w_sub, btn_h };

    Vector2 tap;
    if (sparkles_input_consume_tap((Rectangle){ 0, 0, sw, sh }, &tap)) {
        if (CheckCollisionPointRec(tap, btn_submit) && s_len > 0) {
            if (s_cfg.on_submit) s_cfg.on_submit(s_buf, s_cfg.user_data);
            sparkles_text_prompt_close();
            return;
        } else if (CheckCollisionPointRec(tap, btn_cancel) || !CheckCollisionPointRec(tap, box)) {
            sparkles_text_prompt_close();
            return;
        }

        // On-screen keypad touch buttons (while internal OS keyboard fixed)
        if (is_mobile) {
            static const char *keys[] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", ".", "-", "<<<" };
            float pad_y = box.y + 104.0f * ui_scale;
            float pad_btn_w = (box.width - 28.0f * ui_scale) / 13.0f;
            float pad_btn_h = 28.0f * ui_scale;

            for (int k = 0; k < 13; k++) {
                Rectangle kb = { box.x + 14.0f * ui_scale + (float)k * pad_btn_w, pad_y, pad_btn_w - 2.0f, pad_btn_h };
                if (CheckCollisionPointRec(tap, kb)) {
                    if (k == 12) { // Backspace
                        if (s_len > 0) s_buf[--s_len] = '\0';
                    } else if (s_len < s_cfg.max_len) {
                        s_buf[s_len++] = keys[k][0];
                        s_buf[s_len] = '\0';
                    }
                    return;
                }
            }
        }
    }

    sparkles_input_block_area(box);
}

void sparkles_text_prompt_render(float screen_w, float screen_h) {
    if (!s_open) return;

    DrawRectangle(0, 0, (int)screen_w, (int)screen_h, ColorAlpha(BLACK, 0.78f));

    float ui_scale = sparkles_get_ui_scale();
    bool is_mobile = (screen_h > screen_w);

    float qw = is_mobile ? (screen_w - 20.0f * ui_scale) : fminf(480.0f * ui_scale, screen_w - 40.0f);
    float qh = is_mobile ? (210.0f * ui_scale) : (160.0f * ui_scale);
    float qy = is_mobile ? (screen_h * 0.06f) : (screen_h * 0.18f);
    Rectangle box = { (screen_w - qw) * 0.5f, qy, qw, qh };

    DrawRectangleRec(box, (Color){ 8, 8, 11, 255 });
    DrawRectangleLinesEx(box, 1.0f, COLOR_ACCENT);
    DrawNothingCornerBrackets(box, 8.0f, COLOR_ACCENT);

    DrawText(s_tag, (int)(box.x + 16.0f * ui_scale), (int)(box.y + 16.0f * ui_scale), 10, COLOR_TEXT_MUTED);
    DrawText(s_prompt, (int)(box.x + 16.0f * ui_scale), (int)(box.y + 32.0f * ui_scale), FONT_SIZE_MD, COLOR_TEXT_PRIMARY);

    Rectangle input_box = { box.x + 14.0f * ui_scale, box.y + 56.0f * ui_scale, box.width - 28.0f * ui_scale, 34.0f * ui_scale };
    DrawRectangleRec(input_box, (Color){ 14, 15, 18, 255 });
    DrawRectangleLinesEx(input_box, 1.0f, (Color){ 35, 38, 46, 255 });

    const char *cur = ((int)(GetTime() * 2.5f) % 2 == 0) ? "_" : " ";
    DrawText(TextFormat("%s%s", s_buf, cur), (int)input_box.x + 10, (int)input_box.y + (int)(input_box.height - FONT_SIZE_MD) / 2, FONT_SIZE_MD, COLOR_TEXT_PRIMARY);

    // Quick touch keys on mobile
    if (is_mobile) {
        static const char *keys[] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", ".", "-", "<<<" };
        float pad_y = box.y + 104.0f * ui_scale;
        float pad_btn_w = (box.width - 28.0f * ui_scale) / 13.0f;
        float pad_btn_h = 28.0f * ui_scale;

        for (int k = 0; k < 13; k++) {
            Rectangle kb = { box.x + 14.0f * ui_scale + (float)k * pad_btn_w, pad_y, pad_btn_w - 2.0f, pad_btn_h };
            DrawRectangleRec(kb, (Color){ 18, 20, 26, 255 });
            DrawRectangleLinesEx(kb, 1.0f, (Color){ 32, 35, 45, 255 });
            int kw = MeasureText(keys[k], FONT_SIZE_SM);
            DrawText(keys[k], (int)(kb.x + (kb.width - (float)kw) * 0.5f), (int)(kb.y + (kb.height - (float)FONT_SIZE_SM) * 0.5f), FONT_SIZE_SM, (k == 12) ? COLOR_ACCENT : COLOR_TEXT_PRIMARY);
        }
    }

    Vector2 m = GetMousePosition();
    float btn_w_sub = is_mobile ? (76.0f * ui_scale) : 80.0f;
    float btn_w_can = is_mobile ? (68.0f * ui_scale) : 72.0f;
    float btn_h = is_mobile ? (30.0f * ui_scale) : 26.0f;

    Rectangle btn_cancel = { box.x + box.width - btn_w_can - 12.0f * ui_scale, box.y + box.height - btn_h - 12.0f * ui_scale, btn_w_can, btn_h };
    Rectangle btn_submit = { btn_cancel.x - btn_w_sub - 8.0f * ui_scale, btn_cancel.y, btn_w_sub, btn_h };

    bool hover_sub = CheckCollisionPointRec(m, btn_submit);
    bool hover_can = CheckCollisionPointRec(m, btn_cancel);

    DrawRectangleRec(btn_submit, (hover_sub && s_len > 0) ? ColorAlpha(COLOR_ACCENT, 0.25f) : (Color){ 18, 19, 24, 255 });
    DrawRectangleLinesEx(btn_submit, 1.0f, (hover_sub && s_len > 0) ? COLOR_ACCENT : (s_len > 0 ? COLOR_TEXT_PRIMARY : COLOR_TEXT_DARK));
    DrawText(s_submit_label, (int)btn_submit.x + (btn_submit.width - MeasureText(s_submit_label, FONT_SIZE_SM)) / 2, (int)btn_submit.y + 6, FONT_SIZE_SM, s_len > 0 ? COLOR_TEXT_PRIMARY : COLOR_TEXT_DARK);

    DrawRectangleRec(btn_cancel, hover_can ? ColorAlpha(COLOR_ACCENT, 0.25f) : (Color){ 18, 19, 24, 255 });
    DrawRectangleLinesEx(btn_cancel, 1.0f, hover_can ? COLOR_ACCENT : COLOR_TEXT_MUTED);
    DrawText("Cancel", (int)btn_cancel.x + (btn_cancel.width - MeasureText("Cancel", FONT_SIZE_SM)) / 2, (int)btn_cancel.y + 6, FONT_SIZE_SM, COLOR_TEXT_PRIMARY);
}