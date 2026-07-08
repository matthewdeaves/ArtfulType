/*
 * host_mac_types.h -- Minimal Mac type shims for the HOST test build.
 *
 * The pure markdown core (mdcore) is deliberately Toolbox-free: it works on
 * plain char buffers and its own MdSpan/MdRun/MdLinkTable structs, so it needs
 * NONE of these on either target. This header exists only so that host tests
 * can speak the same vocabulary as the Mac side (Boolean, and length-prefixed
 * Pascal strings for the link table) without dragging in the real Toolbox.
 *
 * Included ONLY when AT_HOST_TEST is defined; the Mac build uses the real
 * Toolbox headers. Pattern borrowed from the author's BomberTalk project.
 */
#ifndef HOST_MAC_TYPES_H
#define HOST_MAC_TYPES_H

#ifndef AT_HOST_TEST
#error "host_mac_types.h is for the host test build only (define AT_HOST_TEST)"
#endif

typedef unsigned char Boolean;
#ifndef true
#define true  1
#define false 0
#endif

/* Pascal string: byte 0 is the length, bytes 1..255 the characters. */
typedef unsigned char Str255[256];

#endif /* HOST_MAC_TYPES_H */
