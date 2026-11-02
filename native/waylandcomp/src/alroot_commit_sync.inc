/* S45: compositor-thread-owned, immutable committed buffer uses.
 * Included by compositor.c after its surface/frame helpers. This is not a
 * per-buffer 'latest fence': each attach retains its own producer dependency.
 */
#define S45_MAX_USES 1024u
#define S45_CACHED_USES 64u

static int s47_progress(void);
static int s47_on_frame_fd(int fd, uint32_t mask, void *data);
static int s47_on_producer_fd(int fd, uint32_t mask, void *data);
static void s47_on_progress_idle(void *data);
static int s45_poll_frame(void);

static void s47_frame_unwatch(void) {
    if (g_s47_frame_source) {
        wl_event_source_remove(g_s47_frame_source);
        g_s47_frame_source = NULL;
        S47_HOST_COUNT(frame_watch_removes);
    }
    g_s47_frame_fd = -1;
}

static int s47_frame_watch(void) {
    const int fd = vkp_frame_completion_fd();
    if (g_s47_frame_source && g_s47_frame_fd != fd) s47_frame_unwatch();
    if (!vkp_s47_enabled() || !g_display || fd < 0 || g_s47_frame_fallback ||
        g_s47_frame_terminal) return 0;
    if (!g_s47_frame_source) {
        g_s47_frame_source = wl_event_loop_add_fd(wl_display_get_event_loop(g_display), fd,
                                                 WL_EVENT_READABLE, s47_on_frame_fd, NULL);
        if (!g_s47_frame_source) {
            g_s47_frame_fallback = 1;
            S47_HOST_COUNT(watch_errors);
            return 0;
        }
        g_s47_frame_fd = fd;
        S47_HOST_COUNT(frame_watch_adds);
    }
    return 1;
}

static void s47_producer_unwatch(struct s45_use *use) {
    if (!use->producer_source) return;
    wl_event_source_remove(use->producer_source);
    use->producer_source = NULL;
    S47_HOST_COUNT(producer_watch_removes);
}

static int s47_producer_watch(struct s45_use *use) {
    if (!vkp_s47_enabled() || !g_display || use->fd < 0 || use->producer_done ||
        use->watch_fallback) return 0;
    if (!use->producer_source) {
        use->producer_source = wl_event_loop_add_fd(wl_display_get_event_loop(g_display), use->fd,
            WL_EVENT_READABLE, s47_on_producer_fd, use);
        if (!use->producer_source) {
            use->watch_fallback = 1;
            S47_HOST_COUNT(watch_errors);
            return 0;
        }
        S47_HOST_COUNT(producer_watch_adds);
    }
    return 1;
}

static void s45_pending_clear(struct surface *s) {
    if (s->pending_s45_buffer) {
        wl_list_remove(&s->pending_s45_destroy.link);
        wl_list_init(&s->pending_s45_destroy.link);
    }
    if (s->pending_s45_fd >= 0) close(s->pending_s45_fd);
    s->pending_s45_buffer = NULL;
    s->pending_s45_fd = -1;
    s->pending_s45_set = 0;
}

static void s45_pending_buffer_destroyed(struct wl_listener *listener, void *data) {
    struct surface *s = wl_container_of(listener, s, pending_s45_destroy);
    wl_list_remove(&s->pending_s45_destroy.link);
    wl_list_init(&s->pending_s45_destroy.link);
    s->pending_s45_buffer = NULL;
    if (s->pending_s45_fd >= 0) close(s->pending_s45_fd);
    s->pending_s45_fd = -1;
    /* Keep pending_s45_set: a subsequent commit must reject the dead target. */
}

/* SYNC_IOC_FILE_INFO validates the object as well as its terminal status.
 * Merely seeing poll()>0 is insufficient (POLLNVAL/error is not readiness).
 */
