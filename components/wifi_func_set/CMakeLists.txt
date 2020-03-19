set(COMPONENT_SRCS "WifiScanner.cpp"
		"WifiBeacon.cpp"
		"WifiSniffer.cpp"
)
set(COMPONENT_ADD_INCLUDEDIRS "include")

set(COMPONENT_REQUIRES mm_common)

register_component()
