import struct
import mmap
import os
from typing import Any

TYPE_STR      = 0  
TYPE_SSTR     = 1  
TYPE_ID       = 2  
TYPE_NUM      = 3  
TYPE_BOOL     = 4  
TYPE_SET      = 5  
TYPE_JSON     = 6  
TYPE_REGEX    = 7  
TYPE_FSTRING  = 8  
TYPE_INJECTED = 9  

CLU_NORMAL  = 0
CLU_META    = 1  
CLU_WMETA   = 2  
CLU_HEADER  = 3  

MOD_NONE     = 0x00
MOD_PRIME    = 0x01  
MOD_REJECTED = 0x02  

PAIR_ONEWAY = 0  
PAIR_TWOWAY = 1  

MAGIC = b'NUQL'

def w_str(buf: bytearray, s: str) -> bytearray:
    b = s.encode('utf-8')           
    buf += struct.pack('<I', len(b)) 
    buf += b                        
    return buf

def w_u8(buf: bytearray, v: int) -> bytearray:
    buf += struct.pack('<B', v)
    return buf

def w_u32(buf: bytearray, v: int) -> bytearray:
    buf += struct.pack('<I', v)
    return buf

def w_f64(buf: bytearray, v: float) -> bytearray:
    buf += struct.pack('<d', v)
    return buf

def w_u64(buf: bytearray, v: int) -> bytearray:
    buf += struct.pack('<Q', v)
    return buf


# -------------------------------------------------------
# 値の書き込み
# -------------------------------------------------------

def write_value(buf: bytearray, v: Any) -> bytearray:
    """
    valueのdict（{"type": ..., "value": ...}）を受け取り、
    バイナリ形式に変換してbufに追記する。

    先頭に型ID（1バイト）を書き、続けて型に応じたデータを書く。
    C++側の read_value() と対応している。
    """

    if v is None:
        buf = w_u8(buf, TYPE_STR)
        buf = w_str(buf, '')
        return buf

    t = v['type'] 

    if t == 'str':
        buf = w_u8(buf, TYPE_STR)
        buf = w_str(buf, v['value'])

    elif t == 'sstr':
        buf = w_u8(buf, TYPE_SSTR)
        buf = w_str(buf, v['value'])

    elif t == 'id':
        buf = w_u8(buf, TYPE_ID)
        buf = w_str(buf, v['value'])

    elif t == 'num':
        buf = w_u8(buf, TYPE_NUM)
        buf = w_f64(buf, float(v['value']))

    elif t == 'bool':
        buf = w_u8(buf, TYPE_BOOL)
        buf = w_u8(buf, 1 if v['value'] else 0)

    elif t == 'set':
        buf = w_u8(buf, TYPE_SET)
        items = list(v['value'])         
        buf = w_u32(buf, len(items))      
        for item in items:
            buf = w_str(buf, item)   

    elif t == 'json':
        buf = w_u8(buf, TYPE_JSON)
        buf = w_str(buf, v['value'])

    elif t == 'regex':
        buf = w_u8(buf, TYPE_REGEX)
        buf = w_str(buf, v['value'])

    elif t == 'fstring':
        buf = w_u8(buf, TYPE_FSTRING)
        buf = w_str(buf, v['value'])

    elif t == 'injected':
        buf = w_u8(buf, TYPE_INJECTED)
        buf = w_str(buf, v['extension']) 
        buf = w_str(buf, v['type_name'])  
        buf = w_str(buf, v['value'])     

    return buf


# -------------------------------------------------------
# datumの書き込み
# -------------------------------------------------------

def write_datum(buf: bytearray, datum: dict) -> bytearray:
    """
    1つのdatum（{"modifiers": [...], "pair": {...}}）を受け取る

    modifier flags: uint8（bit0=protected, bit1=sideway）
    pair_type:      uint8（0=oneway, 1=twoway）
    lhs:            value
    rhs_count:      uint32
    rhs:            value × rhs_count

    C++ read_datum() 
    """

    mods  = datum['modifiers']
    flags = 0
    if '!' in mods: flags |= 0x01  
    if '/' in mods: flags |= 0x02 
    buf = w_u8(buf, flags)

    pair = datum['pair']
    lhs  = pair['lhs']
    rhs  = pair['rhs']

    if isinstance(rhs, list):
        buf = w_u8(buf, PAIR_TWOWAY)
        buf = write_value(buf, lhs)
        buf = w_u32(buf, len(rhs))    
        for r in rhs:
            buf = write_value(buf, r) 
    else:
        buf = w_u8(buf, PAIR_ONEWAY)
        buf = write_value(buf, lhs)
        buf = w_u32(buf, 1)          
        buf = write_value(buf, rhs)

    return buf


