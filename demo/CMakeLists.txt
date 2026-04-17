add_executable(qcodeedit-demo
    main.cpp
    DemoWindow.cpp
    DemoWindow.h
    KateXmlReader.cpp
    KateXmlReader.h
)

target_link_libraries(qcodeedit-demo PRIVATE
    qcodeedit::qcodeedit
    Qt6::Widgets
)
