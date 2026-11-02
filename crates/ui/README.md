# Shared egui helpers

`eroverlay-ui` is statically linked into the plugins. It shares font selection,
bounded mesh submission and POD input conversion; it adds no service DLL and
shares no egui context across a DLL boundary. Only Ubuntu Light is embedded.
System/custom fonts remain available for current-language game text.
