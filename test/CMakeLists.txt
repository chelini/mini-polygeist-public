configure_lit_site_cfg(
        ${CMAKE_CURRENT_SOURCE_DIR}/lit.site.cfg.py.in
        ${CMAKE_CURRENT_BINARY_DIR}/lit.site.cfg.py
        MAIN_CONFIG
        ${CMAKE_CURRENT_SOURCE_DIR}/lit.cfg.py
)

set(MINIPOLYGEIST_OPT_TEST_DEPENDS
        FileCheck count not
        mini-polygeist
        )

add_lit_testsuite(check-mini-polygeist "Running the mini-polygeist regression tests"
        ${CMAKE_CURRENT_BINARY_DIR}
        DEPENDS ${MINIPOLYGEIST_OPT_TEST_DEPENDS}
        )
set_target_properties(check-mini-polygeist PROPERTIES FOLDER "test")

add_lit_testsuites(MINIPOLYGEIST_OPT ${CMAKE_CURRENT_SOURCE_DIR} DEPENDS ${MINIPOLYGEIST_OPT_TEST_DEPENDS})
