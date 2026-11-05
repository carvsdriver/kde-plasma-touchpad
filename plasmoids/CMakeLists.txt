# Dashboard widgets: plain QML packages.  feed.js and Viewer.qml are shared:
# each package carries symlinks to them for running from the source tree, and
# gets real copies here.
foreach(widget org.kde.plasma.touchpad.news org.kde.plasma.touchpad.youtube)
    install(DIRECTORY ${widget}
            DESTINATION ${KDE_INSTALL_DATADIR}/plasma/plasmoids
            PATTERN feed.js EXCLUDE
            PATTERN Viewer.qml EXCLUDE)
    install(FILES common/feed.js common/Viewer.qml
            DESTINATION ${KDE_INSTALL_DATADIR}/plasma/plasmoids/${widget}/contents/ui)
endforeach()
