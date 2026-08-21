#ifndef MPRIS_H
#define MPRIS_H

#ifdef ENABLE_MPRIS
void mpris_init(void);
void mpris_shutdown(void);
#else
static inline void mpris_init(void) {}
static inline void mpris_shutdown(void) {}
#endif

#endif // MPRIS_H