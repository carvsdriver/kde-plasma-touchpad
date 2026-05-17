kcmutils_add_qml_kcm(kcm_yoga9_onscreen_touchpad
    SOURCES
        KCMTouchpad.cpp
)

target_link_libraries(kcm_yoga9_onscreen_touchpad PRIVATE
    Qt6::Core
    Qt6::DBus
    KF6::CoreAddons
    KF6::KCMUtilsQuick
    KF6::ConfigCore
    KF6::I18n
    plasma-touchpad-common
)

install(FILES kcm_yoga9_onscreen_touchpad.json
    DESTINATION ${KDE_INSTALL_DATADIR}/plasma/kcms)
