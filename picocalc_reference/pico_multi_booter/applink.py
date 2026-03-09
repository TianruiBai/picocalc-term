#!/usr/bin/python3

# ****************************************************************************
# *                                                                          *
# *     App linking Tool                                                     *
# *                                                                          *
# ****************************************************************************

PRODUCT = "App Linking Tool"
PACKAGE = "uf2"
PROGRAM = "applink.py"
VERSION = "0.01"
CHANGES = "0000"
TOUCHED = "2022-03-06 18:42:21"
LICENSE = "MIT Licensed"
DETAILS = "https://opensource.org/licenses/MIT"

# .--------------------------------------------------------------------------.
# |     MIT Licence                                                          |
# `--------------------------------------------------------------------------'

# Copyright 2022, "Hippy"

# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included
# in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.

# .--------------------------------------------------------------------------.
# |     Satandard libraries                                                  |
# `--------------------------------------------------------------------------'

import os
import sys;  python3 = sys.version_info[0] == 3
import time; today   = time.strftime("%Y-%m-%d %H:%M:%S")

# .--------------------------------------------------------------------------.
# |     Configuration                                                        |
# `--------------------------------------------------------------------------'

SHOW_INVOCATIONS          = True
LOG_INVOCATIONS           = True
IGNORE_ERRORS             = False
REMOVE_STAGE_2_BOOLOADER  = True
EMIT_INDIVIDUAL_MAP_FILES = False

# .--------------------------------------------------------------------------.
# |     Utility code                                                         |
# `--------------------------------------------------------------------------'

def Pad(s, w, c=" "):
  if len(s) < w:
    s += c * (w-len(s))
  return s

# .--------------------------------------------------------------------------.
# |     UF2 Read Handling                                                    |
# `--------------------------------------------------------------------------'

def Uf2Block(f):
  if python3:
    b = f.read(512)
  else:
    s = f.read(512)
    b = bytearray()
    for c in s:
      b.append(ord(c))
  return b

def Expand(b):
  def Field(b, n, w=4):
    val = 0
    shf = 0
    for count in range(w):
      val = val | (b[n + count] << shf)
      shf = shf + 8
    return val, n + w
  def Bytes(b, n, w):
    return b[n:n+w], n + w
  n = 0
  head,   n = Field(b, n, 8)
  flags,  n = Field(b, n)
  adr,    n = Field(b, n)
  length, n = Field(b, n)
  seq,    n = Field(b, n)
  tot,    n = Field(b, n)
  family, n = Field(b, n)
  data,   n = Bytes(b, n, 476)
  tail,   n = Field(b, n)
  return  head, flags, adr, length, seq, tot, family, data, tail

# .--------------------------------------------------------------------------.
# |     UF2 Write Handling                                                   |
# `--------------------------------------------------------------------------'

def Encode(fDst, head, flags, adr, length, seq, tot, family, data, tail):
  def Field(b, n, w=4):
    for count in range(w):
      b.append(n & 0xFF)
      n = n >> 8
  def Bytes(b, data, w):
    for n in range(w):
      if n >= len(data) : b.append(0)
      else              : b.append(data[n])
  b = bytearray()
  Field(b, head, 8)
  Field(b, flags)
  Field(b, adr)
  Field(b, length)
  Field(b, seq)
  Field(b, tot)
  Field(b, family)
  Bytes(b, data, 476)
  Field(b, tail)
  fDst.write(b)
  return adr + length, seq + 1

# .--------------------------------------------------------------------------.
# |     Create a linker script for an APP build                              |
# `--------------------------------------------------------------------------'

def Align(n, mask):
  if (n & (mask-1)) != 0:
    n = (n | (mask-1)) + 1
  return n

def Size(n, mask):
   return int(Align(n & 0x0FFFFFFF, mask) / 1024)

