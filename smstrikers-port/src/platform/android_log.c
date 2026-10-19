// stdout and stderr into logcat. Everything the port reports goes through one or the other (OSReport,
// fprintf(stderr, "[port] ...")), and on Android both are /dev/null.

#if defined(__ANDROID__)

#include <android/log.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int s_pipe[2];

static void* pump(void* unused)
{
    char buf[2048];
    size_t have = 0;
    (void)unused;
    for (;;)
    {
        const ssize_t n = read(s_pipe[0], buf + have, sizeof buf - 1 - have);
        if (n <= 0)
            return NULL;
        have += (size_t)n;
        buf[have] = '\0';
        // One logcat entry per line; a line longer than the buffer goes out in pieces.
        char* start = buf;
        char* nl;
        while ((nl = strchr(start, '\n')) != NULL)
        {
            *nl = '\0';
            __android_log_write(ANDROID_LOG_INFO, "strikers", start);
            start = nl + 1;
        }
        have = (size_t)(buf + have - start);
        if (have == sizeof buf - 1)
        {
            __android_log_write(ANDROID_LOG_INFO, "strikers", buf);
            have = 0;
        }
        else
            memmove(buf, start, have);
    }
}

// A constructor, and an early one: DVDInit reports from a static constructor of its own, before
// main.
__attribute__((constructor(101))) static void port_android_log_init(void)
{
    pthread_t t;
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    if (pipe(s_pipe) != 0)
        return;
    dup2(s_pipe[1], STDOUT_FILENO);
    dup2(s_pipe[1], STDERR_FILENO);
    if (pthread_create(&t, NULL, pump, NULL) == 0)
        pthread_detach(t);
}

#endif
