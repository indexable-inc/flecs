#include <query.h>

static ecs_query_cache_kind_t cache_kind = EcsQueryCacheDefault;

void External_setup(void) {
    const char *cache_param = test_param("cache_kind");
    if (cache_param) {
        if (!strcmp(cache_param, "default")) {
            // already set to default
        } else if (!strcmp(cache_param, "auto")) {
            cache_kind = EcsQueryCacheAuto;
        } else {
            printf("unexpected value for cache_param '%s'\n", cache_param);
        }
    }
}

/* In-memory external source used by the tests. Rows are matched in order. */
typedef struct {
    ecs_entity_t src;
    ecs_id_t id;
    Position value;
} ext_row_t;

typedef struct {
    ext_row_t rows[16];
    int32_t count;
    int32_t started;
    int32_t finished;
} ext_store_t;

static void ext_add(
    ext_store_t *s,
    ecs_entity_t src,
    ecs_id_t id,
    float x,
    float y)
{
    ext_row_t *r = &s->rows[s->count ++];
    r->src = src;
    r->id = id;
    r->value.x = x;
    r->value.y = y;
}

static bool ext_next(
    ecs_external_iter_t *it)
{
    ext_store_t *s = it->ctx;
    if (!it->state[1]) {
        it->state[1] = 1;
        s->started ++;
    }

    while (it->state[0] < (uint64_t)s->count) {
        ext_row_t *r = &s->rows[it->state[0] ++];
        if (it->src && r->src != it->src) {
            continue;
        }

        if (!ecs_id_match(r->id, it->id)) {
            continue;
        }

        it->match_src = r->src;
        it->match_id = r->id;
        it->ptr = &r->value;
        return true;
    }

    return false;
}

static void ext_fini(
    ecs_external_iter_t *it)
{
    ext_store_t *s = it->ctx;
    s->finished ++;
}

static void ext_set(
    ecs_world_t *world,
    ecs_entity_t id,
    ext_store_t *s)
{
    test_int(0, ecs_set_external_source(world, id,
        &(ecs_external_source_desc_t){
            .next = ext_next,
            .fini = ext_fini,
            .ctx = s
        }));
}

void External_select_this(void) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);

    ecs_entity_t e1 = ecs_new(world);
    ecs_entity_t e2 = ecs_new(world);

    ext_store_t s = {0};
    ext_add(&s, e1, ecs_id(Position), 10, 20);
    ext_add(&s, e2, ecs_id(Position), 30, 40);
    ext_set(world, ecs_id(Position), &s);

    ecs_query_t *q = ecs_query(world, {
        .expr = "Position",
        .cache_kind = cache_kind
    });
    test_assert(q != NULL);

    ecs_iter_t it = ecs_query_iter(world, q);
    test_bool(true, ecs_query_next(&it));
    test_int(1, it.count);
    test_uint(e1, it.entities[0]);
    test_uint(ecs_id(Position), ecs_field_id(&it, 0));
    test_bool(true, ecs_field_is_set(&it, 0));
    {
        Position *p = ecs_field(&it, Position, 0);
        test_assert(p != NULL);
        test_int(p->x, 10); test_int(p->y, 20);
    }

    test_bool(true, ecs_query_next(&it));
    test_int(1, it.count);
    test_uint(e2, it.entities[0]);
    {
        Position *p = ecs_field(&it, Position, 0);
        test_assert(p != NULL);
        test_int(p->x, 30); test_int(p->y, 40);
    }

    test_bool(false, ecs_query_next(&it));
    test_int(s.started, s.finished);

    ecs_query_fini(q);

    ecs_fini(world);
}

void External_select_empty(void) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);

    ext_store_t s = {0};
    ext_set(world, ecs_id(Position), &s);

    ecs_query_t *q = ecs_query(world, {
        .expr = "Position",
        .cache_kind = cache_kind
    });
    test_assert(q != NULL);

    ecs_iter_t it = ecs_query_iter(world, q);
    test_bool(false, ecs_query_next(&it));
    test_int(1, s.started);
    test_int(1, s.finished);

    ecs_query_fini(q);

    ecs_fini(world);
}

