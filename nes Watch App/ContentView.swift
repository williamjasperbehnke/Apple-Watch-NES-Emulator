//
//  ContentView.swift
//  nes Watch App
//
//  Created by William Behnke on 2026-01-29.
//

import SwiftUI

struct ContentView: View {
    @StateObject private var viewModel = EmulatorViewModel()
    @State private var selectedIndex: Int = 0
    @State private var crownValue: Double = 0
    @State private var showingMenu: Bool = true

    var body: some View {
        ZStack {
            if showingMenu {
                CartridgeMenuView(
                    romNames: viewModel.romNames,
                    selectedIndex: $selectedIndex,
                    crownValue: $crownValue
                ) { romName in
                    viewModel.loadRom(named: romName, autoStart: true) { success in
                        if success {
                            showingMenu = false
                        }
                    }
                }
            } else {
                emulatorView
                    .overlay(alignment: .top) {
                        Button("Menu") {
                            viewModel.stop()
                            showingMenu = true
                        }
                        .buttonStyle(.plain)
                        .font(.caption2.weight(.semibold))
                        .padding(.horizontal, 4)
                        .padding(.vertical, 2)
                        .background(Color.gray.opacity(0.7))
                        .clipShape(RoundedRectangle(cornerRadius: 6))
                        .padding(.top, 0)
                    }
            }
        }
        .ignoresSafeArea()
        .onAppear {
            if viewModel.romNames.isEmpty {
                viewModel.loadDefaultRom()
                viewModel.start()
                showingMenu = false
            } else {
                selectedIndex = min(selectedIndex, viewModel.romNames.count - 1)
                crownValue = Double(selectedIndex)
            }
        }
    }

    private var emulatorView: some View {
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

#Preview {
    ContentView()
}
