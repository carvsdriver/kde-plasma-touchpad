kcmutils_add_qml_kcm(kcm_plasma_touchpad
    SOURCES
        KCMTouchpad.cpp
)

target_link_libraries(kcm_plasma_touchpad PRIVATE
    Qt6::Core
    KF6::CoreAddons
    KF6::KCMUtilsQuick
    KF6::ConfigCore
    KF6::I18n
    plasma-touchpad-common
)

install(FILES kcm_plasma_touchpad.json
    DESTINATION ${KDE_INSTALL_DATADIR}/plasma/kcms)