void External_select_skips_prefab(void) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);

    ecs_entity_t e1 = ecs_new(world);
    ecs_entity_t p = ecs_new_w_id(world, EcsPrefab);

    ext_store_t s = {0};
    ext_add(&s, p, ecs_id(Position), 1, 2);
    ext_add(&s, e1, ecs_id(Position), 10, 20);
    ext_set(world, ecs_id(Position), &s);

    ecs_query_t *q = ecs_query(world, {
        .expr = "Position",
        .cache_kind = cache_kind
    });
    test_assert(q != NULL);

    ecs_iter_t it = ecs_query_iter(world, q);
    test_bool(true, ecs_query_next(&it));
    test_int(1, it.count);
    test_uint(e1, it.entities[0]);
    test_bool(false, ecs_query_next(&it));

    ecs_query_fini(q);

    ecs_fini(world);
}

void External_external_then_table_term(void) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);
    ECS_COMPONENT(world, Velocity);

    ecs_entity_t e1 = ecs_new(world);
    ecs_entity_t e2 = ecs_new(world);
    ecs_entity_t e3 = ecs_new(world);
    ecs_set(world, e1, Velocity, {1, 2});
    ecs_set(world, e3, Velocity, {3, 4});

    ext_store_t s = {0};
    ext_add(&s, e1, ecs_id(Position), 10, 20);
    ext_add(&s, e2, ecs_id(Position), 30, 40);
    ext_add(&s, e3, ecs_id(Position), 50, 60);
    ext_set(world, ecs_id(Position), &s);

    ecs_query_t *q = ecs_query(world, {
        .expr = "Position, Velocity",
        .cache_kind = cache_kind
    });
    test_assert(q != NULL);

    ecs_iter_t it = ecs_query_iter(world, q);
    test_bool(true, ecs_query_next(&it));
    test_int(1, it.count);
    test_uint(e1, it.entities[0]);
    {
        Position *p = ecs_field(&it, Position, 0);
        Velocity *v = ecs_field(&it, Velocity, 1);
        test_int(p->x, 10); test_int(p->y, 20);
        test_int(v->x, 1); test_int(v->y, 2);
    }

    test_bool(true, ecs_query_next(&it));
    test_int(1, it.count);
    test_uint(e3, it.entities[0]);
    {
        Position *p = ecs_field(&it, Position, 0);
        Velocity *v = ecs_field(&it, Velocity, 1);
        test_int(p->x, 50); test_int(p->y, 60);
        test_int(v->x, 3); test_int(v->y, 4);
    }

    test_bool(false, ecs_query_next(&it));
    test_int(s.started, s.finished);

    ecs_query_fini(q);

    ecs_fini(world);
}

void External_table_then_external_term(void) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);
    ECS_COMPONENT(world, Velocity);

    ecs_entity_t e1 = ecs_new(world);
    ecs_entity_t e2 = ecs_new(world);
    ecs_entity_t e3 = ecs_new(world);
    ecs_set(world, e1, Velocity, {1, 2});
    ecs_set(world, e2, Velocity, {3, 4});
    ecs_set(world, e3, Velocity, {5, 6});

    ext_store_t s = {0};
    ext_add(&s, e1, ecs_id(Position), 10, 20);
    ext_add(&s, e3, ecs_id(Position), 50, 60);
    ext_set(world, ecs_id(Position), &s);

    ecs_query_t *q = ecs_query(world, {
        .expr = "Velocity, Position",
        .cache_kind = cache_kind
    });
    test_assert(q != NULL);

    ecs_iter_t it = ecs_query_iter(world, q);
    test_bool(true, ecs_query_next(&it));
    test_int(1, it.count);
    test_uint(e1, it.entities[0]);
    {
        Velocity *v = ecs_field(&it, Velocity, 0);
        Position *p = ecs_field(&it, Position, 1);
        test_int(v->x, 1); test_int(v->y, 2);
        test_int(p->x, 10); test_int(p->y, 20);
    }

    test_bool(true, ecs_query_next(&it));
    test_int(1, it.count);
    test_uint(e3, it.entities[0]);
    {
        Velocity *v = ecs_field(&it, Velocity, 0);
        Position *p = ecs_field(&it, Position, 1);
        test_int(v->x, 5); test_int(v->y, 6);
        test_int(p->x, 50); test_int(p->y, 60);
    }

    test_bool(false, ecs_query_next(&it));
    test_int(s.started, s.finished);

    ecs_query_fini(q);

    ecs_fini(world);
}

