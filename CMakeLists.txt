cmake_minimum_required(VERSION 3.20)
project(plasma-touchpad VERSION 0.1.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(ECM 6.0 REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(FeatureSummary)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick DBus)
find_package(KF6 REQUIRED COMPONENTS
    Config       # provides KF6::ConfigCore
    CoreAddons
    I18n
    KCMUtils
    WindowSystem
)
find_package(LayerShellQt REQUIRED)

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)

add_subdirectory(common)
add_subdirectory(daemon)
add_subdirectory(kcm)
