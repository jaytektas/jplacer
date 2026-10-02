# Changes

What changed in each version, in plain words for the people using jplacer: no commit hashes, no file
names. The manual's What's New page (Help > What's New) is generated from this file, and each GitHub
release carries its version's section as its notes.

Every change a user would notice adds a line under Unreleased, in the same commit as the change.
`packaging/build-release.sh` turns Unreleased into the new version's section; a beta carries it as its
notes.

## Unreleased

- jplacer can now talk to a machine. A machine is described by a cell file, and
  Machine > Import OpenPnP Machine… makes one from an OpenPnP machine.xml.
- Machine > Connect connects to the machine's controllers, recognises grblHAL, Grbl
  and other G-code firmware, and reads the settings the controller stores.
- A Machine panel shows the live position of every axis, switches and reads the
  actuators (lights, valves, vacuum sensors), and has a console for sending G-code.
- A red NOT CONNECTED strip across the top of the window while the machine is not connected, saying why
  when a connection failed. Connecting to a port where nothing answers now fails instead of pretending.
- The Machine panel lists the serial devices plugged in, so you can pick the controller's port; it is
  saved by the device's permanent name, which does not change when USB devices start in another order.
- Dialog buttons are always wide enough for their labels.
- The file dialog can show hidden folders, such as OpenPnP's .openpnp2: tick Show hidden, or press
  Ctrl+H.
- A new icon. Run as an AppImage, jplacer adds itself to your applications menu with it.
- Preferences has a General section: tear-off menus (off by default) and whether jplacer appears in
  the applications menu.
- A user manual: Help > User Manual opens it in your browser, and Help > What's New shows what changed
  in each version.

## 0.1.0

- The first release: the jplacer window and its menus, Preferences, and updates that install
  themselves from Help > Check for Updates and when jplacer opens.
