#include "../include/cpu.h"

#include <string.h>

static uint8_t cpu_read(CPU *cpu, uint16_t addr) {
    return cpu->bus ? bus_cpu_read(cpu->bus, addr) : 0;
}

static void cpu_write(CPU *cpu, uint16_t addr, uint8_t data) {
    if (addr == 0x4014) {
        int extra = cpu->cycleCounter % 2;
        bus_request_stall(cpu->bus, 513 + extra);
    }
    if (cpu->bus) {
        bus_cpu_write(cpu->bus, addr, data);
    }
}

static void cpu_push(CPU *cpu, uint8_t value) {
    cpu_write(cpu, (uint16_t)(0x0100 | cpu->sp), value);
    cpu->sp -= 1;
}

static uint8_t cpu_pop(CPU *cpu) {
    cpu->sp += 1;
    return cpu_read(cpu, (uint16_t)(0x0100 | cpu->sp));
}

static uint8_t cpu_get_flag(CPU *cpu, CPUFlag flag) {
    return (cpu->status & flag) ? 1 : 0;
}

static void cpu_set_flag(CPU *cpu, CPUFlag flag, bool value) {
    if (value) {
        cpu->status |= flag;
    } else {
        cpu->status &= (uint8_t)~flag;
    }
}

static void cpu_set_zn(CPU *cpu, uint8_t value) {
    cpu_set_flag(cpu, CPU_FLAG_Z, value == 0);
    cpu_set_flag(cpu, CPU_FLAG_N, (value & 0x80) != 0);
}

static uint8_t cpu_fetch(CPU *cpu) {
    if (cpu->instructions[cpu->opcode].mode != ADDR_IMP) {
        cpu->fetched = cpu_read(cpu, cpu->addrAbs);
    }
    return cpu->fetched;
}

static uint8_t cpu_IMP(CPU *cpu) {
    cpu->fetched = cpu->a;
    return 0;
}

static void cpu_implied_dummy_read(CPU *cpu) {
    (void)cpu_read(cpu, cpu->pc);
}

static uint8_t cpu_IMM(CPU *cpu) {
    cpu->addrAbs = cpu->pc;
    cpu->pc += 1;
    return 0;
}

static uint8_t cpu_ZP0(CPU *cpu) {
    cpu->addrAbs = cpu_read(cpu, cpu->pc);
    cpu->pc += 1;
    cpu->addrAbs &= 0x00FF;
    return 0;
}

static uint8_t cpu_ZPX(CPU *cpu) {
    cpu->addrAbs = (uint16_t)((cpu_read(cpu, cpu->pc) + cpu->x) & 0xFF);
    cpu->pc += 1;
    cpu->addrAbs &= 0x00FF;
    return 0;
}

static uint8_t cpu_ZPY(CPU *cpu) {
    cpu->addrAbs = (uint16_t)((cpu_read(cpu, cpu->pc) + cpu->y) & 0xFF);
    cpu->pc += 1;
    cpu->addrAbs &= 0x00FF;
    return 0;
}

static uint8_t cpu_ABS(CPU *cpu) {
    uint8_t lo = cpu_read(cpu, cpu->pc);
    cpu->pc += 1;
    uint8_t hi = cpu_read(cpu, cpu->pc);
    cpu->pc += 1;
    cpu->baseHigh = hi;
    cpu->addrAbs = (uint16_t)(hi << 8) | lo;
    return 0;
}

static bool cpu_uses_high_byte_bug_for_store(CPU *cpu) {
    switch (cpu->opcode) {
        case 0x93:
        case 0x9F:
        case 0x9B:
        case 0x9C:
        case 0x9E:
            return true;
        default:
            return false;
    }
}

static uint8_t cpu_ABX(CPU *cpu) {
    uint8_t lo = cpu_read(cpu, cpu->pc);
    cpu->pc += 1;
    uint8_t hi = cpu_read(cpu, cpu->pc);
    cpu->pc += 1;
    cpu->baseHigh = hi;
    uint16_t base = (uint16_t)(hi << 8) | lo;
    if (cpu_uses_high_byte_bug_for_store(cpu)) {
        uint8_t low = (uint8_t)(lo + cpu->x);
        cpu->addrAbs = (uint16_t)(cpu->baseHigh << 8) | low;
    } else {
        cpu->addrAbs = (uint16_t)(base + cpu->x);
    }
    bool pageCross = (cpu->addrAbs & 0xFF00) != (base & 0xFF00);
    AccessKind access = cpu->instructions[cpu->opcode].access;
    if (access == ACCESS_WRITE || (access == ACCESS_READ && pageCross) || access == ACCESS_READ_MODIFY_WRITE) {
        uint16_t dummyAddr = (uint16_t)((base & 0xFF00) | (cpu->addrAbs & 0x00FF));
        (void)cpu_read(cpu, dummyAddr);
    }
    return (access == ACCESS_READ && pageCross) ? 1 : 0;
}

static uint8_t cpu_ABY(CPU *cpu) {
    uint8_t lo = cpu_read(cpu, cpu->pc);
    cpu->pc += 1;
    uint8_t hi = cpu_read(cpu, cpu->pc);
    cpu->pc += 1;
    cpu->baseHigh = hi;
    uint16_t base = (uint16_t)(hi << 8) | lo;
    if (cpu_uses_high_byte_bug_for_store(cpu)) {
        uint8_t low = (uint8_t)(lo + cpu->y);
        cpu->addrAbs = (uint16_t)(cpu->baseHigh << 8) | low;
    } else {
        cpu->addrAbs = (uint16_t)(base + cpu->y);
    }
    bool pageCross = (cpu->addrAbs & 0xFF00) != (base & 0xFF00);
    AccessKind access = cpu->instructions[cpu->opcode].access;
    if (access == ACCESS_WRITE || (access == ACCESS_READ && pageCross) || access == ACCESS_READ_MODIFY_WRITE) {
        uint16_t dummyAddr = (uint16_t)((base & 0xFF00) | (cpu->addrAbs & 0x00FF));
        (void)cpu_read(cpu, dummyAddr);
    }
    return (access == ACCESS_READ && pageCross) ? 1 : 0;
}

static uint8_t cpu_IND(CPU *cpu) {
    uint8_t ptrLo = cpu_read(cpu, cpu->pc);
    cpu->pc += 1;
    uint8_t ptrHi = cpu_read(cpu, cpu->pc);
    cpu->pc += 1;
    uint16_t ptr = (uint16_t)(ptrHi << 8) | ptrLo;
    uint8_t lo = cpu_read(cpu, ptr);
    uint8_t hi = cpu_read(cpu, (uint16_t)((ptr & 0xFF00) | (uint8_t)((ptr & 0x00FF) + 1)));
    cpu->addrAbs = (uint16_t)(hi << 8) | lo;
    return 0;
}

static uint8_t cpu_IZX(CPU *cpu) {
    uint8_t t = cpu_read(cpu, cpu->pc);
    cpu->pc += 1;
    uint8_t lo = cpu_read(cpu, (uint16_t)(uint8_t)(t + cpu->x));
    uint8_t hi = cpu_read(cpu, (uint16_t)(uint8_t)(t + cpu->x + 1));
    cpu->addrAbs = (uint16_t)(hi << 8) | lo;
    return 0;
}

static uint8_t cpu_IZY(CPU *cpu) {
    uint8_t t = cpu_read(cpu, cpu->pc);
    cpu->pc += 1;
    uint8_t lo = cpu_read(cpu, t);
    uint8_t hi = cpu_read(cpu, (uint16_t)(uint8_t)(t + 1));
    cpu->baseHigh = hi;
    uint16_t base = (uint16_t)(hi << 8) | lo;
    if (cpu_uses_high_byte_bug_for_store(cpu)) {
        uint8_t low = (uint8_t)(lo + cpu->y);
        cpu->addrAbs = (uint16_t)(cpu->baseHigh << 8) | low;
    } else {
        cpu->addrAbs = (uint16_t)(base + cpu->y);
    }
    bool pageCross = (cpu->addrAbs & 0xFF00) != (base & 0xFF00);
    AccessKind access = cpu->instructions[cpu->opcode].access;
    if ((access == ACCESS_WRITE && pageCross) || (access == ACCESS_READ && pageCross) || (access == ACCESS_READ_MODIFY_WRITE && pageCross)) {
        uint16_t dummyAddr = (uint16_t)((base & 0xFF00) | (cpu->addrAbs & 0x00FF));
        (void)cpu_read(cpu, dummyAddr);
    }
    return (access == ACCESS_READ && pageCross) ? 1 : 0;
}

static uint8_t cpu_REL(CPU *cpu) {
    cpu->addrRel = cpu_read(cpu, cpu->pc);
    cpu->pc += 1;
    if (cpu->addrRel & 0x80) {
        cpu->addrRel |= 0xFF00;
    }
    return 0;
}

