// swift-tools-version: 5.9
import PackageDescription

let package = Package(
    name: "CopilotMenuBar",
    platforms: [.macOS(.v13)],
    targets: [
        .executableTarget(name: "CopilotMenuBar", resources: [.copy("Resources")]),
        .testTarget(name: "CopilotMenuBarTests", dependencies: ["CopilotMenuBar"]),
    ]
)
