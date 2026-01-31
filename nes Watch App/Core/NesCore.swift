import AVFoundation
import CoreGraphics
import Foundation

typealias NESRef = OpaquePointer

@_silgen_name("nes_create") private func nes_create() -> NESRef?
@_silgen_name("nes_destroy") private func nes_destroy(_ nes: NESRef)
@_silgen_name("nes_load_rom") private func nes_load_rom(_ nes: NESRef, _ data: UnsafePointer<UInt8>, _ size: Int) -> Bool
@_silgen_name("nes_reset") private func nes_reset(_ nes: NESRef)
@_silgen_name("nes_step_frame") private func nes_step_frame(_ nes: NESRef)
@_silgen_name("nes_framebuffer") private func nes_framebuffer(_ nes: NESRef) -> UnsafePointer<UInt32>?
@_silgen_name("nes_framebuffer_width") private func nes_framebuffer_width() -> Int32
@_silgen_name("nes_framebuffer_height") private func nes_framebuffer_height() -> Int32
@_silgen_name("nes_set_button") private func nes_set_button(_ nes: NESRef, _ button: UInt8, _ pressed: Bool)
@_silgen_name("nes_apu_next_sample") private func nes_apu_next_sample(_ nes: NESRef, _ sampleRate: Double) -> Float

final class EmulatorCore {
    private var nes: NESRef?

    init() {
        nes = nes_create()
    }

    deinit {
        if let nes {
            nes_destroy(nes)
        }
    }

    func loadRom(_ data: Data) -> Bool {
        return data.withUnsafeBytes { buffer in
            guard let base = buffer.bindMemory(to: UInt8.self).baseAddress, let nes else {
                return false
            }
            return nes_load_rom(nes, base, data.count)
        }
    }

    func reset() {
        guard let nes else { return }
        nes_reset(nes)
    }

    func stepFrame() {
        guard let nes else { return }
        nes_step_frame(nes)
    }

    func currentFrameImage() -> CGImage? {
        guard let nes else { return nil }
        guard let buffer = nes_framebuffer(nes) else { return nil }
        let width = Int(nes_framebuffer_width())
        let height = Int(nes_framebuffer_height())
        let count = width * height
        let data = Data(bytes: buffer, count: count * MemoryLayout<UInt32>.size)
        guard let provider = CGDataProvider(data: data as CFData) else { return nil }
        let colorSpace = CGColorSpaceCreateDeviceRGB()
        let bitmapInfo = CGBitmapInfo.byteOrder32Little.union(
            CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedFirst.rawValue)
        )

        return CGImage(
            width: width,
            height: height,
            bitsPerComponent: 8,
            bitsPerPixel: 32,
            bytesPerRow: width * 4,
            space: colorSpace,
            bitmapInfo: bitmapInfo,
            provider: provider,
            decode: nil,
            shouldInterpolate: false,
            intent: .defaultIntent
        )
    }

    func setButton(_ button: Controller.Button, pressed: Bool) {
        guard let nes else { return }
        nes_set_button(nes, button.rawValue, pressed)
    }

    func makeAudioEngine() -> CAudioEngine? {
        guard let nes else { return nil }
        return CAudioEngine(nes: nes)
    }
}

final class CAudioEngine {
    private let nes: NESRef
    private var engine = AVAudioEngine()
    private var format: AVAudioFormat?
    private var sourceNode: AVAudioSourceNode?
    private var observers: [NSObjectProtocol] = []

    init(nes: NESRef) {
        self.nes = nes
        let center = NotificationCenter.default
        let session = AVAudioSession.sharedInstance()
        observers.append(center.addObserver(forName: AVAudioSession.interruptionNotification, object: session, queue: .main) { [weak self] note in
            self?.handleInterruption(note)
        })
        observers.append(center.addObserver(forName: AVAudioSession.mediaServicesWereResetNotification, object: session, queue: .main) { [weak self] _ in
            self?.handleMediaServicesReset()
        })
    }