static void cpu_push_status(CPU *cpu, bool setBreak) {
    uint8_t flags = (uint8_t)(cpu->status | CPU_FLAG_U);
    if (setBreak) {
        flags |= CPU_FLAG_B;
    } else {
        flags &= (uint8_t)~CPU_FLAG_B;
    }
    cpu_push(cpu, flags);
}

static void cpu_adc_with(CPU *cpu, uint8_t value) {
    uint16_t sum = (uint16_t)cpu->a + value + cpu_get_flag(cpu, CPU_FLAG_C);
    cpu_set_flag(cpu, CPU_FLAG_C, sum > 0xFF);
    cpu_set_flag(cpu, CPU_FLAG_Z, (uint8_t)(sum & 0x00FF) == 0);
    cpu_set_flag(cpu, CPU_FLAG_V, (~((uint16_t)cpu->a ^ value) & ((uint16_t)cpu->a ^ sum) & 0x0080) != 0);
    cpu_set_flag(cpu, CPU_FLAG_N, (sum & 0x80) != 0);
    cpu->a = (uint8_t)(sum & 0x00FF);
}

static void cpu_sbc_with(CPU *cpu, uint8_t value) {
    uint8_t inv = (uint8_t)(value ^ 0xFF);
    uint16_t sum = (uint16_t)cpu->a + inv + cpu_get_flag(cpu, CPU_FLAG_C);
    cpu_set_flag(cpu, CPU_FLAG_C, (sum & 0xFF00) != 0);
    cpu_set_flag(cpu, CPU_FLAG_Z, (uint8_t)(sum & 0x00FF) == 0);
    cpu_set_flag(cpu, CPU_FLAG_V, (sum ^ cpu->a) & (sum ^ inv) & 0x0080);
    cpu_set_flag(cpu, CPU_FLAG_N, (sum & 0x80) != 0);
    cpu->a = (uint8_t)(sum & 0x00FF);
}

static uint8_t cpu_branch(CPU *cpu, bool condition) {
    if (condition) {
        (void)cpu_read(cpu, cpu->pc);
        uint16_t oldPc = cpu->pc;
        cpu->pc += cpu->addrRel;
        if ((cpu->pc & 0xFF00) != (oldPc & 0xFF00)) {
            (void)cpu_read(cpu, (uint16_t)((oldPc & 0xFF00) | (cpu->pc & 0x00FF)));
            return 2;
        }
        return 1;
    }
    return 0;
}

static uint8_t cpu_ADC(CPU *cpu) { cpu_adc_with(cpu, cpu_fetch(cpu)); return 1; }
static uint8_t cpu_AND(CPU *cpu) { cpu->a &= cpu_fetch(cpu); cpu_set_zn(cpu, cpu->a); return 1; }

static uint8_t cpu_ASL(CPU *cpu) {
    if (cpu->instructions[cpu->opcode].mode == ADDR_IMP) {
        cpu_implied_dummy_read(cpu);
    }
    uint8_t value = cpu_fetch(cpu);
    if (cpu->instructions[cpu->opcode].mode != ADDR_IMP) {
        cpu_write(cpu, cpu->addrAbs, value);
    }
    uint16_t result = (uint16_t)value << 1;
    cpu_set_flag(cpu, CPU_FLAG_C, (result & 0xFF00) != 0);
    uint8_t output = (uint8_t)(result & 0x00FF);
    cpu_set_zn(cpu, output);
    if (cpu->instructions[cpu->opcode].mode == ADDR_IMP) {
        cpu->a = output;
    } else {
        cpu_write(cpu, cpu->addrAbs, output);
    }
    return 0;
}

static uint8_t cpu_BCC(CPU *cpu) { return cpu_branch(cpu, cpu_get_flag(cpu, CPU_FLAG_C) == 0); }
static uint8_t cpu_BCS(CPU *cpu) { return cpu_branch(cpu, cpu_get_flag(cpu, CPU_FLAG_C) == 1); }
static uint8_t cpu_BEQ(CPU *cpu) { return cpu_branch(cpu, cpu_get_flag(cpu, CPU_FLAG_Z) == 1); }
static uint8_t cpu_BMI(CPU *cpu) { return cpu_branch(cpu, cpu_get_flag(cpu, CPU_FLAG_N) == 1); }
static uint8_t cpu_BNE(CPU *cpu) { return cpu_branch(cpu, cpu_get_flag(cpu, CPU_FLAG_Z) == 0); }
static uint8_t cpu_BPL(CPU *cpu) { return cpu_branch(cpu, cpu_get_flag(cpu, CPU_FLAG_N) == 0); }
static uint8_t cpu_BVC(CPU *cpu) { return cpu_branch(cpu, cpu_get_flag(cpu, CPU_FLAG_V) == 0); }
static uint8_t cpu_BVS(CPU *cpu) { return cpu_branch(cpu, cpu_get_flag(cpu, CPU_FLAG_V) == 1); }

static uint8_t cpu_BIT(CPU *cpu) {
    uint8_t value = cpu_fetch(cpu);
    uint8_t temp = (uint8_t)(cpu->a & value);
    cpu_set_flag(cpu, CPU_FLAG_Z, temp == 0);
    cpu_set_flag(cpu, CPU_FLAG_V, (value & 0x40) != 0);
    cpu_set_flag(cpu, CPU_FLAG_N, (value & 0x80) != 0);
    return 0;
}

static uint8_t cpu_BRK(CPU *cpu) {
    cpu_implied_dummy_read(cpu);
    cpu->pc += 1;
    cpu_push(cpu, (uint8_t)((cpu->pc >> 8) & 0xFF));
    cpu_push(cpu, (uint8_t)(cpu->pc & 0xFF));
    cpu_set_flag(cpu, CPU_FLAG_B, true);
    cpu_push_status(cpu, true);
    cpu_set_flag(cpu, CPU_FLAG_B, false);
    cpu_set_flag(cpu, CPU_FLAG_I, true);
    uint8_t lo = cpu_read(cpu, 0xFFFE);
    uint8_t hi = cpu_read(cpu, 0xFFFF);
    cpu->pc = (uint16_t)(hi << 8) | lo;
    return 0;
}

static uint8_t cpu_CLC(CPU *cpu) { cpu_implied_dummy_read(cpu); cpu_set_flag(cpu, CPU_FLAG_C, false); return 0; }
static uint8_t cpu_CLD(CPU *cpu) { cpu_implied_dummy_read(cpu); cpu_set_flag(cpu, CPU_FLAG_D, false); return 0; }
static uint8_t cpu_CLI(CPU *cpu) { cpu_implied_dummy_read(cpu); cpu_set_flag(cpu, CPU_FLAG_I, false); return 0; }
static uint8_t cpu_CLV(CPU *cpu) { cpu_implied_dummy_read(cpu); cpu_set_flag(cpu, CPU_FLAG_V, false); return 0; }

static uint8_t cpu_CMP(CPU *cpu) {
    uint8_t value = cpu_fetch(cpu);
    uint16_t temp = (uint16_t)cpu->a - value;
    cpu_set_flag(cpu, CPU_FLAG_C, cpu->a >= value);
    cpu_set_zn(cpu, (uint8_t)(temp & 0x00FF));
    return 1;
}

static uint8_t cpu_CPX(CPU *cpu) {
    uint8_t value = cpu_fetch(cpu);
    uint16_t temp = (uint16_t)cpu->x - value;
    cpu_set_flag(cpu, CPU_FLAG_C, cpu->x >= value);
    cpu_set_zn(cpu, (uint8_t)(temp & 0x00FF));
    return 0;
}

static uint8_t cpu_CPY(CPU *cpu) {
    uint8_t value = cpu_fetch(cpu);
    uint16_t temp = (uint16_t)cpu->y - value;
    cpu_set_flag(cpu, CPU_FLAG_C, cpu->y >= value);
    cpu_set_zn(cpu, (uint8_t)(temp & 0x00FF));
    return 0;
}

static uint8_t cpu_DEC(CPU *cpu) {
    uint8_t value = cpu_fetch(cpu);
    if (cpu->instructions[cpu->opcode].mode != ADDR_IMP) {
        cpu_write(cpu, cpu->addrAbs, value);
    }
    uint8_t result = (uint8_t)(value - 1);
    cpu_write(cpu, cpu->addrAbs, result);
    cpu_set_zn(cpu, result);
    return 0;
}

static uint8_t cpu_DEX(CPU *cpu) { cpu_implied_dummy_read(cpu); cpu->x -= 1; cpu_set_zn(cpu, cpu->x); return 0; }
static uint8_t cpu_DEY(CPU *cpu) { cpu_implied_dummy_read(cpu); cpu->y -= 1; cpu_set_zn(cpu, cpu->y); return 0; }

static uint8_t cpu_EOR(CPU *cpu) { cpu->a ^= cpu_fetch(cpu); cpu_set_zn(cpu, cpu->a); return 1; }

static uint8_t cpu_INC(CPU *cpu) {
    uint8_t value = cpu_fetch(cpu);
    if (cpu->instructions[cpu->opcode].mode != ADDR_IMP) {
        cpu_write(cpu, cpu->addrAbs, value);
    }
    uint8_t result = (uint8_t)(value + 1);
    cpu_write(cpu, cpu->addrAbs, result);
    cpu_set_zn(cpu, result);
    return 0;
}

