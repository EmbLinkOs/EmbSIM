/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* sdk.c -- the authoring commands of SPEC-014 §4-5:
 *   embsim catalog new component NAME [--library DIR] [--lua]
 *   embsim catalog new board NAME [--library DIR]
 *   embsim catalog install DIR [--to LIBRARIES_DIR] [--force]
 * `new` writes a template that validates as written (its fixture passes), so
 * an author starts from a working part and changes it; `install` validates a
 * library and copies it into the user library directory. */
#include "sdk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "embsim/session.h"
#include "host/host.h"
#include "project/catalog.h"
#include "util/json.h"
#include "util/str.h"

/* ---- templates: @LIB@, @NAME@ and @TITLE@ are replaced -------------------- */

static const char *const PASSIVE_JSON =
    "{\n"
    "  \"schema_version\": 1,\n"
    "  \"kind\": \"component\",\n"
    "  \"id\": \"@LIB@/@NAME@\",\n"
    "  \"version\": \"0.1.0\",\n"
    "  \"name\": \"@TITLE@\",\n"
    "  \"category\": \"passives\",\n"
    "  \"description\": \"A two-terminal resistance. Replace this with what your part is.\",\n"
    "  \"keywords\": [\"@NAME@\"],\n"
    "  \"pins\": [\n"
    "    {\"name\": \"1\", \"number\": 1, \"model\": {\"kind\": \"passive\"}, \"doc\": \"First terminal\"},\n"
    "    {\"name\": \"2\", \"number\": 2, \"model\": {\"kind\": \"passive\"}, \"doc\": \"Second terminal\"}\n"
    "  ],\n"
    "  \"params\": {\n"
    "    \"resistance\": {\"type\": \"number\", \"unit\": \"ohm\", \"default\": \"1k\", \"min\": \"1m\", \"max\": \"1G\", \"doc\": \"Resistance\"}\n"
    "  },\n"
    "  \"inputs\": [],\n"
    "  \"outputs\": [],\n"
    "  \"model\": {\"primitive\": \"resistor\", \"bind\": {\"r\": \"resistance\"}},\n"
    "  \"functional\": {\"primitive\": \"wire\"},\n"
    "  \"symbol\": {\n"
    "    \"svg\": \"symbol.svg\",\n"
    "    \"width\": 60,\n"
    "    \"height\": 20,\n"
    "    \"pins\": {\"1\": {\"x\": 0, \"y\": 10, \"side\": \"left\"}, \"2\": {\"x\": 60, \"y\": 10, \"side\": \"right\"}}\n"
    "  },\n"
    "  \"fidelity\": {\n"
    "    \"functional\": {\"claims\": \"A wire.\", \"omissions\": [\"resistance\"]},\n"
    "    \"timed\": {\"claims\": \"As electrical when its nets are analog; otherwise a wire.\", \"omissions\": []},\n"
    "    \"electrical\": {\"claims\": \"Linear resistance (SPEC-004 §7 resistor).\", \"omissions\": [\"noise\", \"self-heating\"],\n"
    "                   \"range\": {\"current_a\": [-1, 1]}, \"validation\": [\"tests/*.json\"]}\n"
    "  },\n"
    "  \"provenance\": {\"author\": \"Your name\", \"license\": \"Apache-2.0\"},\n"
    "  \"tests\": [\"tests/*.json\"]\n"
    "}\n";

static const char *const PASSIVE_SVG =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 60 20\" width=\"60\" height=\"20\" data-grid=\"10\">\n"
    "  <style>.s{stroke:var(--symbol-stroke,#cdd6e4);fill:none;stroke-width:1.5;stroke-linecap:round;stroke-linejoin:round}"
    ".f{stroke:var(--symbol-stroke,#cdd6e4);fill:var(--symbol-fill,#1b2230);stroke-width:1.5}</style>\n"
    "  <line class=\"s\" id=\"pin-1\" x1=\"0\" y1=\"10\" x2=\"15\" y2=\"10\"/>\n"
    "  <rect class=\"f\" x=\"15\" y=\"5\" width=\"30\" height=\"10\" rx=\"1\"/>\n"
    "  <line class=\"s\" id=\"pin-2\" x1=\"45\" y1=\"10\" x2=\"60\" y2=\"10\"/>\n"
    "</svg>\n";