static int s45_fence_status(int fd) {
    S47_HOST_COUNT(producer_queries);
    struct sync_file_info info = {0};
    int result;
    do { result = ioctl(fd, SYNC_IOC_FILE_INFO, &info); }
    while (result < 0 && errno == EINTR);
    if (result < 0) return -1;
    return info.status > 0 ? 1 : info.status < 0 ? -1 : 0;
}

void alroot_gpu_set_acquire(struct wl_client *client, struct wl_resource *manager,
                           struct wl_resource *surface_resource,
                           struct wl_resource *buffer, int fd, int ready) {
    struct surface *s = NULL;
    struct dmabuf_buffer *b = NULL;
    int valid = surface_resource && buffer &&
        wl_resource_get_client(surface_resource) == client &&
        wl_resource_get_client(buffer) == client &&
        wl_resource_instance_of(surface_resource, &wl_surface_interface, &surface_impl) &&
        wl_resource_instance_of(buffer, &wl_buffer_interface, &alroot_gpu_buffer_impl);
    if (valid) {
        s = wl_resource_get_user_data(surface_resource);
        b = wl_resource_get_user_data(buffer);
        valid = s && b && b->gpu_ahb;
    }
    int status = -1;
    if (valid && !ready) {
        if (fd >= 0) {
            S47_HOST_COUNT(producer_validation_queries);
            status = s45_fence_status(fd);
        }
        valid = fd >= 0 && status >= 0 && fcntl(fd, F_SETFD, FD_CLOEXEC) == 0;
    }
    if (!valid) {
        if (fd >= 0) close(fd);
        wl_resource_post_error(manager, ALROOT_GPU_BUFFER_V1_ERROR_INVALID_SYNC,
                               "invalid AHB acquire-fence target or sync_file");
        g_s45_protocol_dirty = 1;
        g_s45.invalid_fences++;
        return;
    }
    /* Replacement changes only uncommitted state. No old committed use is
     * modified, and the superseded pending FD is closed exactly once. */
    s45_pending_clear(s);
    if (!ready && vkp_s47_enabled() && status > 0) {
        close(fd);
        fd = -1;
        ready = 1;
        S47_HOST_COUNT(producer_ready_elisions);
    }
    s->pending_s45_set = 1;
    s->pending_s45_fd = ready ? -1 : fd;
    s->pending_s45_buffer = buffer;
    s->pending_s45_destroy.notify = s45_pending_buffer_destroyed;
    wl_resource_add_destroy_listener(buffer, &s->pending_s45_destroy);
}

static int s45_latch_pending(struct surface *s, struct wl_client *client,
                             int *fd, int *external) {
    *fd = -1;
    *external = 0;
    if (!s->pending_s45_set) return 1;
    if (!s->pending_attach || !s->pending_buffer ||
        s->pending_buffer != s->pending_s45_buffer) {
        s45_pending_clear(s);
        wl_client_post_implementation_error(client,
            "AHB acquire fence must match the next newly attached buffer");
        g_s45_protocol_dirty = 1;
        return 0;
    }
    *fd = s->pending_s45_fd;
    *external = 1;
    s->pending_s45_fd = -1;
    s45_pending_clear(s);
    return 1;
}

static void s45_use_resource_destroyed(struct wl_listener *listener, void *data) {
    struct s45_use *use = wl_container_of(listener, use, destroy);
    wl_list_remove(&use->destroy.link);
    wl_list_init(&use->destroy.link);
    use->resource = NULL;
}

