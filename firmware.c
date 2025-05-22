/*
 * UART 6x64 LED Display Board Firmware
 *
 * SPDX-License-Identifier: CC-PDDC
 * Written by Calvin Owens <calvin@wbinvd.org>
 */

#include <stm32f0xx.h>

#define barrier() do { asm volatile ("" ::: "memory"); } while (0)

struct gpio {
	GPIO_TypeDef *reg;
	uint8_t nr;
};

/*
 * Output data (8.4.6 GPIOx_ODR).
 */

static inline void set_gpio(const struct gpio *gpio, int new)
{
	if (new)
		gpio->reg->ODR |= 1UL << gpio->nr;
	else
		gpio->reg->ODR &= ~(1UL << gpio->nr);
}

/*
 * Ouptut mode (8.4.1 GPIOx_MODER == 1)
 */

static void configure_gpio_pp(const struct gpio *gpio, int init)
{
	set_gpio(gpio, init);
	gpio->reg->MODER |= 1UL << (gpio->nr * 2);
}

static void configure_gpio_od(const struct gpio *gpio, int init)
{
	set_gpio(gpio, init);
	gpio->reg->MODER |= 1UL << (gpio->nr * 2);
	gpio->reg->OTYPER |= 1UL << gpio->nr;
}

/*
 * For enabling SPI/UART peripherals (for alternate function values, see the
 * STM32F030x8 datasheet, tables 12/13).
 */

static void configure_gpio_af(const struct gpio *gpio, uint8_t af)
{
	set_gpio(gpio, 0);
	af &= 0xFUL;

	if (gpio->nr < 8)
		gpio->reg->AFR[0] |= af << (gpio->nr * 4);
	else
		gpio->reg->AFR[1] |= af << ((gpio->nr - 8) * 4);

	gpio->reg->MODER |= 2UL << (gpio->nr * 2);
}

static void configure_uart(void)
{
	/*
	 * Normally, USART1 uses DMA channels 2/3, so RX would overlap with
	 * SPI1 TX. These flags to remap USART1 to channels 4/5 (RM0360 9.1.1).
	 */

	SYSCFG->CFGR1 |= SYSCFG_CFGR1_USART1TX_DMA_RMP;
	SYSCFG->CFGR1 |= SYSCFG_CFGR1_USART1RX_DMA_RMP;

	/*
	 * Use the system clock as the UART clock source (RM0360 7.4.13), and
	 * configure USART1 for DMA RX/TX at 38.4 kbaud (RM0360 23.4.4).
	 */

	USART1->CR1 &= ~USART_CR1_UE;
	RCC->CFGR3 |= RCC_CFGR3_USART1SW_0;
	USART1->BRR = SYSTEM_CLOCK_FREQUENCY / 38400;
	USART1->CR3 |= USART_CR3_DMAR;
	USART1->CR1 |= USART_CR1_UE | USART_CR1_RE | USART_CR1_TE;

	NVIC_SetPriority(DMA1_Channel4_5_IRQn, 2);
	NVIC_EnableIRQ(DMA1_Channel4_5_IRQn);
}

static void start_uart_rx_dma(void *dst, int len)
{
	DMA1_Channel5->CCR &= ~DMA_CCR_EN;
	DMA1_Channel5->CPAR = (uint32_t)&USART1->RDR;
	DMA1_Channel5->CMAR = (uint32_t)dst;
	DMA1_Channel5->CNDTR = len;
	DMA1_Channel5->CCR |= DMA_CCR_MINC | DMA_CCR_TCIE;
	DMA1_Channel5->CCR |= DMA_CCR_EN;
}

static void configure_spi(void)
{
	/*
	 * Configure SPI1 for 8-bit data at 1.5MHz (PCLK/2, see RM0360 24.4.7).
	 *
	 * The shift registers interpret the serial stream as MSB first. We use
	 * LSBFIRST mode to effectively invert the order of the bits in each
	 * byte, and complete the "left-to-right to right-to-left"
	 * transformation to match the hardware (see ledstate_fixup_row below).
	 */

	SPI1->CR1 |= SPI_CR1_MSTR | SPI_CR1_SSI | SPI_CR1_SSM | SPI_CR1_BIDIOE |
		     SPI_CR1_BIDIMODE | SPI_CR1_LSBFIRST;
	SPI1->CR2 |= 0x0700 | SPI_CR2_TXDMAEN;
	SPI1->CR1 |= SPI_CR1_SPE;

	NVIC_SetPriority(DMA1_Channel2_3_IRQn, 1);
	NVIC_EnableIRQ(DMA1_Channel2_3_IRQn);
}

