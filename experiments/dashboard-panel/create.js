// Dashboard panel experiment: a tall Plasma panel along the top edge of the
// bottom screen (screen 0 = eDP-2), holding three widgets side by side.
// Run through run.sh, which substitutes the @PLACEHOLDERS@.

var HEIGHT = @HEIGHT@;
var YOUTUBE_URL = "@YOUTUBE_URL@";
var NEWS_URL = "@NEWS_URL@";

// Remove any panel left over from an earlier run.
panels().forEach(function (p) {
    p.currentConfigGroup = ["General"];
    if (p.readConfig("plasmaTouchpadDashboard", "") === "true")
        p.remove();
});

var panel = new Panel("org.kde.panel");
panel.screen = 0;
panel.location = "top";
panel.height = HEIGHT;
panel.lengthMode = "fill";
panel.hiding = "none";
panel.floating = false;
panel.currentConfigGroup = ["General"];
panel.writeConfig("plasmaTouchpadDashboard", "true");

function addBrowser(url) {
    var w = panel.addWidget("org.kde.plasma.webbrowser");
    w.currentConfigGroup = ["General"];
    w.writeConfig("useDefaultUrl", false);
    w.writeConfig("url", url);
    return w;
}

addBrowser(YOUTUBE_URL);
panel.addWidget("org.kde.plasma.weather");
addBrowser(NEWS_URL);

print("created panel " + panel.id + " screen=" + panel.screen +
      " location=" + panel.location + " height=" + panel.height);