static const char *const PASSIVE_TEST =
    "{\n"
    "  \"schema_version\": 1,\n"
    "  \"kind\": \"component_test\",\n"
    "  \"name\": \"ohms-law\",\n"
    "  \"description\": \"3.3 V across 1 kOhm: 3.3 mA.\",\n"
    "  \"policy\": \"electrical\",\n"
    "  \"until\": \"1ms\",\n"
    "  \"circuit\": {\n"
    "    \"components\": [\n"
    "      {\"id\": \"V1\", \"component\": \"emblink/vsource\", \"params\": {\"kind\": \"dc\", \"dc\": 3.3}},\n"
    "      {\"id\": \"DUT\", \"component\": \"@LIB@/@NAME@\", \"params\": {\"resistance\": \"1k\"}},\n"
    "      {\"id\": \"G\", \"component\": \"emblink/gnd\"}\n"
    "    ],\n"
    "    \"nets\": {\"VIN\": [\"V1.+\", \"DUT.1\"], \"GND\": [\"V1.-\", \"DUT.2\", \"G.GND\"]}\n"
    "  },\n"
    "  \"expect\": {\"device:DUT.i\": {\"at\": \"0.5ms\", \"min\": 0.00325, \"max\": 0.00335}}\n"
    "}\n";

static const char *const LUA_JSON =
    "{\n"
    "  \"schema_version\": 1,\n"
    "  \"kind\": \"component\",\n"
    "  \"id\": \"@LIB@/@NAME@\",\n"
    "  \"version\": \"0.1.0\",\n"
    "  \"name\": \"@TITLE@\",\n"
    "  \"category\": \"logic\",\n"
    "  \"description\": \"A toggle flip-flop written in Lua: Q changes on every rising edge of CLK. Replace this with what your part is.\",\n"
    "  \"keywords\": [\"@NAME@\", \"lua\"],\n"
    "  \"family\": \"cmos_3v3\",\n"
    "  \"pins\": [\n"
    "    {\"name\": \"CLK\", \"number\": 1, \"model\": {\"kind\": \"digital_in\"}, \"doc\": \"Clock\"},\n"
    "    {\"name\": \"Q\", \"number\": 2, \"model\": {\"kind\": \"digital_out\"}, \"doc\": \"Output\"}\n"
    "  ],\n"
    "  \"params\": {\n"
    "    \"tpd\": {\"type\": \"number\", \"unit\": \"s\", \"default\": \"10n\", \"min\": 0, \"max\": \"1m\", \"doc\": \"Clock-to-output delay\"}\n"
    "  },\n"
    "  \"inputs\": [],\n"
    "  \"outputs\": [{\"name\": \"count\", \"type\": \"number\", \"doc\": \"Rising edges seen since reset\"}],\n"
    "  \"model\": {\"script\": \"model.lua\", \"api\": 1},\n"
    "  \"symbol\": {\n"
    "    \"svg\": \"symbol.svg\",\n"
    "    \"width\": 60,\n"
    "    \"height\": 40,\n"
    "    \"pins\": {\"CLK\": {\"x\": 0, \"y\": 20, \"side\": \"left\"}, \"Q\": {\"x\": 60, \"y\": 20, \"side\": \"right\"}}\n"
    "  },\n"
    "  \"fidelity\": {\n"
    "    \"functional\": {\"claims\": \"Q toggles on each rising edge of CLK after tpd.\", \"omissions\": [\"setup and hold\", \"power\"],\n"
    "                   \"validation\": [\"tests/*.json\"]},\n"
    "    \"timed\": {\"claims\": \"As functional.\", \"omissions\": [\"setup and hold\"]},\n"
    "    \"electrical\": null\n"
    "  },\n"
    "  \"provenance\": {\"author\": \"Your name\", \"license\": \"Apache-2.0\"},\n"
    "  \"tests\": [\"tests/*.json\"]\n"
    "}\n";

