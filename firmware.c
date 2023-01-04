/*
 * UART 6x64 LED Display Board Firmware
 *
 * Written by Calvin Owens <jcalvinowens@gmail.com>
 *
 * To the extent possible under law, I waive all copyright and related or
 * neighboring rights. You should have received a copy of the CC0 license along
 * with this work. If not, see http://creativecommons.org/publicdomain/zero/1.0
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <stm32f0xx.h>

#define SYSTEM_CLOCK_FREQUENCY (24000000)
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
 * For enabling SPI/UART peripherals (see STM32F030x8 datasheet, tables 12/13).
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

#if 0
static void start_uart_tx_dma(void *src, int len)
{
	DMA1_Channel2->CCR &= ~DMA_CCR_EN;
	DMA1_Channel2->CPAR = (uint32_t)&USART1->TDR;
	DMA1_Channel2->CMAR = (uint32_t)src;
	DMA1_Channel2->CNDTR = len;
	DMA1_Channel2->CCR |= DMA_CCR_MINC | DMA_CCR_TCIE | DMA_CCR_DIR;
	DMA1_Channel2->CCR |= DMA_CCR_EN;
}
#endif

static void configure_spi(void)
{
	/*
	 * Configure SPI1 for 8-bit data at 1.5MHz (PCLK/2, see RM0360 24.4.7).
	 *
	 * We use LSBFIRST mode to invert the order of the bits in each byte
	 * and complete the "left-to-right to right-to-left" transformation
	 * to match the hardware (see fixup_ledstate() below).
	 */

	SPI1->CR1 |= SPI_CR1_MSTR | SPI_CR1_SSI | SPI_CR1_SSM | SPI_CR1_BIDIOE |
		     SPI_CR1_BIDIMODE | SPI_CR1_LSBFIRST;
	SPI1->CR2 |= 0x0700 | SPI_CR2_TXDMAEN;
	SPI1->CR1 |= SPI_CR1_SPE;

	NVIC_SetPriority(DMA1_Channel2_3_IRQn, 0);
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

#define nr_rows (6)
#define nr_cols (64)

struct ledrow {
	union {
		uint8_t cols[nr_cols / 8];
		uint32_t u32[nr_cols / 8 / 4];
	};
};

struct ledstate {
	struct ledrow rows[nr_rows];
};

static uint32_t flip_nibbles(uint32_t v)
{
	#ifdef HW_REV2

	/*
	 * From revision 2.0 onwards, the columns are strictly backwards, so
	 * only the bit order needs to reversed (which happens at TX time).
	 */

	return v;

	#else

	/*
	 * Rotate each byte by four ({0,1,2,3,4,5,6,7} -> {4,5,6,7,0,1,2,3}) to
	 * match the order columns are wired to the shift registers.
	 */

	return ((v & 0xF0F0F0F0) >> 4) | ((v & 0x0F0F0F0F) << 4);

	#endif
}

static void ledstate_fixup_row(struct ledrow *r)
{
	uint32_t tmp;

	/*
	 * The UART client gives us the rows [0,64) left-to-right. Within each
	 * byte, the bits represent pixels {0,1,2,3,4,5,6,7} where pixel zero is
	 * the left-most pixel, indexed LSB-first.
	 *
	 * Because the shift registers run left-to-right, we must clock out the
	 * new bits right-to-left. Additionally, the columns are not in the same
	 * order as the register bits (for layout reasons).
	 *
	 * Later, at TX time, the SPI peripheral will invert the bit order
	 * within each byte ({4,5,6,7,0,1,2,3} -> {3,2,1,0,7,6,5,4}).
	 */

	tmp = flip_nibbles(__builtin_bswap32(r->u32[0]));
	r->u32[0] = flip_nibbles(__builtin_bswap32(r->u32[1]));
	r->u32[1] = tmp;
}

/*
 * Some of the GPIOs are attached differently in later revisions.
 */

#ifdef HW_REV2
static const struct gpio gpio_ledpwr_en = { .reg = GPIOA, .nr = 12, };
static const struct gpio gpio_regs_pwm;
static const struct gpio gpio_regs_clr;
static const struct gpio gpio_regs_lat = { .reg = GPIOA, .nr = 6, };
static const struct gpio gpio_row_fet[nr_rows] = {
	{ .reg = GPIOA, .nr = 11, },
	{ .reg = GPIOA, .nr = 10, },
	{ .reg = GPIOA, .nr = 9, },
	{ .reg = GPIOA, .nr = 8, },
	{ .reg = GPIOB, .nr = 15, },
	{ .reg = GPIOB, .nr = 14, },
};
#else
static const struct gpio gpio_ledpwr_en;
static const struct gpio gpio_regs_pwm = { .reg = GPIOA, .nr = 3, };
static const struct gpio gpio_regs_clr = { .reg = GPIOA, .nr = 6, };
static const struct gpio gpio_regs_lat = { .reg = GPIOA, .nr = 4, };
static const struct gpio gpio_row_fet[nr_rows] = {
	{ .reg = GPIOA, .nr = 15, },
	{ .reg = GPIOA, .nr = 12, },
	{ .reg = GPIOA, .nr = 11, },
	{ .reg = GPIOA, .nr = 10, },
	{ .reg = GPIOA, .nr = 9, },
	{ .reg = GPIOA, .nr = 8, },
};
#endif

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

static struct ledstate s[2];
static volatile int tx_off;
static volatile int rx_done;
static volatile int tx;

