// The on-disk data root the apps read and write cert material under, named once for every args file - sqlite-common
// and args-common (sqlServer/mysql) both merge this in, so their importers inherit `companyDir`/`certsDir` and no
// args file spells the root literally.
//
// `$(ProgramData)` resolves on both platforms, so this needs no per-OS branch: windows takes the env var
// (C:\ProgramData), linux has none and falls through to `Process::ProgramDataFolder()` in settings.cpp's expander
// ($XDG_CONFIG_HOME, else $HOME/.config).  Keep it branch-free - these paths drifted out of sync with the code once
// already, when the linux root moved from $HOME/.Jde-Cpp to $XDG_CONFIG_HOME/Jde-Cpp and the configs stayed behind.
//
// The trailing company component matches `Process::CompanyName()` exactly ("Jde-Cpp"); linux filesystems are
// case-sensitive, so a "jde-cpp" spelling here silently anchors nothing.
//
// `instanceName` lives here for the same reason: it is the cert CN, and since `Crypto::CryptoSettings` derives the
// cert/key file stems from the CN, it is also a path component.  It must be defined at *this* level - the one file
// every args file reaches - because `args/install` merges only this one.  args-common and sqlite-common both bind
// `std.extVar("buildTarget")` and so must override with `instanceNameFor(buildTarget)`; the base value below cannot,
// because the production service starts without `-tests`/`-ctest` and `Settings::Load` only supplies the buildTarget/
// cwd/logsDir/windows ext vars under those flags (settings.cpp).  An extVar reference here would be forced the moment
// a leaf config reads `args.instanceName` and abort the load with "Undefined external variable".
{
	local paths = self,
	companyDir:: "$(ProgramData)/Jde-Cpp",
	certsDir( product ):: paths.companyDir+"/"+product+"/ssl/certs",
	//The OPC servers a gateway trusts beyond the bundled one (gateway.trustedCertDirs): third-party server certificates, copied in
	//by an operator.  Beside ssl/certs, never in it - certs is what the product issues for itself, and what enrolls with a hub.
	serversDir( product ):: paths.companyDir+"/"+product+"/ssl/servers",
	instanceName: "$(PRODUCT_NAME)",
	//`paths.instanceName`, not `self.instanceName`: helpers are called through the *import* binding, so self is this
	//object rather than the caller's merged one - which is what we want here, the bare product without the override.
	instanceNameFor( buildTarget ):: paths.instanceName+"."+buildTarget,
}