static const char *const LUA_MODEL =
    "-- @TITLE@: a scripted model (SPEC-013). ctx.params holds the parameters in\n"
    "-- SI units, ctx.pins[name].value reads a pin (0, 1, \"z\" or \"x\"),\n"
    "-- ctx.drive(pin, level, strength, delay_ps) drives one, ctx.state keeps\n"
    "-- what must survive between calls. Each instance runs its own copy.\n"
    "local M = {}\n"
    "\n"
    "local function delay(ctx) return math.floor(ctx.params.tpd * 1e12 + 0.5) end\n"
    "\n"
    "function M.reset(ctx)\n"
    "  ctx.state.q, ctx.state.count = 0, 0\n"
    "  ctx.drive(\"Q\", 0)\n"
    "end\n"
    "\n"
    "function M.on_input(ctx, pin)\n"
    "  if pin == \"CLK\" and ctx.pins.CLK.value == 1 then\n"
    "    ctx.state.q = 1 - ctx.state.q\n"
    "    ctx.state.count = ctx.state.count + 1\n"
    "    ctx.drive(\"Q\", ctx.state.q, \"strong\", delay(ctx))\n"
    "  end\n"
    "end\n"
    "\n"
    "function M.observe(ctx)\n"
    "  return { count = ctx.state.count }\n"
    "end\n"
    "\n"
    "return M\n";

static const char *const LUA_SVG =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 60 40\" width=\"60\" height=\"40\" data-grid=\"10\">\n"
    "  <style>.s{stroke:var(--symbol-stroke,#cdd6e4);fill:none;stroke-width:1.5;stroke-linecap:round;stroke-linejoin:round}"
    ".f{stroke:var(--symbol-stroke,#cdd6e4);fill:var(--symbol-fill,#1b2230);stroke-width:1.5}"
    ".t{fill:var(--symbol-stroke,#cdd6e4);font-family:sans-serif;font-size:7px}</style>\n"
    "  <rect class=\"f\" x=\"10\" y=\"5\" width=\"40\" height=\"30\" rx=\"2\"/>\n"
    "  <line class=\"s\" id=\"pin-CLK\" x1=\"0\" y1=\"20\" x2=\"10\" y2=\"20\"/>\n"
    "  <polyline class=\"s\" points=\"10,16 14,20 10,24\"/>\n"
    "  <line class=\"s\" id=\"pin-Q\" x1=\"50\" y1=\"20\" x2=\"60\" y2=\"20\"/>\n"
    "  <text class=\"t\" x=\"16\" y=\"22\">T</text><text class=\"t\" x=\"40\" y=\"22\">Q</text>\n"
    "</svg>\n";

static const char *const LUA_TEST =
    "{\n"
    "  \"schema_version\": 1,\n"
    "  \"kind\": \"component_test\",\n"
    "  \"name\": \"halves\",\n"
    "  \"description\": \"A 1 kHz clock: Q runs at 500 Hz and ten rising edges are counted in 10 ms.\",\n"
    "  \"policy\": \"timed\",\n"
    "  \"until\": \"10ms\",\n"
    "  \"circuit\": {\n"
    "    \"components\": [\n"
    "      {\"id\": \"CK\", \"component\": \"emblink/clock_source\", \"params\": {\"freq\": \"1k\"}},\n"
    "      {\"id\": \"DUT\", \"component\": \"@LIB@/@NAME@\"}\n"
    "    ],\n"
    "    \"nets\": {\"CLK\": [\"CK.OUT\", \"DUT.CLK\"], \"Q\": [\"DUT.Q\"]}\n"
    "  },\n"
    "  \"expect\": {\n"
    "    \"net:Q\": {\"between\": [\"0\", \"10ms\"], \"frequency_hz\": 500, \"tolerance\": 0.05},\n"
    "    \"output:DUT.count\": {\"at\": \"10ms\", \"equals\": 10}\n"
    "  }\n"
    "}\n";

