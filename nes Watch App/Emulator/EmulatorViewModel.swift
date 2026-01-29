import CoreGraphics
import Foundation
import SwiftUI

final class EmulatorViewModel: ObservableObject {
    @Published var frameImage: CGImage?
    @Published var status: String = "Idle"

    private let nes = NES()
    private var timer: DispatchSourceTimer?
    private let emuQueue = DispatchQueue(label: "nes.emulator.queue", qos: .userInitiated)

    func loadDefaultRom() {
        DispatchQueue.global(qos: .userInitiated).async {
            guard let url = Bundle.main.url(forResource: "AccuracyCoin", withExtension: "nes") else {
                DispatchQueue.main.async { self.status = "Missing AccuracyCoin.nes in app bundle." }
                return
            }
            do {
                let data = try Data(contentsOf: url)
                guard let cart = Cartridge(data: data) else {
                    DispatchQueue.main.async { self.status = "Unsupported ROM format or mapper." }
                    return
                }
                self.nes.loadCartridge(cart)
                DispatchQueue.main.async { self.status = "ROM loaded" }
                self.emuQueue.async {
                    self.nes.stepFrame()
                    let image = self.nes.currentFrameImage()
                    DispatchQueue.main.async {
                        self.frameImage = image
                    }
                }
            } catch {
                DispatchQueue.main.async { self.status = "Failed to load ROM: \(error.localizedDescription)" }
            }
        }
    }

    func start() {
        timer?.cancel()
        let timer = DispatchSource.makeTimerSource(queue: emuQueue)
        timer.schedule(deadline: .now(), repeating: 1.0 / 60.0)
        timer.setEventHandler { [weak self] in
            guard let self else { return }
            self.nes.stepFrame()
            let image = self.nes.currentFrameImage()
            Task { @MainActor in
                self.frameImage = image
            }
        }
        self.timer = timer
        timer.resume()
    }

    func stop() {
        timer?.cancel()
        timer = nil
    }

    func setButton(_ button: Controller.Button, pressed: Bool) {
        nes.setButton(button, pressed: pressed)
    }
}
