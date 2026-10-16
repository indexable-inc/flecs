/**
 * @file query/external.c
 * @brief Sources for ids whose matches come from outside the world.
 */

#include "../private_api.h"

static ecs_external_source_desc_t* flecs_external_source_get_mut(
    const ecs_world_t *world,
    ecs_entity_t id)
{
    if (!ecs_map_count(&world->external_sources)) {
        return NULL;
    }

    return ecs_map_get_deref(
        &world->external_sources, ecs_external_source_desc_t, id);
}

const ecs_external_source_desc_t* flecs_external_source_get(
    const ecs_world_t *world,
    ecs_id_t id)
{
    world = ecs_get_world(world); /* Can be called with a stage */
    if (!ecs_map_count(&world->external_sources)) {
        return NULL;
    }

    ecs_entity_t first;
    if (ECS_IS_PAIR(id)) {
        first = ECS_PAIR_FIRST(id);
    } else if (id & ECS_ID_FLAGS_MASK) {
        return NULL;
    } else {
        first = id;
    }

    if (!first || first == EcsWildcard || first == EcsAny ||
        first == EcsThis || first == EcsVariable)
    {
        return NULL;
    }

    return flecs_external_source_get_mut(world, (uint32_t)first);
}

static void flecs_external_source_free(
    ecs_world_t *world,
    ecs_external_source_desc_t *src)
{
    if (src->ctx_free) {
        src->ctx_free(src->ctx);
    }

    flecs_free_t(&world->allocator, ecs_external_source_desc_t, src);
}

int ecs_set_external_source(
    ecs_world_t *world,
    ecs_entity_t id,
    const ecs_external_source_desc_t *desc)
{
    flecs_poly_assert(world, ecs_world_t);
    ecs_check(id != 0, ECS_INVALID_PARAMETER, NULL);
    ecs_check(!ECS_IS_PAIR(id) && !(id & ECS_ID_FLAGS_MASK),
        ECS_INVALID_PARAMETER,
        "external source must be set on a component or relationship, "
        "not on a pair");
    ecs_check(ecs_is_alive(world, id), ECS_INVALID_PARAMETER, NULL);
    ecs_check(!desc || desc->next != NULL, ECS_INVALID_PARAMETER,
        "external source requires a next callback");

    /* Matches for the id come from the source only. Tables that already
     * have the id would silently stop matching, so refuse. */
    if (desc && (ecs_id_in_use(world, id) ||
        ecs_id_in_use(world, ecs_pair(id, EcsWildcard))))
    {
        char *str = ecs_id_str(world, id);
        ecs_err("cannot set external source for '%s': id is in use by tables",
            str);
        ecs_os_free(str);
        return -1;
    }

    uint32_t key = (uint32_t)id;
    ecs_external_source_desc_t *cur = flecs_external_source_get_mut(world, key);
    if (cur) {
        ecs_map_remove(&world->external_sources, key);
        flecs_external_source_free(world, cur);
    }

    if (desc) {
        ecs_external_source_desc_t *src = flecs_alloc_t(
            &world->allocator, ecs_external_source_desc_t);
        *src = *desc;
        ecs_map_insert_ptr(&world->external_sources, key, src);
    }

    return 0;
error:
    return -1;
}

const ecs_external_source_desc_t* ecs_get_external_source(
    const ecs_world_t *world,
    ecs_entity_t id)
{
    ecs_check(world != NULL, ECS_INVALID_PARAMETER, NULL);
    world = ecs_get_world(world);
    if (!id || ECS_IS_PAIR(id) || (id & ECS_ID_FLAGS_MASK)) {
        return NULL;
    }

    return flecs_external_source_get_mut(world, (uint32_t)id);
error:
    return NULL;
}

void flecs_external_sources_fini(
    ecs_world_t *world)
{
    ecs_map_iter_t it = ecs_map_iter(&world->external_sources);
    while (ecs_map_next(&it)) {
        ecs_external_source_desc_t *src = ecs_map_ptr(&it);
        flecs_external_source_free(world, src);
    }

    ecs_map_fini(&world->external_sources);
}
