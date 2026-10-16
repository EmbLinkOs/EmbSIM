/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* lua_model.c -- scripted component models (SPEC-013).
 *
 * The host: one lua_State per engine, its memory through the engine's
 * allocator with a ceiling, the libraries a model may use (base without
 * load/dofile/require, string, table, math, utf8), math.random seeded with a
 * constant, and a count hook that stops an evaluation past its instruction
 * budget. The registry holds the source of each scripted kind and the shared,
 * read-only base environment.
 *
 * An instance: its own environment table (its globals and file-scope locals
 * are its own), the module table its chunk returns, and a ctx table:
 *   ctx.id, ctx.params, ctx.state, ctx.pins[name].value / .level / .name,
 *   ctx.now(), ctx.drive(pin, value, strength?, delay_ps?), ctx.release(pin),
 *   ctx.wake_at(time_ps, token?), ctx.cancel(token?), ctx.input(name),
 *   ctx.diag(severity, code, message).
 * Levels are 0 and 1, "z" (nothing drives the net) and "x" (unknown); a
 * drive accepts those, booleans and "0"/"1". Tokens are 0 to 7.
 *
 * Callbacks: reset (a fresh ctx.state, then M.create and M.reset),
 * on_input(ctx, pin), on_time(ctx, token), on_set(ctx, name, value) (false
 * refuses the value), observe(ctx) -> outputs. An error or an exceeded budget
 * is a diagnostic (ext.error, ext.budget_exceeded) and stops the instance
 * until the next reset. */
#include "ext/lua_model.h"

#include <string.h>

#include "util/json.h"
#include "util/str.h"

#if defined(EMBSIM_LUA)

#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

#define HOOK_EVERY 1000
#define MEM_LIMIT ((size_t)64 << 20)
#define MAX_DEPTH 16

static const char SOURCES_KEY[] = "embsim.sources";
static const char BASE_KEY[] = "embsim.base";

typedef struct lua_host {
    esk_alloc *a;
    lua_State *L;
    size_t mem;
    int64_t budget, count;
    int depth, exceeded;
    esk_comp *current; /* the instance being evaluated (print, diagnostics) */
} lua_host;

typedef struct i2c_slave i2c_slave;
typedef struct spi_slave spi_slave;

typedef struct lua_inst {
    lua_host *h;
    int mod, ctx, env; /* registry references */
    int dead;
    i2c_slave *i2c;    /* ctx.bus helpers, when the model asked for them */
    spi_slave *spi;
} lua_inst;

static int run(esk_comp *c, int nargs, int nres);
static int api_i2c_slave(lua_State *L);
static int api_spi_slave(lua_State *L);

static lua_host *host_of(lua_State *L)
{
    void *ud = NULL;
    lua_getallocf(L, &ud);
    return (lua_host *)ud;
}

static void *host_alloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
    lua_host *h = (lua_host *)ud;
    if (nsize == 0) {
        if (ptr) {
            esk_free(h->a, ptr, osize);
            h->mem -= osize;
        }
        return NULL;
    }
    if (!ptr)
        osize = 0;
    if (nsize > osize && h->mem + (nsize - osize) > MEM_LIMIT)
        return NULL;
    ptr = esk_realloc(h->a, ptr, osize, nsize);
    if (ptr)
        h->mem = h->mem - osize + nsize;
    return ptr;
}

static void budget_hook(lua_State *L, lua_Debug *ar)
{
    lua_host *h = host_of(L);
    (void)ar;
    h->count += HOOK_EVERY;
    if (h->count > h->budget) {
        h->exceeded = 1;
        luaL_error(L, "evaluation exceeded its budget of %d instructions", (int)h->budget);
    }
}

/* ---- values between Lua and JSON ------------------------------------------ */

static void push_json(lua_State *L, const esk_json *j, int depth)
{
    size_t i;
    if (!j || depth > MAX_DEPTH) {
        lua_pushnil(L);
        return;
    }
    switch (j->type) {
    case ESK_JSON_BOOL: lua_pushboolean(L, j->u.boolean); break;
    case ESK_JSON_NUMBER:
        if (j->u.number.is_integer)
            lua_pushinteger(L, (lua_Integer)j->u.number.integer);
        else
            lua_pushnumber(L, j->u.number.value);
        break;
    case ESK_JSON_STRING: lua_pushlstring(L, j->u.string.data, j->u.string.len); break;
    case ESK_JSON_ARRAY:
        lua_createtable(L, (int)esk_json_len(j), 0);
        for (i = 0; i < esk_json_len(j); i++) {
            push_json(L, esk_json_at(j, i), depth + 1);
            lua_rawseti(L, -2, (lua_Integer)i + 1);
        }
        break;
    case ESK_JSON_OBJECT:
        lua_createtable(L, 0, (int)esk_json_len(j));
        for (i = 0; i < esk_json_len(j); i++) {
            const esk_json *m = esk_json_at(j, i);
            push_json(L, m, depth + 1);
            lua_setfield(L, -2, m->key);
        }
        break;
    default: lua_pushnil(L); break;
    }
}

