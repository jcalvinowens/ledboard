/*
 * LED board startup code for STM32F030C8T6
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

.cpu cortex-m0
.fpu softvfp
.syntax unified
.thumb

/*
 * These five constants are defined in link.ld, and ultimately the linker will
 * decide what their literal values are at build time.
 */

.word ____stack
.word ____data_init
.word ____data_start
.word ____data_end
.word ____bss_start
.word ____bss_end

.global __reset
.section .text.__reset

	.type __reset, %function
	.size __reset, .-__reset

	/*
	 * Execution starts here.
	 */

	__reset:
		ldr r0, =____stack
		mov sp, r0

		/*
		 * Copy the data segment from NVMEM to SRAM.
		 */

		movs r0, #0
		ldr r1, =____data_start
		ldr r2, =____data_end
		subs r2, r2, r1
		ldr r3, =____data_init
		copy_data_section_to_sram:
			ldr r4, [r3, r0]
			str r4, [r1, r0]
			adds r0, r0, #4
			cmp r0, r2
			blo copy_data_section_to_sram

		/*
		 * Zero the BSS segment in SRAM.
		 */

		movs r0, #0
		ldr r1, =____bss_start
		ldr r2, =____bss_end
		subs r2, r2, r1
		movs r3, #0
		zero_bss_section_in_sram:
			str r3, [r1, r0]
			adds r0, r0, #4
			cmp r0, r2
			blo zero_bss_section_in_sram

		/*
		 * Branch to main()
		 */

		bl main

.section .text.irq_none, "ax", %progbits

	.type irq_none, %function
	.size irq_none, .-irq_none

	/*
	 * This stub enters an infinite loop, for debugging.
	 */

	irq_none:
		b irq_none

.section .isr, "a", %progbits

	.type isr_vector, %object
	.size isr_vector, .-isr_vector

	/*
	 * This is the array of function pointers the hardware calls to handle
	 * the various possible faults and interrupts.
	 */

	isr_vector:
		.word ____stack
		.word __reset
		.word __nmi
		.word __hardfault
		.word 0
		.word 0
		.word 0
		.word 0
		.word 0
		.word 0
		.word 0
		.word __svc
		.word 0
		.word 0
		.word __pendsv
		.word __systick
		.word irq_wwdg
		.word 0
		.word irq_rtc
		.word irq_flash
		.word irq_rcc
		.word irq_exti01
		.word irq_exti23
		.word irq_exti45
		.word 0
		.word irq_dma1_ch1
		.word irq_dma1_ch23
		.word irq_dma1_ch45
		.word irq_adc1
		.word irq_tim1_brk
		.word irq_tim1_cc
		.word 0
		.word irq_tim3
		.word 0
		.word 0
		.word irq_tim14
		.word irq_tim15
		.word irq_tim16
		.word irq_tim17
		.word irq_i2c1
		.word irq_i2c2
		.word irq_spi1
		.word irq_spi2
		.word irq_usart1
		.word irq_usart2
		.word 0
		.word 0
		.word 0
		.word 0

/*
 * Declare symbols for all the handlers as weak, so they can be overriden in the
 * main C source code without editing this file.
 */

.weak irq_adc1
.weak irq_dma1_ch1
.weak irq_dma1_ch23
.weak irq_dma1_ch45
.weak irq_exti01
.weak irq_exti23
.weak irq_exti45
.weak irq_flash
.weak irq_i2c1
.weak irq_i2c2
.weak irq_rcc
.weak irq_rtc
.weak irq_spi1
.weak irq_spi2
.weak irq_tim14
.weak irq_tim15
.weak irq_tim16
.weak irq_tim17
.weak irq_tim1_brk
.weak irq_tim1_cc
.weak irq_tim3
.weak irq_usart1
.weak irq_usart2
.weak irq_wwdg
.weak __hardfault
.weak __nmi
.weak __pendsv
.weak __svc
.weak __systick

/*
 * For debugging, use an infinite loop as the default interrupt action.
 */

.thumb_set irq_adc1,		irq_none
.thumb_set irq_dma1_ch1,	irq_none
.thumb_set irq_dma1_ch23,	irq_none
.thumb_set irq_dma1_ch45,	irq_none
.thumb_set irq_exti01,		irq_none
.thumb_set irq_exti23,		irq_none
.thumb_set irq_exti45,		irq_none
.thumb_set irq_flash,		irq_none
.thumb_set irq_i2c1,		irq_none
.thumb_set irq_i2c2,		irq_none
.thumb_set irq_rcc,		irq_none
.thumb_set irq_rtc,		irq_none
.thumb_set irq_spi1,		irq_none
.thumb_set irq_spi2,		irq_none
.thumb_set irq_tim14,		irq_none
.thumb_set irq_tim15,		irq_none
.thumb_set irq_tim16,		irq_none
.thumb_set irq_tim17,		irq_none
.thumb_set irq_tim1_brk,	irq_none
.thumb_set irq_tim1_cc,		irq_none
.thumb_set irq_tim3,		irq_none
.thumb_set irq_usart1,		irq_none
.thumb_set irq_usart2,		irq_none
.thumb_set irq_wwdg,		irq_none
.thumb_set __hardfault,		irq_none
.thumb_set __nmi,		irq_none
.thumb_set __pendsv,		irq_none
.thumb_set __svc,		irq_none
.thumb_set __systick,		irq_none