static const char *const BOARD_JSON =
    "{\n"
    "  \"schema_version\": 1,\n"
    "  \"kind\": \"board\",\n"
    "  \"id\": \"@LIB@/@NAME@\",\n"
    "  \"version\": \"0.1.0\",\n"
    "  \"name\": \"@TITLE@\",\n"
    "  \"description\": \"An STM32F103C8 with a 3.3 V regulator and a user LED on PC13. Replace this with your board.\",\n"
    "  \"keywords\": [\"@NAME@\", \"stm32\"],\n"
    "  \"mcu\": {\"ref\": \"st/stm32f103c8\", \"package\": \"lqfp48\", \"instance\": \"U1\"},\n"
    "  \"components\": [\n"
    "    {\"id\": \"U2\", \"component\": \"emblink/regulator\", \"params\": {\"part\": \"AMS1117-3.3\"}, \"doc\": \"3.3 V regulator\"},\n"
    "    {\"id\": \"LED1\", \"component\": \"emblink/led\", \"params\": {\"color\": \"green\"}, \"doc\": \"User LED, cathode on PC13\"},\n"
    "    {\"id\": \"R1\", \"component\": \"emblink/resistor\", \"params\": {\"resistance\": \"1k\"}},\n"
    "    {\"id\": \"R2\", \"component\": \"emblink/resistor\", \"params\": {\"resistance\": \"10k\"}, \"doc\": \"NRST pull-up\"},\n"
    "    {\"id\": \"Y1\", \"component\": \"emblink/crystal\", \"params\": {\"hz\": \"8meg\"}, \"doc\": \"HSE crystal\"},\n"
    "    {\"id\": \"Y2\", \"component\": \"emblink/crystal\", \"params\": {\"hz\": 32768, \"c0\": \"1.5p\", \"cm\": \"3f\", \"lm\": \"7.8k\", \"rm\": \"35k\"}, \"doc\": \"LSE crystal\"},\n"
    "    {\"id\": \"J1\", \"component\": \"emblink/header\", \"params\": {\"pins\": 8}, \"doc\": \"5V 3V3 GND A0 TX RX C13 R\"}\n"
    "  ],\n"
    "  \"oscillators\": {\n"
    "    \"hse\": {\"hz\": 8000000, \"source\": \"external_crystal\", \"component\": \"Y1\"},\n"
    "    \"lse\": {\"hz\": 32768, \"source\": \"external_crystal\", \"component\": \"Y2\"}\n"
    "  },\n"
    "  \"nets\": {\n"
    "    \"5V\": [\"U2.IN\", \"J1.1\"],\n"
    "    \"3V3\": [\"U2.OUT\", \"U1.VDD\", \"U1.VDDA\", \"U1.VBAT\", \"R1.1\", \"R2.1\", \"J1.2\"],\n"
    "    \"GND\": [\"U2.GND\", \"U1.VSS\", \"U1.VSSA\", \"J1.3\"],\n"
    "    \"LED_A\": [\"R1.2\", \"LED1.A\"],\n"
    "    \"PC13\": [\"U1.PC13\", \"LED1.K\", \"J1.7\"],\n"
    "    \"NRST\": [\"U1.NRST\", \"R2.2\", \"J1.8\"],\n"
    "    \"OSC_IN\": [\"U1.OSC_IN\", \"Y1.1\"],\n"
    "    \"OSC_OUT\": [\"U1.OSC_OUT\", \"Y1.2\"],\n"
    "    \"PC14\": [\"U1.PC14\", \"Y2.1\"],\n"
    "    \"PC15\": [\"U1.PC15\", \"Y2.2\"],\n"
    "    \"PA0\": [\"U1.PA0\", \"J1.4\"],\n"
    "    \"PA9\": [\"U1.PA9\", \"J1.5\"],\n"
    "    \"PA10\": [\"U1.PA10\", \"J1.6\"]\n"
    "  },\n"
    "  \"ports\": {\n"
    "    \"5V\": {\"net\": \"5V\", \"connector\": \"J1\", \"pin\": 1},\n"
    "    \"3V3\": {\"net\": \"3V3\", \"connector\": \"J1\", \"pin\": 2},\n"
    "    \"GND\": {\"net\": \"GND\", \"connector\": \"J1\", \"pin\": 3},\n"
    "    \"A0\": {\"net\": \"PA0\", \"connector\": \"J1\", \"pin\": 4},\n"
    "    \"TX\": {\"net\": \"PA9\", \"connector\": \"J1\", \"pin\": 5},\n"
    "    \"RX\": {\"net\": \"PA10\", \"connector\": \"J1\", \"pin\": 6},\n"
    "    \"C13\": {\"net\": \"PC13\", \"connector\": \"J1\", \"pin\": 7},\n"
    "    \"R\": {\"net\": \"NRST\", \"connector\": \"J1\", \"pin\": 8}\n"
    "  },\n"
    "  \"leds\": [{\"name\": \"user\", \"component\": \"LED1\", \"gpio\": \"PC13\", \"active\": \"low\", \"color\": \"green\"}],\n"
    "  \"console\": {\"peripheral\": \"USART1\", \"tx\": \"PA9\", \"rx\": \"PA10\", \"baud\": 115200},\n"
    "  \"fidelity\": {\n"
    "    \"functional\": {\"claims\": \"Nets, ports and the user LED as logic-level parts; the MCU runs firmware.\", \"omissions\": [\"decoupling\"],\n"
    "                   \"validation\": [\"tests/*.json\"]},\n"
    "    \"timed\": {\"claims\": \"As functional.\", \"omissions\": [\"decoupling\"]},\n"
    "    \"electrical\": {\"claims\": \"Regulator, LED with its resistor and the reset pull-up.\", \"omissions\": [\"decoupling\", \"ESD diodes\"],\n"
    "                   \"validation\": [\"tests/*.json\"]}\n"
    "  },\n"
    "  \"provenance\": {\"author\": \"Your name\", \"license\": \"Apache-2.0\"},\n"
    "  \"tests\": [\"tests/*.json\"]\n"
    "}\n";