/* the Lua value at `idx` as JSON (NULL when it has none: functions, userdata) */
static esk_json *to_json(lua_State *L, esk_alloc *a, int idx, int depth)
{
    idx = lua_absindex(L, idx);
    switch (lua_type(L, idx)) {
    case LUA_TNIL: return esk_json_new_null(a);
    case LUA_TBOOLEAN: return esk_json_new_bool(a, lua_toboolean(L, idx));
    case LUA_TNUMBER:
        if (lua_isinteger(L, idx))
            return esk_json_new_integer(a, (int64_t)lua_tointeger(L, idx));
        {
            double d = (double)lua_tonumber(L, idx);
            return d == d && d - d == 0 ? esk_json_new_number(a, d) : esk_json_new_null(a);
        }
    case LUA_TSTRING: {
        size_t n;
        const char *s = lua_tolstring(L, idx, &n);
        return esk_json_new_stringn(a, s, n);
    }
    case LUA_TTABLE: {
        lua_Integer n = (lua_Integer)lua_rawlen(L, idx), i;
        esk_json *out;
        if (depth > MAX_DEPTH)
            return esk_json_new_null(a);
        if (n > 0) {
            out = esk_json_new_array(a);
            for (i = 1; out && i <= n; i++) {
                esk_json *v;
                lua_rawgeti(L, idx, i);
                v = to_json(L, a, -1, depth + 1);
                lua_pop(L, 1);
                if (v && esk_json_array_push(a, out, v))
                    esk_json_free(a, v);
            }
            return out;
        }
        out = esk_json_new_object(a);
        lua_pushnil(L);
        while (out && lua_next(L, idx)) {
            if (lua_type(L, -2) == LUA_TSTRING) {
                esk_json *v = to_json(L, a, -1, depth + 1);
                if (v && esk_json_object_set(a, out, lua_tostring(L, -2), v))
                    esk_json_free(a, v);
            }
            lua_pop(L, 1);
        }
        return out;
    }
    default: return NULL;
    }
}

/* ---- the ctx API ------------------------------------------------------------ */

static esk_comp *ctx_comp(lua_State *L) { return (esk_comp *)lua_touserdata(L, lua_upvalueindex(1)); }

static esk_pin *check_pin(lua_State *L, esk_comp *c, int arg)
{
    const char *name = luaL_checkstring(L, arg);
    esk_pin *p = esk_comp_pin(c, name);
    if (!p)
        luaL_error(L, "%s has no pin %s", c->kind, name);
    return p;
}

static void push_level(lua_State *L, esk_logic v)
{
    switch (v) {
    case ESK_L0: lua_pushinteger(L, 0); break;
    case ESK_L1: lua_pushinteger(L, 1); break;
    case ESK_LZ: lua_pushliteral(L, "z"); break;
    default: lua_pushliteral(L, "x"); break;
    }
}

static esk_logic check_level(lua_State *L, int arg)
{
    switch (lua_type(L, arg)) {
    case LUA_TBOOLEAN: return lua_toboolean(L, arg) ? ESK_L1 : ESK_L0;
    case LUA_TNUMBER: {
        lua_Integer v = lua_tointeger(L, arg);
        if (lua_isinteger(L, arg) && (v == 0 || v == 1))
            return v ? ESK_L1 : ESK_L0;
        break;
    }
    case LUA_TSTRING: {
        const char *s = lua_tostring(L, arg);
        if (!strcmp(s, "0")) return ESK_L0;
        if (!strcmp(s, "1")) return ESK_L1;
        if (!strcmp(s, "z") || !strcmp(s, "Z")) return ESK_LZ;
        if (!strcmp(s, "x") || !strcmp(s, "X")) return ESK_LX;
        break;
    }
    default: break;
    }
    luaL_argerror(L, arg, "a level is 0, 1, true, false, \"z\" or \"x\"");
    return ESK_LX;
}

static es_time_t check_time(lua_State *L, int arg, es_time_t def)
{
    lua_Integer t;
    if (lua_isnoneornil(L, arg))
        return def;
    t = luaL_checkinteger(L, arg);
    luaL_argcheck(L, t >= 0, arg, "a time is a non-negative integer of picoseconds");
    return (es_time_t)t;
}

static uint32_t check_token(lua_State *L, int arg)
{
    lua_Integer t = luaL_optinteger(L, arg, 0);
    luaL_argcheck(L, t >= 0 && t < 8, arg, "a token is 0 to 7");
    return (uint32_t)t;
}

static int api_now(lua_State *L)
{
    lua_pushinteger(L, (lua_Integer)esk_comp_now(ctx_comp(L)));
    return 1;
}

static int api_drive(lua_State *L)
{
    esk_comp *c = ctx_comp(L);
    esk_pin *p = check_pin(L, c, 1);
    esk_logic v = check_level(L, 2);
    const char *strength = luaL_optstring(L, 3, "strong");
    es_time_t delay = check_time(L, 4, 0);
    esk_strength s;
    if (!strcmp(strength, "strong"))
        s = ESK_STR_STRONG;
    else if (!strcmp(strength, "weak"))
        s = ESK_STR_WEAK;
    else if (!strcmp(strength, "supply"))
        s = ESK_STR_SUPPLY;
    else
        return luaL_argerror(L, 3, "a strength is \"strong\", \"weak\" or \"supply\"");
    esk_pin_set_direction(p, 1);
    if (v == ESK_LZ)
        esk_pin_release(p);
    else
        esk_pin_drive(p, v, s, delay);
    return 0;
}

static int api_release(lua_State *L)
{
    esk_comp *c = ctx_comp(L);
    esk_pin_release(check_pin(L, c, 1));
    return 0;
}