static void start_spi_tx_dma(void *src, int len)
{
	DMA1_Channel3->CCR &= ~DMA_CCR_EN;
	DMA1_Channel3->CPAR = (uint32_t)&SPI1->DR;
	DMA1_Channel3->CMAR = (uint32_t)src;
	DMA1_Channel3->CNDTR = len;
	DMA1_Channel3->CCR |= DMA_CCR_MINC | DMA_CCR_DIR | DMA_CCR_TCIE;
	DMA1_Channel3->CCR |= DMA_CCR_EN;
}

static int spi_txlvl(void)
{
	/*
	 * True if the SPI1 TXFIFO is not empty.
	 */
	return (SPI1->SR & SPI_SR_FTLVL) >> 11;
}

static int spi_busy(void)
{
	/*
	 * True if SPI1 is actively transmitting.
	 */
	return SPI1->SR & SPI_SR_BSY;
}

struct ledrow {
	union {
		uint8_t cols[NR_COLS / 8];
		uint32_t u32[NR_COLS / 8 / 4];
	};
};

static void ledstate_fixup_row(struct ledrow *r)
{
	uint32_t tmp;

	/*
	 * The UART client gives us the rows [0,64) left-to-right. Within each
	 * byte, the bits represent pixels {0,1,2,3,4,5,6,7} where pixel zero is
	 * the left-most pixel, indexed LSB-first.
	 *
	 * Because the shift registers run left-to-right, we must clock out the
	 * new bits right-to-left. Here, we invert the byte order of the buffer.
	 * Later at TX time, the SPI peripheral will complete the transformation
	 * by inverting the bit order in each byte (see configure_spi() above).
	 */

	tmp = __builtin_bswap32(r->u32[0]);
	r->u32[0] = __builtin_bswap32(r->u32[1]);
	r->u32[1] = tmp;
}

static const struct gpio gpio_ledpwr_en = { .reg = GPIOA, .nr = 12, };
static const struct gpio gpio_regs_lat = { .reg = GPIOA, .nr = 6, };
static const struct gpio gpio_row_fet[NR_ROWS] = {
	{ .reg = GPIOA, .nr = 11, },
	{ .reg = GPIOA, .nr = 10, },
	{ .reg = GPIOA, .nr = 9, },
	{ .reg = GPIOA, .nr = 8, },
	{ .reg = GPIOB, .nr = 15, },
	{ .reg = GPIOB, .nr = 14, },
};

static const struct gpio gpio_regs_clk = { .reg = GPIOA, .nr = 5, };
static const struct gpio gpio_regs_ser = { .reg = GPIOA, .nr = 7, };
static const struct gpio gpio_uart_tx = { .reg = GPIOB, .nr = 6, };
static const struct gpio gpio_uart_rx = { .reg = GPIOB, .nr = 7, };

/*
 * This is a simple "flip buffer" implementation: the system tick timer triggers
 * SPI1 TX DMA to loop over the rows in the active buffer, and the USART1 RX DMA
 * recieves new ledstate into the inactive buffer. When the RX DMA is complete,
 * the buffers are swapped, and the process continues.
 */

struct ledstate {
	struct ledrow rows[NR_ROWS];
};

static struct ledstate s[2];
static volatile int tx_off;
static volatile int tx;

static volatile uint32_t overruns;
static volatile int spi_active;
static volatile int rx_done;

void irq_dma1_ch45(void)
{
	/*
	 * This IRQ means UART RX DMA has finished receiving a new ledstate:
	 * trigger the fixup in the main loop, which will flip the buffers.
	 */

	DMA1->IFCR |= DMA_IFCR_CGIF5;
	rx_done = 1;
}

void irq_dma1_ch23(void)
{
	/*
	 * This IRQ means SPI TX DMA has finished transmitting a row. But, the
	 * SPI TXFIFO still has 2-3 bytes left to transmit: we need to wait for
	 * it to finish before latching.
	 */

	DMA1->IFCR |= DMA_IFCR_CGIF3;
	while (spi_txlvl() || spi_busy())
		barrier();

	spi_active = 0;
}

void __systick(void)
{
	/*
	 * This IRQ drives the row refreshes.
	 */

	while (spi_active)
		overruns++;

	set_gpio(&gpio_row_fet[tx_off ? tx_off - 1 : NR_ROWS - 1], 1);
	asm volatile ("nop;nop;nop;nop;nop;nop;nop;nop" ::: "memory");
	asm volatile ("nop;nop;nop;nop;nop;nop;nop;nop" ::: "memory");
	asm volatile ("nop;nop;nop;nop;nop;nop;nop;nop" ::: "memory");
	asm volatile ("nop;nop;nop;nop;nop;nop;nop;nop" ::: "memory");
	asm volatile ("nop;nop;nop;nop;nop;nop;nop;nop" ::: "memory");
	asm volatile ("nop;nop;nop;nop;nop;nop;nop;nop" ::: "memory");
	asm volatile ("nop;nop;nop;nop;nop;nop;nop;nop" ::: "memory");
	asm volatile ("nop;nop;nop;nop;nop;nop;nop;nop" ::: "memory");
	set_gpio(&gpio_regs_lat, 1);
	set_gpio(&gpio_regs_lat, 0);
	set_gpio(&gpio_row_fet[tx_off], 0);

	if (++tx_off == NR_ROWS)
		tx_off = 0;

	spi_active = 1;
	start_spi_tx_dma(&s[tx].rows[tx_off].cols, NR_COLS / 8);
}

