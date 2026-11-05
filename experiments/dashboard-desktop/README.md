# Dashboard desktop experiment

Follow-up to `../dashboard-panel`: put the three dashboard widgets on the
bottom screen's desktop containment (screen 0 = eDP-2) instead of a panel,
splitting the free top area (1800×452 logical) into even columns.

```bash
./run.sh create [height]   # default 452
./run.sh measure
./run.sh remove
```

Widgets: Web Browser (`org.kde.plasma.webbrowser`) at m.youtube.com, Weather
Report (`org.kde.plasma.weather`), and Web Browser at text.npr.org as a
stand-in news reader (KDE ships no RSS widget).

## Results (2026-11-04, Plasma 6.7.4)

- **Widgets show their full content.** Unlike in a panel, both browsers
  render a live page and the weather widget its full view (it shows
  "Configure…" until a location is set).
- **Even split works.** Plasma placed them at 580, 580 and 548 px wide by
  420 tall (14 px margins, snapped to its grid) across the top of eDP-2.
- **`addWidget(type, 0, 0, w, h)` ignores the position.** A position of
  exactly (0,0) counts as "none given" and the widget is centred on the
  screen, so `create.js` nudges the first column to x=1.
- **Positions can't be changed afterwards from a script.** Assigning
  `widget.geometry` is silently ignored; remove and re-add instead.
- **Positions are stored per screen size and orientation**
  (`ItemGeometries-1800x1125` vs `-1125x1800` in
  `plasma-org.kde.plasma.desktop-appletsrc`), so a rotated bottom screen has
  its own, separate layout.
- **The browser's URL can't be set on a freshly added widget.** It loads
  before the script writes its config and saves its built-in default
  (kde.org) as `url`. Setting `useDefaultUrl=true` + `defaultUrl` makes the
  configured page its home page: used on the next Plasma start, or when its
  Home button is tapped.
- **The widgets stay on the desktop when the keyboard is off.** A real
  dashboard mode would add them on dock and remove them on undock, or
  park them in a separate activity or virtual desktop.

Conclusion: the desktop containment is a workable host (approach A2). Open
items for the framework: weather location, a real news reader, URL on first
load, and showing the widgets only in dashboard mode.
