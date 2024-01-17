from pcapng import FileScanner
from pcapng.blocks import EnhancedPacket
import struct, traceback, sys
from collections import defaultdict
from PIL import Image

class Packet:
    def __init__(self, port, destFlag, vsyncFlag, contents):
        self.port = port
        self.destFlag = destFlag
        self.vsyncFlag = vsyncFlag
        self.contents = contents
        self.seqNum = None
        self.destBitmap = None
        
drawingId = 0

import math

def display(drawing):
    # drawing contains a bunch of 8x8 tiles.
    # Tiles goes from top to bottom, left to right
    # therefore, we have (8*8*4)/8 = 32 bytes for a tile
    global drawingId
    im = Image.new("RGB", (256, math.ceil((len(drawing) - 36) * 8 / (32 * 32))), (127, 127, 127))
    pix = im.load()
    
    palette = [
        (255, 255, 255),
        (0, 0, 0)
    ]
    for n in range(14):
        palette.append(tuple(round(255 * (n + 1) / 15) for _ in range(3)))
    
    tilesPerLine = 32
    tileIndex = 0
    for index in range(36, len(drawing), 32):
        tile = drawing[index:index+32]
        tileX = (tileIndex % tilesPerLine) * 8
        tileY = (tileIndex // tilesPerLine) * 8
        
        for pos, byte in enumerate(tile):
            x = (pos * 2) % 8
            y = (pos * 2) // 8
            
            pix[x+tileX, y+tileY] = palette[byte & 0xF]
            pix[x+tileX+1, y+tileY] = palette[byte >> 4]
        
        tileIndex += 1
        
    im = im.resize((im.width * 4, im.height * 4), Image.NEAREST)
    im.save("drawing_%d.png" % (drawingId))

    drawingId += 1
    return
    
    indexes = []
    print(len(drawing))
    for i, c in enumerate(drawing[24:]):
        a = c & 0xF0
        b = c & 0x0F
        
        if a or b:
            indexes.append(i)
        
        s = ""
        if a: s += "#"
        else: s += " "
        if b: s += "#"
        else: s += " "
        print(s, end="")
        if i % 32 == 0:
            print("|")
            
    print("...")
    print(indexes)
        
def parseBody(body):
    pos = 0
    packets = []
    
    while pos < len(body):
        if len(body) - pos < 2:
            print("corrupt (1)")
            break
            
        header = struct.unpack("<H", body[pos:pos+2])[0]
        pos += 2
        
        size = (header & 0xFF) * 2
        port = (header >> 8) & 0xF
        destFlag = (header & (1<<12)) != 0
        vsyncFlag = (header & (1<<15)) != 0
        
        contents = body[pos:pos+size]
        if len(body) - pos < size + (2 if destFlag else 0) + (2 if (port & 0x8) else 0):
            print("corrupt (size %u, remaining %u)" % (size, len(body) - pos))
            break
            
        pos += size
        pkt = Packet(port, destFlag, vsyncFlag, contents)
            
        if (port & 0x8) != 0:
            seqNum = struct.unpack("<H", body[pos:pos+2])[0]
            pkt.seqNum = seqNum
            pos += 2
            
        if destFlag:
            dstBitmap = struct.unpack("<H", body[pos:pos+2])[0]
            pkt.destBitmap = dstBitmap
            pos += 2
            
        packets.append(pkt)
        
    return packets
    
    
lastSeqNum = None
lastTimestamp = None
drawing = bytearray()

def parseMP(receiver, transmitter, body):
    global timestamp, lastTimestamp
    global lastSeqNum
    global drawing
    
    txop, bitmap = struct.unpack("<HH", body[:4])
    packets = parseBody(body[4:])
    
    for packet in packets:
        if packet.port == 14:
            if lastSeqNum != None and packet.seqNum != lastSeqNum + 1:
                continue
                
            if lastTimestamp != None and abs(timestamp - lastTimestamp) > .5e6:
                print()
                
            lastTimestamp = timestamp
                
            lastSeqNum = packet.seqNum
            #print(packet.contents[:16])
            
            unk1, length, unk3, unk4, offset, unk6 = struct.unpack("<HHHHHH", packet.contents[:12])
            
            if offset == len(drawing):
                drawing.extend(packet.contents[12:])
            elif offset < len(drawing):
                display(drawing)
                drawing = bytearray(packet.contents[12:])
            else:
                print("** drawing has missing data **")
                drawing = bytearray(b"\0" * offset + packet.contents[12:])
            
            assert length == len(packet.contents)
            print("%.2f" % (timestamp * 1e-6), packet.seqNum, "|", unk1, unk3, unk4, unk6)
            
            """
            image = b""
            data = packet.contents[4:]
            print(len(data))
            for index, byte in enumerate(data):
                print(format(byte, "b").rjust(8, "0").replace("0", " "), end="")
                if index % 8 == 0:
                    print()
                
            print()
            """
    
def parseMPKey(receiver, transmitter, body):
    packets = parseBody(body)
    #for packet in packets:
    #    if packet.port not in (0, 12, 14):
    #        print(packet.port)
            
    #for packet in packets:
    #    print(transmitter.hex(":"), packet.port, packet.seqNum)
    

with open('pictochat.pcapng', 'rb') as fp:
    scanner = FileScanner(fp)
    for block in scanner:
        if isinstance(block, EnhancedPacket):
            timestamp = int.from_bytes(block.packet_data[0x10:0x18], "little")
            
            data = block.packet_data
            rtap = struct.unpack("<H", data[2:4])[0]
            packet = data[rtap:]
                
            fcs = struct.unpack("<H", packet[:2])[0]
            type = (fcs >> 2) & 0x3
            subtype = (fcs >> 4) & 0xF
            adrs1 = packet[4:10]
            adrs2 = packet[10:16]
            adrs3 = packet[16:22]
            
            toDS = (fcs & 0x100) != 0
            fromDS = (fcs & 0x200) != 0
            
            if not toDS and not fromDS:
                receiver, transmitter, bssid = adrs1, adrs2, adrs3
            elif not toDS and fromDS:
                receiver, bssid, transmitter = adrs1, adrs2, adrs3
            elif toDS and not fromDS:
                bssid, transmitter, receiver = adrs1, adrs2, adrs3
            elif toDS and fromDS:
                print("Cannot parse this packet! (toDS = fromDS = 1)")
                continue
            
            if receiver == b"\x03\x09\xBF\x00\x00\x00":
                body = packet[24:-4]
                #print("MP_ADRS", type, subtype, bssid.hex(":"))
                parseMP(receiver, transmitter, body)
                
            elif receiver == b"\x03\x09\xBF\x00\x00\x10":
                body = packet[24:-4]
                #print("MPKEY_ADRS", type, subtype, bssid.hex(":"))
                parseMPKey(receiver, transmitter, body)
                
            else:
                #print(receiver.hex(":"), transmitter.hex(":"), bssid.hex(":"))
                pass
                

display(drawing)