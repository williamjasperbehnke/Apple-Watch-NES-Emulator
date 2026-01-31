import CoreGraphics
import Foundation
import SwiftUI

final class EmulatorViewModel: ObservableObject {
    @Published var frameImage: CGImage?
    @Published var status: String = "Idle"
    @Published private(set) var romNames: [String] = []

    private let core = EmulatorCore()
    private var timer: DispatchSourceTimer?
    private let emuQueue = DispatchQueue(label: "nes.emulator.queue", qos: .userInitiated)
    private var audioEngine: CAudioEngine?

    init() {
        romNames = Self.discoverRoms()
    }

    func loadDefaultRom() {
        guard let first = romNames.first else {
            status = "Missing .nes file in app bundle."
            return
        }
        loadRom(named: first)
    }

    func loadRom(named name: String, autoStart: Bool = false, completion: ((Bool) -> Void)? = nil) {
        stop()
        DispatchQueue.global(qos: .userInitiated).async {
            let url = Bundle.main.url(forResource: name, withExtension: "nes", subdirectory: "Roms")
                ?? Bundle.main.url(forResource: name, withExtension: "nes")
            guard let url else {
                DispatchQueue.main.async {
                    self.status = "Missing .nes file in app bundle."
                    completion?(false)
                }
                return
            }
            do {
                let data = try Data(contentsOf: url)
                guard self.core.loadRom(data) else {
                    DispatchQueue.main.async {
                        self.status = "Unsupported ROM format or mapper."
                        completion?(false)
                    }
                    return
                }
                DispatchQueue.main.async {
                    self.status = "ROM loaded"
                    completion?(true)
                    if autoStart {
                        self.start()
                    }
                }
                self.emuQueue.async {
                    self.core.stepFrame()
                    let image = self.core.currentFrameImage()
                    DispatchQueue.main.async {
                        self.frameImage = image
                    }
                }
            } catch {
                DispatchQueue.main.async {
                    self.status = "Failed to load ROM: \(error.localizedDescription)"
                    completion?(false)
                }
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

    private static func discoverRoms() -> [String] {
        var urls: [URL] = []
        if let roms = Bundle.main.urls(forResourcesWithExtension: "nes", subdirectory: "Roms") {
            urls.append(contentsOf: roms)
        }
        if let root = Bundle.main.urls(forResourcesWithExtension: "nes", subdirectory: nil) {
            urls.append(contentsOf: root)
        }
        if urls.isEmpty, let resourceURL = Bundle.main.resourceURL {
            if let enumerator = FileManager.default.enumerator(at: resourceURL, includingPropertiesForKeys: nil) {
                for case let fileURL as URL in enumerator {
                    if fileURL.pathExtension.lowercased() == "nes" {
                        urls.append(fileURL)
                    }
                }
            }
        }
        let names = urls.map { $0.deletingPathExtension().lastPathComponent }
        return Array(Set(names)).sorted()
    }
}
