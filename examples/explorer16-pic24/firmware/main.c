/* SPDX-License-Identifier: Apache-2.0 */
/* main.c -- the Explorer 16/32 with its PIC24FJ1024GB610 PIM: the 8 MHz
 * crystal through the 96 MHz PLL to a 32 MHz FOSC (16 MHz FCY, the
 * configuration words below), Timer1's interrupt every 100 ms blinking D3
 * at 1 Hz, S3 (RD6) counted through the interrupt-on-change and shown on
 * D4 to D10, and UART2 at 115200 baud on the USB-UART bridge (put on RF5
 * and RF4 by the Peripheral Pin Select) echoing what it receives in upper
 * case. Between interrupts the core idles (PWRSAV). */
#include <xc.h>
#include <stdint.h>

#pragma config FNOSC = PRIPLL, PLLMODE = PLL96DIV2, IESO = OFF
#pragma config POSCMD = HS, OSCIOFCN = OFF, IOL1WAY = OFF, FCKSM = CSECMD
#pragma config FWDTEN = OFF, ICS = PGD2, JTAGEN = OFF

static volatile uint16_t ticks;
static volatile uint16_t presses;

static void putc2(char c)
{
    while (U2STAbits.UTXBF)
        ;
    U2TXREG = (uint8_t)c;
}

static void puts2(const char *s)
{
    while (*s)
        putc2(*s++);
}

static void putu(uint32_t v)
{
    char b[12];
    int n = 0;
    do {
        b[n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v);
    while (n)
        putc2(b[--n]);
}

static void puthex(uint16_t v)
{
    int i;
    puts2("0x");
    for (i = 12; i >= 0; i -= 4)
        putc2("0123456789abcdef"[(v >> i) & 15u]);
}

/* FOSC from the oscillator's registers: the 96 MHz PLL divided to 32 MHz
 * by CPDIV, or the 8 MHz primary oscillator or FRC */
static uint32_t fosc(void)
{
    uint16_t cosc = OSCCONbits.COSC;
    if (cosc == 1 || cosc == 3)
        return 32000000ul >> CLKDIVbits.CPDIV;
    return 8000000ul;
}

/* a configuration word, by a table read */
static uint16_t config_word(uint32_t addr)
{
    uint16_t v;
    TBLPAG = (uint16_t)(addr >> 16);
    v = __builtin_tblrdl((uint16_t)addr);
    return v;
}

static void show_presses(void)
{
    /* D4 to D10 (RA1-RA7): the count; D3 (RA0) is the blink's */
    LATA = (LATA & ~0xfeu) | ((presses << 1) & 0xfeu);
}

void __attribute__((interrupt, no_auto_psv)) _T1Interrupt(void)
{
    IFS0bits.T1IF = 0;
    if (++ticks % 5u == 0)
        LATAbits.LATA0 ^= 1; /* D3 */
}

void __attribute__((interrupt, no_auto_psv)) _IOCInterrupt(void)
{
    uint16_t d = PORTD;
    IOCFD = 0;
    IFS1bits.IOCIF = 0;
    if (!(d & (1u << 6))) {
        presses++;
        show_presses();
        puts2("S3 pressed ");
        putu(presses);
        puts2("\r\n");
    }
}

void __attribute__((interrupt, no_auto_psv)) _U2RXInterrupt(void)
{
    while (U2STAbits.URXDA) {
        char c = (char)U2RXREG;
        putc2(c >= 'a' && c <= 'z' ? (char)(c - 32) : c);
    }
    IFS1bits.U2RXIF = 0;
}

int main(void)
{
    uint32_t hz = fosc();
    ANSA = 0;                    /* RA6, RA7 digital */
    TRISA = 0xff00u;             /* RA0-RA7 out: D3 to D10 */
    LATA = 0;
    /* UART2 on the USB-UART bridge: U2TX to RP17 (RF5), U2RX from RP10
     * (RF4) */
    __builtin_write_OSCCONL(OSCCON & ~0x40);
    RPOR8bits.RP17R = 5;         /* U2TX */
    RPINR19bits.U2RXR = 10;
    __builtin_write_OSCCONL(OSCCON | 0x40);
    /* 115200 baud at FCY 16 MHz, BRGH (4 clocks a bit): 16 MHz / (4 x 35) */
    U2BRG = 34;
    U2MODE = (1u << 15) | (1u << 3);
    U2STA = (1u << 12) | (1u << 10); /* URXEN, UTXEN */
    puts2("explorer16: PIC24FJ1024GB610, FOSC ");
    putu(hz / 1000u);
    puts2(" kHz, FCY ");
    putu(hz / 2000u);
    puts2(" kHz, FOSCSEL ");
    puthex(config_word(0xabf18ul));
    puts2("\r\n");
    /* Timer1: FCY / 256 = 62500 Hz, 6250 counts: 100 ms */
    T1CON = 3u << 4;
    TMR1 = 0;
    PR1 = 6250u - 1u;
    IPC0bits.T1IP = 3;
    IFS0bits.T1IF = 0;
    IEC0bits.T1IE = 1;
    T1CONbits.TON = 1;
    /* S3 on RD6: the interrupt-on-change, on its falling edge */
    TRISDbits.TRISD6 = 1;
    ANSDbits.ANSD6 = 0;
    PADCONbits.IOCON = 1;
    IOCNDbits.IOCND6 = 1;
    IOCFD = 0;
    IPC4bits.IOCIP = 2;
    IFS1bits.IOCIF = 0;
    IEC1bits.IOCIE = 1;
    /* UART2's receiver */
    IPC7bits.U2RXIP = 4;
    IFS1bits.U2RXIF = 0;
    IEC1bits.U2RXIE = 1;
    for (;;)
        Idle();
}
