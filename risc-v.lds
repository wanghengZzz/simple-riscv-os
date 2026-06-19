OUTPUT_ARCH("riscv")
ENTRY(_start)

MEMORY
{
	rom (rxa) : ORIGIN = 0x80000000, LENGTH = 1M
	ram (rwx) : ORIGIN = 0x80100000, LENGTH = 127M
}

PHDRS
{
	boot PT_LOAD FLAGS(5);   /* R | X */
	text PT_LOAD FLAGS(5);   /* R | X */
	rodata PT_LOAD FLAGS(4); /* R */
	data PT_LOAD FLAGS(6);   /* R | W */
	bss PT_LOAD FLAGS(6);    /* R | W */
	tls PT_TLS FLAGS(4);     /* R */
}

_stack_top = ORIGIN(ram) + LENGTH(ram); /* 0x88000000 */
_stack_size = 0x8000; /* 32KB */

max_harts = 4;

SECTIONS
{
	.boot : ALIGN(8)
	{
		_rom_start = ORIGIN(rom);
		KEEP(*(.text.boot))
	} > rom AT>rom :boot

	.text : ALIGN(8)
	{
		_text_start = .;
		*(.text .text.*)
		_text_end = .;
	} > ram AT>rom :text
	_text_lma = LOADADDR(.text);

	.rodata : ALIGN(8)
	{	
		_rodata_start = .;
		*(.rodata .rodata.*)
		*(.srodata .srodata.*)
		_rodata_end = .;
	} > ram AT>rom :rodata
	_rodata_lma = LOADADDR(.rodata);

	.data : ALIGN(8)
	{
		_data_start = .;
		*(.data .data.*)
		. = ALIGN(8);
		__global_pointer$ = . + 0x800;
		*(.sdata .sdata.*)
	} > ram AT>rom :data

	.tdata : ALIGN(8)
	{
		*(.tdata .tdata.*)
		_data_end = .;
	} > ram AT>rom :data :tls

	_data_lma = LOADADDR(.data);
	_ram_end = .;

	_bss_start = .;

	.tbss : ALIGN(8)
	{
		*(.tbss .tbss.*)
	} > ram :tls

	.bss : ALIGN(8)
	{
		*(.bss .bss.*)
		*(.sbss .sbss.*)
		*(COMMON)
		_bss_end = .;
	} > ram :bss
	. = ALIGN(8);

	__heap_start = .;
	__heap_end = _stack_top - _stack_size * max_harts;

	_heap_start = __heap_start;
	_heap_end = __heap_end;

	/DISCARD/ :
	{
		*(.riscv.attributes)
		*(.comment)
		*(.note*)
	}
}
