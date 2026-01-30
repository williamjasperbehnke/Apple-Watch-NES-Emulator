final class CPU6502 {
    enum Flag: UInt8 {
        case C = 0x01
        case Z = 0x02
        case I = 0x04
        case D = 0x08
        case B = 0x10
        case U = 0x20
        case V = 0x40
        case N = 0x80
    }

    enum AddressingMode {
        case imp
        case imm
        case zp0
        case zpx
        case zpy
        case abs
        case abx
        case aby
        case ind
        case izx
        case izy
        case rel
    }

    enum AccessKind {
        case read
        case write
        case readModifyWrite
        case implied
    }

    struct Instruction {
        let name: String
        let operate: () -> UInt8
        let addrMode: () -> UInt8
        let mode: AddressingMode
        let access: AccessKind
        let cycles: UInt8
    }

    var bus: Bus?

    private(set) var a: UInt8 = 0
    private(set) var x: UInt8 = 0
    private(set) var y: UInt8 = 0
    private(set) var sp: UInt8 = 0xFD
    private(set) var pc: UInt16 = 0
    private(set) var status: UInt8 = Flag.U.rawValue

    private var fetched: UInt8 = 0
    private var addrAbs: UInt16 = 0
    private var addrRel: UInt16 = 0
    private var opcode: UInt8 = 0
    private var baseHigh: UInt8 = 0
    private var cycleCounter: Int = 0

    private var instructions: [Instruction] = []

    init() {
        buildInstructionTable()
    }

    func reset() {
        a = 0
        x = 0
        y = 0
        sp = 0xFD
        status = Flag.U.rawValue | Flag.I.rawValue

        let lo = read(0xFFFC)
        let hi = read(0xFFFD)
        pc = UInt16(hi) << 8 | UInt16(lo)
    }

    func irq() {
        if getFlag(.I) == 0 {
            push(UInt8((pc >> 8) & 0xFF))
            push(UInt8(pc & 0xFF))
            setFlag(.I, true)
            pushStatus(setBreak: false)
            setFlag(.B, false)
            setFlag(.U, true)
            let lo = read(0xFFFE)
            let hi = read(0xFFFF)
            pc = UInt16(hi) << 8 | UInt16(lo)
        }
    }

    func nmi() {
        push(UInt8((pc >> 8) & 0xFF))
        push(UInt8(pc & 0xFF))
        setFlag(.I, true)
        pushStatus(setBreak: false)
        setFlag(.B, false)
        setFlag(.U, true)
        let lo = read(0xFFFA)
        let hi = read(0xFFFB)
        pc = UInt16(hi) << 8 | UInt16(lo)
    }

    func step() -> Int {
        if bus?.consumeStallCycle() == true {
            cycleCounter += 1
            bus?.tick(cycles: 1)
            return 1
        }
        opcode = bus?.cpuReadOpcode(pc) ?? 0
        pc &+= 1

        let instruction = instructions[Int(opcode)]
        let additional1 = instruction.addrMode()
        let additional2 = instruction.operate()
        let cycles = instruction.cycles + (additional1 & additional2)
        cycleCounter += Int(cycles)
        status |= Flag.U.rawValue
        if let bus, bus.isIrqPending(), getFlag(.I) == 0 {
            bus.acknowledgeIrq()
            irq()
        }
        bus?.tick(cycles: Int(cycles))
        return Int(cycles)
    }

    private func read(_ addr: UInt16) -> UInt8 {
        return bus?.cpuRead(addr) ?? 0
    }

    private func write(_ addr: UInt16, _ data: UInt8) {
        if addr == 0x4014 {
            let extra = cycleCounter % 2
            bus?.requestStall(cycles: 513 + extra)
        }
        bus?.cpuWrite(addr, data)
    }

    private func push(_ value: UInt8) {
        write(0x0100 | UInt16(sp), value)
        sp &-= 1
    }

    private func pop() -> UInt8 {
        sp &+= 1
        return read(0x0100 | UInt16(sp))
    }

    private func getFlag(_ flag: Flag) -> UInt8 {
        return (status & flag.rawValue) > 0 ? 1 : 0
    }

    private func setFlag(_ flag: Flag, _ value: Bool) {
        if value {
            status |= flag.rawValue
        } else {
            status &= ~flag.rawValue
        }
    }

    private func setZN(_ value: UInt8) {
        setFlag(.Z, value == 0)
        setFlag(.N, (value & 0x80) != 0)
    }

    private func fetch() -> UInt8 {
        if instructions[Int(opcode)].mode != .imp {
            fetched = read(addrAbs)
        }
        return fetched
    }

    // Addressing modes
    private func IMP() -> UInt8 {
        fetched = a
        return 0
    }

    private func impliedDummyRead() {
        _ = read(pc)
    }

    private func IMM() -> UInt8 {
        addrAbs = pc
        pc &+= 1
        return 0
    }

    private func ZP0() -> UInt8 {
        addrAbs = UInt16(read(pc))
        pc &+= 1
        addrAbs &= 0x00FF
        return 0
    }

    private func ZPX() -> UInt8 {
        addrAbs = UInt16(read(pc) &+ x)
        pc &+= 1
        addrAbs &= 0x00FF
        return 0
    }

    private func ZPY() -> UInt8 {
        addrAbs = UInt16(read(pc) &+ y)
        pc &+= 1
        addrAbs &= 0x00FF
        return 0
    }

    private func ABS() -> UInt8 {
        let lo = read(pc)
        pc &+= 1
        let hi = read(pc)
        pc &+= 1
        baseHigh = hi
        addrAbs = UInt16(hi) << 8 | UInt16(lo)
        return 0
    }

    private func ABX() -> UInt8 {
        let lo = read(pc)
        pc &+= 1
        let hi = read(pc)
        pc &+= 1
        baseHigh = hi
        let base = UInt16(hi) << 8 | UInt16(lo)
        if usesHighByteBugForStore() {
            let low = UInt8(lo) &+ x
            addrAbs = (UInt16(baseHigh) << 8) | UInt16(low)
        } else {
            addrAbs = base &+ UInt16(x)
        }
        let pageCross = (addrAbs & 0xFF00) != (base & 0xFF00)
        let access = instructions[Int(opcode)].access
        if access == .write || (access == .read && pageCross) || access == .readModifyWrite {
            let dummyAddr = (base & 0xFF00) | (addrAbs & 0x00FF)
            _ = read(dummyAddr)
        }
        return (access == .read && pageCross) ? 1 : 0
    }

    private func ABY() -> UInt8 {
        let lo = read(pc)
        pc &+= 1
        let hi = read(pc)
        pc &+= 1
        baseHigh = hi
        let base = UInt16(hi) << 8 | UInt16(lo)
        if usesHighByteBugForStore() {
            let low = UInt8(lo) &+ y
            addrAbs = (UInt16(baseHigh) << 8) | UInt16(low)
        } else {
            addrAbs = base &+ UInt16(y)
        }
        let pageCross = (addrAbs & 0xFF00) != (base & 0xFF00)
        let access = instructions[Int(opcode)].access
        if access == .write || (access == .read && pageCross) || access == .readModifyWrite {
            let dummyAddr = (base & 0xFF00) | (addrAbs & 0x00FF)
            _ = read(dummyAddr)
        }
        return (access == .read && pageCross) ? 1 : 0
    }

    private func IND() -> UInt8 {
        let ptrLo = read(pc)
        pc &+= 1
        let ptrHi = read(pc)
        pc &+= 1
        let ptr = UInt16(ptrHi) << 8 | UInt16(ptrLo)
        let lo = read(ptr)
        let hi = read((ptr & 0xFF00) | UInt16(UInt8(ptr & 0x00FF) &+ 1))
        addrAbs = UInt16(hi) << 8 | UInt16(lo)
        return 0
    }

    private func IZX() -> UInt8 {
        let t = read(pc)
        pc &+= 1
        let lo = read(UInt16(UInt8(t &+ x)))
        let hi = read(UInt16(UInt8(t &+ x &+ 1)))
        addrAbs = UInt16(hi) << 8 | UInt16(lo)
        return 0
    }

    private func IZY() -> UInt8 {
        let t = read(pc)
        pc &+= 1
        let lo = read(UInt16(t))
        let hi = read(UInt16(UInt8(t &+ 1)))
        baseHigh = hi
        let base = UInt16(hi) << 8 | UInt16(lo)
        if usesHighByteBugForStore() {
            let low = UInt8(lo) &+ y
            addrAbs = (UInt16(baseHigh) << 8) | UInt16(low)
        } else {
            addrAbs = base &+ UInt16(y)
        }
        let pageCross = (addrAbs & 0xFF00) != (base & 0xFF00)
        let access = instructions[Int(opcode)].access
        if (access == .write && pageCross) || (access == .read && pageCross) || (access == .readModifyWrite && pageCross) {
            let dummyAddr = (base & 0xFF00) | (addrAbs & 0x00FF)
            _ = read(dummyAddr)
        }
        return (access == .read && pageCross) ? 1 : 0
    }

    private func REL() -> UInt8 {
        addrRel = UInt16(read(pc))
        pc &+= 1
        if addrRel & 0x80 != 0 {
            addrRel |= 0xFF00
        }
        return 0
    }

    // Operations
    private func ADC() -> UInt8 {
        adcWith(fetch())
        return 1
    }

    private func AND() -> UInt8 {
        a &= fetch()
        setZN(a)
        return 1
    }

    private func ASL() -> UInt8 {
        if instructions[Int(opcode)].mode == .imp {
            impliedDummyRead()
        }
        let value = fetch()
        if instructions[Int(opcode)].mode != .imp {
            write(addrAbs, value)
        }
        let result = UInt16(value) << 1
        setFlag(.C, (result & 0xFF00) != 0)
        let output = UInt8(result & 0x00FF)
        setZN(output)
        if instructions[Int(opcode)].mode == .imp {
            a = output
        } else {
            write(addrAbs, output)
        }
        return 0
    }

    private func BCC() -> UInt8 { return branch(getFlag(.C) == 0) }
    private func BCS() -> UInt8 { return branch(getFlag(.C) == 1) }
    private func BEQ() -> UInt8 { return branch(getFlag(.Z) == 1) }
    private func BMI() -> UInt8 { return branch(getFlag(.N) == 1) }
    private func BNE() -> UInt8 { return branch(getFlag(.Z) == 0) }
    private func BPL() -> UInt8 { return branch(getFlag(.N) == 0) }
    private func BVC() -> UInt8 { return branch(getFlag(.V) == 0) }
    private func BVS() -> UInt8 { return branch(getFlag(.V) == 1) }

    private func BIT() -> UInt8 {
        let value = fetch()
        let temp = a & value
        setFlag(.Z, temp == 0)
        setFlag(.V, (value & 0x40) != 0)
        setFlag(.N, (value & 0x80) != 0)
        return 0
    }

    private func BRK() -> UInt8 {
        impliedDummyRead()
        pc &+= 1
        push(UInt8((pc >> 8) & 0xFF))
        push(UInt8(pc & 0xFF))
        setFlag(.B, true)
        pushStatus(setBreak: true)
        setFlag(.B, false)
        setFlag(.I, true)
        let lo = read(0xFFFE)
        let hi = read(0xFFFF)
        pc = UInt16(hi) << 8 | UInt16(lo)
        return 0
    }

    private func CLC() -> UInt8 { impliedDummyRead(); setFlag(.C, false); return 0 }
    private func CLD() -> UInt8 { impliedDummyRead(); setFlag(.D, false); return 0 }
    private func CLI() -> UInt8 { impliedDummyRead(); setFlag(.I, false); return 0 }
    private func CLV() -> UInt8 { impliedDummyRead(); setFlag(.V, false); return 0 }

    private func CMP() -> UInt8 {
        let value = fetch()
        let temp = UInt16(a) &- UInt16(value)
        setFlag(.C, a >= value)
        setZN(UInt8(temp & 0x00FF))
        return 1
    }

    private func CPX() -> UInt8 {
        let value = fetch()
        let temp = UInt16(x) &- UInt16(value)
        setFlag(.C, x >= value)
        setZN(UInt8(temp & 0x00FF))
        return 0
    }

    private func CPY() -> UInt8 {
        let value = fetch()
        let temp = UInt16(y) &- UInt16(value)
        setFlag(.C, y >= value)
        setZN(UInt8(temp & 0x00FF))
        return 0
    }

    private func DEC() -> UInt8 {
        let value = fetch()
        if instructions[Int(opcode)].mode != .imp {
            write(addrAbs, value)
        }
        let result = value &- 1
        write(addrAbs, result)
        setZN(result)
        return 0
    }

    private func DEX() -> UInt8 {
        impliedDummyRead()
        x &-= 1
        setZN(x)
        return 0
    }

    private func DEY() -> UInt8 {
        impliedDummyRead()
        y &-= 1
        setZN(y)
        return 0
    }

    private func EOR() -> UInt8 {
        a ^= fetch()
        setZN(a)
        return 1
    }

    private func INC() -> UInt8 {
        let value = fetch()
        if instructions[Int(opcode)].mode != .imp {
            write(addrAbs, value)
        }
        let result = value &+ 1
        write(addrAbs, result)
        setZN(result)
        return 0
    }

    private func INX() -> UInt8 {
        impliedDummyRead()
        x &+= 1
        setZN(x)
        return 0
    }

    private func INY() -> UInt8 {
        impliedDummyRead()
        y &+= 1
        setZN(y)
        return 0
    }

    private func JMP() -> UInt8 {
        pc = addrAbs
        return 0
    }

    private func JSR() -> UInt8 {
        pc &-= 1
        push(UInt8((pc >> 8) & 0xFF))
        push(UInt8(pc & 0xFF))
        pc = addrAbs

        // AccuracyCoin: JSR leaves the *second operand byte* on the data bus
        bus?.setCpuDataBus(UInt8((addrAbs >> 8) & 0xFF))

        return 0
    }

    private func LDA() -> UInt8 {
        a = fetch()
        setZN(a)
        return 1
    }

    private func LDX() -> UInt8 {
        x = fetch()
        setZN(x)
        return 1
    }

    private func LDY() -> UInt8 {
        y = fetch()
        setZN(y)
        return 1
    }

    private func LSR() -> UInt8 {
        if instructions[Int(opcode)].mode == .imp {
            impliedDummyRead()
        }
        let value = fetch()
        if instructions[Int(opcode)].mode != .imp {
            write(addrAbs, value)
        }
        setFlag(.C, (value & 0x01) != 0)
        let result = value >> 1
        setZN(result)
        if instructions[Int(opcode)].mode == .imp {
            a = result
        } else {
            write(addrAbs, result)
        }
        return 0
    }

    private func NOP() -> UInt8 {
        impliedDummyRead()
        if instructions[Int(opcode)].mode != .imp {
            _ = fetch()
        }
        return 0
    }

    private func NOPR() -> UInt8 {
        impliedDummyRead()
        if instructions[Int(opcode)].mode != .imp {
            _ = fetch()
        }
        return 1
    }

    // Unofficial instructions
    private func SLO() -> UInt8 {
        let value = fetch()
        write(addrAbs, value)
        let result = UInt8((UInt16(value) << 1) & 0x00FF)
        setFlag(.C, (value & 0x80) != 0)
        write(addrAbs, result)
        a |= result
        setZN(a)
        return 0
    }

    private func RLA() -> UInt8 {
        let value = fetch()
        write(addrAbs, value)
        let carryIn = getFlag(.C)
        setFlag(.C, (value & 0x80) != 0)
        let result = UInt8((UInt16(value) << 1) & 0x00FF) | carryIn
        write(addrAbs, result)
        a &= result
        setZN(a)
        return 0
    }

    private func SRE() -> UInt8 {
        let value = fetch()
        write(addrAbs, value)
        setFlag(.C, (value & 0x01) != 0)
        let result = value >> 1
        write(addrAbs, result)
        a ^= result
        setZN(a)
        return 0
    }

    private func RRA() -> UInt8 {
        let value = fetch()
        write(addrAbs, value)
        let carryIn = getFlag(.C)
        setFlag(.C, (value & 0x01) != 0)
        let result = UInt8((UInt16(carryIn) << 7) | UInt16(value >> 1))
        write(addrAbs, result)
        adcWith(result)
        return 0
    }

    private func SAX() -> UInt8 {
        write(addrAbs, a & x)
        return 0
    }

    private func LAX() -> UInt8 {
        let value = fetch()
        a = value
        x = value
        setZN(value)
        return 1
    }

    private func DCP() -> UInt8 {
        let value = fetch()
        write(addrAbs, value)
        let result = value &- 1
        write(addrAbs, result)
        let temp = UInt16(a) &- UInt16(result)
        setFlag(.C, a >= result)
        setZN(UInt8(temp & 0x00FF))
        return 0
    }

    private func ISC() -> UInt8 {
        let value = fetch()
        write(addrAbs, value)
        let result = value &+ 1
        write(addrAbs, result)
        sbcWith(result)
        return 0
    }

    private func ANC() -> UInt8 {
        a &= fetch()
        setZN(a)
        setFlag(.C, (a & 0x80) != 0)
        return 0
    }

    private func ASR() -> UInt8 {
        a &= fetch()
        setFlag(.C, (a & 0x01) != 0)
        a >>= 1
        setZN(a)
        return 0
    }

    private func ARR() -> UInt8 {
        a &= fetch()
        let carryIn = getFlag(.C)
        let result = UInt8((UInt16(carryIn) << 7) | UInt16(a >> 1))
        a = result
        setZN(a)
        setFlag(.C, (a & 0x40) != 0)
        setFlag(.V, ((a >> 5) ^ (a >> 6)) & 0x01 != 0)
        return 0
    }

    private func ANE() -> UInt8 {
        let value = fetch()
        a = (a | 0xEE) & x & value
        setZN(a)
        return 0
    }

    private func LXA() -> UInt8 {
        let value = fetch()
        a = (a | 0xEE) & value
        x = a
        setZN(a)
        return 0
    }

    private func AXS() -> UInt8 {
        let value = fetch()
        let temp = (a & x) &- value
        setFlag(.C, (a & x) >= value)
        x = temp
        setZN(x)
        return 0
    }

    private func SHA() -> UInt8 {
        let high = UInt8((addrAbs >> 8) & 0xFF)
        let value = a & x & (high &+ 1)
        write(addrAbs, value)
        return 0
    }

    private func SHX() -> UInt8 {
        let high = UInt8((addrAbs >> 8) & 0xFF)
        let value = x & (high &+ 1)
        write(addrAbs, value)
        return 0
    }

    private func SHY() -> UInt8 {
        let high = UInt8((addrAbs >> 8) & 0xFF)
        let value = y & (high &+ 1)
        write(addrAbs, value)
        return 0
    }

    private func SHS() -> UInt8 {
        sp = a & x
        let high = UInt8((addrAbs >> 8) & 0xFF)
        let value = sp & (high &+ 1)
        write(addrAbs, value)
        return 0
    }

    private func LAE() -> UInt8 {
        let value = fetch() & sp
        a = value
        x = value
        sp = value
        setZN(value)
        return 1
    }

    private func usesHighByteBugForStore() -> Bool {
        switch opcode {
        case 0x93, 0x9F, 0x9B, 0x9C, 0x9E:
            return true
        default:
            return false
        }
    }



    private func ORA() -> UInt8 {
        a |= fetch()
        setZN(a)
        return 1
    }

    private func PHA() -> UInt8 { impliedDummyRead(); push(a); return 0 }
    private func PHP() -> UInt8 { impliedDummyRead(); pushStatus(setBreak: true); return 0 }
    private func PLA() -> UInt8 { impliedDummyRead(); a = pop(); setZN(a); return 0 }
    private func PLP() -> UInt8 {
        impliedDummyRead()
        status = pop()
        setFlag(.U, true)
        return 0
    }

    private func ROL() -> UInt8 {
        if instructions[Int(opcode)].mode == .imp {
            impliedDummyRead()
        }
        let value = fetch()
        if instructions[Int(opcode)].mode != .imp {
            write(addrAbs, value)
        }
        let result = UInt16(value) << 1 | UInt16(getFlag(.C))
        setFlag(.C, (result & 0xFF00) != 0)
        let output = UInt8(result & 0x00FF)
        setZN(output)
        if instructions[Int(opcode)].mode == .imp {
            a = output
        } else {
            write(addrAbs, output)
        }
        return 0
    }

    private func ROR() -> UInt8 {
        if instructions[Int(opcode)].mode == .imp {
            impliedDummyRead()
        }
        let value = fetch()
        if instructions[Int(opcode)].mode != .imp {
            write(addrAbs, value)
        }
        let result = UInt16(getFlag(.C)) << 7 | UInt16(value >> 1)
        setFlag(.C, (value & 0x01) != 0)
        let output = UInt8(result & 0x00FF)
        setZN(output)
        if instructions[Int(opcode)].mode == .imp {
            a = output
        } else {
            write(addrAbs, output)
        }
        return 0
    }

    private func RTI() -> UInt8 {
        impliedDummyRead()
        status = pop()
        setFlag(.U, true)
        let lo = pop()
        let hi = pop()
        pc = UInt16(hi) << 8 | UInt16(lo)
        return 0
    }

    private func RTS() -> UInt8 {
        impliedDummyRead()
        let lo = pop()
        let hi = pop()
        pc = (UInt16(hi) << 8 | UInt16(lo)) &+ 1
        return 0
    }

    private func SBC() -> UInt8 {
        sbcWith(fetch())
        return 1
    }

    private func SEC() -> UInt8 { impliedDummyRead(); setFlag(.C, true); return 0 }
    private func SED() -> UInt8 { impliedDummyRead(); setFlag(.D, true); return 0 }
    private func SEI() -> UInt8 { impliedDummyRead(); setFlag(.I, true); return 0 }

    private func STA() -> UInt8 { write(addrAbs, a); return 0 }
    private func STX() -> UInt8 { write(addrAbs, x); return 0 }
    private func STY() -> UInt8 { write(addrAbs, y); return 0 }

    private func TAX() -> UInt8 { impliedDummyRead(); x = a; setZN(x); return 0 }
    private func TAY() -> UInt8 { impliedDummyRead(); y = a; setZN(y); return 0 }
    private func TSX() -> UInt8 { impliedDummyRead(); x = sp; setZN(x); return 0 }
    private func TXA() -> UInt8 { impliedDummyRead(); a = x; setZN(a); return 0 }
    private func TXS() -> UInt8 { impliedDummyRead(); sp = x; return 0 }
    private func TYA() -> UInt8 { impliedDummyRead(); a = y; setZN(a); return 0 }

    private func branch(_ condition: Bool) -> UInt8 {
        if condition {
            _ = read(pc)
            let oldPc = pc
            pc &+= addrRel
            if (pc & 0xFF00) != (oldPc & 0xFF00) {
                _ = read((oldPc & 0xFF00) | (pc & 0x00FF))
                return 2
            }
            return 1
        }
        return 0
    }

    private func pushStatus(setBreak: Bool) {
        var flags = status | Flag.U.rawValue
        if setBreak {
            flags |= Flag.B.rawValue
        } else {
            flags &= ~Flag.B.rawValue
        }
        push(flags)
    }

    private func adcWith(_ value: UInt8) {
        let sum = UInt16(a) + UInt16(value) + UInt16(getFlag(.C))
        setFlag(.C, sum > 0xFF)
        setFlag(.Z, UInt8(sum & 0x00FF) == 0)
        setFlag(.V, (~(UInt16(a) ^ UInt16(value)) & (UInt16(a) ^ sum) & 0x0080) != 0)
        setFlag(.N, (sum & 0x80) != 0)
        a = UInt8(sum & 0x00FF)
    }

    private func sbcWith(_ value: UInt8) {
        let inv = value ^ 0xFF
        let sum = UInt16(a) + UInt16(inv) + UInt16(getFlag(.C))
        setFlag(.C, (sum & 0xFF00) != 0)
        setFlag(.Z, UInt8(sum & 0x00FF) == 0)
        setFlag(.V, (sum ^ UInt16(a)) & (sum ^ UInt16(inv)) & 0x0080 != 0)
        setFlag(.N, (sum & 0x80) != 0)
        a = UInt8(sum & 0x00FF)
    }

    private func buildInstructionTable() {
        instructions = Array(
            repeating: Instruction(name: "NOP", operate: NOP, addrMode: IMP, mode: .imp, access: .implied, cycles: 2),
            count: 256
        )

        func accessFor(name: String) -> AccessKind {
            switch name {
            case "STA", "STX", "STY", "SAX", "SHA", "SHX", "SHY", "SHS":
                return .write
            case "ASL", "LSR", "ROL", "ROR", "INC", "DEC", "SLO", "RLA", "SRE", "RRA", "DCP", "ISC":
                return .readModifyWrite
            case "NOP":
                return .read
            default:
                return .read
            }
        }

        func set(_ opcode: UInt8, _ name: String, _ op: @escaping () -> UInt8, _ mode: @escaping () -> UInt8, _ kind: AddressingMode, _ cycles: UInt8) {
            let access = accessFor(name: name)
            instructions[Int(opcode)] = Instruction(name: name, operate: op, addrMode: mode, mode: kind, access: access, cycles: cycles)
        }

        set(0x00, "BRK", BRK, IMM, .imm, 7)
        set(0x01, "ORA", ORA, IZX, .izx, 6)
        set(0x05, "ORA", ORA, ZP0, .zp0, 3)
        set(0x06, "ASL", ASL, ZP0, .zp0, 5)
        set(0x08, "PHP", PHP, IMP, .imp, 3)
        set(0x09, "ORA", ORA, IMM, .imm, 2)
        set(0x0A, "ASL", ASL, IMP, .imp, 2)
        set(0x0D, "ORA", ORA, ABS, .abs, 4)
        set(0x0E, "ASL", ASL, ABS, .abs, 6)

        set(0x10, "BPL", BPL, REL, .rel, 2)
        set(0x11, "ORA", ORA, IZY, .izy, 5)
        set(0x15, "ORA", ORA, ZPX, .zpx, 4)
        set(0x16, "ASL", ASL, ZPX, .zpx, 6)
        set(0x18, "CLC", CLC, IMP, .imp, 2)
        set(0x19, "ORA", ORA, ABY, .aby, 4)
        set(0x1D, "ORA", ORA, ABX, .abx, 4)
        set(0x1E, "ASL", ASL, ABX, .abx, 7)

        set(0x20, "JSR", JSR, ABS, .abs, 6)
        set(0x21, "AND", AND, IZX, .izx, 6)
        set(0x24, "BIT", BIT, ZP0, .zp0, 3)
        set(0x25, "AND", AND, ZP0, .zp0, 3)
        set(0x26, "ROL", ROL, ZP0, .zp0, 5)
        set(0x28, "PLP", PLP, IMP, .imp, 4)
        set(0x29, "AND", AND, IMM, .imm, 2)
        set(0x2A, "ROL", ROL, IMP, .imp, 2)
        set(0x2C, "BIT", BIT, ABS, .abs, 4)
        set(0x2D, "AND", AND, ABS, .abs, 4)
        set(0x2E, "ROL", ROL, ABS, .abs, 6)

        set(0x30, "BMI", BMI, REL, .rel, 2)
        set(0x31, "AND", AND, IZY, .izy, 5)
        set(0x35, "AND", AND, ZPX, .zpx, 4)
        set(0x36, "ROL", ROL, ZPX, .zpx, 6)
        set(0x38, "SEC", SEC, IMP, .imp, 2)
        set(0x39, "AND", AND, ABY, .aby, 4)
        set(0x3D, "AND", AND, ABX, .abx, 4)
        set(0x3E, "ROL", ROL, ABX, .abx, 7)

        set(0x40, "RTI", RTI, IMP, .imp, 6)
        set(0x41, "EOR", EOR, IZX, .izx, 6)
        set(0x45, "EOR", EOR, ZP0, .zp0, 3)
        set(0x46, "LSR", LSR, ZP0, .zp0, 5)
        set(0x48, "PHA", PHA, IMP, .imp, 3)
        set(0x49, "EOR", EOR, IMM, .imm, 2)
        set(0x4A, "LSR", LSR, IMP, .imp, 2)
        set(0x4C, "JMP", JMP, ABS, .abs, 3)
        set(0x4D, "EOR", EOR, ABS, .abs, 4)
        set(0x4E, "LSR", LSR, ABS, .abs, 6)

        set(0x50, "BVC", BVC, REL, .rel, 2)
        set(0x51, "EOR", EOR, IZY, .izy, 5)
        set(0x55, "EOR", EOR, ZPX, .zpx, 4)
        set(0x56, "LSR", LSR, ZPX, .zpx, 6)
        set(0x58, "CLI", CLI, IMP, .imp, 2)
        set(0x59, "EOR", EOR, ABY, .aby, 4)
        set(0x5D, "EOR", EOR, ABX, .abx, 4)
        set(0x5E, "LSR", LSR, ABX, .abx, 7)

        set(0x60, "RTS", RTS, IMP, .imp, 6)
        set(0x61, "ADC", ADC, IZX, .izx, 6)
        set(0x65, "ADC", ADC, ZP0, .zp0, 3)
        set(0x66, "ROR", ROR, ZP0, .zp0, 5)
        set(0x68, "PLA", PLA, IMP, .imp, 4)
        set(0x69, "ADC", ADC, IMM, .imm, 2)
        set(0x6A, "ROR", ROR, IMP, .imp, 2)
        set(0x6C, "JMP", JMP, IND, .ind, 5)
        set(0x6D, "ADC", ADC, ABS, .abs, 4)
        set(0x6E, "ROR", ROR, ABS, .abs, 6)

        set(0x70, "BVS", BVS, REL, .rel, 2)
        set(0x71, "ADC", ADC, IZY, .izy, 5)
        set(0x75, "ADC", ADC, ZPX, .zpx, 4)
        set(0x76, "ROR", ROR, ZPX, .zpx, 6)
        set(0x78, "SEI", SEI, IMP, .imp, 2)
        set(0x79, "ADC", ADC, ABY, .aby, 4)
        set(0x7D, "ADC", ADC, ABX, .abx, 4)
        set(0x7E, "ROR", ROR, ABX, .abx, 7)

        set(0x81, "STA", STA, IZX, .izx, 6)
        set(0x84, "STY", STY, ZP0, .zp0, 3)
        set(0x85, "STA", STA, ZP0, .zp0, 3)
        set(0x86, "STX", STX, ZP0, .zp0, 3)
        set(0x88, "DEY", DEY, IMP, .imp, 2)
        set(0x8A, "TXA", TXA, IMP, .imp, 2)
        set(0x8C, "STY", STY, ABS, .abs, 4)
        set(0x8D, "STA", STA, ABS, .abs, 4)
        set(0x8E, "STX", STX, ABS, .abs, 4)

        set(0x90, "BCC", BCC, REL, .rel, 2)
        set(0x91, "STA", STA, IZY, .izy, 6)
        set(0x94, "STY", STY, ZPX, .zpx, 4)
        set(0x95, "STA", STA, ZPX, .zpx, 4)
        set(0x96, "STX", STX, ZPY, .zpy, 4)
        set(0x98, "TYA", TYA, IMP, .imp, 2)
        set(0x99, "STA", STA, ABY, .aby, 5)
        set(0x9A, "TXS", TXS, IMP, .imp, 2)
        set(0x9D, "STA", STA, ABX, .abx, 5)

        set(0xA0, "LDY", LDY, IMM, .imm, 2)
        set(0xA1, "LDA", LDA, IZX, .izx, 6)
        set(0xA2, "LDX", LDX, IMM, .imm, 2)
        set(0xA4, "LDY", LDY, ZP0, .zp0, 3)
        set(0xA5, "LDA", LDA, ZP0, .zp0, 3)
        set(0xA6, "LDX", LDX, ZP0, .zp0, 3)
        set(0xA8, "TAY", TAY, IMP, .imp, 2)
        set(0xA9, "LDA", LDA, IMM, .imm, 2)
        set(0xAA, "TAX", TAX, IMP, .imp, 2)
        set(0xAC, "LDY", LDY, ABS, .abs, 4)
        set(0xAD, "LDA", LDA, ABS, .abs, 4)
        set(0xAE, "LDX", LDX, ABS, .abs, 4)

        set(0xB0, "BCS", BCS, REL, .rel, 2)
        set(0xB1, "LDA", LDA, IZY, .izy, 5)
        set(0xB4, "LDY", LDY, ZPX, .zpx, 4)
        set(0xB5, "LDA", LDA, ZPX, .zpx, 4)
        set(0xB6, "LDX", LDX, ZPY, .zpy, 4)
        set(0xB8, "CLV", CLV, IMP, .imp, 2)
        set(0xB9, "LDA", LDA, ABY, .aby, 4)
        set(0xBA, "TSX", TSX, IMP, .imp, 2)
        set(0xBC, "LDY", LDY, ABX, .abx, 4)
        set(0xBD, "LDA", LDA, ABX, .abx, 4)
        set(0xBE, "LDX", LDX, ABY, .aby, 4)

        set(0xC0, "CPY", CPY, IMM, .imm, 2)
        set(0xC1, "CMP", CMP, IZX, .izx, 6)
        set(0xC4, "CPY", CPY, ZP0, .zp0, 3)
        set(0xC5, "CMP", CMP, ZP0, .zp0, 3)
        set(0xC6, "DEC", DEC, ZP0, .zp0, 5)
        set(0xC8, "INY", INY, IMP, .imp, 2)
        set(0xC9, "CMP", CMP, IMM, .imm, 2)
        set(0xCA, "DEX", DEX, IMP, .imp, 2)
        set(0xCC, "CPY", CPY, ABS, .abs, 4)
        set(0xCD, "CMP", CMP, ABS, .abs, 4)
        set(0xCE, "DEC", DEC, ABS, .abs, 6)

        set(0xD0, "BNE", BNE, REL, .rel, 2)
        set(0xD1, "CMP", CMP, IZY, .izy, 5)
        set(0xD5, "CMP", CMP, ZPX, .zpx, 4)
        set(0xD6, "DEC", DEC, ZPX, .zpx, 6)
        set(0xD8, "CLD", CLD, IMP, .imp, 2)
        set(0xD9, "CMP", CMP, ABY, .aby, 4)
        set(0xDD, "CMP", CMP, ABX, .abx, 4)
        set(0xDE, "DEC", DEC, ABX, .abx, 7)

        set(0xE0, "CPX", CPX, IMM, .imm, 2)
        set(0xE1, "SBC", SBC, IZX, .izx, 6)
        set(0xE4, "CPX", CPX, ZP0, .zp0, 3)
        set(0xE5, "SBC", SBC, ZP0, .zp0, 3)
        set(0xE6, "INC", INC, ZP0, .zp0, 5)
        set(0xE8, "INX", INX, IMP, .imp, 2)
        set(0xE9, "SBC", SBC, IMM, .imm, 2)
        set(0xEA, "NOP", NOP, IMP, .imp, 2)
        set(0xEC, "CPX", CPX, ABS, .abs, 4)
        set(0xED, "SBC", SBC, ABS, .abs, 4)
        set(0xEE, "INC", INC, ABS, .abs, 6)

        set(0xF0, "BEQ", BEQ, REL, .rel, 2)
        set(0xF1, "SBC", SBC, IZY, .izy, 5)
        set(0xF5, "SBC", SBC, ZPX, .zpx, 4)
        set(0xF6, "INC", INC, ZPX, .zpx, 6)
        set(0xF8, "SED", SED, IMP, .imp, 2)
        set(0xF9, "SBC", SBC, ABY, .aby, 4)
        set(0xFD, "SBC", SBC, ABX, .abx, 4)
        set(0xFE, "INC", INC, ABX, .abx, 7)

        // Unofficial NOPs (for test ROM compatibility)
        set(0x04, "NOP", NOP, ZP0, .zp0, 3)
        set(0x0C, "NOP", NOP, ABS, .abs, 4)
        set(0x14, "NOP", NOP, ZPX, .zpx, 4)
        set(0x1A, "NOP", NOP, IMP, .imp, 2)
        set(0x1C, "NOP", NOPR, ABX, .abx, 4)
        set(0x34, "NOP", NOP, ZPX, .zpx, 4)
        set(0x3A, "NOP", NOP, IMP, .imp, 2)
        set(0x3C, "NOP", NOPR, ABX, .abx, 4)
        set(0x44, "NOP", NOP, ZP0, .zp0, 3)
        set(0x54, "NOP", NOP, ZPX, .zpx, 4)
        set(0x5A, "NOP", NOP, IMP, .imp, 2)
        set(0x5C, "NOP", NOPR, ABX, .abx, 4)
        set(0x64, "NOP", NOP, ZP0, .zp0, 3)
        set(0x74, "NOP", NOP, ZPX, .zpx, 4)
        set(0x7A, "NOP", NOP, IMP, .imp, 2)
        set(0x7C, "NOP", NOPR, ABX, .abx, 4)
        set(0x80, "NOP", NOP, IMM, .imm, 2)
        set(0x82, "NOP", NOP, IMM, .imm, 2)
        set(0x89, "NOP", NOP, IMM, .imm, 2)
        set(0xC2, "NOP", NOP, IMM, .imm, 2)
        set(0xD4, "NOP", NOP, ZPX, .zpx, 4)
        set(0xDA, "NOP", NOP, IMP, .imp, 2)
        set(0xDC, "NOP", NOPR, ABX, .abx, 4)
        set(0xE2, "NOP", NOP, IMM, .imm, 2)
        set(0xF4, "NOP", NOP, ZPX, .zpx, 4)
        set(0xFA, "NOP", NOP, IMP, .imp, 2)
        set(0xFC, "NOP", NOPR, ABX, .abx, 4)

        // Unofficial opcodes
        set(0x03, "SLO", SLO, IZX, .izx, 8)
        set(0x07, "SLO", SLO, ZP0, .zp0, 5)
        set(0x0F, "SLO", SLO, ABS, .abs, 6)
        set(0x13, "SLO", SLO, IZY, .izy, 8)
        set(0x17, "SLO", SLO, ZPX, .zpx, 6)
        set(0x1B, "SLO", SLO, ABY, .aby, 7)
        set(0x1F, "SLO", SLO, ABX, .abx, 7)

        set(0x23, "RLA", RLA, IZX, .izx, 8)
        set(0x27, "RLA", RLA, ZP0, .zp0, 5)
        set(0x2F, "RLA", RLA, ABS, .abs, 6)
        set(0x33, "RLA", RLA, IZY, .izy, 8)
        set(0x37, "RLA", RLA, ZPX, .zpx, 6)
        set(0x3B, "RLA", RLA, ABY, .aby, 7)
        set(0x3F, "RLA", RLA, ABX, .abx, 7)

        set(0x43, "SRE", SRE, IZX, .izx, 8)
        set(0x47, "SRE", SRE, ZP0, .zp0, 5)
        set(0x4F, "SRE", SRE, ABS, .abs, 6)
        set(0x53, "SRE", SRE, IZY, .izy, 8)
        set(0x57, "SRE", SRE, ZPX, .zpx, 6)
        set(0x5B, "SRE", SRE, ABY, .aby, 7)
        set(0x5F, "SRE", SRE, ABX, .abx, 7)

        set(0x63, "RRA", RRA, IZX, .izx, 8)
        set(0x67, "RRA", RRA, ZP0, .zp0, 5)
        set(0x6F, "RRA", RRA, ABS, .abs, 6)
        set(0x73, "RRA", RRA, IZY, .izy, 8)
        set(0x77, "RRA", RRA, ZPX, .zpx, 6)
        set(0x7B, "RRA", RRA, ABY, .aby, 7)
        set(0x7F, "RRA", RRA, ABX, .abx, 7)

        set(0x83, "SAX", SAX, IZX, .izx, 6)
        set(0x87, "SAX", SAX, ZP0, .zp0, 3)
        set(0x8F, "SAX", SAX, ABS, .abs, 4)
        set(0x97, "SAX", SAX, ZPY, .zpy, 4)

        set(0xA3, "LAX", LAX, IZX, .izx, 6)
        set(0xA7, "LAX", LAX, ZP0, .zp0, 3)
        set(0xAF, "LAX", LAX, ABS, .abs, 4)
        set(0xB3, "LAX", LAX, IZY, .izy, 5)
        set(0xB7, "LAX", LAX, ZPY, .zpy, 4)
        set(0xBF, "LAX", LAX, ABY, .aby, 4)

        set(0xC3, "DCP", DCP, IZX, .izx, 8)
        set(0xC7, "DCP", DCP, ZP0, .zp0, 5)
        set(0xCF, "DCP", DCP, ABS, .abs, 6)
        set(0xD3, "DCP", DCP, IZY, .izy, 8)
        set(0xD7, "DCP", DCP, ZPX, .zpx, 6)
        set(0xDB, "DCP", DCP, ABY, .aby, 7)
        set(0xDF, "DCP", DCP, ABX, .abx, 7)

        set(0xE3, "ISC", ISC, IZX, .izx, 8)
        set(0xE7, "ISC", ISC, ZP0, .zp0, 5)
        set(0xEF, "ISC", ISC, ABS, .abs, 6)
        set(0xF3, "ISC", ISC, IZY, .izy, 8)
        set(0xF7, "ISC", ISC, ZPX, .zpx, 6)
        set(0xFB, "ISC", ISC, ABY, .aby, 7)
        set(0xFF, "ISC", ISC, ABX, .abx, 7)

        set(0x0B, "ANC", ANC, IMM, .imm, 2)
        set(0x2B, "ANC", ANC, IMM, .imm, 2)
        set(0x4B, "ASR", ASR, IMM, .imm, 2)
        set(0x6B, "ARR", ARR, IMM, .imm, 2)
        set(0x8B, "ANE", ANE, IMM, .imm, 2)
        set(0xAB, "LXA", LXA, IMM, .imm, 2)
        set(0xCB, "AXS", AXS, IMM, .imm, 2)
        set(0xEB, "SBC", SBC, IMM, .imm, 2)

        set(0x9F, "SHA", SHA, ABY, .aby, 5)
        set(0x93, "SHA", SHA, IZY, .izy, 6)
        set(0x9E, "SHX", SHX, ABY, .aby, 5)
        set(0x9C, "SHY", SHY, ABX, .abx, 5)
        set(0x9B, "SHS", SHS, ABY, .aby, 5)
        set(0xBB, "LAE", LAE, ABY, .aby, 4)
    }
}
