# OpenPnP's defaults

What OpenPnP starts with (GPL-3.0, from https://github.com/openpnp/openpnp), used as OpenPnP uses them:

- `config/`: OpenPnP's `src/main/resources/config` (packages, parts, vision settings and its simulated
  machine). A configuration file that is not there yet is read from here (and then saved), and jplacer
  started with no machine brings in this one, as OpenPnP does on its first start.
- `samples/pnp-test/pnp-test.png`: OpenPnP's `src/main/resources/samples/pnp-test/pnp-test.png`, the picture
  of the table its simulated machine's ImageCamera shows (`classpath://samples/...` in a machine.xml
  is found here); and OpenPnP's sample job (`samples/pnp-test/*.xml`: the job, its board and panel),
  copied to the configuration's samples folder on a first start.

They travel beside the executable (`usr/bin/openpnp-defaults` in the AppImage) and are found by
`JPlacerPaths::bundled`.
