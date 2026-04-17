add_executable(qcodeedit-demo
    main.cpp
    DemoWindow.cpp
    DemoWindow.h
)

target_link_libraries(qcodeedit-demo PRIVATE
    qcodeedit::qcodeedit
    Qt6::Widgets
)
