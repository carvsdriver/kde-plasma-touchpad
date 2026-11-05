// Report the dashboard panel's actual size and each widget's geometry, to see
// whether Plasma honoured the requested height and how it split the width.

panels().forEach(function (p) {
    p.currentConfigGroup = ["General"];
    if (p.readConfig("plasmaTouchpadDashboard", "") !== "true")
        return;
    print("panel " + p.id + " screen=" + p.screen + " location=" + p.location +
          " height=" + p.height + " length=" + p.length +
          " lengthMode=" + p.lengthMode + "\n");
    p.widgets().forEach(function (w) {
        var g = w.geometry;
        print("  " + w.type + " x=" + g.x + " y=" + g.y +
              " " + g.width + "x" + g.height + "\n");
    });
});
