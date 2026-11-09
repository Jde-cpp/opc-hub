import { Signal, InjectionToken } from '@angular/core';

//Server-side persistence for ProfileStore, provided under the 'IProfileService' token (implemented in jde-framework over AppService).
export interface IProfileService{
	userKey: Signal<string|undefined>; //stable id of the logged-in user; undefined ⇒ not logged in ⇒ ProfileStore stays on localStorage
	load( key:string ):Promise<string|null>; //raw JSON string; null ⇒ no server row
	loadRows( prefix:string ):Promise<Map<string,string>>; //every row whose key starts with `prefix`, its raw JSON by key
	save( key:string, value:string|null ):Promise<void>; //null deletes the row
}
//angular-review3 C13: a typed token in place of the string one - a typo now fails the build instead of resolving to nothing at runtime, and inject() can take it.
export const IPROFILE_SERVICE = new InjectionToken<IProfileService>( 'IProfileService' );