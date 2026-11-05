// Parse an RSS 2.0, RSS 1.0 (RDF) or Atom document, as returned in
// XMLHttpRequest.responseXML, into [{ title, link, time, source }].
// time is milliseconds since the epoch, or 0 when the feed gives no
// readable date.
.pragma library

function localName(node) {
    var name = node.nodeName || "";
    var colon = name.indexOf(":");
    return colon >= 0 ? name.substring(colon + 1) : name;
}

function children(node, name) {
    var out = [];
    var kids = node ? node.childNodes : [];
    for (var i = 0; i < kids.length; i++) {
        if (kids[i].nodeType === 1 && (!name || localName(kids[i]) === name))
            out.push(kids[i]);
    }
    return out;
}

function child(node, name) {
    return children(node, name)[0] || null;
}

function text(node) {
    if (!node)
        return "";
    var out = "";
    var kids = node.childNodes;
    for (var i = 0; i < kids.length; i++) {
        // 3 = text, 4 = CDATA section
        if (kids[i].nodeType === 3 || kids[i].nodeType === 4)
            out += kids[i].nodeValue;
    }
    return out.replace(/\s+/g, " ").trim();
}

function attribute(node, name) {
    var attrs = node ? node.attributes : [];
    for (var i = 0; i < attrs.length; i++) {
        if (attrs[i].name === name)
            return attrs[i].value;
    }
    return "";
}

function parseTime(value) {
    var t = Date.parse(value);
    return isNaN(t) ? 0 : t;
}

function atomLink(entry) {
    var links = children(entry, "link");
    for (var i = 0; i < links.length; i++) {
        var rel = attribute(links[i], "rel");
        if (!rel || rel === "alternate")
            return attribute(links[i], "href");
    }
    return links.length ? attribute(links[0], "href") : "";
}

function parse(doc) {
    var root = doc ? doc.documentElement : null;
    if (!root)
        return [];
    var items = [];
    var kind = localName(root);

    if (kind === "feed") {
        var source = text(child(root, "title"));
        children(root, "entry").forEach(function (e) {
            items.push({
                title: text(child(e, "title")),
                link: atomLink(e),
                time: parseTime(text(child(e, "published")) || text(child(e, "updated"))),
                source: source,
                author: text(child(child(e, "author"), "name"))
            });
        });
    } else if (kind === "rss" || kind === "RDF") {
        var channel = child(root, "channel");
        var source = text(child(channel, "title"));
        // RSS 2.0 puts items in <channel>, RSS 1.0 beside it.
        var entries = children(channel, "item").concat(children(root, "item"));
        entries.forEach(function (e) {
            items.push({
                title: text(child(e, "title")),
                link: text(child(e, "link")) || text(child(e, "guid")),
                time: parseTime(text(child(e, "pubDate")) || text(child(e, "date"))),
                source: source,
                author: text(child(e, "creator")) || text(child(e, "author"))
            });
        });
    }
    return items.filter(function (i) { return i.title && /^https?:\/\//.test(i.link); });
}

// "5 min", "3 h", "2 d" since `time`, or "" if unknown.
function age(time, now) {
    if (!time)
        return "";
    var minutes = Math.max(0, Math.round((now - time) / 60000));
    if (minutes < 60)
        return minutes + " min";
    if (minutes < 48 * 60)
        return Math.round(minutes / 60) + " h";
    return Math.round(minutes / 1440) + " d";
}