void External_not(void) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);
    ECS_COMPONENT(world, Velocity);

    ecs_entity_t e1 = ecs_new(world);
    ecs_entity_t e2 = ecs_new(world);
    ecs_entity_t e3 = ecs_new(world);
    ecs_set(world, e1, Velocity, {1, 2});
    ecs_set(world, e2, Velocity, {3, 4});
    ecs_set(world, e3, Velocity, {5, 6});

    ext_store_t s = {0};
    ext_add(&s, e2, ecs_id(Position), 30, 40);
    ext_set(world, ecs_id(Position), &s);

    ecs_query_t *q = ecs_query(world, {
        .expr = "Velocity, !Position",
        .cache_kind = cache_kind
    });
    test_assert(q != NULL);

    ecs_iter_t it = ecs_query_iter(world, q);
    test_bool(true, ecs_query_next(&it));
    test_int(1, it.count);
    test_uint(e1, it.entities[0]);
    test_bool(false, ecs_field_is_set(&it, 1));
    {
        Velocity *v = ecs_field(&it, Velocity, 0);
        test_int(v->x, 1); test_int(v->y, 2);
    }

    test_bool(true, ecs_query_next(&it));
    test_int(1, it.count);
    test_uint(e3, it.entities[0]);
    test_bool(false, ecs_field_is_set(&it, 1));
    {
        Velocity *v = ecs_field(&it, Velocity, 0);
        test_int(v->x, 5); test_int(v->y, 6);
    }

    test_bool(false, ecs_query_next(&it));
    test_int(s.started, s.finished);

    ecs_query_fini(q);

    ecs_fini(world);
}

void External_optional(void) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);
    ECS_COMPONENT(world, Velocity);

    ecs_entity_t e1 = ecs_new(world);
    ecs_entity_t e2 = ecs_new(world);
    ecs_set(world, e1, Velocity, {1, 2});
    ecs_set(world, e2, Velocity, {3, 4});

    ext_store_t s = {0};
    ext_add(&s, e2, ecs_id(Position), 30, 40);
    ext_set(world, ecs_id(Position), &s);

    ecs_query_t *q = ecs_query(world, {
        .expr = "Velocity, ?Position",
        .cache_kind = cache_kind
    });
    test_assert(q != NULL);

    int32_t e1_count = 0, e2_count = 0;
    ecs_iter_t it = ecs_query_iter(world, q);
    while (ecs_query_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            if (it.entities[i] == e1) {
                e1_count ++;
                test_bool(false, ecs_field_is_set(&it, 1));
            } else if (it.entities[i] == e2) {
                e2_count ++;
                test_bool(true, ecs_field_is_set(&it, 1));
                test_int(1, it.count);
                Position *p = ecs_field(&it, Position, 1);
                test_int(p->x, 30); test_int(p->y, 40);
            } else {
                test_assert(false);
            }
        }
    }

    test_int(1, e1_count);
    test_int(1, e2_count);
    test_int(s.started, s.finished);

    ecs_query_fini(q);

    ecs_fini(world);
}

void External_optional_pair_var_tgt(void) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Velocity);
    ECS_TAG(world, Likes);
    ecs_entity_t alice = ecs_entity(world, { .name = "alice" });
    ecs_entity_t bob = ecs_entity(world, { .name = "bob" });

    ecs_entity_t e1 = ecs_new(world);
    ecs_entity_t e2 = ecs_new(world);
    ecs_entity_t e3 = ecs_new(world);
    ecs_set(world, e1, Velocity, {1, 2});
    ecs_set(world, e2, Velocity, {3, 4});
    ecs_set(world, e3, Velocity, {5, 6});

    ext_store_t s = {0};
    ext_add(&s, e1, ecs_pair(Likes, alice), 0, 0);
    ext_add(&s, e1, ecs_pair(Likes, bob), 0, 0);
    ext_add(&s, e3, ecs_pair(Likes, bob), 0, 0);
    ext_set(world, Likes, &s);

    ecs_query_t *q = ecs_query(world, {
        .expr = "Velocity, ?(Likes, $x)",
        .cache_kind = cache_kind
    });
    test_assert(q != NULL);

    int32_t x_var = ecs_query_find_var(q, "x");
    test_assert(x_var != -1);

    ecs_iter_t it = ecs_query_iter(world, q);
    test_bool(true, ecs_query_next(&it));
    test_int(1, it.count);
    test_uint(e1, it.entities[0]);
    test_bool(true, ecs_field_is_set(&it, 1));
    test_uint(alice, ecs_iter_get_var(&it, x_var));

    test_bool(true, ecs_query_next(&it));
    test_int(1, it.count);
    test_uint(e1, it.entities[0]);
    test_bool(true, ecs_field_is_set(&it, 1));
    test_uint(bob, ecs_iter_get_var(&it, x_var));

    test_bool(true, ecs_query_next(&it));
    test_int(1, it.count);
    test_uint(e2, it.entities[0]);
    test_bool(false, ecs_field_is_set(&it, 1));
    test_assert(ecs_field(&it, Velocity, 0) != NULL);

    test_bool(true, ecs_query_next(&it));
    test_int(1, it.count);
    test_uint(e3, it.entities[0]);
    test_bool(true, ecs_field_is_set(&it, 1));
    test_uint(bob, ecs_iter_get_var(&it, x_var));

    test_bool(false, ecs_query_next(&it));
    test_int(s.started, s.finished);

    ecs_query_fini(q);

    ecs_fini(world);
}

