/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Project-owned blocks reuse the native composite elaborator, never a second solver. */
#include "project/hierarchy.h"
#include "project/loader.h"
#include "project/version_rules.h"
#include "util/str.h"
#include <string.h>

const esk_catalog_item *esk_project_component(const es_session *s, const char *id)
{
    size_t i;
    if (!id)
        return NULL;
    if (esk_str_has_prefix(id, "block/")) {
        for (i = 0; i < s->local_blocks.len; i++)
            if (!strcmp(s->local_blocks.data[i]->id, id))
                return s->local_blocks.data[i];
        return NULL;
    }
    return esk_catalog_find(s->catalog, "component", id);
}

void esk_project_blocks_free(es_session *s)
{
    size_t i;
    for (i = 0; i < s->local_blocks.len; i++) {
        esk_catalog_item *it = s->local_blocks.data[i];
        esk_strfree(s->a, it->id);
        esk_json_free(s->a, it->desc);
        esk_free(s->a, it, sizeof *it);
    }
    esk_vec_free(s->a, &s->local_blocks);
}

static es_status invalid(es_session *s, const char *subject, const char *message)
{
    esk_diag_raise(&s->diag, ES_SEV_ERROR, "hierarchy.invalid", 0, "project", subject, NULL, 0,
                   "%s", message);
    return ES_ESCHEMA;
}

static int valid_ref(es_session *s, const esk_json *components, const char *ref)
{
    char id[160], pin[64];
    size_t i, j;
    if (!ref || esk_split_pin_ref(ref, id, sizeof id, pin, sizeof pin))
        return 0;
    for (i = 0; i < esk_json_len(components); i++) {
        const esk_json *c = esk_json_at(components, i);
        const esk_catalog_item *it;
        if (!esk_str_eq(id, esk_json_get_str(c, "id", "")))
            continue;
        it = esk_project_component(s, esk_json_get_str(c, "component", ""));
        if (!it)
            return 0;
        for (j = 0; j < esk_json_len(esk_json_get(it->desc, "pins")); j++) {
            const esk_json *p = esk_json_at(esk_json_get(it->desc, "pins"), j);
            char number[32];
            esk_snprintf(number, sizeof number, "%lld",
                         (long long)esk_json_get_int(p, "number", -1));
            if (esk_str_eq(pin, esk_json_get_str(p, "name", "")) ||
                esk_str_eq(pin, esk_json_get_str(p, "number", "")) ||
                (esk_json_get(p, "number") && !strcmp(pin, number)))
                return 1;
        }
    }
    return 0;
}

/* Validate even unused definitions; bounded expansion also rejects path truncation. */
static es_status validate_block(es_session *s, const esk_json *block, const char **stack,
                                size_t depth, size_t path_length, size_t *budget)
{
    const esk_json *components = esk_json_get(block, "components");
    const esk_json *nets = esk_json_get(block, "nets");
    const esk_json *ports = esk_json_get(block, "ports");
    size_t i, j, k;
    if (depth > 8 || !*budget)
        return invalid(s, "blocks", "Hierarchy exceeds eight levels or 10000 expansion nodes");
    (*budget)--;
    if (!components || components->type != ESK_JSON_ARRAY || !nets ||
        nets->type != ESK_JSON_OBJECT || !ports || ports->type != ESK_JSON_OBJECT)
        return invalid(s, "blocks", "Each block needs components, nets and explicit ports");
    for (i = 0; i < esk_json_len(components); i++) {
        const esk_json *c = esk_json_at(components, i);
        const char *id = esk_json_get_str(c, "id", NULL),
                   *ref = esk_json_get_str(c, "component", "");
        if (!*budget)
            return invalid(s, "blocks", "Hierarchy exceeds 10000 expanded components");
        (*budget)--;
        if (!esk_is_identifier(id) || path_length + strlen(id) + 1 >= 150)
            return invalid(s, "blocks",
                           "Invalid component id or hierarchy path longer than 149 characters");
        for (j = 0; j < i; j++)
            if (esk_str_eq(id, esk_json_get_str(esk_json_at(components, j), "id", "")))
                return invalid(s, id, "Duplicate instance id in one sheet");
        if (!esk_project_component(s, ref))
            return invalid(s, id, "Unknown block or catalog component");
        if (esk_project_require_version(s, esk_project_component(s, ref), c, id) != ES_OK) return ES_ESCHEMA;
        if (esk_str_has_prefix(ref, "block/")) {
            const esk_json *child = esk_json_get(esk_json_get(s->project, "blocks"), ref + 6);
            es_status st;
            for (j = 0; j < depth; j++)
                if (!strcmp(stack[j], ref))
                    return invalid(s, ref, "Recursive circuit block reference");
            stack[depth] = ref;
            st = validate_block(s, child, stack, depth + 1, path_length + strlen(id) + 1, budget);
            if (st != ES_OK)
                return st;
        }
    }
    for (i = 0; i < esk_json_len(ports); i++) {
        const esk_json *port = esk_json_at(ports, i);
        if (!port->key || !*port->key || strlen(port->key) > 32 ||
            strspn(port->key,
                   "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_+-/~#") !=
                strlen(port->key) ||
            !valid_ref(s, components, esk_json_str(port)))
            return invalid(s, port->key, "Port must name an existing local component pin");
        {
            int found = 0;
            for (j = 0; j < esk_json_len(nets); j++)
                for (k = 0; k < esk_json_len(esk_json_at(nets, j)); k++)
                    if (esk_str_eq(esk_json_str(port),
                                   esk_json_str(esk_json_at(esk_json_at(nets, j), k))))
                        found = 1;
            if (!found)
                return invalid(s, port->key, "Port pin must belong to a named local net");
        }
    }
    for (i = 0; i < esk_json_len(nets); i++) {
        const esk_json *net = esk_json_at(nets, i);
        if (!net->key || !*net->key || strlen(net->key) > 63 || strchr(net->key, '.') ||
            net->type != ESK_JSON_ARRAY)
            return invalid(s, "nets", "Block nets must be named pin arrays");
        for (j = 0; j < esk_json_len(net); j++) {
            const char *pin = esk_json_str(esk_json_at(net, j));
            if (!valid_ref(s, components, pin))
                return invalid(s, net->key, "Net references an unknown local pin");
            for (k = 0; k <= i; k++) {
                const esk_json *other = esk_json_at(nets, k);
                size_t n, end = k == i ? j : esk_json_len(other);
                for (n = 0; n < end; n++)
                    if (esk_str_eq(pin, esk_json_str(esk_json_at(other, n))))
                        return invalid(s, net->key, "A local pin is listed more than once");
            }
        }
    }
    {
        char why[256];
        if (esk_project_validate_organization(block, why, sizeof why))
            return invalid(s, "blocks", why);
    }
    return ES_OK;
}

