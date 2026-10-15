add_executable(modal-ports modal_ports.cpp)
target_link_libraries(modal-ports PRIVATE modal::platform modal::warnings)