static uint8_t cpu_INX(CPU *cpu) { cpu_implied_dummy_read(cpu); cpu->x += 1; cpu_set_zn(cpu, cpu->x); return 0; }
static uint8_t cpu_INY(CPU *cpu) { cpu_implied_dummy_read(cpu); cpu->y += 1; cpu_set_zn(cpu, cpu->y); return 0; }

static uint8_t cpu_JMP(CPU *cpu) { cpu->pc = cpu->addrAbs; return 0; }

static uint8_t cpu_JSR(CPU *cpu) {
    cpu->pc -= 1;
    cpu_push(cpu, (uint8_t)((cpu->pc >> 8) & 0xFF));
    cpu_push(cpu, (uint8_t)(cpu->pc & 0xFF));
    cpu->pc = cpu->addrAbs;
    if (cpu->bus) {
        bus_set_cpu_bus(cpu->bus, (uint8_t)((cpu->addrAbs >> 8) & 0xFF));
    }
    return 0;
}

static uint8_t cpu_LDA(CPU *cpu) { cpu->a = cpu_fetch(cpu); cpu_set_zn(cpu, cpu->a); return 1; }
static uint8_t cpu_LDX(CPU *cpu) { cpu->x = cpu_fetch(cpu); cpu_set_zn(cpu, cpu->x); return 1; }
static uint8_t cpu_LDY(CPU *cpu) { cpu->y = cpu_fetch(cpu); cpu_set_zn(cpu, cpu->y); return 1; }

static uint8_t cpu_LSR(CPU *cpu) {
    if (cpu->instructions[cpu->opcode].mode == ADDR_IMP) {
        cpu_implied_dummy_read(cpu);
    }
    uint8_t value = cpu_fetch(cpu);
    if (cpu->instructions[cpu->opcode].mode != ADDR_IMP) {
        cpu_write(cpu, cpu->addrAbs, value);
    }
    cpu_set_flag(cpu, CPU_FLAG_C, (value & 0x01) != 0);
    uint8_t result = (uint8_t)(value >> 1);
    cpu_set_zn(cpu, result);
    if (cpu->instructions[cpu->opcode].mode == ADDR_IMP) {
        cpu->a = result;
    } else {
        cpu_write(cpu, cpu->addrAbs, result);
    }
    return 0;
}

static uint8_t cpu_NOP(CPU *cpu) {
    cpu_implied_dummy_read(cpu);
    if (cpu->instructions[cpu->opcode].mode != ADDR_IMP) {
        (void)cpu_fetch(cpu);
    }
    return 0;
}

static uint8_t cpu_NOPR(CPU *cpu) {
    cpu_implied_dummy_read(cpu);
    if (cpu->instructions[cpu->opcode].mode != ADDR_IMP) {
        (void)cpu_fetch(cpu);
    }
    return 1;
}

static uint8_t cpu_SLO(CPU *cpu) {
    uint8_t value = cpu_fetch(cpu);
    cpu_write(cpu, cpu->addrAbs, value);
    uint8_t result = (uint8_t)(((uint16_t)value << 1) & 0x00FF);
    cpu_set_flag(cpu, CPU_FLAG_C, (value & 0x80) != 0);
    cpu_write(cpu, cpu->addrAbs, result);
    cpu->a |= result;
    cpu_set_zn(cpu, cpu->a);
    return 0;
}

static uint8_t cpu_RLA(CPU *cpu) {
    uint8_t value = cpu_fetch(cpu);
    cpu_write(cpu, cpu->addrAbs, value);
    uint8_t carryIn = cpu_get_flag(cpu, CPU_FLAG_C);
    cpu_set_flag(cpu, CPU_FLAG_C, (value & 0x80) != 0);
    uint8_t result = (uint8_t)(((uint16_t)value << 1) & 0x00FF) | carryIn;
    cpu_write(cpu, cpu->addrAbs, result);
    cpu->a &= result;
    cpu_set_zn(cpu, cpu->a);
    return 0;
}

static uint8_t cpu_SRE(CPU *cpu) {
    uint8_t value = cpu_fetch(cpu);
    cpu_write(cpu, cpu->addrAbs, value);
    cpu_set_flag(cpu, CPU_FLAG_C, (value & 0x01) != 0);
    uint8_t result = (uint8_t)(value >> 1);
    cpu_write(cpu, cpu->addrAbs, result);
    cpu->a ^= result;
    cpu_set_zn(cpu, cpu->a);
    return 0;
}

static uint8_t cpu_RRA(CPU *cpu) {
    uint8_t value = cpu_fetch(cpu);
    cpu_write(cpu, cpu->addrAbs, value);
    uint8_t carryIn = cpu_get_flag(cpu, CPU_FLAG_C);
    cpu_set_flag(cpu, CPU_FLAG_C, (value & 0x01) != 0);
    uint8_t result = (uint8_t)(((uint16_t)carryIn << 7) | (value >> 1));
    cpu_write(cpu, cpu->addrAbs, result);
    cpu_adc_with(cpu, result);
    return 0;
}

static uint8_t cpu_SAX(CPU *cpu) { cpu_write(cpu, cpu->addrAbs, (uint8_t)(cpu->a & cpu->x)); return 0; }

static uint8_t cpu_LAX(CPU *cpu) {
    uint8_t value = cpu_fetch(cpu);
    cpu->a = value;
    cpu->x = value;
    cpu_set_zn(cpu, value);
    return 1;
}

static uint8_t cpu_DCP(CPU *cpu) {
    uint8_t value = cpu_fetch(cpu);
    cpu_write(cpu, cpu->addrAbs, value);
    uint8_t result = (uint8_t)(value - 1);
    cpu_write(cpu, cpu->addrAbs, result);
    uint16_t temp = (uint16_t)cpu->a - result;
    cpu_set_flag(cpu, CPU_FLAG_C, cpu->a >= result);
    cpu_set_zn(cpu, (uint8_t)(temp & 0x00FF));
    return 0;
}

static uint8_t cpu_ISC(CPU *cpu) {
    uint8_t value = cpu_fetch(cpu);
    cpu_write(cpu, cpu->addrAbs, value);
    uint8_t result = (uint8_t)(value + 1);
    cpu_write(cpu, cpu->addrAbs, result);
    cpu_sbc_with(cpu, result);
    return 0;
}

static uint8_t cpu_ANC(CPU *cpu) {
    cpu->a &= cpu_fetch(cpu);
    cpu_set_zn(cpu, cpu->a);
    cpu_set_flag(cpu, CPU_FLAG_C, (cpu->a & 0x80) != 0);
    return 0;
}

static uint8_t cpu_ASR(CPU *cpu) {
    cpu->a &= cpu_fetch(cpu);
    cpu_set_flag(cpu, CPU_FLAG_C, (cpu->a & 0x01) != 0);
    cpu->a >>= 1;
    cpu_set_zn(cpu, cpu->a);
    return 0;
}

static uint8_t cpu_ARR(CPU *cpu) {
    cpu->a &= cpu_fetch(cpu);
    uint8_t carryIn = cpu_get_flag(cpu, CPU_FLAG_C);
    uint8_t result = (uint8_t)(((uint16_t)carryIn << 7) | (cpu->a >> 1));
    cpu->a = result;
    cpu_set_zn(cpu, cpu->a);
    cpu_set_flag(cpu, CPU_FLAG_C, (cpu->a & 0x40) != 0);
    cpu_set_flag(cpu, CPU_FLAG_V, (((cpu->a >> 5) ^ (cpu->a >> 6)) & 0x01) != 0);
    return 0;
}

static uint8_t cpu_ANE(CPU *cpu) {
    uint8_t value = cpu_fetch(cpu);
    cpu->a = (uint8_t)((cpu->a | 0xEE) & cpu->x & value);
    cpu_set_zn(cpu, cpu->a);
    return 0;
}

static uint8_t cpu_LXA(CPU *cpu) {
    uint8_t value = cpu_fetch(cpu);
    cpu->a = (uint8_t)((cpu->a | 0xEE) & value);
    cpu->x = cpu->a;
    cpu_set_zn(cpu, cpu->a);
    return 0;
}

static uint8_t cpu_AXS(CPU *cpu) {
    uint8_t value = cpu_fetch(cpu);
    uint8_t temp = (uint8_t)((cpu->a & cpu->x) - value);
    cpu_set_flag(cpu, CPU_FLAG_C, (cpu->a & cpu->x) >= value);
    cpu->x = temp;
    cpu_set_zn(cpu, cpu->x);
    return 0;
}

static uint8_t cpu_SHA(CPU *cpu) {
    uint8_t high = (uint8_t)((cpu->addrAbs >> 8) & 0xFF);
    uint8_t value = (uint8_t)(cpu->a & cpu->x & (uint8_t)(high + 1));
    cpu_write(cpu, cpu->addrAbs, value);
    return 0;
}