es_status esk_project_blocks_prepare(es_session *s)
{
    const esk_json *blocks = esk_json_get(s->project, "blocks");
    size_t i;
    if (!blocks)
        return ES_OK;
    if (blocks->type != ESK_JSON_OBJECT || esk_json_len(blocks) > 256)
        return invalid(s, "blocks", "Expected at most 256 project block definitions");
    for (i = 0; i < esk_json_len(blocks); i++) {
        const esk_json *block = esk_json_at(blocks, i), *ports = esk_json_get(block, "ports");
        esk_catalog_item *it;
        esk_json *pins, *model, *composite;
        size_t j;
        char id[80];
        const esk_json *components = esk_json_get(block, "components"),
                       *nets = esk_json_get(block, "nets");
        const esk_json *params = esk_json_get(block, "params");
        const char *name = esk_json_get_str(block, "name", NULL);
        if (block->type != ESK_JSON_OBJECT || !name || !*name || !components ||
            components->type != ESK_JSON_ARRAY || !nets || nets->type != ESK_JSON_OBJECT ||
            !ports || ports->type != ESK_JSON_OBJECT || (params && params->type != ESK_JSON_OBJECT))
            return invalid(s, "blocks",
                           "Malformed block definition: expected name, component array, nets, "
                           "ports and optional parameter objects");
        for (j = 0; j < esk_json_len(params); j++)
            if (esk_json_at(params, j)->type != ESK_JSON_OBJECT)
                return invalid(s, "blocks", "Parameter declarations must be objects");
        if (!esk_is_identifier(block->key))
            return invalid(s, "blocks", "Invalid block identifier");
        for (j = 0; block->key[j]; j++)
            if (block->key[j] >= 'A' && block->key[j] <= 'Z')
                return invalid(s, "blocks", "Block identifiers must be lowercase");
        it = (esk_catalog_item *)esk_calloc(s->a, 1, sizeof *it);
        if (!it)
            return ES_ENOMEM;
        esk_snprintf(id, sizeof id, "block/%s", block->key);
        it->id = esk_strdup(s->a, id);
        it->desc = esk_json_new_object(s->a);
        if (esk_vec_push(s->a, &s->local_blocks, it)) {
            esk_strfree(s->a, it->id);
            esk_json_free(s->a, it->desc);
            esk_free(s->a, it, sizeof *it);
            return ES_ENOMEM;
        }
        if (!it->id || !it->desc)
            return ES_ENOMEM;
        esk_json_set_string(s->a, it->desc, "id", id);
        esk_json_set_string(s->a, it->desc, "kind", "component");
        esk_json_set_string(s->a, it->desc, "name", esk_json_get_str(block, "name", block->key));
        pins = esk_json_new_array(s->a);
        for (j = 0; pins && j < esk_json_len(ports); j++) {
            esk_json *pin = esk_json_new_object(s->a), *pm = esk_json_new_object(s->a);
            if (!pin || !pm) {
                esk_json_free(s->a, pin);
                esk_json_free(s->a, pm);
                esk_json_free(s->a, pins);
                return ES_ENOMEM;
            }
            esk_json_set_string(s->a, pin, "name", esk_json_at(ports, j)->key);
            esk_json_set_string(s->a, pm, "kind", "passive");
            esk_json_object_set(s->a, pin, "model", pm);
            esk_json_array_push(s->a, pins, pin);
        }
        if (!pins)
            return ES_ENOMEM;
        esk_json_object_set(s->a, it->desc, "pins", pins);
        if (esk_json_get(block, "params"))
            esk_json_object_set(s->a, it->desc, "params",
                                esk_json_clone(s->a, esk_json_get(block, "params")));
        model = esk_json_new_object(s->a);
        composite = esk_json_clone(s->a, block);
        if (!model || !composite) {
            esk_json_free(s->a, model);
            esk_json_free(s->a, composite);
            return ES_ENOMEM;
        }
        esk_json_object_set(s->a, model, "composite", composite);
        esk_json_object_set(s->a, it->desc, "model", model);
    }
    for (i = 0; i < esk_json_len(blocks); i++) {
        const char *stack[10];
        size_t budget = 10000;
        es_status st;
        stack[0] = s->local_blocks.data[i]->id;
        st = validate_block(s, esk_json_at(blocks, i), stack, 1, 0, &budget);
        if (st != ES_OK)
            return st;
    }
    {
        const esk_json *parts = esk_json_get(s->project, "components");
        size_t budget = 10000;
        for (i = 0; i < esk_json_len(parts); i++) {
            const esk_json *part = esk_json_at(parts, i);
            const char *ref = esk_json_get_str(part, "component", ""), *stack[10];
            es_status st;
            if (!esk_str_has_prefix(ref, "block/"))
                continue;
            {
                const esk_json *root_nets = esk_json_get(s->project, "nets");
                const char *id = esk_json_get_str(part, "id", "");
                size_t k, len = strlen(id);
                for (k = 0; k < esk_json_len(root_nets); k++) {
                    const char *net = esk_json_at(root_nets, k)->key;
                    if (!strncmp(net, id, len) && net[len] == '.')
                        return invalid(s, net, "Root net overlaps a block instance namespace");
                }
            }
            stack[0] = ref;
            st = validate_block(s, esk_json_get(blocks, ref + 6), stack, 1,
                                strlen(esk_json_get_str(part, "id", "")) + 1, &budget);
            if (st != ES_OK)
                return st;
        }
    }
    return ES_OK;
}