void External_fixed_src(void) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);

    ecs_entity_t e1 = ecs_entity(world, { .name = "e1" });
    ecs_entity_t e2 = ecs_entity(world, { .name = "e2" });

    ext_store_t s = {0};
    ext_add(&s, e1, ecs_id(Position), 10, 20);
    ext_add(&s, e2, ecs_id(Position), 30, 40);
    ext_set(world, ecs_id(Position), &s);

    ecs_query_t *q = ecs_query(world, {
        .expr = "Position(e2)",
        .cache_kind = cache_kind
    });
    test_assert(q != NULL);

    ecs_iter_t it = ecs_query_iter(world, q);
    test_bool(true, ecs_query_next(&it));
    test_int(0, it.count);
    test_uint(e2, ecs_field_src(&it, 0));
    {
        Position *p = ecs_field(&it, Position, 0);
        test_int(p->x, 30); test_int(p->y, 40);
    }
    test_bool(false, ecs_query_next(&it));
    test_int(s.started, s.finished);

    ecs_query_fini(q);

    ecs_fini(world);
}

void External_fixed_src_no_match(void) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);

    ecs_entity_t e1 = ecs_entity(world, { .name = "e1" });
    ecs_entity(world, { .name = "e2" });

    ext_store_t s = {0};
    ext_add(&s, e1, ecs_id(Position), 10, 20);
    ext_set(world, ecs_id(Position), &s);

    ecs_query_t *q = ecs_query(world, {
        .expr = "Position(e2)",
        .cache_kind = cache_kind
    });
    test_assert(q != NULL);

    ecs_iter_t it = ecs_query_iter(world, q);
    test_bool(false, ecs_query_next(&it));
    test_int(s.started, s.finished);

    ecs_query_fini(q);

    ecs_fini(world);
}

void External_var_src(void) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);
    ECS_COMPONENT(world, Velocity);

    ecs_entity_t e1 = ecs_new(world);
    ecs_entity_t e2 = ecs_new(world);
    ecs_set(world, e2, Velocity, {3, 4});

    ext_store_t s = {0};
    ext_add(&s, e1, ecs_id(Position), 10, 20);
    ext_add(&s, e2, ecs_id(Position), 30, 40);
    ext_set(world, ecs_id(Position), &s);

    ecs_query_t *q = ecs_query(world, {
        .expr = "Position($x), Velocity($x)",
        .cache_kind = cache_kind
    });
    test_assert(q != NULL);

    int32_t x_var = ecs_query_find_var(q, "x");
    test_assert(x_var != -1);

    ecs_iter_t it = ecs_query_iter(world, q);
    test_bool(true, ecs_query_next(&it));
    test_uint(e2, ecs_iter_get_var(&it, x_var));
    test_uint(e2, ecs_field_src(&it, 0));
    test_uint(e2, ecs_field_src(&it, 1));
    {
        Position *p = ecs_field(&it, Position, 0);
        test_int(p->x, 30); test_int(p->y, 40);
    }
    test_bool(false, ecs_query_next(&it));
    test_int(s.started, s.finished);

    ecs_query_fini(q);

    ecs_fini(world);
}

