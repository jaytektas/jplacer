# Log

The **Log** tab shows what jplacer's log says as it says it, as OpenPnP's Log tab does: one line an
entry, its time, where it comes from, its level and what it says ("2026-10-05 12:00:00.123 job INFO:
Job finished without error"), coloured by its level (trace green, information blue, warnings and errors
red, an error on a background of its own). While the list is at its end it follows the newest entry.

<!-- src: src/ui/JPLogPanel.cpp -->

**Global Logging Settings ▸ Global Log Level** sets how much the log says at all: it is the log's own
level, the same as the Console's **Log**, and kept for next time.

<!-- src: src/ui/JPLogPanel.cpp (onLogLevels); src/app/JPlacerOpenPnpTabs.cpp -->

**Filter Logging Panel** chooses what is shown of it, not what is logged:

| | |
|---|---|
| Search | Only entries with this text in them, whatever its case. |
| Log Level | Only entries of this level and above. |
| System Output | Entries from the framework and libraries jplacer uses, as well as jplacer's own. |
| Clear log (✕) | The entries taken away. |
| Copy to clipboard | The entries shown, copied as text. |
| Scroll down | The newest entry brought into view. |

Ctrl+C copies the entries chosen in the list.

<!-- src: src/ui/JPLogPanel.cpp (filter, filteredText); src/ui/JPTable.cpp (copySelection) -->
