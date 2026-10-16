/**
 * @file query/engine/eval_external.c
 * @brief Evaluation of terms for ids with an external source.
 *
 * An external source reports which entities have an id, and the value for
 * each match. Terms for such ids don't look at tables. When the source of the
 * term is not yet known, the source enumerates entities that have the id
 * (select). When it is known, each entity is tested against the source (with).
 * Results produced by an external term always have a single entity, so that
 * the field can point at the value returned by the source.
 */

#include "../../private_api.h"

#ifdef FLECS_QUERY_PLANS

static void flecs_query_external_end(
    ecs_query_external_ctx_t *op_ctx)
{
    if (op_ctx->active) {
        op_ctx->active = false;
        if (op_ctx->source->fini) {
            op_ctx->source->fini(&op_ctx->it);
        }
    }
}

void flecs_query_external_fini(
    ecs_query_external_ctx_t *op_ctx)
{
    flecs_query_external_end(op_ctx);
}

static void flecs_query_external_begin(
    const ecs_query_run_ctx_t *ctx,
    ecs_query_external_ctx_t *op_ctx,
    ecs_entity_t src,
    ecs_id_t id)
{
    flecs_query_external_end(op_ctx);
    ecs_os_zeromem(&op_ctx->it);
    op_ctx->it.world = ctx->world;
    op_ctx->it.src = src;
    op_ctx->it.id = id;
    op_ctx->it.ctx = op_ctx->source->ctx;
    op_ctx->active = true;

    /* Make sure fini is called if the iterator is finished early */
    ctx->it->flags |= EcsIterOpCtxFini;
}

/* Get the next match from the source that the query can use. */
static bool flecs_query_external_next(
    const ecs_query_op_t *op,
    const ecs_query_run_ctx_t *ctx,
    ecs_query_external_ctx_t *op_ctx)
{
    ecs_external_iter_t *it = &op_ctx->it;
    ecs_flags16_t flags_2nd = flecs_query_ref_flags(op->flags, EcsQuerySecond);
    bool tgt_var = (flags_2nd & EcsQueryIsVar) &&
        (op->written & (1ull << op->second.var));

    while (op_ctx->source->next(it)) {
        ecs_assert(it->match_src != 0, ECS_INVALID_OPERATION,
            "external source returned match without entity");
        ecs_assert(!it->src || it->match_src == it->src,
            ECS_INVALID_OPERATION,
            "external source returned match for a different entity");
        ecs_assert(it->match_id != 0 && !ecs_id_is_wildcard(it->match_id),
            ECS_INVALID_OPERATION,
            "external source returned wildcard or empty id");
        ecs_assert(ecs_id_match(it->match_id, it->id), ECS_INVALID_OPERATION,
            "external source returned id that doesn't match requested id");

        /* A target that is assigned to a variable must be alive, as other
         * terms may use the variable. */
        if (tgt_var && !ecs_get_alive(ctx->world,
            ECS_PAIR_SECOND(it->match_id)))
        {
            continue;
        }

        return true;
    }

    flecs_query_external_end(op_ctx);
    return false;
}

static void flecs_query_external_set_field(
    const ecs_query_op_t *op,
    const ecs_query_run_ctx_t *ctx,
    ecs_id_t id,
    const void *ptr)
{
    int8_t field_index = op->field_index;
    if (field_index == -1) {
        return;
    }

    ecs_iter_t *it = ctx->it;
    it->ids[field_index] = id;
    flecs_query_it_set_tr(it, field_index, NULL);
    ctx->qit->external_ptrs[field_index] = ptr;
}

/* Assign a matched entity to the (unwritten) source variable. */
static bool flecs_query_external_set_src(
    const ecs_query_op_t *op,
    const ecs_query_run_ctx_t *ctx,
    ecs_entity_t e)
{
    ecs_var_id_t var_id = op->src.var;
    ecs_var_t *var = &ctx->vars[var_id];
    bool is_table = ctx->query_vars[var_id].kind == EcsVarTable;

    if (!ecs_is_alive(ctx->world, e)) {
        /* Entities that aren't alive can only be returned if no other term
         * needs to find the entity in a table. */
        if (!(ctx->query->external_unalive_vars & (1ull << var_id))) {
            return false;
        }

        var->range = (ecs_table_range_t){ .table = NULL, .count = 1 };
        var->entity = e;
        return true;
    }

    ecs_record_t *r = flecs_entities_get(ctx->world, e);
    ecs_table_t *table = r->table;
    ecs_assert(table != NULL, ECS_INTERNAL_ERROR, NULL);

    if (flecs_query_table_filter(table, op->other,
        (EcsTableNotQueryable|EcsTableIsPrefab|EcsTableIsDisabled)))
    {
        return false;
    }

    if (is_table) {
        flecs_query_var_set_range(op, var_id, table,
            ECS_RECORD_TO_ROW(r->row), 1, ctx);
    } else {
        flecs_query_var_set_entity(op, var_id, e, ctx);
    }

    return true;
}

