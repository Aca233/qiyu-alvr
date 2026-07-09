#!/usr/bin/env python3
"""Parse ELF64 DT_NEEDED entries from Android .so files to determine the
true native dependency graph. This lets us compute the correct load order
and find any missing dependency (the root cause of dlopen 'not found' errors).
"""
import struct
import sys
import os

# ELF constants
PT_LOAD = 1
PT_DYNAMIC = 2
DT_NEEDED = 1
DT_STRTAB = 5
DT_STRSZ = 10
DT_SONAME = 14

def parse_elf64_needed(path):
    with open(path, 'rb') as f:
        data = f.read()

    # ELF header
    if data[:4] != b'\x7fELF':
        return None, "not ELF"
    ei_class = data[4]   # 2 = 64-bit
    if ei_class != 2:
        return None, "not 64-bit"
    # ELF64 header layout after e_ident[16]
    (e_type, e_machine, e_version, e_entry, e_phoff, e_shoff,
     e_flags, e_ehsize, e_phentsize, e_phnum,
     e_shentsize, e_shnum, e_shstrndx) = struct.unpack_from('<HHIQQQIHHHHHH', data, 16)

    # Parse program headers
    phdrs = []
    for i in range(e_phnum):
        off = e_phoff + i * e_phentsize
        (p_type, p_flags, p_offset, p_vaddr, p_paddr,
         p_filesz, p_memsz, p_align) = struct.unpack_from('<IIQQQQQQ', data, off)
        phdrs.append((p_type, p_flags, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align))

    def vaddr_to_off(vaddr):
        for (p_type, p_flags, p_offset, p_vaddr_seg, p_paddr, p_filesz, p_memsz, p_align) in phdrs:
            if p_type == PT_LOAD and p_vaddr_seg <= vaddr < p_vaddr_seg + p_filesz:
                return p_offset + (vaddr - p_vaddr_seg)
        return None

    # Find PT_DYNAMIC
    dyn_off = None
    dyn_size = None
    for ph in phdrs:
        if ph[0] == PT_DYNAMIC:
            dyn_off = ph[2]
            dyn_size = ph[5]
            break
    if dyn_off is None:
        return None, "no PT_DYNAMIC"

    # Parse dynamic entries
    strtab_vaddr = None
    strtab_size = None
    needed_offsets = []
    soname_off = None
    n = dyn_size // 16
    for i in range(n):
        off = dyn_off + i * 16
        d_tag, d_val = struct.unpack_from('<qQ', data, off)
        if d_tag == DT_NEEDED:
            needed_offsets.append(d_val)
        elif d_tag == DT_STRTAB:
            strtab_vaddr = d_val
        elif d_tag == DT_STRSZ:
            strtab_size = d_val
        elif d_tag == DT_SONAME:
            soname_off = d_val
        elif d_tag == 0:  # DT_NULL
            break

    if strtab_vaddr is None:
        return None, "no DT_STRTAB"

    strtab_off = vaddr_to_off(strtab_vaddr)
    if strtab_off is None:
        return None, "cannot map strtab vaddr 0x%x" % strtab_vaddr

    def read_str(str_off):
        end = data.index(b'\x00', strtab_off + str_off)
        return data[strtab_off + str_off:end].decode('utf-8', 'replace')

    needed = [read_str(o) for o in needed_offsets]
    soname = read_str(soname_off) if soname_off is not None else None
    return (soname, needed), None

def main():
    dirs = sys.argv[1:]
    if not dirs:
        dirs = [os.path.join(os.path.dirname(__file__), '..', 'app', 'src', 'main', 'jniLibs', 'arm64-v8a')]
    # collect all .so
    files = []
    for d in dirs:
        if os.path.isfile(d):
            files.append(d)
        else:
            for name in sorted(os.listdir(d)):
                if name.endswith('.so'):
                    files.append(os.path.join(d, name))

    # known system libs (always present on Android, never need packaging)
    system_libs = {
        'libc.so', 'libdl.so', 'libm.so', 'liblog.so', 'libz.so',
        'libdl.so', 'librt.so', 'libpthread.so', 'libandroid.so',
        'libnativewindow.so', 'libsync.so', 'libEGL.so', 'libGLESv1_CM.so',
        'libGLESv2.so', 'libGLESv3.so', 'libvulkan.so', 'libOpenSLES.so',
        'libOpenMAXAL.so', 'libbinder.so', 'libutils.so', 'libcutils.so',
        'libhardware.so', 'libui.so', 'libgui.so', 'libsync.so',
        'libandroid_runtime.so', 'libstagefright.so', 'libmedia.so',
        'libcamera_client.so', 'libsensor.so', 'libinput.so',
        'libsurfaceflinger.so', 'libskia.so', 'libjpeg.so', 'libpng.so',
        'libsqlite.so', 'libexpat.so', 'libicuuc.so', 'libicui18n.so',
        'libssl.so', 'libcrypto.so', 'libstlport.so', 'libgnustl_shared.so',
        'libstdc++.so', 'libcrypto.so', 'libwpa_client.so',
        'libvrapi.so',  # we package this, but it's also system on some
    }

    print("=" * 70)
    print("ELF DT_NEEDED dependency analysis")
    print("=" * 70)
    packaged = set(os.path.basename(f) for f in files)
    for f in files:
        base = os.path.basename(f)
        result, err = parse_elf64_needed(f)
        if err:
            print("\n%s: ERROR %s" % (base, err))
            continue
        soname, needed = result
        print("\n%s (SONAME: %s)" % (base, soname or '?'))
        print("  size: %d bytes" % os.path.getsize(f))
        missing = []
        for n in needed:
            mark = ''
            if n in system_libs:
                mark = '  [system, ok]'
            elif n in packaged:
                mark = '  [packaged]'
            else:
                mark = '  *** NOT PACKAGED ***'
                missing.append(n)
            print("    -> %s%s" % (n, mark))
        if missing:
            print("  !! MISSING DEPENDENCIES: %s" % missing)

if __name__ == '__main__':
    main()
