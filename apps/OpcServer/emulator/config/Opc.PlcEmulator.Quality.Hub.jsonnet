// Opc.PlcEmulator.Quality.jsonnet against a Jde.Opc.Hub - the same one-key swap Opc.PlcEmulator.Hub.jsonnet makes:
// the login TLS anchor is the hub's http cert instead of the split AppServer's.
(import 'Opc.PlcEmulator.Quality.jsonnet') + {
	web+:{ client+:{ ssl+:{ caFile: "$(ProgramData)/Jde-Cpp/OpcHub/ssl/certs/OpcHub.pem" } } }
}
