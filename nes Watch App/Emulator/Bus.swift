final class Bus {
    var cpu: CPU6502?
    var ppu: PPU?
    var cartridge: Cartridge?
    var controller: Controller = Controller()

    private var cpuRam: [UInt8] = Array(repeating: 0, count: 2048)
    private var dataBus: UInt8 = 0
    private var irqPending: Bool = false

    func cpuReadOpcode(_ addr: UInt16) -> UInt8 {
        return cpuReadInternal(addr, isOpcodeFetch: true)
    }

    func cpuRead(_ addr: UInt16) -> UInt8 {
        return cpuReadInternal(addr, isOpcodeFetch: false)
    }

    private func cpuReadInternal(_ addr: UInt16, isOpcodeFetch: Bool) -> UInt8 {
        var data: UInt8 = 0
        let isOpenBus = isOpenBusAddr(addr)
        let isUnmappedOpenBus = isOpenBus && !(0x2000...0x3FFF).contains(addr) && !(0x4000...0x401F).contains(addr)
        if isUnmappedOpenBus {
            return dataBus
        }

        if let cart = cartridge, let cartData = cart.cpuRead(addr) {
            data = cartData
            dataBus = data
            return data
        }

        switch addr {
        case 0x0000...0x1FFF:
            data = cpuRam[Int(addr & 0x07FF)]
            dataBus = data
            return data
        case 0x2000...0x3FFF:
            let reg = 0x2000 + (addr & 0x0007)
            data = ppu?.cpuRead(addr: reg) ?? dataBus
            dataBus = data
            return data
        case 0x4016:
            let value = controller.read()
            data = (dataBus & 0xE0) | (value & 0x01)
            dataBus = data
            return data
        case 0x4017:
            data = dataBus
            dataBus = data
            return data
        case 0x4015:
            // APU status not implemented; treat as open bus and do not update data bus.
            data = dataBus
            return data
        default:
            data = dataBus
            dataBus = data
            return data
        }
    }

    func cpuWrite(_ addr: UInt16, _ data: UInt8) {
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
            // OAM DMA not implemented yet.
            break
        case 0x4016:
            controller.write(data)
        case 0x4017:
            // Minimal frame IRQ hook for test ROMs.
            let irqEnabled = (data & 0x40) == 0
            irqPending = irqEnabled
        default:
            break
        }
    }

    func isIrqPending() -> Bool {
        return irqPending
    }

    func acknowledgeIrq() {
        irqPending = false
    }

    func tick(cycles: Int) {
        _ = cycles
    }

    private func isOpenBusAddr(_ addr: UInt16) -> Bool {
        if (0x2000...0x3FFF).contains(addr) { return true }
        if (0x4000...0x401F).contains(addr) { return true }
        if addr < 0x2000 { return false }
        if addr >= 0x8000 { return false }
        return true
    }
}
