/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* stm32f103.h -- the STM32F103's peripheral registers for this firmware,
 * laid out as RM0008 Rev 21 documents them (written from the manual, in the
 * CMSIS naming firmware expects: RCC->CR, USART1->SR, ...). Only what the
 * tests use; not a vendor header. */
#ifndef TEST_FW_STM32F103_H
#define TEST_FW_STM32F103_H
#include <stdint.h>

#define IO volatile uint32_t

typedef struct { IO CR, CFGR, CIR, APB2RSTR, APB1RSTR, AHBENR, APB2ENR, APB1ENR, BDCR, CSR; } RCC_TypeDef;
typedef struct { IO CRL, CRH, IDR, ODR, BSRR, BRR, LCKR; } GPIO_TypeDef;
typedef struct { IO EVCR, MAPR, EXTICR[4]; uint32_t RESERVED0; IO MAPR2; } AFIO_TypeDef;
typedef struct { IO IMR, EMR, RTSR, FTSR, SWIER, PR; } EXTI_TypeDef;
typedef struct { IO SR, DR, BRR, CR1, CR2, CR3, GTPR; } USART_TypeDef;
typedef struct {
    IO CR1, CR2, SMCR, DIER, SR, EGR, CCMR1, CCMR2, CCER, CNT, PSC, ARR, RCR, CCR1, CCR2, CCR3, CCR4,
        BDTR, DCR, DMAR;
} TIM_TypeDef;
typedef struct { IO ACR, KEYR, OPTKEYR, SR, CR, AR; uint32_t RESERVED; IO OBR, WRPR; } FLASH_TypeDef;

#define TIM2 ((TIM_TypeDef *)0x40000000u)
#define TIM3 ((TIM_TypeDef *)0x40000400u)
#define TIM4 ((TIM_TypeDef *)0x40000800u)
#define USART2 ((USART_TypeDef *)0x40004400u)
#define USART3 ((USART_TypeDef *)0x40004800u)
#define AFIO ((AFIO_TypeDef *)0x40010000u)
#define EXTI ((EXTI_TypeDef *)0x40010400u)
#define GPIOA ((GPIO_TypeDef *)0x40010800u)
#define GPIOB ((GPIO_TypeDef *)0x40010C00u)
#define GPIOC ((GPIO_TypeDef *)0x40011000u)
#define GPIOD ((GPIO_TypeDef *)0x40011400u)
#define TIM1 ((TIM_TypeDef *)0x40012C00u)
#define USART1 ((USART_TypeDef *)0x40013800u)
#define RCC ((RCC_TypeDef *)0x40021000u)
#define FLASH ((FLASH_TypeDef *)0x40022000u)

/* interrupt numbers (RM0008 Table 63) */
enum {
    EXTI0_IRQn = 6, EXTI1_IRQn = 7, EXTI2_IRQn = 8, EXTI3_IRQn = 9, EXTI4_IRQn = 10,
    EXTI9_5_IRQn = 23, TIM1_UP_IRQn = 25, TIM2_IRQn = 28, TIM3_IRQn = 29, TIM4_IRQn = 30,
    USART1_IRQn = 37, USART2_IRQn = 38, USART3_IRQn = 39, EXTI15_10_IRQn = 40,
};

/* the core's registers (PM0056) */
#define NVIC_ISER(n) (*(volatile uint32_t *)(0xE000E100u + 4u * (n)))
#define NVIC_ICER(n) (*(volatile uint32_t *)(0xE000E180u + 4u * (n)))
#define NVIC_ICPR(n) (*(volatile uint32_t *)(0xE000E280u + 4u * (n)))
#define SysTick_CSR (*(volatile uint32_t *)0xE000E010u)
#define SysTick_RVR (*(volatile uint32_t *)0xE000E014u)
#define SysTick_CVR (*(volatile uint32_t *)0xE000E018u)

static inline void nvic_enable(int irq) { NVIC_ISER(irq >> 5) = 1u << (irq & 31); }

#endif
