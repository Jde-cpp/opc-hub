import { computed, Service, signal } from '@angular/core';
import type Highcharts from 'highcharts/esm/highstock';

export type HC = typeof Highcharts;

//Highcharts Stock is an add-on the install never carries (#302):  its licence is a commercial one, so a site that holds one copies
//the package's esm folder under the site as addons/highcharts/esm/, and the trend imports it from there by URL on first use -
//highstock and the accessibility module, which share the highcharts.js both reach by relative path.  The package is a
//devDependency for the typings and the specs alone;  nothing of it is bundled.  Absent, the trend says so and the tab works from
//the table (web/CLAUDE.md, "Add-ons load by URL").
@Service()
export class HighchartsLoader{
	//undefined when the site has no add-on, kept for the page's life;  a rejection when it is there and failed to load - the hub
	//restarting, say - kept for nothing, so the next load tries again
	load():Promise<HC|undefined>{ return this.#loading ??= this.#import(); }
	//How the last load settled, which the trend and its toolbar both read:  unset until one has;  'absent' holds for the page's
	//life, 'failed' until a load succeeds.
	readonly state = computed( ()=>this.#state() );
	get highcharts():HC|undefined{ return this.#highcharts; }//once loaded, before the state says so
	static readonly folder = 'addons/highcharts/esm/';//under the site's base href
	//by URL from the site;  a spec's are the package's own, bundled into the test
	protected importFile( file:string ):Promise<any>{ return import(/* @vite-ignore */ this.#base+file); }

	async #import():Promise<HC|undefined>{
		try{
			//One after the other:  the accessibility module gives the range selector's buttons their keyboard handling as it runs,
			//only if the RangeSelector of highstock's stock.js is there by then, and imported together the smaller graph could run first.
			const m = await this.importFile( 'highstock.js' );
			await this.importFile( 'modules/accessibility.js' );
			this.#highcharts = <HC>m.default;
			this.#state.set( 'loaded' );
			return this.#highcharts;
		}
		catch( e ){
			if( await absent(this.#base) ){
				console.info( `The history trend is off:  Highcharts is not at ${this.#base}.`, e );
				this.#state.set( 'absent' );
				return undefined;
			}
			this.#loading = undefined;
			console.warn( `Highcharts is at ${this.#base} but did not load.`, e );
			this.#state.set( 'failed' );
			throw e;
		}
	}
	#base = new URL( HighchartsLoader.folder, document.baseURI ).href;
	#highcharts:HC|undefined;
	#loading:Promise<HC|undefined>|undefined;
	#state = signal<'loaded'|'absent'|'failed'|undefined>( undefined );
}
//A failed import says nothing of why, so ask for its first file:  a definite no - the hub's 404, or the page nginx and IIS answer
//in its place - is no add-on;  a script there, a 5xx or no answer is a load that failed.
const absent = async ( base:string ):Promise<boolean>=>{
	try{
		const r = await fetch( base+'highstock.js', {cache: 'no-store'} );
		return r.status<500 && !(r.ok && r.headers.get( 'content-type' )?.includes( 'javascript' ));
	}
	catch{
		return false;
	}
};