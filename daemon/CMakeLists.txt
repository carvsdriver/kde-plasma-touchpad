qt_add_executable(plasma-touchpad-daemon
    main.cpp
    DockWatcher.cpp
    TouchInputHandler.cpp
    TouchpadOverlay.cpp
    WindowMover.cpp
    PanelMover.cpp
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
    KF6::ConfigCore
    KF6::WindowSystem
    LayerShellQt::Interface
    plasma-touchpad-common
)

target_include_directories(plasma-touchpad-daemon PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

install(TARGETS plasma-touchpad-daemon DESTINATION ${KDE_INSTALL_LIBEXECDIR})
install(FILES plasma-touchpad.desktop DESTINATION ${KDE_INSTALL_AUTOSTARTDIR})

# systemd user service — handles startup and automatic restart on resume/crash.
configure_file(plasma-touchpad.service.in plasma-touchpad.service @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/plasma-touchpad.service"
        DESTINATION "${KDE_INSTALL_SYSTEMDUSERUNITDIR}")