/* Source is not known: ask external source for all entities with the id. */
static bool flecs_query_external_select(
    const ecs_query_op_t *op,
    bool redo,
    const ecs_query_run_ctx_t *ctx)
{
    ecs_query_external_ctx_t *op_ctx = flecs_op_ctx(ctx, external);

    if (!redo) {
        ecs_id_t id = flecs_query_op_get_id(op, ctx);
        op_ctx->source = flecs_external_source_get(ctx->world, id);
        if (!op_ctx->source) {
            return false;
        }

        flecs_query_external_begin(ctx, op_ctx, 0, id);
    } else if (!op_ctx->active) {
        return false;
    }

    while (flecs_query_external_next(op, ctx, op_ctx)) {
        ecs_external_iter_t *eit = &op_ctx->it;
        if (!flecs_query_external_set_src(op, ctx, eit->match_src)) {
            continue;
        }

        flecs_query_external_set_field(op, ctx, eit->match_id, eit->ptr);
        flecs_query_set_vars(op, eit->match_id, ctx);
        return true;
    }

    return false;
}

/* Get the entity at the current row of the evaluated range. */
static ecs_entity_t flecs_query_external_cur_entity(
    const ecs_query_op_t *op,
    const ecs_query_run_ctx_t *ctx,
    ecs_query_external_ctx_t *op_ctx)
{
    ecs_table_t *table = op_ctx->range.table;
    if (table) {
        return ecs_table_entities(table)[op_ctx->cur];
    }

    if (op->flags & (EcsQueryIsEntity << EcsQuerySrc)) {
        return op->src.entity;
    }

    /* Source variable holds an entity without a table */
    return ctx->vars[op->src.var].entity;
}

/* Restrict the source variable to the current row. */
static void flecs_query_external_narrow(
    const ecs_query_op_t *op,
    const ecs_query_run_ctx_t *ctx,
    ecs_query_external_ctx_t *op_ctx)
{
    if (op_ctx->range.table && (op->flags & (EcsQueryIsVar << EcsQuerySrc))) {
        flecs_query_src_set_single(op, op_ctx->cur, ctx);
    }
}

/* Restore the source variable to the range it had before the operation. */
static void flecs_query_external_restore(
    const ecs_query_op_t *op,
    const ecs_query_run_ctx_t *ctx,
    ecs_query_external_ctx_t *op_ctx)
{
    if (op_ctx->range.table && (op->flags & (EcsQueryIsVar << EcsQuerySrc))) {
        flecs_query_src_set_range(op, &op_ctx->range, ctx);
    }
}

static bool flecs_query_external_init_range(
    const ecs_query_op_t *op,
    const ecs_query_run_ctx_t *ctx,
    ecs_query_external_ctx_t *op_ctx)
{
    ecs_id_t id = flecs_query_op_get_id(op, ctx);
    op_ctx->source = flecs_external_source_get(ctx->world, id);
    op_ctx->it.id = id;

    if (op->flags & (EcsQueryIsEntity << EcsQuerySrc)) {
        op_ctx->range = (ecs_table_range_t){ .table = NULL, .count = 1 };
    } else {
        ecs_var_t *var = &ctx->vars[op->src.var];
        if (var->range.table) {
            op_ctx->range = var->range;
            if (!op_ctx->range.count) {
                op_ctx->range.count = ecs_table_count(op_ctx->range.table);
            }
        } else {
            ecs_entity_t e = flecs_query_var_get_entity(op->src.var, ctx);
            if (ecs_is_alive(ctx->world, e)) {
                op_ctx->range = flecs_range_from_entity(ctx->world, e);
                if (ctx->query_vars[op->src.var].kind != EcsVarTable) {
                    /* Entity variables aren't narrowed to a row */
                    op_ctx->range.table = NULL;
                }
            } else {
                op_ctx->range = (ecs_table_range_t){ .table = NULL, .count = 1 };
            }
        }
    }

    op_ctx->cur = op_ctx->range.offset;
    op_ctx->per_entity = false;
    return op_ctx->range.count != 0;
}

/* Source is known: test entities in the source range against the source. */
bool flecs_query_external_with(
    const ecs_query_op_t *op,
    bool redo,
    const ecs_query_run_ctx_t *ctx)
{
    ecs_query_external_ctx_t *op_ctx = flecs_op_ctx(ctx, external);
    int32_t end;

    if (!redo) {
        if (!flecs_query_external_init_range(op, ctx, op_ctx)) {
            return false;
        }

        if (!op_ctx->source) {
            return false;
        }
    } else if (!op_ctx->per_entity) {
        /* Previous call exhausted the range */
        return false;
    }

    end = op_ctx->range.offset + op_ctx->range.count;

    while (op_ctx->cur < end) {
        if (!op_ctx->per_entity) {
            ecs_entity_t e = flecs_query_external_cur_entity(op, ctx, op_ctx);
            flecs_query_external_begin(ctx, op_ctx, e, op_ctx->it.id);
            op_ctx->per_entity = true;
        }

        if (flecs_query_external_next(op, ctx, op_ctx)) {
            ecs_external_iter_t *eit = &op_ctx->it;
            flecs_query_external_narrow(op, ctx, op_ctx);
            flecs_query_external_set_field(op, ctx, eit->match_id, eit->ptr);
            flecs_query_set_vars(op, eit->match_id, ctx);
            return true;
        }

        op_ctx->per_entity = false;
        op_ctx->cur ++;
    }

    flecs_query_external_restore(op, ctx, op_ctx);
    return false;
}