static uint8_t cpu_SHX(CPU *cpu) {
    uint8_t high = (uint8_t)((cpu->addrAbs >> 8) & 0xFF);
    uint8_t value = (uint8_t)(cpu->x & (uint8_t)(high + 1));
    cpu_write(cpu, cpu->addrAbs, value);
    return 0;
}

static uint8_t cpu_SHY(CPU *cpu) {
    uint8_t high = (uint8_t)((cpu->addrAbs >> 8) & 0xFF);
    uint8_t value = (uint8_t)(cpu->y & (uint8_t)(high + 1));
    cpu_write(cpu, cpu->addrAbs, value);
    return 0;
}

static uint8_t cpu_SHS(CPU *cpu) {
    cpu->sp = (uint8_t)(cpu->a & cpu->x);
    uint8_t high = (uint8_t)((cpu->addrAbs >> 8) & 0xFF);
    uint8_t value = (uint8_t)(cpu->sp & (uint8_t)(high + 1));
    cpu_write(cpu, cpu->addrAbs, value);
    return 0;
}

static uint8_t cpu_LAE(CPU *cpu) {
    uint8_t value = (uint8_t)(cpu_fetch(cpu) & cpu->sp);
    cpu->a = value;
    cpu->x = value;
    cpu->sp = value;
    cpu_set_zn(cpu, value);
    return 1;
}

static uint8_t cpu_ORA(CPU *cpu) { cpu->a |= cpu_fetch(cpu); cpu_set_zn(cpu, cpu->a); return 1; }

static uint8_t cpu_PHA(CPU *cpu) { cpu_implied_dummy_read(cpu); cpu_push(cpu, cpu->a); return 0; }
static uint8_t cpu_PHP(CPU *cpu) { cpu_implied_dummy_read(cpu); cpu_push_status(cpu, true); return 0; }
static uint8_t cpu_PLA(CPU *cpu) { cpu_implied_dummy_read(cpu); cpu->a = cpu_pop(cpu); cpu_set_zn(cpu, cpu->a); return 0; }
static uint8_t cpu_PLP(CPU *cpu) { cpu_implied_dummy_read(cpu); cpu->status = cpu_pop(cpu); cpu_set_flag(cpu, CPU_FLAG_U, true); return 0; }

static uint8_t cpu_ROL(CPU *cpu) {
    if (cpu->instructions[cpu->opcode].mode == ADDR_IMP) {
        cpu_implied_dummy_read(cpu);
    }
    uint8_t value = cpu_fetch(cpu);
    if (cpu->instructions[cpu->opcode].mode != ADDR_IMP) {
        cpu_write(cpu, cpu->addrAbs, value);
    }
    uint16_t result = (uint16_t)value << 1 | cpu_get_flag(cpu, CPU_FLAG_C);
    cpu_set_flag(cpu, CPU_FLAG_C, (result & 0xFF00) != 0);
    uint8_t output = (uint8_t)(result & 0x00FF);
    cpu_set_zn(cpu, output);
    if (cpu->instructions[cpu->opcode].mode == ADDR_IMP) {
        cpu->a = output;
    } else {
        cpu_write(cpu, cpu->addrAbs, output);
    }
    return 0;
}

static uint8_t cpu_ROR(CPU *cpu) {
    if (cpu->instructions[cpu->opcode].mode == ADDR_IMP) {
        cpu_implied_dummy_read(cpu);
    }
    uint8_t value = cpu_fetch(cpu);
    if (cpu->instructions[cpu->opcode].mode != ADDR_IMP) {
        cpu_write(cpu, cpu->addrAbs, value);
    }
    uint16_t result = (uint16_t)cpu_get_flag(cpu, CPU_FLAG_C) << 7 | (uint16_t)(value >> 1);
    cpu_set_flag(cpu, CPU_FLAG_C, (value & 0x01) != 0);
    uint8_t output = (uint8_t)(result & 0x00FF);
    cpu_set_zn(cpu, output);
    if (cpu->instructions[cpu->opcode].mode == ADDR_IMP) {
        cpu->a = output;
    } else {
        cpu_write(cpu, cpu->addrAbs, output);
    }
    return 0;
}

static uint8_t cpu_RTI(CPU *cpu) {
    cpu_implied_dummy_read(cpu);
    cpu->status = cpu_pop(cpu);
    cpu_set_flag(cpu, CPU_FLAG_U, true);
    uint8_t lo = cpu_pop(cpu);
    uint8_t hi = cpu_pop(cpu);
    cpu->pc = (uint16_t)(hi << 8) | lo;
    return 0;
}

static uint8_t cpu_RTS(CPU *cpu) {
    cpu_implied_dummy_read(cpu);
    uint8_t lo = cpu_pop(cpu);
    uint8_t hi = cpu_pop(cpu);
    cpu->pc = (uint16_t)((hi << 8) | lo) + 1;
    return 0;
}

static uint8_t cpu_SBC(CPU *cpu) { cpu_sbc_with(cpu, cpu_fetch(cpu)); return 1; }

static uint8_t cpu_SEC(CPU *cpu) { cpu_implied_dummy_read(cpu); cpu_set_flag(cpu, CPU_FLAG_C, true); return 0; }
static uint8_t cpu_SED(CPU *cpu) { cpu_implied_dummy_read(cpu); cpu_set_flag(cpu, CPU_FLAG_D, true); return 0; }
static uint8_t cpu_SEI(CPU *cpu) { cpu_implied_dummy_read(cpu); cpu_set_flag(cpu, CPU_FLAG_I, true); return 0; }

static uint8_t cpu_STA(CPU *cpu) { cpu_write(cpu, cpu->addrAbs, cpu->a); return 0; }
static uint8_t cpu_STX(CPU *cpu) { cpu_write(cpu, cpu->addrAbs, cpu->x); return 0; }
static uint8_t cpu_STY(CPU *cpu) { cpu_write(cpu, cpu->addrAbs, cpu->y); return 0; }

static uint8_t cpu_TAX(CPU *cpu) { cpu_implied_dummy_read(cpu); cpu->x = cpu->a; cpu_set_zn(cpu, cpu->x); return 0; }
static uint8_t cpu_TAY(CPU *cpu) { cpu_implied_dummy_read(cpu); cpu->y = cpu->a; cpu_set_zn(cpu, cpu->y); return 0; }
static uint8_t cpu_TSX(CPU *cpu) { cpu_implied_dummy_read(cpu); cpu->x = cpu->sp; cpu_set_zn(cpu, cpu->x); return 0; }
static uint8_t cpu_TXA(CPU *cpu) { cpu_implied_dummy_read(cpu); cpu->a = cpu->x; cpu_set_zn(cpu, cpu->a); return 0; }
static uint8_t cpu_TXS(CPU *cpu) { cpu_implied_dummy_read(cpu); cpu->sp = cpu->x; return 0; }
static uint8_t cpu_TYA(CPU *cpu) { cpu_implied_dummy_read(cpu); cpu->a = cpu->y; cpu_set_zn(cpu, cpu->a); return 0; }

static AccessKind cpu_access_for(const char *name) {
    if (!strcmp(name, "STA") || !strcmp(name, "STX") || !strcmp(name, "STY") || !strcmp(name, "SAX") ||
        !strcmp(name, "SHA") || !strcmp(name, "SHX") || !strcmp(name, "SHY") || !strcmp(name, "SHS")) {
        return ACCESS_WRITE;
    }
    if (!strcmp(name, "ASL") || !strcmp(name, "LSR") || !strcmp(name, "ROL") || !strcmp(name, "ROR") ||
        !strcmp(name, "INC") || !strcmp(name, "DEC") || !strcmp(name, "SLO") || !strcmp(name, "RLA") ||
        !strcmp(name, "SRE") || !strcmp(name, "RRA") || !strcmp(name, "DCP") || !strcmp(name, "ISC")) {
        return ACCESS_READ_MODIFY_WRITE;
    }
    return ACCESS_READ;
}

static void cpu_set_instruction(CPU *cpu, uint8_t opcode, const char *name, cpu_op op, cpu_op mode, AddressingMode kind, uint8_t cycles) {
    cpu->instructions[opcode].name = name;
    cpu->instructions[opcode].operate = op;
    cpu->instructions[opcode].addrMode = mode;
    cpu->instructions[opcode].mode = kind;
    cpu->instructions[opcode].access = cpu_access_for(name);
    cpu->instructions[opcode].cycles = cycles;
}

