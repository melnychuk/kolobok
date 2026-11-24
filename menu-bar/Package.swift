// swift-tools-version: 5.9
import PackageDescription

// BAA: Bar for Agent Activity. A menu bar miniature of the Stick's avatar,
// showing whatever agents Herdr runs, as the Stick's bridge reports them.
let package = Package(
    name: "BAA",
    platforms: [.macOS("14.4")],  // Native two-line menu items (NSMenuItem.subtitle).
    targets: [
        .executableTarget(name: "BAA"),
        .testTarget(name: "BAATests", dependencies: ["BAA"])
    ]
)
