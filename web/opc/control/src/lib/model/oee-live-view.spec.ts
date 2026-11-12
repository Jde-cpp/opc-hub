import { OeeLiveStatus } from './oee-live';
import { growOeeBands, oeeLiveRange, oeeTile, oeeTileNext, oeeTileView } from './oee-live-view';
import { toOeeTimeline } from './oee';

const t = ( h:number, m=0, s=0 )=>new Date( Date.UTC( 2026, 10, 11, h, m, s ) );
const Jam = {id: 3, name: 'Jam', category: 'Unplanned'};
const shift = { name: 'First', start: t( 6 ), end: t( 14 ) };
const figures = ( at:Date, extra:object={} )=>({ start: t( 6 ), end: at, availability: 0.8, performance: 0.9, quality: 0.95, oee: 0.684, partial: false, ...extra });
const running = ( since:Date, at:Date ):OeeLiveStatus=>({ state: 'running', since, planned: true, shift, figures: figures( at ), calendarMissing: false });
const stopped = ( since:Date, at:Date, stop:object={} ):OeeLiveStatus=>({ state: 'stopped', since, planned: true, shift, figures: figures( at ), calendarMissing: false, stop: {start: since, small: false, ...stop} });
const machine = { id: 4, name: 'Pump 1', slug: 'pump1' };

describe( 'a tile', ()=>{
	it( 'follows a sequence of statuses:  running, a small stop, uncoded once it outlasts the threshold, coded, running again', ()=>{
		let tile = oeeTile( machine );
		expect( oeeTileView( tile ) ).toMatchObject( {state: 'none', stateLabel: 'Connecting', figures: [{label: 'Availability', value: '–'}, {label: 'Performance', value: '–'}, {label: 'Quality', value: '–'}, {label: 'OEE', value: '–'}]} );
		tile = oeeTileNext( tile, {kind: 'status', status: running( t( 7, 30 ), t( 8 ) )} );
		expect( oeeTileView( tile ) ).toMatchObject( {state: 'running', stateLabel: 'Running', since: t( 7, 30 ), length: '30m', reason: undefined, badge: undefined, period: 'First', partial: false, notes: [],
			figures: [{label: 'Availability', value: '80.0%'}, {label: 'Performance', value: '90.0%'}, {label: 'Quality', value: '95.0%'}, {label: 'OEE', value: '68.4%'}]} );
		tile = oeeTileNext( tile, {kind: 'status', status: stopped( t( 8, 12 ), t( 8, 13 ), {small: true} )} );
		expect( oeeTileView( tile ) ).toMatchObject( {state: 'stopped', stateLabel: 'Stopped', since: t( 8, 12 ), length: '1m', badge: 'Small stop'} );
		tile = oeeTileNext( tile, {kind: 'status', status: stopped( t( 8, 12 ), t( 8, 18 ) )} );
		expect( oeeTileView( tile ) ).toMatchObject( {badge: 'Uncoded', reason: undefined, length: '6m'} );
		tile = oeeTileNext( tile, {kind: 'status', status: stopped( t( 8, 12 ), t( 8, 20 ), {reason: Jam} )} );
		expect( oeeTileView( tile ) ).toMatchObject( {badge: undefined, reason: Jam} );
		tile = oeeTileNext( tile, {kind: 'status', status: running( t( 8, 40 ), t( 8, 40 ) )} );
		expect( oeeTileView( tile ) ).toMatchObject( {state: 'running', since: t( 8, 40 ), length: undefined, reason: undefined, badge: undefined} );
	} );
	it( 'says a break, a missing calendar and the time between shifts', ()=>{
		const live = ( status:OeeLiveStatus )=>oeeTileView( oeeTileNext( oeeTile( machine ), {kind: 'status', status} ) );
		expect( live( {...running( t( 7 ), t( 10, 5 ) ), planned: false} ).notes ).toEqual( ['Outside planned time.'] );
		const utc = live( {...running( t( 7 ), t( 10, 5 ) ), shift: undefined, calendarMissing: true, figures: figures( t( 10, 5 ), {partial: true} )} );
		expect( [utc.period, utc.partial, utc.notes] ).toEqual( ['UTC day', true, ["Its calendar isn't found, so the figures are the UTC day's."]] );
		const between = live( {state: 'stopped', since: t( 14 ), planned: false, calendarMissing: false, stop: {start: t( 14 ), small: false}} );
		expect( [between.period, between.length, between.notes, between.figures.map( f=>f.value )] ).toEqual( [undefined, undefined, ['Between shifts.'], ['–', '–', '–', '–']] );
	} );
	it( 'keeps what the gateway last said across a drop, marked, and is live again with the next status', ()=>{
		let tile = oeeTileNext( oeeTile( machine ), {kind: 'status', status: running( t( 7 ), t( 8 ) )} );
		tile = oeeTileNext( tile, {kind: 'dropped'} );
		expect( [tile.link, oeeTileView( tile ).state, oeeTileView( tile ).notes] ).toEqual( ['dropped', 'running', ['The connection to the gateway was lost:  this is the last it said.']] );
		tile = oeeTileNext( tile, {kind: 'status', status: running( t( 7 ), t( 8, 1 ) )} );
		expect( [tile.link, oeeTileView( tile ).notes] ).toEqual( ['live', []] );
		expect( oeeTileView( oeeTileNext( oeeTile( machine ), {kind: 'dropped'} ) ).stateLabel ).toBe( 'Disconnected' );
	} );
	it( 'says why its subscription ended, and takes nothing after', ()=>{
		let tile = oeeTileNext( oeeTile( machine ), {kind: 'status', status: running( t( 7 ), t( 8 ) )} );
		tile = oeeTileNext( tile, {kind: 'ended', message: "Machine 'Pump 1' was deleted, which ends the subscription."} );
		expect( oeeTileView( tile ) ).toMatchObject( {state: 'none', stateLabel: 'Ended', notes: ["Machine 'Pump 1' was deleted, which ends the subscription."]} );
		expect( oeeTileView( tile ).since ).toBeUndefined();
		expect( oeeTileNext( tile, {kind: 'status', status: running( t( 7 ), t( 8, 1 ) )} ) ).toBe( tile );
		expect( oeeTileNext( tile, {kind: 'dropped'} ) ).toBe( tile );
	} );
} );