static const char *const BOARD_TEST =
    "{\n"
    "  \"schema_version\": 1,\n"
    "  \"kind\": \"component_test\",\n"
    "  \"name\": \"power\",\n"
    "  \"description\": \"5 V in: the regulator makes the 3.3 V rail and the pulled-up reset reads high.\",\n"
    "  \"policy\": \"electrical\",\n"
    "  \"until\": \"10ms\",\n"
    "  \"circuit\": {\n"
    "    \"boards\": [{\"id\": \"B1\", \"board\": \"@LIB@/@NAME@\"}],\n"
    "    \"components\": [\n"
    "      {\"id\": \"V5\", \"component\": \"emblink/vsource\", \"params\": {\"kind\": \"dc\", \"dc\": 5}},\n"
    "      {\"id\": \"G\", \"component\": \"emblink/gnd\"}\n"
    "    ],\n"
    "    \"nets\": {\"5V\": [\"V5.+\", \"B1.5V\"], \"GND\": [\"V5.-\", \"G.GND\", \"B1.GND\"], \"RAIL\": [\"B1.3V3\"], \"RST\": [\"B1.R\"]}\n"
    "  },\n"
    "  \"expect\": {\n"
    "    \"net:RAIL\": {\"at\": \"5ms\", \"min_v\": 3.2, \"max_v\": 3.4},\n"
    "    \"net:RST\": {\"at\": \"5ms\", \"min_v\": 3.1, \"max_v\": 3.4}\n"
    "  }\n"
    "}\n";