void External_pair_fixed_tgt(void) {
    ecs_world_t *world = ecs_mini();

    ECS_TAG(world, Likes);
    ecs_entity_t alice = ecs_entity(world, { .name = "alice" });
    ecs_entity_t bob = ecs_entity(world, { .name = "bob" });

    ecs_entity_t e1 = ecs_new(world);
    ecs_entity_t e2 = ecs_new(world);

    ext_store_t s = {0};
    ext_add(&s, e1, ecs_pair(Likes, alice), 0, 0);
    ext_add(&s, e2, ecs_pair(Likes, bob), 0, 0);
    ext_set(world, Likes, &s);

    ecs_query_t *q = ecs_query(world, {
        .expr = "(Likes, bob)",
        .cache_kind = cache_kind
    });
    test_assert(q != NULL);

    ecs_iter_t it = ecs_query_iter(world, q);
    test_bool(true, ecs_query_next(&it));
    test_int(1, it.count);
    test_uint(e2, it.entities[0]);
    test_uint(ecs_pair(Likes, bob), ecs_field_id(&it, 0));
    test_bool(false, ecs_query_next(&it));

    ecs_query_fini(q);

    ecs_fini(world);
}

void External_pair_wildcard_tgt(void) {
    ecs_world_t *world = ecs_mini();

    ECS_TAG(world, Likes);
    ecs_entity_t alice = ecs_entity(world, { .name = "alice" });
    ecs_entity_t bob = ecs_entity(world, { .name = "bob" });

    ecs_entity_t e1 = ecs_new(world);

    ext_store_t s = {0};
    ext_add(&s, e1, ecs_pair(Likes, alice), 0, 0);
    ext_add(&s, e1, ecs_pair(Likes, bob), 0, 0);
    ext_set(world, Likes, &s);

    ecs_query_t *q = ecs_query(world, {
        .expr = "(Likes, *)",
        .cache_kind = cache_kind
    });
    test_assert(q != NULL);

    ecs_iter_t it = ecs_query_iter(world, q);
    test_bool(true, ecs_query_next(&it));
    test_int(1, it.count);
    test_uint(e1, it.entities[0]);
    test_uint(ecs_pair(Likes, alice), ecs_field_id(&it, 0));

    test_bool(true, ecs_query_next(&it));
    test_int(1, it.count);
    test_uint(e1, it.entities[0]);
    test_uint(ecs_pair(Likes, bob), ecs_field_id(&it, 0));

    test_bool(false, ecs_query_next(&it));
    test_int(s.started, s.finished);

    ecs_query_fini(q);

    ecs_fini(world);
}

void External_pair_var_tgt_feeds_term(void) {
    ecs_world_t *world = ecs_mini();

    ECS_TAG(world, Likes);
    ECS_TAG(world, Tag);
    ecs_entity_t alice = ecs_entity(world, { .name = "alice" });
    ecs_entity_t bob = ecs_entity(world, { .name = "bob" });
    ecs_add(world, bob, Tag);

    ecs_entity_t e1 = ecs_new(world);
    ecs_entity_t e2 = ecs_new(world);

    ext_store_t s = {0};
    ext_add(&s, e1, ecs_pair(Likes, alice), 0, 0);
    ext_add(&s, e2, ecs_pair(Likes, bob), 0, 0);
    ext_set(world, Likes, &s);

    ecs_query_t *q = ecs_query(world, {
        .expr = "(Likes, $x), Tag($x)",
        .cache_kind = cache_kind
    });
    test_assert(q != NULL);

    int32_t x_var = ecs_query_find_var(q, "x");
    test_assert(x_var != -1);

    ecs_iter_t it = ecs_query_iter(world, q);
    test_bool(true, ecs_query_next(&it));
    test_int(1, it.count);
    test_uint(e2, it.entities[0]);
    test_uint(bob, ecs_iter_get_var(&it, x_var));
    test_uint(ecs_pair(Likes, bob), ecs_field_id(&it, 0));
    test_uint(bob, ecs_field_src(&it, 1));
    test_bool(false, ecs_query_next(&it));
    test_int(s.started, s.finished);

    ecs_query_fini(q);

    ecs_fini(world);
}

