#!/usr/bin/env python3
import sys, os, time, struct, argparse

parser = argparse.ArgumentParser()
parser.add_argument('hexfile', nargs='?', default='custom_packet.hex')
args = parser.parse_args()
hexpath = args.hexfile
if not os.path.exists(hexpath):
    print('Hex file not found:', hexpath)
    sys.exit(1)
with open(hexpath, 'r', encoding='utf-8') as f:
    lines = f.readlines()

bytes_out = bytearray()
for line in lines:
    # strip comments (# or //)
    line = line.split('#',1)[0]
    line = line.split('//',1)[0]
    toks = line.strip().split()
    for tok in toks:
        if tok.startswith('0x') or tok.startswith('0X'):
            tok = tok[2:]
        # strip non-hex chars around
        while tok and (not all(c in '0123456789abcdefABCDEF' for c in tok)):
            tok = tok[1:] if not tok[0].isalnum() else tok[:-1]
        if not tok:
            continue
        if len(tok) == 2:
            try:
                bytes_out.append(int(tok,16))
            except ValueError:
                pass
        else:
            # split into byte pairs
            if len(tok) % 2 != 0:
                print('Odd-length token, skipping:', tok)
                continue
            for i in range(0,len(tok),2):
                bytes_out.append(int(tok[i:i+2],16))

if not bytes_out:
    print('No bytes parsed from', hexpath)
    sys.exit(1)

pcap_path = os.path.splitext(hexpath)[0] + '.pcap'
# pcap global header (little-endian): magic, ver_major, ver_minor, thiszone, sigfigs, snaplen, network
magic = 0xa1b2c3d4
ver_major = 2
ver_minor = 4
thiszone = 0
sigfigs = 0
snaplen = 262144
network = 1  # LINKTYPE_ETHERNET
with open(pcap_path, 'wb') as out:
    out.write(struct.pack('<IHHIIII', magic, ver_major, ver_minor, thiszone, sigfigs, snaplen, network))
    ts = time.time()
    ts_sec = int(ts)
    ts_usec = int((ts - ts_sec) * 1_000_000)
    incl_len = len(bytes_out)
    orig_len = incl_len
    out.write(struct.pack('<IIII', ts_sec, ts_usec, incl_len, orig_len))
    out.write(bytes_out)

print('Wrote', pcap_path, 'size', os.path.getsize(pcap_path))
