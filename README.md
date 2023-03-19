# Pleccer

**A slicer focused on supporting multicolor 3D printing based on SuperSlicer**

!!! ### **Please do not clone and build the source code yet, the source is not up to date. See releases for prerelease versions, for now this is the only option to test/try** ###

### **Source will be released in April 2023 together with the stable version** ###

Pleccer incorporates features to make it easier to make a colored 3d model and slice it for a multicolor mixing or non-mixing 3D printer. It is part of the PLEC (purgeless extruding colorer) project; which develops the needs to upgrade your/any printer to a mixing colorr 3D printer. You can find out more about PLEC on the [community site](https://plec.community).

Prebuilt Windows, Linux and macOS 64-bit releases are available through the [git releases page](https://github.com/rvmn/Pleccer/releases).  
Nightly builds are available through the [git actions page](https://github.com/rvmn/Pleccer/actions). Click on the build for your platform and then on the archive name (`nightly_win64` or `SuperSlicer-gtk2.AppImage` or `nightly_macos.dmg`) in the "Artifacts" section in the top right corner.

Pleccer takes 3D models (STL, OBJ, AMF) and converts them into G-code
instructions for FFF printers or PNG layers for mSLA 3D printers. It's compatible with any modern printer based on the RepRap toolchain which is running a firmware based on Marlin, Prusa, Klipper, etc.

Pleccer is based on [SuperSlicer](https://github.com/supermerill/SuperSlicer) by supermerill.
SuperSlicer is based on [PrusaSlicer](https://github.com/prusa3d/PrusaSlicer) by Prusa Research.
PrusaSlicer is based on [Slic3r](https://github.com/Slic3r/Slic3r) by Alessandro Ranellucci and the RepRap community.

See the [wiki](https://github.com/rvmn/Pleccer/wiki) and
the [documentation directory](doc/) for information about compilation.

### What are Pleccer main features?

* Improved color picker (thanks to [colorgrab](https://github.com/nielssp/colorgrab))
* Mixing color calibration setting (requires a diy/professional color measurer)
* Mixing color g-code generation based on calibrated data and the color you pick
* Enhanced smart retraction and gradient change calibration and automatic model updating for purge-free printing (no prime tower/purge)
* Includes Arc overhang infill ["Multiplex"-version](https://github.com/stmcculloch/arc-overhang/issues/10) for almost-support-free printing
* Includes Lightning infill from [PrusaSlicer/Cura](https://blog.prusa3d.com/prusaslicer-2-5-is-here-new-perimeter-generator-step-file-support-lightning-infill-and-more_70562/)
* Includes Arachne perimeter generator for adaptive perimeter widths
* It has also all the current SuperSlicer features.

### Complete changelog [here](https://github.com/rvmn/Pleccer/wiki)

See the wiki for examples.

### What are Pleccer / SuperSlicer / PrusaSlicer / Slic3r's main features?

Key features are:

* **Multi-platform** (Linux/Mac/Win) and packaged as standalone-app with no dependencies required
* Complete **command-line interface** to use it without GUI
* Multi-material **(multiple extruders)** object printing
* Multiple G-code flavors supported (RepRap, Makerbot, Mach3, Machinekit, etc.)
* Ability to plate **multiple objects having distinct print settings**
* **Multithread** processing
* **STL auto-repair** (tolerance for broken models)
* Wide automated unit testing

Other major features are:

* Combine infill every 'n' perimeters layer & varying density to speed up printing
* **3D preview** (including multi-material files)
* **Multiple layer heights** in a single print
* **Spiral vase** mode for bumpless vases
* Fine adjustment of speed, acceleration, and extrusion width
* Several infill patterns including honeycomb, spirals, Hilbert curves, gyroid
* Support material, raft, brim, skirt
* **Standby temperature** and automatic wiping for multi-extruder printing
* [Customizable **G-code macros**](https://github.com/prusa3d/PrusaSlicer/wiki/Slic3r-Prusa-Edition-Macro-Language) and output filename with variable placeholders
* Support for **post-processing scripts**
* **Cooling logic** controlling fan speed and dynamic print speed

[Command-Line Interface](https://github.com/prusa3d/PrusaSlicer/wiki/Command-Line-Interface) wiki page.

## Development

### What language is it written in?

Almost everything is written in C++.

The slicing core is the `libslic3r` library, which can be built and used in a standalone way.
The command-line interface is a thin wrapper over `libslic3r`.
You can download a precompiled package from the release page - it will run without the need for any dependency.

### How to compile

If you want to compile the source yourself, follow the instructions on one of
these documentation pages:
* [Linux](doc/How%20to%20build%20-%20Linux%20et%20al.md)
* [macOS](doc/How%20to%20build%20-%20Mac%20OS.md)
* [Windows](doc/How%20to%20build%20-%20Windows.md)

You can also look at the workflow YAML-files for [git actions](https://github.com/supermerill/Slic3r/tree/master/.github/workflows), as they describe how to build from source from a "virgin" dev computer.

### Can I help?

Sure! You can do the following to find things that are available to help with:
* Add an issue to the GitHub tracker **if it isn't already present**.

Before sending patches and pull requests contact me (preferably through opening a GitHub issue or commenting on an existing, related, issue) to discuss your proposed
changes. This way we can ensure that nobody wastes their time and no conflicts arise in development.

## License and attribution
Pleccer is licensed under the _GNU Affero General Public License, version 3_.
Pleccer is based on SuperSlicer by supermerill.

SuperSlicer is licensed under the _GNU Affero General Public License, version 3_.
SuperSlicer is based on PrusaSlicer by PrusaResearch.

PrusaSlicer is licensed under the _GNU Affero General Public License, version 3_.
PrusaSlicer is owned by Prusa Research.
PrusaSlicer is originally based on Slic3r by Alessandro Ranellucci.

Slic3r is licensed under the _GNU Affero General Public License, version 3_.
Slic3r was created by Alessandro Ranellucci with the help of many other contributors.

The _GNU Affero General Public License, version 3_ ensures that if you **use** any part of this software in any way (even behind a web server), your software must be released under the same license.
