// Soak overlay for the PLC emulator (soak.sh --emulator):  the production config with a distinct login-cert common name.
// The soak client (Jde.Opc.Soak) and the emulator (Jde.Opc.PlcEmulator) carry no version-resource ProductName on Windows
// - the soak client's own config says so - so both fall back to the company name 'Jde-cpp' for their login CN and would
// enroll under the same users.slug;  the second to log in is refused 401 on the unique index.  A fixed CN here (under
// ssl.certificate, where CryptoSettings reads it) makes the emulator its own AppServer user.  The block-level productName
// still puts the cert under PlcEmulator/ssl, which the soak AppServer and OpcServer already trust.
(import '../../../OpcServer/emulator/config/Opc.PlcEmulator.jsonnet') + {
	http+: { ssl+: { certificate: { commonName: "PlcEmulator.soak.webServer" } } }
}