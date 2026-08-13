#!/usr/bin/env python3
from __future__ import annotations

import re, sys, zlib
from pathlib import Path
from urllib.parse import unquote_to_bytes

MAX_WIDGETS=48; MAX_ASSETS=48; MAX_BYTES=294912; MAX_RUNTIME_ASSET_BYTES=196608
BINDINGS={"NONE","SOC","SOH","VOLTAGE","CURRENT","POWER","REMAINING_AH","TOTAL_AH","RANGE_KM","MOS_TEMP","DELTA_V","MAX_CELL_V","MIN_CELL_V","AVG_CELL_V","CYCLE_COUNT","CELL_COUNT","MAX_CELL_INDEX","MIN_CELL_INDEX","CHARGE_MOS","DISCHARGE_MOS","BALANCER","CONNECTED"}
SWITCH_BINDINGS={"CHARGE_MOS","DISCHARGE_MOS","CONNECTED"}
FONTS={"M14","M16","M22","M32","M48","CN16"}; ALIGNS={"L","C","R","LEFT","CENTER","RIGHT"}
TOKEN_RE=re.compile(r"^[A-Za-z0-9_-]{1,31}$"); ID_RE=re.compile(r"^[A-Za-z0-9_-]{1,20}$"); COLOR_RE=re.compile(r"^#?[0-9A-Fa-f]{6}$"); HEX_RE=re.compile(r"^[0-9A-Fa-f]+$")

def iv(v,lo,hi):
    x=int(v)
    if not lo<=x<=hi: raise ValueError(f"integer {x} outside [{lo},{hi}]")
    return x

def fv(v,lo,hi):
    x=float(v)
    if not lo<=x<=hi: raise ValueError(f"float {x} outside [{lo},{hi}]")
    return x

def color(v):
    if not COLOR_RE.fullmatch(v): raise ValueError(f"invalid color {v!r}")

def text(v,max_bytes):
    raw=unquote_to_bytes(v)
    if len(raw)>max_bytes: raise ValueError(f"decoded text exceeds {max_bytes} bytes")
    return raw.decode("utf-8")

def rect(p):
    iv(p[2],-320,640);iv(p[3],-240,480);iv(p[4],1,640);iv(p[5],1,480)

def opacity(v): iv(v,0,100)
def cap(v):
    if v.upper() not in {"ROUND","SQUARE","STRAIGHT"}: raise ValueError("cap style must be ROUND/SQUARE")

