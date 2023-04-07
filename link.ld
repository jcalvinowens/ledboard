/*
 * LED board linker script for STM32F030C8T6
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

MEMORY {
	NVMEM	(rx)	: ORIGIN = 0x8000000,	LENGTH = 64K
	SRAM	(xrw)	: ORIGIN = 0x20000000,	LENGTH = 8K
}

SECTIONS {
	/*
	 * This is a table of function pointers understood by the hardware, and
	 * used to service exceptions and interrupts. It is defined in start.s.
	 */

	.isr : {
		. = ALIGN(4);
		KEEP(*(.isr))
	} >NVMEM

	/*
	 * The text segment contains the executable code. On the STM32, we can
	 * execute this code directly from the flash.
	 */

	.text : {
		. = ALIGN(4);
		*(.text)
	} >NVMEM

	/*
	 * ...similar to text, the read only data segment can be utilized "as it
	 * sits" directly from flash.
	 */

	.rodata : {
		. = ALIGN(4);
		*(.rodata)
	} >NVMEM

	/*
	 * The data segment cannot be used in flash: even if the flash were
	 * writable, we would exhaust its write cycles unacceptably quickly...
	 *
	 * This arranges for space to be made in SRAM for the data segment. Code
	 * in start.s will copy .data into that SRAM at runtime.
	 */

	.data : {
		. = ALIGN(4);
		____data_start = .;
		*(.data)
		. = ALIGN(4);
		____data_end = .;
	} >SRAM AT> NVMEM

	/*
	 * The BSS segment is for zeroed data, so arrange for space to be made
	 * in SRAM for that as well. Code in start.s will zero this at runtime.
	 */

	.bss : {
		. = ALIGN(4);
		____bss_start = .;
		*(.bss)
		*(COMMON)
		. = ALIGN(4);
		____bss_end = .;
	} >SRAM
}

____stack = 0x20001FFF;
____data_init = LOADADDR(.data);
ENTRY(__reset)
