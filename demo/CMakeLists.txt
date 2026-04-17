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
