# VIDEO ACCELERATION COMPATIBILITY PACK NuGet Package

This package contains a copy of libva and its associated development headers for Windows, the vainfo utility and a D3D12-based VAAPI driver named vaon12.

## Setup instructions

### Visual Studio Project

1. Install this nuget package to your VS project
2. Follow the steps in the "Environment variables" subsection
3. Run your VS application

### Manual install
1. Copy all files under build\\native\\\<arch\>\\bin\\\> into your preferred folder.
2. Add the folder containing the files from the step above to your PATH environment variable.
3. Follow the steps in the "Environment variables" subsection
4. You should now be able to open a new powershell terminal and run vainfo to enumerate the supported entrypoints/profiles in your system.

### Environment variables

Please set the following environment variables for your current user and save the changes. Please note you have to reopen any powershell or command prompt terminals for the changes to take effect.

    LIBVA_DRIVER_NAME=vaon12
    LIBVA_DRIVERS_PATH=<Path to directory containing vaon12_drv_video.dll>

## Changelog

### Version 1.0.2

Update libva to v2.19.0 and libva-utils to v2.19.0.
Update vaon12 driver with the following fixes:
    - Add support for QVBR rate control mode
    - Performance optimizations

### Version 1.0.1

Update libva to v2.18.0 and libva-utils to v2.18.1.
Update vaon12 driver with the following fixes:
    - Fix video decode for interlaced streams with reference only textures required
    - H264/HEVC Encode - Fix PPS frequency and rate control issues
    - Fix VP9 decode corruption on some GPUs
    - Video Encode - Fix ID3D12CommandAllocator leak

### Version 1.0.0

Initial release. The following entrypoints/profiles are exposed via vaon12 when supported by the underlying GPU/driver.
Depending on your vendor hardware and driver, some of them may not be available.

    VAProfileH264ConstrainedBaseline: VAEntrypointVLD
    VAProfileH264ConstrainedBaseline: VAEntrypointEncSlice
    VAProfileH264Main               : VAEntrypointVLD
    VAProfileH264Main               : VAEntrypointEncSlice
    VAProfileH264High               : VAEntrypointVLD
    VAProfileH264High               : VAEntrypointEncSlice
    VAProfileHEVCMain               : VAEntrypointVLD
    VAProfileHEVCMain               : VAEntrypointEncSlice
    VAProfileHEVCMain10             : VAEntrypointVLD
    VAProfileHEVCMain10             : VAEntrypointEncSlice
    VAProfileVP9Profile0            : VAEntrypointVLD
    VAProfileVP9Profile2            : VAEntrypointVLD
    VAProfileAV1Profile0            : VAEntrypointVLD
    VAProfileNone                   : VAEntrypointVideoProc
