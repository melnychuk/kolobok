// swift-tools-version: 5.9
import PackageDescription

// Kolobok: a menu bar miniature of the Stick's avatar,
// showing whatever agents Herdr runs, as the Stick's bridge reports them.
let package = Package(
    name: "Kolobok",
    platforms: [.macOS("14.4")],  // Native two-line menu items (NSMenuItem.subtitle).
    targets: [
        .executableTarget(name: "Kolobok"),
        .testTarget(name: "KolobokTests", dependencies: ["Kolobok"])
    ]
)