static struct s45_use *s45_use_create(struct surface *s, struct dmabuf_buffer *b,
                                     struct wl_resource *buffer, int fd, int external) {
    struct s45_use *use;
    if (!b || b->ahb_state) {
        if (fd >= 0) close(fd);
        return NULL;
    }
    /* A paced release for an older use cannot outlive a new attachment. */
    struct pending_release *release, *release_tmp;
    int canceled = 0;
    wl_list_for_each_safe(release, release_tmp, &g_pending_releases, link) {
        if (release->buffer != buffer) continue;
        pending_release_free(release);
        canceled = 1;
    }
    if (canceled) arm_release_timer();
    /* Geometry-only reattach retains the exact prior content dependency. */
    if (!external && s->gpu_use && s->gpu_use->buffer == b &&
        s->gpu_use->resource == buffer && !s->gpu_use->retired)
        return s->gpu_use;
    if (g_s45.live >= S45_MAX_USES) {
        if (fd >= 0) close(fd);
        return NULL;
    }
    if (!wl_list_empty(&g_s45_free_uses)) {
        use = wl_container_of(g_s45_free_uses.next, use, link);
        wl_list_remove(&use->link);
        g_s45.cached--;
        memset(use, 0, sizeof(*use));
    } else {
        use = calloc(1, sizeof(*use));
        if (!use) { if (fd >= 0) close(fd); return NULL; }
    }
    use->serial = ++g_s45.next_serial;
    use->buffer = b;
    b->refs++;
    use->resource = buffer;
    use->fd = fd;
    use->producer_done = fd < 0;
    use->external = external;
    use->destroy.notify = s45_use_resource_destroyed;
    wl_resource_add_destroy_listener(buffer, &use->destroy);
    wl_list_insert(g_s45_uses.prev, &use->link);
    g_s45.live++;
    if (g_s45.live > g_s45.peak) g_s45.peak = g_s45.live;
    g_s45.commits++;
    if (external) g_s45.explicit_commits++;
    return use;
}

static void s47_producer_resolve(struct s45_use *use, int status) {
    if (status) {
        /* Remove the event source before closing/reusing its descriptor. */
        s47_producer_unwatch(use);
        if (use->fd >= 0) close(use->fd);
        use->fd = -1;
        use->producer_done = status;
        if (status < 0) {
            g_s45.invalid_fences++;
            if (use->resource) {
                wl_client_post_implementation_error(wl_resource_get_client(use->resource),
                                                     "AHB producer fence failed");
                g_s45_protocol_dirty = 1;
            }
        }
    }
}

static int s45_use_ready(struct s45_use *use) {
    if (!use || use->producer_done > 0) return 1;
    if (use->producer_done < 0) return -1;
    if (s47_producer_watch(use)) return 0;
    int status = s45_fence_status(use->fd);
    s47_producer_resolve(use, status);
    return status;
}

static void s47_arm_timer(void) {
    if (!g_display || g_s45_timer_armed) return;
    if (!g_s45_timer)
        g_s45_timer = wl_event_loop_add_timer(wl_display_get_event_loop(g_display),
                                             s45_on_timer, NULL);
    if (g_s45_timer) {
        g_s45_timer_armed = 1;
        S47_HOST_COUNT(timer_arms);
        wl_event_source_timer_update(g_s45_timer, 1);
    }
}

static void s45_arm(void) {
    if (!vkp_s47_enabled()) { s47_arm_timer(); return; }
    if (!g_display) return;
    /* A normal in-flight frame wakes through its exported sync_file. A
     * source-only retirement gets one event-loop idle to install its watches,
     * not a recurring completion-query timer. */
    if (s47_frame_watch() && !g_s45.retired) return;
    if (vkp_frame_pending() && !vkp_frame_completion_exported() && !g_s47_frame_terminal) {
        s47_arm_timer();
        return;
    }
    if (!g_s47_progress_idle)
        g_s47_progress_idle = wl_event_loop_add_idle(wl_display_get_event_loop(g_display),
                                                    s47_on_progress_idle, NULL);
    if (!g_s47_progress_idle) {
        S47_HOST_COUNT(watch_errors);
        s47_arm_timer();
    }
}

static void s45_use_retire(struct s45_use *use, int paced) {
    if (!use || use->retired) return;
    use->retired = 1;
    g_s45.retired++;
    use->paced = paced;
    use->since_ns = now_ns();
    s45_arm();
}

static void s45_use_activate(struct surface *s, struct s45_use *use) {
    if (s->gpu_use != use) s45_use_retire(s->gpu_use, 1);
    s->gpu_use = use;
    if (use) use->owner = s;
}

