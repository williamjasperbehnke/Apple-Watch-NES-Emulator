final class Bus {
    var cpu: CPU6502?
    var ppu: PPU?
    var cartridge: Cartridge?
    var controller: Controller = Controller()

    private var cpuRam: [UInt8] = Array(repeating: 0, count: 2048)

    // CPU-side floating data bus latch (this is what "open bus" should return)
    private var dataBus: UInt8 = 0

    private var irqPending: Bool = false
    private var stallCycles: Int = 0

    func cpuReadOpcode(_ addr: UInt16) -> UInt8 {
        cpuReadInternal(addr)
    }

    func cpuRead(_ addr: UInt16) -> UInt8 {
        cpuReadInternal(addr)
    }

    private func cpuReadInternal(_ addr: UInt16) -> UInt8 {
        // Cartridge first (if it responds, it drives the bus)
        if let cart = cartridge, let cartData = cart.cpuRead(addr) {
            dataBus = cartData
            return cartData
        }

        switch addr {
        case 0x0000...0x1FFF:
            let value = cpuRam[Int(addr & 0x07FF)]
            dataBus = value
            return value

        case 0x2000...0x3FFF:
            let reg = 0x2000 + (addr & 0x0007)

            let value = ppu?.cpuRead(addr: reg) ?? dataBus

            dataBus = value
            return value

        case 0x4016:
            let value = controller.read()
            let result = (dataBus & 0xE0) | (value & 0x01)
            dataBus = result
            return result

        case 0x4017:
            // Treat as open bus for now, but still "read" it (keep latch consistent)
            let value = dataBus
            dataBus = value
            return value

        case 0x4015:
            // APU status not implemented: open bus.
            // Still update latch to the returned value (some tests expect read cycles to refresh)
            let value = dataBus
            dataBus = value
            return value

        default:
            // Unmapped / open bus
            return dataBus
        }
    }

    func cpuWrite(_ addr: UInt16, _ data: UInt8) {
        // Any write cycle drives the CPU data bus latch
        dataBus = data

        if let cart = cartridge, cart.cpuWrite(addr, data) {
            return
        }

        switch addr {
        case 0x0000...0x1FFF:
            cpuRam[Int(addr & 0x07FF)] = data

        case 0x2000...0x3FFF:
            ppu?.cpuWrite(addr: 0x2000 + (addr & 0x0007), data: data)

        case 0x4014:
            startDma(page: data)

        case 0x4016:
            controller.write(data)

        case 0x4017:
            let irqEnabled = (data & 0x40) == 0
            irqPending = irqEnabled

        default:
            break
        }
    }

    func isIrqPending() -> Bool { irqPending }
    func acknowledgeIrq() { irqPending = false }

    func tick(cycles: Int) { _ = cycles }

    // DMA
    private var dmaActive: Bool = false
    private var dmaPage: UInt8 = 0
    private var dmaIndex: UInt16 = 0
    private var dmaCycle: Int = 0
    private var dmaData: UInt8 = 0

    private func startDma(page: UInt8) {
        dmaActive = true
        dmaPage = page
        dmaIndex = 0
        dmaCycle = 0
    }

    private func stepDma() {
        if !dmaActive { return }

        if dmaCycle % 2 == 0 {
            let addr = (UInt16(dmaPage) << 8) | dmaIndex
            dmaData = cpuRead(addr)
        } else {
            dmaIndex &+= 1
            if dmaIndex == 256 {
                dmaActive = false
            }
        }
        dmaCycle += 1
    }

    func requestStall(cycles: Int) {
        if cycles > stallCycles { stallCycles = cycles }
    }

    func consumeStallCycle() -> Bool {
        if stallCycles > 0 {
            stallCycles -= 1
            stepDma()
            return true
        }
        return false
    }

    func setCpuDataBus(_ value: UInt8) {
        dataBus = value
    }
}
