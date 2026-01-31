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
@_silgen_name("nes_apu_fill_buffer") private func nes_apu_fill_buffer(_ nes: NESRef, _ sampleRate: Double, _ out: UnsafeMutablePointer<Float>, _ count: Int32)

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
    private var ring = AudioRingBuffer(capacity: 88200)
    private var sampleRate: Double = 44100
    private var samplesPerFrame: Int = 735
    private let audioQueue = DispatchQueue(label: "nes.audio.queue", qos: .userInitiated)
    private var audioTimer: DispatchSourceTimer?

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
        stopProducer()
    }

    func shutdown() {
        engine.stop()
        if let node = sourceNode {
            engine.detach(node)
            sourceNode = nil
        }
        stopProducer()
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
        self.sampleRate = sampleRate
        self.samplesPerFrame = max(1, Int(sampleRate / 60.0))
        ring = AudioRingBuffer(capacity: max(4096, Int(sampleRate * 3)))

        if sourceNode == nil {
            let source = AVAudioSourceNode { [weak self] _, _, frameCount, audioBufferList -> OSStatus in
                guard let self else { return noErr }
                let bufferList = UnsafeMutableAudioBufferListPointer(audioBufferList)
                let frameCountInt = Int(frameCount)
                if bufferList.count == 0 {
                    return noErr
                }
                let bytes = frameCountInt * MemoryLayout<Float>.size
                guard let firstData = bufferList[0].mData else { return noErr }
                bufferList[0].mDataByteSize = UInt32(bytes)
                let firstSamples = firstData.assumingMemoryBound(to: Float.self)
                let read = self.ring.read(into: firstSamples, count: frameCountInt)
                if read < frameCountInt {
                    firstSamples.advanced(by: read).initialize(repeating: 0, count: frameCountInt - read)
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

        startProducer()
    }

    private func rebuildEngine() {
        engine.stop()
        if let node = sourceNode {
            engine.detach(node)
        }
        sourceNode = nil
        format = nil
        ring.clear()
        stopProducer()
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

    func enqueueSamples() {
        let count = samplesPerFrame
        if count <= 0 {
            return
        }
        let targetFill = max(count * 2, Int(sampleRate * 0.25))
        var attempts = 0
        while ring.availableToRead < targetFill && ring.availableToWrite >= count && attempts < 4 {
            var temp = Array(repeating: Float(0), count: count)
            temp.withUnsafeMutableBufferPointer { buffer in
                guard let base = buffer.baseAddress else { return }
                nes_apu_fill_buffer(nes, sampleRate, base, Int32(count))
            }
            _ = ring.write(temp, count: count)
            attempts += 1
        }
    }

    private func startProducer() {
        if audioTimer != nil {
            return
        }
        let timer = DispatchSource.makeTimerSource(queue: audioQueue)
        timer.schedule(deadline: .now(), repeating: 1.0 / 240.0)
        timer.setEventHandler { [weak self] in
            self?.enqueueSamples()
        }
        audioTimer = timer
        timer.resume()
    }

    private func stopProducer() {
        audioTimer?.cancel()
        audioTimer = nil
    }

}

private final class AudioRingBuffer {
    private var buffer: [Float]
    private var readIndex = 0
    private var writeIndex = 0
    private var count = 0
    private let lock = NSLock()

    init(capacity: Int) {
        buffer = Array(repeating: 0, count: max(1, capacity))
    }

    func clear() {
        lock.lock()
        readIndex = 0
        writeIndex = 0
        count = 0
        lock.unlock()
    }

    var availableToRead: Int {
        lock.lock()
        let value = count
        lock.unlock()
        return value
    }

    var availableToWrite: Int {
        lock.lock()
        let value = buffer.count - count
        lock.unlock()
        return value
    }

    func write(_ samples: [Float], count writeCount: Int) -> Int {
        if writeCount <= 0 {
            return 0
        }
        lock.lock()
        let capacity = buffer.count
        let space = capacity - count
        let toWrite = min(writeCount, space)
        if toWrite > 0 {
            let first = min(toWrite, capacity - writeIndex)
            for i in 0..<first {
                buffer[writeIndex + i] = samples[i]
            }
            if toWrite > first {
                for i in 0..<(toWrite - first) {
                    buffer[i] = samples[first + i]
                }
            }
            writeIndex = (writeIndex + toWrite) % capacity
            count += toWrite
        }
        lock.unlock()
        return toWrite
    }

    func read(into out: UnsafeMutablePointer<Float>, count readCount: Int) -> Int {
        if readCount <= 0 {
            return 0
        }
        lock.lock()
        let capacity = buffer.count
        let toRead = min(readCount, count)
        if toRead > 0 {
            let first = min(toRead, capacity - readIndex)
            for i in 0..<first {
                out[i] = buffer[readIndex + i]
            }
            if toRead > first {
                for i in 0..<(toRead - first) {
                    out[first + i] = buffer[i]
                }
            }
            readIndex = (readIndex + toRead) % capacity
            count -= toRead
        }
        lock.unlock()
        return toRead
    }
}
