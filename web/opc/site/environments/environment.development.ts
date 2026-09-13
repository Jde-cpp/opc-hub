//swapped in for environment.ts by the build's `development` configuration (angular.json fileReplacements), so
//`ng serve` and `ng build --configuration development` get these values and a production build never sees them.
//Keep the key set identical to environment.ts:  EnvironmentService.get() looks keys up by name, so one spelled
//only here reads back undefined in production instead of failing the build.
//Dev-only overrides belong here - e.g. googleCredential, a signed-in id token that lets LoginPage skip
//the interactive Google prompt (Google refuses that prompt in a CDP-driven browser).
import { ETransport } from 'jde-framework';

//httpTransport picks ws/http (Unsecure), wss/https (Secure), or ws/http with an https escape hatch for the calls that ask
//for it (Hybrid).  AppService reads it by name, so the key has to exist in BOTH files or production reads back undefined.
//setup.sh feeds the stamped version in through the builder's `define`, which replaces the identifier with a string literal
//under serve, build and test alike;  typeof keeps a build without one (a bare ng new) from throwing.
declare const JDE_VERSION:string;
export const environment = {
	defaultNS: 0,
	httpTransport: ETransport.Unsecure,
	applicationServer: {port:1967, host:"localhost"},//a loopback host is rewritten to the page's (resolveInstanceHost) - localhost under ng serve, 127.0.0.1 when browsed so.
	version: typeof JDE_VERSION=='string' ? JDE_VERSION : 'unversioned',
	production: false
};