void External_pair_var_tgt_not_alive(void) {
    ecs_world_t *world = ecs_mini();

    ECS_TAG(world, Likes);
    ecs_entity_t bob = ecs_entity(world, { .name = "bob" });
    ecs_entity_t gone = 100000;
    test_bool(false, ecs_is_alive(world, gone));

    ecs_entity_t e1 = ecs_new(world);
    ecs_entity_t e2 = ecs_new(world);

    ext_store_t s = {0};
    ext_add(&s, e1, ecs_pair(Likes, gone), 0, 0);
    ext_add(&s, e2, ecs_pair(Likes, bob), 0, 0);
    ext_set(world, Likes, &s);

    ecs_query_t *q = ecs_query(world, {
        .expr = "(Likes, $x)",
        .cache_kind = cache_kind
    });
    test_assert(q != NULL);

    int32_t x_var = ecs_query_find_var(q, "x");

    ecs_iter_t it = ecs_query_iter(world, q);
    test_bool(true, ecs_query_next(&it));
    test_uint(e2, it.entities[0]);
    test_uint(bob, ecs_iter_get_var(&it, x_var));
    test_bool(false, ecs_query_next(&it));

    ecs_query_fini(q);

    ecs_fini(world);
}

void External_not_alive_this(void) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);

    ecs_entity_t e1 = ecs_new(world);
    ecs_entity_t cold = 100000;
    test_bool(false, ecs_is_alive(world, cold));

    ext_store_t s = {0};
    ext_add(&s, cold, ecs_id(Position), 1, 2);
    ext_add(&s, e1, ecs_id(Position), 10, 20);
    ext_set(world, ecs_id(Position), &s);

    ecs_query_t *q = ecs_query(world, {
        .expr = "Position",
        .cache_kind = cache_kind
    });
    test_assert(q != NULL);

    ecs_iter_t it = ecs_query_iter(world, q);
    test_bool(true, ecs_query_next(&it));
    test_int(1, it.count);
    test_assert(it.table == NULL);
    test_uint(cold, it.entities[0]);
    {
        Position *p = ecs_field(&it, Position, 0);
        test_int(p->x, 1); test_int(p->y, 2);
    }

    test_bool(true, ecs_query_next(&it));
    test_int(1, it.count);
    test_assert(it.table != NULL);
    test_uint(e1, it.entities[0]);
    {
        Position *p = ecs_field(&it, Position, 0);
        test_int(p->x, 10); test_int(p->y, 20);
    }

    test_bool(false, ecs_query_next(&it));

    ecs_query_fini(q);

    ecs_fini(world);
}

void External_not_alive_skipped_for_table_term(void) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);
    ECS_COMPONENT(world, Velocity);

    ecs_entity_t e1 = ecs_new(world);
    ecs_set(world, e1, Velocity, {1, 2});

    ext_store_t s = {0};
    ext_add(&s, 100000, ecs_id(Position), 1, 2);
    ext_add(&s, e1, ecs_id(Position), 10, 20);
    ext_set(world, ecs_id(Position), &s);

    ecs_query_t *q = ecs_query(world, {
        .expr = "Position, Velocity",
        .cache_kind = cache_kind
    });
    test_assert(q != NULL);

    ecs_iter_t it = ecs_query_iter(world, q);
    test_bool(true, ecs_query_next(&it));
    test_int(1, it.count);
    test_uint(e1, it.entities[0]);
    test_bool(false, ecs_query_next(&it));

    ecs_query_fini(q);

    ecs_fini(world);
}

void External_not_alive_joined_with_external(void) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);
    ECS_TAG(world, Likes);
    ECS_TAG(world, Tag);
    ecs_entity_t alice = ecs_entity(world, { .name = "alice" });
    ecs_entity_t bob = ecs_entity(world, { .name = "bob" });
    ecs_add(world, bob, Tag);

    ecs_entity_t cold_1 = 100000, cold_2 = 100001;

    ext_store_t pos = {0};
    ext_add(&pos, cold_1, ecs_id(Position), 1, 2);
    ext_add(&pos, cold_2, ecs_id(Position), 3, 4);
    ext_set(world, ecs_id(Position), &pos);

    ext_store_t likes = {0};
    ext_add(&likes, cold_1, ecs_pair(Likes, alice), 0, 0);
    ext_add(&likes, cold_2, ecs_pair(Likes, bob), 0, 0);
    ext_set(world, Likes, &likes);

    ecs_query_t *q = ecs_query(world, {
        .expr = "Position, (Likes, $x), Tag($x)",
        .cache_kind = cache_kind
    });
    test_assert(q != NULL);

    int32_t x_var = ecs_query_find_var(q, "x");

    ecs_iter_t it = ecs_query_iter(world, q);
    test_bool(true, ecs_query_next(&it));
    test_int(1, it.count);
    test_assert(it.table == NULL);
    test_uint(cold_2, it.entities[0]);
    test_uint(bob, ecs_iter_get_var(&it, x_var));
    {
        Position *p = ecs_field(&it, Position, 0);
        test_int(p->x, 3); test_int(p->y, 4);
    }
    test_bool(false, ecs_query_next(&it));
    test_int(pos.started, pos.finished);
    test_int(likes.started, likes.finished);

    ecs_query_fini(q);

    ecs_fini(world);
}

