/*
 * LED board linker script for STM32F030
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

ENTRY(__reset)

MEMORY {
	NVMEM	(rx)	: ORIGIN = 0x8000000,	LENGTH = 64K
	SRAM	(xrw)	: ORIGIN = 0x20000000,	LENGTH = 8K
}

SECTIONS {
	.isr : {
		. = ALIGN(4);
		KEEP(*(.isr))
		. = ALIGN(4);
	} >NVMEM

	.text : {
		. = ALIGN(4);
		*(.text)
		. = ALIGN(4);
		_etext = .;
	} >NVMEM

	.rodata : {
		. = ALIGN(4);
		*(.rodata)
		. = ALIGN(4);
	} >NVMEM

	_estack = 0x20001FFF;
	_sidata = LOADADDR(.data);

	.data : {
		. = ALIGN(4);
		_sdata = .;
		*(.data)
		. = ALIGN(4);
		_edata = .;
	} >SRAM AT> NVMEM

	. = ALIGN(4);
	.bss : {
		_sbss = .;
		*(.bss)
		*(COMMON)
		. = ALIGN(4);
		_ebss = .;
	} >SRAM
}
