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
    @State private var joystickDirection: JoystickDirection? = nil

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
                        JoystickView { direction in
                            updateJoystick(direction)
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

    private func updateJoystick(_ direction: JoystickDirection?) {
        if joystickDirection == direction {
            return
        }
        if let previous = joystickDirection {
            setJoystick(previous, pressed: false)
        }
        joystickDirection = direction
        if let current = direction {
            setJoystick(current, pressed: true)
        }
    }

    private func setJoystick(_ direction: JoystickDirection, pressed: Bool) {
        switch direction {
        case .up:
            viewModel.setButton(.up, pressed: pressed)
        case .down:
            viewModel.setButton(.down, pressed: pressed)
        case .left:
            viewModel.setButton(.left, pressed: pressed)
        case .right:
            viewModel.setButton(.right, pressed: pressed)
        }
    }
}

private enum JoystickDirection {
    case up
    case down
    case left
    case right
}

private enum ButtonStyleVariant {
    case primary
    case secondary
}

private struct JoystickView: View {
    var onDirectionChanged: (JoystickDirection?) -> Void
    @State private var thumbOffset: CGSize = .zero

    private let size: CGFloat = 54
    private let thumbSize: CGFloat = 22
    private let deadZone: CGFloat = 8
    private let maxOffset: CGFloat = 16

    var body: some View {
        ZStack {
            Circle()
                .fill(Color.gray.opacity(0.5))
            Circle()
                .stroke(Color.white.opacity(0.4), lineWidth: 1)

            Circle()
                .fill(Color.gray.opacity(0.85))
                .frame(width: thumbSize, height: thumbSize)
                .offset(thumbOffset)
        }
        .frame(width: size, height: size)
        .gesture(
            DragGesture(minimumDistance: 0)
                .onChanged { value in
                    let center = CGPoint(x: size / 2, y: size / 2)
                    let dx = value.location.x - center.x
                    let dy = value.location.y - center.y
                    let distance = sqrt(dx * dx + dy * dy)
                    if distance < deadZone {
                        thumbOffset = .zero
                        onDirectionChanged(nil)
                        return
                    }

                    let clampedDistance = min(distance, maxOffset)
                    let scale = clampedDistance / max(distance, 0.001)
                    thumbOffset = CGSize(width: dx * scale, height: dy * scale)

                    if abs(dx) > abs(dy) {
                        onDirectionChanged(dx > 0 ? .right : .left)
                    } else {
                        onDirectionChanged(dy > 0 ? .down : .up)
                    }
                }
                .onEnded { _ in
                    thumbOffset = .zero
                    onDirectionChanged(nil)
                }
        )
    }
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