def Prepare(dir, app):
  # dir = "./build"
  # app = "app"

  if REMOVE_STAGE_2_BOOLOADER : stage2 = " Removed "
  else                        : stage2 = "*"

  mapFile = os.path.join(dir, "applink.map")
  if not os.path.isfile(mapFile):
    print("LINK {} - '{}' not found".format(app, mapFile))
    if IGNORE_ERRORS : sys.exit()
    else             : sys.exit(1)

  last = 0
  with open(mapFile, "r") as f:
    for s in f:
      if s[0] != "S":
        a = s.strip().split()
        this = int(a[1], 16)
        if this > last:
          last = this
  used = "{:>3}k".format(Size(last, 4 * 1024))

  dstFile = os.path.join(dir, app + ".ld")

  with open(dstFile, "w") as f:
    f.write(
"""
/* Based on GCC ARM embedded samples.
   Defines the following symbols for use by code:
    __exidx_start
    __exidx_end
    __etext
    __data_start__
    __preinit_array_start
    __preinit_array_end
    __init_array_start
    __init_array_end
    __fini_array_start
    __fini_array_end
    __data_end__
    __bss_start__
    __bss_end__
    __end__
    end
    __HeapLimit
    __StackLimit
    __StackTop
    __stack (== StackTop)
*/

/*
    Increased the FLASH-ORIGIN by 64KB to not overwrite the bootloader
    (reduce LENGTH accordingly)
*/

MEMORY
{
    FLASH(rx)      : ORIGIN = 0x10000000 +###k, LENGTH = 2048k -###k
    RAM(rwx)       : ORIGIN = 0x20000000,       LENGTH = 256k
    SCRATCH_X(rwx) : ORIGIN = 0x20040000,       LENGTH = 4k
    SCRATCH_Y(rwx) : ORIGIN = 0x20041000,       LENGTH = 4k
}

ENTRY(_entry_point)

SECTIONS
{
    /* Second stage bootloader is prepended to the image. It must be 256 bytes big
       and checksummed. It is usually built by the boot_stage2 target
       in the Raspberry Pi Pico SDK
    */

    .flash_begin : {
        __flash_binary_start = .;
    } > FLASH

/*###/
    .boot2 : {
        __boot2_start__ = .;
        KEEP (*(.boot2))
        __boot2_end__ = .;
    } > FLASH

    ASSERT(__boot2_end__ - __boot2_start__ == 256,
        "ERROR: Pico second stage bootloader must be 256 bytes in size")
/###*/

    /* The second stage will always enter the image at the start of .text.
       The debugger will use the ELF entry point, which is the _entry_point
       symbol if present, otherwise defaults to start of .text.
       This can be used to transfer control back to the bootrom on debugger
       launches only, to perform proper flash setup.
    */

    .text : {
        __logical_binary_start = .;
        KEEP (*(.vectors))
        KEEP (*(.binary_info_header))
        __binary_info_header_end = .;
        KEEP (*(.reset))
        /* TODO revisit this now memset/memcpy/float in ROM */
        /* bit of a hack right now to exclude all floating point and time critical (e.g. memset, memcpy) code from
         * FLASH ... we will include any thing excluded here in .data below by default */
/*
        . = ALIGN(4);
    } > FLASH
    .text2 0x10010000 : {
*/
        *(.init)
        *(EXCLUDE_FILE(*libgcc.a: *libc.a:*lib_a-mem*.o *libm.a:) .text*)
        *(.fini)
        /* Pull all c'tors into .text */
        *crtbegin.o(.ctors)
        *crtbegin?.o(.ctors)
        *(EXCLUDE_FILE(*crtend?.o *crtend.o) .ctors)
        *(SORT(.ctors.*))
        *(.ctors)
        /* Followed by destructors */
        *crtbegin.o(.dtors)
        *crtbegin?.o(.dtors)
        *(EXCLUDE_FILE(*crtend?.o *crtend.o) .dtors)
        *(SORT(.dtors.*))
        *(.dtors)

        *(.eh_frame*)
        . = ALIGN(4);
    } > FLASH

    .rodata : {
        *(EXCLUDE_FILE(*libgcc.a: *libc.a:*lib_a-mem*.o *libm.a:) .rodata*)
        . = ALIGN(4);
        *(SORT_BY_ALIGNMENT(SORT_BY_NAME(.flashdata*)))
        . = ALIGN(4);
    } > FLASH

    .ARM.extab :
    {
        *(.ARM.extab* .gnu.linkonce.armextab.*)
    } > FLASH

    __exidx_start = .;
    .ARM.exidx :
    {
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
    } > FLASH
    __exidx_end = .;

    /* Machine inspectable binary information */
    . = ALIGN(4);
    __binary_info_start = .;
    .binary_info :
    {
        KEEP(*(.binary_info.keep.*))
        *(.binary_info.*)
    } > FLASH
    __binary_info_end = .;
    . = ALIGN(4);

    /* End of .text-like segments */
    __etext = .;

   .ram_vector_table (COPY): {
        *(.ram_vector_table)
    } > RAM

    .data : {
        __data_start__ = .;
        *(vtable)

        *(.time_critical*)

        /* remaining .text and .rodata; i.e. stuff we exclude above because we want it in RAM */
        *(.text*)
        . = ALIGN(4);
        *(.rodata*)
        . = ALIGN(4);

        *(.data*)

        . = ALIGN(4);
        *(.after_data.*)
        . = ALIGN(4);
        /* preinit data */
        PROVIDE_HIDDEN (__mutex_array_start = .);
        KEEP(*(SORT(.mutex_array.*)))
        KEEP(*(.mutex_array))
        PROVIDE_HIDDEN (__mutex_array_end = .);

        . = ALIGN(4);
        /* preinit data */
        PROVIDE_HIDDEN (__preinit_array_start = .);
        KEEP(*(SORT(.preinit_array.*)))
        KEEP(*(.preinit_array))
        PROVIDE_HIDDEN (__preinit_array_end = .);

        . = ALIGN(4);
        /* init data */
        PROVIDE_HIDDEN (__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        PROVIDE_HIDDEN (__init_array_end = .);

        . = ALIGN(4);
        /* finit data */
        PROVIDE_HIDDEN (__fini_array_start = .);
        *(SORT(.fini_array.*))
        *(.fini_array)
        PROVIDE_HIDDEN (__fini_array_end = .);

        *(.jcr)
        . = ALIGN(4);
        /* All data end */
        __data_end__ = .;
    } > RAM AT> FLASH

    .uninitialized_data (COPY): {
        . = ALIGN(4);
        *(.uninitialized_data*)
    } > RAM

    /* Start and end symbols must be word-aligned */
    .scratch_x : {
        __scratch_x_start__ = .;
        *(.scratch_x.*)
        . = ALIGN(4);
        __scratch_x_end__ = .;
    } > SCRATCH_X AT > FLASH
    __scratch_x_source__ = LOADADDR(.scratch_x);

    .scratch_y : {
        __scratch_y_start__ = .;
        *(.scratch_y.*)
        . = ALIGN(4);
        __scratch_y_end__ = .;
    } > SCRATCH_Y AT > FLASH
    __scratch_y_source__ = LOADADDR(.scratch_y);

    .bss  : {
        . = ALIGN(4);
        __bss_start__ = .;
        *(SORT_BY_ALIGNMENT(SORT_BY_NAME(.bss*)))
        *(COMMON)
        . = ALIGN(4);
        __bss_end__ = .;
    } > RAM

    .heap (COPY):
    {
        __end__ = .;
        end = __end__;
        *(.heap*)
        __HeapLimit = .;
    } > RAM

    /* .stack*_dummy section doesn't contains any symbols. It is only
     * used for linker to calculate size of stack sections, and assign
     * values to stack symbols later
     *
     * stack1 section may be empty/missing if platform_launch_core1 is not used */

    /* by default we put core 0 stack at the end of scratch Y, so that if core 1
     * stack is not used then all of SCRATCH_X is free.
     */
    .stack1_dummy (COPY):
    {
        *(.stack1*)
    } > SCRATCH_X
    .stack_dummy (COPY):
    {
        *(.stack*)
    } > SCRATCH_Y

    .flash_end : {
        __flash_binary_end = .;
    } > FLASH

    /* stack limit is poorly named, but historically is maximum heap ptr */
    __StackLimit = ORIGIN(RAM) + LENGTH(RAM);
    __StackOneTop = ORIGIN(SCRATCH_X) + LENGTH(SCRATCH_X);
    __StackTop = ORIGIN(SCRATCH_Y) + LENGTH(SCRATCH_Y);
    __StackOneBottom = __StackOneTop - SIZEOF(.stack1_dummy);
    __StackBottom = __StackTop - SIZEOF(.stack_dummy);
    PROVIDE(__stack = __StackTop);

    /* Check if data + heap + stack exceeds RAM limit */
    ASSERT(__StackLimit >= __HeapLimit, "region RAM overflowed")

    ASSERT( __binary_info_header_end - __logical_binary_start <= 256, "Binary info must be in first 256 bytes of the binary")
    /* todo assert on extra code */
}
"""
.strip()
.replace("###k", used)
.replace("###", stage2))