static void cpu_build_table(CPU *cpu) {
    for (int i = 0; i < 256; i++) {
        cpu->instructions[i].name = "NOP";
        cpu->instructions[i].operate = cpu_NOP;
        cpu->instructions[i].addrMode = cpu_IMP;
        cpu->instructions[i].mode = ADDR_IMP;
        cpu->instructions[i].access = ACCESS_IMPLIED;
        cpu->instructions[i].cycles = 2;
    }

    cpu_set_instruction(cpu, 0x00, "BRK", cpu_BRK, cpu_IMM, ADDR_IMM, 7);
    cpu_set_instruction(cpu, 0x01, "ORA", cpu_ORA, cpu_IZX, ADDR_IZX, 6);
    cpu_set_instruction(cpu, 0x05, "ORA", cpu_ORA, cpu_ZP0, ADDR_ZP0, 3);
    cpu_set_instruction(cpu, 0x06, "ASL", cpu_ASL, cpu_ZP0, ADDR_ZP0, 5);
    cpu_set_instruction(cpu, 0x08, "PHP", cpu_PHP, cpu_IMP, ADDR_IMP, 3);
    cpu_set_instruction(cpu, 0x09, "ORA", cpu_ORA, cpu_IMM, ADDR_IMM, 2);
    cpu_set_instruction(cpu, 0x0A, "ASL", cpu_ASL, cpu_IMP, ADDR_IMP, 2);
    cpu_set_instruction(cpu, 0x0D, "ORA", cpu_ORA, cpu_ABS, ADDR_ABS, 4);
    cpu_set_instruction(cpu, 0x0E, "ASL", cpu_ASL, cpu_ABS, ADDR_ABS, 6);

    cpu_set_instruction(cpu, 0x10, "BPL", cpu_BPL, cpu_REL, ADDR_REL, 2);
    cpu_set_instruction(cpu, 0x11, "ORA", cpu_ORA, cpu_IZY, ADDR_IZY, 5);
    cpu_set_instruction(cpu, 0x15, "ORA", cpu_ORA, cpu_ZPX, ADDR_ZPX, 4);
    cpu_set_instruction(cpu, 0x16, "ASL", cpu_ASL, cpu_ZPX, ADDR_ZPX, 6);
    cpu_set_instruction(cpu, 0x18, "CLC", cpu_CLC, cpu_IMP, ADDR_IMP, 2);
    cpu_set_instruction(cpu, 0x19, "ORA", cpu_ORA, cpu_ABY, ADDR_ABY, 4);
    cpu_set_instruction(cpu, 0x1D, "ORA", cpu_ORA, cpu_ABX, ADDR_ABX, 4);
    cpu_set_instruction(cpu, 0x1E, "ASL", cpu_ASL, cpu_ABX, ADDR_ABX, 7);

    cpu_set_instruction(cpu, 0x20, "JSR", cpu_JSR, cpu_ABS, ADDR_ABS, 6);
    cpu_set_instruction(cpu, 0x21, "AND", cpu_AND, cpu_IZX, ADDR_IZX, 6);
    cpu_set_instruction(cpu, 0x24, "BIT", cpu_BIT, cpu_ZP0, ADDR_ZP0, 3);
    cpu_set_instruction(cpu, 0x25, "AND", cpu_AND, cpu_ZP0, ADDR_ZP0, 3);
    cpu_set_instruction(cpu, 0x26, "ROL", cpu_ROL, cpu_ZP0, ADDR_ZP0, 5);
    cpu_set_instruction(cpu, 0x28, "PLP", cpu_PLP, cpu_IMP, ADDR_IMP, 4);
    cpu_set_instruction(cpu, 0x29, "AND", cpu_AND, cpu_IMM, ADDR_IMM, 2);
    cpu_set_instruction(cpu, 0x2A, "ROL", cpu_ROL, cpu_IMP, ADDR_IMP, 2);
    cpu_set_instruction(cpu, 0x2C, "BIT", cpu_BIT, cpu_ABS, ADDR_ABS, 4);
    cpu_set_instruction(cpu, 0x2D, "AND", cpu_AND, cpu_ABS, ADDR_ABS, 4);
    cpu_set_instruction(cpu, 0x2E, "ROL", cpu_ROL, cpu_ABS, ADDR_ABS, 6);

    cpu_set_instruction(cpu, 0x30, "BMI", cpu_BMI, cpu_REL, ADDR_REL, 2);
    cpu_set_instruction(cpu, 0x31, "AND", cpu_AND, cpu_IZY, ADDR_IZY, 5);
    cpu_set_instruction(cpu, 0x35, "AND", cpu_AND, cpu_ZPX, ADDR_ZPX, 4);
    cpu_set_instruction(cpu, 0x36, "ROL", cpu_ROL, cpu_ZPX, ADDR_ZPX, 6);
    cpu_set_instruction(cpu, 0x38, "SEC", cpu_SEC, cpu_IMP, ADDR_IMP, 2);
    cpu_set_instruction(cpu, 0x39, "AND", cpu_AND, cpu_ABY, ADDR_ABY, 4);
    cpu_set_instruction(cpu, 0x3D, "AND", cpu_AND, cpu_ABX, ADDR_ABX, 4);
    cpu_set_instruction(cpu, 0x3E, "ROL", cpu_ROL, cpu_ABX, ADDR_ABX, 7);

    cpu_set_instruction(cpu, 0x40, "RTI", cpu_RTI, cpu_IMP, ADDR_IMP, 6);
    cpu_set_instruction(cpu, 0x41, "EOR", cpu_EOR, cpu_IZX, ADDR_IZX, 6);
    cpu_set_instruction(cpu, 0x45, "EOR", cpu_EOR, cpu_ZP0, ADDR_ZP0, 3);
    cpu_set_instruction(cpu, 0x46, "LSR", cpu_LSR, cpu_ZP0, ADDR_ZP0, 5);
    cpu_set_instruction(cpu, 0x48, "PHA", cpu_PHA, cpu_IMP, ADDR_IMP, 3);
    cpu_set_instruction(cpu, 0x49, "EOR", cpu_EOR, cpu_IMM, ADDR_IMM, 2);
    cpu_set_instruction(cpu, 0x4A, "LSR", cpu_LSR, cpu_IMP, ADDR_IMP, 2);
    cpu_set_instruction(cpu, 0x4C, "JMP", cpu_JMP, cpu_ABS, ADDR_ABS, 3);
    cpu_set_instruction(cpu, 0x4D, "EOR", cpu_EOR, cpu_ABS, ADDR_ABS, 4);
    cpu_set_instruction(cpu, 0x4E, "LSR", cpu_LSR, cpu_ABS, ADDR_ABS, 6);

    cpu_set_instruction(cpu, 0x50, "BVC", cpu_BVC, cpu_REL, ADDR_REL, 2);
    cpu_set_instruction(cpu, 0x51, "EOR", cpu_EOR, cpu_IZY, ADDR_IZY, 5);
    cpu_set_instruction(cpu, 0x55, "EOR", cpu_EOR, cpu_ZPX, ADDR_ZPX, 4);
    cpu_set_instruction(cpu, 0x56, "LSR", cpu_LSR, cpu_ZPX, ADDR_ZPX, 6);
    cpu_set_instruction(cpu, 0x58, "CLI", cpu_CLI, cpu_IMP, ADDR_IMP, 2);
    cpu_set_instruction(cpu, 0x59, "EOR", cpu_EOR, cpu_ABY, ADDR_ABY, 4);
    cpu_set_instruction(cpu, 0x5D, "EOR", cpu_EOR, cpu_ABX, ADDR_ABX, 4);
    cpu_set_instruction(cpu, 0x5E, "LSR", cpu_LSR, cpu_ABX, ADDR_ABX, 7);

    cpu_set_instruction(cpu, 0x60, "RTS", cpu_RTS, cpu_IMP, ADDR_IMP, 6);
    cpu_set_instruction(cpu, 0x61, "ADC", cpu_ADC, cpu_IZX, ADDR_IZX, 6);
    cpu_set_instruction(cpu, 0x65, "ADC", cpu_ADC, cpu_ZP0, ADDR_ZP0, 3);
    cpu_set_instruction(cpu, 0x66, "ROR", cpu_ROR, cpu_ZP0, ADDR_ZP0, 5);
    cpu_set_instruction(cpu, 0x68, "PLA", cpu_PLA, cpu_IMP, ADDR_IMP, 4);
    cpu_set_instruction(cpu, 0x69, "ADC", cpu_ADC, cpu_IMM, ADDR_IMM, 2);
    cpu_set_instruction(cpu, 0x6A, "ROR", cpu_ROR, cpu_IMP, ADDR_IMP, 2);
    cpu_set_instruction(cpu, 0x6C, "JMP", cpu_JMP, cpu_IND, ADDR_IND, 5);
    cpu_set_instruction(cpu, 0x6D, "ADC", cpu_ADC, cpu_ABS, ADDR_ABS, 4);
    cpu_set_instruction(cpu, 0x6E, "ROR", cpu_ROR, cpu_ABS, ADDR_ABS, 6);

    cpu_set_instruction(cpu, 0x70, "BVS", cpu_BVS, cpu_REL, ADDR_REL, 2);
    cpu_set_instruction(cpu, 0x71, "ADC", cpu_ADC, cpu_IZY, ADDR_IZY, 5);
    cpu_set_instruction(cpu, 0x75, "ADC", cpu_ADC, cpu_ZPX, ADDR_ZPX, 4);
    cpu_set_instruction(cpu, 0x76, "ROR", cpu_ROR, cpu_ZPX, ADDR_ZPX, 6);
    cpu_set_instruction(cpu, 0x78, "SEI", cpu_SEI, cpu_IMP, ADDR_IMP, 2);
    cpu_set_instruction(cpu, 0x79, "ADC", cpu_ADC, cpu_ABY, ADDR_ABY, 4);
    cpu_set_instruction(cpu, 0x7D, "ADC", cpu_ADC, cpu_ABX, ADDR_ABX, 4);
    cpu_set_instruction(cpu, 0x7E, "ROR", cpu_ROR, cpu_ABX, ADDR_ABX, 7);

    cpu_set_instruction(cpu, 0x81, "STA", cpu_STA, cpu_IZX, ADDR_IZX, 6);
    cpu_set_instruction(cpu, 0x84, "STY", cpu_STY, cpu_ZP0, ADDR_ZP0, 3);
    cpu_set_instruction(cpu, 0x85, "STA", cpu_STA, cpu_ZP0, ADDR_ZP0, 3);
    cpu_set_instruction(cpu, 0x86, "STX", cpu_STX, cpu_ZP0, ADDR_ZP0, 3);
    cpu_set_instruction(cpu, 0x88, "DEY", cpu_DEY, cpu_IMP, ADDR_IMP, 2);
    cpu_set_instruction(cpu, 0x8A, "TXA", cpu_TXA, cpu_IMP, ADDR_IMP, 2);
    cpu_set_instruction(cpu, 0x8C, "STY", cpu_STY, cpu_ABS, ADDR_ABS, 4);
    cpu_set_instruction(cpu, 0x8D, "STA", cpu_STA, cpu_ABS, ADDR_ABS, 4);
    cpu_set_instruction(cpu, 0x8E, "STX", cpu_STX, cpu_ABS, ADDR_ABS, 4);

    cpu_set_instruction(cpu, 0x90, "BCC", cpu_BCC, cpu_REL, ADDR_REL, 2);
    cpu_set_instruction(cpu, 0x91, "STA", cpu_STA, cpu_IZY, ADDR_IZY, 6);
    cpu_set_instruction(cpu, 0x94, "STY", cpu_STY, cpu_ZPX, ADDR_ZPX, 4);
    cpu_set_instruction(cpu, 0x95, "STA", cpu_STA, cpu_ZPX, ADDR_ZPX, 4);
    cpu_set_instruction(cpu, 0x96, "STX", cpu_STX, cpu_ZPY, ADDR_ZPY, 4);
    cpu_set_instruction(cpu, 0x98, "TYA", cpu_TYA, cpu_IMP, ADDR_IMP, 2);
    cpu_set_instruction(cpu, 0x99, "STA", cpu_STA, cpu_ABY, ADDR_ABY, 5);
    cpu_set_instruction(cpu, 0x9A, "TXS", cpu_TXS, cpu_IMP, ADDR_IMP, 2);
    cpu_set_instruction(cpu, 0x9D, "STA", cpu_STA, cpu_ABX, ADDR_ABX, 5);

    cpu_set_instruction(cpu, 0xA0, "LDY", cpu_LDY, cpu_IMM, ADDR_IMM, 2);
    cpu_set_instruction(cpu, 0xA1, "LDA", cpu_LDA, cpu_IZX, ADDR_IZX, 6);
    cpu_set_instruction(cpu, 0xA2, "LDX", cpu_LDX, cpu_IMM, ADDR_IMM, 2);
    cpu_set_instruction(cpu, 0xA4, "LDY", cpu_LDY, cpu_ZP0, ADDR_ZP0, 3);
    cpu_set_instruction(cpu, 0xA5, "LDA", cpu_LDA, cpu_ZP0, ADDR_ZP0, 3);
    cpu_set_instruction(cpu, 0xA6, "LDX", cpu_LDX, cpu_ZP0, ADDR_ZP0, 3);
    cpu_set_instruction(cpu, 0xA8, "TAY", cpu_TAY, cpu_IMP, ADDR_IMP, 2);
    cpu_set_instruction(cpu, 0xA9, "LDA", cpu_LDA, cpu_IMM, ADDR_IMM, 2);
    cpu_set_instruction(cpu, 0xAA, "TAX", cpu_TAX, cpu_IMP, ADDR_IMP, 2);
    cpu_set_instruction(cpu, 0xAC, "LDY", cpu_LDY, cpu_ABS, ADDR_ABS, 4);
    cpu_set_instruction(cpu, 0xAD, "LDA", cpu_LDA, cpu_ABS, ADDR_ABS, 4);
    cpu_set_instruction(cpu, 0xAE, "LDX", cpu_LDX, cpu_ABS, ADDR_ABS, 4);

    cpu_set_instruction(cpu, 0xB0, "BCS", cpu_BCS, cpu_REL, ADDR_REL, 2);
    cpu_set_instruction(cpu, 0xB1, "LDA", cpu_LDA, cpu_IZY, ADDR_IZY, 5);
    cpu_set_instruction(cpu, 0xB4, "LDY", cpu_LDY, cpu_ZPX, ADDR_ZPX, 4);
    cpu_set_instruction(cpu, 0xB5, "LDA", cpu_LDA, cpu_ZPX, ADDR_ZPX, 4);
    cpu_set_instruction(cpu, 0xB6, "LDX", cpu_LDX, cpu_ZPY, ADDR_ZPY, 4);
    cpu_set_instruction(cpu, 0xB8, "CLV", cpu_CLV, cpu_IMP, ADDR_IMP, 2);
    cpu_set_instruction(cpu, 0xB9, "LDA", cpu_LDA, cpu_ABY, ADDR_ABY, 4);
    cpu_set_instruction(cpu, 0xBA, "TSX", cpu_TSX, cpu_IMP, ADDR_IMP, 2);
    cpu_set_instruction(cpu, 0xBC, "LDY", cpu_LDY, cpu_ABX, ADDR_ABX, 4);
    cpu_set_instruction(cpu, 0xBD, "LDA", cpu_LDA, cpu_ABX, ADDR_ABX, 4);
    cpu_set_instruction(cpu, 0xBE, "LDX", cpu_LDX, cpu_ABY, ADDR_ABY, 4);

    cpu_set_instruction(cpu, 0xC0, "CPY", cpu_CPY, cpu_IMM, ADDR_IMM, 2);
    cpu_set_instruction(cpu, 0xC1, "CMP", cpu_CMP, cpu_IZX, ADDR_IZX, 6);
    cpu_set_instruction(cpu, 0xC4, "CPY", cpu_CPY, cpu_ZP0, ADDR_ZP0, 3);
    cpu_set_instruction(cpu, 0xC5, "CMP", cpu_CMP, cpu_ZP0, ADDR_ZP0, 3);
    cpu_set_instruction(cpu, 0xC6, "DEC", cpu_DEC, cpu_ZP0, ADDR_ZP0, 5);
    cpu_set_instruction(cpu, 0xC8, "INY", cpu_INY, cpu_IMP, ADDR_IMP, 2);
    cpu_set_instruction(cpu, 0xC9, "CMP", cpu_CMP, cpu_IMM, ADDR_IMM, 2);
    cpu_set_instruction(cpu, 0xCA, "DEX", cpu_DEX, cpu_IMP, ADDR_IMP, 2);
    cpu_set_instruction(cpu, 0xCC, "CPY", cpu_CPY, cpu_ABS, ADDR_ABS, 4);
    cpu_set_instruction(cpu, 0xCD, "CMP", cpu_CMP, cpu_ABS, ADDR_ABS, 4);
    cpu_set_instruction(cpu, 0xCE, "DEC", cpu_DEC, cpu_ABS, ADDR_ABS, 6);

    cpu_set_instruction(cpu, 0xD0, "BNE", cpu_BNE, cpu_REL, ADDR_REL, 2);
    cpu_set_instruction(cpu, 0xD1, "CMP", cpu_CMP, cpu_IZY, ADDR_IZY, 5);
    cpu_set_instruction(cpu, 0xD5, "CMP", cpu_CMP, cpu_ZPX, ADDR_ZPX, 4);
    cpu_set_instruction(cpu, 0xD6, "DEC", cpu_DEC, cpu_ZPX, ADDR_ZPX, 6);
    cpu_set_instruction(cpu, 0xD8, "CLD", cpu_CLD, cpu_IMP, ADDR_IMP, 2);
    cpu_set_instruction(cpu, 0xD9, "CMP", cpu_CMP, cpu_ABY, ADDR_ABY, 4);
    cpu_set_instruction(cpu, 0xDD, "CMP", cpu_CMP, cpu_ABX, ADDR_ABX, 4);
    cpu_set_instruction(cpu, 0xDE, "DEC", cpu_DEC, cpu_ABX, ADDR_ABX, 7);

    cpu_set_instruction(cpu, 0xE0, "CPX", cpu_CPX, cpu_IMM, ADDR_IMM, 2);
    cpu_set_instruction(cpu, 0xE1, "SBC", cpu_SBC, cpu_IZX, ADDR_IZX, 6);
    cpu_set_instruction(cpu, 0xE4, "CPX", cpu_CPX, cpu_ZP0, ADDR_ZP0, 3);
    cpu_set_instruction(cpu, 0xE5, "SBC", cpu_SBC, cpu_ZP0, ADDR_ZP0, 3);
    cpu_set_instruction(cpu, 0xE6, "INC", cpu_INC, cpu_ZP0, ADDR_ZP0, 5);
    cpu_set_instruction(cpu, 0xE8, "INX", cpu_INX, cpu_IMP, ADDR_IMP, 2);
    cpu_set_instruction(cpu, 0xE9, "SBC", cpu_SBC, cpu_IMM, ADDR_IMM, 2);
    cpu_set_instruction(cpu, 0xEA, "NOP", cpu_NOP, cpu_IMP, ADDR_IMP, 2);
    cpu_set_instruction(cpu, 0xEC, "CPX", cpu_CPX, cpu_ABS, ADDR_ABS, 4);
    cpu_set_instruction(cpu, 0xED, "SBC", cpu_SBC, cpu_ABS, ADDR_ABS, 4);
    cpu_set_instruction(cpu, 0xEE, "INC", cpu_INC, cpu_ABS, ADDR_ABS, 6);

    cpu_set_instruction(cpu, 0xF0, "BEQ", cpu_BEQ, cpu_REL, ADDR_REL, 2);
    cpu_set_instruction(cpu, 0xF1, "SBC", cpu_SBC, cpu_IZY, ADDR_IZY, 5);
    cpu_set_instruction(cpu, 0xF5, "SBC", cpu_SBC, cpu_ZPX, ADDR_ZPX, 4);
    cpu_set_instruction(cpu, 0xF6, "INC", cpu_INC, cpu_ZPX, ADDR_ZPX, 6);
    cpu_set_instruction(cpu, 0xF8, "SED", cpu_SED, cpu_IMP, ADDR_IMP, 2);
    cpu_set_instruction(cpu, 0xF9, "SBC", cpu_SBC, cpu_ABY, ADDR_ABY, 4);
    cpu_set_instruction(cpu, 0xFD, "SBC", cpu_SBC, cpu_ABX, ADDR_ABX, 4);
    cpu_set_instruction(cpu, 0xFE, "INC", cpu_INC, cpu_ABX, ADDR_ABX, 7);

    cpu_set_instruction(cpu, 0x03, "SLO", cpu_SLO, cpu_IZX, ADDR_IZX, 8);
    cpu_set_instruction(cpu, 0x07, "SLO", cpu_SLO, cpu_ZP0, ADDR_ZP0, 5);
    cpu_set_instruction(cpu, 0x0F, "SLO", cpu_SLO, cpu_ABS, ADDR_ABS, 6);
    cpu_set_instruction(cpu, 0x13, "SLO", cpu_SLO, cpu_IZY, ADDR_IZY, 8);
    cpu_set_instruction(cpu, 0x17, "SLO", cpu_SLO, cpu_ZPX, ADDR_ZPX, 6);
    cpu_set_instruction(cpu, 0x1B, "SLO", cpu_SLO, cpu_ABY, ADDR_ABY, 7);
    cpu_set_instruction(cpu, 0x1F, "SLO", cpu_SLO, cpu_ABX, ADDR_ABX, 7);

    cpu_set_instruction(cpu, 0x23, "RLA", cpu_RLA, cpu_IZX, ADDR_IZX, 8);
    cpu_set_instruction(cpu, 0x27, "RLA", cpu_RLA, cpu_ZP0, ADDR_ZP0, 5);
    cpu_set_instruction(cpu, 0x2F, "RLA", cpu_RLA, cpu_ABS, ADDR_ABS, 6);
    cpu_set_instruction(cpu, 0x33, "RLA", cpu_RLA, cpu_IZY, ADDR_IZY, 8);
    cpu_set_instruction(cpu, 0x37, "RLA", cpu_RLA, cpu_ZPX, ADDR_ZPX, 6);
    cpu_set_instruction(cpu, 0x3B, "RLA", cpu_RLA, cpu_ABY, ADDR_ABY, 7);
    cpu_set_instruction(cpu, 0x3F, "RLA", cpu_RLA, cpu_ABX, ADDR_ABX, 7);

    cpu_set_instruction(cpu, 0x43, "SRE", cpu_SRE, cpu_IZX, ADDR_IZX, 8);
    cpu_set_instruction(cpu, 0x47, "SRE", cpu_SRE, cpu_ZP0, ADDR_ZP0, 5);
    cpu_set_instruction(cpu, 0x4F, "SRE", cpu_SRE, cpu_ABS, ADDR_ABS, 6);
    cpu_set_instruction(cpu, 0x53, "SRE", cpu_SRE, cpu_IZY, ADDR_IZY, 8);
    cpu_set_instruction(cpu, 0x57, "SRE", cpu_SRE, cpu_ZPX, ADDR_ZPX, 6);
    cpu_set_instruction(cpu, 0x5B, "SRE", cpu_SRE, cpu_ABY, ADDR_ABY, 7);
    cpu_set_instruction(cpu, 0x5F, "SRE", cpu_SRE, cpu_ABX, ADDR_ABX, 7);

    cpu_set_instruction(cpu, 0x63, "RRA", cpu_RRA, cpu_IZX, ADDR_IZX, 8);
    cpu_set_instruction(cpu, 0x67, "RRA", cpu_RRA, cpu_ZP0, ADDR_ZP0, 5);
    cpu_set_instruction(cpu, 0x6F, "RRA", cpu_RRA, cpu_ABS, ADDR_ABS, 6);
    cpu_set_instruction(cpu, 0x73, "RRA", cpu_RRA, cpu_IZY, ADDR_IZY, 8);
    cpu_set_instruction(cpu, 0x77, "RRA", cpu_RRA, cpu_ZPX, ADDR_ZPX, 6);
    cpu_set_instruction(cpu, 0x7B, "RRA", cpu_RRA, cpu_ABY, ADDR_ABY, 7);
    cpu_set_instruction(cpu, 0x7F, "RRA", cpu_RRA, cpu_ABX, ADDR_ABX, 7);

    cpu_set_instruction(cpu, 0x83, "SAX", cpu_SAX, cpu_IZX, ADDR_IZX, 6);
    cpu_set_instruction(cpu, 0x87, "SAX", cpu_SAX, cpu_ZP0, ADDR_ZP0, 3);
    cpu_set_instruction(cpu, 0x8F, "SAX", cpu_SAX, cpu_ABS, ADDR_ABS, 4);

    cpu_set_instruction(cpu, 0x97, "SAX", cpu_SAX, cpu_ZPY, ADDR_ZPY, 4);

    cpu_set_instruction(cpu, 0xA3, "LAX", cpu_LAX, cpu_IZX, ADDR_IZX, 6);
    cpu_set_instruction(cpu, 0xA7, "LAX", cpu_LAX, cpu_ZP0, ADDR_ZP0, 3);
    cpu_set_instruction(cpu, 0xAF, "LAX", cpu_LAX, cpu_ABS, ADDR_ABS, 4);
    cpu_set_instruction(cpu, 0xB3, "LAX", cpu_LAX, cpu_IZY, ADDR_IZY, 5);
    cpu_set_instruction(cpu, 0xB7, "LAX", cpu_LAX, cpu_ZPY, ADDR_ZPY, 4);
    cpu_set_instruction(cpu, 0xBF, "LAX", cpu_LAX, cpu_ABY, ADDR_ABY, 4);

    cpu_set_instruction(cpu, 0xC3, "DCP", cpu_DCP, cpu_IZX, ADDR_IZX, 8);
    cpu_set_instruction(cpu, 0xC7, "DCP", cpu_DCP, cpu_ZP0, ADDR_ZP0, 5);
    cpu_set_instruction(cpu, 0xCF, "DCP", cpu_DCP, cpu_ABS, ADDR_ABS, 6);
    cpu_set_instruction(cpu, 0xD3, "DCP", cpu_DCP, cpu_IZY, ADDR_IZY, 8);
    cpu_set_instruction(cpu, 0xD7, "DCP", cpu_DCP, cpu_ZPX, ADDR_ZPX, 6);
    cpu_set_instruction(cpu, 0xDB, "DCP", cpu_DCP, cpu_ABY, ADDR_ABY, 7);
    cpu_set_instruction(cpu, 0xDF, "DCP", cpu_DCP, cpu_ABX, ADDR_ABX, 7);

    cpu_set_instruction(cpu, 0xE3, "ISC", cpu_ISC, cpu_IZX, ADDR_IZX, 8);
    cpu_set_instruction(cpu, 0xE7, "ISC", cpu_ISC, cpu_ZP0, ADDR_ZP0, 5);
    cpu_set_instruction(cpu, 0xEF, "ISC", cpu_ISC, cpu_ABS, ADDR_ABS, 6);
    cpu_set_instruction(cpu, 0xF3, "ISC", cpu_ISC, cpu_IZY, ADDR_IZY, 8);
    cpu_set_instruction(cpu, 0xF7, "ISC", cpu_ISC, cpu_ZPX, ADDR_ZPX, 6);
    cpu_set_instruction(cpu, 0xFB, "ISC", cpu_ISC, cpu_ABY, ADDR_ABY, 7);
    cpu_set_instruction(cpu, 0xFF, "ISC", cpu_ISC, cpu_ABX, ADDR_ABX, 7);

    cpu_set_instruction(cpu, 0x0B, "ANC", cpu_ANC, cpu_IMM, ADDR_IMM, 2);
    cpu_set_instruction(cpu, 0x2B, "ANC", cpu_ANC, cpu_IMM, ADDR_IMM, 2);
    cpu_set_instruction(cpu, 0x4B, "ASR", cpu_ASR, cpu_IMM, ADDR_IMM, 2);
    cpu_set_instruction(cpu, 0x6B, "ARR", cpu_ARR, cpu_IMM, ADDR_IMM, 2);
    cpu_set_instruction(cpu, 0x8B, "ANE", cpu_ANE, cpu_IMM, ADDR_IMM, 2);
    cpu_set_instruction(cpu, 0xAB, "LXA", cpu_LXA, cpu_IMM, ADDR_IMM, 2);
    cpu_set_instruction(cpu, 0xCB, "AXS", cpu_AXS, cpu_IMM, ADDR_IMM, 2);
    cpu_set_instruction(cpu, 0x9F, "SHA", cpu_SHA, cpu_ABY, ADDR_ABY, 5);
    cpu_set_instruction(cpu, 0x93, "SHA", cpu_SHA, cpu_IZY, ADDR_IZY, 6);
    cpu_set_instruction(cpu, 0x9E, "SHX", cpu_SHX, cpu_ABY, ADDR_ABY, 5);
    cpu_set_instruction(cpu, 0x9C, "SHY", cpu_SHY, cpu_ABX, ADDR_ABX, 5);
    cpu_set_instruction(cpu, 0x9B, "SHS", cpu_SHS, cpu_ABY, ADDR_ABY, 5);
    cpu_set_instruction(cpu, 0xBB, "LAE", cpu_LAE, cpu_ABY, ADDR_ABY, 4);

    cpu_set_instruction(cpu, 0x1A, "NOP", cpu_NOP, cpu_IMP, ADDR_IMP, 2);
    cpu_set_instruction(cpu, 0x3A, "NOP", cpu_NOP, cpu_IMP, ADDR_IMP, 2);
    cpu_set_instruction(cpu, 0x5A, "NOP", cpu_NOP, cpu_IMP, ADDR_IMP, 2);
    cpu_set_instruction(cpu, 0x7A, "NOP", cpu_NOP, cpu_IMP, ADDR_IMP, 2);
    cpu_set_instruction(cpu, 0xDA, "NOP", cpu_NOP, cpu_IMP, ADDR_IMP, 2);
    cpu_set_instruction(cpu, 0xFA, "NOP", cpu_NOP, cpu_IMP, ADDR_IMP, 2);

    cpu_set_instruction(cpu, 0x80, "NOP", cpu_NOPR, cpu_IMM, ADDR_IMM, 2);
    cpu_set_instruction(cpu, 0x82, "NOP", cpu_NOPR, cpu_IMM, ADDR_IMM, 2);
    cpu_set_instruction(cpu, 0x89, "NOP", cpu_NOPR, cpu_IMM, ADDR_IMM, 2);
    cpu_set_instruction(cpu, 0xC2, "NOP", cpu_NOPR, cpu_IMM, ADDR_IMM, 2);
    cpu_set_instruction(cpu, 0xE2, "NOP", cpu_NOPR, cpu_IMM, ADDR_IMM, 2);

    cpu_set_instruction(cpu, 0x04, "NOP", cpu_NOPR, cpu_ZP0, ADDR_ZP0, 3);
    cpu_set_instruction(cpu, 0x44, "NOP", cpu_NOPR, cpu_ZP0, ADDR_ZP0, 3);
    cpu_set_instruction(cpu, 0x64, "NOP", cpu_NOPR, cpu_ZP0, ADDR_ZP0, 3);

    cpu_set_instruction(cpu, 0x14, "NOP", cpu_NOPR, cpu_ZPX, ADDR_ZPX, 4);
    cpu_set_instruction(cpu, 0x34, "NOP", cpu_NOPR, cpu_ZPX, ADDR_ZPX, 4);
    cpu_set_instruction(cpu, 0x54, "NOP", cpu_NOPR, cpu_ZPX, ADDR_ZPX, 4);
    cpu_set_instruction(cpu, 0x74, "NOP", cpu_NOPR, cpu_ZPX, ADDR_ZPX, 4);
    cpu_set_instruction(cpu, 0xD4, "NOP", cpu_NOPR, cpu_ZPX, ADDR_ZPX, 4);
    cpu_set_instruction(cpu, 0xF4, "NOP", cpu_NOPR, cpu_ZPX, ADDR_ZPX, 4);

    cpu_set_instruction(cpu, 0x0C, "NOP", cpu_NOPR, cpu_ABS, ADDR_ABS, 4);
    cpu_set_instruction(cpu, 0x1C, "NOP", cpu_NOPR, cpu_ABX, ADDR_ABX, 4);
    cpu_set_instruction(cpu, 0x3C, "NOP", cpu_NOPR, cpu_ABX, ADDR_ABX, 4);
    cpu_set_instruction(cpu, 0x5C, "NOP", cpu_NOPR, cpu_ABX, ADDR_ABX, 4);
    cpu_set_instruction(cpu, 0x7C, "NOP", cpu_NOPR, cpu_ABX, ADDR_ABX, 4);
    cpu_set_instruction(cpu, 0xDC, "NOP", cpu_NOPR, cpu_ABX, ADDR_ABX, 4);
    cpu_set_instruction(cpu, 0xFC, "NOP", cpu_NOPR, cpu_ABX, ADDR_ABX, 4);
}

