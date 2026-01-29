import Foundation

final class Bus {
    var cpu: CPU6502?
    var ppu: PPU?
    var cartridge: Cartridge?
    var controller: Controller = Controller()

    private var cpuRam: [UInt8] = Array(repeating: 0, count: 2048)

    func cpuRead(_ addr: UInt16) -> UInt8 {
        if let cart = cartridge, let data = cart.cpuRead(addr) {
            return data
        }

        switch addr {
        case 0x0000...0x1FFF:
            return cpuRam[Int(addr & 0x07FF)]
        case 0x2000...0x3FFF:
            return ppu?.cpuRead(addr: 0x2000 + (addr & 0x0007)) ?? 0
        case 0x4016:
            return controller.read()
        case 0x4017:
            return 0
        default:
            return 0
        }
    }

    func cpuWrite(_ addr: UInt16, _ data: UInt8) {
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
        default:
            break
        }
    }
}