static int api_wake_at(lua_State *L)
{
    esk_comp *c = ctx_comp(L);
    es_time_t t = check_time(L, 1, 0);
    uint32_t token = check_token(L, 2);
    if (t < esk_comp_now(c))
        return luaL_error(L, "wake_at(%I): that time has passed (now %I)", (lua_Integer)t, (lua_Integer)esk_comp_now(c));
    if (esk_comp_wake_at(c, t, token) != ES_OK)
        return luaL_error(L, "wake_at: the scheduler refused the event");
    return 0;
}

static int api_cancel(lua_State *L)
{
    esk_comp_wake_cancel(ctx_comp(L), check_token(L, 1));
    return 0;
}

static int api_input(lua_State *L)
{
    esk_comp *c = ctx_comp(L);
    push_json(L, esk_json_get(c->inputs, luaL_checkstring(L, 1)), 0);
    return 1;
}

static int api_diag(lua_State *L)
{
    esk_comp *c = ctx_comp(L);
    static const char *const names[] = { "info", "warning", "error", NULL };
    int sev = luaL_checkoption(L, 1, "info", names);
    const char *code = luaL_checkstring(L, 2);
    const char *msg = luaL_optstring(L, 3, "");
    esk_comp_diag(c, (es_severity_t)sev, code, "%s", msg);
    return 0;
}

/* print(...) becomes an info diagnostic of the instance being evaluated */
static int api_print(lua_State *L)
{
    lua_host *h = host_of(L);
    int n = lua_gettop(L), i;
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    for (i = 1; i <= n; i++) {
        if (i > 1)
            luaL_addchar(&b, '\t');
        luaL_tolstring(L, i, NULL);
        luaL_addvalue(&b);
    }
    luaL_pushresult(&b);
    if (h->current)
        esk_comp_diag(h->current, ES_SEV_INFO, "ext.print", "%s", lua_tostring(L, -1));
    return 0;
}

/* ctx.pins[name]: a proxy whose fields read the pin when asked */
static int pin_field(lua_State *L)
{
    esk_pin *p = (esk_pin *)lua_touserdata(L, lua_upvalueindex(1));
    const char *k = luaL_checkstring(L, 2);
    if (!strcmp(k, "value"))
        push_level(L, esk_pin_read(p));
    else if (!strcmp(k, "level"))
        push_level(L, esk_pin_level(p));
    else if (!strcmp(k, "name"))
        lua_pushstring(L, p->name);
    else if (!strcmp(k, "connected"))
        lua_pushboolean(L, p->net != NULL);
    else
        lua_pushnil(L);
    return 1;
}

static int pins_index(lua_State *L)
{
    esk_comp *c = ctx_comp(L);
    const char *name = luaL_checkstring(L, 2);
    esk_pin *p = esk_comp_pin(c, name);
    if (!p)
        return luaL_error(L, "%s has no pin %s", c->kind, name);
    lua_newtable(L);                       /* the proxy */
    lua_createtable(L, 0, 2);              /* its metatable */
    lua_pushlightuserdata(L, p);
    lua_pushcclosure(L, pin_field, 1);
    lua_setfield(L, -2, "__index");
    lua_pushliteral(L, "pin");
    lua_setfield(L, -2, "__metatable");
    lua_setmetatable(L, -2);
    lua_pushvalue(L, 2);
    lua_pushvalue(L, -2);
    lua_rawset(L, 1);                      /* cached */
    return 1;
}

static void set_api(lua_State *L, esk_comp *c, const char *name, lua_CFunction f)
{
    lua_pushlightuserdata(L, c);
    lua_pushcclosure(L, f, 1);
    lua_setfield(L, -2, name);
}

/* pushes a new ctx for `c` */
static void push_ctx(lua_State *L, esk_comp *c)
{
    lua_createtable(L, 0, 12);
    lua_pushstring(L, c->id);
    lua_setfield(L, -2, "id");
    push_json(L, c->params, 0);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
    }
    lua_setfield(L, -2, "params");
    lua_newtable(L);
    lua_setfield(L, -2, "state");
    lua_newtable(L);                       /* pins */
    lua_createtable(L, 0, 1);
    lua_pushlightuserdata(L, c);
    lua_pushcclosure(L, pins_index, 1);
    lua_setfield(L, -2, "__index");
    lua_setmetatable(L, -2);
    lua_setfield(L, -2, "pins");
    set_api(L, c, "now", api_now);
    set_api(L, c, "drive", api_drive);
    set_api(L, c, "release", api_release);
    set_api(L, c, "wake_at", api_wake_at);
    set_api(L, c, "cancel", api_cancel);
    set_api(L, c, "input", api_input);
    set_api(L, c, "diag", api_diag);
    lua_createtable(L, 0, 2);
    set_api(L, c, "i2c_slave", api_i2c_slave);
    set_api(L, c, "spi_slave", api_spi_slave);
    lua_setfield(L, -2, "bus");
}


/* ---- ctx.bus: I2C and SPI devices over the pins (SPEC-013 §2) ---------------
 *
 * The protocol is decoded here, in C, from the pins' edges; the model sees
 * whole bytes through its handlers:
 *   ctx.bus.i2c_slave(address, {start(ctx, "read"|"write"), write(ctx, byte)
 *     -> false to NACK, read(ctx) -> byte, stop(ctx)}, {sda=, scl=}?)
 *   ctx.bus.spi_slave({select(ctx) -> first byte, transfer(ctx, byte) -> the
 *     next byte to send, deselect(ctx)}, {sck=, mosi=, miso=, cs=, mode=}?)
 * I2C: 7-bit addresses, open drain (the pins are released for 1), clock
 * stretching not used. SPI: 8-bit frames MSB first, modes 0 to 3, CS active
 * low, MISO released while not selected. Handlers may be absent. */