void cpu_init(CPU *cpu) {
    cpu_build_table(cpu);
}

void cpu_reset(CPU *cpu) {
    cpu->a = 0;
    cpu->x = 0;
    cpu->y = 0;
    cpu->sp = 0xFD;
    cpu->status = CPU_FLAG_U | CPU_FLAG_I;
    uint8_t lo = cpu_read(cpu, 0xFFFC);
    uint8_t hi = cpu_read(cpu, 0xFFFD);
    cpu->pc = (uint16_t)(hi << 8) | lo;
}

void cpu_irq(CPU *cpu) {
    if (cpu_get_flag(cpu, CPU_FLAG_I) == 0) {
        cpu_push(cpu, (uint8_t)((cpu->pc >> 8) & 0xFF));
        cpu_push(cpu, (uint8_t)(cpu->pc & 0xFF));
        cpu_set_flag(cpu, CPU_FLAG_I, true);
        cpu_push_status(cpu, false);
        cpu_set_flag(cpu, CPU_FLAG_B, false);
        cpu_set_flag(cpu, CPU_FLAG_U, true);
        uint8_t lo = cpu_read(cpu, 0xFFFE);
        uint8_t hi = cpu_read(cpu, 0xFFFF);
        cpu->pc = (uint16_t)(hi << 8) | lo;
    }
}

