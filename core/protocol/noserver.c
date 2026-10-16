/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* noserver.c -- es_serve_websocket when the build has no server (the
 * EMBSIM_SERVER option is off and ws.c is not compiled): ES_EUNSUPPORTED. */
#include "embsim/session.h"

#ifndef EMBSIM_SERVER
es_status es_serve_websocket(es_session *s, const char *host, int port, const char *ui_dir, int open_browser)
{
    ES_UNUSED(s);
    ES_UNUSED(host);
    ES_UNUSED(port);
    ES_UNUSED(ui_dir);
    ES_UNUSED(open_browser);
    return ES_EUNSUPPORTED;
}
#else
typedef int esk_noserver_not_built; /* ISO C forbids an empty translation unit */
#endif
