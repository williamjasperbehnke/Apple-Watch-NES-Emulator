final class Controller {
    enum Button: UInt8 {
        case a = 0x01
        case b = 0x02
        case select = 0x04
        case start = 0x08
        case up = 0x10
        case down = 0x20
        case left = 0x40
        case right = 0x80
    }

    private var state: UInt8 = 0
    private var shift: UInt8 = 0
    private var strobe: Bool = false

    func setButton(_ button: Button, pressed: Bool) {
        if pressed {
            state |= button.rawValue
        } else {
            state &= ~button.rawValue
        }
    }

    func setState(_ buttons: UInt8) {
        state = buttons
    }

    func write(_ data: UInt8) {
        strobe = (data & 0x01) != 0
        if strobe {
            shift = state
        }
    }

    func read() -> UInt8 {
        if strobe {
            return (state & 0x01)
        }
        let value = shift & 0x01
        shift >>= 1
        return value
    }
}
