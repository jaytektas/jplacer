# Log

The **Log** tab shows what jplacer's log says as it says it, as OpenPnP's Log tab does: one line an
entry, its time, where it comes from, its level and what it says ("2026-10-05 12:00:00.123 job INFO:
Job finished without error"), coloured by its level in the theme's colours, readable on its background
(information and debug in the text's own colour, trace dimmed, warnings in the warning colour, errors in
the danger colour; OpenPnP's blue and its errors' pale band are for its white background). While the list is at its end it follows the newest entry.

The log is also written to a file as it comes, as OpenPnP's `log/OpenPnP.log`: `log/jplacer.log` in jplacer's
folder (`~/.config/jplacer`), each entry a line as the tab shows it. Past 10 MB it is moved aside to
`jplacer.log.1` (the one before that dropped) and begun again. Help > Submit Diagnostics can include it.

<!-- src: src/ui/JPLogPanel.cpp; src/common/JPLogFile.cpp; src/common/JPLogLine.cpp -->

**Global Logging Settings ▸ Global Log Level** sets how much the log says at all: it is the log's own
level, the same as the Console's **Log**, and kept for next time.

<!-- src: src/ui/JPLogPanel.cpp (onLogLevels); src/app/JPlacerOpenPnpTabs.cpp -->

**Filter Logging Panel** chooses what is shown of it, not what is logged:

| | |
|---|---|
| Search | Only entries with this text in them, whatever its case; its **✕** empties it. |
| Log Level | Only entries of this level and above. |
| System Output | Entries from the framework and libraries jplacer uses, as well as jplacer's own. |
| Clear log (✕) | The entries taken away. |
| Copy to clipboard | The entries shown, copied as text. |
| Scroll down | The newest entry brought into view. |

Ctrl+C copies the entries chosen in the list.

<!-- src: src/ui/JPLogPanel.cpp (filter, filteredText); src/ui/JPTable.cpp (copySelection) -->
