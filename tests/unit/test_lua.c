/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Lua model tests (SPEC-013): a scripted oscillator clocking two scripted
 * counters (wake-ups, inputs, outputs, interactive inputs, one environment
 * per instance), the sandbox, determinism across sessions, the analog side
 * (ctx.analog: a node's voltage, a comparator at the model's thresholds, a
 * threshold moved past the node crossing at once, a reset restoring the pin
 * model's), and the failures: a syntax error refuses the load, a runtime
 * error and a runaway loop are diagnostics that stop only the instance. */
#include "harness.h"
#include "ext/lua_model.h"
#include "host/host.h"
#include "session/session_priv.h"

static char *lib_dir;

static void write_file(const char *rel, const char *text)
{
    esk_alloc a;
    char *path;
    esk_alloc_init(&a, NULL);
    path = esk_host_path_join(&a, lib_dir, rel);
    CHECK(path != NULL);
    if (path) {
        char *dir = esk_host_dirname(&a, path);
        CHECK(dir && esk_host_mkdir_p(dir) == ES_OK);
        CHECK(esk_host_write_file(path, text, strlen(text)) == ES_OK);
        esk_strfree(&a, dir);
        esk_strfree(&a, path);
    }
}

#define SYMBOL(w) "\"symbol\":{\"svg\":\"symbol.svg\",\"width\":" #w ",\"height\":40,\"pins\":{}},"
#define TAIL "\"fidelity\":{\"functional\":{\"claims\":\"test\",\"omissions\":[]}},\"provenance\":{\"author\":\"test\",\"license\":\"Apache-2.0\"}}"
#define IN(n) "{\"name\":\"" n "\",\"number\":1,\"model\":{\"kind\":\"digital_in\"}}"
#define OUT(n) "{\"name\":\"" n "\",\"number\":2,\"model\":{\"kind\":\"digital_out\"}}"

static const char *OSC = "{\"schema_version\":1,\"kind\":\"component\",\"id\":\"l/osc\",\"version\":\"1.0.0\",\"name\":\"Oscillator\",\"category\":\"sources\","
    "\"family\":\"cmos_3v3\",\"pins\":[" OUT("OUT") "],\"params\":{\"half_ps\":{\"type\":\"integer\",\"default\":500000000}},"
    "\"model\":{\"script\":\"model.lua\",\"api\":1}," SYMBOL(40) TAIL;
static const char *OSC_LUA =
    "local M = {}\n"
    "function M.reset(ctx) ctx.state.level = 0; ctx.drive('OUT', 0); ctx.wake_at(ctx.params.half_ps) end\n"
    "function M.on_time(ctx, token)\n"
    "  ctx.state.level = 1 - ctx.state.level\n"
    "  ctx.drive('OUT', ctx.state.level)\n"
    "  ctx.wake_at(ctx.now() + ctx.params.half_ps, token)\n"
    "end\n"
    "return M\n";

static const char *COUNTER = "{\"schema_version\":1,\"kind\":\"component\",\"id\":\"l/counter\",\"version\":\"1.0.0\",\"name\":\"Counter\",\"category\":\"logic\","
    "\"family\":\"cmos_3v3\",\"pins\":[" IN("CLK") "," OUT("Q0") "," OUT("Q1") "," OUT("Q2") "," OUT("Q3") "],\"params\":{},"
    "\"inputs\":[{\"name\":\"load\",\"type\":\"number\",\"min\":0,\"max\":15}],\"outputs\":[{\"name\":\"count\",\"type\":\"number\"}],"
    "\"model\":{\"script\":\"model.lua\",\"api\":1}," SYMBOL(60) TAIL;
/* `n` and `seen` are file-scope: each instance has its own */
static const char *COUNTER_LUA =
    "local M = {}\n"
    "local n = 0\n"
    "local function out(ctx)\n"
    "  for i = 0, 3 do ctx.drive('Q' .. i, (n >> i) & 1) end\n"
    "end\n"
    "function M.create(ctx) n = 0; seen = (seen or 0) + 1 end\n"
    "function M.reset(ctx) out(ctx) end\n"
    "function M.on_input(ctx, pin)\n"
    "  if pin == 'CLK' and ctx.pins.CLK.value == 1 then n = (n + 1) % 16; out(ctx) end\n"
    "end\n"
    "function M.on_set(ctx, name, value)\n"
    "  if name ~= 'load' or math.type(value) ~= 'integer' then return false end\n"
    "  n = value; out(ctx)\n"
    "end\n"
    "function M.observe(ctx)\n"
    "  return { count = n, seen = seen, sandboxed = os == nil and io == nil and load == nil and require == nil\n"
    "           and dofile == nil and string.dump == nil, sorted = table.concat((function() local t = {3, 1, 2}\n"
    "           table.sort(t) return t end)(), ',') }\n"
    "end\n"
    "return M\n";

static const char *BAD = "{\"schema_version\":1,\"kind\":\"component\",\"id\":\"l/bad\",\"version\":\"1.0.0\",\"name\":\"Bad\",\"category\":\"logic\","
    "\"family\":\"cmos_3v3\",\"pins\":[" IN("A") "],\"params\":{},\"model\":{\"script\":\"model.lua\",\"api\":1}," SYMBOL(40) TAIL;
static const char *BAD_LUA =
    "return { on_input = function(ctx, pin) if ctx.pins[pin].value == 1 then error('boom') end end,\n"
    "         observe = function(ctx) return { alive = true } end }\n";

static const char *SPIN = "{\"schema_version\":1,\"kind\":\"component\",\"id\":\"l/spin\",\"version\":\"1.0.0\",\"name\":\"Spin\",\"category\":\"logic\","
    "\"family\":\"cmos_3v3\",\"pins\":[" IN("A") "],\"params\":{},\"model\":{\"script\":\"model.lua\",\"api\":1}," SYMBOL(40) TAIL;
static const char *SPIN_LUA = "return { reset = function(ctx) while true do end end }\n";

static const char *BROKEN = "{\"schema_version\":1,\"kind\":\"component\",\"id\":\"l/broken\",\"version\":\"1.0.0\",\"name\":\"Broken\",\"category\":\"logic\","
    "\"family\":\"cmos_3v3\",\"pins\":[" IN("A") "],\"params\":{},\"model\":{\"script\":\"model.lua\",\"api\":1}," SYMBOL(40) TAIL;
static const char *BROKEN_LUA = "return { reset = function(ctx) end\n";

#define IO(n, k) "{\"name\":\"" n "\",\"number\":" #k ",\"model\":{\"kind\":\"digital_io\"}}"

static const char *PULLUP = "{\"schema_version\":1,\"kind\":\"component\",\"id\":\"l/pullup\",\"version\":\"1.0.0\",\"name\":\"Pull-up\",\"category\":\"passive\","
    "\"family\":\"cmos_3v3\",\"pins\":[{\"name\":\"1\",\"number\":1,\"model\":{\"kind\":\"digital_out\",\"strength\":\"weak\"}}],\"params\":{},"
    "\"model\":{\"primitive\":\"pull\",\"params\":{\"level\":1}}," SYMBOL(20) TAIL;

/* a bit-banging bus master: a list of pin operations, one every 5 us */
#define MASTER_HEAD \
    "local M = {}\n" \
    "local T = 5000000\n" \
    "local ops, pc, got, acks\n" \
    "local function push(...) ops[#ops + 1] = {...} end\n" \
    "local function set(ctx, pin, v) if v == 1 then ctx.release(pin) else ctx.drive(pin, 0) end end\n"
#define MASTER_TAIL \
    "function M.on_time(ctx)\n" \
    "  local op = ops[pc]; if not op then return end\n" \
    "  pc = pc + 1\n" \
    "  if op[1] == 'pin' then if op[3] == 'od' then set(ctx, op[2], op[4]) else ctx.drive(op[2], op[4]) end\n" \
    "  elseif op[1] == 'ack' then acks = acks .. (ctx.pins.SDA.value == 0 and 'A' or 'N')\n" \
    "  elseif op[1] == 'bit' then bits = (bits << 1) | (ctx.pins[op[2]].value == 1 and 1 or 0); n = n + 1\n" \
    "    if n == 8 then got[#got + 1] = bits; bits, n = 0, 0 end end\n" \
    "  ctx.wake_at(ctx.now() + T)\n" \
    "end\n" \
    "function M.observe(ctx) return { got = table.concat(got, ','), acks = acks, done = ops[pc] == nil } end\n" \
    "return M\n"

static const char *I2C_MASTER = "{\"schema_version\":1,\"kind\":\"component\",\"id\":\"l/i2c_master\",\"version\":\"1.0.0\",\"name\":\"I2C master\",\"category\":\"logic\","
    "\"family\":\"cmos_3v3\",\"pins\":[" IO("SDA", 1) "," IO("SCL", 2) "],\"params\":{},\"model\":{\"script\":\"model.lua\",\"api\":1}," SYMBOL(40) TAIL;
static const char *I2C_MASTER_LUA = MASTER_HEAD
    "local function sda(v) push('pin', 'SDA', 'od', v) end\n"
    "local function scl(v) push('pin', 'SCL', 'od', v) end\n"
    "local function start() sda(1) scl(1) sda(0) scl(0) end\n"
    "local function stop() sda(0) scl(1) sda(1) end\n"
    "local function bit(v) sda(v) scl(1) scl(0) end\n"
    "local function byte(b) for i = 7, 0, -1 do bit((b >> i) & 1) end sda(1) scl(1) push('ack') scl(0) end\n"
    "local function readbyte(ack) sda(1) for i = 1, 8 do scl(1) push('bit', 'SDA') scl(0) end bit(ack and 0 or 1) end\n"
    "function M.reset(ctx)\n"
    "  ops, pc, got, acks, bits, n = {}, 1, {}, '', 0, 0\n"
    "  start() byte(0x50 << 1) byte(5) byte(0xA5) byte(0x5A) stop()\n"
    "  start() byte(0x50 << 1) byte(5) start() byte(0x50 << 1 | 1) readbyte(true) readbyte(false) stop()\n"
    "  start() byte(0x51 << 1) stop()\n"
    "  ctx.release('SDA') ctx.release('SCL') ctx.wake_at(T)\n"
    "end\n" MASTER_TAIL;

static const char *EEPROM = "{\"schema_version\":1,\"kind\":\"component\",\"id\":\"l/eeprom\",\"version\":\"1.0.0\",\"name\":\"EEPROM\",\"category\":\"memory\","
    "\"family\":\"cmos_3v3\",\"pins\":[" IO("SDA", 1) "," IN("SCL") "],\"params\":{},\"model\":{\"script\":\"model.lua\",\"api\":1}," SYMBOL(40) TAIL;
static const char *EEPROM_LUA =
    "local M = {}\n"
    "local mem, ptr, first, stops\n"
    "function M.create(ctx)\n"
    "  mem, ptr, stops = {}, 0, 0\n"
    "  for i = 0, 255 do mem[i] = 0xFF end\n"
    "  ctx.bus.i2c_slave(0x50, {\n"
    "    start = function(ctx, dir) first = dir == 'write' end,\n"
    "    write = function(ctx, b) if first then ptr, first = b, false else mem[ptr] = b; ptr = (ptr + 1) % 256 end end,\n"
    "    read = function(ctx) local b = mem[ptr]; ptr = (ptr + 1) % 256; return b end,\n"
    "    stop = function(ctx) stops = stops + 1 end })\n"
    "end\n"
    "function M.observe(ctx) return { m5 = mem[5], m6 = mem[6], stops = stops } end\n"
    "return M\n";

static const char *SPI_MASTER = "{\"schema_version\":1,\"kind\":\"component\",\"id\":\"l/spi_master\",\"version\":\"1.0.0\",\"name\":\"SPI master\",\"category\":\"logic\","
    "\"family\":\"cmos_3v3\",\"pins\":[" OUT("SCK") "," OUT("MOSI") "," IN("MISO") "," OUT("CS") "],\"params\":{},\"model\":{\"script\":\"model.lua\",\"api\":1}," SYMBOL(40) TAIL;
static const char *SPI_MASTER_LUA = MASTER_HEAD
    "local function pin(name, v) push('pin', name, 'pp', v) end\n"
    "local function byte(b) for i = 7, 0, -1 do pin('MOSI', (b >> i) & 1) pin('SCK', 1) push('bit', 'MISO') pin('SCK', 0) end end\n"
    "function M.reset(ctx)\n"
    "  ops, pc, got, acks, bits, n = {}, 1, {}, '', 0, 0\n"
    "  ctx.drive('CS', 1) ctx.drive('SCK', 0) ctx.drive('MOSI', 0)\n"
    "  pin('CS', 0) byte(0x3C) byte(0x81) pin('CS', 1)\n"
    "  ctx.wake_at(T)\n"
    "end\n" MASTER_TAIL;

static const char *ECHO = "{\"schema_version\":1,\"kind\":\"component\",\"id\":\"l/echo\",\"version\":\"1.0.0\",\"name\":\"SPI echo\",\"category\":\"logic\","
    "\"family\":\"cmos_3v3\",\"pins\":[" IN("SCK") "," IN("MOSI") "," OUT("MISO") "," IN("CS") "],\"params\":{},\"model\":{\"script\":\"model.lua\",\"api\":1}," SYMBOL(40) TAIL;
static const char *ECHO_LUA =
    "local M = {}\n"
    "local seen\n"
    "function M.create(ctx)\n"
    "  seen = {}\n"
    "  ctx.bus.spi_slave({ select = function(ctx) return 0x42 end,\n"
    "                      transfer = function(ctx, b) seen[#seen + 1] = b; return b ~ 0xFF end,\n"
    "                      deselect = function(ctx) seen[#seen + 1] = 'end' end })\n"
    "end\n"
    "function M.observe(ctx) return { seen = table.concat(seen, ',') } end\n"
    "return M\n";

/* a voltage detector: OUT follows SENSE through a comparator at `rise` and
 * `fall` volts, watched from M.create, or from `late_ps` on */
static const char *DETECTOR = "{\"schema_version\":1,\"kind\":\"component\",\"id\":\"l/detector\",\"version\":\"1.0.0\",\"name\":\"Detector\",\"category\":\"sensors\","
    "\"family\":\"cmos_3v3\",\"pins\":[" IN("SENSE") "," OUT("OUT") "],\"params\":{\"rise\":{\"type\":\"number\",\"default\":2.0},"
    "\"fall\":{\"type\":\"number\",\"default\":1.0},\"late_ps\":{\"type\":\"integer\",\"default\":0}},"
    "\"model\":{\"script\":\"model.lua\",\"api\":1}," SYMBOL(40) TAIL;
static const char *DETECTOR_LUA =
    "local M = {}\n"
    "local function watch(ctx) ctx.state.watching = ctx.analog.watch('SENSE', ctx.params.rise, ctx.params.fall) end\n"
    "function M.create(ctx)\n"
    "  ctx.state.ups, ctx.state.downs = 0, 0\n"
    "  if ctx.params.late_ps > 0 then ctx.wake_at(ctx.params.late_ps) else watch(ctx) end\n"
    "  ctx.drive('OUT', 0)\n"
    "end\n"
    "function M.on_time(ctx) watch(ctx) end\n"
    "function M.on_input(ctx, pin)\n"
    "  local v = ctx.pins.SENSE.value\n"
    "  if v == 1 then ctx.state.ups = ctx.state.ups + 1; ctx.state.up_ps = ctx.now(); ctx.state.up_v = ctx.analog.voltage('SENSE')\n"
    "  elseif v == 0 then ctx.state.downs = ctx.state.downs + 1; ctx.state.down_ps = ctx.now(); ctx.state.down_v = ctx.analog.voltage('SENSE') end\n"
    "  ctx.drive('OUT', v == 1 and 1 or 0)\n"
    "end\n"
    "function M.observe(ctx)\n"
    "  return { watching = ctx.state.watching == true, ups = ctx.state.ups, downs = ctx.state.downs,\n"
    "           up_ps = ctx.state.up_ps or -1, down_ps = ctx.state.down_ps or -1, up_mv = math.floor((ctx.state.up_v or -1) * 1000 + 0.5),\n"
    "           down_mv = math.floor((ctx.state.down_v or -1) * 1000 + 0.5), mv = math.floor((ctx.analog.voltage('SENSE') or -1) * 1000 + 0.5),\n"
    "           off_analog = ctx.analog.voltage('OUT') == nil and ctx.analog.between('SENSE', 'OUT') == nil }\n"
    "end\n"
    "return M\n";

/* SENSE ramps 0 to 3.3 V in 10 ms and back in the next 10 */
static const char *ANALOG = "{\"schema_version\":1,\"kind\":\"project\",\"name\":\"analog\",\"policy\":\"electrical\",\"libraries\":[\".\"],"
    "\"components\":[{\"id\":\"V1\",\"component\":\"emblink/vsource\",\"params\":{\"kind\":\"pwl\",\"points\":[[0,0],[0.01,3.3],[0.02,0]]}},"
    "{\"id\":\"G1\",\"component\":\"emblink/gnd\"},{\"id\":\"D1\",\"component\":\"l/detector\"},"
    "{\"id\":\"D2\",\"component\":\"l/detector\",\"params\":{\"rise\":1.0,\"fall\":0.5,\"late_ps\":4000000000}}],"
    "\"nets\":{\"SENSE\":[\"V1.+\",\"D1.SENSE\",\"D2.SENSE\"],\"GND\":[\"V1.-\",\"G1.GND\"],\"O1\":[\"D1.OUT\"],\"O2\":[\"D2.OUT\"]}}";

static const char *BUSES = "{\"schema_version\":1,\"kind\":\"project\",\"name\":\"buses\",\"policy\":\"functional\",\"libraries\":[\".\"],"
    "\"components\":[{\"id\":\"M1\",\"component\":\"l/i2c_master\"},{\"id\":\"E1\",\"component\":\"l/eeprom\"},"
    "{\"id\":\"P1\",\"component\":\"l/pullup\"},{\"id\":\"P2\",\"component\":\"l/pullup\"},"
    "{\"id\":\"M2\",\"component\":\"l/spi_master\"},{\"id\":\"S1\",\"component\":\"l/echo\"}],"
    "\"nets\":{\"SDA\":[\"M1.SDA\",\"E1.SDA\",\"P1.1\"],\"SCL\":[\"M1.SCL\",\"E1.SCL\",\"P2.1\"],"
    "\"SCK\":[\"M2.SCK\",\"S1.SCK\"],\"MOSI\":[\"M2.MOSI\",\"S1.MOSI\"],\"MISO\":[\"M2.MISO\",\"S1.MISO\"],\"CS\":[\"M2.CS\",\"S1.CS\"]}}";

static const char *PROJECT = "{\"schema_version\":1,\"kind\":\"project\",\"name\":\"lua\",\"policy\":\"functional\",\"libraries\":[\".\"],"
    "\"components\":[{\"id\":\"O1\",\"component\":\"l/osc\"},{\"id\":\"C1\",\"component\":\"l/counter\"},{\"id\":\"C2\",\"component\":\"l/counter\"}],"
    "\"nets\":{\"CLK\":[\"O1.OUT\",\"C1.CLK\",\"C2.CLK\"],\"Q0\":[\"C1.Q0\"],\"Q1\":[\"C1.Q1\"],\"Q2\":[\"C1.Q2\"],\"Q3\":[\"C1.Q3\"]}}";

static const char *FAULTS = "{\"schema_version\":1,\"kind\":\"project\",\"name\":\"faults\",\"policy\":\"functional\",\"libraries\":[\".\"],"
    "\"components\":[{\"id\":\"O1\",\"component\":\"l/osc\"},{\"id\":\"B1\",\"component\":\"l/bad\"},{\"id\":\"S1\",\"component\":\"l/spin\"},"
    "{\"id\":\"C1\",\"component\":\"l/counter\"}],\"nets\":{\"CLK\":[\"O1.OUT\",\"B1.A\",\"S1.A\",\"C1.CLK\"]}}";

static const char *SYNTAX = "{\"schema_version\":1,\"kind\":\"project\",\"name\":\"syntax\",\"policy\":\"functional\",\"libraries\":[\".\"],"
    "\"components\":[{\"id\":\"X1\",\"component\":\"l/broken\"}],\"nets\":{}}";

static void setup_library(void)
{
    write_file("library.json", "{\"schema_version\":1,\"kind\":\"library\",\"name\":\"l\",\"version\":\"0.0.1\",\"license\":\"Apache-2.0\"}");
    write_file("components/osc/component.json", OSC);
    write_file("components/osc/model.lua", OSC_LUA);
    write_file("components/counter/component.json", COUNTER);
    write_file("components/counter/model.lua", COUNTER_LUA);
    write_file("components/bad/component.json", BAD);
    write_file("components/bad/model.lua", BAD_LUA);
    write_file("components/spin/component.json", SPIN);
    write_file("components/spin/model.lua", SPIN_LUA);
    write_file("components/broken/component.json", BROKEN);
    write_file("components/broken/model.lua", BROKEN_LUA);
    write_file("components/pullup/component.json", PULLUP);
    write_file("components/i2c_master/component.json", I2C_MASTER);
    write_file("components/i2c_master/model.lua", I2C_MASTER_LUA);
    write_file("components/eeprom/component.json", EEPROM);
    write_file("components/eeprom/model.lua", EEPROM_LUA);
    write_file("components/spi_master/component.json", SPI_MASTER);
    write_file("components/spi_master/model.lua", SPI_MASTER_LUA);
    write_file("components/echo/component.json", ECHO);
    write_file("components/echo/model.lua", ECHO_LUA);
    write_file("components/detector/component.json", DETECTOR);
    write_file("components/detector/model.lua", DETECTOR_LUA);
}

static es_session *open(const char *project)
{
    es_session *s = NULL;
    es_session_config_t cfg;
    memset(&cfg, 0, sizeof cfg);
    CHECK(es_session_create(&cfg, &s) == ES_OK);
    if (s && es_session_load_json(s, project, strlen(project), lib_dir) != ES_OK) {
        es_session_destroy(s);
        return NULL;
    }
    return s;
}

/* whether the session's log holds a diagnostic with this code */
static int has_diag(es_session *s, const char *code)
{
    esk_json *log = esk_diag_to_json(&s->diag);
    size_t i;
    int found = 0;
    for (i = 0; i < esk_json_len(log); i++)
        if (t_str_eq(esk_json_get_str(esk_json_at(log, i), "code", ""), code))
            found = 1;
    esk_json_free(s->diag.a, log);
    return found;
}

static const esk_json *outputs_of(const esk_json *snap, const char *id)
{
    return esk_json_get(esk_json_get(esk_json_get(snap, "components"), id), "outputs");
}

static int bus(es_session *s)
{
    int v = 0, i;
    for (i = 0; i < 4; i++) {
        char name[4] = { 'Q', (char)('0' + i), 0, 0 };
        if (esk_net_value(esk_engine_net(&s->eng, name)) == ESK_L1)
            v |= 1 << i;
    }
    return v;
}

static void test_counter(void)
{
    es_session *s = open(PROJECT);
    esk_json *snap;
    esk_json v;
    es_time_t at;
    uint64_t seq;
    CHECK(s != NULL);
    if (!s)
        return;
    CHECK(bus(s) == 0);
    /* rising edges at 0.5, 1.5, ... 9.5 ms */
    CHECK(es_session_advance(s, 10 * ES_PS_PER_MS) == ES_OK);
    CHECK_EQ(bus(s), 10);
    snap = esk_session_snapshot(s, NULL);
    CHECK(snap != NULL);
    if (snap) {
        const esk_json *c1 = outputs_of(snap, "C1"), *c2 = outputs_of(snap, "C2");
        CHECK_EQ(esk_json_get_int(c1, "count", -1), 10);
        CHECK_EQ(esk_json_get_int(c2, "count", -1), 10); /* not 20: n is per instance */
        CHECK_EQ(esk_json_get_int(c1, "seen", -1), 1);   /* globals are per instance too */
        CHECK(esk_json_get_bool(c1, "sandboxed", 0) == 1);
        CHECK_STR(esk_json_get_str(c1, "sorted", ""), "1,2,3");
        esk_json_free(s->a, snap);
    }
    /* an interactive input: accepted, and refused when the script says so */
    memset(&v, 0, sizeof v);
    v.type = ESK_JSON_NUMBER;
    v.u.number.value = 3;
    v.u.number.integer = 3;
    v.u.number.is_integer = 1;
    CHECK(esk_session_apply_input(s, "C1", "load", &v, 10 * ES_PS_PER_MS + 1, &at, &seq) == ES_OK);
    CHECK(es_session_advance(s, 10 * ES_PS_PER_MS + ES_PS_PER_MS / 4) == ES_OK);
    CHECK_EQ(bus(s), 3);
    CHECK(es_session_advance(s, 11 * ES_PS_PER_MS) == ES_OK); /* the edge at 10.5 ms */
    CHECK_EQ(bus(s), 4);
    CHECK(esk_diag_count(&s->diag, ES_SEV_ERROR) == 0);
    /* reset restarts the scripts from M.create */
    CHECK(es_session_reset(s) == ES_OK);
    CHECK_EQ(bus(s), 0);
    CHECK(es_session_advance(s, 3 * ES_PS_PER_MS) == ES_OK);
    CHECK_EQ(bus(s), 3);
    es_session_destroy(s);
}

static void test_determinism(void)
{
    es_session *a = open(PROJECT), *b = open(PROJECT);
    CHECK(a && b);
    if (a && b) {
        CHECK(es_session_advance(a, 37 * ES_PS_PER_MS) == ES_OK);
        CHECK(es_session_advance(b, 37 * ES_PS_PER_MS) == ES_OK);
        CHECK_EQ(bus(a), bus(b));
        CHECK_EQ(bus(a), 37 % 16);
        CHECK_EQ(a->eng.signal_events, b->eng.signal_events);
    }
    if (a)
        es_session_destroy(a);
    if (b)
        es_session_destroy(b);
}

static void test_faults(void)
{
    es_session *s = open(FAULTS);
    esk_json *snap;
    CHECK(s != NULL);
    if (!s)
        return;
    /* the runaway reset was stopped at load */
    CHECK(has_diag(s, "ext.budget_exceeded"));
    CHECK(es_session_advance(s, 5 * ES_PS_PER_MS) == ES_OK);
    CHECK(has_diag(s, "ext.error"));
    snap = esk_session_snapshot(s, NULL);
    if (snap) {
        CHECK(esk_json_get(outputs_of(snap, "B1"), "alive") == NULL);  /* stopped */
        CHECK_EQ(esk_json_get_int(outputs_of(snap, "C1"), "count", -1), 5); /* the others run on */
        esk_json_free(s->a, snap);
    }
    /* a reset revives the failed instance */
    CHECK(es_session_reset(s) == ES_OK);
    snap = esk_session_snapshot(s, NULL);
    if (snap) {
        CHECK(esk_json_get_bool(outputs_of(snap, "B1"), "alive", 0) == 1);
        esk_json_free(s->a, snap);
    }
    es_session_destroy(s);
}

/* ctx.bus: an EEPROM written and read back over I2C (with a NACK for an
 * absent address), and an SPI device answering a master in mode 0 */
static void test_buses(void)
{
    es_session *s = open(BUSES);
    esk_json *snap;
    CHECK(s != NULL);
    if (!s)
        return;
    CHECK(es_session_advance(s, 10 * ES_PS_PER_MS) == ES_OK);
    snap = esk_session_snapshot(s, NULL);
    CHECK(snap != NULL);
    if (snap) {
        const esk_json *m1 = outputs_of(snap, "M1"), *e1 = outputs_of(snap, "E1");
        const esk_json *m2 = outputs_of(snap, "M2"), *s1 = outputs_of(snap, "S1");
        CHECK(esk_json_get_bool(m1, "done", 0) == 1);
        CHECK_STR(esk_json_get_str(m1, "acks", ""), "AAAAAAAN");
        CHECK_STR(esk_json_get_str(m1, "got", ""), "165,90");
        CHECK_EQ(esk_json_get_int(e1, "m5", -1), 0xA5);
        CHECK_EQ(esk_json_get_int(e1, "m6", -1), 0x5A);
        CHECK_EQ(esk_json_get_int(e1, "stops", -1), 2);
        CHECK(esk_json_get_bool(m2, "done", 0) == 1);
        CHECK_STR(esk_json_get_str(m2, "got", ""), "66,195");  /* 0x42, then 0x3C ^ 0xFF */
        CHECK_STR(esk_json_get_str(s1, "seen", ""), "60,129,end");
        esk_json_free(s->a, snap);
    }
    CHECK(esk_diag_count(&s->diag, ES_SEV_ERROR) == 0);
    es_session_destroy(s);
}

/* microseconds as a signed time, for the outputs' integers */
#define US(n) ((int64_t)(n) * 1000000)

static void test_analog(void)
{
    es_session *s = open(ANALOG);
    int pass;
    CHECK(s != NULL);
    if (!s)
        return;
    for (pass = 0; pass < 2; pass++) { /* the second after a reset: the same */
        esk_json *snap;
        CHECK(es_session_advance(s, 20 * ES_PS_PER_MS) == ES_OK);
        snap = esk_session_snapshot(s, NULL);
        CHECK(snap != NULL);
        if (snap) {
            const esk_json *d1 = outputs_of(snap, "D1"), *d2 = outputs_of(snap, "D2");
            int64_t up = esk_json_get_int(d1, "up_ps", -1), down = esk_json_get_int(d1, "down_ps", -1);
            CHECK(esk_json_get_bool(d1, "watching", 0) == 1);
            CHECK(esk_json_get_bool(d1, "off_analog", 0) == 1);
            /* the operating point's first sample (low, at 0), then up at
             * 2.0 V (6.06 ms on the ramp) and down at 1.0 V (16.97 ms): the
             * model's thresholds, not the family's, each at its time */
            CHECK_EQ(esk_json_get_int(d1, "ups", -1), 1);
            CHECK_EQ(esk_json_get_int(d1, "downs", -1), 2);
            CHECK(up > US(6000) && up < US(6120));
            CHECK(down > US(16910) && down < US(17030));
            CHECK(esk_json_get_int(d1, "up_mv", -1) >= 1990 && esk_json_get_int(d1, "up_mv", -1) <= 2030);
            CHECK(esk_json_get_int(d1, "down_mv", -1) >= 970 && esk_json_get_int(d1, "down_mv", -1) <= 1010);
            CHECK(esk_json_get_int(d1, "mv", 99) <= 5);
            /* D2 watches from 4 ms at 1.0 V, the node already at 1.32 V: it
             * crosses then, at once; down at 0.5 V (18.48 ms) */
            CHECK_EQ(esk_json_get_int(d2, "ups", -1), 1);
            CHECK_EQ(esk_json_get_int(d2, "up_ps", -1), US(4000));
            CHECK(esk_json_get_int(d2, "up_mv", -1) >= 1300 && esk_json_get_int(d2, "up_mv", -1) <= 1340);
            CHECK_EQ(esk_json_get_int(d2, "downs", -1), 2);
            CHECK(esk_json_get_int(d2, "down_ps", -1) > US(18420) && esk_json_get_int(d2, "down_ps", -1) < US(18540));
            esk_json_free(s->a, snap);
        }
        CHECK(esk_diag_count(&s->diag, ES_SEV_ERROR) == 0);
        CHECK(es_session_reset(s) == ES_OK);
    }
    es_session_destroy(s);
}

static void test_syntax_error(void)
{
    es_session *s = NULL;
    es_session_config_t cfg;
    memset(&cfg, 0, sizeof cfg);
    CHECK(es_session_create(&cfg, &s) == ES_OK);
    CHECK(es_session_load_json(s, SYNTAX, strlen(SYNTAX), lib_dir) != ES_OK);
    CHECK(has_diag(s, "ext.error"));
    es_session_destroy(s);
}

TEST_MAIN_BEGIN
if (!esk_lua_available()) {
    printf("Lua tier not built; skipped\n");
    return 0;
}
{
    esk_alloc a;
    char *tmp;
    esk_alloc_init(&a, NULL);
    tmp = esk_host_temp_dir(&a);
    CHECK(tmp != NULL);
    lib_dir = esk_host_path_join(&a, tmp ? tmp : ".", "embsim-test-lua");
    esk_strfree(&a, tmp);
}
setup_library();
RUN(test_counter);
RUN(test_determinism);
RUN(test_faults);
RUN(test_buses);
RUN(test_analog);
RUN(test_syntax_error);
TEST_MAIN_END
