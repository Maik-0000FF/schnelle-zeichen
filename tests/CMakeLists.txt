# SPDX-FileCopyrightText: 2026 Maik-0000FF
# SPDX-License-Identifier: GPL-3.0-or-later

# Engine behavior tests: the ported gesture semantics driven through fake
# ports (clock, sink, overlay), no hardware and no event loop involved.
add_executable(engine_test engine_test.cpp)
target_link_libraries(engine_test PRIVATE schnelle_zeichen_engine)
add_test(NAME engine_test COMMAND engine_test)
