//
//  ContentView.swift
//  nes Watch App
//
//  Created by William Behnke on 2026-01-29.
//

import SwiftUI

struct ContentView: View {
    @StateObject private var viewModel = EmulatorViewModel()

    var body: some View {
        GeometryReader { proxy in
            ZStack {
                if let frameImage = viewModel.frameImage {
                    Image(decorative: frameImage, scale: 1, orientation: .up)
                        .resizable()
                        .scaledToFit()
                        .frame(maxWidth: proxy.size.width, maxHeight: proxy.size.height)
                } else {
                    RoundedRectangle(cornerRadius: 8)
                        .fill(Color.black.opacity(0.7))
                        .overlay(Text("No Frame").font(.caption2))
                        .frame(maxWidth: proxy.size.width, maxHeight: proxy.size.height)
                }

                VStack {
                    HStack {
                        Spacer()
                        Button("Play") { viewModel.start() }
                            .buttonStyle(CompactIconButtonStyle(systemName: "play.fill"))
                        Button("Pause") { viewModel.stop() }
                            .buttonStyle(CompactIconButtonStyle(systemName: "pause.fill"))
                        Spacer()
                    }
                    .padding(.top, -4)

                    Spacer()

                    HStack {
                        VStack(spacing: 4) {
                            PressableButton(label: "▲") { pressed in
                                viewModel.setButton(.up, pressed: pressed)
                            }
                            HStack(spacing: 4) {
                                PressableButton(label: "◀") { pressed in
                                    viewModel.setButton(.left, pressed: pressed)
                                }
                                PressableButton(label: "▶") { pressed in
                                    viewModel.setButton(.right, pressed: pressed)
                                }
                            }
                            PressableButton(label: "▼") { pressed in
                                viewModel.setButton(.down, pressed: pressed)
                            }
                        }

                        Spacer()

                        VStack(spacing: 4) {
                            PressableButton(label: "A", style: .primary) { pressed in
                                viewModel.setButton(.a, pressed: pressed)
                            }
                            PressableButton(label: "B", style: .primary) { pressed in
                                viewModel.setButton(.b, pressed: pressed)
                            }
                        }
                    }

                    HStack(spacing: 6) {
                        PressableButton(label: "Select", style: .secondary) { pressed in
                            viewModel.setButton(.select, pressed: pressed)
                        }
                        PressableButton(label: "Start", style: .secondary) { pressed in
                            viewModel.setButton(.start, pressed: pressed)
                        }
                    }
                }
                .padding(6)
            }
        }
        .ignoresSafeArea()
        .onAppear {
            viewModel.loadDefaultRom()
        }
    }
}

private enum ButtonStyleVariant {
    case primary
    case secondary
}

private struct PressableButton: View {
    let label: String
    var style: ButtonStyleVariant = .secondary
    let onPressChanged: (Bool) -> Void

    var body: some View {
        Text(label)
            .font(.caption2.weight(.semibold))
            .frame(minWidth: label.count > 1 ? 42 : 24, minHeight: 20)
            .background(backgroundColor)
            .foregroundColor(.white)
            .clipShape(RoundedRectangle(cornerRadius: 6))
            .gesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { _ in onPressChanged(true) }
                    .onEnded { _ in onPressChanged(false) }
            )
    }

    private var backgroundColor: Color {
        switch style {
        case .primary:
            return Color.blue
        case .secondary:
            return Color.gray.opacity(0.7)
        }
    }
}

private struct CompactIconButtonStyle: ButtonStyle {
    let systemName: String

    func makeBody(configuration: Configuration) -> some View {
        Image(systemName: systemName)
            .font(.caption2.weight(.semibold))
            .padding(.horizontal, 6)
            .padding(.vertical, 2)
            .background(Color.black.opacity(configuration.isPressed ? 0.8 : 0.6))
            .foregroundColor(.white)
            .clipShape(RoundedRectangle(cornerRadius: 6))
    }
}

#Preview {
    ContentView()
}
