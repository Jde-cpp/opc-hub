// Opc.PlcEmulator.jsonnet against a Jde.Opc.Hub: the login TLS anchor is the hub's http cert (Opc.Hub.jsonnet's /http/app
// commonName "OpcHub") instead of the split AppServer's.  Nothing else changes - the hub's AppServer role listens on 1967.
// The hub must trust this product's dir in return: the dev args do; an installed hub's (apps/OpcHub/config/args/install)
// and OpcServer's list only each other, so the emulator's dir is added to both by hand.
(import 'Opc.PlcEmulator.jsonnet') + {
	web+:{ client+:{ ssl+:{ caFile: "$(ProgramData)/Jde-Cpp/OpcHub/ssl/certs/OpcHub.pem" } } }
}
