// Remove the dashboard panel created by create.js.

panels().forEach(function (p) {
    p.currentConfigGroup = ["General"];
    if (p.readConfig("plasmaTouchpadDashboard", "") === "true") {
        print("removing panel " + p.id + "\n");
        p.remove();
    }
});
