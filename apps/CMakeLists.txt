add_executable(modal-ports modal_ports.cpp)
target_link_libraries(modal-ports PRIVATE modal::platform modal::warnings)

add_executable(modal-play modal_play.cpp)
target_link_libraries(modal-play PRIVATE modal::core modal::platform modal::warnings)

add_executable(modal-loopback modal_loopback.cpp)
target_link_libraries(modal-loopback PRIVATE modal::core modal::platform modal::warnings)

add_executable(modal-record modal_record.cpp)
target_link_libraries(modal-record PRIVATE modal::core modal::platform modal::warnings)
