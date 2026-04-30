SECTIONS {
 /DISCARD/ : {
  *(.discard)
  *(.discard.*)
  *(.export_symbol)
 }
 __ksymtab 0 : { *(SORT(___ksymtab+*)) }
 __ksymtab_gpl 0 : { *(SORT(___ksymtab_gpl+*)) }
 __kcrctab 0 : { *(SORT(___kcrctab+*)) }
 __kcrctab_gpl 0 : { *(SORT(___kcrctab_gpl+*)) }
 .ctors 0 : ALIGN(8) { *(SORT(.ctors.*)) *(.ctors) }
 .init_array 0 : ALIGN(8) { *(SORT(.init_array.*)) *(.init_array) }
 .altinstructions 0 : ALIGN(8) { KEEP(*(.altinstructions)) }
 __bug_table 0 : ALIGN(8) { KEEP(*(__bug_table)) }
 __jump_table 0 : ALIGN(8) { KEEP(*(__jump_table)) }
 __patchable_function_entries : { *(__patchable_function_entries) }
 .data : {
  . = ALIGN(8); __start_alloc_tags = .; KEEP(*(alloc_tags)) __stop_alloc_tags = .;
 }
}
SECTIONS {
 .plt 0 : { BYTE(0) }
 .init.plt 0 : { BYTE(0) }
 .text.ftrace_trampoline 0 : { BYTE(0) }
 .init.text.ftrace_trampoline 0 : { BYTE(0) }
}
