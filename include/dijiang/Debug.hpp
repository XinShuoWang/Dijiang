#pragma once

#define DEBUG


#ifdef DEBUG
#define SAY(reason) {fprintf(stdout, "%s\n", reason);};
#define DIE(reason) {fprintf(stderr, "%s\n", reason), exit(EXIT_FAILURE);};
#else
#define SAY(reason) ;
#define DIE(reason) ;
#endif

#define TEST_NZ(x)                                            \
    do                                                        \
    {                                                         \
        if ((x))                                              \
            DIE("error: " #x " failed (returned non-zero)."); \
    } while (0)

#define TEST_Z(x)                                              \
    do                                                         \
    {                                                          \
        if (!(x))                                              \
            DIE("error: " #x " failed (returned zero/null)."); \
    } while (0)