void irq_dma1_ch45(void)
{
	if (DMA1->ISR & DMA_ISR_TCIF4)
		DMA1->IFCR |= DMA_IFCR_CGIF4;

	if (DMA1->ISR & DMA_ISR_TCIF5) {
		/*
		 * UART RX complete: trigger the fixup in the main loop.
		 */
		DMA1->IFCR |= DMA_IFCR_CGIF5;
		rx_done = 1;
	}
}

void irq_dma1_ch23(void)
{
	/*
	 * This IRQ means SPI TX DMA has finished transmitting a row.
	 */

	DMA1->IFCR |= DMA_IFCR_CGIF3;

	/*
	 * Open the old FET (turn off the previous row). It takes a little bit
	 * for the pull-up to charge the gate capacitance, so do it before the
	 * busywait for SPI to complete.
	 */

	set_gpio(&gpio_row_fet[tx_off ? tx_off - 1 : nr_rows - 1], 1);

	/*
	 * We know TX DMA is complete, but the SPI TXFIFO still has 2-3 bytes
	 * left to transmit. We need to wait for it to finish before latching.
	 */

	while (spi_txlvl() || spi_busy())
		barrier();

	/*
	 * Switch the next row in: latch the column values SPI DMA just wrote
	 * out, and close the new FET. With 3MHz PCLK no waiting is necessary.
	 */

	set_gpio(&gpio_regs_lat, 1);
	set_gpio(&gpio_regs_lat, 0);
	set_gpio(&gpio_row_fet[tx_off], 0);
}

void __systick(void)
{
	/*
	 * This IRQ drives the row refreshes.
	 */

	if (++tx_off == nr_rows)
		tx_off = 0;

	start_spi_tx_dma(&s[tx].rows[tx_off].cols, nr_cols / 8);
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
	 * Make a 24MHz clock, which works out nicely because 24M is multiple of
	 * both 6 and 38400.
	 *
	 * HCLK is configured to be undivided, and PCLK to be HCLK divided by 8
	 * (so 3MHz). The HSI input to the PLL is always divided by two.
	 */

	#ifdef HW_REV2
	RCC->CR |= RCC_CR_HSEON;
	while (!(RCC->CR & RCC_CR_HSERDY))
		barrier();

	RCC->CFGR |= RCC_CFGR_PLLSRC_HSE_PREDIV | RCC_CFGR_PLLMUL2 |
		     RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE_DIV8;
	#else
	RCC->CR |= RCC_CR_HSION;
	while (!(RCC->CR & RCC_CR_HSIRDY))
		barrier();

	RCC->CFGR |= RCC_CFGR_PLLSRC_HSI_PREDIV | RCC_CFGR_PLLMUL6 |
		     RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE_DIV8;
	#endif

	RCC->CR |= RCC_CR_PLLON;
	while (!(RCC->CR & RCC_CR_PLLRDY))
		barrier();

	RCC->CFGR &= ~RCC_CFGR_SW;
	RCC->CFGR |= RCC_CFGR_SW_PLL;
	while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL)
		barrier();

	#ifdef HW_REV2
	RCC->CR &= ~RCC_CR_HSION;
	#endif
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

	for (i = 0; i < nr_rows; i++)
		configure_gpio_od(gpio_row_fet + i, 1);

	if (gpio_regs_clr.reg) {
		configure_gpio_pp(&gpio_regs_clr, 1);
		set_gpio(&gpio_regs_clr, 0);
		set_gpio(&gpio_regs_clr, 1);
	}

	if (gpio_regs_pwm.reg)
		configure_gpio_pp(&gpio_regs_pwm, 1);

	if (gpio_ledpwr_en.reg)
		configure_gpio_pp(&gpio_ledpwr_en, 0);

	/*
	 * Setup SPI TX DMA from the active buffer, start the system tick (which
	 * starts the DMA), and wait for it to begin.
	 */

	configure_gpio_pp(&gpio_regs_lat, 0);
	configure_gpio_af(&gpio_regs_ser, 0);
	configure_gpio_af(&gpio_regs_clk, 0);
	configure_spi();

	NVIC_SetPriority(SysTick_IRQn, 1);
	SysTick_Config(SYSTEM_CLOCK_FREQUENCY / (SCREEN_REFRESH_HZ * nr_rows));
	while (tx_off == -1)
		barrier();

	if (gpio_regs_pwm.reg)
		set_gpio(&gpio_regs_pwm, 0);

	if (gpio_ledpwr_en.reg)
		set_gpio(&gpio_ledpwr_en, 1);

	/*
	 * Setup and start UART RX DMA to the inactive buffer.
	 */

	configure_gpio_af(&gpio_uart_tx, 0);
	configure_gpio_af(&gpio_uart_rx, 0);
	configure_uart();
	start_uart_rx_dma(&s[!tx], nr_rows * nr_cols / 8);

	while (1) {
		asm volatile ("wfi" ::: "cc", "memory");

		/*
		 * The fixup code would limit the refresh rate if it had to run
		 * in a single scan interval, so it is "deferred" here where it
		 * can be interrupted.
		 */

		if (rx_done) {
			rx_done = 0;
			for (i = 0; i < nr_rows; i++)
				ledstate_fixup_row(&s[!tx].rows[i]);

			tx = !tx;
			start_uart_rx_dma(&s[!tx], nr_rows * nr_cols / 8);
		}
	}
}