static void init_clocks(void)
{
	/*
	 * Reset everything.
	 */

	RCC->CR &= ~(RCC_CR_HSEON | RCC_CR_CSSON | RCC_CR_PLLON |
		     RCC_CR_HSEBYP);

	RCC->CR2 &= ~RCC_CR2_HSI14ON;

	RCC->CFGR &= ~(RCC_CFGR_SW | RCC_CFGR_HPRE | RCC_CFGR_PPRE |
		       RCC_CFGR_ADCPRE | RCC_CFGR_MCO | RCC_CFGR_PLLSRC |
		       RCC_CFGR_PLLXTPRE | RCC_CFGR_PLLMUL);

	RCC->CFGR2 &= ~RCC_CFGR2_PREDIV1;

	RCC->CFGR3 &= ~(RCC_CFGR3_USART1SW | RCC_CFGR3_I2C1SW |
			RCC_CFGR3_CECSW | RCC_CFGR3_ADCSW);

	/*
	 * No external flash.
	 */

	FLASH->ACR = FLASH_ACR_PRFTBE;
	RCC->CIR = 0;

	/*
	 * Make a 24MHz clock, assuming a 12MHz HSE cryctal. This works out
	 * nicely because 24M is multiple of both 6 and 38400. HCLK is
	 * undivided, PCLK is HCLK/8 (3MHz @24MHz).
	 */

	RCC->CR |= RCC_CR_HSEON;
	while (!(RCC->CR & RCC_CR_HSERDY))
		barrier();

	RCC->CFGR |= RCC_CFGR_PLLSRC_HSE_PREDIV | RCC_CFGR_PLLMUL2 |
		     RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE_DIV8;

	RCC->CR |= RCC_CR_PLLON;
	while (!(RCC->CR & RCC_CR_PLLRDY))
		barrier();

	RCC->CFGR &= ~RCC_CFGR_SW;
	RCC->CFGR |= RCC_CFGR_SW_PLL;
	while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL)
		barrier();

	RCC->CR &= ~RCC_CR_HSION;
}

int main(void)
{
	int i;

	/*
	 * Enable the AHB/APB peripheral clocks and setup GPIOs.
	 */

	init_clocks();

	RCC->AHBENR |= RCC_AHBENR_GPIOAEN | RCC_AHBENR_GPIOBEN |
		       RCC_AHBENR_DMAEN;

	RCC->APB2ENR |= RCC_APB2ENR_SPI1EN | RCC_APB2ENR_USART1EN |
			RCC_APB2ENR_SYSCFGEN;

	configure_gpio_pp(&gpio_ledpwr_en, 0);
	configure_gpio_pp(&gpio_regs_lat, 0);
	configure_gpio_af(&gpio_regs_ser, 0);
	configure_gpio_af(&gpio_regs_clk, 0);
	configure_gpio_af(&gpio_uart_tx, 0);
	configure_gpio_af(&gpio_uart_rx, 0);

	for (i = 0; i < NR_ROWS; i++)
		configure_gpio_od(gpio_row_fet + i, 1);

	/*
	 * Start the system tick, which drives the SPI TX DMA loop over the rows
	 * in the active buffer.
	 */

	configure_spi();
	SysTick_Config(SYSTEM_CLOCK_FREQUENCY / (SCREEN_REFRESH_HZ * NR_ROWS));
	while (tx_off == -1)
		barrier();

	/*
	 * Now that the SPI TX DMA loop is running, enable power to the LEDs.
	 */

	set_gpio(&gpio_ledpwr_en, 1);

	/*
	 * Setup and start UART RX DMA to the inactive buffer.
	 */

	configure_uart();
	start_uart_rx_dma(&s[!tx], NR_ROWS * NR_COLS / 8);

	while (1) {
		asm volatile ("wfi" ::: "cc", "memory");

		/*
		 * The fixup code would limit the refresh rate if it had to run
		 * in a single scan interval, so it is "deferred" here.
		 */

		if (rx_done) {
			rx_done = 0;

			for (i = 0; i < NR_ROWS; i++)
				ledstate_fixup_row(&s[!tx].rows[i]);

			while (spi_active)
				barrier();

			tx = !tx;
			start_uart_rx_dma(&s[!tx], NR_ROWS * NR_COLS / 8);
		}
	}
}