enum { I2C_IDLE, I2C_ADDR, I2C_WRITE, I2C_READ, I2C_IGNORE };

struct i2c_slave {
    esk_pin *sda, *scl;
    int handlers;          /* registry reference */
    unsigned addr;
    int phase;
    int bits;              /* bits received in this byte */
    unsigned shift;
    int slot;              /* the ninth clock: 0 none, 1 we ACK, 2 we NACK */
    int in_slot;           /* its falling edge has passed */
    int sent;              /* READ: bits the master has clocked in; 9 after its ACK clock */
    int master_ack;
    int addressed;         /* since the address matched, until a STOP */
    unsigned out;
    esk_logic scl_last, sda_last;
};

struct spi_slave {
    esk_pin *sck, *mosi, *miso, *cs;
    int handlers;
    int cpol, cpha;
    int selected;
    int bits, obit;
    unsigned in, out, next;
    esk_logic sck_last, cs_last;
};

/* calls handlers[name](ctx, arg?) and returns its integer result, or `def`
 * when it returns something else; *refused is set when it returned false */
static int call_handler(esk_comp *c, int handlers, const char *name, int nargs, int arg, const char *sarg, int def,
                        int *refused)
{
    lua_inst *s = (lua_inst *)c->state;
    lua_State *L = s->h->L;
    int r = def;
    if (refused)
        *refused = 0;
    if (s->dead)
        return def;
    lua_rawgeti(L, LUA_REGISTRYINDEX, handlers);
    lua_getfield(L, -1, name);
    lua_remove(L, -2);
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        return def;
    }
    lua_rawgeti(L, LUA_REGISTRYINDEX, s->ctx);
    if (nargs) {
        if (sarg)
            lua_pushstring(L, sarg);
        else
            lua_pushinteger(L, arg);
    }
    if (run(c, 1 + nargs, 1) != 0)
        return def;
    if (refused)
        *refused = lua_isboolean(L, -1) && !lua_toboolean(L, -1);
    if (lua_isinteger(L, -1))
        r = (int)(lua_tointeger(L, -1) & 0xFF);
    lua_pop(L, 1);
    return r;
}

static void od_drive(esk_pin *p, int bit)
{
    if (bit)
        esk_pin_release(p);
    else
        esk_pin_drive(p, ESK_L0, ESK_STR_STRONG, 0);
}

static void i2c_send_bit(i2c_slave *b) { od_drive(b->sda, (int)(b->out >> (7 - b->sent)) & 1); }

static void i2c_edge(esk_comp *c, i2c_slave *b, esk_pin *p)
{
    esk_logic scl = esk_pin_level(b->scl), sda = esk_pin_level(b->sda);
    int scl_hi = scl == ESK_L1 || scl == ESK_LZ, sda_hi = sda == ESK_L1 || sda == ESK_LZ; /* pulled up */
    int was_scl_hi = b->scl_last == ESK_L1 || b->scl_last == ESK_LZ;
    int was_sda_hi = b->sda_last == ESK_L1 || b->sda_last == ESK_LZ;
    int refused;
    b->scl_last = scl;
    b->sda_last = sda;
    if (p == b->sda) {
        if (!scl_hi || !was_scl_hi || sda_hi == was_sda_hi)
            return;
        if (!sda_hi) {                       /* START, or a repeated START */
            b->phase = I2C_ADDR;
            b->bits = 0;
            b->shift = 0;
            b->slot = b->in_slot = 0;
        } else {                             /* STOP */
            if (b->addressed)
                call_handler(c, b->handlers, "stop", 0, 0, NULL, 0, NULL);
            b->addressed = 0;
            b->phase = I2C_IDLE;
            esk_pin_release(b->sda);
        }
        return;
    }
    if (p != b->scl || scl_hi == was_scl_hi || b->phase == I2C_IDLE || b->phase == I2C_IGNORE)
        return;
    if (scl_hi) {                            /* rising: data is sampled */
        if (b->in_slot)
            return;
        if (b->phase == I2C_READ) {
            if (b->sent < 8)
                b->sent++;
            else {
                b->master_ack = !sda_hi;
                b->sent = 9;
            }
            return;
        }
        b->shift = (b->shift << 1 | (unsigned)sda_hi) & 0xFF;
        if (++b->bits < 8)
            return;
        if (b->phase == I2C_ADDR) {
            if ((b->shift >> 1) != b->addr) {
                b->phase = I2C_IGNORE;
                return;
            }
            call_handler(c, b->handlers, "start", 1, 0, (b->shift & 1) ? "read" : "write", 0, NULL);
            b->phase = (b->shift & 1) ? I2C_READ : I2C_WRITE;
            b->addressed = 1;
            b->slot = 1;
        } else {
            call_handler(c, b->handlers, "write", 1, (int)b->shift, NULL, 0, &refused);
            b->slot = refused ? 2 : 1;
        }
        return;
    }
    /* falling: the slave changes SDA */
    if (b->slot && !b->in_slot) {
        if (b->slot == 1)
            esk_pin_drive(b->sda, ESK_L0, ESK_STR_STRONG, 0);
        b->in_slot = 1;
        return;
    }
    if (b->in_slot) {
        b->in_slot = b->slot = 0;
        b->bits = 0;
        b->shift = 0;
        esk_pin_release(b->sda);
        if (b->phase == I2C_READ) {
            b->out = (unsigned)call_handler(c, b->handlers, "read", 0, 0, NULL, 0xFF, NULL);
            b->sent = 0;
            i2c_send_bit(b);
        }
        return;
    }
    if (b->phase == I2C_READ) {
        if (b->sent > 0 && b->sent < 8)
            i2c_send_bit(b);
        else if (b->sent == 8)
            esk_pin_release(b->sda);         /* the master's ACK */
        else if (b->sent == 9) {
            if (b->master_ack) {
                b->out = (unsigned)call_handler(c, b->handlers, "read", 0, 0, NULL, 0xFF, NULL);
                b->sent = 0;
                i2c_send_bit(b);
            } else {
                b->phase = I2C_IGNORE;       /* the master ends with a STOP */
                esk_pin_release(b->sda);
            }
        }
    }
}

