final class PPU {
    private(set) var frameBuffer = FrameBuffer()

    private var cartridge: Cartridge?
    private var mirroring: Mirroring = .horizontal
    private var dataBus: UInt8 = 0

    private var ctrl: UInt8 = 0
    private var mask: UInt8 = 0
    private var status: UInt8 = 0

    private var addressLatch: Bool = false
    private var vramAddr: UInt16 = 0
    private var readBuffer: UInt8 = 0

    private var cycle: Int = 0
    private var scanline: Int = 0
    private(set) var frameComplete: Bool = false
    private(set) var nmiRequested: Bool = false

    private var nametableRam: [UInt8] = Array(repeating: 0, count: 2048)
    private var paletteRam: [UInt8] = Array(repeating: 0, count: 32)

    private let nesPalette: [UInt32] = [
        0xFF7C7C7C, 0xFF0000FC, 0xFF0000BC, 0xFF4428BC, 0xFF940084, 0xFFA80020, 0xFFA81000, 0xFF881400,
        0xFF503000, 0xFF007800, 0xFF006800, 0xFF005800, 0xFF004058, 0xFF000000, 0xFF000000, 0xFF000000,
        0xFFBCBCBC, 0xFF0078F8, 0xFF0058F8, 0xFF6844FC, 0xFFD800CC, 0xFFE40058, 0xFFF83800, 0xFFE45C10,
        0xFFAC7C00, 0xFF00B800, 0xFF00A800, 0xFF00A844, 0xFF008888, 0xFF000000, 0xFF000000, 0xFF000000,
        0xFFF8F8F8, 0xFF3CBCFC, 0xFF6888FC, 0xFF9878F8, 0xFFF878F8, 0xFFF85898, 0xFFF87858, 0xFFFCA044,
        0xFFF8B800, 0xFFB8F818, 0xFF58D854, 0xFF58F898, 0xFF00E8D8, 0xFF787878, 0xFF000000, 0xFF000000,
        0xFFFCFCFC, 0xFFA4E4FC, 0xFFB8B8F8, 0xFFD8B8F8, 0xFFF8B8F8, 0xFFF8A4C0, 0xFFF0D0B0, 0xFFFCE0A8,
        0xFFF8D878, 0xFFD8F878, 0xFFB8F8B8, 0xFFB8F8D8, 0xFF00FCFC, 0xFFF8D8F8, 0xFF000000, 0xFF000000
    ]

    func connectCartridge(_ cartridge: Cartridge) {
        self.cartridge = cartridge
        self.mirroring = cartridge.mirroring
    }

    func resetFrameState() {
        frameComplete = false
    }

    func cpuRead(addr: UInt16) -> UInt8 {
        switch addr {
        case 0x2002:
            let value = (status & 0xE0) | (dataBus & 0x1F)
            status &= 0x7F
            addressLatch = false
            dataBus = value
            return value
        case 0x2007:
            let value: UInt8
            if vramAddr >= 0x3F00 {
                value = ppuReadMemory(vramAddr)
                readBuffer = ppuReadMemory(vramAddr &- 0x1000)
            } else {
                value = readBuffer
                readBuffer = ppuReadMemory(vramAddr)
            }
            vramAddr &+= (ctrl & 0x04) != 0 ? 32 : 1
            dataBus = value
            return value
        default:
            return dataBus
        }
    }

    func cpuWrite(addr: UInt16, data: UInt8) {
        dataBus = data
        switch addr {
        case 0x2000:
            ctrl = data
        case 0x2001:
            mask = data
        case 0x2005:
            addressLatch.toggle()
        case 0x2006:
            if !addressLatch {
                vramAddr = (UInt16(data) << 8)
                addressLatch = true
            } else {
                vramAddr = (vramAddr & 0xFF00) | UInt16(data)
                addressLatch = false
            }
        case 0x2007:
            ppuWriteMemory(vramAddr, data)
            vramAddr &+= (ctrl & 0x04) != 0 ? 32 : 1
        default:
            break
        }
    }