/* Return true when a committed-use record owns the release obligation. */
static int s45_drop_buffer(struct surface *s, struct dmabuf_buffer *b,
                           struct wl_resource *buffer, int paced) {
    struct s45_use *use;
    int found = 0;
    wl_list_for_each(use, &g_s45_uses, link) {
        if (use->buffer != b) continue;
        found = 1;
        if (use->owner == s) s45_use_retire(use, paced);
    }
    if (s->gpu_use && s->gpu_use->buffer == b) s->gpu_use = NULL;
    return found;
}

static void s45_release_idle(struct surface *s, struct wl_resource *buffer) {
    struct dmabuf_buffer *b = get_dmabuf(buffer);
    if (b && s45_drop_buffer(s, b, buffer, 0)) return;
    wl_buffer_send_release(buffer);
    g_s45_protocol_dirty = 1;
}

static void s45_forget_surface(struct surface *s) {
    struct s45_use *use;
    s45_pending_clear(s);
    wl_list_for_each(use, &g_s45_uses, link) {
        if (use->owner == s) {
            s45_use_retire(use, 0);
            use->owner = NULL;
        }
    }
    s->gpu_use = NULL;
}

static void s45_use_free(struct s45_use *use) {
    s47_producer_unwatch(use);
    if (use->fd >= 0) close(use->fd);
    if (use->resource) wl_list_remove(&use->destroy.link);
    wl_list_remove(&use->link);
    dmabuf_buffer_unref(use->buffer);
    g_s45.live--;
    if (use->retired) g_s45.retired--;
    if (g_s45.cached < S45_CACHED_USES) {
        wl_list_insert(&g_s45_free_uses, &use->link);
        g_s45.cached++;
    } else free(use);
}

static void s45_retire_ready(void) {
    if (!g_s45.retired) return;
restart:
    ;
    struct s45_use *use, *other, *tmp;
    wl_list_for_each(use, &g_s45_uses, link) {
        if (!use->retired || use->frame_refs || vkp_image_busy(use->buffer->img)) continue;
        struct dmabuf_buffer *b = use->buffer;
        int pending = 0, failed = 0;
        struct wl_resource *resource = NULL;
        struct surface *owner = NULL;
        int paced = 0;
        int64_t since = use->since_ns;
        wl_list_for_each(other, &g_s45_uses, link) {
            if (other->buffer != b) continue;
            if (!other->retired || other->frame_refs) { pending = 1; break; }
            int ready = s45_use_ready(other);
            if (!ready) { pending = 1; break; }
            if (ready < 0) failed = 1;
            if (other->resource) resource = other->resource;
            if (other->owner) owner = other->owner;
            paced |= other->paced;
        }
        if (pending) continue;
        /* All uses of this wl_buffer retire together. A repeated attach must
         * not receive an older generation's release while it is still live. */
        if (resource && !failed) {
            if (paced && owner) g_s45_protocol_dirty |= release_buffer(owner, resource, since);
            else {
                wl_buffer_send_release(resource);
                perf_note_release(since);
                g_s45_protocol_dirty = 1;
            }
            g_s45.releases++;
        }
        wl_list_for_each_safe(other, tmp, &g_s45_uses, link)
            if (other->buffer == b) s45_use_free(other);
        goto restart;
    }
}

static int s45_prepare_frame(size_t count) {
    if (count <= g_s45_frame.capacity) return 1;
    struct s45_use **uses = realloc(g_s45_frame.uses, count * sizeof(*uses));
    if (!uses) return 0;
    g_s45_frame.uses = uses;
    g_s45_frame.capacity = count;
    return 1;
}

