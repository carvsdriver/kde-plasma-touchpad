// Remove the dashboard widgets created by create.js from every desktop.

desktops().forEach(function (d) {
    d.widgets().forEach(function (w) {
        w.currentConfigGroup = ["General"];
        if (w.readConfig("plasmaTouchpadDashboard", "") === "true") {
            print("removing " + w.type + " " + w.id + " from desktop " + d.id + "\n");
            w.remove();
        }
    });
});
