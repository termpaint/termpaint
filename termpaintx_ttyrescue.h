#ifndef TERMPAINT_TERMPAINT_TTYRESCUE_INCLUDED
#define TERMPAINT_TERMPAINT_TTYRESCUE_INCLUDED

#ifdef __cplusplus
#ifndef _Bool
#define _Bool bool
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__GNUC__) && defined(TERMPAINT_EXPORT_SYMBOLS)
#define _tERMPAINT_PUBLIC __attribute__((visibility("default")))
#else
#define _tERMPAINT_PUBLIC
#endif

_tERMPAINT_PUBLIC void termpaint_ttyrescue_stop(int fd);
_tERMPAINT_PUBLIC int termpaint_ttyrescue_start(const char *restore_seq);


#ifdef __cplusplus
}
#endif

#endif
