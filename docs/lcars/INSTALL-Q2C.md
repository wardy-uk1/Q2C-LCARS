# Installing Q2C-LCARS on a QIDI Q2C

Q2C-LCARS is HelixScreen with an LCARS skin. It installs the same way HelixScreen does, and
replaces QIDI's own screen program.

## Read this first

- **QIDI's apps:** while it runs, QIDI's screen program is off. QIDI Studio box sync, the
  QIDI cloud app and Box filament edits from QIDI's apps stop working. You can still print
  from Fluidd, Mainsail or any slicer that talks to Moonraker. Uninstalling brings them back.
- **QIDI firmware updates** put the stock screen back. Reinstall afterwards.
- **In-app updates are off.** A stock HelixScreen update would replace the LCARS build, so
  Settings › Updates just shows "From Q2C-LCARS on GitHub". Install new LCARS builds as below.
  To turn in-app updates back on, add `HELIX_DISABLE_AUTO_UPDATES=0` to
  `~/helixscreen/config/helixscreen.env` and restart.

## 1. Get a build

Builds are made by GitHub Actions on this repository.

1. On GitHub, open the repo's **Actions** tab. The first time, click **I understand my
   workflows, go ahead and enable them**.
2. Pick **LCARS build (QIDI Q2C / Pi aarch64)** and click **Run workflow** on the
   `lcars-skin` branch. Pushes to that branch also start a build. A build takes about 1½–2 hours.
3. When it finishes, the file `helixscreen-pi.zip` appears under the repo's **Releases**,
   named `lcars-<date>-<commit>`.

## 2. Install it on the printer

From your PC, copy the package to the printer. This works over Tailscale or on the home
network.

```bash
scp helixscreen-pi.zip mks@qidi-q2.tail985254.ts.net:~
```

Then on the printer:

```bash
ssh mks@qidi-q2.tail985254.ts.net
unzip -o helixscreen-pi.zip -d q2c-lcars
./q2c-lcars/install.sh
```

The installer shows its plan and asks `Continue? [Y/n]` before changing anything. It asks for
the `mks` password when it needs `sudo`. It installs the 64-bit build, stops QIDI's screen
program and keeps it off at boot.

## 3. First start

The setup wizard appears on the screen. The connection is `127.0.0.1`, port `7125`. Then
turn the skin on: **Settings › Appearance › LCARS System**, and choose **24th Century**
or **25th Century**.

## Removing it

```bash
curl -sSL https://releases.helixscreen.org/install.sh | sh -s -- --uninstall
```

This removes HelixScreen and brings QIDI's screen program back.

## If the screen stays on QIDI's interface

Wait a few minutes; a first start can be slow. Then collect the log:

```bash
journalctl -u helixscreen -n 100
```

## Sound

See [the sound board guide](../../pico/README.md) for the Pico speaker add-on.
