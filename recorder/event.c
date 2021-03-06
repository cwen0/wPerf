#include "defs.h"

static void on_read(uv_fs_t *req);

static inline void cleanup(struct event_ctx *event)
{
    uv_fs_close(event->loop, &event->req.close, event->fd[0], NULL);
    uv_fs_fsync(event->loop, &event->req.close, event->fd[1], NULL);
    uv_fs_close(event->loop, &event->req.close, event->fd[1], NULL);
}

static void on_write(uv_fs_t *req)
{
    struct event_ctx *event;

    event = container_of(req, struct event_ctx, req);

    if (req->result < 0) {
        fprintf(stderr, "Write error: %s\n", uv_strerror((int)req->result));
        goto cleanup;
    }

    if (event->recorder->expired) {
        goto cleanup;
    }

    uv_fs_read(event->loop, &event->req.read, event->fd[0],
               &event->iov, 1, -1, on_read);
    return;

cleanup:
    cleanup(event);
}

static void on_read(uv_fs_t *req)
{
    struct event_ctx *event;

    event = container_of(req, struct event_ctx, req);

    if (event->recorder->expired)
        goto cleanup;

    if (req->result <= 0) {
        if (req->result != UV_EAGAIN) {
            fprintf(stderr, "Read error: %s\n", uv_strerror(req->result));
            goto cleanup;
        }

        uv_fs_read(event->loop, &event->req.read, event->fd[0],
                   &event->iov, 1, -1, on_read);
        return;
    }

    event->iov.len = req->result;
    uv_fs_write(event->loop, &event->req.write, event->fd[1],
                &event->iov, 1, -1, on_write);
    return;

cleanup:
    cleanup(event);
}

static void on_open(uv_fs_t *req)
{
    struct event_ctx *event;

    event = container_of(req, struct event_ctx, req);

    if (req->result < 0) {
        fprintf(stderr, "Open error: %s\n", uv_strerror((int)req->result));
        return;
    }

    event->fd[0] = req->result;
    uv_fs_read(event->loop, &event->req.read, event->fd[0],
               &event->iov, 1, -1, on_read);
}

static int init_event_ctx(struct event_ctx *event, struct recorder *recorder)
{
    char *buf;
    int len = 4096;

    buf = malloc(sizeof(char) * len);
    if (!event || !buf)
        return -1;

    event->loop = recorder->loop;
    event->recorder = recorder;
    event->fd[0] = -1;
    event->fd[1] = -1;

    event->iov = uv_buf_init(buf, len);

    return 0;
}

int record_events(struct recorder *recorder)
{
    struct config cf = recorder->cf;
    struct event_ctx *event;
    int i;

    recorder->events = malloc(cf.instances_num * sizeof(recorder->events[0]));
    if (!recorder->events)
        return -1;

    for (i = 0; i < cf.instances_num; i++) {
        event = &recorder->events[i];
        assert(!init_event_ctx(event, recorder));
        event->fd[1] = uv_fs_open(event->loop, &event->req.open,
                                  cf.instances_out[i],
                                  O_CREAT | O_RDWR, 0644, NULL);
        uv_fs_open(event->loop, &event->req.open, cf.instances_in[i],
                   O_RDONLY|O_NONBLOCK, 0, on_open);
    }

    return 0;
}

void setup_event_instances(struct config *cf, const char *base, const char **p)
{
    char dir[MAX_PATH_LEN];
    uv_fs_t req;
    int i, r;

    r = uv_fs_mkdir(NULL, &req, cf->output_dir, 0755, NULL);
    assert(r == 0 || r == UV_EEXIST);

    for (i = 0; i < cf->instances_num; i++) {
        create_instance_dir(dir, &req, base, *p);
        create_instance_output(dir, &req, cf->output_dir, *p);
        cf->instances_in[i] = get_instance_input(dir, base, *p);
        cf->instances_out[i] = get_instance_output(dir, cf->output_dir, *p);
        p++;
    }

    uv_fs_req_cleanup(&req);
}