/* ---- helpers ---------------------------------------------------------------- */

static char *expand(esk_alloc *a, const char *tmpl, const char *lib, const char *name, const char *title)
{
    esk_strbuf b;
    const char *p = tmpl;
    esk_strbuf_init(&b, a);
    while (*p) {
        if (!strncmp(p, "@LIB@", 5)) {
            esk_strbuf_puts(&b, lib);
            p += 5;
        } else if (!strncmp(p, "@NAME@", 6)) {
            esk_strbuf_puts(&b, name);
            p += 6;
        } else if (!strncmp(p, "@TITLE@", 7)) {
            esk_strbuf_puts(&b, title);
            p += 7;
        } else {
            esk_strbuf_putc(&b, *p++);
        }
    }
    return esk_strbuf_detach(&b, NULL);
}

static int write_text(esk_alloc *a, const char *dir, const char *rel, const char *text)
{
    char *path = esk_host_path_join(a, dir, rel);
    char *parent = path ? esk_host_dirname(a, path) : NULL;
    int rc = 0;
    if (!path || !parent || esk_host_mkdir_p(parent) != ES_OK || esk_host_write_file(path, text, strlen(text)) != ES_OK) {
        fprintf(stderr, "embsim: cannot write %s\n", path ? path : rel);
        rc = -1;
    } else {
        printf("  wrote %s\n", path);
    }
    esk_strfree(a, parent);
    esk_strfree(a, path);
    return rc;
}

static int valid_name(const char *n)
{
    size_t i, len = strlen(n);
    if (len == 0 || len > 64 || n[0] < 'a' || n[0] > 'z')
        return 0;
    for (i = 0; i < len; i++)
        if (!((n[i] >= 'a' && n[i] <= 'z') || (n[i] >= '0' && n[i] <= '9') || n[i] == '_' || n[i] == '-'))
            return 0;
    return 1;
}

/* the library's name from DIR/library.json, which is written when missing
 * (named after the directory) */
static char *library_name(esk_alloc *a, const char *dir)
{
    char *path = esk_host_path_join(a, dir, "library.json");
    char *text = NULL, *name = NULL;
    size_t len = 0;
    if (!path)
        return NULL;
    if (esk_host_read_file(a, path, &text, &len) == ES_OK) {
        int line;
        const char *err;
        esk_json *j = esk_json_parse(a, text, len, &line, &err);
        esk_free(a, text, len + 1);
        if (j && esk_json_get_str(j, "name", NULL))
            name = esk_strdup(a, esk_json_get_str(j, "name", NULL));
        else
            fprintf(stderr, "embsim: %s has no name\n", path);
        esk_json_free(a, j);
    } else {
        /* a new library named after its directory */
        const char *base = dir + strlen(dir);
        char buf[65], json[512];
        size_t n = 0;
        while (base > dir && (base[-1] == '/' || base[-1] == '\\'))
            base--;
        {
            const char *end = base;
            while (base > dir && base[-1] != '/' && base[-1] != '\\')
                base--;
            for (; base < end && n < 64; base++) {
                char ch = *base;
                if (ch >= 'A' && ch <= 'Z')
                    ch = (char)(ch - 'A' + 'a');
                if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '-')
                    buf[n++] = ch;
            }
        }
        buf[n] = 0;
        if (!valid_name(buf))
            esk_snprintf(buf, sizeof buf, "%s", "mylib");
        esk_snprintf(json, sizeof json,
                     "{\n  \"schema_version\": 1,\n  \"kind\": \"library\",\n  \"name\": \"%s\",\n  \"version\": \"0.1.0\",\n"
                     "  \"description\": \"Components and boards for EmbSIM.\",\n  \"license\": \"Apache-2.0\",\n"
                     "  \"authors\": [\"Your name\"]\n}\n", buf);
        if (write_text(a, dir, "library.json", json) == 0)
            name = esk_strdup(a, buf);
    }
    esk_strfree(a, path);
    return name;
}

