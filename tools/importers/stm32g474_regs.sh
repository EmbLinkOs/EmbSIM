#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
# STMicroelectronics STM32G474.svd v3.0 (the STM32G4 SVD pack, Apache-2.0) to
# catalog/mcus/st/stm32g474re/registers.json, with the corrections RM0440 and
# STM32CubeG4 (stm32g474xx.h, the HAL) give; run from the repository root
SVD=${1:?usage: stm32g474_regs.sh STM32G474.svd [registers.json]}
OUT=${2:-catalog/mcus/st/stm32g474re/registers.json}
python3 tools/importers/svd2regs.py "$SVD" -o "$OUT" --id st/stm32g474re/registers \
  --strip-prefixes \
  --drop "FDCAN=its address is the FDCAN message RAM's (SRAMCAN, RM0440 2.2.2); FDCAN1 to FDCAN3 keep its registers" \
  --rename TIM15.CCMR1_Output=CCMR1 \
  --field PWR.SR2=PVMO1:12:1:r --field PWR.SR2=PVMO2:13:1:r --field PWR.SR2=PVMO3:14:1:r --field PWR.SR2=PVMO4:15:1:r \
  --field QUADSPI.CR=FTHRES:8:4 \
  --field TIM2.CNT=CNT:0:32 \
  --field DBGMCU.IDCODE=DEV_ID:0:12:r \
  --field TIM1.CCMR1=OC1M_3:16:1 --field TIM1.CCMR1=OC2M_3:24:1 \
  --field TIM1.CCMR2=OC3M_3:16:1 --field TIM1.CCMR2=OC4M_3:24:1 \
  --field TIM2.CCMR1=OC1M_3:16:1 --field TIM2.CCMR1=OC2M_3:24:1 \
  --field TIM2.CCMR2=OC3M_3:16:1 --field TIM2.CCMR2=OC4M_3:24:1 \
  --field TIM3.CCMR1=OC1M_3:16:1 --field TIM3.CCMR1=OC2M_3:24:1 \
  --field TIM3.CCMR2=OC3M_3:16:1 --field TIM3.CCMR2=OC4M_3:24:1 \
  --field TIM16.CCMR1=OC1M_3:16:1 \
  --reset USART1.ISR=0x008000C0 --reset UART4.ISR=0x008000C0 --reset LPUART1.ISR=0x008000C0 \
  --reset FLASH.OPTR=0xFFEFF8AA \
  --reset RCC.CR=0x00000500 \
  --reset DBGMCU.IDCODE=0x20016469 \
  --reset EXTI.IMR1=0x1F840000 --reset EXTI.IMR2=0x00000C3C \
  --interrupts TAMP=RTC_TAMP_CSS_LSE --interrupts PWR=PVD_PVM \
  --interrupts EXTI=EXTI0,EXTI1,EXTI2,EXTI3,EXTI4,EXTI9_5,EXTI15_10 \
  --interrupts HRTIM_TIME=HRTIM_TIME_IRQn --interrupts HRTIM_TIMF=HRTIM_TIMF_IRQn \
  --interrupts TIM15=TIM1_BRK_TIM15 --interrupts TIM16=TIM1_UP_TIM16 --interrupts TIM17=TIM1_TRG_COM \
  --interrupts ADC2=ADC1_2 \
  --interrupts DAC1=TIM6_DACUNDER --interrupts DAC3=TIM6_DACUNDER --interrupts DAC2=TIM7 --interrupts DAC4=TIM7
# the SVD's version in the description's source, as ST's pack names it
python3 - "$OUT" <<'PY'
import json, sys
p = sys.argv[1]
d = json.load(open(p, encoding="utf-8"))
d["source"] = d["source"].replace("converted from STM32G474.svd by", "converted from STMicroelectronics STM32G474.svd v3.0 by", 1)
open(p, "w", encoding="utf-8").write(json.dumps(d, indent=1) + "\n")
PY