void External_fini_on_early_stop(void) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);

    ecs_entity_t e1 = ecs_new(world);
    ecs_entity_t e2 = ecs_new(world);

    ext_store_t s = {0};
    ext_add(&s, e1, ecs_id(Position), 10, 20);
    ext_add(&s, e2, ecs_id(Position), 30, 40);
    ext_set(world, ecs_id(Position), &s);

    ecs_query_t *q = ecs_query(world, {
        .expr = "Position",
        .cache_kind = cache_kind
    });
    test_assert(q != NULL);

    ecs_iter_t it = ecs_query_iter(world, q);
    test_bool(true, ecs_query_next(&it));
    test_uint(e1, it.entities[0]);
    test_int(1, s.started);
    test_int(0, s.finished);
    ecs_iter_fini(&it);
    test_int(1, s.finished);

    ecs_query_fini(q);

    ecs_fini(world);
}

void External_page_iter(void) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);

    ecs_entity_t e1 = ecs_new(world);
    ecs_entity_t e2 = ecs_new(world);
    ecs_entity_t e3 = ecs_new(world);

    ext_store_t s = {0};
    ext_add(&s, e1, ecs_id(Position), 10, 20);
    ext_add(&s, e2, ecs_id(Position), 30, 40);
    ext_add(&s, e3, ecs_id(Position), 50, 60);
    ext_set(world, ecs_id(Position), &s);

    ecs_query_t *q = ecs_query(world, {
        .expr = "Position",
        .cache_kind = cache_kind
    });
    test_assert(q != NULL);

    ecs_iter_t qit = ecs_query_iter(world, q);
    ecs_iter_t it = ecs_page_iter(&qit, 1, 1);
    test_bool(true, ecs_page_next(&it));
    test_int(1, it.count);
    test_uint(e2, it.entities[0]);
    {
        Position *p = ecs_field(&it, Position, 0);
        test_assert(p != NULL);
        test_int(p->x, 30); test_int(p->y, 40);
    }
    test_bool(false, ecs_page_next(&it));
    test_int(s.started, s.finished);

    ecs_query_fini(q);

    ecs_fini(world);
}

void External_is_true(void) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);
    ECS_COMPONENT(world, Velocity);

    ecs_entity_t e1 = ecs_new(world);

    ext_store_t s = {0};
    ext_set(world, ecs_id(Position), &s);

    ecs_query_t *q = ecs_query(world, {
        .expr = "Position",
        .cache_kind = cache_kind
    });
    test_assert(q != NULL);
    test_bool(false, ecs_query_is_true(q));

    ext_add(&s, e1, ecs_id(Position), 10, 20);
    test_bool(true, ecs_query_is_true(q));
    test_int(s.started, s.finished);

    ecs_query_fini(q);

    ecs_fini(world);
}

void External_remove_source(void) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);

    ecs_entity_t e1 = ecs_new(world);

    ext_store_t s = {0};
    ext_add(&s, e1, ecs_id(Position), 10, 20);
    ext_set(world, ecs_id(Position), &s);

    ecs_query_t *q = ecs_query(world, {
        .expr = "Position",
        .cache_kind = cache_kind
    });
    test_assert(q != NULL);

    test_int(0, ecs_set_external_source(world, ecs_id(Position), NULL));
    test_assert(ecs_get_external_source(world, ecs_id(Position)) == NULL);

    ecs_iter_t it = ecs_query_iter(world, q);
    test_bool(false, ecs_query_next(&it));
    test_int(0, s.started);

    ecs_query_fini(q);

    ecs_fini(world);
}

static int ctx_free_count = 0;

static void ext_ctx_free(void *ctx) {
    test_assert(ctx != NULL);
    ctx_free_count ++;
}