    func tick() {
        nmiRequested = false
        if scanline == 241 && cycle == 1 {
            status |= 0x80
            if (ctrl & 0x80) != 0 {
                nmiRequested = true
            }
        }

        if scanline == 261 && cycle == 1 {
            status &= 0x7F
        }

        cycle += 1
        if cycle >= 341 {
            cycle = 0
            scanline += 1
            if scanline >= 262 {
                scanline = 0
                frameComplete = true
                renderBackground()
            }
        }
    }

    private func renderBackground() {
        let width = FrameBuffer.width
        let height = FrameBuffer.height
        let baseNameTable = UInt16(0x2000 + (Int(ctrl & 0x03) * 0x400))
        let patternBase: UInt16 = (ctrl & 0x10) != 0 ? 0x1000 : 0x0000

        for y in 0..<height {
            let tileY = y / 8
            let fineY = y % 8
            for x in 0..<width {
                let tileX = x / 8
                let fineX = x % 8

                let nameAddr = baseNameTable + UInt16(tileY * 32 + tileX)
                let tileId = ppuReadMemory(nameAddr)
                let patternAddr = patternBase + UInt16(tileId) * 16 + UInt16(fineY)
                let plane0 = ppuReadMemory(patternAddr)
                let plane1 = ppuReadMemory(patternAddr + 8)
                let bit = 7 - fineX
                let color = ((plane1 >> bit) & 0x01) << 1 | ((plane0 >> bit) & 0x01)

                let attrAddr = baseNameTable + 0x03C0 + UInt16((tileY / 4) * 8 + (tileX / 4))
                let attr = ppuReadMemory(attrAddr)
                let quadrantX = (tileX % 4) / 2
                let quadrantY = (tileY % 4) / 2
                let shift = (quadrantY * 2 + quadrantX) * 2
                let palette = Int((attr >> shift) & 0x03)

                let pixelColor = paletteColor(palette: palette, color: Int(color))
                frameBuffer.pixels[y * width + x] = pixelColor
            }
        }
    }

    private func paletteColor(palette: Int, color: Int) -> UInt32 {
        let indexAddr: UInt16
        if color == 0 {
            indexAddr = 0x3F00
        } else {
            indexAddr = 0x3F00 + UInt16(palette * 4 + color)
        }
        let paletteIndex = Int(ppuReadMemory(indexAddr) & 0x3F)
        return nesPalette[paletteIndex % nesPalette.count]
    }

    private func ppuReadMemory(_ addr: UInt16) -> UInt8 {
        let address = addr & 0x3FFF
        if address < 0x2000 {
            return cartridge?.ppuRead(address) ?? 0
        }
        if address < 0x3F00 {
            let index = mirrorNametableAddr(address)
            return nametableRam[index]
        }
        let paletteIndex = mirrorPaletteAddr(address)
        return paletteRam[paletteIndex]
    }

    private func ppuWriteMemory(_ addr: UInt16, _ data: UInt8) {
        let address = addr & 0x3FFF
        if address < 0x2000 {
            _ = cartridge?.ppuWrite(address, data)
            return
        }
        if address < 0x3F00 {
            let index = mirrorNametableAddr(address)
            nametableRam[index] = data
            return
        }
        let paletteIndex = mirrorPaletteAddr(address)
        paletteRam[paletteIndex] = data
    }

    private func mirrorNametableAddr(_ addr: UInt16) -> Int {
        let offset = Int(addr & 0x0FFF)
        switch mirroring {
        case .vertical:
            return offset & 0x07FF
        case .horizontal:
            let table = (offset / 0x400) & 0x03
            let index = offset & 0x03FF
            if table == 0 || table == 1 {
                return index
            }
            return 0x400 + index
        }
    }

    private func mirrorPaletteAddr(_ addr: UInt16) -> Int {
        var index = Int(addr & 0x001F)
        if index == 0x10 { index = 0x00 }
        if index == 0x14 { index = 0x04 }
        if index == 0x18 { index = 0x08 }
        if index == 0x1C { index = 0x0C }
        return index
    }
}