# .--------------------------------------------------------------------------.
# |     Post UF2 build Handling                                              |
# `--------------------------------------------------------------------------'

def Built(dir, app):
  # dir  = "./build"
  # app  = "app"

  uf2File = os.path.join(dir, app + ".uf2")
  if not os.path.isfile(uf2File):
    print("BUILT {} - '{}' not found".format(app, uf2File))
    if IGNORE_ERRORS : sys.exit()
    else             : sys.exit(1)

  strt = -1
  last = -1
  with open(uf2File, "rb") as f:
    block = Uf2Block(f)
    while block:
      head, flags, adr, length, seq, tot, family, data, tail = Expand(block)
      if  head   == 0x9E5D51570A324655 \
      and tail   == 0xAB16F30 \
      and family == 0xE48BFF56:
        if strt < 0                : strt = adr
        if adr + length - 1 > last : last = adr + length - 1
      block = Uf2Block(f)

##enlarge sd_boot flash size
  if app == "sd_boot":
    print("original last: ", last," ",'{:02X}'.format(last))
    print("orignal size: ","{:>3}k".format(Size(last-strt, 1024)))
    print("orignal used: ","{:>3}k".format(Size(last, 4*1024)))
    if last < 0x10032000:
      last = 0x10032000

  size = "{:>3}k".format(Size(last-strt, 1024))
  used = "{:>3}k".format(Size(last, 4*1024))
  
  print(last,strt) 
  print(size,used)
 
  mapfile = os.path.join(dir, "applink.map")
  if (strt & 0x0FFFFFFF) == 0:
    with open(mapfile, "w") as f:
      #        12345678 12345678 ###k ###k
      f.write("Start    Last     Size Used\n")
      f.write("{:>08X} {:>08X} {} {}  {}\n".format(strt, last,
                                                   size, used, app))
      info = Align(last, 256)
      f.write("{:>08X} {:>08X}  256 {:>3}k  {}\n".format(info, info | 0xFF,
                                                  Size(info | 0XFF, 4*1024),
                                                  "Info Table"))
  else:
    with open(mapfile, "a") as f:
      f.write("{:>08X} {:>08X} {} {}  {}\n".format(strt, last,
                                                   size, used, app))

  mapFile = os.path.join(dir, app + ".map")
  if EMIT_INDIVIDUAL_MAP_FILES:
    with open(mapFile, "w") as f:
      #        12345678 12345678 ###k ###k
      f.write("Start    Last     Size Used\n")
      f.write("{:>08X} {:>08X} {} {}  {}\n".format(strt, last,
                                                  size, used, app))
      if (strt & 0x0FFFFFFF) == 0:
        info = Align(last, 256)
        f.write("{:>08X} {:>08X}  256 {:>3}k  {}\n".format(info, info | 0xFF,
                                                    Size(info | 0XFF, 4*1024),
                                                    "Info Table"))
  elif os.path.isfile(mapFile):
    os.remove(mapFile)