static void spi_shift_out(spi_slave *b)
{
    if (!b->miso)
        return;
    if (b->obit < 0) {
        b->out = b->next;
        b->obit = 7;
    }
    esk_pin_drive(b->miso, (b->out >> b->obit) & 1 ? ESK_L1 : ESK_L0, ESK_STR_STRONG, 0);
    b->obit--;
}

static void spi_edge(esk_comp *c, spi_slave *b, esk_pin *p)
{
    esk_logic cs = esk_pin_level(b->cs), sck = esk_pin_level(b->sck);
    int was_cs = b->cs_last == ESK_L1 || b->cs_last == ESK_LZ, is_cs = cs == ESK_L1 || cs == ESK_LZ;
    int was_sck = b->sck_last == ESK_L1, is_sck = sck == ESK_L1;
    b->cs_last = cs;
    b->sck_last = sck;
    if (p == b->cs) {
        if (was_cs && !is_cs) {              /* selected */
            b->selected = 1;
            b->bits = 0;
            b->in = 0;
            b->next = (unsigned)call_handler(c, b->handlers, "select", 0, 0, NULL, 0xFF, NULL);
            b->obit = -1;
            if (!b->cpha)
                spi_shift_out(b);
        } else if (!was_cs && is_cs) {       /* released */
            b->selected = 0;
            if (b->miso)
                esk_pin_release(b->miso);
            call_handler(c, b->handlers, "deselect", 0, 0, NULL, 0, NULL);
        }
        return;
    }
    if (p != b->sck || !b->selected || was_sck == is_sck)
        return;
    {
        int leading = is_sck != b->cpol;     /* away from the idle level */
        if (leading != b->cpha) {            /* the sampling edge */
            esk_logic v = esk_pin_level(b->mosi);
            b->in = (b->in << 1 | (v == ESK_L1 ? 1u : 0u)) & 0xFF;
            if (++b->bits == 8) {
                b->next = (unsigned)call_handler(c, b->handlers, "transfer", 1, (int)b->in, NULL, 0xFF, NULL);
                b->bits = 0;
                b->in = 0;
            }
        } else {
            spi_shift_out(b);
        }
    }
}

static esk_pin *opt_pin(lua_State *L, esk_comp *c, int opts, const char *key, const char *const *names)
{
    esk_pin *p = NULL;
    if (opts && lua_getfield(L, opts, key) == LUA_TSTRING) {
        p = esk_comp_pin(c, lua_tostring(L, -1));
        if (!p)
            luaL_error(L, "%s has no pin %s", c->kind, lua_tostring(L, -1));
    }
    if (opts)
        lua_pop(L, 1);
    for (; !p && *names; names++)
        p = esk_comp_pin(c, *names);
    return p;
}

static void free_buses(lua_inst *s, esk_alloc *a)
{
    if (s->i2c) {
        luaL_unref(s->h->L, LUA_REGISTRYINDEX, s->i2c->handlers);
        esk_free(a, s->i2c, sizeof *s->i2c);
        s->i2c = NULL;
    }
    if (s->spi) {
        luaL_unref(s->h->L, LUA_REGISTRYINDEX, s->spi->handlers);
        esk_free(a, s->spi, sizeof *s->spi);
        s->spi = NULL;
    }
}

static int api_i2c_slave(lua_State *L)
{
    static const char *const sda_names[] = { "SDA", NULL }, *const scl_names[] = { "SCL", NULL };
    esk_comp *c = ctx_comp(L);
    lua_inst *s = (lua_inst *)c->state;
    lua_Integer addr = luaL_checkinteger(L, 1);
    int opts = lua_istable(L, 3) ? 3 : 0;
    esk_pin *sda, *scl;
    i2c_slave *b;
    luaL_argcheck(L, addr >= 0 && addr < 128, 1, "a 7-bit address");
    luaL_checktype(L, 2, LUA_TTABLE);
    sda = opt_pin(L, c, opts, "sda", sda_names);
    scl = opt_pin(L, c, opts, "scl", scl_names);
    if (!sda || !scl)
        return luaL_error(L, "i2c_slave: %s needs SDA and SCL pins", c->kind);
    if (s->i2c) {
        luaL_unref(L, LUA_REGISTRYINDEX, s->i2c->handlers);
        esk_free(c->e->a, s->i2c, sizeof *s->i2c);
        s->i2c = NULL;
    }
    b = (i2c_slave *)esk_calloc(c->e->a, 1, sizeof *b);
    if (!b)
        return luaL_error(L, "out of memory");
    b->sda = sda;
    b->scl = scl;
    b->addr = (unsigned)addr;
    b->sda_last = esk_pin_level(sda);
    b->scl_last = esk_pin_level(scl);
    lua_pushvalue(L, 2);
    b->handlers = luaL_ref(L, LUA_REGISTRYINDEX);
    esk_pin_set_direction(sda, 1);
    esk_pin_release(sda);
    esk_pin_watch(sda, 1);
    esk_pin_watch(scl, 1);
    s->i2c = b;
    return 0;
}

