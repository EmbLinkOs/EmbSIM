/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "harness.h"
#include "mcu/esp32/matrix.h"
static uint32_t map_address(unsigned cpu,unsigned source) { return 0x3ff00104u+cpu*0x114u+source*4u; }
static void drive(esk_esp32_matrix *m,unsigned cpu,unsigned source,int asserted)
{
    if(source==22 || source==23) CHECK_EQ(esk_esp32_matrix_gpio(m,cpu,source==23,asserted),ES_OK);
    else if(source>=24 && source<=27) CHECK_EQ(esk_esp32_matrix_write(m,0x3ff000dcu+(source-24)*4u,4,asserted ? 1u : 0u),ES_OK);
    else CHECK_EQ(esk_esp32_matrix_source(m,source,asserted),ES_OK);
}
static void all_routes(void)
{
    unsigned cpu,source,input;const uint32_t internal=UINT32_C(0x200188c0);
    for(cpu=0;cpu<2;cpu++) for(source=0;source<69;source++) if(source!=41 && source!=42) for(input=0;input<32;input++) {
        esk_esp32_matrix m;uint32_t value=0,bit=UINT32_C(1)<<input,expected=bit&internal ? 0u : bit;
        esk_esp32_matrix_init(&m);
        CHECK_EQ(esk_esp32_matrix_read(&m,map_address(cpu,source),4,&value),ES_OK);CHECK_EQ(value,16);
        CHECK_EQ(esk_esp32_matrix_write(&m,map_address(cpu,source),4,input|UINT32_C(0xffffffe0)),ES_OK);
        CHECK_EQ(esk_esp32_matrix_read(&m,map_address(cpu,source),4,&value),ES_OK);CHECK_EQ(value,input);
        drive(&m,cpu,source,1);
        CHECK_EQ(esk_esp32_matrix_levels(&m,cpu),expected);CHECK_EQ(esk_esp32_matrix_levels(&m,1-cpu),0);
        CHECK_EQ(esk_esp32_matrix_status(&m,cpu,source/32),UINT32_C(1)<<(source%32));
        if(source!=22 && source!=23) CHECK_EQ(esk_esp32_matrix_status(&m,1-cpu,source/32),UINT32_C(1)<<(source%32));
        else CHECK_EQ(esk_esp32_matrix_status(&m,1-cpu,0),0);
        CHECK_EQ(esk_esp32_matrix_nmi_mask(&m,cpu,1),ES_OK);
        CHECK_EQ(esk_esp32_matrix_levels(&m,cpu),expected&~UINT32_C(0x4000));
        CHECK_EQ(esk_esp32_matrix_status(&m,cpu,source/32),UINT32_C(1)<<(source%32));
        drive(&m,cpu,source,0);CHECK_EQ(esk_esp32_matrix_levels(&m,cpu),0);
    }
}
static void sharing_and_permissions(void)
{
    esk_esp32_matrix m,before;uint32_t v=0;unsigned i;
    esk_esp32_matrix_init(&m);
    CHECK_EQ(esk_esp32_matrix_write(&m,map_address(0,34),4,19),ES_OK);
    CHECK_EQ(esk_esp32_matrix_write(&m,map_address(0,35),4,19),ES_OK);
    CHECK_EQ(esk_esp32_matrix_source(&m,34,1),ES_OK);CHECK_EQ(esk_esp32_matrix_source(&m,35,1),ES_OK);
    CHECK_EQ(esk_esp32_matrix_levels(&m,0),1u<<19);
    CHECK_EQ(esk_esp32_matrix_source(&m,34,0),ES_OK);CHECK_EQ(esk_esp32_matrix_levels(&m,0),1u<<19);
    CHECK_EQ(esk_esp32_matrix_write(&m,map_address(0,35),4,20),ES_OK);CHECK_EQ(esk_esp32_matrix_levels(&m,0),1u<<20);
    CHECK_EQ(esk_esp32_matrix_write(&m,map_address(1,35),4,21),ES_OK);CHECK_EQ(esk_esp32_matrix_levels(&m,1),1u<<21);
    for(i=0;i<6;i++) {
        before=m;CHECK_EQ(esk_esp32_matrix_write(&m,0x3ff000ecu+i*4u,4,UINT32_MAX),ES_OK);
        CHECK_EQ(memcmp(&before,&m,sizeof m),0);
        CHECK_EQ(esk_esp32_matrix_read(&m,0x3ff000ecu+i*4u,4,&v),ES_OK);
        CHECK_EQ(v,i%3==1 ? 8u : 0u);
    }
    CHECK_EQ(esk_esp32_matrix_write(&m,0x3ff000dc,4,2),ES_OK);
    CHECK_EQ(esk_esp32_matrix_read(&m,0x3ff000dc,4,&v),ES_OK);CHECK_EQ(v,0);
    CHECK_EQ(esk_esp32_matrix_write(&m,0x3ff000dc,4,UINT32_MAX),ES_OK);
    CHECK_EQ(esk_esp32_matrix_read(&m,0x3ff000dc,4,&v),ES_OK);CHECK_EQ(v,1);
    before=m;
    CHECK_EQ(esk_esp32_matrix_write(&m,0x3ff000dd,4,0),ES_EINVAL);
    CHECK_EQ(esk_esp32_matrix_write(&m,0x3ff000dc,1,0),ES_EINVAL);
    CHECK_EQ(esk_esp32_matrix_write(&m,map_address(0,41),4,0),ES_EUNSUPPORTED);
    CHECK_EQ(esk_esp32_matrix_read(&m,map_address(1,42),4,&v),ES_EUNSUPPORTED);
    CHECK_EQ(esk_esp32_matrix_read(&m,0x3ff0032c,4,&v),ES_EUNSUPPORTED);
    CHECK_EQ(esk_esp32_matrix_write(&m,0x3ff0002c,4,0),ES_EUNSUPPORTED);
    CHECK_EQ(esk_esp32_matrix_source(&m,22,1),ES_EINVAL);CHECK_EQ(esk_esp32_matrix_source(&m,24,1),ES_EINVAL);
    CHECK_EQ(esk_esp32_matrix_source(&m,41,1),ES_EINVAL);CHECK_EQ(esk_esp32_matrix_source(&m,69,1),ES_EINVAL);
    CHECK_EQ(esk_esp32_matrix_gpio(&m,2,0,1),ES_EINVAL);CHECK_EQ(esk_esp32_matrix_nmi_mask(&m,2,1),ES_EINVAL);
    CHECK_EQ(memcmp(&before,&m,sizeof m),0);
}
TEST_MAIN_BEGIN
    (void)t_str_eq;RUN(all_routes);RUN(sharing_and_permissions);
TEST_MAIN_END
