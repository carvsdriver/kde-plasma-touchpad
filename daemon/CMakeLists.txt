qt_add_executable(plasma-touchpad-daemon
    main.cpp
    DockWatcher.cpp
    TouchInputHandler.cpp
    TouchpadOverlay.cpp
    WindowMover.cpp
    PanelMover.cpp
    PlasmaScript.cpp
    KWinScript.cpp
    DashboardManager.cpp
    YouTubeAccount.cpp
)

qt_policy(SET QTP0004 NEW)
qt_add_qml_module(plasma-touchpad-daemon
    URI "org.kde.plasma.touchpad"
    VERSION 1.0
    QML_FILES
        qml/Touchpad.qml
        qml/Blackout.qml
    RESOURCES
        qml/qmldir
    RESOURCE_PREFIX /
)

target_link_libraries(plasma-touchpad-daemon PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::DBus
    Qt6::Network
    Qt6::NetworkAuth
    KF6::ConfigCore
    KF6::Wallet
    KF6::WindowSystem
    LayerShellQt::Interface
    plasma-touchpad-common
)

target_include_directories(plasma-touchpad-daemon PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Google OAuth client (a "Desktop app" client) for the YouTube sign-in.  May
# be left empty and set in plasma-touchpadrc [YouTube] clientId/clientSecret.
set(PLASMA_TOUCHPAD_YOUTUBE_CLIENT_ID "" CACHE STRING "Google OAuth client ID for YouTube sign-in")
set(PLASMA_TOUCHPAD_YOUTUBE_CLIENT_SECRET "" CACHE STRING "Google OAuth client secret for YouTube sign-in")
target_compile_definitions(plasma-touchpad-daemon PRIVATE
    PLASMA_TOUCHPAD_YOUTUBE_CLIENT_ID="${PLASMA_TOUCHPAD_YOUTUBE_CLIENT_ID}"
    PLASMA_TOUCHPAD_YOUTUBE_CLIENT_SECRET="${PLASMA_TOUCHPAD_YOUTUBE_CLIENT_SECRET}")

install(TARGETS plasma-touchpad-daemon DESTINATION ${KDE_INSTALL_LIBEXECDIR})

# systemd user service — handles startup and automatic restart on resume/crash.
configure_file(plasma-touchpad.service.in plasma-touchpad.service @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/plasma-touchpad.service"
        DESTINATION "${KDE_INSTALL_SYSTEMDUSERUNITDIR}")
