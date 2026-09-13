import { ETransport } from 'jde-framework';

//httpTransport picks ws/http (Unsecure), wss/https (Secure), or ws/http with an https escape hatch for the calls that ask
//for it (Hybrid).  AppService reads it by name, so the key has to exist in BOTH files or production reads back undefined.
//setup.sh feeds the stamped version in through the builder's `define`, which replaces the identifier with a string literal
//under serve, build and test alike;  typeof keeps a build without one (a bare ng new) from throwing.
declare const JDE_VERSION:string;
export const environment = {
	defaultNS: 0,
	httpTransport: ETransport.Unsecure,
	applicationServer: {port:1967, host:""},//"": the host the page was served from (resolveInstanceHost, install-issues #2) - the installed site is browsed by whatever name reaches the hub's 1967, and that name is what it calls.
	version: typeof JDE_VERSION=='string' ? JDE_VERSION : 'unversioned',
	production: true
};