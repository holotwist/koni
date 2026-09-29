#ifndef KONI_PATHS_H
#define KONI_PATHS_H

#include <stdbool.h>
#include <stddef.h>

void koni_paths_init(const char *custom_base_dir);
const char* koni_get_base_dir(void);
void koni_get_path(char *out_buf, size_t sz, const char *relative_path);
void koni_ensure_dir(const char *dir_path);

#endif // KONI_PATHS_H