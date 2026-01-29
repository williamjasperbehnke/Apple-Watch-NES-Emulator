import CoreGraphics
import Foundation

final class FrameBuffer {
    static let width = 256
    static let height = 240

    var pixels: [UInt32] = Array(repeating: 0xFF000000, count: width * height)

    func clear(color: UInt32 = 0xFF000000) {
        pixels = Array(repeating: color, count: FrameBuffer.width * FrameBuffer.height)
    }

    func makeImage() -> CGImage? {
        let data = Data(bytes: pixels, count: pixels.count * MemoryLayout<UInt32>.size)
        guard let provider = CGDataProvider(data: data as CFData) else { return nil }
        let colorSpace = CGColorSpaceCreateDeviceRGB()
        let bitmapInfo = CGBitmapInfo.byteOrder32Little.union(
            CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedFirst.rawValue)
        )

        return CGImage(
            width: FrameBuffer.width,
            height: FrameBuffer.height,
            bitsPerComponent: 8,
            bitsPerPixel: 32,
            bytesPerRow: FrameBuffer.width * 4,
            space: colorSpace,
            bitmapInfo: bitmapInfo,
            provider: provider,
            decode: nil,
            shouldInterpolate: false,
            intent: .defaultIntent
        )
    }
}
