# Example fish launcher (~/.config/fish/functions/theft4.fish).
function theft4 --description 'GTA IV via Theft4/LibertyRecomp (native ARM64, Linux port)'
    set -l dir $HOME/Applications/theft4/bin
    # Native Wayland window (set SDL_VIDEO_DRIVER=x11 to use XWayland instead).
    set -q SDL_VIDEO_DRIVER; or set -lx SDL_VIDEO_DRIVER wayland
    # No depth-of-field blur (set THEFT4_DEPTH_OF_FIELD=1 to restore it).
    set -q THEFT4_DEPTH_OF_FIELD; or set -lx THEFT4_DEPTH_OF_FIELD 0
    env -C $dir $dir/LibertyRecomp $argv
end