static int api_spi_slave(lua_State *L)
{
    static const char *const sck_names[] = { "SCK", "SCLK", "CLK", NULL }, *const mosi_names[] = { "MOSI", "SDI", "DIN", "SI", NULL },
                             *const miso_names[] = { "MISO", "SDO", "DOUT", "SO", NULL }, *const cs_names[] = { "CS", "SS", "CE", "NSS", NULL };
    esk_comp *c = ctx_comp(L);
    lua_inst *s = (lua_inst *)c->state;
    int opts = lua_istable(L, 2) ? 2 : 0;
    lua_Integer mode = 0;
    spi_slave *b;
    esk_pin *sck, *mosi, *miso, *cs;
    luaL_checktype(L, 1, LUA_TTABLE);
    sck = opt_pin(L, c, opts, "sck", sck_names);
    mosi = opt_pin(L, c, opts, "mosi", mosi_names);
    miso = opt_pin(L, c, opts, "miso", miso_names);
    cs = opt_pin(L, c, opts, "cs", cs_names);
    if (opts) {
        if (lua_getfield(L, opts, "mode") != LUA_TNIL)
            mode = luaL_checkinteger(L, -1);
        lua_pop(L, 1);
    }
    luaL_argcheck(L, mode >= 0 && mode <= 3, 2, "mode is 0 to 3");
    if (!sck || !mosi || !cs)
        return luaL_error(L, "spi_slave: %s needs SCK, MOSI and CS pins", c->kind);
    if (s->spi) {
        luaL_unref(L, LUA_REGISTRYINDEX, s->spi->handlers);
        esk_free(c->e->a, s->spi, sizeof *s->spi);
        s->spi = NULL;
    }
    b = (spi_slave *)esk_calloc(c->e->a, 1, sizeof *b);
    if (!b)
        return luaL_error(L, "out of memory");
    b->sck = sck;
    b->mosi = mosi;
    b->miso = miso;
    b->cs = cs;
    b->cpol = (int)(mode >> 1);
    b->cpha = (int)(mode & 1);
    b->cs_last = esk_pin_level(cs);
    b->sck_last = esk_pin_level(sck);
    b->obit = -1;
    lua_pushvalue(L, 1);
    b->handlers = luaL_ref(L, LUA_REGISTRYINDEX);
    if (miso) {
        esk_pin_set_direction(miso, 1);
        esk_pin_release(miso);
    }
    esk_pin_watch(sck, 1);
    esk_pin_watch(mosi, 1);
    esk_pin_watch(cs, 1);
    s->spi = b;
    return 0;
}

/* ---- the host ---------------------------------------------------------------- */

static void host_free(void *p)
{
    lua_host *h = (lua_host *)p;
    if (!h)
        return;
    if (h->L)
        lua_close(h->L);
    esk_free(h->a, h, sizeof *h);
}

/* the base environment every instance reads through: the allowed globals */
static int open_base(lua_State *L)
{
    static const char *const keep[] = { "assert", "error", "getmetatable", "ipairs", "next", "pairs", "pcall",
                                         "rawequal", "rawget", "rawlen", "rawset", "select", "setmetatable",
                                         "tonumber", "tostring", "type", "xpcall", "_VERSION", NULL };
    static const char *const libs[] = { LUA_STRLIBNAME, LUA_TABLIBNAME, LUA_MATHLIBNAME, LUA_UTF8LIBNAME, NULL };
    int i;
    luaL_requiref(L, LUA_GNAME, luaopen_base, 1);
    lua_pop(L, 1);
    luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1);
    lua_pop(L, 1);
    luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1);
    lua_pop(L, 1);
    luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1);
    lua_pop(L, 1);
    luaL_requiref(L, LUA_UTF8LIBNAME, luaopen_utf8, 1);
    lua_pop(L, 1);
    /* string.dump would hand out bytecode; nothing loads it, but drop it */
    lua_getglobal(L, LUA_STRLIBNAME);
    lua_pushnil(L);
    lua_setfield(L, -2, "dump");
    lua_pop(L, 1);
    /* a constant seed: the same session draws the same numbers */
    lua_getglobal(L, LUA_MATHLIBNAME);
    lua_getfield(L, -1, "randomseed");
    lua_pushinteger(L, 0);
    lua_call(L, 1, 0);
    lua_pop(L, 1);

    lua_newtable(L);
    for (i = 0; keep[i]; i++) {
        lua_getglobal(L, keep[i]);
        lua_setfield(L, -2, keep[i]);
    }
    for (i = 0; libs[i]; i++) {
        lua_getglobal(L, libs[i]);
        lua_setfield(L, -2, libs[i]);
    }
    lua_pushcfunction(L, api_print);
    lua_setfield(L, -2, "print");
    lua_setfield(L, LUA_REGISTRYINDEX, BASE_KEY);
    lua_newtable(L);
    lua_setfield(L, LUA_REGISTRYINDEX, SOURCES_KEY);
    return 0;
}

