import CoreGraphics

final class NES {
    private let bus = Bus()
    private let cpu = CPU6502()
    private let ppu = PPU()

    private var cartridge: Cartridge?

    init() {
        cpu.bus = bus
        bus.cpu = cpu
        bus.ppu = ppu
    }

    func loadCartridge(_ cartridge: Cartridge) {
        self.cartridge = cartridge
        bus.cartridge = cartridge
        ppu.connectCartridge(cartridge)
        reset()
    }

    func reset() {
        cpu.reset()
    }

    func stepFrame() {
        guard cartridge != nil else { return }
        ppu.resetFrameState()
        while !ppu.frameComplete {
            let cycles = cpu.step()
            if cycles > 0 {
                for _ in 0..<(cycles * 3) {
                    ppu.tick()
                    if ppu.nmiRequested {
                        cpu.nmi()
                    }
                }
            }
        }
    }

    func currentFrameImage() -> CGImage? {
        return ppu.frameBuffer.makeImage()
    }

    func setButton(_ button: Controller.Button, pressed: Bool) {
        bus.controller.setButton(button, pressed: pressed)
    }
}
