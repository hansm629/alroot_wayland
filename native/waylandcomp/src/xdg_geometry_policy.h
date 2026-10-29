#ifndef ALROOT_XDG_GEOMETRY_POLICY_H
#define ALROOT_XDG_GEOMETRY_POLICY_H

/* Protocol-level, GPU/desktop-vendor-independent xdg_toplevel size policy.
 * For normal windowed surfaces configure(0,0) gives the application control
 * of its buffer size (the spec's unconstrained/default recommendation).
 * Only a fullscreen, maximized or compositor-designated desktop role gets
 * the full output size. This never changes wl_output advertised geometry.
 */
static inline void
alroot_xdg_configure_dimensions(int output_width, int output_height,
                                 int fullscreen, int maximized, int desktop,
                                 int *width, int *height)
{
    const int output_sized = fullscreen || maximized || desktop;
    *width = output_sized && output_width > 0 ? output_width : 0;
    *height = output_sized && output_height > 0 ? output_height : 0;
}

#endif
