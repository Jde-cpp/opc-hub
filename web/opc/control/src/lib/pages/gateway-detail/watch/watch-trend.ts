import { Component, computed, inject, input, model } from '@angular/core';
import { MatButtonToggleModule } from '@angular/material/button-toggle';
import { MatIconModule } from '@angular/material/icon';
import { MatToolbarModule } from '@angular/material/toolbar';
import { MatTooltipModule } from '@angular/material/tooltip';
import { HistValue } from '../../../model/hist';
import { PushBuffer } from '../../../model/push-buffer';
import { CnnctnSlug } from '../../../model/server-cnnctn';
import { WatchRow } from '../../../model/watch';
import { NodeSearchProvider } from '../../../services/node-search-provider';
import { HighchartsLoader } from '../../node-detail/node-history/highcharts-loader';
import { HistTrend } from '../../node-detail/node-history/hist-trend';
import { TrendSeries } from '../../node-detail/node-history/hist-trend-data';

//The Trend tab of the watch list (clients spec *Live trend*, #420):  the ticked rows' pushes over the buffer's window, drawn with the
//History tab's chart - the same lines, range buttons and navigator - with nothing earlier to load:  the buffer is all there is.
//The page owns the buffer, which fills whether or not this tab is open, and the window, kept between visits.
@Component({
	selector: 'watch-trend',
	templateUrl: './watch-trend.html',
	styleUrl: './watch-trend.scss',
	imports: [MatButtonToggleModule, MatIconModule, MatToolbarModule, MatTooltipModule, HistTrend]
})
export class WatchTrend{
	rows = input.required<WatchRow[]>();//the ticked rows, in palette order
	buffer = input.required<PushBuffer>();
	names = input.required<( slug:CnnctnSlug )=>string>();//a connection's name by its slug
	window = model.required<number>();//minutes
	stepped = model( true );//the page's, kept while the tabs switch (420 #10)
	//whether the trend draws:  Highcharts is an add-on the install never carries (#302), and without it the line shape has nothing to shape
	chart = computed<boolean>( ()=>this.#highcharts.state()=='loaded' );
	//a row's line by the row's name;  where another ticked row has the name, by its path, as the breadcrumbs spell it, and by its
	//connection where those differ:  two pumps' motorRpm, or one node id on two connections.  A twin on its connection that still
	//reads alike - neither has a path, as the gateway could not place them - adds the node id (420 #11).
	series = computed<TrendSeries[]>( ()=>{
		const rows = this.rows();
		return rows.map( r=>({ key: r.trendKey, id: r.seriesId, name: this.#seriesName( r, rows.filter( x=>x!==r && x.name==r.name ) )}) );
	}, {equal: ( a, b )=>a.length==b.length && a.every( (s, i)=>s.id==b[i].id && s.name==b[i].name )} );//the page hands new rows with every push (420 #5)
	#seriesName( r:WatchRow, twins:WatchRow[] ):string{
		if( !twins.length )
			return r.name;
		const label = ( x:WatchRow )=>x.path ? NodeSearchProvider.displayPath( x.path ) : x.name;
		let name = label( r );
		if( twins.some( x=>x.opc==r.opc && label( x )==name ) )
			name = `${name} ${r.node.uaString()}`;
		return twins.some( x=>x.opc!=r.opc ) ? `${name} (${this.names()( r.opc )})` : name;
	}
	//the chart's range button for the window:  the buffer's windows are the chart's first buttons, 1m, 10m and 1h
	range = computed<number>( ()=>{ const i = PushBuffer.windows.indexOf( this.window() ); return i<0 ? 4 : i; } );
	//the ticked rows' values, again only when one of theirs changes:  every row is buffered, and a push to another moved them (420 #5)
	values = computed<HistValue[]>( ()=>{
		const buffer = this.buffer(), keys = this.#keys();
		for( const key of keys )
			buffer.version( key );
		return buffer.values( keys );
	} );
	#keys = computed<string[]>( ()=>this.rows().map( r=>r.key ), {equal: ( a, b )=>a.length==b.length && a.every( (k, i)=>k==b[i] )} );
	windows = PushBuffer.windows;
	windowText( minutes:number ):string{ return minutes<60 ? `${minutes} min` : `${minutes/60} h`; }
	static readonly description = 'Live values of the ticked watch rows over time, as their servers publish them.  A Bad reading is a gap in its line, flagged with its status;  an Uncertain one keeps its value under a triangle.  The last value of a row holds on to the newest time of any row.';
	description = WatchTrend.description;
	#highcharts = inject( HighchartsLoader );
}