static void s45_capture_frame(const struct vkp_draw *draws, int n, int64_t stamp,
                               int copy, int successful) {
    struct surface *s;
    g_s45_frame.active = 1;
    g_s45_frame.successful = successful;
    g_s45_frame.stamp = stamp;
    g_s45_frame.copy = copy;
    g_s45_frame.hud = successful && g_hud_new;
    if (successful) g_hud_new = 0;
    g_s45_frame.count = 0;
    g_s47_frame_fallback = g_s47_frame_terminal = 0;
    const int full_wait_proof = vkp_s47_enabled() && successful && vkp_frame_sources_submitted();
    for (int i = 0; i < n; i++) {
        if (!draws[i].acquire_serial) continue;
        struct s45_use *use;
        wl_list_for_each(use, &g_s45_uses, link) {
            if (use->serial != draws[i].acquire_serial) continue;
            int duplicate = 0;
            for (size_t j = 0; j < g_s45_frame.count; j++)
                if (g_s45_frame.uses[j] == use) duplicate = 1;
            if (!duplicate) {
                use->frame_proven = full_wait_proof && draws[i].has_acquire_fence &&
                    draws[i].img == use->buffer->img && draws[i].acquire_fence_fd == use->fd &&
                    use->fd >= 0 && use->producer_done == 0;
                use->frame_refs++;
                g_s45_frame.uses[g_s45_frame.count++] = use;
            }
            break;
        }
    }
    wl_list_for_each(s, &g_surfaces, link) {
        wl_list_insert_list(g_s45_frame.frames.prev, &s->frames);
        wl_list_init(&s->frames);
        if (successful && s->drawn) {
            wl_list_insert_list(g_s45_frame.feedback.prev, &s->feedback);
            wl_list_init(&s->feedback);
        } else {
            if (!wl_list_empty(&s->feedback)) g_s45_protocol_dirty = 1;
            feedback_discard_all(&s->feedback);
        }
    }
    s45_arm();
}

static void s45_finish_frame(int completed) {
    if (!g_s45_frame.active) return;
    const int full_success = completed && g_s45_frame.successful;
    if (!wl_list_empty(&g_s45_frame.frames) || !wl_list_empty(&g_s45_frame.feedback))
        g_s45_protocol_dirty = 1;
    for (size_t i = 0; i < g_s45_frame.count; i++) {
        struct s45_use *use = g_s45_frame.uses[i];
        if (use->frame_refs) use->frame_refs--;
        /* A helper can submit only part of a scene before returning an
         * error. Confirm each original dependency rather than inferring
         * all producers completed from the host submission's fence. */
        if (full_success && use->frame_proven && use->producer_done == 0) {
            /* Exact immutable serial/image/FD was part of the successfully
             * submitted source wait set, and that exact host frame completed.
             * This proves the GPU dependency was consumed, but a Vulkan
             * binary wait does not expose sync_file's negative error status.
             * Preserve one terminal validation, without repeated pending
             * queries. Failed/partial helpers never receive this proof. */
            S47_HOST_COUNT(producer_gpu_proofs);
            S47_HOST_COUNT(producer_terminal_queries);
            int status = s45_fence_status(use->fd);
            s47_producer_resolve(use, status);
            if (status <= 0) g_s45_frame.successful = 0;
            if (!status) {
                /* Inconsistent completion must not release a still-writing
                 * producer. Retain its original FD and wait for its event. */
                (void)s47_producer_watch(use);
                g_s45.invalid_fences++;
                if (use->resource) {
                    wl_client_post_implementation_error(wl_resource_get_client(use->resource),
                        "AHB producer fence remained pending after its GPU wait completed");
                    g_s45_protocol_dirty = 1;
                }
            }
        } else if (completed) (void)s45_use_ready(use);
        if (completed && use->producer_done < 0) g_s45_frame.successful = 0;
        use->frame_proven = 0;
    }
    if (completed && g_s45_frame.successful) {
        feedback_present_all(&g_s45_frame.feedback, g_s45_frame.stamp);
        g_stat_frames++;
        if (g_s45_frame.copy) g_perf.copy_scenes++;
        if (g_s45_frame.hud) hud_frame_shown(g_s45_frame.stamp);
    } else feedback_discard_all(&g_s45_frame.feedback);
    fire_frames(&g_s45_frame.frames);
    g_s45_frame.count = 0;
    g_s45_frame.active = 0;
}

