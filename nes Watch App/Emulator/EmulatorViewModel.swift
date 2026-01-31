import CoreGraphics
import Foundation
import SwiftUI

final class EmulatorViewModel: ObservableObject {
    @Published var frameImage: CGImage?
    @Published var status: String = "Idle"

    private let core = EmulatorCore()
    private var timer: DispatchSourceTimer?
    private let emuQueue = DispatchQueue(label: "nes.emulator.queue", qos: .userInitiated)
    private var audioEngine: CAudioEngine?

    func loadDefaultRom() {
        DispatchQueue.global(qos: .userInitiated).async {
            guard let url = Bundle.main.url(forResource: "DigDug", withExtension: "nes") else {
                DispatchQueue.main.async { self.status = "Missing .nes file in app bundle." }
                return
            }
            do {
                let data = try Data(contentsOf: url)
                guard self.core.loadRom(data) else {
                    DispatchQueue.main.async { self.status = "Unsupported ROM format or mapper." }
                    return
                }
                DispatchQueue.main.async { self.status = "ROM loaded" }
                self.emuQueue.async {
                    self.core.stepFrame()
                    let image = self.core.currentFrameImage()
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
            self.core.stepFrame()
            let image = self.core.currentFrameImage()
            Task { @MainActor in
                self.frameImage = image
            }
        }
        self.timer = timer
        if audioEngine == nil {
            audioEngine = core.makeAudioEngine()
        }
        audioEngine?.start()
        timer.resume()
    }

    func stop() {
        timer?.cancel()
        timer = nil
        audioEngine?.stop()
    }

    func setButton(_ button: Controller.Button, pressed: Bool) {
        core.setButton(button, pressed: pressed)
    }
}
