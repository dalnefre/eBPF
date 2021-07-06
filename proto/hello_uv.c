#include <stdio.h>
#include <stdlib.h>
#include <uv.h>

#define DEBUG(x) x /**/

#define USE_DEFAULT_LOOP 1
#define USE_IDLE_COUNTER 1

void
uv_fail(int rv)
{
    if (rv < 0) {
        fprintf(stderr, "FAIL! (%d) %s: %s\n",
            rv, uv_err_name(rv), uv_strerror(rv));
    } else {
        fprintf(stderr, "FAIL! (%d)\n", rv);
    }
    exit(EXIT_FAILURE);
}

#if USE_IDLE_COUNTER
typedef struct countdown {
    uv_idle_t   idle;
    int         n;
} countdown_t;

void
countdown_cb(uv_idle_t* idle)
{
    countdown_t *cdp = (countdown_t *)(idle->data);
    DEBUG(printf("countdown = %d\n", cdp->n));
    if (cdp->n > 0) {
        --cdp->n;
    } else {
        // stop idle handle
        uv_idle_stop(idle);
        printf("Idle stopped.\n");
        // release idle handle
        uv_unref((uv_handle_t *)idle);
        // stop event loop
        uv_stop(idle->loop);
        printf("Stop requested.\n");
    }
}
#endif

int
main()
{
    int rv;

#if USE_DEFAULT_LOOP
    uv_loop_t *loop = uv_default_loop();
#else
    uv_loop_t *loop = malloc(sizeof(uv_loop_t));
    rv = uv_loop_init(loop);
    if (rv < 0) uv_fail(rv);
#endif

#if USE_IDLE_COUNTER
    countdown_t countdown;
    uv_idle_init(loop, &countdown.idle);
    countdown.idle.data = &countdown;
    countdown.n = 13;
    rv = uv_idle_start(&countdown.idle, countdown_cb);
    if (rv < 0) uv_fail(rv);
#endif

    printf("Starting...\n");
    rv = uv_run(loop, UV_RUN_DEFAULT);
    if (rv < 0) uv_fail(rv);
    printf("Finishing...\n");

    DEBUG(printf("loop_alive = %d\n", uv_loop_alive(loop)));
    DEBUG(uv_print_all_handles(loop, stdout));
    rv = uv_loop_close(loop);
    if (rv < 0) uv_fail(rv);
#if !USE_DEFAULT_LOOP
    free(loop);
#endif

    return (exit(EXIT_SUCCESS), 0);
}