# .--------------------------------------------------------------------------.
# |     Combining UF2 files                                                  |
# `--------------------------------------------------------------------------'

def ToBytes(s):
  b = bytearray()
  for c in s:
    b.append(ord(c))
  return b

def Join(dir, dst, uf2):
  # dir = "./build"
  # dst = "out.uf2"
  # uf2 = [ "boot.uf2", "app1.uf2", "app2.uf2", ... ]

  # Check the files exist
  for src in uf2:
    uf2File = os.path.join(dir, src)
    if not os.path.isfile(uf2File):
      print("JOIN {} - '{}' not found".format(dst, uf2File))
      if IGNORE_ERRORS : sys.exit()
      else             : sys.exit(1)

  # Determine how many blocks in output including padding and info block
  adr = -1
  tot = 0
  for src in uf2:
    uf2File = os.path.join(dir, src)
    with open(uf2File, "rb") as fSrc:
      block = Uf2Block(fSrc)
      while block:
        head, flags, Xadr, length, seq, Xtot, family, data, tail = Expand(block)
        if adr < 0:
          adr = Xadr
        else:
          while adr < Xadr:
            adr = adr + 256
            tot = tot + 1
          adr = adr + length
          tot = tot + 1
        block = Uf2Block(fSrc)

  # Build the info table

  info = ""
  mapFile = os.path.join(dir, "applink.map")
  with open(mapFile, "r") as f:
    line = 0
    for s in f:
      line = line + 1
      if line > 3:
        a = s.strip().split()
        strt = int(a[0], 16)
        info += chr((strt >>  0) & 0xFF)
        info += chr((strt >>  8) & 0xFF)
        info += chr((strt >> 16) & 0xFF)
        info += chr((strt >> 24) & 0xFF)
        info += Pad(a[-1][:12], 12, chr(0))

  # Concatenate the files
  dstFile = os.path.join(dir, dst)
  adr = -1
  seq = 0
  with open(dstFile, "wb") as fDst:
    for n in range(len(uf2)):
      uf2File = os.path.join(dir, uf2[n])
      with open(uf2File, "rb") as fSrc:
        block = Uf2Block(fSrc)
        while block:
          head, flags, Xadr, length, Xseq, Xtot, family, data, tail = Expand(block)
          if adr < 0:
            adr = Xadr
          else:
            while adr < Xadr:
              #               123456789-123456
              pads = ToBytes("Padding before  {}".format(uf2[n]))
              adr, seq = Encode(fDst, head, flags, adr, 256, seq, tot, family,
                                pads, tail)
          adr, seq = Encode(fDst, head, flags, adr, 256, seq, tot, family,
                            data[:256], tail)
          block = Uf2Block(fSrc)
      if n == 0:
        # Add info block
        adr, seq = Encode(fDst, head, flags, adr, 256, seq, tot, family,
                          ToBytes(info)[:256], tail)

