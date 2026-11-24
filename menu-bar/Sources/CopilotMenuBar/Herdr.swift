import Foundation

struct Herdr: Sendable {
    let executable: URL

    static func locate() -> Self? {
        let env = ProcessInfo.processInfo.environment
        let path = (env["PATH"] ?? "").split(separator: ":").map(String.init)
        let home = FileManager.default.homeDirectoryForCurrentUser.path
        let bundled = ["\(home)/Applications", "/Applications"].map { "\($0)/Herdr.app/Contents/MacOS/Herdr" }
        return (path.filter { $0.hasPrefix("/") }.map { "\($0)/herdr-app" } + bundled)
            .first(where: FileManager.default.isExecutableFile(atPath:))
            .map { Self(executable: URL(fileURLWithPath: $0)) }
    }

    func snapshot() -> Snapshot {
        guard let data = run(["ls"]), let world = try? JSONDecoder().decode(HerdrSnapshot.self, from: data) else {
            return .offline
        }
        return world.snapshot
    }

    func focus(_ id: String) -> Bool {
        // Recheck the target before asking Herdr to focus it. No terminal input.
        guard snapshot().sessions.contains(where: { $0.id == id }),
              let args = try? JSONEncoder().encode(["terminal": id]) else { return false }
        return run(["do", "agent.focus", String(decoding: args, as: UTF8.self)]) != nil
    }

    // Called off the UI thread. The Herdr client bounds its local socket read.
    private func run(_ arguments: [String]) -> Data? {
        let process = Process()
        let output = Pipe()
        process.executableURL = executable
        process.arguments = arguments
        process.standardOutput = output
        process.standardError = FileHandle.nullDevice
        do { try process.run() } catch { return nil }
        let data = output.fileHandleForReading.readDataToEndOfFile()
        process.waitUntilExit()
        return process.terminationStatus == 0 ? data : nil
    }
}