static lua_host *host_get(esk_engine *e)
{
    lua_host *h;
    if (e->ext)
        return e->ext_free == host_free ? (lua_host *)e->ext : NULL;
    h = (lua_host *)esk_calloc(e->a, 1, sizeof *h);
    if (!h)
        return NULL;
    h->a = e->a;
    h->budget = ESK_LUA_BUDGET;
    h->L = lua_newstate(host_alloc, h);
    if (!h->L) {
        esk_free(e->a, h, sizeof *h);
        return NULL;
    }
    lua_pushcfunction(h->L, open_base);
    if (lua_pcall(h->L, 0, 0, 0) != LUA_OK) {
        host_free(h);
        return NULL;
    }
    lua_sethook(h->L, budget_hook, LUA_MASKCOUNT, HOOK_EVERY);
    e->ext = h;
    e->ext_free = host_free;
    return h;
}

int esk_lua_available(void) { return 1; }

es_status esk_lua_register(esk_engine *e, const char *kind, const char *source, size_t len, const char *chunkname,
                           char *why, size_t cap)
{
    lua_host *h = host_get(e);
    lua_State *L;
    int st;
    if (why && cap)
        why[0] = 0;
    if (!h)
        return ES_ENOMEM;
    L = h->L;
    lua_getfield(L, LUA_REGISTRYINDEX, SOURCES_KEY);
    if (lua_getfield(L, -1, kind) != LUA_TNIL) {
        lua_pop(L, 2);
        return ES_OK;
    }
    lua_pop(L, 1);
    /* compile once to report syntax errors at load; text chunks only */
    st = luaL_loadbufferx(L, source, len, chunkname, "t");
    if (st != LUA_OK) {
        if (why && cap)
            esk_snprintf(why, cap, "%s", lua_tostring(L, -1));
        lua_pop(L, 2);
        return st == LUA_ERRMEM ? ES_ENOMEM : ES_ESCHEMA;
    }
    lua_pop(L, 1);
    lua_createtable(L, 0, 2);
    lua_pushlstring(L, source, len);
    lua_setfield(L, -2, "source");
    lua_pushstring(L, chunkname);
    lua_setfield(L, -2, "name");
    lua_setfield(L, -2, kind);
    lua_pop(L, 1);
    return ES_OK;
}

/* ---- evaluations --------------------------------------------------------- */

/* runs the function under the `nargs` arguments on the stack in protected
 * mode with the budget; reports a failure and stops the instance */
static int run(esk_comp *c, int nargs, int nres)
{
    lua_inst *s = (lua_inst *)c->state;
    lua_host *h = s->h;
    esk_comp *outer = h->current;
    int st;
    if (h->depth++ == 0) {
        h->count = 0;
        h->exceeded = 0;
    }
    h->current = c;
    st = lua_pcall(h->L, nargs, nres, 0);
    h->current = outer;
    h->depth--;
    if (st == LUA_OK)
        return 0;
    {
        const char *msg = lua_tostring(h->L, -1);
        int budget = h->exceeded;
        if (h->depth == 0)
            h->exceeded = 0;
        esk_comp_diag(c, ES_SEV_ERROR, budget ? "ext.budget_exceeded" : "ext.error",
                      "%s: %s (the model stops until the next reset)", c->kind, msg ? msg : "error object is not a string");
    }
    lua_pop(h->L, 1);
    s->dead = 1;
    return -1;
}

/* pushes M[name] and ctx; 0 when the module has no such function */
static int begin(lua_inst *s, const char *name)
{
    lua_State *L = s->h->L;
    if (s->dead || !s->mod)
        return 0;
    lua_rawgeti(L, LUA_REGISTRYINDEX, s->mod);
    lua_getfield(L, -1, name);
    lua_remove(L, -2);
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        return 0;
    }
    lua_rawgeti(L, LUA_REGISTRYINDEX, s->ctx);
    return 1;
}

static void unref(lua_inst *s)
{
    lua_State *L = s->h->L;
    free_buses(s, s->h->a);
    if (s->mod)
        luaL_unref(L, LUA_REGISTRYINDEX, s->mod);
    if (s->ctx)
        luaL_unref(L, LUA_REGISTRYINDEX, s->ctx);
    if (s->env)
        luaL_unref(L, LUA_REGISTRYINDEX, s->env);
    s->mod = s->ctx = s->env = 0;
}

