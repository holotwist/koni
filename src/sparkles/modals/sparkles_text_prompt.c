#include "sparkles_text_prompt.h"
#include "sparkles_theme.h"
#include <string.h>
#include <math.h>

static bool s_open = false;
static SparklesTextPromptConfig s_cfg = {0};
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
    float qw = fminf(480.0f, sw - 40.0f);
    float qh = 160.0f;
    float qy = sh * 0.18f;
    Rectangle box = { (sw - qw) * 0.5f, qy, qw, qh };
    Rectangle btn_submit = { box.x + box.width - 180, box.y + box.height - 42, 80, 26 };
    Rectangle btn_cancel = { box.x + box.width - 92,  box.y + box.height - 42, 72, 26 };

    Vector2 m = GetMousePosition();
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (CheckCollisionPointRec(m, btn_submit) && s_len > 0) {
            if (s_cfg.on_submit) s_cfg.on_submit(s_buf, s_cfg.user_data);
            s_open = false;
        } else if (CheckCollisionPointRec(m, btn_cancel) || !CheckCollisionPointRec(m, box)) {
            sparkles_text_prompt_close();
        }
    }
}

void sparkles_text_prompt_render(float screen_w, float screen_h) {
    if (!s_open) return;

    DrawRectangle(0, 0, (int)screen_w, (int)screen_h, ColorAlpha(BLACK, 0.78f));

    float qw = fminf(480.0f, screen_w - 40.0f);
    float qh = 160.0f;
    // Anchored to top third to stay clear of the soft keyboard
    float qy = screen_h * 0.18f;
    Rectangle box = { (screen_w - qw) * 0.5f, qy, qw, qh };

    DrawRectangleRec(box, (Color){ 8, 8, 11, 255 });
    DrawRectangleLinesEx(box, 1.0f, COLOR_ACCENT);
    DrawNothingCornerBrackets(box, 8.0f, COLOR_ACCENT);

    const char *tag = s_cfg.tag ? s_cfg.tag : "// PROMPT";
    const char *prompt = s_cfg.prompt ? s_cfg.prompt : "Enter text";
    const char *sub_label = s_cfg.submit_label ? s_cfg.submit_label : "Confirm";

    DrawText(tag, (int)box.x + 20, (int)box.y + 16, 10, COLOR_TEXT_MUTED);
    DrawText(prompt, (int)box.x + 20, (int)box.y + 34, FONT_SIZE_MD, COLOR_TEXT_PRIMARY);

    Rectangle input_box = { box.x + 20, box.y + 64, box.width - 40, 32 };
    DrawRectangleRec(input_box, (Color){ 14, 15, 18, 255 });
    DrawRectangleLinesEx(input_box, 1.0f, (Color){ 35, 38, 46, 255 });

    const char *cur = ((int)(GetTime() * 2.5f) % 2 == 0) ? "_" : " ";
    DrawText(TextFormat("%s%s", s_buf, cur), (int)input_box.x + 10, (int)input_box.y + 8, FONT_SIZE_MD, COLOR_TEXT_PRIMARY);

    Vector2 m = GetMousePosition();
    Rectangle btn_submit = { box.x + box.width - 180, box.y + box.height - 42, 80, 26 };
    Rectangle btn_cancel = { box.x + box.width - 92,  box.y + box.height - 42, 72, 26 };

    bool hover_sub = CheckCollisionPointRec(m, btn_submit);
    bool hover_can = CheckCollisionPointRec(m, btn_cancel);

    DrawRectangleRec(btn_submit, (hover_sub && s_len > 0) ? ColorAlpha(COLOR_ACCENT, 0.25f) : (Color){ 18, 19, 24, 255 });
    DrawRectangleLinesEx(btn_submit, 1.0f, (hover_sub && s_len > 0) ? COLOR_ACCENT : (s_len > 0 ? COLOR_TEXT_PRIMARY : COLOR_TEXT_DARK));
    DrawText(sub_label, (int)btn_submit.x + (btn_submit.width - MeasureText(sub_label, FONT_SIZE_SM)) / 2, (int)btn_submit.y + 6, FONT_SIZE_SM, s_len > 0 ? COLOR_TEXT_PRIMARY : COLOR_TEXT_DARK);

    DrawRectangleRec(btn_cancel, hover_can ? ColorAlpha(COLOR_ACCENT, 0.25f) : (Color){ 18, 19, 24, 255 });
    DrawRectangleLinesEx(btn_cancel, 1.0f, hover_can ? COLOR_ACCENT : COLOR_TEXT_MUTED);
    DrawText("Cancel", (int)btn_cancel.x + (btn_cancel.width - MeasureText("Cancel", FONT_SIZE_SM)) / 2, (int)btn_cancel.y + 6, FONT_SIZE_SM, COLOR_TEXT_PRIMARY);
}