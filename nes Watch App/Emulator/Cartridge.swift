import Foundation

enum Mirroring {
    case horizontal
    case vertical
}

final class Cartridge {
    let prgROM: [UInt8]
    var chrROM: [UInt8]
    let mapperID: UInt8
    let mirroring: Mirroring
    let hasChrRam: Bool
    private let mapper: Mapper

    init?(data: Data) {
        guard data.count >= 16 else { return nil }
        let header = [UInt8](data.prefix(16))
        guard header[0] == 0x4E, header[1] == 0x45, header[2] == 0x53, header[3] == 0x1A else {
            return nil
        }

        let prgBanks = Int(header[4])
        let chrBanks = Int(header[5])
        let flags6 = header[6]
        let flags7 = header[7]

        mapperID = (flags7 & 0xF0) | (flags6 >> 4)
        mirroring = (flags6 & 0x01) == 0 ? .horizontal : .vertical

        var offset = 16
        if (flags6 & 0x04) != 0 {
            offset += 512
        }

        let prgSize = prgBanks * 16 * 1024
        let chrSize = chrBanks * 8 * 1024
        let prgStart = offset
        let chrStart = prgStart + prgSize

        guard data.count >= chrStart + chrSize else { return nil }

        prgROM = [UInt8](data[prgStart..<prgStart + prgSize])
        if chrSize > 0 {
            chrROM = [UInt8](data[chrStart..<chrStart + chrSize])
            hasChrRam = false
        } else {
            chrROM = [UInt8](repeating: 0, count: 8 * 1024)
            hasChrRam = true
        }

        if mapperID == 0 {
            mapper = MapperNROM(prgBanks: prgBanks, chrBanks: chrBanks)
        } else {
            return nil
        }
    }

    func cpuRead(_ addr: UInt16) -> UInt8? {
        guard let mapped = mapper.mapCpuRead(addr) else { return nil }
        return prgROM[mapped]
    }

    func cpuWrite(_ addr: UInt16, _ data: UInt8) -> Bool {
        return mapper.mapCpuWrite(addr) != nil
    }

    func ppuRead(_ addr: UInt16) -> UInt8? {
        guard let mapped = mapper.mapPpuRead(addr) else { return nil }
        return chrROM[mapped]
    }

    func ppuWrite(_ addr: UInt16, _ data: UInt8) -> Bool {
        guard let mapped = mapper.mapPpuWrite(addr) else { return false }
        if hasChrRam {
            chrROM[mapped] = data
            return true
        }
        return false
    }
}

protocol Mapper {
    func mapCpuRead(_ addr: UInt16) -> Int?
    func mapCpuWrite(_ addr: UInt16) -> Int?
    func mapPpuRead(_ addr: UInt16) -> Int?
    func mapPpuWrite(_ addr: UInt16) -> Int?
}
