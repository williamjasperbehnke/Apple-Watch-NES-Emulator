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
    private let engine = AVAudioEngine()
    private let format: AVAudioFormat
    private var sourceNode: AVAudioSourceNode?

    init(nes: NESRef) {
        self.nes = nes
        self.format = AVAudioFormat(standardFormatWithSampleRate: 44100, channels: 1)!
    }

    func start() {
        if sourceNode != nil { return }

        do {
            let session = AVAudioSession.sharedInstance()
            try session.setCategory(.ambient, options: [.mixWithOthers])
            try session.setActive(true)
        } catch {
            // Best-effort on watchOS.
        }

        let source = AVAudioSourceNode { [weak self] _, _, frameCount, audioBufferList -> OSStatus in
            guard let self else { return noErr }
            let bufferList = UnsafeMutableAudioBufferListPointer(audioBufferList)
            guard let buffer = bufferList.first else { return noErr }
            let frameCountInt = Int(frameCount)
            let samples = buffer.mData!.assumingMemoryBound(to: Float.self)
            let rate = self.format.sampleRate
            for i in 0..<frameCountInt {
                samples[i] = nes_apu_next_sample(self.nes, rate)
            }
            return noErr
        }

        sourceNode = source
        engine.attach(source)
        engine.connect(source, to: engine.mainMixerNode, format: format)
        engine.mainMixerNode.outputVolume = 0.8

        do {
            try engine.start()
        } catch {
            // Leave engine stopped on failure.
        }
    }

    func stop() {
        engine.stop()
        if let node = sourceNode {
            engine.detach(node)
            sourceNode = nil
        }
    }
}
