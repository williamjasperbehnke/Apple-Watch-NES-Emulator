import Foundation

final class MapperNROM: Mapper {
    private let prgBanks: Int
    private let chrBanks: Int

    init(prgBanks: Int, chrBanks: Int) {
        self.prgBanks = prgBanks
        self.chrBanks = chrBanks
    }

    func mapCpuRead(_ addr: UInt16) -> Int? {
        guard addr >= 0x8000 else { return nil }
        if prgBanks > 1 {
            return Int(addr & 0x7FFF)
        }
        return Int(addr & 0x3FFF)
    }

    func mapCpuWrite(_ addr: UInt16) -> Int? {
        guard addr >= 0x8000 else { return nil }
        if prgBanks > 1 {
            return Int(addr & 0x7FFF)
        }
        return Int(addr & 0x3FFF)
    }

    func mapPpuRead(_ addr: UInt16) -> Int? {
        guard addr < 0x2000 else { return nil }
        return Int(addr)
    }

    func mapPpuWrite(_ addr: UInt16) -> Int? {
        guard addr < 0x2000 else { return nil }
        if chrBanks == 0 {
            return Int(addr)
        }
        return nil
    }
}
