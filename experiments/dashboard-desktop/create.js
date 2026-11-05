// Dashboard desktop experiment: three widgets on the bottom screen's desktop
// (screen 0 = eDP-2), splitting the free top area evenly.
// Run through run.sh, which substitutes the @PLACEHOLDERS@.

var AREA_HEIGHT = @HEIGHT@;
var YOUTUBE_URL = "@YOUTUBE_URL@";
var NEWS_URL = "@NEWS_URL@";

var desktop = desktops().filter(function (d) { return d.screen === 0; })[0];
if (!desktop)
    throw "no desktop containment on screen 0";

// Remove widgets left over from an earlier run.
desktop.widgets().forEach(function (w) {
    w.currentConfigGroup = ["General"];
    if (w.readConfig("plasmaTouchpadDashboard", "") === "true")
        w.remove();
});

var g = screenGeometry(0);
var columnWidth = Math.floor(g.width / 3);

function addTagged(type, column) {
    // addWidget treats a position of exactly (0,0) as "none given" and
    // centres the widget, so nudge the first column off the origin.
    var x = Math.max(column * columnWidth, 1);
    var w = desktop.addWidget(type, x, 0, columnWidth, AREA_HEIGHT);
    w.currentConfigGroup = ["General"];
    w.writeConfig("plasmaTouchpadDashboard", "true");
    return w;
}

function addBrowser(url, column) {
    var w = addTagged("org.kde.plasma.webbrowser", column);
    // The widget has already loaded and saved its built-in default as "url"
    // by now, so set the URL it opens on every start instead, and reload.
    w.writeConfig("useDefaultUrl", true);
    w.writeConfig("defaultUrl", url);
    w.writeConfig("url", url);
    w.reloadConfig();
    return w;
}

addBrowser(YOUTUBE_URL, 0);
addTagged("org.kde.plasma.weather", 1);
addBrowser(NEWS_URL, 2);

print("added 3 widgets to desktop " + desktop.id + " on screen 0 (" +
      g.width + "x" + g.height + "), columns of " + columnWidth + "x" + AREA_HEIGHT + "\n");
