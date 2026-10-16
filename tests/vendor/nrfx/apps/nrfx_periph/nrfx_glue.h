/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* nrfx_glue.h -- nrfx's glue to a bare Cortex-M4 with CMSIS, as an nrfx
 * integration provides it: the NVIC for the interrupt macros, PRIMASK for
 * the critical sections, GCC's builtins for the atomics and bit scans,
 * and nrfx's own busy-wait for the delays. */
#ifndef NRFX_GLUE_H__
#define NRFX_GLUE_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NRFX_ASSERT(expression) ((void)0)
#define NRFX_STATIC_ASSERT(expression) _Static_assert((expression), "nrfx")

#define NRFX_IRQ_PRIORITY_SET(irq_number, priority) NVIC_SetPriority((IRQn_Type)(irq_number), (priority))
#define NRFX_IRQ_ENABLE(irq_number) NVIC_EnableIRQ((IRQn_Type)(irq_number))
#define NRFX_IRQ_IS_ENABLED(irq_number) (NVIC_GetEnableIRQ((IRQn_Type)(irq_number)) != 0)
#define NRFX_IRQ_DISABLE(irq_number) NVIC_DisableIRQ((IRQn_Type)(irq_number))
#define NRFX_IRQ_PENDING_SET(irq_number) NVIC_SetPendingIRQ((IRQn_Type)(irq_number))
#define NRFX_IRQ_PENDING_CLEAR(irq_number) NVIC_ClearPendingIRQ((IRQn_Type)(irq_number))
#define NRFX_IRQ_IS_PENDING(irq_number) (NVIC_GetPendingIRQ((IRQn_Type)(irq_number)) != 0)

#define NRFX_CRITICAL_SECTION_ENTER() \
    {                                 \
        uint32_t nrfx_primask = __get_PRIMASK(); \
        __disable_irq();
#define NRFX_CRITICAL_SECTION_EXIT()  \
        __set_PRIMASK(nrfx_primask);  \
    }

#define NRFX_COREDEP_DELAY_DWT_BASED 0
#include <lib/nrfx_coredep.h>
#define NRFX_DELAY_US(us_time) nrfx_coredep_delay_us(us_time)

#define nrfx_atomic_t uint32_t
#define NRFX_ATOMIC_FETCH_STORE(p_data, value) __atomic_exchange_n((p_data), (value), __ATOMIC_SEQ_CST)
#define NRFX_ATOMIC_FETCH_OR(p_data, value) __atomic_fetch_or((p_data), (value), __ATOMIC_SEQ_CST)
#define NRFX_ATOMIC_FETCH_AND(p_data, value) __atomic_fetch_and((p_data), (value), __ATOMIC_SEQ_CST)
#define NRFX_ATOMIC_FETCH_XOR(p_data, value) __atomic_fetch_xor((p_data), (value), __ATOMIC_SEQ_CST)
#define NRFX_ATOMIC_FETCH_ADD(p_data, value) __atomic_fetch_add((p_data), (value), __ATOMIC_SEQ_CST)
#define NRFX_ATOMIC_FETCH_SUB(p_data, value) __atomic_fetch_sub((p_data), (value), __ATOMIC_SEQ_CST)
#define NRFX_ATOMIC_CAS(p_data, old_value, new_value)                                              \
    ({                                                                                             \
        uint32_t nrfx_old = (old_value);                                                           \
        __atomic_compare_exchange_n((p_data), &nrfx_old, (new_value), 0, __ATOMIC_SEQ_CST,         \
                                    __ATOMIC_SEQ_CST);                                             \
    })
#define NRFX_CLZ(value) __builtin_clz(value)
#define NRFX_CTZ(value) __builtin_ctz(value)

#define NRFX_EVENT_READBACK_ENABLED 1

#define NRFY_CACHE_WB(p_buffer, size)
#define NRFY_CACHE_INV(p_buffer, size)
#define NRFY_CACHE_WBINV(p_buffer, size)

#define NRFX_DPPI_CHANNELS_USED 0
#define NRFX_DPPI_GROUPS_USED 0
#define NRFX_PPI_CHANNELS_USED 0
#define NRFX_PPI_GROUPS_USED 0
#define NRFX_GPIOTE_CHANNELS_USED 0
#define NRFX_EGUS_USED 0
#define NRFX_TIMERS_USED 0

#ifdef __cplusplus
}
#endif

#endif
