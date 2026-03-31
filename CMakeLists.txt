# Copyright (C) 2025 Signal Slot Inc.
# SPDX-License-Identifier: LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

cmake_minimum_required(VERSION 3.16.0)

include(.cmake.conf)
project(QtMcp
    VERSION "${QT_REPO_MODULE_VERSION}"
    DESCRIPTION "Qt Mcp Libraries"
    HOMEPAGE_URL "https://gitlab.com/signal-slot/qtmcp.git"
    LANGUAGES CXX C
)

option(QT_MCP_CORE_VERBOSE_DIAGNOSTICS "Enable verbose core MCP diagnostics" OFF)
option(QT_MCP_STREAMABLEHTTP_VERBOSE_DIAGNOSTICS "Enable verbose Streamable HTTP MCP diagnostics" OFF)

find_package(Qt6 ${PROJECT_VERSION} CONFIG REQUIRED COMPONENTS
    BuildInternals
    Core
)
find_package(Qt6 ${PROJECT_VERSION} CONFIG OPTIONAL_COMPONENTS
    Gui
    Widgets
    Network
)

qt_internal_project_setup()

qt_build_repo()