/* Shared by the timer and vsync. The frame snapshot is consumed only once,
 * even when the two callbacks run consecutively after the same completion. */
static int s45_poll_frame(void) {
    if (!vkp_frame_pending()) { s47_frame_unwatch(); return 0; }
    if (vkp_s47_enabled() && g_s47_frame_source) return 0;
    enum vkp_frame_poll_result status = vkp_frame_poll();
    if (status == VKP_FRAME_COMPLETE) {
        s47_frame_unwatch();
        s45_finish_frame(1);
        return 1;
    }
    if (status == VKP_FRAME_FAILED && !vkp_frame_pending()) {
        s47_frame_unwatch();
        s45_finish_frame(0);
        return 1;
    }
    if (status == VKP_FRAME_FAILED && vkp_s47_enabled()) {
        g_s47_frame_terminal = 1;
        s47_frame_unwatch();
    }
    return 0;
}

static void s45_flush_protocol(void) {
    if (!g_s45_protocol_dirty) return;
    g_s45_protocol_dirty = 0;
    wl_display_flush_clients(g_display);
}

static int s47_progress(void) {
    s45_poll_frame();
    s45_retire_ready();
    int frame_event = vkp_s47_enabled() && s47_frame_watch();
    int need_timer = vkp_frame_pending() && !frame_event && !g_s47_frame_terminal;
    struct s45_use *use;
    if (g_s45.retired)
        wl_list_for_each(use, &g_s45_uses, link) {
            if (!use->retired) continue;
            if (!vkp_s47_enabled()) {
                if (!use->producer_done || use->frame_refs) need_timer = 1;
                continue;
            }
            if (use->frame_refs) {
                if (!frame_event && !g_s47_frame_terminal) need_timer = 1;
            } else if (!use->producer_done) {
                if (s45_use_ready(use) == 0 && !use->producer_source) need_timer = 1;
            }
        }
    struct surface *s;
    wl_list_for_each(s, &g_surfaces, link) {
        if (s->role != ROLE_CURSOR || !s->idle_buffer) continue;
        if (vkp_frame_pending()) {
            if (!frame_event && !g_s47_frame_terminal) need_timer = 1;
            continue;
        }
        if (s45_use_ready(s->gpu_use) == 0) {
            if (!s->gpu_use || !s->gpu_use->producer_source) need_timer = 1;
            continue;
        }
        struct wl_resource *buffer = surface_take_idle(s);
        if (s45_use_ready(s->gpu_use) > 0) cursor_publish_buffer(s, buffer);
        s45_release_idle(s, buffer);
    }
    if (need_timer) s47_arm_timer();
    else if (g_dirty) schedule_render();
    s45_flush_protocol();
    return 0;
}

static int s45_on_timer(void *data) {
    g_s45_timer_armed = 0;
    S47_HOST_COUNT(timer_callbacks);
    return s47_progress();
}

static void s47_on_progress_idle(void *data) {
    /* Wayland destroys an idle source when its callback returns. */
    g_s47_progress_idle = NULL;
    S47_HOST_COUNT(progress_idles);
    (void)s47_progress();
}

static int s47_on_frame_fd(int fd, uint32_t mask, void *data) {
    if (!g_s47_frame_source || fd != g_s47_frame_fd || fd != vkp_frame_completion_fd()) return 0;
    S47_HOST_COUNT(frame_watch_callbacks);
    s47_frame_unwatch();
    /* A spurious notification retains a bounded timer fallback for this one
     * generation; it never falls back to the copy-reset native VkFence. */
    g_s47_frame_fallback = 1;
    return s47_progress();
}

