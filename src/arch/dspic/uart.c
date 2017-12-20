#include "uart.h"
#include "buffer_utils.h"
#include "p33FJ128MC802.h"
#include <string.h>

void setBaud57600(void) {
#define BAUD 57600
#ifdef USE_2X
#define FREQ_SCALE 4
#else
#define FREQ_SCALE 16
#endif
#ifndef FCY
#define FCY 4000000
#endif

    U2BRG = (FCY / (FREQ_SCALE * BAUD)) - 1;
#undef BAUD
#undef FCY
#undef FREQ_SCALE
}

void setBaud115200(void) {
#define BAUD 115200
#ifdef USE_2X
#define FREQ_SCALE 4
#else
#define FREQ_SCALE 16
#endif
#ifndef FCY
#define FCY 4000000
#endif

    U2BRG = (FCY / (FREQ_SCALE * BAUD)) - 1;
#undef BAUD
#undef FCY
#undef FREQ_SCALE
}

#define UART_BUFFER_SIZE 256

typedef struct myUART {
    int tx_buffer[UART_BUFFER_SIZE];
    volatile uint8_t tx_start;
    volatile uint8_t tx_end;
    volatile uint8_t tx_size;

    int rx_buffer[UART_BUFFER_SIZE];
    volatile uint8_t rx_start;
    volatile uint8_t rx_end;
    volatile uint8_t rx_size;

    int baud;
    int uart_num; // hardware uart;
} myUART;

static myUART uart_0;

struct myUART* UART_init(const char* device __attribute__((unused)), uint32_t baud) {
    myUART* uart = &uart_0;
    uart->uart_num = 0;

    switch (baud) {
        case 57600: setBaud57600();
            break;
        case 115200: setBaud115200();
            break;
        default: return 0;
    }

    uart->tx_start = 0;
    uart->tx_end = 0;
    uart->tx_size = 0;
    uart->rx_start = 0;
    uart->rx_end = 0;
    uart->rx_size = 0;

    // configure uart
    //UCSR0C = _BV(UCSZ01) | _BV(UCSZ00); /* 8-bit data */
    //UCSR0B = _BV(RXEN0) | _BV(TXEN0) | _BV(RXCIE0);   /* Enable RX and TX */
    //sei();

    // DanCode

    //U1MODE = 1<<UARTEN;
    //U1MODE = 0<<PDSEL1 | 1<<PDSEL0;
    //U1MODE = 0<<STSEL;
    //U1STA = 1<<UT1EN // transmit enabled: maybe it must be used in ISR
    //U1MODE = 00<<UEN;

    U1MODEbits.UARTEN = 1; // enables RX and TX
    U1MODEbits.PDSEL1 = 0; // mode 01: 8-bit data, even parity
    U1MODEbits.PDSEL0 = 1; // ^
    U1MODEbits.STSEL = 0; // 1 stop bit

    U1MODEbits.UEN = 0; // U1TX and U1RX enabled -- is it required?

    return &uart_0;
}

// returns the free space in the buffer

int UART_rxbufferSize(struct myUART* uart __attribute__((unused))) {
    return UART_BUFFER_SIZE;
}

// returns the free occupied space in the buffer

int UART_txBufferSize(struct myUART* uart __attribute__((unused))) {
    return UART_BUFFER_SIZE;
}

// number of chars in rx buffer

int UART_rxBufferFull(myUART* uart) {
    return uart->rx_size;
}

// number of chars in rx buffer

int UART_txBufferFull(myUART* uart) {
    return uart->tx_size;
}

// number of free chars in tx buffer

int UART_txBufferFree(myUART* uart) {
    return UART_BUFFER_SIZE - uart->tx_size;
}

void UART_putChar(struct myUART* uart, uint8_t c) {
    // loops until there is some space in the buffer
    while (uart->tx_size >= UART_BUFFER_SIZE);
    //ATOMIC_BLOCK(ATOMIC_FORCEON){
    uart->tx_buffer[uart->tx_end] = c;
    BUFFER_PUT(uart->tx, UART_BUFFER_SIZE);
    //}
    //UCSR0B |= _BV(UDRIE0); // enable transmit interrupt
}

uint8_t UART_getChar(struct myUART* uart) {
    while (uart->rx_size == 0);
    uint8_t c;
    //  ATOMIC_BLOCK(ATOMIC_FORCEON){
    c = uart->rx_buffer[uart->rx_start];
    BUFFER_GET(uart->rx, UART_BUFFER_SIZE);
    //}
    return c;
}

// in ISR?
// IFS0 = 1<<U1TXIF // enables tx interrupt
// IFS0 = 1<<U1RXIF  // enables rx interrupt

/*
ISR(USART0_RX_vect) {
  uint8_t c=0;//UDR0;
  if (uart_0.rx_size<UART_BUFFER_SIZE){
    uart_0.rx_buffer[uart_0.rx_end] = c;
    //BUFFER_PUT(uart_0.rx, UART_BUFFER_SIZE);
  }
}

ISR(USART0_UDRE_vect){
  if (! uart_0.tx_size){
    //UCSR0B &= ~_BV(UDRIE0);
  } else {
    //UDR0 = uart_0.tx_buffer[uart_0.tx_start];
    BUFFER_GET(uart_0.tx, UART_BUFFER_SIZE);
  }
}
 */
