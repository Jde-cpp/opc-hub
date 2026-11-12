import { toOeeLiveStatus } from './oee-live';

const t = ( h:number, m=0, s=0 )=>new Date( Date.UTC( 2026, 10, 11, h, m, s ) );
const Jam = {id: 3, name: 'Jam', category: 'Unplanned'};

describe( 'toOeeLiveStatus', ()=>{
	it( 'reads the state, the stop with its reason, the shift and the figures off the message', ()=>{
		const s = toOeeLiveStatus( <any>{ state: 2, since: t( 8, 12 ), planned: true, calendarMissing: false, stop: {start: t( 8, 12 ), reason: Jam, small: false, code: 12},
			shift: {name: 'First', start: t( 6 ), end: t( 14 )}, figures: {start: t( 6 ), end: t( 8, 20 ), availability: 0.8, oee: 0.7, partial: true, measured: 1}, lastCount: t( 8, 11 )} );
		expect( s ).toEqual( { state: 'stopped', since: t( 8, 12 ), planned: true, calendarMissing: false, stop: {start: t( 8, 12 ), reason: Jam, small: false, code: '12'},
			shift: {name: 'First', start: t( 6 ), end: t( 14 )}, figures: {start: t( 6 ), end: t( 8, 20 ), availability: 0.8, performance: undefined, quality: undefined, oee: 0.7, partial: true}, lastCount: t( 8, 11 )} );
	} );
	it( 'takes a machine no engine runs, between shifts, as unknown with nothing else', ()=>{
		expect( toOeeLiveStatus( <any>{state: 0, planned: false, calendarMissing: false} ) ).toEqual( {state: 'unknown', since: undefined, planned: false, stop: undefined, shift: undefined, figures: undefined, lastCount: undefined, calendarMissing: false} );
		expect( toOeeLiveStatus( undefined ).state ).toBe( 'unknown' );
		expect( toOeeLiveStatus( <any>{state: 1, planned: true, shift: {name: '', start: t( 6 ), end: t( 14 )}} ).shift ).toEqual( {start: t( 6 ), end: t( 14 ), name: undefined} );
	} );
} );