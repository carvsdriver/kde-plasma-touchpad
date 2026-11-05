// Report the geometry Plasma gave each dashboard widget on screen 0's desktop.

desktops().forEach(function (d) {
    if (d.screen !== 0)
        return;
    d.widgets().forEach(function (w) {
        w.currentConfigGroup = ["General"];
        if (w.readConfig("plasmaTouchpadDashboard", "") !== "true")
            return;
        var g = w.geometry;
        print(w.type + " x=" + g.x + " y=" + g.y + " " + g.width + "x" + g.height + "\n");
    });
});