void cpu_nmi(CPU *cpu) {
    cpu_push(cpu, (uint8_t)((cpu->pc >> 8) & 0xFF));
    cpu_push(cpu, (uint8_t)(cpu->pc & 0xFF));
    cpu_set_flag(cpu, CPU_FLAG_I, true);
    cpu_push_status(cpu, false);
    cpu_set_flag(cpu, CPU_FLAG_B, false);
    cpu_set_flag(cpu, CPU_FLAG_U, true);
    uint8_t lo = cpu_read(cpu, 0xFFFA);
    uint8_t hi = cpu_read(cpu, 0xFFFB);
    cpu->pc = (uint16_t)(hi << 8) | lo;
}

int cpu_step(CPU *cpu) {
    if (cpu->bus && bus_consume_stall(cpu->bus)) {
        cpu->cycleCounter += 1;
        bus_tick(cpu->bus, 1);
        return 1;
    }

    cpu->opcode = bus_cpu_read_opcode(cpu->bus, cpu->pc);
    cpu->pc += 1;

    Instruction *inst = &cpu->instructions[cpu->opcode];
    uint8_t additional1 = inst->addrMode(cpu);
    uint8_t additional2 = inst->operate(cpu);
    uint8_t cycles = inst->cycles + (additional1 & additional2);
    cpu->cycleCounter += cycles;
    cpu->status |= CPU_FLAG_U;
    if (cpu->bus && bus_is_irq_pending(cpu->bus) && cpu_get_flag(cpu, CPU_FLAG_I) == 0) {
        bus_ack_irq(cpu->bus);
        cpu_irq(cpu);
    }
    if (cpu->bus) {
        bus_tick(cpu->bus, cycles);
    }
    return cycles;
}