describe( 'the live bands', ()=>{
	const iso = ( d:Date )=>d.toISOString();
	const read = ()=>toOeeTimeline( [{planned: [], segments: [
		{start: iso( t( 6 ) ), end: iso( t( 8, 12 ) ), state: 'running', small: false, uncoded: false},
		{start: iso( t( 8, 12 ) ), end: iso( t( 8, 40 ) ), state: 'stopped', stop: 7, reason: Jam, small: false, uncoded: false},
		{start: iso( t( 8, 40 ) ), end: iso( t( 9 ) ), state: 'running', small: false, uncoded: false}]}] ).segments;
	const spans = ( segments:{start:Date; end:Date; state:string}[] )=>segments.map( s=>[s.state, s.start.toISOString().slice( 11, 19 ), s.end.toISOString().slice( 11, 19 )] );

	it( "grows the last band to the engine's time with each status of its state, never back", ()=>{
		let bands = growOeeBands( read(), running( t( 8, 40 ), t( 9, 0, 5 ) ), t( 12 ) );
		expect( spans( bands ) ).toEqual( [['running', '06:00:00', '08:12:00'], ['stopped', '08:12:00', '08:40:00'], ['running', '08:40:00', '09:00:05']] );
		expect( bands.at( -1 )!.endText ).toBe( iso( t( 9, 0, 5 ) ) );
		bands = growOeeBands( bands, running( t( 8, 40 ), t( 9, 0, 6 ) ), t( 12 ) );
		bands = growOeeBands( bands, running( t( 8, 40 ), t( 8, 59 ) ), t( 12 ) );//the engine a moment behind the read
		expect( spans( bands ).at( -1 ) ).toEqual( ['running', '08:40:00', '09:00:06'] );
		expect( bands ).toHaveLength( 3 );
	} );
	it( 'opens a new band where the state began, closing the one before there', ()=>{
		let bands = growOeeBands( read(), running( t( 8, 40 ), t( 9, 5 ) ), t( 12 ) );
		bands = growOeeBands( bands, stopped( t( 9, 4 ), t( 9, 5 ), {small: true} ), t( 12 ) );
		expect( spans( bands ).slice( -2 ) ).toEqual( [['running', '08:40:00', '09:04:00'], ['stopped', '09:04:00', '09:05:00']] );
		expect( bands.at( -1 ) ).toMatchObject( {small: true, uncoded: false} );
		expect( [bands.at( -1 )!.reason, bands.at( -1 )!.stop] ).toEqual( [undefined, undefined] );
		bands = growOeeBands( bands, running( t( 9, 6 ), t( 9, 6 ) ), t( 12 ) );
		expect( spans( bands ).slice( -2 ) ).toEqual( [['stopped', '09:04:00', '09:06:00'], ['running', '09:06:00', '09:06:00']] );
	} );
	it( "takes a growing stop's reason and small as the status has them", ()=>{
		let bands = growOeeBands( read(), stopped( t( 9 ), t( 9, 1 ), {small: true} ), t( 12 ) );
		expect( bands.at( -1 ) ).toMatchObject( {small: true, uncoded: false} );
		bands = growOeeBands( bands, stopped( t( 9 ), t( 9, 6 ) ), t( 12 ) );
		expect( bands.at( -1 ) ).toMatchObject( {small: false, uncoded: true, reason: undefined} );
		bands = growOeeBands( bands, stopped( t( 9 ), t( 9, 7 ), {reason: Jam} ), t( 12 ) );
		expect( bands.at( -1 ) ).toMatchObject( {uncoded: false, reason: Jam} );
		expect( spans( bands ).at( -1 ) ).toEqual( ['stopped', '09:00:00', '09:07:00'] );
	} );
	it( "leaves a piece a coding cut its own reason, and its stop's id", ()=>{
		const pieces = toOeeTimeline( [{planned: [], segments: [
			{start: iso( t( 8 ) ), end: iso( t( 8, 10 ) ), state: 'stopped', stop: 7, small: false, uncoded: true},
			{start: iso( t( 8, 10 ) ), end: iso( t( 8, 20 ) ), state: 'stopped', stop: 7, reason: Jam, small: false, uncoded: false}]}] ).segments;
		const bands = growOeeBands( pieces, stopped( t( 8 ), t( 8, 25 ) ), t( 12 ) );
		expect( spans( bands ) ).toEqual( [['stopped', '08:00:00', '08:10:00'], ['stopped', '08:10:00', '08:25:00']] );
		expect( bands.at( -1 ) ).toMatchObject( {reason: Jam, uncoded: false, stop: 7} );
	} );
	it( 'starts a row with nothing read at the state, no earlier than the chart begins, and at now without figures', ()=>{
		expect( spans( growOeeBands( [], running( t( 5 ), t( 7 ) ), t( 12 ), t( 6 ) ) ) ).toEqual( [['running', '06:00:00', '07:00:00']] );
		expect( spans( growOeeBands( [], {state: 'unknown', planned: true, calendarMissing: false}, t( 12 ), t( 6 ) ) ) ).toEqual( [['unknown', '12:00:00', '12:00:00']] );
		expect( spans( growOeeBands( read(), {state: 'unknown', planned: true, calendarMissing: false}, t( 12 ) ) ).at( -1 ) ).toEqual( ['unknown', '09:00:00', '12:00:00'] );
	} );
	it( 'keeps one band where the state it was in is said to begin again', ()=>{
		let bands = growOeeBands( read(), {state: 'unknown', planned: true, calendarMissing: false, since: t( 9, 10 )}, t( 9, 11 ) );
		bands = growOeeBands( bands, running( t( 9, 5 ), t( 9, 12 ) ), t( 12 ) );//the unknown time taken back
		expect( spans( bands ) ).toEqual( [['running', '06:00:00', '08:12:00'], ['stopped', '08:12:00', '08:40:00'], ['running', '08:40:00', '09:12:00']] );
	} );
	it( 'cuts away what was drawn past a state that began before it', ()=>{
		const bands = growOeeBands( read(), stopped( t( 8, 50 ), t( 9, 1 ) ), t( 12 ) );
		expect( spans( bands ).slice( -2 ) ).toEqual( [['running', '08:40:00', '08:50:00'], ['stopped', '08:50:00', '09:01:00']] );
	} );
	it( "covers the calendar's shift, or without one the UTC day of the figures, and nothing between shifts", ()=>{
		expect( oeeLiveRange( running( t( 7 ), t( 8 ) ) ) ).toEqual( {start: t( 6 ), end: t( 14 )} );
		expect( oeeLiveRange( {...running( t( 7 ), t( 8 ) ), shift: undefined, figures: figures( t( 8 ), {start: t( 0 )} )} ) ).toEqual( {start: t( 0 ), end: t( 24 )} );
		expect( oeeLiveRange( {state: 'running', planned: false, calendarMissing: false} ) ).toBeUndefined();
	} );
} );