# .--------------------------------------------------------------------------.
# |     Main program and command dispatcher                                  |
# `--------------------------------------------------------------------------'

def GetArgv(n):
  if   n < 0             : return sys.argv[-n:]
  elif n < len(sys.argv) : return sys.argv[n]
  else                   : return ""

def Main():

  # 0             1        2        3        4
  # ./applink.py  PREPARE  ./build  app
  # ./applink.py  BUILT    ./build  app
  # ./applink.py  JOIN     ./build  out.uf2  boot.uf2 app1.uf2 app2.uf2 ...

  cmd = GetArgv(1)
  dir = GetArgv(2)
  app = GetArgv(3)
  uf2 = GetArgv(-4)

  if SHOW_INVOCATIONS:
    for n in range(1, len(sys.argv)):
      print("****** applink.py : [{}] {}".format(n, GetArgv(n)))

  logFile = os.path.join(dir, "applink.log")
  if LOG_INVOCATIONS:
    s = " ".join(sys.argv[2:]).replace("/home/pi/", "~/")
    with open(logFile, "a") as f:
      f.write("{}  {:<5} {}\n".format(today, cmd, s))
  elif os.path.isfile(logFile):
    os.remove(logFile)

  if   cmd == "PREPARE" : Prepare (dir, app)
  elif cmd == "BUILT"   : Built   (dir, app)
  elif cmd == "JOIN"    : Join    (dir, app, uf2)
  else:
    print("Unknown '{}' command".format(cmd))
    sys.exit(1)

if __name__ == "__main__":
  Main()