static es_status lua_create(esk_comp *c)
{
    lua_inst *s = (lua_inst *)c->state;
    lua_host *h = c->e->ext_free == host_free ? (lua_host *)c->e->ext : NULL;
    lua_State *L;
    size_t len;
    const char *src;
    uint32_t i;
    if (!h) {
        esk_comp_diag(c, ES_SEV_ERROR, "ext.error", "%s: no script was registered", c->kind);
        return ES_ESTATE;
    }
    s->h = h;
    L = h->L;
    lua_getfield(L, LUA_REGISTRYINDEX, SOURCES_KEY);
    if (lua_getfield(L, -1, c->kind) != LUA_TTABLE) {
        lua_pop(L, 2);
        esk_comp_diag(c, ES_SEV_ERROR, "ext.error", "%s: no script was registered", c->kind);
        return ES_ESTATE;
    }
    lua_getfield(L, -1, "name");
    lua_getfield(L, -2, "source");
    src = lua_tolstring(L, -1, &len);
    if (luaL_loadbufferx(L, src, len, lua_tostring(L, -2), "t") != LUA_OK) {
        esk_comp_diag(c, ES_SEV_ERROR, "ext.error", "%s: %s", c->kind, lua_tostring(L, -1));
        lua_pop(L, 5);
        return ES_ESCHEMA;
    }
    lua_replace(L, -5);                    /* [chunk, entry, name, source] */
    lua_pop(L, 3);                         /* [chunk] */
    /* the instance's environment, reading through to the base */
    lua_newtable(L);
    lua_createtable(L, 0, 1);
    lua_getfield(L, LUA_REGISTRYINDEX, BASE_KEY);
    lua_setfield(L, -2, "__index");
    lua_setmetatable(L, -2);
    lua_pushvalue(L, -1);
    s->env = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_setupvalue(L, -2, 1);              /* the chunk's _ENV */
    if (run(c, 0, 1) != 0) {
        unref(s);
        return ES_ESCHEMA;
    }
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        esk_comp_diag(c, ES_SEV_ERROR, "ext.error", "%s: the script must return its model table", c->kind);
        unref(s);
        return ES_ESCHEMA;
    }
    s->mod = luaL_ref(L, LUA_REGISTRYINDEX);
    push_ctx(L, c);
    s->ctx = luaL_ref(L, LUA_REGISTRYINDEX);
    for (i = 0; i < c->npins; i++)
        if (c->pins[i].model.kind == ESK_PIN_DIGITAL_IN || c->pins[i].model.kind == ESK_PIN_DIGITAL_IO)
            esk_pin_watch(&c->pins[i], 1);
    return ES_OK;
}

static void lua_destroy(esk_comp *c)
{
    lua_inst *s = (lua_inst *)c->state;
    if (s->h)
        unref(s);
}

static void lua_reset(esk_comp *c)
{
    lua_inst *s = (lua_inst *)c->state;
    lua_State *L;
    if (!s->h || !s->mod)
        return;
    L = s->h->L;
    s->dead = 0;
    free_buses(s, s->h->a);                /* create registers them again */
    lua_rawgeti(L, LUA_REGISTRYINDEX, s->ctx);
    lua_newtable(L);
    lua_setfield(L, -2, "state");
    lua_pop(L, 1);
    if (begin(s, "create") && run(c, 1, 0) != 0)
        return;
    if (begin(s, "reset"))
        run(c, 1, 0);
}

static void lua_on_input(esk_comp *c, esk_pin *p)
{
    lua_inst *s = (lua_inst *)c->state;
    if (s->i2c && !s->dead && (p == s->i2c->sda || p == s->i2c->scl))
        i2c_edge(c, s->i2c, p);
    if (s->spi && !s->dead && (p == s->spi->sck || p == s->spi->cs || p == s->spi->mosi))
        spi_edge(c, s->spi, p);
    if (!begin(s, "on_input"))
        return;
    lua_pushstring(s->h->L, p->name);
    run(c, 2, 0);
}

static void lua_on_time(esk_comp *c, uint32_t token)
{
    lua_inst *s = (lua_inst *)c->state;
    if (!begin(s, "on_time"))
        return;
    lua_pushinteger(s->h->L, (lua_Integer)token);
    run(c, 2, 0);
}

static es_status lua_on_set(esk_comp *c, const char *input, const esk_json *value)
{
    lua_inst *s = (lua_inst *)c->state;
    lua_State *L;
    int refused;
    if (!begin(s, "on_set"))
        return ES_EINVAL;
    L = s->h->L;
    lua_pushstring(L, input);
    push_json(L, value, 0);
    if (run(c, 3, 1) != 0)
        return ES_ESTATE;
    refused = lua_isboolean(L, -1) && !lua_toboolean(L, -1);
    lua_pop(L, 1);
    return refused ? ES_EINVAL : ES_OK;
}

static void lua_observe(esk_comp *c, esk_json *out)
{
    lua_inst *s = (lua_inst *)c->state;
    lua_State *L;
    if (!begin(s, "observe"))
        return;
    L = s->h->L;
    if (run(c, 1, 1) != 0)
        return;
    if (lua_istable(L, -1)) {
        lua_pushnil(L);
        while (lua_next(L, -2)) {
            if (lua_type(L, -2) == LUA_TSTRING) {
                esk_json *v = to_json(L, c->e->a, -1, 0);
                if (v && esk_json_object_set(c->e->a, out, lua_tostring(L, -2), v))
                    esk_json_free(c->e->a, v);
            }
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
}

static const esk_model_ops lua_model_ops = { "lua", sizeof(lua_inst), lua_create, lua_destroy, lua_reset,
                                             lua_on_input, lua_on_time, lua_on_set, lua_observe, NULL, 0 };

const esk_model_ops *esk_lua_ops(void) { return &lua_model_ops; }

#else /* !EMBSIM_LUA */

int esk_lua_available(void) { return 0; }

es_status esk_lua_register(esk_engine *e, const char *kind, const char *source, size_t len, const char *chunkname,
                           char *why, size_t cap)
{
    ES_UNUSED(e); ES_UNUSED(kind); ES_UNUSED(source); ES_UNUSED(len); ES_UNUSED(chunkname);
    if (why && cap)
        esk_snprintf(why, cap, "%s", "this build has no Lua model tier (EMBSIM_LUA=OFF)");
    return ES_EUNSUPPORTED;
}

const esk_model_ops *esk_lua_ops(void) { return NULL; }

#endif