def validate(path:Path):
    raw=path.read_bytes()
    if not 0<len(raw)<=MAX_BYTES: raise ValueError(f"package size {len(raw)} outside 1..{MAX_BYTES}")
    lines=raw.decode("utf-8-sig").splitlines(); version=0; title=""; header=False; widgets=0
    ids=set(); assets={}; refs=[]; glyph_refs=[]; total_asset_bytes=0
    for line_no,source in enumerate(lines,1):
        if len(source.encode())>1024: raise ValueError(f"line {line_no}: line exceeds 1024 bytes")
        line=source.strip()
        if not line or line.startswith("#"): continue
        p=line.split("|"); kind=p[0].upper()
        try:
            if not header:
                if len(p)!=6 or kind!="BMSUI": raise ValueError("invalid header")
                version=iv(p[1],1,7)
                if iv(p[2],1,4096)!=320 or iv(p[3],1,4096)!=240: raise ValueError("unsupported canvas")
                color(p[4]); title=text(p[5],63); header=True; continue
            if kind=="ASSET":
                if version<2 or len(p)!=7: raise ValueError("invalid ASSET")
                name=p[1]
                if not TOKEN_RE.fullmatch(name) or name.lower() in {k.lower() for k in assets}: raise ValueError("invalid/duplicate asset name")
                maxw,maxh=(320,240) if version>=3 else (64,64)
                w=iv(p[2],1,maxw);h=iv(p[3],1,maxh)
                fmt=p[4].upper()
                if version>=7:
                    allowed_formats={"RGB565A8","RGB565","A8"}
                elif version>=3:
                    allowed_formats={"RGB565A8","A8"}
                else:
                    allowed_formats={"RGB565A8"}
                if fmt not in allowed_formats: raise ValueError("unsupported asset format")
                bc=iv(p[5],1,MAX_RUNTIME_ASSET_BYTES)
                expected=w*h*(1 if fmt=="A8" else (2 if fmt=="RGB565" else 3))
                if bc!=expected: raise ValueError("asset byte count does not match dimensions/format")
                crc=int(p[6],16)
                assets[name]={"w":w,"h":h,"format":fmt,"bytes":bc,"crc":crc,"data":bytearray()};total_asset_bytes+=bc
                if len(assets)>MAX_ASSETS or total_asset_bytes>MAX_RUNTIME_ASSET_BYTES: raise ValueError("embedded assets exceed runtime limit")
                continue
            if kind=="ASSETDATA":
                if version<2 or len(p)!=3 or p[1] not in assets: raise ValueError("invalid ASSETDATA")
                if not p[2] or len(p[2])%2 or not HEX_RE.fullmatch(p[2]): raise ValueError("invalid ASSETDATA hex")
                chunk=bytes.fromhex(p[2]);a=assets[p[1]]
                if len(a["data"])+len(chunk)>a["bytes"]: raise ValueError("ASSETDATA exceeds declared size")
                a["data"].extend(chunk);continue
            if kind=="ASSETB64":
                if version<7 or len(p)!=3 or p[1] not in assets: raise ValueError("invalid ASSETB64")
                import base64
                try: chunk=base64.b64decode(p[2], validate=True)
                except Exception as exc: raise ValueError("invalid ASSETB64 data") from exc
                a=assets[p[1]]
                if len(a["data"])+len(chunk)>a["bytes"]: raise ValueError("ASSETB64 exceeds declared size")
                a["data"].extend(chunk);continue
            if widgets>=MAX_WIDGETS: raise ValueError("too many widgets")
            if len(p)<2 or not ID_RE.fullmatch(p[1]) or p[1].lower() in ids: raise ValueError("invalid/duplicate widget id")
            ids.add(p[1].lower());widgets+=1
            if kind=="LABEL":
                if len(p) not in ({14,15} if version>=3 else {14}): raise ValueError("invalid LABEL field count")
                rect(p)
                if p[6].upper() not in FONTS or p[8].upper() not in ALIGNS or p[9].upper() not in BINDINGS: raise ValueError("invalid LABEL metadata")
                color(p[7]);iv(p[10],0,3);text(p[11],40);text(p[12],40);text(p[13],160)
                if len(p)==15: opacity(p[14])
            elif kind=="RECT":
                if len(p) not in ({10,11} if version>=3 else {10}): raise ValueError("invalid RECT field count")
                rect(p);color(p[6]);color(p[7]);iv(p[8],0,16);iv(p[9],0,64)
                if len(p)==11:opacity(p[10])
            elif kind=="LINE":
                if len(p) not in ({9,10} if version>=3 else {9}): raise ValueError("invalid LINE field count")
                iv(p[2],-320,640);iv(p[3],-240,480);iv(p[4],-320,640);iv(p[5],-240,480);color(p[6]);iv(p[7],1,16)
                if p[8].upper()!="SOLID":raise ValueError("line style")
                if len(p)==10:opacity(p[9])
            elif kind=="ARC":
                if len(p) not in ({13,15,16,18} if version>=4 else ({13,15,16} if version>=3 else {13})):raise ValueError("invalid ARC field count")
                rect(p);lo=fv(p[6],-1e6,1e6);hi=fv(p[7],-1e6,1e6)
                if hi<=lo or (version>=2 and p[8].upper()!="SOC"):raise ValueError("invalid ARC range/binding")
                color(p[9])
                if len(p) in {16,18}:
                    color(p[10]);iv(p[11],1,32);a=iv(p[12],-720,720);b=iv(p[13],-720,720);cap(p[14]);opacity(p[15])
                    if len(p)==18:
                        iv(p[16],0,1);color(p[17])
                else:
                    iv(p[10],1,32);a=iv(p[11],-720,720);b=iv(p[12],-720,720)
                    if len(p)==15:cap(p[13]);opacity(p[14])
                if b<=a or b-a>=360:raise ValueError("invalid ARC angles")
            elif kind=="BAR":
                if len(p) not in ({12,14,17} if version>=4 else ({12,14} if version>=3 else {12})):raise ValueError("invalid BAR field count")
                rect(p);lo=fv(p[6],-1e6,1e6);hi=fv(p[7],-1e6,1e6)
                allowed_bar={"SOC","REMAINING_AH","RANGE_KM"}
                if hi<=lo or (version>=2 and p[8].upper() not in allowed_bar):raise ValueError("invalid BAR range/binding")
                color(p[9]);color(p[10]);iv(p[11],0,64)
                if len(p) in {14,17}:cap(p[12]);opacity(p[13])
                if len(p)==17:
                    iv(p[14],0,1);color(p[15])
                    if p[16].upper() not in {"AUTO","H","V","HORIZONTAL","VERTICAL"}:raise ValueError("gradient direction")
            elif kind=="STATEIMG":
                if version<2 or len(p) not in ({9,10} if version>=3 else {9}):raise ValueError("invalid STATEIMG")
                rect(p);w=iv(p[4],8,64);h=iv(p[5],8,64)
                if p[6].upper() not in SWITCH_BINDINGS or not TOKEN_RE.fullmatch(p[7]) or not TOKEN_RE.fullmatch(p[8]):raise ValueError("invalid STATEIMG metadata")
                if len(p)==10:opacity(p[9]);refs += [(p[7],w,h),(p[8],w,h)]
                else: refs += [(p[7],w,h),(p[8],w,h)]
            elif kind=="STATEMASK":
                if version<7 or len(p)!=12:raise ValueError("invalid STATEMASK")
                rect(p);w=iv(p[4],1,320);h=iv(p[5],1,240)
                if p[6].upper() not in SWITCH_BINDINGS or not TOKEN_RE.fullmatch(p[7]) or not TOKEN_RE.fullmatch(p[8]):raise ValueError("invalid STATEMASK metadata")
                color(p[9]);color(p[10]);opacity(p[11]);refs += [(p[7],w,h),(p[8],w,h)]
            elif kind=="MASKIMG":
                if version<7 or len(p)!=9:raise ValueError("invalid MASKIMG")
                rect(p);asset=p[6];opacity(p[7]);color(p[8])
                if not TOKEN_RE.fullmatch(asset):raise ValueError("invalid MASKIMG asset")
                refs.append((asset,iv(p[4],1,320),iv(p[5],1,240)))
            elif kind=="GLYPHVAL":
                if version<6: raise ValueError("GLYPHVAL requires v6")
                if len(p)!=21: raise ValueError("GLYPHVAL needs 21 fields")
                rect(p)
                asset=p[6]
                if not TOKEN_RE.fullmatch(asset): raise ValueError("invalid glyph asset")
                opacity(p[7]); color(p[8])
                if p[9].upper() not in ALIGNS or p[10].upper() not in BINDINGS or p[10].upper()=="NONE": raise ValueError("invalid GLYPHVAL metadata")
                iv(p[11],0,3); text(p[12],40); text(p[13],40); text(p[14],80)
                iv(p[15],5,96); iv(p[16],0,1)
                cellw=iv(p[17],1,120); cols=iv(p[18],1,48)
                cps=[x for x in p[19].split(",") if x]
                adv=[x for x in p[20].split(",") if x]
                if not cps or len(cps)>48 or len(cps)!=len(adv): raise ValueError("invalid glyph metadata")
                for cp in cps:
                    v=int(cp,16)
                    if v<=0 or v>0x10FFFF: raise ValueError("invalid glyph codepoint")
                for a in adv: iv(a,1,120)
                if cols>len(cps): cols=len(cps)
                rows=(len(cps)+cols-1)//cols
                glyph_refs.append((asset,cellw*cols,rows*iv(p[5],1,480)))
            elif kind=="VSHAPE":
                if version<5: raise ValueError("VSHAPE requires v5")
                if len(p)!=14: raise ValueError("VSHAPE needs 14 fields")
                rect(p)
                if p[6] not in {"RECT","CIRCLE"}: raise ValueError("invalid VSHAPE type")
                if p[7] not in {"SQUARE","ROUND"}: raise ValueError("invalid VSHAPE corner")
                iv(p[8],0,1)
                color(p[9]); color(p[10])
                iv(p[11],0,16); iv(p[12],0,120); iv(p[13],0,100)
            elif kind in {"IMAGE","TEXTIMG","SHAPE"}:
                if version<3:raise ValueError(f"{kind} requires v3")
                rect(p)
                if kind=="IMAGE":
                    if len(p)!=8:raise ValueError("IMAGE needs 8 fields")
                    asset,op=p[6],p[7]
                elif kind=="TEXTIMG":
                    if len(p)!=14:raise ValueError("TEXTIMG needs 14 fields")
                    asset,op=p[6],p[7];text(p[8],80);iv(p[9],6,200);iv(p[10],0,1)
                    if p[11].upper() not in ALIGNS:raise ValueError("TEXTIMG alignment")
                    color(p[12]);text(p[13],160)
                else:
                    if len(p)!=16:raise ValueError("SHAPE needs 16 fields")
                    asset,op=p[6],p[7]
                    if p[8].upper() not in {"RECT","ELLIPSE","CIRCLE"}:raise ValueError("shape type")
                    if p[9].upper() not in {"SQUARE","ROUND","CHAMFER"}:raise ValueError("corner style")
                    iv(p[10],0,1);color(p[11]);color(p[12]);iv(p[13],0,32);iv(p[14],0,64);iv(p[15],0,64)
                if not TOKEN_RE.fullmatch(asset):raise ValueError("invalid asset reference")
                opacity(op);refs.append((asset,iv(p[4],1,320),iv(p[5],1,240)))
            else: raise ValueError(f"unknown widget type {kind}")
        except Exception as exc:
            raise ValueError(f"line {line_no}: {exc}") from exc
    if not header or widgets==0:raise ValueError("missing header/widgets")
    for name,a in assets.items():
        if len(a["data"])!=a["bytes"]:raise ValueError(f"asset {name}: data incomplete")
        if zlib.crc32(a["data"]) & 0xffffffff != a["crc"]:raise ValueError(f"asset {name}: CRC mismatch")
    for name,w,h in refs:
        a=assets.get(name)
        if not a or a["w"]!=w or a["h"]!=h:raise ValueError(f"asset reference {name}: missing/dimension mismatch")
    for name,w,h in glyph_refs:
        a=assets.get(name)
        if not a or a["format"]!="A8" or a["w"]!=w or a["h"]!=h:raise ValueError(f"glyph asset {name}: missing/dimension mismatch")
    return title,widgets,len(assets),len(raw),version

if __name__=="__main__":
    if len(sys.argv)!=2:raise SystemExit("usage: validate_bmsui.py <file.bmsui>")
    try:
        title,widgets,assets,size,version=validate(Path(sys.argv[1]))
        print(f"VALID: v{version}, title={title!r}, widgets={widgets}, assets={assets}, bytes={size}")
    except Exception as exc:
        print(f"INVALID: {exc}",file=sys.stderr);raise SystemExit(2)
