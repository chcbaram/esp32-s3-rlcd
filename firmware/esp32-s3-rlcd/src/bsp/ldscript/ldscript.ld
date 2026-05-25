

SECTION_DATA_PROLOGUE(.module,,)
{
    . = ALIGN(4);
    _smodule = .;
    
    KEEP(*(.module))
    KEEP(*(.module.*))
    
    . = ALIGN(4);
    _emodule = .;
} GROUP_DATA_LINK_IN(RAMABLE_REGION, ROMABLE_REGION)