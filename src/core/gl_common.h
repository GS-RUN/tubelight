// SPDX-License-Identifier: MIT
// Copyright (c) 2026 GS-RUN
//
// Single include for the OpenGL function loader (libepoxy) plus GLFW.
// All in-tree code must include this rather than <GL/gl.h> directly to keep
// loader handling consistent across Windows and Linux.

#pragma once

#include <epoxy/gl.h>

// GLFW must come AFTER the GL header to avoid include order issues on some
// distros (libepoxy provides GL_VERSION_4_5 symbols GLFW would otherwise re-declare).
#include <GLFW/glfw3.h>
