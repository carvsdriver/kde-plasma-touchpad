Name:           plasma-touchpad
Version:        0.1.0
Release:        1%{?dist}
Summary:        On-screen touchpad daemon for Lenovo Yoga Book 9

License:        GPL-2.0-or-later
URL:            https://github.com/carvsdriver/kde-plasma-touchpad
# GitHub tag archive.  Packit replaces this with a git archive of the commit
# being built; scripts/build-rpm.sh produces the same layout locally.
Source0:        %{url}/archive/v%{version}/kde-plasma-touchpad-%{version}.tar.gz

# Hardware-specific: the Yoga Book 9 is an x86_64-only machine.
ExclusiveArch:  x86_64

BuildRequires:  cmake >= 3.20
BuildRequires:  extra-cmake-modules
BuildRequires:  gcc-c++

BuildRequires:  qt6-qtbase-devel
BuildRequires:  qt6-qtbase-private-devel
BuildRequires:  qt6-qtdeclarative-devel

BuildRequires:  kf6-kconfig-devel
BuildRequires:  kf6-kcoreaddons-devel
BuildRequires:  kf6-kcmutils-devel
BuildRequires:  kf6-ki18n-devel
BuildRequires:  kf6-kwindowsystem-devel

BuildRequires:  layer-shell-qt-devel
BuildRequires:  wayland-devel
BuildRequires:  wayland-protocols-devel
BuildRequires:  systemd-rpm-macros

Requires:       qt6-qtbase
Requires:       qt6-qtdeclarative
Requires:       kf6-kconfig
Requires:       kf6-kwindowsystem
Requires:       layer-shell-qt
Requires:       plasma-workspace

%description
Daemon and KDE System Settings module for the Lenovo Yoga Book 9 14IAH10
on-screen touchpad (INGENIC 17EF:6161).  Displays a touchpad overlay on
the bottom eDP-2 screen and translates raw touch events into pointer
movement, scrolling, and gestures via uinput.

%prep
%autosetup -n kde-plasma-touchpad-%{version}

%build
%cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=OFF
%cmake_build

%install
%cmake_install

# Enable the user service for every user by default.
mkdir -p %{buildroot}%{_userpresetdir}
echo 'enable plasma-touchpad.service' \
    > %{buildroot}%{_userpresetdir}/90-plasma-touchpad.preset

%post
%systemd_user_post plasma-touchpad.service

%preun
%systemd_user_preun plasma-touchpad.service

%postun
%systemd_user_postun plasma-touchpad.service

%files
%license LICENSE
%doc README.md
%{_libdir}/libexec/plasma-touchpad-daemon
%{_userunitdir}/plasma-touchpad.service
%{_userpresetdir}/90-plasma-touchpad.preset
%{_libdir}/qt6/plugins/plasma/kcms/systemsettings/kcm_yoga9_onscreen_touchpad.so
%{_datadir}/plasma/kcms/kcm_yoga9_onscreen_touchpad.json
%{_datadir}/applications/kcm_yoga9_onscreen_touchpad.desktop

%changelog
* Thu May 14 2026 Dave Carey <carvsdriver@gmail.com> - 0.1.0-1
- Initial package