static int cmd_new(int argc, char **argv)
{
    const char *what = argc > 0 ? argv[0] : NULL, *name = argc > 1 ? argv[1] : NULL, *dir = ".";
    int lua = 0, i, rc = 0;
    esk_alloc a;
    char *lib, *sub, title[96];
    for (i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--library") && i + 1 < argc)
            dir = argv[++i];
        else if (!strcmp(argv[i], "--lua"))
            lua = 1;
        else {
            fprintf(stderr, "embsim: unknown option %s\n", argv[i]);
            return 2;
        }
    }
    if (!what || !name || (strcmp(what, "component") && strcmp(what, "board"))) {
        fputs("usage: embsim catalog new component|board NAME [--library DIR] [--lua]\n", stderr);
        return 2;
    }
    if (!valid_name(name)) {
        fprintf(stderr, "embsim: a name is [a-z][a-z0-9_-]*, 64 characters at most: %s\n", name);
        return 2;
    }
    esk_alloc_init(&a, NULL);
    lib = library_name(&a, dir);
    if (!lib)
        return 1;
    /* "my_part" -> "My part" */
    esk_snprintf(title, sizeof title, "%s", name);
    for (i = 0; title[i]; i++)
        if (title[i] == '_' || title[i] == '-')
            title[i] = ' ';
    if (title[0] >= 'a' && title[0] <= 'z')
        title[0] = (char)(title[0] - 'a' + 'A');
    sub = esk_host_path_join(&a, dir, !strcmp(what, "board") ? "boards" : "components");
    {
        char *item = sub ? esk_host_path_join(&a, sub, name) : NULL;
        if (!item || esk_host_is_dir(item)) {
            fprintf(stderr, "embsim: %s already exists\n", item ? item : name);
            esk_strfree(&a, item);
            esk_strfree(&a, sub);
            esk_strfree(&a, lib);
            return 1;
        }
        printf("%s %s/%s in %s\n", what, lib, name, dir);
        {
            struct { const char *file, *tmpl; } files[4];
            int n = 0, k;
            if (!strcmp(what, "board")) {
                files[n].file = "board.json"; files[n++].tmpl = BOARD_JSON;
                files[n].file = "tests/power.json"; files[n++].tmpl = BOARD_TEST;
            } else if (lua) {
                files[n].file = "component.json"; files[n++].tmpl = LUA_JSON;
                files[n].file = "model.lua"; files[n++].tmpl = LUA_MODEL;
                files[n].file = "symbol.svg"; files[n++].tmpl = LUA_SVG;
                files[n].file = "tests/halves.json"; files[n++].tmpl = LUA_TEST;
            } else {
                files[n].file = "component.json"; files[n++].tmpl = PASSIVE_JSON;
                files[n].file = "symbol.svg"; files[n++].tmpl = PASSIVE_SVG;
                files[n].file = "tests/ohms-law.json"; files[n++].tmpl = PASSIVE_TEST;
            }
            for (k = 0; k < n && rc == 0; k++) {
                char *text = expand(&a, files[k].tmpl, lib, name, title);
                rc = text ? write_text(&a, item, files[k].file, text) : -1;
                if (text)
                    esk_strfree(&a, text);
            }
        }
        if (rc == 0)
            printf("next: edit the files, then run  embsim catalog validate %s\n", dir);
        esk_strfree(&a, item);
    }
    esk_strfree(&a, sub);
    esk_strfree(&a, lib);
    return rc ? 1 : 0;
}

