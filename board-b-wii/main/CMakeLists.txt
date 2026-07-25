# NOTE: bt_reference/*.c are included for build purposes since
# bt_device_role.c depends on zephyr_hci_defs.h / zephyr_types.h from that
# directory, but hci.c/l2cap.c/sdp.c/host.c/wii.c themselves implement
# BlueRetro's HOST-role logic and are not called from anywhere in
# bt_device_role — see docs/STATUS.md. If you later find you don't need
# them at all (i.e. you're not cross-compiling against their functions),
# trim this list down to just the two header-only files plus your own code.
idf_component_register(
    SRCS
        "main.c"
        "uart_bridge.c"
        "bt_device_role/bt_device_role.c"
        "bt_device_role/bt_vhci_transport.c"
        "bt_device_role/bt_l2cap_device_role.c"
        "bt_device_role/bt_sdp_device_role.c"
        "bt_device_role/bt_bdaddr_setup.c"
        "wiimote/wm_reports.c"
        "wiimote/wm_crypto.c"
        "wiimote/wm_eeprom.c"
    INCLUDE_DIRS
        "."
        "bt_reference"
        "bt_device_role"
        "wiimote"
        "../../board-a-controller/include"
    REQUIRES
        bt
)