static int s47_on_producer_fd(int fd, uint32_t mask, void *data) {
    struct s45_use *use = data;
    if (!use || !use->producer_source || use->fd != fd || use->producer_done) return 0;
    S47_HOST_COUNT(producer_watch_callbacks);
    s47_producer_unwatch(use);
    int status = s45_fence_status(fd);
    if (!status) {
        /* Do not turn an anomalous readable-but-unsignaled FD into a ready
         * producer, nor spin a level-triggered event source indefinitely. */
        use->watch_fallback = 1;
        S47_HOST_COUNT(watch_errors);
    }
    s47_producer_resolve(use, status);
    return s47_progress(); /* May free use: no access after this point. */
}

static void s47_report(void) {
    if (!vkp_s47_profile_enabled()) return;
    struct vkp_s47_stats backend;
    vkp_s47_get_stats(&backend);
    char line[4096];
    size_t at = (size_t)snprintf(line, sizeof(line),
        "S47_HOST_STATS {\"pid\":%d,\"enabled\":%s,\"completion_capable\":%s,\"final\":true",
        getpid(), backend.enabled ? "true" : "false", backend.completion_capable ? "true" : "false");
#define ADD_BACKEND(name) at += (size_t)snprintf(line + at, sizeof(line) - at, ",\"" #name "\":%llu", (unsigned long long)backend.name)
    ADD_BACKEND(async_submitted); ADD_BACKEND(async_retired); ADD_BACKEND(source_proof_frames);
    ADD_BACKEND(empty_async_frames);
    ADD_BACKEND(export_attempts); ADD_BACKEND(export_fds); ADD_BACKEND(export_ready); ADD_BACKEND(export_errors);
    ADD_BACKEND(completion_fd_polls); ADD_BACKEND(completion_fd_queries); ADD_BACKEND(native_fence_polls);
    ADD_BACKEND(completion_errors); ADD_BACKEND(fence_wait_calls); ADD_BACKEND(startup_fence_wait_calls);
    ADD_BACKEND(cancel_wait_calls); ADD_BACKEND(device_idle_calls); ADD_BACKEND(reconfigure_idle_calls);
    ADD_BACKEND(error_idle_calls); ADD_BACKEND(shutdown_idle_calls);
#undef ADD_BACKEND
#define ADD_HOST(name) at += (size_t)snprintf(line + at, sizeof(line) - at, ",\"" #name "\":%llu", (unsigned long long)g_s47.name)
    ADD_HOST(frame_watch_adds); ADD_HOST(frame_watch_callbacks); ADD_HOST(frame_watch_removes);
    ADD_HOST(producer_queries); ADD_HOST(producer_validation_queries); ADD_HOST(producer_terminal_queries);
    ADD_HOST(producer_ready_elisions);
    ADD_HOST(producer_gpu_proofs); ADD_HOST(producer_watch_adds); ADD_HOST(producer_watch_callbacks);
    ADD_HOST(producer_watch_removes); ADD_HOST(timer_arms); ADD_HOST(timer_callbacks);
    ADD_HOST(progress_idles); ADD_HOST(watch_errors);
#undef ADD_HOST
    if (at + 2 <= sizeof(line)) { line[at++] = '}'; line[at] = 0; s47_emit_stats(line); }
}

static void s45_shutdown(void) {
    s47_frame_unwatch();
    if (g_s47_progress_idle) {
        wl_event_source_remove(g_s47_progress_idle);
        g_s47_progress_idle = NULL;
    }
    if (g_s45_timer) {
        wl_event_source_remove(g_s45_timer);
        g_s45_timer = NULL;
        g_s45_timer_armed = 0;
    }
    vkp_wait_idle();
    (void)vkp_frame_poll();
    s45_finish_frame(0);
    struct s45_use *use, *tmp;
    wl_list_for_each_safe(use, tmp, &g_s45_uses, link) s45_use_free(use);
    wl_list_for_each_safe(use, tmp, &g_s45_free_uses, link) {
        wl_list_remove(&use->link);
        free(use);
    }
    free(g_s45_frame.uses);
    g_s45_frame.uses = NULL;
    g_s45_frame.capacity = 0;
    g_s45.cached = 0;
    s47_report();
}
