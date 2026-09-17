#ifndef SPARKLES_TEXT_PROMPT_H
#define SPARKLES_TEXT_PROMPT_H

#include "raylib.h"
#include <stdbool.h>

typedef struct {
    const char *tag;
    const char *prompt;
    const char *initial_text;
    const char *submit_label;
    int max_len;
    void (*on_submit)(const char *text, void *user_data);
    void (*on_cancel)(void *user_data);
    void *user_data;
} SparklesTextPromptConfig;

void sparkles_text_prompt_init(void);
void sparkles_text_prompt_open(const SparklesTextPromptConfig *cfg);
void sparkles_text_prompt_close(void);
bool sparkles_text_prompt_is_open(void);

void sparkles_text_prompt_update(void);
void sparkles_text_prompt_render(float screen_w, float screen_h);

#endif // SPARKLES_TEXT_PROMPT_H