# -------------------------------------------------------
# クラスターの書き込み
# -------------------------------------------------------

def write_cluster(buf: bytearray, node: dict) -> bytearray:
    """
    1つのクラスターノード（dict）を受け取る

    clu_type:    uint8
    modifier:    uint8
    name:        string
    inh_count:   uint32
    inheritance: (flags:uint8 + name:string) × inh_count
    datum_count: uint32
    data:        datum × datum_count
    diff_count:  uint32
    diffs:       (name:string + datum_count:uint32 + datum×N) × diff_count

    C++　read_cluster()
    """

    if 'meta_type' in node:
        # meta / wmeta クラスター
        clu_type = CLU_META if node['meta_type'] == 'meta' else CLU_WMETA
        buf = w_u8(buf, clu_type)
        buf = w_u8(buf, MOD_NONE)  
        buf = w_str(buf, '')      

    elif 'header_name' in node:
        buf = w_u8(buf, CLU_HEADER)
        buf = w_u8(buf, MOD_NONE)
        buf = w_str(buf, node['header_name'])

    else:
        buf = w_u8(buf, CLU_NORMAL)
        mods = node.get('modifier', [])
        mod = 0
        if '*' in mods: 
            mod |= MOD_PRIME    
        if 'rejected' in mods: 
            mod |= MOD_REJECTED
        buf = w_u8(buf, mod)

    inh = node.get('inheritance') or []  
    buf = w_u32(buf, len(inh))          
    for entry in inh:
        flags = 0
        name  = entry[-1]               
        for f in entry[:-1]:
            if f == '*': flags |= 0x01  
            if f == '?': flags |= 0x02   
        buf = w_u8(buf, flags)
        buf = w_str(buf, name)


    data = node.get('data') or node.get('meta_data') or []
    is_diff = len(data) > 0 and 'diff_name' in data[0]

    if is_diff:
        buf = w_u32(buf, 0)           
        buf = w_u32(buf, len(data))  
        for diff in data:
            buf = w_str(buf, diff['diff_name'])            
            buf = w_u32(buf, len(diff['diff_content']))     
            for datum in diff['diff_content']:
                buf = write_datum(buf, datum)                
    else:
        buf = w_u32(buf, len(data))   
        for datum in data:
            buf = write_datum(buf, datum)
        buf = w_u32(buf, 0)           

    return buf


# -------------------------------------------------------
# メインのシリアライザ
# -------------------------------------------------------

def write_nuqlb(parsed: list, output_path: str):
    """
    parse_nuql()のパース結果（list）を受け取る

      [header]
        magic:     4bytes  'NUQL'
        version:   uint8   バージョン番号
        cc_mode:   uint8   0=ncc（大文字小文字区別なし）/ 1=cc
        clu_count: uint32  クラスター総数
        offsets:   uint64 × clu_count 

      [cluster_data × clu_count]
        write_cluster()の出力

    C++側がクラスター名からオフセットを引く
    """

    version  = '1.0'   
    cc_mode  = 0      
    clusters = []      

    for node in parsed:
        if isinstance(node, tuple):
            version = node[0] or '1.0'
            cc_mode = 1 if node[1] == 'cc' else 0
        else:
            clusters.append(node)

    clu_count = len(clusters)

    clu_bufs = []
    for clu in clusters:
        buf = bytearray()
        buf = write_cluster(buf, clu)
        clu_bufs.append(bytes(buf)) 

    header_size = 4 + 1 + 1 + 4 + 8 * clu_count

    offsets = []
    cursor  = header_size 
    for buf in clu_bufs:
        offsets.append(cursor)  
        cursor += len(buf)      

    total_size = cursor 

    with open(output_path, 'wb') as f:
        f.write(b'\x00' * total_size)

    with open(output_path, 'r+b') as f:
        mm = mmap.mmap(f.fileno(), total_size)

        # ヘッダーを書く
        mm.write(MAGIC)                         
        mm.write(struct.pack('<B', 1))           
        mm.write(struct.pack('<B', cc_mode))    
        mm.write(struct.pack('<I', clu_count))   

        for off in offsets:
            mm.write(struct.pack('<Q', off))     

        for buf in clu_bufs:
            mm.write(buf)

        mm.close()

    print(f"[nuqlb] written: {output_path} ({total_size} bytes, {clu_count} clusters)")
