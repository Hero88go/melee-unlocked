/* Augment GNU ld's shared-object layout. Snapshot exactly the same writable
 * classes as the PE build, and fail at link time before overlapping GX's 64 MiB
 * physical window. MEM1 occupies 0x80000000..0x82800000. */
SECTIONS
{
  __data_start__ = ADDR(.data);
  __data_end__ = ADDR(.data) + SIZEOF(.data);
  __bss_start__ = ADDR(.bss);
  __bss_end__ = ADDR(.bss) + SIZEOF(.bss);
  ASSERT(ADDR(.bss) + SIZEOF(.bss) <= 0x84000000,
         "melee_game exceeds the console-compatible GX address window")
}
INSERT AFTER .bss;