/* Source is known: return entities in the source range without the id. */
bool flecs_query_external_not(
    const ecs_query_op_t *op,
    bool redo,
    const ecs_query_run_ctx_t *ctx)
{
    ecs_query_external_ctx_t *op_ctx = flecs_op_ctx(ctx, external);
    int32_t end;

    if (!redo) {
        if (!flecs_query_external_init_range(op, ctx, op_ctx)) {
            return false;
        }

        op_ctx->cur --;
    } else if (!op_ctx->per_entity) {
        return false;
    }

    op_ctx->per_entity = true;
    end = op_ctx->range.offset + op_ctx->range.count;

    while (++ op_ctx->cur < end) {
        if (op_ctx->source) {
            ecs_entity_t e = flecs_query_external_cur_entity(op, ctx, op_ctx);
            flecs_query_external_begin(ctx, op_ctx, e, op_ctx->it.id);
            bool has = flecs_query_external_next(op, ctx, op_ctx);
            flecs_query_external_end(op_ctx);
            if (has) {
                continue;
            }
        }

        flecs_query_external_narrow(op, ctx, op_ctx);
        flecs_query_external_set_field(op, ctx, op_ctx->it.id, NULL);
        return true;
    }

    op_ctx->per_entity = false;
    flecs_query_external_restore(op, ctx, op_ctx);
    return false;
}

/* Source is known: return entities in the source range, with the field set for
 * each match and not set for entities without a match. */
bool flecs_query_external_optional(
    const ecs_query_op_t *op,
    bool redo,
    const ecs_query_run_ctx_t *ctx)
{
    ecs_query_external_ctx_t *op_ctx = flecs_op_ctx(ctx, external);
    ecs_iter_t *it = ctx->it;
    ecs_termset_t field_bit;
    int32_t end;

    ecs_assert(op->field_index != -1, ECS_INTERNAL_ERROR, NULL);
    field_bit = (ecs_termset_t)(1u << op->field_index);

    if (!redo) {
        if (!flecs_query_external_init_range(op, ctx, op_ctx)) {
            return false;
        }

        op_ctx->in_range = true;
    } else if (!op_ctx->in_range) {
        return false;
    }

    end = op_ctx->range.offset + op_ctx->range.count;

    while (op_ctx->cur < end) {
        if (!op_ctx->per_entity) {
            op_ctx->per_entity = true;
            op_ctx->matched = false;
            if (op_ctx->source) {
                ecs_entity_t e = flecs_query_external_cur_entity(
                    op, ctx, op_ctx);
                flecs_query_external_begin(ctx, op_ctx, e, op_ctx->it.id);
            }
        }

        if (op_ctx->source && op_ctx->active &&
            flecs_query_external_next(op, ctx, op_ctx))
        {
            ecs_external_iter_t *eit = &op_ctx->it;
            op_ctx->matched = true;
            flecs_query_external_narrow(op, ctx, op_ctx);
            flecs_query_external_set_field(op, ctx, eit->match_id, eit->ptr);
            flecs_query_set_vars(op, eit->match_id, ctx);
            ECS_TERMSET_SET(it->set_fields, field_bit);
            return true;
        }

        op_ctx->per_entity = false;

        if (!op_ctx->matched) {
            /* Return entity once without the field */
            flecs_query_external_narrow(op, ctx, op_ctx);
            flecs_query_external_set_field(op, ctx, op_ctx->it.id, NULL);
            ECS_TERMSET_CLEAR(it->set_fields, field_bit);

            /* A target variable written by this term has no value */
            if ((op->flags & (EcsQueryIsVar << EcsQuerySecond)) &&
                (op->written & (1ull << op->second.var)))
            {
                flecs_query_var_reset(op->second.var, ctx);
            }

            op_ctx->cur ++;
            return true;
        }

        op_ctx->cur ++;
    }

    op_ctx->in_range = false;
    ECS_TERMSET_SET(it->set_fields, field_bit);
    flecs_query_external_restore(op, ctx, op_ctx);
    return false;
}

bool flecs_query_external(
    const ecs_query_op_t *op,
    bool redo,
    const ecs_query_run_ctx_t *ctx)
{
    uint64_t written = ctx->written[ctx->op_index];
    if (written & (1ull << op->src.var)) {
        return flecs_query_external_with(op, redo, ctx);
    } else {
        return flecs_query_external_select(op, redo, ctx);
    }
}

#endif // FLECS_QUERY_PLANS
