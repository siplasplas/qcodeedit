add_executable(qcodeedit-demo
    main.cpp
    DemoWindow.cpp
    DemoWindow.h
)

target_link_libraries(qcodeedit-demo PRIVATE
    qcodeedit::qcodeedit
    qcodeedit::kate
    Qt6::Widgets
)

# Optional code page support: detection on open, Save, switch to UTF-8.
if(TARGET qcodeedit::encoding)
    target_link_libraries(qcodeedit-demo PRIVATE qcodeedit::encoding)
    target_compile_definitions(qcodeedit-demo PRIVATE QCE_DEMO_HAVE_ENCODING)
endif()

# Optional "Download Kate syntax & themes" menu entry.
if(TARGET qcodeedit::katedata)
    target_link_libraries(qcodeedit-demo PRIVATE qcodeedit::katedata)
    target_compile_definitions(qcodeedit-demo PRIVATE QCE_DEMO_HAVE_KATEDATA)
endif()