void External_get_source_and_ctx_free(void) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);
    ECS_COMPONENT(world, Velocity);

    ext_store_t s = {0};
    ctx_free_count = 0;

    test_int(0, ecs_set_external_source(world, ecs_id(Position),
        &(ecs_external_source_desc_t){
            .next = ext_next,
            .ctx = &s,
            .ctx_free = ext_ctx_free
        }));

    const ecs_external_source_desc_t *src =
        ecs_get_external_source(world, ecs_id(Position));
    test_assert(src != NULL);
    test_assert(src->next == ext_next);
    test_assert(src->ctx == &s);
    test_assert(ecs_get_external_source(world, ecs_id(Velocity)) == NULL);

    /* Replacing a source frees the old context */
    test_int(0, ecs_set_external_source(world, ecs_id(Position),
        &(ecs_external_source_desc_t){
            .next = ext_next,
            .ctx = &s,
            .ctx_free = ext_ctx_free
        }));
    test_int(1, ctx_free_count);

    ecs_fini(world);
    test_int(2, ctx_free_count);
}

void External_set_in_use(void) {
    ecs_log_set_level(-4);

    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);
    ECS_TAG(world, Likes);

    ecs_entity_t e1 = ecs_new(world);
    ecs_set(world, e1, Position, {10, 20});
    ecs_add_pair(world, e1, Likes, e1);

    ext_store_t s = {0};
    test_assert(0 != ecs_set_external_source(world, ecs_id(Position),
        &(ecs_external_source_desc_t){ .next = ext_next, .ctx = &s }));
    test_assert(0 != ecs_set_external_source(world, Likes,
        &(ecs_external_source_desc_t){ .next = ext_next, .ctx = &s }));
    test_assert(ecs_get_external_source(world, ecs_id(Position)) == NULL);
    test_assert(ecs_get_external_source(world, Likes) == NULL);

    ecs_fini(world);
}

void External_invalid_up(void) {
    ecs_log_set_level(-4);

    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);

    ext_store_t s = {0};
    ext_set(world, ecs_id(Position), &s);

    ecs_query_t *q = ecs_query(world, {
        .expr = "Position(up ChildOf)",
        .cache_kind = cache_kind
    });
    test_assert(q == NULL);

    ecs_fini(world);
}

void External_invalid_or(void) {
    ecs_log_set_level(-4);

    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);
    ECS_COMPONENT(world, Velocity);

    ext_store_t s = {0};
    ext_set(world, ecs_id(Position), &s);

    ecs_query_t *q = ecs_query(world, {
        .expr = "Position || Velocity",
        .cache_kind = cache_kind
    });
    test_assert(q == NULL);

    ecs_fini(world);
}

void External_invalid_any_tgt(void) {
    ecs_log_set_level(-4);

    ecs_world_t *world = ecs_mini();

    ECS_TAG(world, Likes);

    ext_store_t s = {0};
    ext_set(world, Likes, &s);

    ecs_query_t *q = ecs_query(world, {
        .expr = "(Likes, _)",
        .cache_kind = cache_kind
    });
    test_assert(q == NULL);

    ecs_fini(world);
}

static void Dummy(ecs_iter_t *it) {
    (void)it;
}

void External_invalid_observer(void) {
    ecs_log_set_level(-4);

    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);
    ECS_COMPONENT(world, Velocity);

    ext_store_t s = {0};
    ext_set(world, ecs_id(Position), &s);

    ecs_entity_t o = ecs_observer(world, {
        .query.expr = "Velocity, Position",
        .events = { EcsOnSet },
        .callback = Dummy
    });
    test_assert(o == 0);

    ecs_fini(world);
}

void External_plan(void) {
    ecs_world_t *world = ecs_mini();

    ECS_COMPONENT(world, Position);
    ECS_COMPONENT(world, Velocity);

    ext_store_t s = {0};
    ext_set(world, ecs_id(Position), &s);

    ecs_query_t *q = ecs_query(world, {
        .expr = "Velocity, Position, !Position($x)",
        .cache_kind = EcsQueryCacheNone
    });
    test_assert(q != NULL);

    char *plan = ecs_query_plan(q);
    test_assert(plan != NULL);
    test_assert(strstr(plan, "external") != NULL);
    ecs_os_free(plan);

    ecs_query_fini(q);

    ecs_fini(world);
}
