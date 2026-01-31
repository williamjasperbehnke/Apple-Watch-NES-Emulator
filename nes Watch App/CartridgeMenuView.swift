import SwiftUI

struct CartridgeMenuView: View {
    let romNames: [String]
    @Binding var selectedIndex: Int
    @Binding var crownValue: Double
    let onSelect: (String) -> Void
    @FocusState private var crownFocused: Bool

    var body: some View {
        ZStack {
            Color.black.ignoresSafeArea()

            VStack(spacing: 10) {
                HStack {
                    Text("Cartridges \(selectedIndex + 1)/\(max(romNames.count, 1))")
                        .font(.caption2.weight(.semibold))
                        .foregroundColor(.white.opacity(0.8))
                    Spacer()
                    Spacer()
                }

                if romNames.isEmpty {
                    Text("No ROMs found")
                        .font(.caption2)
                        .foregroundColor(.white.opacity(0.7))
                } else {
                    ScrollViewReader { proxy in
                        ScrollView(.vertical, showsIndicators: false) {
                            VStack(spacing: 6) {
                                ForEach(romNames.indices, id: \.self) { index in
                                    HStack(spacing: 8) {
                                        RoundedRectangle(cornerRadius: 2)
                                            .fill(index == selectedIndex ? Color.white : Color.white.opacity(0.25))
                                            .frame(width: 4)

                                        Text(romNames[index])
                                            .font(.caption2.weight(index == selectedIndex ? .semibold : .regular))
                                            .foregroundColor(index == selectedIndex ? .white : .white.opacity(0.7))
                                            .lineLimit(1)
                                            .frame(maxWidth: .infinity, alignment: .leading)
                                    }
                                    .padding(.horizontal, 8)
                                    .padding(.vertical, 6)
                                    .background(index == selectedIndex ? Color.white.opacity(0.12) : Color.white.opacity(0.03))
                                    .clipShape(RoundedRectangle(cornerRadius: 8))
                                    .id(index)
                                    .onTapGesture {
                                        selectedIndex = index
                                        crownValue = Double(index)
                                        onSelect(romNames[index])
                                    }
                                }
                            }
                            .padding(.vertical, 4)
                        }
                        .frame(maxHeight: 180)
                        .focusable(true)
                        .focused($crownFocused)
                        .digitalCrownRotation(
                            $crownValue,
                            from: 0,
                            through: max(0, Double(romNames.count - 1)),
                            by: 1,
                            sensitivity: .medium,
                            isContinuous: false,
                            isHapticFeedbackEnabled: true
                        )
                        .onChange(of: crownValue) { newValue in
                            let index = Int(newValue.rounded())
                            if index != selectedIndex {
                                selectedIndex = min(max(0, index), max(0, romNames.count - 1))
                            }
                        }
                        .onChange(of: selectedIndex) { value in
                            withAnimation(.easeOut(duration: 0.15)) {
                                proxy.scrollTo(value, anchor: .center)
                            }
                        }
                        .onAppear {
                            crownValue = Double(selectedIndex)
                            crownFocused = true
                            proxy.scrollTo(selectedIndex, anchor: .center)
                        }
                    }

                }
            }
            .padding(10)
        }
    }

    private var displayedRomIndices: [Int] {
        guard !romNames.isEmpty else { return [] }
        let start = max(0, selectedIndex - 2)
        let end = min(romNames.count - 1, selectedIndex + 2)
        return Array(start...end)
    }
}

#Preview {
    CartridgeMenuView(romNames: ["Tetris", "DigDug", "AccuracyCoin"], selectedIndex: .constant(0), crownValue: .constant(0)) { _ in }
}
