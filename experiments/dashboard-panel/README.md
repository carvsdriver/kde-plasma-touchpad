# Dashboard panel experiment

Can a tall Plasma panel along the top of the bottom screen (eDP-2) host a
dashboard of three widgets, split evenly, in the area left free when the
keyboard is docked on the bottom half (1800×452 logical)?

```bash
./run.sh create [height]   # default 452
./run.sh measure
./run.sh remove
```

Widgets: Web Browser (`org.kde.plasma.webbrowser`) at m.youtube.com, Weather
Report (`org.kde.plasma.weather`), and Web Browser at text.npr.org as a
stand-in news reader (KDE ships no RSS widget).

## Results (2026-11-04, Plasma 6.7.4)

- **Height works.** Plasma accepted and kept a 452 px panel on the inner top
  edge of eDP-2.
- **The widgets show as icons, not content.** In a panel each applet uses its
  compact representation, sized square to the panel thickness (444×444). The
  browser showed a globe icon and the weather widget a weather icon; their
  content opens only as a popup on tap. `lengthMode = "fill"` stretches the
  panel, not the applets, so there is no even three-way split.
- **The daemon's PanelMover grabbed it.** When the touchpad activated it moved
  every panel on screen 0 to screen 1, including this one. Any dashboard
  panel would need excluding (it is tagged `plasmaTouchpadDashboard=true` in
  its `[General]` config).

Conclusion: a panel cannot show these widgets' full content or split the width
evenly, so approach A1 is out. The next candidate is the bottom screen's
desktop containment, where widgets render their full view at a size and
position the script can set.