    func start() {
        start(rebuildIfNeeded: true)
    }

    func stop() {
        engine.pause()
    }

    func shutdown() {
        engine.stop()
        if let node = sourceNode {
            engine.detach(node)
            sourceNode = nil
        }
    }

    deinit {
        let center = NotificationCenter.default
        for obs in observers {
            center.removeObserver(obs)
        }
    }

    private func handleInterruption(_ note: Notification) {
        guard let info = note.userInfo,
              let typeValue = info[AVAudioSessionInterruptionTypeKey] as? UInt,
              let type = AVAudioSession.InterruptionType(rawValue: typeValue) else {
            return
        }
        if type == .began {
            engine.pause()
            return
        }
        let optionsValue = info[AVAudioSessionInterruptionOptionKey] as? UInt ?? 0
        let options = AVAudioSession.InterruptionOptions(rawValue: optionsValue)
        if options.contains(.shouldResume) {
            start()
        }
    }

    private func handleMediaServicesReset() {
        rebuildEngine()
    }

    private func start(rebuildIfNeeded: Bool) {
        let outputFormat = engine.outputNode.inputFormat(forBus: 0)
        let sampleRate = outputFormat.sampleRate > 0 ? outputFormat.sampleRate : 44100
        let channelCount = outputFormat.channelCount > 0 ? outputFormat.channelCount : 2
        let activeFormat = AVAudioFormat(standardFormatWithSampleRate: sampleRate, channels: channelCount)!
        format = activeFormat

        if sourceNode == nil {
            let source = AVAudioSourceNode { [weak self] _, _, frameCount, audioBufferList -> OSStatus in
                guard let self else { return noErr }
                let bufferList = UnsafeMutableAudioBufferListPointer(audioBufferList)
                let frameCountInt = Int(frameCount)
                let rate = activeFormat.sampleRate
                if bufferList.count == 0 {
                    return noErr
                }
                let bytes = frameCountInt * MemoryLayout<Float>.size
                guard let firstData = bufferList[0].mData else { return noErr }
                bufferList[0].mDataByteSize = UInt32(bytes)
                let firstSamples = firstData.assumingMemoryBound(to: Float.self)
                for i in 0..<frameCountInt {
                    firstSamples[i] = nes_apu_next_sample(self.nes, rate)
                }
                if bufferList.count > 1 {
                    for bufferIndex in 1..<bufferList.count {
                        guard let mData = bufferList[bufferIndex].mData else { continue }
                        bufferList[bufferIndex].mDataByteSize = UInt32(bytes)
                        mData.copyMemory(from: firstData, byteCount: bytes)
                    }
                }
                return noErr
            }

            sourceNode = source
            engine.attach(source)
            engine.connect(source, to: engine.mainMixerNode, format: activeFormat)
        }

        engine.mainMixerNode.outputVolume = 0.8

        ensureOutputConnection()

        if !engine.isRunning {
            do {
                engine.prepare()
                try engine.start()
            } catch {
                if rebuildIfNeeded {
                    rebuildEngine()
                }
            }
        }
    }

    private func rebuildEngine() {
        engine.stop()
        if let node = sourceNode {
            engine.detach(node)
        }
        sourceNode = nil
        format = nil
        engine = AVAudioEngine()
        start(rebuildIfNeeded: false)
    }

    private func ensureOutputConnection() {
        let points = engine.outputConnectionPoints(for: engine.mainMixerNode, outputBus: 0)
        if points.isEmpty {
            let outputFormat = engine.outputNode.inputFormat(forBus: 0)
            if outputFormat.sampleRate > 0 && outputFormat.channelCount > 0 {
                engine.connect(engine.mainMixerNode, to: engine.outputNode, format: outputFormat)
            } else {
                engine.connect(engine.mainMixerNode, to: engine.outputNode, format: nil)
            }
        }
    }
}
