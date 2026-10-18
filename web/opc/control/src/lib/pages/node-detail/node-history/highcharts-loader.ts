import { Injectable } from '@angular/core';
import type Highcharts from 'highcharts/esm/highstock';

export type HC = typeof Highcharts;

//Highcharts Stock is an add-on the install never carries (#302):  its licence is a commercial one, so a site that holds one copies
//the package's esm folder under the site as addons/highcharts/esm/, and the trend imports it from there by URL on first use -
//highstock and the accessibility module, which share the highcharts.js both reach by relative path.  The package is a
//devDependency for the typings and hist-trend.spec alone;  nothing of it is bundled.  Absent, the trend says so and the tab
//works from the table (web/CLAUDE.md, "Add-ons load by URL").
@Injectable({ providedIn: 'root' })
export class HighchartsLoader{
	load():Promise<HC|undefined>{ return this.#loading ??= this.#import(); }
	static readonly folder = 'addons/highcharts/esm/';//under the site's base href
	async #import():Promise<HC|undefined>{
		const base = new URL( HighchartsLoader.folder, document.baseURI ).href;
		try{
			const [m] = await Promise.all( [import(/* @vite-ignore */ base+'highstock.js'), import(/* @vite-ignore */ base+'modules/accessibility.js')] );
			return <HC>m.default;
		}
		catch( e ){
			console.info( `The history trend is off:  Highcharts is not at ${base}.`, e );
			return undefined;
		}
	}
	#loading:Promise<HC|undefined>|undefined;
}