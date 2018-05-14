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

void termpaint_ttyrescue_stop(int fd);
int termpaint_ttyrescue_start(const char *restore_seq);


#ifdef __cplusplus
}
#endif

#endif
