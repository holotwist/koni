#ifndef EXT_REGISTRY_H
#define EXT_REGISTRY_H

#include "extension.h"

/* Accessor for all registered extension definitions */
const KoniExtension** koni_extensions_get_all(void);

/* Returns count of currently active extensions based on activation rules */
int koni_extensions_get_active(KoniExtension **out_active, int max_count);

/* Returns active tabs provided by extensions */
int koni_extensions_get_active_tabs(ExtTabDescriptor **out_tabs, KoniExtension **out_exts, int max_count);

#endif // EXT_REGISTRY_H