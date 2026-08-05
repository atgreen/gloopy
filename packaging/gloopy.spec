# Traditional RPM spec for gloopy (replaces CPack). It PACKAGES a pre-built tree — the
# CI builds gloopy + the bundled Surge XT LV2 first — by staging only the `gloopy` CMake
# install component, so the vendored deps (JUCE headers/juceaide, sfizz, grpc CMake configs)
# never land in the package. Ships an FHS layout: the binary in bin, data in share/gloopy,
# the plugin in lib/gloopy/plugins.
#
#   rpmbuild -bb packaging/gloopy.spec \
#     --define "_gloopy_srcdir $PWD" --define "_gloopy_builddir $PWD/build" \
#     --define "_gloopy_version 0.1.0"

%global gloopy_builddir %{?_gloopy_builddir}%{!?_gloopy_builddir:%{_builddir}/build}
%global gloopy_srcdir   %{?_gloopy_srcdir}%{!?_gloopy_srcdir:%{_builddir}}
# Don't advertise the bundled Surge LV2's .so as a system-provided library.
%global __provides_exclude_from ^%{_libdir}/gloopy/.*$

Name:           gloopy
Version:        %{?_gloopy_version}%{!?_gloopy_version:0.1.0}
Release:        1%{?dist}
Summary:        A scriptable, composition-as-code DAW
# The binary embeds Surge XT (GPL-3.0), combined with gloopy's own AGPL-3.0.
License:        AGPL-3.0-only AND GPL-3.0-or-later
URL:            https://github.com/atgreen/gloopy
# Runtime library deps (freetype, X, GL, grpc, protobuf, ...) are auto-detected from the
# ELF binary and the bundled plugin's .so.
AutoReqProv:    yes

%description
Gloopy is a linear-arranger DAW driven end to end by an OSC + gRPC control API over a
diff-friendly composition-as-code text format. It hosts VST3/LV2 plugins, ships a bundled
Surge XT synth, and its projects are plain, version-controllable text.

# The software is already built; packaging only stages the `gloopy` install component.
%install
DESTDIR=%{buildroot} cmake --install "%{gloopy_builddir}" --component gloopy --prefix %{_prefix}
install -Dm0644 "%{gloopy_srcdir}/LICENSE" "%{buildroot}%{_defaultlicensedir}/%{name}/LICENSE"

# Bundled third-party license texts belong in the RPM license directory
# (/usr/share/licenses/<pkg>/, the Fedora convention) beside our own LICENSE — not under
# share/doc. The CMake install stages them under share/doc/gloopy (that is the Debian
# layout, which the .deb keeps); relocate that tree here for the RPM so `rpm -qd`/`--licensefiles`
# and users find every notice in one place.
if [ -f "%{buildroot}%{_datadir}/doc/%{name}/THIRD-PARTY-LICENSES.md" ]; then
    mv "%{buildroot}%{_datadir}/doc/%{name}/THIRD-PARTY-LICENSES.md" \
       "%{buildroot}%{_defaultlicensedir}/%{name}/"
fi
if [ -d "%{buildroot}%{_datadir}/doc/%{name}/third-party-licenses" ]; then
    mv "%{buildroot}%{_datadir}/doc/%{name}/third-party-licenses" \
       "%{buildroot}%{_defaultlicensedir}/%{name}/"
fi
rmdir "%{buildroot}%{_datadir}/doc/%{name}" 2>/dev/null || true

%files
# The whole license directory (our LICENSE + the bundled third-party texts + manifest).
%license %{_defaultlicensedir}/%{name}/
%{_bindir}/gloopy
%{_datadir}/gloopy/
%{_libdir}/gloopy/
%{_datadir}/applications/gloopy.desktop
%{_datadir}/icons/hicolor/scalable/apps/gloopy.svg
%{_datadir}/metainfo/io.github.atgreen.gloopy.metainfo.xml
%{_datadir}/emacs/site-lisp/gloopy.el

%changelog
* Wed Jul 29 2026 Anthony Green <green@moxielogic.com> - 0.1.0-1
- Traditional rpmbuild packaging (FHS layout, gloopy component only), replacing CPack.
