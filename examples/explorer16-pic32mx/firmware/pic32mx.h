/* SPDX-License-Identifier: Apache-2.0 */
/* pic32mx.h -- the PIC32MX795F512L registers this example uses, at their
 * kseg1 (uncached) addresses, each with its CLR, SET and INV registers at
 * +4, +8 and +0xC; and the interrupt numbers and vectors, as the device
 * pack has them (catalog/mcus/microchip/pic32mx795f512l/registers.json). */
#ifndef PIC32MX_H
#define PIC32MX_H

#include <stdint.h>

#define SFR(a) (*(volatile uint32_t *)(0xA0000000u | (a)))
#define CLR(r) (*(&(r) + 1))
#define SET(r) (*(&(r) + 2))
#define INV(r) (*(&(r) + 3))

/* timer 1 */
#define T1CON SFR(0x1F800600u)
#define TMR1 SFR(0x1F800610u)
#define PR1 SFR(0x1F800620u)
/* UART2 */
#define U2MODE SFR(0x1F806800u)
#define U2STA SFR(0x1F806810u)
#define U2TXREG SFR(0x1F806820u)
#define U2RXREG SFR(0x1F806830u)
#define U2BRG SFR(0x1F806840u)
/* the oscillator, the system key, the device ID */
#define OSCCON SFR(0x1F80F000u)
#define DDPCON SFR(0x1F80F200u)
#define DEVID SFR(0x1F80F220u)
#define SYSKEY SFR(0x1F80F230u)
/* the interrupt controller */
#define INTCON SFR(0x1F881000u)
#define INTSTAT SFR(0x1F881010u)
#define IFS0 SFR(0x1F881030u)
#define IFS1 SFR(0x1F881040u)
#define IEC0 SFR(0x1F881060u)
#define IEC1 SFR(0x1F881070u)
#define IPC1 SFR(0x1F8810A0u)
#define IPC6 SFR(0x1F8810F0u)
#define IPC8 SFR(0x1F881110u)
/* the prefetch cache */
#define CHECON SFR(0x1F884000u)
/* the ports */
#define TRISA SFR(0x1F886000u)
#define PORTA SFR(0x1F886010u)
#define LATA SFR(0x1F886020u)
#define TRISD SFR(0x1F8860C0u)
#define PORTD SFR(0x1F8860D0u)
#define CNCON SFR(0x1F8861C0u)
#define CNEN SFR(0x1F8861D0u)
#define CNPUE SFR(0x1F8861E0u)

/* the configuration words (DEVCFG2's PLL input divider is read back for
 * the clock's report) */
#define DEVCFG2 (*(volatile const uint32_t *)0xBFC02FF4u)

/* interrupt requests (their IFS/IEC bit) and the vectors they share */
#define IRQ_T1 4         /* IFS0.T1IF, vector 4 */
#define IRQ_CN 32        /* IFS1.CNIF, vector 26 */
#define IRQ_U2E 40       /* IFS1.U2EIF, vector 32 */
#define IRQ_U2RX 41      /* IFS1.U2RXIF, vector 32 */
#define IRQ_U2TX 42      /* IFS1.U2TXIF, vector 32 */
#define VEC_T1 4
#define VEC_CN 26
#define VEC_UART2 32

#define BIT(irq) (1u << ((irq) % 32))

#endif