static void locations(es_session *s, const esk_json *sheet, const char *path, const char *block,
                      esk_json *out, const esk_net *trace, size_t depth, size_t *budget)
{
    const esk_json *nets = esk_json_get(sheet, "nets"), *parts = esk_json_get(sheet, "components");
    esk_json *node, *aliases;
    size_t i;
    char full[256];
    if (depth > 8 || !*budget)
        return;
    (*budget)--;
    node = esk_json_new_object(s->a);
    aliases = esk_json_new_object(s->a);
    if (!node || !aliases) {
        esk_json_free(s->a, node);
        esk_json_free(s->a, aliases);
        return;
    }
    esk_json_set_string(s->a, node, "path", path);
    esk_json_set_string(s->a, node, "block", block);
    if (*path) {
        const esk_comp *instance = esk_engine_comp(&s->eng, path);
        if (instance && instance->params)
            esk_json_object_set(s->a, node, "params", esk_json_clone(s->a, instance->params));
    }
    for (i = 0; i < esk_json_len(nets); i++) {
        const char *name = esk_json_at(nets, i)->key;
        esk_net *net;
        esk_snprintf(full, sizeof full, "%s%s%s", path, *path ? "." : "", name);
        net = esk_engine_net(&s->eng, full);
        if (net && (!trace || net == trace))
            esk_json_set_string(s->a, aliases, name, net->name);
    }
    esk_json_object_set(s->a, node, "nets", aliases);
    if (!trace || esk_json_len(aliases))
        esk_json_array_push(s->a, out, node);
    else
        esk_json_free(s->a, node);
    for (i = 0; i < esk_json_len(parts); i++) {
        const esk_json *part = esk_json_at(parts, i);
        const char *ref = esk_json_get_str(part, "component", "");
        if (!esk_str_has_prefix(ref, "block/"))
            continue;
        esk_snprintf(full, sizeof full, "%s%s%s", path, *path ? "." : "",
                     esk_json_get_str(part, "id", ""));
        locations(s, esk_json_get(esk_json_get(s->project, "blocks"), ref + 6), full, ref + 6, out,
                  trace, depth + 1, budget);
    }
}

esk_json *esk_project_hierarchy(es_session *s, const char *trace)
{
    esk_json *out = esk_json_new_array(s->a);
    esk_net *net = trace ? esk_engine_net(&s->eng, trace) : NULL;
    size_t budget = 10000;
    if (out && (!trace || net))
        locations(s, s->project, "", "", out, net, 0, &budget);
    return out;
}