/* copies the directory tree `from` to `to` (created) */
static int copy_tree(esk_alloc *a, const char *from, const char *to)
{
    esk_json *names = NULL;
    size_t i;
    int rc = 0;
    if (esk_host_mkdir_p(to) != ES_OK || esk_host_list_dir(a, from, &names) != ES_OK)
        return -1;
    for (i = 0; i < esk_json_len(names) && rc == 0; i++) {
        const char *n = esk_json_str(esk_json_at(names, i));
        char *src = esk_host_path_join(a, from, n), *dst = esk_host_path_join(a, to, n);
        if (!src || !dst)
            rc = -1;
        else if (n[0] == '.')
            ; /* .git and editor files stay behind */
        else if (esk_host_is_dir(src))
            rc = copy_tree(a, src, dst);
        else {
            char *data = NULL;
            size_t len = 0;
            rc = esk_host_read_file(a, src, &data, &len) == ES_OK && esk_host_write_file(dst, data, len) == ES_OK ? 0 : -1;
            if (data)
                esk_free(a, data, len + 1);
        }
        esk_strfree(a, src);
        esk_strfree(a, dst);
    }
    esk_json_free(a, names);
    return rc;
}

static int cmd_install(int argc, char **argv, es_catalog *deps)
{
    const char *src = NULL, *to_opt = NULL;
    int force = 0, i, rc;
    char *to, *name, *dst, *report = NULL;
    esk_alloc a;
    es_status st;
    for (i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "--to") && i + 1 < argc)
            to_opt = argv[++i];
        else if (!strcmp(argv[i], "--force"))
            force = 1;
        else if (!src)
            src = argv[i];
    }
    if (!src) {
        fputs("usage: embsim catalog install LIBRARY_DIR [--to LIBRARIES_DIR] [--force]\n", stderr);
        return 2;
    }
    esk_alloc_init(&a, NULL);
    {
        char *lj = esk_host_path_join(&a, src, "library.json");
        int ok = lj && esk_host_file_exists(lj);
        esk_strfree(&a, lj);
        if (!esk_host_is_dir(src) || !ok) {
            fprintf(stderr, "embsim: %s is not a library directory (no library.json)\n", src);
            return 2;
        }
    }
    st = es_catalog_validate(deps, src, &report);
    if (!report) {
        fprintf(stderr, "embsim: cannot validate %s: %s\n", src, es_strerror(st));
        return 1;
    }
    if (st != ES_OK && !force) {
        fprintf(stderr, "embsim: %s does not validate; run  embsim catalog validate %s  (or install with --force)\n", src, src);
        es_free(report);
        return 1;
    }
    es_free(report);
    name = library_name(&a, src);
    to = to_opt ? esk_strdup(&a, to_opt) : esk_catalog_user_dir(&a);
    if (!name || !to) {
        fputs("embsim: no user library directory; pass --to DIR\n", stderr);
        esk_strfree(&a, name);
        esk_strfree(&a, to);
        return 1;
    }
    dst = esk_host_path_join(&a, to, name);
    if (dst && esk_host_is_dir(dst) && !force) {
        fprintf(stderr, "embsim: %s is installed already at %s (--force replaces its files)\n", name, dst);
        rc = 1;
    } else {
        rc = dst && copy_tree(&a, src, dst) == 0 ? 0 : 1;
        if (rc == 0)
            printf("installed %s in %s\n", name, dst);
        else
            fprintf(stderr, "embsim: cannot copy %s to %s\n", src, dst ? dst : to);
    }
    esk_strfree(&a, dst);
    esk_strfree(&a, to);
    esk_strfree(&a, name);
    return rc;
}

int embsim_catalog_sdk(const char *sub, int argc, char **argv, es_catalog *deps)
{
    if (!strcmp(sub, "new"))
        return cmd_new(argc, argv);
    if (!strcmp(sub, "install"))
        return cmd_install(argc, argv, deps);
    return -1;
}
