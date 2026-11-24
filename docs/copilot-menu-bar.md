# Copilot in the macOS menu bar

This standalone, opt-in companion uses the Copilot mascot. It needs macOS 13+
with Swift 5.9+ to build, but no Stick, firmware, sidebar mod, or login item.

## Preview the states

From the checkout:

```sh
swift run --package-path menu-bar CopilotMenuBar --demo
```

Click the Copilot face and choose **Preview state**. The item and popover update
immediately; demo mode never calls Herdr or starts any agents.

| State | Menu-bar cue |
| --- | --- |
| No sessions / Idle | Mascot alone; the popover distinguishes the two |
| Working | White count on a dark menu bar, dark count on a light menu bar |
| Needs attention / Failed | Stick-yellow `!` instead of a working count; the popover labels the individual status |
| Done | One small Stick-green dot at the top-right of the mascot, regardless of how many tasks completed |
| Unknown / Offline | Question mark / dash |
| Working + done | Working count and green dot |
| Mixed | Yellow `!` and green dot; the popover still shows the working tasks |

The green dot has a thin dark outline to separate it from the mascot. The `!`
alternates between regular and bold without changing width; it stays bold if
macOS **Reduce motion** is on.

Use **Quit** in the popover or Ctrl-C in the launching terminal. Nothing is
installed or started at login.

## Watch live Copilot CLI sessions

With Herdr Desktop open, run:

```sh
swift run --package-path menu-bar CopilotMenuBar
```

The companion reads `herdr-app ls` about every two seconds, filters to Copilot
terminals in the open Desktop layout, and uses Herdr's own unread completion
state. It uses `herdr-app` from `PATH` or a Herdr.app in `~/Applications` or
`/Applications`. Click a session to focus it in Herdr. The companion doesn't
read transcripts, send terminal input, or approve prompts. Copilot running
outside Herdr Desktop is not discovered. Herdr may report failures as needing
attention rather than distinguishing them.

## Icon credit

The unmodified [Copilot icon](https://github.com/microsoft/vscode-codicons/blob/8d1826a9bce0a52283ecaeb63499c149e97a4f14/src/icons/copilot.svg)
is from Microsoft's VS Code Codicons project, licensed under
[CC BY 4.0](../menu-bar/Sources/CopilotMenuBar/Resources/Codicons-LICENSE.txt).
The app scales and tints it for display.
