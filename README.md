# Antistatic for Pebble

The antistatic.exchange homepage charts on your wrist, drawn like a hi-fi
tuner: segment digits, a red needle, and a status column showing which line
you're reading.

![Pebble 2 Duo](docs/flint.png)
![Pebble Time 2](docs/emery.png)
![Pebble Time Round](docs/chalk.png)

Works on every Pebble: aplite, basalt, chalk, diorite, emery, flint (Pebble 2
Duo) and gabbro (Pebble Round 2). Black-and-white watches get dithered bands
in place of colour. Aplite holds 8 markets; the others hold 24.

## Controls

| Screen | Up / Down | Select | Hold Select |
| --- | --- | --- | --- |
| Markets: one homepage card per page | Previous / next market | Tune in | Refresh |
| Tuner: the card with a needle | Move the needle along the dates or thresholds (hold to sweep) | Point chart for the first curve | Crowd ↔ house |
| Point chart: one curve, bar by bar | Previous / next bar | Next curve (event or year) | Crowd ↔ house |

Back steps out. A shake (wrist flick) refreshes from anywhere.

- **Tuner, count markets**: the homepage's ranges (90% dim, middle half
  bright, median tick) with the needle running down through them. The
  readout is the threshold (≥ 2.5%) and each row reads P(value ≥ threshold).
- **Tuner, date markets**: the readout is the date (By 2027 Dec), each row
  is the probability by then, and the needle runs along a dial with a tick
  per bar and a long tick each January.
- **Point chart**: every bar of the curve, the shown line as the bar and the
  other line as a tick across it; the big digits are the selected bar's
  probability, with the other line under it.
- **Status column**: Crowd or House lit for the line shown; Settled (or Yes)
  lights red when the bar under the needle has resolved. Settled curves read
  Yes or No in the rows.
- A short buzz when the needle takes the first row across 50%.
- The launcher glance shows the first market's headline number.

## How it works

The phone side (`src/pkjs/index.js`) reads the public API, no token needed:
`/markets`, then `/markets/:code/forecast?limit=0` (crowd and house
probability per bar) and, for date markets, `/markets/:code/submarkets` (the
bar dates). It works out what the homepage shows (count percentiles
interpolated as `HomeCountBandBuilder` does; date bars at the end of next
year) and sends each market as one small binary message. A market's full
curves go over when you tune into it. The last fetch is cached in the
phone's local storage, so the list appears straight away and then refreshes.

On the watch (`src/c/`): `main.c` receives messages and holds the model,
`draw.c` has the segment readout, bars and formatting, and each screen has
its own file.

## Build and install

Needs the Pebble SDK (`uv tool install pebble-tool`, then
`pebble sdk install latest`).

```bash
pebble build
```

```bash
pebble install --emulator flint
```

On your watch: turn on the developer connection in the Pebble app and run
`pebble install --phone <phone IP>`, or open `build/antistatic-pebble.pbw` on
the phone.

`scripts/tour.sh <platform>` clicks through every screen on a running
emulator and saves screenshots to `shots/`; `scripts/sheet.py` tiles them.
