set(COMPONENT_REQUIRES )
set(COMPONENT_PRIV_REQUIRES )

set(COMPONENT_EMBED_FILES "web/favicon.ico" "web/index.html")

set(COMPONENT_SRCS "main.cpp"		
		"wifi_ap_scan.cpp"
		"wifi_beacon.cpp"
		"wifi_sniff.cpp"
		"wifi_route.cpp"
		"web_host.cpp"
		"init.cpp")

set(COMPONENT_ADD_INCLUDEDIRS "include")

register_component()
